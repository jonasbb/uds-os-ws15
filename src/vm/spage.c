#include "vm/spage.h"
#include <debug.h>
#include <hash.h>
#include <string.h>
#include "userprog/pagedir.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "vm/frames.h"
#include "vm/swap.h"

static bool install_page (void *upage, void *kpage, bool writable);
static bool install_not_present_page (void *upage);
static bool spage_map_file(struct file *f,
                           size_t       ofs,
                           void        *uaddr,
                           const bool   writable,
                           size_t       size,
                           bool         is_mmap);

unsigned
spte_hash(const struct hash_elem  *e,
          void                    *aux UNUSED) {
    struct spage_table_entry *spte = hash_entry(e,
                                                struct spage_table_entry,
                                                elem);
    return hash_int((uint32_t) spte->vaddr);
}

bool
spte_less(const struct hash_elem *a_,
          const struct hash_elem *b_,
          void                   *aux UNUSED) {
    struct spage_table_entry *a = hash_entry(a_,
                                             struct spage_table_entry,
                                             elem);
    struct spage_table_entry *b = hash_entry(b_,
                                             struct spage_table_entry,
                                             elem);
    return a->vaddr < b -> vaddr;
}

void
spte_destroy(struct hash_elem *e,
             void             *aux UNUSED) {
    free(hash_entry(e,
                    struct spage_table_entry,
                    elem));
}

void
spage_destroy() {
    // TODO cleanup everything
    // especially free space on swap device
    // write to files if writeable
    struct thread *t = thread_current();
    struct hash_iterator iter;
    struct hash_elem *e_;
    struct spage_table_entry *e;
    hash_first(&iter, &t->sup_pagetable);
    e_ = hash_next(&iter);
    while(e_ != NULL) {
      e = hash_entry(e_, struct spage_table_entry, elem);
      switch (e->backing) {
      case SWAPPED:
      case ZEROPAGE:
        // discard, nothing to do
        break;
      case FROMFILE:
        if (e->flags & SPTE_W && e->flags & SPTE_MMAP) {
          NOT_REACHED();
        } else {
          // not writeable, so nothing to write back
        }
        break;
      default:
        NOT_REACHED();
      }
      e_ = hash_next(&iter);
    }
    // TODO MUST be _destroy to avoid memory leak
    // clear so that we can still check for leftover entries
    hash_destroy(&t->sup_pagetable, &spte_destroy);
    memset(&t->sup_pagetable, 0, sizeof(t->sup_pagetable));
}

bool
spage_valid_and_load(void *vaddr) {
    log_debug("@@@ spage_valid_and_load called (tid: %d, vaddr 0x%08x) @@@\n",
              thread_current()->tid, (uint32_t) vaddr);
    bool success = true;
    void *p;
    struct spage_table_entry ecmp, *e;
    struct hash_elem *elem;
    struct thread *t = thread_current();
    ecmp.vaddr = vaddr;
    elem = hash_find(&t->sup_pagetable, &ecmp.elem);
    // valid if any element found
    if (elem == NULL) {
        success = false;
        goto done;
    }

    e = hash_entry(elem, struct spage_table_entry, elem);
    if (!(e->flags & SPTE_IS_VALID)) {
      PANIC("Memory handling wrong!");
    }
    p = frame_get_free();

    if (p == NULL) {
            success = false;
            goto done;
    }

    switch(e->backing) {
    case SWAPPED:

        swap_read(e->st_e, p);
        e->flags &= ~SPTE_IS_VALID;
        break;

    case FROMFILE:

        // page may not be fully written to
        memset(p, 0, PGSIZE);

        // check to read enough bytes
        size_t bread = file_read_at(e->file, p, e->file_size, e->file_ofs);
        success = bread == e->file_size;

        break;

    case ZEROPAGE:;

        memset(p, 0, PGSIZE);
        break;

    default:
        NOT_REACHED();
    }

  if (!install_page(e->vaddr, p, e->flags & SPTE_W)) {
    success = false;
    frame_remove(p);
  }

done:
    log_debug("@@@ spage_valid_and_load return: %s @@@\n",
              success ? "success" : "!!PROBLEM!!");
    return success;
}

static bool
spage_map_file(struct file *f,
               size_t       ofs,
               void        *uaddr,
               const bool   writeable,
               size_t       size,
               bool         is_mmap) {
    ASSERT(pg_ofs(uaddr) == 0);
    ASSERT(size <= PGSIZE);

    struct spage_table_entry *e = malloc(sizeof (*e));
    e->vaddr = uaddr;
    e->backing = FROMFILE;
    e->file = f;
    e->file_ofs = ofs;
    e->file_size = size;
    e->flags = SPTE_IS_VALID;
    if(writeable) {
        e->flags |= SPTE_W;
    }
    if(is_mmap) {
        e->flags |= SPTE_MMAP;
    }

    // insert w/o replace
    // NULL if insert successful
    return install_not_present_page(uaddr)
                 && hash_insert(&thread_current()->sup_pagetable, &e->elem) == NULL;
}

/* Maps up to a single page (PGSIZE bytes) of file `f` starting at position
 * `ofs` into the address space at position `uaddr`. If `writeable`
 * the page is marked writeable. `size` bytes will be read from the file,
 * the rest will be padded with 0's.
 * `uaddr` MUST point to a page, offset = 0.
 *
 * Dirty pages will be written back to the file.
 */
bool
spage_map_mmap(struct file *f,
               size_t       ofs,
               void        *uaddr,
               const bool   writable,
               size_t       size){
    return spage_map_file(f,
                          ofs,
                          uaddr,
                          writable,
                          size,
                          true);
}

/* Undoes everything done by spage_map_mmap and evict page holding resources
 */
void
spage_map_munmap(void *uaddr) {
    struct spage_table_entry ecmp, *e;
    struct hash_elem *e_;
    ecmp.vaddr = uaddr;
    e_ = hash_delete(&thread_current()->sup_pagetable, &ecmp.elem);
    if (!e_) {
        return;
    }
    e = hash_entry(e_, struct spage_table_entry, elem);
    pagedir_clear_page(thread_current()->pagedir, e->vaddr);
    free(e);
}

/* Maps up to a single page (PGSIZE bytes) of file `f` starting at position
 * `ofs` into the address space at position `uaddr`. If `writeable`
 * the page is marked writeable. `size` bytes will be read from the file,
 * the rest will be padded with 0's.
 * `uaddr` MUST point to a page, offset = 0.
 *
 * Dirty pages will NOT be written back to the file.
 */
bool
spage_map_segment(struct file *f,
                  size_t       ofs,
                  void        *uaddr,
                  const bool   writable,
                  size_t       size){
    return spage_map_file(f,
                          ofs,
                          uaddr,
                          writable,
                          size,
                          false);
}

/* Maps a single page (PGSIZE bytes) of 0's into the address space at position
 * `uaddr`. If `writable` the page will be writeable.
 * `uaddr` MUST point to a page, offset = 0.
 */
bool
spage_map_zero(void *uaddr,
               const bool writeable) {
    ASSERT(pg_ofs(uaddr) == 0);

    struct spage_table_entry *e = malloc(sizeof (*e));
    e->vaddr = uaddr;
    e->backing = ZEROPAGE;
    e->flags = SPTE_IS_VALID;
    if(writeable) {
        e->flags |= SPTE_W;
    }

    // insert w/o replace
    // NULL if insert successful
    return install_not_present_page(uaddr)
                 && hash_insert(&thread_current()->sup_pagetable, &e->elem) == NULL;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writeable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writeable));
}

static bool
install_not_present_page (void *upage) {
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page_not_present (t->pagedir, upage));
}
