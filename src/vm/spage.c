#include "vm/spage.h"
#include <debug.h>
#include <hash.h>
#include <string.h>
#include "userprog/pagedir.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "vm/frames.h"

static bool install_page (void *upage, void *kpage, bool writable);
static bool install_not_present_page (void *upage);

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
spte_destroy(const struct hash_elem *e,
             void                   *aux UNUSED) {
    free(hash_entry(e,
                    struct spage_table_entry,
                    elem));
}

void spage_destroy(struct hash *h) {
    // TODO cleanup everything
    // especially free space on swap device
    // write to files if writeable
    struct hash_iterator iter;
    struct hash_elem *e_;
    struct spage_table_entry *e;
    hash_first(&iter, h);
    e_ = hash_next(&iter);
    while(e_ != NULL) {
      e = hash_entry(e_, struct spage_table_entry, elem);
      switch (e->backing) {
      case SWAPPED:
      case ZEROPAGE:
        // discard, nothing to do
        break;
      case FROMFILE:
        if (e->perm & FLG_W) {
          NOT_REACHED();
        } else {
          // not writeable, so nothing to write back
        }
        break;
      default:
        NOT_REACHED();
      }
    }
    // TODO MUST be _destroy to avoid memory leak
    // clear so that we can still check for leftover entries
    hash_clear(h, &spte_destroy);
}

bool
spage_valid_and_load(void *vaddr) {
    log_debug("@@@ spage_valid_and_load called (tid: %d, vaddr 0x%08x) @@@\n",
              thread_current()->tid, vaddr);
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
    switch(e->backing) {
    case SWAPPED:
        // TODO implement
        success = false;
        goto done;
        break;

    case FROMFILE:
        p = frame_get_free();
        if (p == NULL ||
            !install_page(e->vaddr,
                          p,
                          e->perm & FLG_W)) {
            success = false;
            goto done;
        }
        // page may not be fully written to
        memset(p, 0, PGSIZE);

        // TODO may reach EOF, so may be less than PGSIZE
        // but currently do not carry over the real size to read
        // maybe this is needed to check if the segment or file is malformed
        size_t bread = file_read_at(e->file, p, e->file_size, e->file_ofs);

        goto done;
        break;

    case ZEROPAGE:;
        p = frame_get_free();
        if (p == NULL ||
            !install_page(e->vaddr,
                          p,
                          e->perm & FLG_W)) {
            success = false;
            goto done;
        }
        memset(p, 0, PGSIZE);
        break;

    default:
        NOT_REACHED();
    }

done:
    log_debug("@@@ spage_valid_and_load return: %s @@@\n",
              success ? "success" : "!!PROBLEM!!");
    return success;
}

bool
spage_map_file(struct file *f,
               size_t       ofs,
               void        *uaddr,
               const bool   writable,
               size_t       size) {
    ASSERT(pg_ofs(uaddr) == 0);

    struct spage_table_entry *e = malloc(sizeof (*e));
    e->vaddr = uaddr;
    e->backing = FROMFILE;
    e->file = f;
    e->file_ofs = ofs;
    e->file_size = size;
    e->perm = writable ? FLG_R | FLG_W | FLG_X : FLG_R | FLG_X;

    // insert w/o replace
    // NULL if insert successful
    return install_not_present_page(uaddr)
                 && hash_insert(&thread_current()->sup_pagetable, &e->elem) == NULL;
}

bool
spage_map_zero(void *uaddr,
               const bool writable) {
    ASSERT(pg_ofs(uaddr) == 0);

    struct spage_table_entry *e = malloc(sizeof (*e));
    e->vaddr = uaddr;
    e->backing = ZEROPAGE;
    e->perm = writable ? FLG_R | FLG_W | FLG_X : FLG_R | FLG_X;

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
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}

static bool
install_not_present_page (void *upage) {
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page_not_present (t->pagedir, upage));
}
