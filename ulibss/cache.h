#include <unistd.h>

// #define GOOD_SIZE 65536

// #define HIT_RATIO

#define WRITE_AROUND    0
#define WRITE_WAIT      1
#define WRITE_BACK      2


struct cache_attr
{
    size_t cache_sz;    // overall cache size, in bytes, cache_sz % blk_sz should be 0
    size_t blk_sz;      // size of a cache line, in bytes
    bool ra_enable;     // enable read ahead, global setting for all files' default value of ra_enable
    uint32_t ra_before; // blks
    uint32_t ra_after;  // blks
    int cache_type;
};

typedef struct cache_attr cache_attr_t;

int init_cache(cache_attr_t *attr);

int fini_cache();

int open_file_cache(int fd);

int close_file_cache(int fd);

// if in cache, read from it; else, read from disk and save it into cache
int read_cache(int fd, void *buf, size_t len, off_t offset);

// write to disk and drop the cache
int write_cache(int fd, const void *buf, size_t len, off_t offset);

#ifdef HIT_RATIO
double get_hr();
void get_hr_detailed(size_t *hit, size_t *miss);
void clear_hr();
#endif