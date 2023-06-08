#include <glib.h>

#include "ulibss.h"
#include "cache.h"

#ifdef __GNUC__
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#else
#define likely(x) (x)
#define unlikely(x) (x)
#endif

#define EVICT_CNT 32
#define TEMP_ARRAY_SZ 15

// int cache_type = WRITE_AROUND;

ssize_t ss_pread_nocache(int fd, void *buf, size_t len, off_t offset);
ssize_t ss_pwrite_nocache(int fd, const void *buf, size_t len, off_t offset, bool wait);

struct lru_entry_s;
typedef struct lru_entry_s lru_entry_t;
struct file_cache_s;
typedef struct file_cache_s file_cache_t;

struct lru_entry_s
{
    uint32_t inode;
    off_t blk_offset;
    struct lru_entry_s *prev;
    struct lru_entry_s *next;
    int removed;
    int written;
    int fd; // for write back only.
    file_cache_t *fc;
};

__thread lru_entry_t *temp_array[TEMP_ARRAY_SZ];
__thread int temp_cnt;

#ifdef HIT_RATIO
struct hr_calc_s
{
    size_t hit;
    size_t miss;
    pthread_mutex_t hr_lock;
};
typedef struct hr_calc_s hr_calc_t;
#endif

struct file_cache_s // per-inode cache
{
    int inode;
    GHashTable *cache_blks; // offset to lru_entry map
    // pthread_mutex_t         caches_lock;
    pthread_rwlock_t caches_rw_lock;
    bool ra_enable; // per-file setting for read ahead
    int ref_cnt;    // multiple fds may point to one file_cache_s
};

struct cache_s
{
    struct cache_attr status;
    void *mempool; // cache mempool here
    lru_entry_t *head;
    lru_entry_t *tail;
    lru_entry_t *free_head;
    lru_entry_t *free_tail;
    pthread_mutex_t head_lock;
    pthread_mutex_t free_head_lock;
    size_t used_sz; // bytes
    // pthread_mutex_t used_lock;
    GHashTable *files_cache; // inode to file_cache map
    pthread_mutex_t files_lock;
#ifdef HIT_RATIO
    hr_calc_t hr_calc; // hit ratio calculator
#endif
};

static struct cache_s *cache;

// NOT THREAD SAFE!!!
int init_cache(cache_attr_t *attr)
{
#ifdef SS_DEBUG
    printf("[CACHE] file cache init\n");
#endif

    temp_cnt = 0;

    cache = (struct cache_s *)malloc(sizeof(struct cache_s));

    cache->status.blk_sz = attr->blk_sz;
    cache->status.cache_sz = attr->cache_sz;
    cache->status.ra_after = attr->ra_after;
    cache->status.ra_before = attr->ra_before;
    cache->status.ra_enable = attr->ra_enable;
    cache->status.cache_type = attr->cache_type;

    // lru list init
    cache->head = (lru_entry_t *)malloc(sizeof(lru_entry_t)); // idle node, will always at the head
    cache->tail = (lru_entry_t *)malloc(sizeof(lru_entry_t)); // idle node, will always at the tail
    cache->head->next = cache->tail;
    cache->tail->prev = cache->head;
    cache->head->prev = cache->tail->next = NULL;

    // lru free list init
    cache->free_head = (lru_entry_t *)malloc(sizeof(lru_entry_t)); // idle node, will always at the head
    cache->free_tail = (lru_entry_t *)malloc(sizeof(lru_entry_t)); // idle node, will always at the tail
    cache->free_head->next = cache->free_tail;
    cache->free_tail->prev = cache->free_head;
    cache->free_head->prev = cache->free_tail->next = NULL;

    cache->head->blk_offset = cache->tail->blk_offset = cache->free_head->blk_offset = cache->free_tail->blk_offset = 123;
    cache->head->inode = cache->tail->inode = 1;
    cache->free_head->inode = cache->free_tail->inode = 0;

    lru_entry_t *tmp;
    for (int i = 0; i < cache->status.cache_sz; i += cache->status.blk_sz) 
    {
        tmp = (lru_entry_t *)malloc(sizeof(lru_entry_t));
        tmp->removed = 1;
        tmp->written = 0;
        tmp->blk_offset = 0;
        tmp->next = cache->free_head->next;
        tmp->prev = cache->free_head;
        cache->free_head->next = tmp;
        tmp->next->prev = tmp;
    }

    cache->mempool = (void *)ss_buf_init(attr->blk_sz * (cache->status.ra_after + cache->status.ra_before + 1));
    cache->files_cache = g_hash_table_new(g_direct_hash, g_direct_equal);

    // pthread_mutex_init(&cache->used_lock, NULL); // never used
    pthread_mutex_init(&cache->head_lock, NULL);
    pthread_mutex_init(&cache->free_head_lock, NULL);
    pthread_mutex_init(&cache->files_lock, NULL);

#ifdef HIT_RATIO
    cache->hr_calc.hit = 0;
    cache->hr_calc.miss = 0;
    pthread_mutex_init(&cache->hr_calc.hr_lock, NULL);
#endif

#ifdef SS_DEBUG
    printf("[CACHE] file cache init done!\n");
#endif

    return 0;
}

