#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include <stdbool.h>
#include <stdlib.h>

typedef uint8_t cache_t;
typedef uint8_t cache_state_t;

void cache_init();
cache_t get_and_pin_block(block_sector_t sector);
bool load_and_overwrite_block(block_sector_t  sector,
                              size_t          ofs,
                              void           *data,
                              size_t          length);
bool in_cache_and_read(block_sector_t  sector,
                       size_t          ofs,
                       void           *data,
                       size_t          length);
bool in_cache(block_sector_t sector);
void unpin (cache_t centry);
#endif
