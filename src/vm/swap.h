#include "vm/frame.h"
#include "devices/block.h"

struct swaptable_entry {
    block_sector_t swap_sector;
    struct frametable_entry* frame; 
}

void swap_init();
struct swaptable_enrty* create_swaptable_entry(struct frame *);
void swap_add(struct swaptable_entry *);
void swap_read(struct swaptable_entry *, void *);
void swap_remove(struct swaptable_entry *);
block_sector_t swap_get_next_free();















 