gboolean __iter_clean_up_caches(gpointer key, gpointer value, gpointer data)
{
    lru_entry_t *ent = (lru_entry_t *)value;
    ent->prev->next = ent->next;
    ent->next->prev = ent->prev;

    ent->prev = cache->free_head;
    ent->next = cache->free_head->next;
    ent->prev->next = ent;
    ent->next->prev = ent;
    // free(ent);
    return true;
}

int clean_up(file_cache_t *fc)
{
    g_hash_table_foreach_remove(fc->cache_blks, __iter_clean_up_caches, NULL);
}

gboolean __iter_clean_up_files(gpointer key, gpointer value, gpointer data)
{
    file_cache_t *fc = (file_cache_t *)value;
    clean_up(fc);

    return true;
}

// NOT THREAD SAFE!!!
int fini_cache()
{
#ifdef SS_DEBUG
    printf("[CACHE] file cache finalize\n");
#endif

    // free lru list
    g_hash_table_foreach_remove(cache->files_cache, __iter_clean_up_files, NULL);
    // print_cache();
    if (unlikely(cache->head->next != cache->tail || cache->tail->prev != cache->head)) // FIXME: Error exists.
    {
        // leakage
        // fprintf(stderr, "[CACHE ERROR] Ooops, the doubly linked list seems to be wrong.\n");

        lru_entry_t *ent;
        while (cache->head->next != cache->tail)
        {
            ent = cache->head->next;
            cache->head->next = ent->next;
            ent->next->prev = cache->head;
            free(ent);
            // fprintf(stderr, "[CACHE ERROR] wrong entry: inode %ld blk %ld\n", ent->inode, ent->blk_offset);
        }
    }
#ifdef SS_DEBUG
    printf("[CACHE] free lru idle nodes\n");
#endif
    free(cache->head); // free idle head node
    free(cache->tail); // free idle tail node

#ifdef SS_DEBUG
    printf("[CACHE] free free list\n");
#endif
    // free free list
    while (cache->free_head->next)
    {
        cache->free_head = cache->free_head->next;
        free(cache->free_head->prev);
    }
    free(cache->free_head);

#ifdef SS_DEBUG
    printf("[CACHE] destroy hash table\n");
#endif
    g_hash_table_destroy(cache->files_cache);
    ss_buf_clear(cache->mempool);
    free(cache);
#ifdef SS_DEBUG
    printf("[CACHE] file cache finalize done\n");
#endif
    return 0;
}

