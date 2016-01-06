#include "vm/frames.h"
#include "vm/spage.h"
#include <debug.h>
#include <round.h>
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "userprog/pagedir.h"


static uint32_t page_to_pagenum(void* page);
static void* pagenum_to_page(uint32_t pgnum);

// TODO define frame table
struct frametable {
    // Number of frametable entries in the array
    uint32_t size;
    // Counts the number of frames in use
    // must always be >= 0 and < size
    uint32_t used;
    // Counts the number of frames to store the frametable
    // must always be >= 0 and < size
    uint32_t own_used;
    // Points to the next frametable entry for inspection
    // must always be >= 0 and < size
    uint32_t evict_ptr;
    // Points to the start location of the next free frame
    // must always be >= 0 and < size
    uint32_t search_ptr;
    // Array of framtable entries
    // contains `size` many entries
    struct frametable_entry* frametable;
    // first page in consecutive memory segment
    // used for page number calculations
    void* base_addr;
    
};

struct frametable frametable;

/* Create a new frametable.
 *
 * `base_addr` is a pointer to the first page of a `size` large
 * consecutive memory region. All pages/frames in this region will
 * be used by this frametable.
 */
void
frame_init(uint32_t size,
           void* frame_base_addr) {
    // init global variable
    frametable.size = size;
    frametable.used = 0;
    frametable.evict_ptr = 0;
    frametable.search_ptr = 0;
    frametable.base_addr = frame_base_addr;
    lock_init(&vm_lock);

    // create frame table with size of number of pages
    frametable.frametable = frame_base_addr;

    // calculate memory requirements and map them
    frametable.own_used = DIV_ROUND_UP(sizeof(struct frametable_entry) * size, PGSIZE);
    frametable.used = frametable.own_used;
    uint32_t t;
    for (i = 0; i < frametable.own_used; i++) {
        frametable_entry_create(&frametable.frametable[i],
                                (void*) 0xFFFFFFFF, // FIXME not the niced but may not be NULL
                                0,
                                NULL,
                                true // never evict pages
        );
    }
}

/*
 * Insert a new frame table entry for address `frame_address`.
 * Needs the corresponding `tid` and `virt_address` to look up the
 * correct supplementary page table.
 * The `pte` is used to access the dirty/accessed bits for the frame.
 */
bool
frame_insert(void *frame_address,
             tid_t tid,
             void *virt_address,
             struct pagetable_entry* pte) {
    lock_acquire_re(&vm_lock);
    // frames MUST always be page aligned
    ASSERT(frame_address != NULL);
    ASSERT(pg_ofs(frame_address) == 0);
    // virtual addresses MUST also be page aligned
    ASSERT(pg_ofs(virt_address) == 0);

    // TODO insert entry
    uint32_t pgnum = page_to_pagenum(frame_address);
    frametable_entry_create(&frametable.frametable[pgnum],
                            pte,
                            tid,
                            virt_address,
                            false);
    lock_release_re(&vm_lock);
    return true;
}

/*
 * Removes the frame entry for physical address `frame_address` from the frame
 * tables. Access to all fields of the corresponding entry are invalid afterwards
 * until `frame_insert` is called.
 *
 * Only non-pinned entries can be removed.
 */
void
frame_remove(void *frame_address) {
    lock_acquire_re(&vm_lock);
    log_debug("--- frame_remove (used: %d, own used: %d) ---\n", frametable.used, frametable.own_used);
    // frames MUST always be page aligned
    ASSERT(frame_address != NULL);
    ASSERT(pg_ofs(frame_address) == 0);

    uint32_t pgnum = page_to_pagenum(frame_address);
    if (frametable.frametable[pgnum].pin != false) {
        PANIC("Remove of pinned frame!"); 
    }
    ASSERT(frametable.frametable[pgnum].pin == false);

    // TODO reset everything
    frametable.frametable[pgnum].pte = NULL;
    frametable.used--;
    lock_release_re(&vm_lock);
}

// remove multiple pages
// analogue to palloc_free_multiple
void
frame_remove_mult(void *frame_address,
                  size_t cnt) {
    // only one page at a time requestable
    ASSERT(cnt == 1);
    frame_remove(frame_address);
}

/*
 * Searches for a free frame and returns one if available.
 * If all frames are full, evict a frame and return pointer to evicted frame.
 */
