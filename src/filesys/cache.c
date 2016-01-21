#include "filesys/cache.h"
#include <string.h>
#include <round.h>
#include "filesys/filesys.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

// static functions
static cache_t evict_block(void);
static void set_accessed (cache_t centry, bool accessed);
static void set_dirty (cache_t centry, bool dirty);
static void pin (cache_t centry);
static cache_t get_cache_position (block_sector_t sector);
static void update_cache_stats(void);

// Maximal number of pages
const cache_t CACHE_SIZE = 64;
const cache_t NOT_IN_CACHE = 0xFF;

enum cache_state {
    ACCESSED = 1<<0,
    DIRTY = 1<<1,
    PIN = 1<<2
};

struct cache_entry {
    block_sector_t sector;
    cache_state_t state;
};

// lock for datastructures
struct lock cache_lock;

// array of actual blocks
void *blocks[];
// array of metadata for cache entries
struct cache_entry *blocks_meta;
// next block to check for eviction
cache_t evict_ptr;

// how many blocks have the pin + any other bit set
uint8_t stats_pin;
// how many blocks are dirty but not pinned
uint8_t stats_dirty;
// how many block have only the accessed bit set
uint8_t stats_accessed;
// how many blocks are free to use, no flags set
uint8_t stats_evictable;

void cache_init() {
    // init locks
    lock_init(&cache_lock);

    // init state
    evict_ptr = 0;

    // reserve memory for actual blocks
    size_t numpages = DIV_ROUND_UP(CACHE_SIZE * BLOCK_SECTOR_SIZE, PGSIZE);
    // TODO get consecutive pages from pool

    // reserve metadata memory
    numpages = DIV_ROUND_UP(CACHE_SIZE * sizeof(struct cache_entry), PGSIZE);
    // TODO get consecutive pages from pool

    // init all stats
    update_cache_stats();
}

/*
 * Returns an empty space in the buffer. The space in buffer is pinned until
 * the pin is removed manually.
 * Load a block into cache and return position in cache.
 */
cache_t get_and_pin_block (block_sector_t sector) {
    lock_acquire_re(&cache_lock);
    // TODO implement
    // get page and pin
    // release lock
    // shedule read sync
    // acq lock
    // remove pin
    // return position

    // may be already contained
    cache_t ind = get_cache_position(sector);
    if (ind != NOT_IN_CACHE) {
        goto done;
    }

    while(ind == NOT_IN_CACHE) {
        ind = evict_block();

        // no suitable cache place could be found
        // let other processes run in hope the situation changes
        if (ind == NOT_IN_CACHE) {
            thread_yield();
        }
    }
    pin (ind);
    int lock_cnt = lock_release_re_mult(&cache_lock);
    //TODO shedule read sync
    thread_yield();
    lock_acquire_re_mult(&cache_lock, lock_cnt);
    lock_release_re(&cache_lock);
done:
    return ind;
}

/* Evict a block. Performs clock algorithm until suitable space is found.
 * Return cache index.
 * Returns NOT_IN_CACHE on failure.
 */
static
cache_t evict_block() {
    lock_acquire_re(&cache_lock);
    if (stats_pin >= CACHE_SIZE) {
        return NOT_IN_CACHE;
    }

    cache_t res = 0;
    // if we start with evictable pages within one clock cycle we will have
    // found a usable slot
    bool has_evictable_pages = stats_evictable > 0;
    // whether we sheduled a write
    bool did_shedule_write = false;

    while(true) {
        // increment
        evict_ptr = (evict_ptr + 1) % CACHE_SIZE;

        // if there are no easy evictable blocks we need at least two clock
        // cycles to find a block to evict. In this case we will yield the
        // thread to perform the write in the background thread.
        // Of corse this should only happen if we caused outstanding writes.
        // Shedule this every 32 checked blocks, so twice per iteration
        if ((evict_ptr & 0xE0) == 0 && !has_evictable_pages && did_shedule_write) {
            int lock_cnt = lock_release_re_mult(&cache_lock);
            // yield for writes
            thread_yield();
            lock_acquire_re_mult(&cache_lock, lock_cnt);
            // update yield condition
            has_evictable_pages = stats_evictable > 0;
        }

        if ((blocks_meta[evict_ptr].state & PIN) != 0) {
            // pinned page, may not do anything about it
            continue;
        } else if ((blocks_meta[evict_ptr].state & DIRTY) == 1) {
            // dirty, shedule write

            // pin page so that we
            pin(evict_ptr);
            did_shedule_write = true;
            //TODO shedule write back
            // we probably need to yield the lock now so that the pin flag may get removed
            continue;
        } else if ((blocks_meta[evict_ptr].state & DIRTY) == 0
                   && (blocks_meta[evict_ptr].state & ACCESSED) == 1) {
            // was access, give chance again
            set_accessed(evict_ptr, false);
            continue;
        } else if ((blocks_meta[evict_ptr].state & DIRTY) == 0
                   && (blocks_meta[evict_ptr].state & ACCESSED) == 0) {
            // not accessed since last time, may be overwritten
            res = evict_ptr;
            break;
        }
    }
    lock_release_re(&cache_lock);
    return res;
}
/*
 * Loads `sector` into cache if not already present and write `length` bytes
 * from `data` to `ofs` within the block.
 *
 * `length` == 0 calls are nops.
 *
 * ofs + length MUST be smaller than BLOCK_SECTOR_SIZE.
 */
