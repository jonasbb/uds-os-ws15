#ifndef __VM_SPAGE_H
#define __VM_SPAGE_H

#include <stdint.h>
#include <hash.h>
#include "devices/block.h"
#include "filesys/file.h"

#define STACK_MAX ((void *) PHYS_BASE - 512 * PGSIZE) 

enum spte_backing {
    SWAPPED  = 0,
    FROMFILE = 1,
    ZEROPAGE = 2
};
typedef uint8_t spte_backing;

enum spte_flags {
    SPTE_W    = 1 << 0,
    SPTE_MMAP = 1 << 7,
    SPTE_IS_VALID = 1 << 2
};
typedef uint8_t spte_flags;

typedef struct spage_table_entry {
    // hashable entry
    struct hash_elem elem;
    // <hash> These elements are relevant for the hash
    void *vaddr;
    // </hash>

    spte_backing backing;
    spte_flags flags;
    union {
        /* swapped */
        struct swaptable_entry *st_e;
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
void spte_destroy(struct hash_elem *e,
                  void             *aux);

void spage_destroy(void);
bool spage_valid_and_load(void *vaddr, bool pin, void *esp);
bool spage_map_mmap(struct file *f,
                    size_t       ofs,
                    void        *uaddr,
                    const bool   writeable,
                    size_t       size);
void spage_map_munmap(void *uaddr);
void spage_flush_mmap(struct spage_table_entry *e,
                      void                     *kaddr);
bool spage_map_segment(struct file *f,
                       size_t       ofs,
                       void        *uaddr,
                       const bool   writeable,
                       size_t       size);
bool spage_map_zero(void *uaddr,
                    const bool writeable);

bool install_page (void *upage, void *kpage, bool writable, bool pin);
#endif