void*
frame_get_free() {
    lock_acquire_re(&vm_lock);
    log_debug("+++ frame_get_free (used: %d, own used: %d) +++\n", frametable.used, frametable.own_used);
    // TODO
    if (frametable.used < frametable.size) {
        // some free frames left
        while(1) {
            if (frametable.frametable[frametable.search_ptr].pte == NULL) {
                // FIXME reserve entry by changing pointer
                frametable.frametable[frametable.search_ptr].pte = (void*) 0xFFFFFFFF;
                frametable.used++;
                void* tmp = pagenum_to_page(frametable.search_ptr);
                log_debug("### Free page at 0x%08x ###\n", (uint32_t) tmp);
                lock_release_re(&vm_lock);
                return tmp;
            }
            // jump to next position
            frametable.search_ptr = (frametable.search_ptr + 1) % frametable.size;
        }
    } else {
        // no free frames left
        // evict frame
        

        // TODO crash until eviction implemented
        //NOT_REACHED();
        //        && frametable.frametable[frametable.search_ptr].pin == false) {
        void *tmp = frame_evict();
        lock_release_re(&vm_lock);
        return tmp;
    }
    NOT_REACHED();
    return NULL;
}

void
frame_set_pin(void *page, bool pin) {
    lock_acquire_re(&vm_lock);
    // frames MUST always be page aligned
    ASSERT(page != NULL);
    ASSERT(pg_ofs(page) == 0);

    frametable.frametable[page_to_pagenum(page)].pin = pin;
    lock_release_re(&vm_lock);
}

/*
 * Converts a pointer to any address into a number from
 * 0 to USER_PAGES-1 (the amount of pages in the userpool)
 * This can be used to as an index to access the frames
 * within the frame table.
 */
static uint32_t
page_to_pagenum(void *page) {
    // pages are consecutive and nothing can be before the base
    ASSERT(page >= frametable.base_addr);

    // remove offset within frame
    page = pg_round_down(page);
    // address shift relative to base addr
    page -= (uintptr_t) frametable.base_addr;
    return pg_no(page);
}

static void*
pagenum_to_page(uint32_t pgnum) {
    ASSERT(pgnum < frametable.size);

    return (void*) (pg_no_to_addr(pgnum) + frametable.base_addr);
}

/* Fill a frametable_entry with data
 */
void
frametable_entry_create(struct frametable_entry* fte,
                        struct pagetable_entry* pte,
                        tid_t tid,
                        void *virt_address,
                        bool pin) {
    fte->pte = pte;
    fte->tid = tid;
    fte->virt_address = pg_no(virt_address);
    fte->pin = pin;
}


void *
frame_evict() {
    ASSERT(lock_held_by_current_thread(&vm_lock));
    uint32_t e_ptr = frametable.evict_ptr;
    while(1) {
            if (frametable.frametable[frametable.evict_ptr].pin == false &&
                frametable.frametable[frametable.evict_ptr].pte != (void*) 0xFFFFFFFF) {
                    struct pagetable_entry* old_pte = frametable.frametable[frametable.evict_ptr].pte;
                    struct thread *t = thread_from_tid(frametable.frametable[frametable.evict_ptr].tid);
                    if (old_pte->accessed) {
                        pagedir_set_accessed(t->pagedir, 
                                        pg_no_to_addr(frametable.frametable[frametable.evict_ptr].virt_address), false);
                        continue;
                    }

                    // First we MUST mark the page as not present so that no
                    // further accesses and modifications of the page content
                    // are possible
                    pagedir_set_not_present(t->pagedir,
                                    pg_no_to_addr(frametable.frametable[frametable.evict_ptr].virt_address));
                    if (old_pte->writable) {
                        struct spage_table_entry ecmp, *e;
                        struct hash_elem *elem;
                        ecmp.vaddr = pg_no_to_addr(frametable.frametable[frametable.evict_ptr].virt_address);
                        elem = hash_find(&t->sup_pagetable, &ecmp.elem);
                        if (elem == NULL) {
                            //swap
                            struct swaptable_entry * st_e = 
                                    create_swaptable_entry(pagenum_to_page(frametable.evict_ptr));
                            swap_add(st_e);
                            spage_map_swap(pg_no_to_addr(
                                        frametable.frametable[frametable.evict_ptr].virt_address), st_e, t);
                        }
                        else {
                            e = hash_entry(elem, struct spage_table_entry, elem);
                            if (e->flags & SPTE_MMAP && old_pte->dirty) {
                                //flush
                                spage_flush_mmap(e, pagenum_to_page(frametable.evict_ptr));
                                //reset dirty bit
                                pagedir_set_dirty(t->pagedir, e->vaddr, false);
                            }
                            else if (!(e->flags & SPTE_MMAP)) {
                                //file backed writable entry
                                PANIC("SWAP of page with spage entry!");
                            
                            }
                        }
                        
                    }
                    frametable.frametable[frametable.evict_ptr].pte = (void*) 0xFFFFFFFF;
                    void* tmp = pagenum_to_page(frametable.evict_ptr);
                    log_debug("### Evict page at 0x%08x ###\n", (uint32_t) tmp);
                    frametable.evict_ptr = (frametable.evict_ptr + 1) % frametable.size;
                    return tmp;
            }
                        // jump to next position
            frametable.evict_ptr = (frametable.evict_ptr + 1) % frametable.size;
            if (e_ptr == frametable.evict_ptr)
                PANIC("Nothing to evict and nothing swappable");
       }

}