int open_file_cache(int fd)
{
    pthread_mutex_lock(&cache->files_lock);

    int inode = get_inode(fd);
    file_cache_t *fc = (file_cache_t *)g_hash_table_lookup(cache->files_cache, GINT_TO_POINTER(inode));

    // file count != NULL
    if (fc)
    {
        fc->ref_cnt++;
    }
    else
    {
        fc = (file_cache_t *)malloc(sizeof(file_cache_t));
        memset(fc, 0, sizeof(file_cache_t));
        fc->inode = inode;
        fc->ra_enable = cache->status.ra_enable;
        fc->ref_cnt = 1;
        fc->cache_blks = g_hash_table_new(g_direct_hash, g_direct_equal);
        // pthread_mutex_init(&fc->caches_lock, NULL);
        pthread_rwlock_init(&fc->caches_rw_lock, NULL);
        g_hash_table_insert(cache->files_cache, GINT_TO_POINTER(inode), fc);
    }

    pthread_mutex_unlock(&cache->files_lock);
#ifdef SS_DEBUG
    printf("[CACHE] file cache open for fd %d\n", fd);
#endif
    return 0;
}

int close_file_cache(int fd)
{
    pthread_mutex_lock(&cache->files_lock);

    int inode = get_inode(fd);
    file_cache_t *fc = (file_cache_t *)g_hash_table_lookup(cache->files_cache, GINT_TO_POINTER(inode));
    if (!fc)
    {
        fprintf(stderr, "[CACHE ERROR] closing fd %d inode %d that is already closed.\n", fd, inode);
        return -1;
    }

    fc->ref_cnt--;
    if (fc->ref_cnt == 0)
    {
        clean_up(fc);
        pthread_rwlock_wrlock(&fc->caches_rw_lock);
        g_hash_table_destroy(fc->cache_blks);
        pthread_rwlock_unlock(&fc->caches_rw_lock);
        g_hash_table_remove(cache->files_cache, GINT_TO_POINTER(inode));
        free(fc);
    }

    pthread_mutex_unlock(&cache->files_lock);
#ifdef SS_DEBUG
    printf("[CACHE] file cache closed for fd %d\n", fd);
#endif
    return 0;
}

static void lru_update(lru_entry_t *ent)
{
    // put into temp array
    temp_array[temp_cnt] = ent;
    temp_cnt++;

    // if temp array is full, update the lru list
    if (temp_cnt == TEMP_ARRAY_SZ)
    {
        temp_cnt = 0;

        pthread_mutex_lock(&cache->head_lock);

        for (int i = 0; i < TEMP_ARRAY_SZ; i++)
        {
            ent = temp_array[i];
            // the entry may be removed when in temp array, drop it in that case
            if (!ent->removed)
            {
                // take from current pos
                ent->prev->next = ent->next;
                ent->next->prev = ent->prev;

                // insert into head pos
                ent->prev = cache->head;
                ent->next = cache->head->next;
                cache->head->next = ent;
                ent->next->prev = ent;
            }
        }
        pthread_mutex_unlock(&cache->head_lock);
    }
}

