#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include "devices/block.h"

typedef uint8_t cache_t;
typedef uint8_t cache_state_t;

void cache_init(void);
cache_t get_and_pin_block(block_sector_t sector);
void zero_out_sector_data(block_sector_t sector);
void in_cache_and_overwrite_block(block_sector_t  sector,
                                  size_t          ofs,
                                  void           *data,
                                  size_t          length);
void in_cache_and_read(block_sector_t  sector,
                       size_t          ofs,
                       void           *data,
                       size_t          length);
void unpin (cache_t centry);
#endif
