#include "vm/frames.h"
#include "devices/block.h"

struct swaptable_entry {
    block_sector_t swap_sector;
    void * frame_kaddr; 
};

void init_swap(void);
struct swaptable_entry* create_swaptable_entry(void *);
void swap_add(struct swaptable_entry *);
void swap_read(struct swaptable_entry *, void *);
void swap_remove(struct swaptable_entry *);
block_sector_t swap_get_next_free(void);
void read_page_from_block(struct swaptable_entry * , void *);
void write_page_to_block(struct swaptable_entry * , int);













 