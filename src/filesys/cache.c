#include "filesys/cache.h"

enum cache_state {
    USED = 1<<0,
    DIRTY = 1<<1,
    PIN = 1<<2
};

struct cache_entry {
    block_sector_t sector;
    cache_entry_t state;
};

void cache_init() {
    // reserve memory from user pool
}

/*
 * Returns an empty space in the buffer. The space in buffer is pinned until
 * the pin is removed manually.
 *
 * Returns 0xFF on failure.
 */
cache_t get_and_pin_block(block_sector_t sector) {

}

/*
 * Loads `sector` into cache if not already present and write `length` bytes
 * from `data` to `ofs` within the block.
 *
 * ofs + length MUST be smaller than BLOCK_SECTOR_SIZE.
 */
bool load_and_overwrite_block(block_sector_t  sector,
                         size_t          ofs,
                         void           *data,
                         size_t          length) {

}

/* analoge load_and_overwrite_block but read */;
bool in_cache_and_read(block_sector_t  sector,
                  size_t          ofs,
                  void           *data,
                  size_t          length) {

}

/*
 * Returns true if block is already in cache
 */
bool in_cache(block_sector_t sector) {

}

/*
 * Removes the pin flag
 */
void unpin (cache_t centry) {

}