static void lru_evict_to_free()
{
#ifdef SS_DEBUG
    printf("[CACHE] EVICT start\n");
#endif
    lru_entry_t *enta, *entb;

    // LOCK ORDER: head_lock FIRST, THEN free_head_lock, THEN cache lock
    pthread_mutex_lock(&cache->head_lock);
    pthread_mutex_lock(&cache->free_head_lock);

    if (cache->head->next == cache->tail)
    {
        pthread_mutex_unlock(&cache->head_lock);
        pthread_mutex_unlock(&cache->free_head_lock);
        return;
    }
    int blk_sz = cache->status.blk_sz;
    // take from lru
    // ent = cache->tail->prev;
    // ent->prev->next = cache->tail;
    // cache->tail->prev = ent->prev;
    enta = entb = cache->tail->prev;

    // remove from hash table
#ifdef SS_DEBUG
    printf("[CACHE] REMOVE blk %d of inode %d\n", enta->blk_offset, enta->fc->inode);
#endif
    // LOCK ORDER: head lock FIRST, THEN cache lock
    pthread_rwlock_wrlock(&enta->fc->caches_rw_lock);
    g_hash_table_remove(enta->fc->cache_blks, GINT_TO_POINTER(enta->blk_offset));
    pthread_rwlock_unlock(&enta->fc->caches_rw_lock);
    

    enta->removed = 1;
    // for dirty cache write back
    if (cache->status.cache_type == WRITE_BACK && enta->written)
    {
        ss_pwrite_nocache(enta->fd, cache->mempool, blk_sz, enta->blk_offset * blk_sz, false);
    }
    enta->written = 0;
#ifdef SS_DEBUG
    printf("[CACHE] EVICT blk %ld of inode %d\n", enta->blk_offset, enta->inode);
#endif
    for (int i = 1; i < EVICT_CNT; i++)
    {
        enta = enta->prev;
        if (unlikely(enta == cache->head))
        {
            enta = enta->next;
            break;
        }
        // remove from hash table
#ifdef SS_DEBUG
        printf("[CACHE] REMOVE blk %d of inode %d\n", enta->blk_offset, enta->fc->inode);
#endif
        // LOCK ORDER: head lock FIRST, THEN cache lock
        pthread_rwlock_wrlock(&enta->fc->caches_rw_lock);
        g_hash_table_remove(enta->fc->cache_blks, GINT_TO_POINTER(enta->blk_offset));
        pthread_rwlock_unlock(&enta->fc->caches_rw_lock);
        enta->removed = 1;
        // for dirty cache write back
        if (cache->status.cache_type == WRITE_BACK && enta->written)
        {
            ss_pwrite_nocache(enta->fd, cache->mempool, blk_sz, enta->blk_offset * blk_sz, false);
        }
        enta->written = 0;
        
#ifdef SS_DEBUG
        printf("[CACHE] EVICT blk %ld of inode %d\n", enta->blk_offset, enta->inode);
#endif
    }

    enta->prev->next = entb->next;
    entb->next->prev = enta->prev;

    // put into free
    entb->next = cache->free_tail;
    enta->prev = cache->free_tail->prev;
    enta->prev->next = enta;
    entb->next->prev = entb;

    pthread_mutex_unlock(&cache->head_lock);
    pthread_mutex_unlock(&cache->free_head_lock);
}

static lru_entry_t *try_take_from_free()
{
    pthread_mutex_lock(&cache->free_head_lock);
    if (cache->free_head->next == cache->free_tail)
    {
        pthread_mutex_unlock(&cache->free_head_lock);
        return NULL;
    }

    // take from free list head
    lru_entry_t *ent;
    ent = cache->free_head->next;
    ent->prev->next = ent->next;
    ent->next->prev = ent->prev;

    pthread_mutex_unlock(&cache->free_head_lock);
    return ent;
}

static lru_entry_t *take_from_free()
{
    lru_entry_t *ent;
    while (!(ent = try_take_from_free()))
    {
        lru_evict_to_free();
    }
    return ent;
}

// ent is an orphan
static void insert_to_file_cache(file_cache_t *fc, off_t off_blk, lru_entry_t *ent, int iswrite)
{
    // if already exists, return ent to the free list
    pthread_rwlock_rdlock(&fc->caches_rw_lock);
    if (g_hash_table_contains(fc->cache_blks, GINT_TO_POINTER(off_blk)))
    {
        pthread_rwlock_unlock(&fc->caches_rw_lock);
        pthread_mutex_lock(&cache->free_head_lock);
        ent->next = cache->free_tail;
        ent->prev = cache->free_tail->prev;
        ent->prev->next = ent;
        ent->next->prev = ent;
        pthread_mutex_unlock(&cache->free_head_lock);
        return;
    }
    pthread_rwlock_unlock(&fc->caches_rw_lock);

    // LOCK ORDER: head_lock FIRST, THEN caches_lock
    
    pthread_mutex_lock(&cache->head_lock); // crossed lock for handle
    
    // update to lru

    ent->removed = 0;
    ent->written = iswrite;

    ent->prev = cache->head;
    ent->next = cache->head->next;
    ent->prev->next = ent;
    ent->next->prev = ent;

    pthread_rwlock_wrlock(&fc->caches_rw_lock);
    // insert to file caches
    g_hash_table_insert(fc->cache_blks, GINT_TO_POINTER(off_blk), ent);
    pthread_mutex_unlock(&cache->head_lock);
    pthread_rwlock_unlock(&fc->caches_rw_lock);
}

