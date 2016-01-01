#ifndef __VM_SPAGE_H
#define __VM_SPAGE_H

#include <stdint.h>
#include <hash.h>
#include "devices/block.h"
#include "filesys/file.h"

enum spte_backing {
    SWAPPED  = 0,
    FROMFILE = 1,
    ZEROPAGE = 2
};
typedef uint8_t spte_backing;

typedef struct spage_table_entry {
    // hashable entry
    struct hash_elem elem;
    // <hash> These elements are relevant for the hash
    void *vaddr;
    // </hash>

    spte_backing backing;
    union {
        /* swapped */
        struct {
            // which block device is used for swapping
            struct block *swap_dev;
            // offset within the device to locate the page
            size_t swap_ofs;
        };
        /* file */
        struct {
            // file as backing store
            struct file *file;
            // offset within file
            size_t file_ofs;
        };
    };
} spte;


unsigned spte_hash(const struct hash_elem *e,
                   void                   *aux);

bool spte_less(const struct hash_elem *a,
               const struct hash_elem *b,
               void                   *aux);
#endif