void in_cache_and_overwrite_block(block_sector_t  sector,
                              size_t          ofs,
                              void           *data,
                              size_t          length) {
    ASSERT(ofs + length <= BLOCK_SECTOR_SIZE);

    if (length == 0) {
        return;
    }

    lock_acquire_re(&cache_lock);
    // get block pos
    cache_t ind = get_and_pin_block(sector);
    unpin(ind);
    // write data
    // to, from, length
    memcpy(blocks[ind]+ofs, data, length);
    set_dirty(ind, true);
    set_accessed(ind, true);
    lock_release_re(&cache_lock);
}

/* analoge in_cache_and_overwrite_block but read */;
void in_cache_and_read(block_sector_t  sector,
                       size_t          ofs,
                       void           *data,
                       size_t          length) {
    ASSERT(ofs + length <= BLOCK_SECTOR_SIZE);

    if (length == 0) {
        return;
    }

    lock_acquire_re(&cache_lock);
    // get block pos
    cache_t ind = get_and_pin_block(sector);
    unpin(ind);
    // read data
    // to, from, length
    memcpy(data, blocks[ind]+ofs, length);
    set_accessed(ind, true);
    lock_release_re(&cache_lock);
}

/*
 * Returns true if block is already in cache
 */
bool in_cache(block_sector_t sector) {
    return get_cache_position(sector) != NOT_IN_CACHE;
}

/*
 * Set the accessed flag
 */
static
void set_accessed (cache_t centry, bool accessed) {
    // valid range
    ASSERT(centry <= CACHE_SIZE);
    lock_acquire_re(&cache_lock);
    if (accessed) {
        blocks_meta[centry].state |= ACCESSED;
    } else {
        blocks_meta[centry].state &= ~ACCESSED;
    }
    update_cache_stats();
    lock_release_re(&cache_lock);
}

/*
 * Set the accessed flag
 */
static
void set_dirty (cache_t centry, bool dirty) {
    // valid range
    ASSERT(centry <= CACHE_SIZE);
    lock_acquire_re(&cache_lock);
    if (dirty) {
        blocks_meta[centry].state |= DIRTY;
    } else {
        blocks_meta[centry].state &= ~DIRTY;
    }
    update_cache_stats();
    lock_release_re(&cache_lock);
}

/*
 * Set the pin flag
 */
static
void pin (cache_t centry) {
    // valid range
    ASSERT(centry <= CACHE_SIZE);
    lock_acquire_re(&cache_lock);
    blocks_meta[centry].state |= PIN;
    update_cache_stats();
    lock_release_re(&cache_lock);
}

/*
 * Removes the pin flag
 */
void unpin (cache_t centry) {
    // valid range
    ASSERT(centry <= CACHE_SIZE);
    lock_acquire_re(&cache_lock);
    blocks_meta[centry].state &= ~PIN;
    update_cache_stats();
    lock_release_re(&cache_lock);
}

/*
 * Get cache position for sector.
 * Returns NOT_IN_CACHE if not in cache.
 */
static
cache_t get_cache_position (block_sector_t sector) {
    cache_t res = NOT_IN_CACHE;
    lock_acquire_re(&cache_lock);
    cache_t i;
    for (i = 0; i < CACHE_SIZE; i++) {
        if (blocks_meta[i].sector == sector) {
            break;
        }
    }
    lock_release_re(&cache_lock);
    return res;
}

/* Update stats about blocks in cache
 */
static
void update_cache_stats() {
    lock_acquire_re(&cache_lock);
    stats_pin = 0;
    stats_dirty = 0;
    stats_accessed = 0;
    stats_evictable = 0;

    cache_t i;
    for (i = 0; i < CACHE_SIZE; i++) {
        switch(blocks_meta[i].state & (DIRTY | ACCESSED | PIN)) {
        case PIN | ACCESSED | DIRTY:
        case PIN | ACCESSED:
        case PIN | DIRTY:
        case PIN:
            stats_pin++;
            break;
        case DIRTY | ACCESSED:
        case DIRTY:
            stats_dirty++;
            break;
        case ACCESSED:
            stats_accessed++;
            break;
        default:
            stats_evictable++;
            break;
        }
    }
    lock_release_re(&cache_lock);
}
