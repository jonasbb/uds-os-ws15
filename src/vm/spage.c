#include "vm/spage.h"
#include <debug.h>
#include "threads/thread.h"

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
spage_init() {
}

bool
spage_valid_and_load(void *vaddr) {
    struct spage_table_entry ecmp, *e;
    struct hash_elem *elem;
    ecmp.vaddr = vaddr;
    elem = hash_find(&thread_current()->sup_pagetable, &ecmp.elem);
    // valid if any element found
    if (elem == NULL) {
        return false;
    }

    e = hash_elem(elem, struct spage_table_entry, elem);
    switch(e->backing) {
    case SWAPPED:
        // TODO implement
        break;
    case FROMFILE:
        // TODO implement
        break;
    default:
        NOT_REACHED();
    }

    return true;
}

bool
spage_map_file() {
    return true;
}
