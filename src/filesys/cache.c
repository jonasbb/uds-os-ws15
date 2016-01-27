#include "filesys/cache.h"
#include <list.h>
#include <string.h>
#include <round.h>
#include "filesys/filesys.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "vm/frames.h"

// static functions
static cache_t get_and_lock_sector_data(block_sector_t sector);
static void set_accessed (cache_t idx,
                          bool    accessed);
static void set_dirty (cache_t idx,
                       bool    dirty);
static void set_pin (cache_t idx,
                     bool    pin);
static void set_unready (cache_t idx,
                         bool    unready);
static void pin (cache_t idx);
static void *idx_to_ptr(cache_t idx);

/***********************************************************
 * Configuration / Data for cache
 ***********************************************************/

// Maximal number of pages
#define CACHE_SIZE ((cache_t)64)
const cache_t NOT_IN_CACHE = 0xFF;
const block_sector_t NO_SECTOR = 0xFFFFFFFF;

enum cache_state {
    ACCESSED = 1<<0,
    DIRTY = 1<<1,
    PIN = 1<<2, // bitte bitte lieber evict algorithm, lass meinen Block im Cache
    UNREADY = 1<<3 // Eintrag wird mal Daten für sector enthalten, aber nocht nicht jetzt, warte auf condition und recheck
};
typedef uint8_t cache_state_t;

struct cache_entry {
    volatile block_sector_t sector;
    uint16_t refs;
    cache_state_t state;
    struct lock lock;
    struct condition cond;
};

// lock for datastructures
struct lock cache_lock;

// array of actual blocks
void *blocks[CACHE_SIZE];
// array of metadata for cache entries
struct cache_entry *blocks_meta;
// next block to check for eviction
volatile cache_t evict_ptr;

/***********************************************************
 * Configuration / Data for cache END
 ***********************************************************/

/***********************************************************
 * scheduler
 ***********************************************************/
// function declaraions
static
bool request_item_less_func (const struct list_elem *a_,
                             const struct list_elem *b_,
                             void *aux);
static
struct request_item *sched_contains_req(block_sector_t sector,
                                        bool           read);
static
void sched_init(void);
static
void sched_background(void *aux UNUSED);
static
cache_t sched_read(block_sector_t sector);
static
cache_t sched_read_do(block_sector_t sector,
                      bool           isprefetch);
static
void sched_write(block_sector_t sector,
                 cache_t        idx);
static
struct request_item *sched_insert(block_sector_t sector,
                                  cache_t        cache_idx);

struct lock sched_lock;
struct list sched_outstanding_requests;
struct condition sched_new_requests_cond;
struct request_item {
    struct list_elem elem;
    block_sector_t sector;
    struct condition cond;
    cache_t idx;
    bool read;
};

static
bool request_item_less_func (const struct list_elem *a_,
                             const struct list_elem *b_,
                             void *aux UNUSED) {
    struct request_item *a = list_entry(a_, struct request_item, elem);
    struct request_item *b = list_entry(b_, struct request_item, elem);
    return a->sector < b->sector;
}

struct request_item *sched_contains_req(block_sector_t sector,
                                        bool           read) {
    lock_acquire_re(&sched_lock);
    struct request_item *res = NULL;
    struct list_elem *e;
    for (e = list_begin (&sched_outstanding_requests);
         e != list_end (&sched_outstanding_requests);
         e = list_next (e)) {
        struct request_item *r = list_entry (e, struct request_item, elem);
        if (r->sector == sector && r->read == read) {
            res = r;
            break;
        }
    }
    lock_release_re(&sched_lock);
    return res;
}

static
void sched_init() {
    // init data structures
    lock_init(&sched_lock);
    list_init(&sched_outstanding_requests);
    cond_init(&sched_new_requests_cond);

    // start background thread for reading/writing blocks
    thread_create("BLCK_WRTR",
                  thread_current()->priority,
                  &sched_background,
                  NULL);
}