static void remove_from_file_cache(file_cache_t *fc, off_t off_blk)
{
    // LOCK ORDER: head_lock FIRST, THEN free_head_lock THEN caches_lock 
    // pthread_mutex_lock(&fc->caches_lock);
    pthread_mutex_lock(&cache->head_lock);
    pthread_mutex_lock(&cache->free_head_lock);
    pthread_rwlock_wrlock(&fc->caches_rw_lock);

    lru_entry_t *ent = (lru_entry_t *)g_hash_table_lookup(fc->cache_blks, GINT_TO_POINTER(off_blk));
    if (ent == NULL)
    {
        pthread_mutex_unlock(&cache->head_lock);
        pthread_mutex_unlock(&cache->free_head_lock);
        pthread_rwlock_unlock(&fc->caches_rw_lock);
        return;
    }
#ifdef SS_DEBUG
    printf("[CACHE] REMOVE blk %d of inode %d\n", off_blk, fc->inode);
#endif
    g_hash_table_remove(fc->cache_blks, GINT_TO_POINTER(off_blk));
    
    // take from lru
    // ent = cache->tail->prev;
    ent->prev->next = ent->next;
    ent->next->prev = ent->prev;
    // put into free

    ent->removed = 1;
    ent->written = 0;

    
    ent->next = cache->free_tail;
    ent->prev = cache->free_tail->prev;
    ent->prev->next = ent;
    ent->next->prev = ent;
    pthread_mutex_unlock(&cache->head_lock);
    pthread_mutex_unlock(&cache->free_head_lock);
    pthread_rwlock_unlock(&fc->caches_rw_lock);
}

