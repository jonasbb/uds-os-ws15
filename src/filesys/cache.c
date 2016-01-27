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

/***********************************************************
 * Configuration / Data for cache
 ***********************************************************/

// Maximal number of pages
const cache_t CACHE_SIZE = 64;
const cache_t NOT_IN_CACHE = 0xFF;
const block_sector_t NO_SECTOR = 0xFFFFFFFF;

enum cache_state {
    ACCESSED = 1<<0,
    DIRTY = 1<<1,
    PIN = 1<<2, // bitte bitte lieber evict algorithm, lass meinen Block im Cache
    UNREADY = 1<<3 // Eintrag wird mal Daten für sector enthalten, aber nocht nicht jetzt, warte auf condition und recheck
};
typedef uint8_t cache_state_t;

// TODO have locks per entry
struct cache_entry {
    volatile block_sector_t sector;
    cache_state_t state;
    struct lock lock;
    struct condition cond;
};

// lock for datastructures
struct lock cache_lock;
// message system to inform about newly read blocks
struct condition block_read;

// array of actual blocks
void *blocks[];
// array of metadata for cache entries
struct cache_entry blocks_meta[];
// next block to check for eviction
volatile cache_t evict_ptr;

/***********************************************************
 * Configuration / Data for cache END
 ***********************************************************/

/***********************************************************
 * scheduler
 ***********************************************************/
void sched_read_do(block_sector_t sector, bool isprefetch);

struct lock sched_lock;
struct list sched_outstanding_requests;
struct condition sched_new_requests_cond;
struct request_item {
    struct list_elem elem;
    block_sector_t sector;
    struct condition cond;
    cache_t idx;
    bool read;
}

bool request_item_less_func (const struct list_elem *a_,
                             const struct list_elem *b_,
                             void *aux) {
    struct request_item *a = list_elem(a_, struct request_item, elem);
    struct request_item *b = list_elem(b_, struct request_item, elem);
    return a->sector < b->sector;
}

struct request_item *sched_contains_req(block_sector_t sector,
                                        bool           read) {
    lock_acquire_re(&sched_lock);
    struct request_item *res = NULL;
    struct list_elem *e;
    for (e = list_begin (&process_states[pid].fdlist);
         e != list_end (&process_states[pid].fdlist);
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
            list_remove(&sched_outstanding_requests, e);
            int cnt = lock_release_re_mult(&sched_lock);
            // perform block operation
            if (r->read) {
                block_read(fs_device,
                           r->sector,
                           idx_to_ptr(r->idx));
                lock_acquire(&blocks_meta[r->idx].lock);
                // now ready as data is loaded and inform interrested parties
                blocks_meta[r->idx].state &= ~UNREADY;
                cond_broadcast(&blocks_meta[r->idx].cond, &blocks_meta[r->idx].lock);
                lock_release(&blocks_meta[r->idx].lock);
            } else {
                block_write(fs_device,
                            r->sector,
                            idx_to_ptr(r->idx));
                set_dirty(r->idx, false);
            }
            // mark cache as reusable again
            unpin(r->idx);
            lock_acquire_re_mult(&sched_lock, cnt);
            // notify interested parties about success
            cond_broadcast(r->cond, &sched_lock);
            free(e);
            e = NULL;
            r = NULL
        }
        if (!list_empty(&sched_outstanding_requests)) {
            continue;
        }

        // wait until there is something to do
        cond_wait(&sched_new_requests_cond, &sched_lock);
    }
}

cache_t sched_read(block_sector_t sector) {
    return sched_read_do(sector, false);
}

static
cache_t sched_read_do(block_sector_t sector, bool isprefetch) {
    lock_acquire_re(&sched_lock);
    cache_t r1, r2;
    if ((r1 = sched_contains_req(sector, true)) == NULL) {
        r1 = sched_insert(sector, NOT_IN_CACHE);
        lock_release(&blocks_meta[r1->idx].lock);
    }
    if (!isprefetch && (r2 = sched_contains_req(sector+1, true)) == NULL) {
        r2 = sched_insert(sector+1, NOT_IN_CACHE);
        lock_release(&blocks_meta[r2->idx].lock);
    }
    lock_release_re(&sched_lock);
    return r1;
}

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

/*
 * cache entry is locked in cache cache_idx is NOT_IN_CACHE
 */
static
cache_t sched_insert(block_sector_t sector,
                     cache_t        cache_idx) {
    lock_acquire_re(&sched_lock);
    cache_t res;
    struct request_item *r = malloc(sizeof(*r));
    ASSERT(r != NULL);
    r->sector = sector;
    r->read = cache_idx == NOT_IN_CACHE;
    cond_init(r->cond);
    if (cache_idx == NOT_IN_CACHE) {
        r->idx = get_and_lock_block(sector);
    } else {
        r->idx = cache_idx;
    }
    res = r->idx;

    // add to queue
    list_insert_ordered(&sched_outstanding_requests,
                        r,
                        request_item_less_func,
                        void);
    cond_broadcast(&sched_new_requests_cond);
    lock_release_re(&sched_lock);
    return res;
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
    // TODO get consecutive pages from pool

    // reserve metadata memory
    numpages = DIV_ROUND_UP(CACHE_SIZE * sizeof(struct cache_entry), PGSIZE);
    // TODO get consecutive pages from pool

    cache_t i;
    for (i = 0; i < CACHE_SIZE; i++) {
        blocks_meta[i].sector = NO_SECTOR;
        blocks_meta[i].state = 0;
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

        if (lock_try_acquire(&blocks_meta[ptr])) {
            if ((blocks_meta[ptr].state & PIN) != 0) {
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
                blocks_meta[ptr].state |= UNREADY;
                goto done;
            }
cont:
            lock_release(&blocks_meta[ptr]);
            continue;
        }
    }
done:
    lock_release(&blocks_meta[ptr]);
    return ptr;
}

static
cache_t get_block_and_lock(block_sector_t sector) {
    // return locked block with data from sector
    // if not already in cache load into cache

    // search for existing position
    lock_acquire(&cache_lock);
    cache_t i;
    for (i = 0; i < CACHE_SIZE; i++) {
        if (blocks_meta[i].sector == sector) {
            lock_acquire(&blocks_meta[i].lock);
            // re-check
            if (blocks_meta[i].sector != sector) {
                // somehow sector changed, no other
                // concurrent thread will have requested a cache position for this
                // sector so this is up to us
                lock_release(&blocks_meta[i].lock);
                break;
            } else if (blocks_meta[i].state & UNREADY != 0) {
                // wait until data is in cache
                cond_wait(&blocks_meta[i].cond, &blocks_meta[i].lock);

                // re-re-check
                // entry may have changed
                if (blocks_meta[i].sector != sector) {
                    // somehow sector changed, no other
                    // concurrent thread will have requested a cache position for this
                    // sector so this is up to us
                    lock_release(&blocks_meta[i].lock);
                    break;
                } else {
                    // sector is the correct one and data is available (due to cond)
                    // and metadata lock is held
                    lock_release(&cache_lock);
                    return i;
                }
            }
            // is usable
        }
    }

    // schedule sync read
    sched_read_sync(sector);
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
    cache_t ind = get_block_and_lock(sector);
    // write data
    // to, from, length
    memcpy(blocks[ind]+ofs, data, length);
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

    lock_acquire_re(&cache_lock);
    // get block pos
    cache_t ind = get_block(sector);
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
/*
static
void update_cache_stats() {
  //TODO do one dec and one inc step
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
*/

static
void *idx_to_ptr(cache_t idx) {
    // valid range
    ASSERT(idx <= CACHE_SIZE);
    return blocks[idx];
}