static
void sched_background(void *aux UNUSED) {
    lock_acquire_re(&sched_lock);
    while(true) {
        struct list_elem *e = NULL;
        struct request_item *r = NULL;

        for (e = list_begin(&sched_outstanding_requests);
             e != list_end(&sched_outstanding_requests);
             e = list_next(e)) {

            r = list_entry (e, struct request_item, elem);
            list_remove(e);
            int cnt = lock_release_re_mult(&sched_lock);
            // perform block operation
            if (r->read) {
                block_read(fs_device,
                           r->sector,
                           idx_to_ptr(r->idx));
                lock_acquire_re(&blocks_meta[r->idx].lock);
                // now ready as data is loaded and inform interrested parties
                set_unready(r->idx, false);
                cond_broadcast(&blocks_meta[r->idx].cond, &blocks_meta[r->idx].lock);
                lock_release_re(&blocks_meta[r->idx].lock);
            } else {
                lock_acquire_re(&blocks_meta[r->idx].lock);
                block_write(fs_device,
                            r->sector,
                            idx_to_ptr(r->idx));
                set_dirty(r->idx, false);
                lock_release_re(&blocks_meta[r->idx].lock);
            }
            // mark cache as reusable again
            unpin(r->idx);
            lock_acquire_re_mult(&sched_lock, cnt);
            // notify interested parties about success
            cond_broadcast(&r->cond, &sched_lock);
            free(e);
            e = NULL;
            r = NULL;
        }
        if (!list_empty(&sched_outstanding_requests)) {
            continue;
        }

        // wait until there is something to do
        cond_wait(&sched_new_requests_cond, &sched_lock);
    }
}

/* Increases reference count on new block */
static
cache_t sched_read(block_sector_t sector) {
    lock_acquire_re(&sched_lock);
    cache_t res;
    res = sched_read_do(sector, false);
    lock_acquire_re(&blocks_meta[res].lock);
    blocks_meta[res].refs += 1;
    lock_release_re(&blocks_meta[res].lock);
    lock_release_re(&sched_lock);
    return res;
}

static
cache_t sched_read_do(block_sector_t sector,
                      bool           isprefetch) {
    lock_acquire_re(&sched_lock);
    struct request_item *res;
    if ((res = sched_contains_req(sector, true)) == NULL) {
        res = sched_insert(sector, NOT_IN_CACHE);
    }
    if (!isprefetch && sched_contains_req(sector+1, true) == NULL) {
        sched_insert(sector+1, NOT_IN_CACHE);
    }
    cache_t r = res->idx;
    lock_release_re(&sched_lock);
    return r;
}

/*
void sched_read_sync(block_sector_t sector) {
    lock_acquire_re(&sched_lock);
    // prefetch block
    sched_read_do(sector+1, NOT_IN_CACHE);
    struct request_item *r;
    cache_t idx;
    if ((r = sched_contains_req(sector, true)) == NULL) {
        // request not present, create new one
        r = sched_insert(sector, NOT_IN_CACHE);
        // we already have a lock for r from this thread
        idx = r->idx;
    } else {
        // r not locked
        idx = r->idx;
        lock_acquire(&blocks_meta[idx].lock);
    }
    lock_release_re(&sched_lock);
    // schedule and wait
    cond_wait(&blocks_meta[idx].cond, &blocks_meta[idx].lock);
    lock_release(&blocks_meta[idx].lock);
}
*/

static
void sched_write(block_sector_t sector,
                 cache_t        idx) {
    lock_acquire_re(&sched_lock);
    if (sched_contains_req(sector, false) == NULL) {
        sched_insert(sector, idx);
    }
    lock_release_re(&sched_lock);
}

/*void sched_write_sync(block_sector_t sector,
                      cache_t        idx) {
    lock_acquire_re(&sched_lock);
    struct request_item *r;
    if ((r = sched_contains_req(sector, false)) == NULL) {
        // request not present, create new one
        r = sched_insert(sector, idx);
    }
    // schedule and wait
    cond_wait(r->cond, &sched_lock);
    lock_release_re(&sched_lock);
}*/

static
struct request_item *sched_insert(block_sector_t sector,
                                  cache_t        cache_idx) {
    lock_acquire_re(&sched_lock);
    struct request_item *r = malloc(sizeof(*r));
    ASSERT(r != NULL);
    r->sector = sector;
    r->read = cache_idx == NOT_IN_CACHE;
    cond_init(&r->cond);
    if (cache_idx == NOT_IN_CACHE) {
        r->idx = get_and_pin_block(sector);
    } else {
        r->idx = cache_idx;
    }

    // add to queue
    list_insert_ordered(&sched_outstanding_requests,
                        &r->elem,
                        request_item_less_func,
                        NULL);
    cond_broadcast(&sched_new_requests_cond, &sched_lock);
    lock_release_re(&sched_lock);
    return r;
}
/***********************************************************
 * scheduler END
 ***********************************************************/

