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

enum spte_perm {
    FLG_R = 1 << 0,
    FLG_W = 1 << 1,
    FLG_X = 1 << 2
};
typedef uint8_t spte_perm;

typedef struct spage_table_entry {
    // hashable entry
    struct hash_elem elem;
    // <hash> These elements are relevant for the hash
    void *vaddr;
    // </hash>

    spte_backing backing;
    spte_perm perm;
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
            size_t file_size;
        };
    };
} spte;


unsigned spte_hash(const struct hash_elem *e,
                   void                   *aux);

bool spte_less(const struct hash_elem *a,
               const struct hash_elem *b,
               void                   *aux);

bool spage_valid_and_load(void *vaddr);
bool spage_map_file(struct file *f,
                    size_t ofs,
                    void *uaddr,
                    const bool writable,
                    size_t size);
bool spage_map_zero(void *uaddr,
                    const bool writable);
#endif
