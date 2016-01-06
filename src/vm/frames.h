#ifndef __FRAMES_H
#define __FRAMES_H

#include "threads/pte.h"
#include "threads/thread.h"

/*
 * A FTE is free if the `pte` field is NULL.
 * The meaning of other fields is undefined in this case.
 *
 * If `pte` is a valid pointer, `pid` must correspond to a
 * valid, currently used process id and `virt_address` to a
 * virtual address currently mapped into the address space
 * of the process `pid`.
 */
struct frametable_entry {
    struct pagetable_entry* pte;
    tid_t tid; // TODO this is limited, may be merged with virtual address below
    uint32_t virt_address : 20;
    // pin frame, not swapable
    bool pin : 1;
};

void frametable_entry_create(struct frametable_entry* fte,
                             struct pagetable_entry* pte,
                             tid_t tid,
                             void *virt_address,
                             bool pin);

void frame_init(uint32_t size,
                void *frame_base_addr);
bool frame_insert(void *frame_address,
                  tid_t tid,
                  void *virt_address,
                  struct pagetable_entry* pte);
void frame_remove(void *frame_address);
void frame_remove_mult(void *frame_address,
                       size_t cnt);
void* frame_get_free(void);
void frame_set_pin(void *page, bool pin);
void* frame_evict(void);

#endif