void cache_init() {
    sched_init();
    lock_init(&cache_lock);

    // init state
    evict_ptr = 0;

    // reserve memory for actual blocks
    size_t numpages = DIV_ROUND_UP(CACHE_SIZE * BLOCK_SECTOR_SIZE, PGSIZE);
    int i;
    for (i = 0; i < CACHE_SIZE; i+=8) {
        void *page = frame_get_free();
        ASSERT(page != NULL);
        blocks[i+0] = page + 0 * BLOCK_SECTOR_SIZE;
        blocks[i+1] = page + 1 * BLOCK_SECTOR_SIZE;
        blocks[i+2] = page + 2 * BLOCK_SECTOR_SIZE;
        blocks[i+3] = page + 3 * BLOCK_SECTOR_SIZE;
        blocks[i+4] = page + 4 * BLOCK_SECTOR_SIZE;
        blocks[i+5] = page + 5 * BLOCK_SECTOR_SIZE;
        blocks[i+6] = page + 6 * BLOCK_SECTOR_SIZE;
        blocks[i+7] = page + 7 * BLOCK_SECTOR_SIZE;
    }

    // reserve metadata memory
    numpages = DIV_ROUND_UP(CACHE_SIZE * sizeof(struct cache_entry), PGSIZE);
    ASSERT(numpages == 1);
    blocks_meta = frame_get_free();
    ASSERT(blocks_meta != NULL);

    for (i = 0; i < CACHE_SIZE; i++) {
        blocks_meta[i].sector = NO_SECTOR;
        blocks_meta[i].state = 0;
        blocks_meta[i].refs = 0;
        lock_init(&blocks_meta[i].lock);
        cond_init(&blocks_meta[i].cond);
    }
}

/*
 * Returns an empty space in the buffer. The space in buffer is pinned until
 * the pin is removed manually.
 * Load a block into cache and return position in cache.
 */
/* Evict a block. Performs clock algorithm until suitable space is found.
 * Return cache index.
 * Returns NOT_IN_CACHE on failure.
 */
cache_t get_and_pin_block (block_sector_t sector) {
    // sector is used to relabel the cache entry for new usage
    cache_t ptr;

    while(true) {
        ptr = evict_ptr;
        // increment
        evict_ptr = (evict_ptr + 1) % CACHE_SIZE;

        if (lock_try_acquire_re(&blocks_meta[ptr].lock)) {
            if ((blocks_meta[ptr].state & PIN) != 0
                    || blocks_meta[ptr].refs > 0) {
                // pinned page, may not do anything about it
                goto cont;
            } else if ((blocks_meta[ptr].state & DIRTY) == 1) {
                // dirty, shedule write

                // pin page so that we may release the lock
                pin(ptr);
                sched_write(blocks_meta[ptr].sector, ptr);
                goto cont;
            } else if ((blocks_meta[ptr].state & DIRTY) == 0
                       && (blocks_meta[ptr].state & ACCESSED) == 1) {
                // was access, give chance again
                set_accessed(ptr, false);
                goto cont;
            } else if ((blocks_meta[ptr].state & DIRTY) == 0
                       && (blocks_meta[ptr].state & ACCESSED) == 0) {
                // not accessed since last time, may be overwritten
                // mark this entry as to be used by new sector
                blocks_meta[ptr].sector = sector;
                pin(ptr);
                set_unready(ptr, true);
                goto done;
            }
cont:
            lock_release_re(&blocks_meta[ptr].lock);
            continue;
        }
    }
done:
    lock_release_re(&blocks_meta[ptr].lock);
    return ptr;
}

/* Set a whole block to only zeros */
void zero_out_sector_data(block_sector_t sector) {
    lock_acquire(&cache_lock);
    cache_t idx = get_and_pin_block(sector);
    lock_release(&cache_lock);

    lock_acquire_re(&blocks_meta[idx].lock);
    memset(idx_to_ptr(idx), 0, BLOCK_SECTOR_SIZE);
    unpin(idx);
    set_unready(idx, false);
    lock_release_re(&blocks_meta[idx].lock);
}