int read_cache(int fd, void *buf, size_t len, off_t offset)
{
    TIME_REC_ADD;
    int inode = get_inode(fd);
    file_cache_t *fc = (file_cache_t *)g_hash_table_lookup(cache->files_cache, GINT_TO_POINTER(inode));
    if (!fc)
    {
        fprintf(stderr, "[CACHE ERROR] reading fd %d inode %d that is already closed.\n", fd, inode);
    }
    TIME_REC_ADD;

    int blk_sz = cache->status.blk_sz;
    off_t off_blk = offset / blk_sz;   // which block now
    off_t blk_start = offset % blk_sz; // from where to read
    int len0 = len;                  // length left
    int sz;                          // copy size for each iter

    off_t ra_start_blk;
    off_t ra_end_blk;
    off_t ra_blks = cache->status.ra_before + cache->status.ra_after + 1;

    while (len0 > 0)
    {
        TIME_REC_ADD;
        // pthread_mutex_lock(&fc->caches_lock);
        pthread_rwlock_rdlock(&fc->caches_rw_lock);
        lru_entry_t *ent = (lru_entry_t *)g_hash_table_lookup(fc->cache_blks, GINT_TO_POINTER(off_blk));
        // pthread_mutex_unlock(&fc->caches_lock);
        pthread_rwlock_unlock(&fc->caches_rw_lock);
        TIME_REC_ADD;
        if (ent)
        {
#ifdef SS_DEBUG
            printf("[CACHE] FOUND blk %d of fd %d inode %d\n", off_blk, fd, inode);
#endif
            // data at blk_start in off_blk, move to buf, size is blk_sz-blk_start
            sz = MIN(blk_sz - blk_start, len0);
            ss_copy_from_buf(cache->mempool, buf, sz);
            lru_update(ent);
            buf += sz;
            len0 -= sz;
            off_blk++;
            blk_start = 0;
#ifdef HIT_RATIO
            pthread_mutex_lock(&cache->hr_calc.hr_lock);
            cache->hr_calc.hit++;
            pthread_mutex_unlock(&cache->hr_calc.hr_lock);
#endif
        }
        else
        {
            // read many blks
            if (fc->ra_enable)
            {
                ra_start_blk = off_blk - cache->status.ra_before;
                if (ra_start_blk < 0)
                {
                    ra_start_blk = 0;
                }

                ra_end_blk = ra_start_blk + ra_blks;
#ifdef SS_DEBUG
                printf("[CACHE] READ_AHEAD blk start %d end %d\n", ra_start_blk, ra_end_blk);
#endif
                ss_pread_nocache(fd, cache->mempool, ra_blks * blk_sz, ra_start_blk * blk_sz); // XXX: remove

                for (off_t blk_p = ra_start_blk; blk_p < ra_end_blk; blk_p++)
                {
#ifdef SS_DEBUG
                    printf("[CACHE] READ_AHEAD blk %d of fd %d inode %d\n", blk_p, fd, inode);
#endif
                    lru_entry_t *ent = take_from_free();
                    ent->inode = inode;
                    ent->fd = fd;
                    ent->blk_offset = blk_p;
                    ent->fc = fc;

                    insert_to_file_cache(fc, blk_p, ent, false);
                }
                // do nothing, copy in next round
            }
            // read one blk
            else
            {
                ss_pread_nocache(fd, cache->mempool, blk_sz, off_blk * blk_sz);
#ifdef SS_DEBUG
                printf("[CACHE] READ_ONE blk %d of fd %d inode %d\n", off_blk, fd, inode);
#endif
                lru_entry_t *ent = take_from_free();
                ent->inode = inode;
                ent->fd = fd;
                ent->blk_offset = off_blk;
                ent->fc = fc;

                insert_to_file_cache(fc, off_blk, ent, false);
                // do nothing, copy in next round
            }
#ifdef HIT_RATIO
            pthread_mutex_lock(&cache->hr_calc.hr_lock);
            cache->hr_calc.miss++;
            // hit will be added in the next round, so reduce it first. overflow is ok.
            cache->hr_calc.hit--;
            pthread_mutex_unlock(&cache->hr_calc.hr_lock);
#endif
        }
        TIME_REC_ADD;
    }
    TIME_REC_NEWLN;
    return len;
}

int write_cache_waround(int fd, const void *buf, size_t len, off_t offset)
{
    int inode = get_inode(fd);
    TIME_REC_ADD;
    TIME_REC_ADD;
    file_cache_t *fc = (file_cache_t *)g_hash_table_lookup(cache->files_cache, GINT_TO_POINTER(inode));
    TIME_REC_ADD;

    int blk_sz = cache->status.blk_sz;
    off_t off_blk_start = offset / blk_sz;
    off_t off_blk_end = (offset + len) / blk_sz + 1;
    TIME_REC_ADD;
    for (off_t off_blk = off_blk_start; off_blk < off_blk_end; off_blk++)
    {
        remove_from_file_cache(fc, off_blk);
    }
    TIME_REC_ADD;
    int ret = ss_pwrite_nocache(fd, buf, len, offset, false); // XXX: remove this
    // printf("nocache pw: %d\n", ret);
    TIME_REC_ADD;
    // simulate_latency(50000); // FIXME: decide pwrite latency
    TIME_REC_NEWLN;
    return ret;
}

