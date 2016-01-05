#include "vm/swap.h"
#include "lib/kernel/bitmap.h"

struct block swap_block;
int swap_block_size;
struct bitmap swap_map;


// Init Swap at block device
void init_swap() {
    swap_block = block_get_role(BLOCK_SWAP);
    swap_block_size = block_size(swap_block);
    swap_map = bitmap_create(swap_block_size);
}

// Create new swaptable entry from frametable entry
struct swaptable_entry* 
create_swaptable_entry(struct frametable_entry * f) {
    struct swaptable_entry* swap_entry = 0;//TODO: Where to allocate memory
    swap_entry -> frame = f;
    return swap_entry;
} 

// Returns next free swap address to store  
block_sector_t
swap_get_next_free() {
    int bitmap_size = bitmap_size(swap_map);
    int swap_page_size; //TODO How big is a swap page?
    //TODO check if swap is full
    block_sector_t next_free_swap = bitmap_scan(swap_map, 0, swap_page_size, false);
    if (next_free_swap != BITMAP_ERROR) {
        bitmap_set_multiple(swap_map, next_free_swap, swap_page_size, true);
    }
    return next_free_swap;
    
}

// Writes a swaptable entry to memory
void 
swap_add(struct swaptable_entry * st_e) {
    //find free entry
    block_sector_t next_free_swap = swap_get_next_free();
    // Iterate over all sectors
    block_write(swap_block, next_free_swap, st_e->frame->addr);
    
    // Set addr in swaptable entry
    st_e -> swap_sector = next_free_swap;
}

// Removes a swaptable entry
void
swap_remove(struct swaptable_entry * st_e) {
    int swap_page_size; //TODO
    bitmap_set_multiple(swap_map, st_e -> swap_sector, swap_page_size, false);
}

// Loads a swaptable entry to the given address
void
swap_read(struct swaptable_entry * st_e, void *addr) {
    // TODO Iterate over all sectors
    block_read(swap_block, st_e ->swap_sector, addr);
    swap_remove(st_e);
}