static
cache_t get_and_lock_sector_data(block_sector_t sector) {
    // return locked block with data from sector
    // if not already in cache load into cache

    cache_t res = NOT_IN_CACHE;

    // search for existing position

    // assures no other insertions are possible w/o our knowledge
    lock_acquire(&cache_lock);
    cache_t i;
    for (i = 0; i < CACHE_SIZE; i++) {
        if (blocks_meta[i].sector == sector) {
            lock_acquire_re(&blocks_meta[i].lock);
            // re-check
            if (blocks_meta[i].sector != sector) {
                // somehow sector changed, no other
                // concurrent thread will have requested a cache position for this
                // sector so this is up to us
                lock_release_re(&blocks_meta[i].lock);
                break;
            } else if ((blocks_meta[i].state & UNREADY) != 0) {
                // count how many threads are interested in this block
                blocks_meta[i].refs += 1;

                lock_release(&cache_lock);
                // wait until data is in cache
                cond_wait(&blocks_meta[i].cond, &blocks_meta[i].lock);

                blocks_meta[i].refs -= 1;
                res = i;
                goto entry_found;
            }
            res = i;
            lock_release(&cache_lock);
            goto entry_found;
        }
    }

    // schedule read
    res = sched_read(sector);
    // reference count is increase for our thread
    // this entry will be valid until ref is 0 again

    lock_release(&cache_lock);
    lock_acquire_re(&blocks_meta[res].lock);
    if ((blocks_meta[res].state & UNREADY) != 0) {
        // wait until data is in cache
        cond_wait(&blocks_meta[res].cond, &blocks_meta[res].lock);
    }
    blocks_meta[i].refs -= 1;

entry_found:
    // sector is the correct one and data is available (due to cond)
    // and metadata lock is held
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

    // get block pos
    cache_t ind = get_and_lock_sector_data(sector);
    // write data
    // to, from, length
    memcpy(idx_to_ptr(ind)+ofs, data, length);
    set_dirty(ind, true);
    set_accessed(ind, true);
    lock_release_re(&blocks_meta[ind].lock);
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

    // get block pos
    cache_t ind = get_and_lock_sector_data(sector);
    // read data
    // to, from, length
    memcpy(data, idx_to_ptr(ind)+ofs, length);
    set_accessed(ind, true);
    lock_release_re(&blocks_meta[ind].lock);
}

/*
 * Set the accessed flag
 */
static
void set_accessed (cache_t idx, bool accessed) {
    // valid range
    ASSERT(idx <= CACHE_SIZE);
    lock_acquire_re(&blocks_meta[idx].lock);
    if (accessed) {
        blocks_meta[idx].state |= ACCESSED;
    } else {
        blocks_meta[idx].state &= ~ACCESSED;
    }
    lock_release_re(&blocks_meta[idx].lock);
}

/*
 * Set the dirty flag
 */
static
void set_dirty (cache_t idx, bool dirty) {
    // valid range
    ASSERT(idx <= CACHE_SIZE);
    lock_acquire_re(&blocks_meta[idx].lock);
    if (dirty) {
        blocks_meta[idx].state |= DIRTY;
    } else {
        blocks_meta[idx].state &= ~DIRTY;
    }
    lock_release_re(&blocks_meta[idx].lock);
}

/*
 * Set the unready flag
 */
static
void set_unready (cache_t idx, bool unready) {
    // valid range
    ASSERT(idx <= CACHE_SIZE);
    lock_acquire_re(&blocks_meta[idx].lock);
    if (unready) {
        blocks_meta[idx].state |= UNREADY;
    } else {
        blocks_meta[idx].state &= ~UNREADY;
    }
    lock_release_re(&blocks_meta[idx].lock);
}

/*
 * Set the pin flag
 */
static
void set_pin (cache_t idx, bool pin) {
    // valid range
    ASSERT(idx <= CACHE_SIZE);
    lock_acquire_re(&blocks_meta[idx].lock);
    if (pin) {
        blocks_meta[idx].state |= PIN;
    } else {
        blocks_meta[idx].state &= ~PIN;
    }
    lock_release_re(&blocks_meta[idx].lock);
}

/*
 * Set the pin flag
 */
static
void pin (cache_t idx) {
    set_pin(idx, true);
}

/*
 * Removes the pin flag
 */
void unpin (cache_t idx) {
    set_pin(idx, false);
}

static
void *idx_to_ptr(cache_t idx) {
    // valid range
    ASSERT(idx <= CACHE_SIZE);
    return blocks[idx];
}