int write_cache_wwait(int fd, const void *buf, size_t len, off_t offset)
{
    int inode = get_inode(fd);
    file_cache_t *fc = (file_cache_t *)g_hash_table_lookup(cache->files_cache, GINT_TO_POINTER(inode));

    int blk_sz = cache->status.blk_sz;
    off_t off_blk_start = offset / blk_sz;
    off_t off_blk_end = (offset + len) / blk_sz + 1;

    for (off_t off_blk = off_blk_start; off_blk < off_blk_end; off_blk++)
    {
        remove_from_file_cache(fc, off_blk);
    }
    return len;
}

int write_cache_wback(int fd, const void *buf, size_t len, off_t offset)
{
    int inode = get_inode(fd);
    file_cache_t *fc = (file_cache_t *)g_hash_table_lookup(cache->files_cache, GINT_TO_POINTER(inode));

    int blk_sz = cache->status.blk_sz;
    off_t off_blk_start = offset / blk_sz;
    off_t off_blk_end = (offset + len) / blk_sz + 1;

    for (off_t off_blk = off_blk_start; off_blk < off_blk_end; off_blk++)
    {
        // remove_from_file_cache(fc, off_blk);
        // TODO: wback
        pthread_rwlock_rdlock(&fc->caches_rw_lock);
        lru_entry_t *ent = (lru_entry_t *)g_hash_table_lookup(fc->cache_blks, GINT_TO_POINTER(off_blk));
        pthread_rwlock_unlock(&fc->caches_rw_lock);
        if (ent)
        {
            // need lock?
            ent->written = 1;
            lru_update(ent);
        }
        else
        {
            ent = take_from_free();
            ent->inode = inode;
            ent->fd = fd;
            ent->blk_offset = off_blk;
            ent->fc = fc;

            insert_to_file_cache(fc, off_blk, ent, true);
        }
        memcpy(buf, cache->mempool, blk_sz);
    }
    return len;
}

int write_cache(int fd, const void *buf, size_t len, off_t offset)
{
    switch (cache->status.cache_type)
    {
    case WRITE_AROUND:
        return write_cache_waround(fd, buf, len, offset);
    case WRITE_WAIT:
        return write_cache_wwait(fd, buf, len, offset);
    case WRITE_BACK:
        return write_cache_wback(fd, buf, len, offset);
    default:
        return -1;
    }
}

// int read_from_cache(int fd, void *buf, size_t len, off_t offset) {
//     int blk_offset = offset /
//     return 0;
// }

// int write_to_cache(int fd, const void *buf, size_t len, off_t offset) {
//     return 0;
// }

#ifdef SS_DEBUG
void print_cache() // XXX: finalize use this to debug, count this
{
    lru_entry_t *ent = cache->head->next;
    int total = 0;
    printf("head %x tail %x freeh %x freet %x\n", cache->head, cache->tail, cache->free_head, cache->free_tail);
    while (ent != cache->tail)
    {
        printf("[PRT CACHE] %x inode %d blk %ld removed %d\n", ent, ent->inode, ent->blk_offset, ent->removed);
        ent = ent->next;
        total++; // XXX: count
    }
    printf("[PRT CACHE] total %d\n", total);
}
#endif

#ifdef HIT_RATIO
double get_hr()
{
    int h, m;
    pthread_mutex_lock(&cache->hr_calc.hr_lock);
    h = cache->hr_calc.hit;
    m = cache->hr_calc.miss;
    pthread_mutex_unlock(&cache->hr_calc.hr_lock);
    return (double)h / (double)m;
}

void get_hr_detailed(size_t *hit, size_t *miss)
{
    pthread_mutex_lock(&cache->hr_calc.hr_lock);
    *hit = cache->hr_calc.hit;
    *miss = cache->hr_calc.miss;
    pthread_mutex_unlock(&cache->hr_calc.hr_lock);
}

void clear_hr()
{
    pthread_mutex_lock(&cache->hr_calc.hr_lock);
    cache->hr_calc.hit = 0;
    cache->hr_calc.miss = 0;
    pthread_mutex_unlock(&cache->hr_calc.hr_lock);
}
#endif