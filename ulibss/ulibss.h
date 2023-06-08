#ifndef ULIBSS_H
#define ULIBSS_H

#include <stdbool.h>
#include "spdk/nvme.h"

#include "klibss.h"
#include "profile_utils.h"

#ifndef __USE_FILE_OFFSET64
#define __USE_FILE_OFFSET64
#endif
#ifndef __USE_LARGEFILE64
#define __USE_LARGEFILE64
#endif
#ifndef _LARGEFILE64_SOURCE
#define _LARGEFILE64_SOURCE
#endif

// StorStack Debug
// #define SS_DEBUG

/**
 * Raw buffer support.
 *
 * \note By default, StorStack only supports hugepage buffers that are
 * allocated by spdk_dma_malloc(). Turn this on to support buffers
 * that are not allocated by spdk_dma_malloc(). This may harm the
 * performance since there will be more memcopies.
 *
 * \note Extern functions are:
 * \param ss_buf_init()
 * \param ss_buf_clear()
 * \param ss_copy_to_buf()
 * \param ss_copy_from_buf()
 */
// #define RAW_BUF_SUPPORT

/**
 * Pthread Background Daemon
 *
 * With this macro defined, a pthread daemon will be running in the
 * background to handle all the completions of all qpairs with
 * spdk_nvme_qpair_process_completions(), just like the bottom half
 * in OS kernel.
 *
 * Note that the function is not thread safe, it can be called by
 * only one thread at a time. So if there is a daemon running, all
 * other functions in ulibss and ulibss_aio should not call
 * spdk_nvme_qpair_process_completions() any more.
 *
 * \bug FIXME: Problems exist!
 */
// #define PTHREAD_DAEMON

/**
 * StorStack Queue Scheduling Method
 *
 * \param SS_STATIC_SCHEDULING Static binding using hash
 * \param SS_DYNAMIC_SCHEDULING Dynamically allocate depend on core usage
 * \param SS_RANDOM_SCHEDULING Randomized allocate
 */
#define SS_STATIC_SCHEDULING
//#define SS_DYNAMIC_SCHEDULING
//#define SS_RANDOM_SCHEDULING

#ifdef SS_DYNAMIC_SCHEDULING
#define SOCK_NAME "/tmp/ss_scheduler"
#define MAX_PROC 65536
#endif

/**
 * StorStack NVMe QPair Num
 *
 * Each core binds a qpair.
 */
#define SS_QPAIR_NUM 4

/**
 * StorStack fd to qpair_id map size
 *
 * Allocate each fd to a qpair (core).
 * Default value is 1048576, which map size is 4MiB(uint8_t) or 1MiB(uint32_t).
 */
#define SS_INODE_MAP_SIZE 1 << 20

/**
 * StorStack Latency tester flag
 *
 * \param SS_LATENCY_TIMER Using timer to get timestamp (ns) of the point, each call takes about 30 nanoseconds (except first call)
 * \param SS_LATENCY_TIMER_GROUP Timer group size - points should be recorded
 * \param SS_LATENCY_TIMER_SIZE Timer group num
 */
// #define SS_LATENCY_TIMER

#ifdef SS_LATENCY_TIMER
#include <sys/time.h>
#define SS_LATENCY_TIMER_GROUP 10
#define SS_LATENCY_TIMER_SIZE 16384
static inline void __ss_latency_timer(const unsigned int count, unsigned int group);
void ss_latency_print(const unsigned int count, unsigned int group);
#endif

#ifndef MAX(x, y)
#define MAX(x, y) ((x) > (y) ? (x) : (y))
#endif
#ifndef MIN(x, y)
#define MIN(x, y) ((x) < (y) ? (x) : (y))
#endif

// User info for permission check
struct klib_info
{
    uint8_t token[TOKEN_LEN]; // init when spdk init
    uint32_t token_32;
    uint32_t uid;
};

struct ss_aiocb;

struct spdk_nvme_qpair;

extern __thread struct spdk_nvme_qpair *ss_qpair[SS_QPAIR_NUM];

// transfer callback returns
struct cb_ret
{
    volatile uint32_t data;    // return data
    volatile uint32_t inode;   // file inode
    volatile bool isvalid;     // is valid for check
    volatile bool ispermitted; // is permitted for uid check res

    // void   *__to_be_free;
    struct ss_aiocb *__aiocbp;
};

// SPDK NVMe Controller data
struct ss_ctrlr_entry
{
    struct spdk_nvme_ctrlr *ctrlr;
};

// SPDK NVMe Namespace and its related values
struct ss_ns_entry
{
    struct spdk_nvme_ctrlr *ctrlr;
    struct spdk_nvme_ns *ns;
};

// struct for dynamic sched
#ifdef SS_DYNAMIC_SCHEDULING
struct sched_trans_data
{
    int op;  // 0: open; 1: close
    int ino; // inode num
};
#endif

/**
 * StorStack Utils Functions
 *
 * \return struct ss_ns_entry* ns_entry
 */
extern struct ss_ns_entry *get_ns_entry();

/**
 * StorStack Queue Schedule
 * get qpair id of a fd
 *
 * \param fd
 * \return qpair id
 */
extern uint8_t get_qpair_id(int fd);

extern uint32_t get_inode(int fd);

/**
 * StorStack I/O DMA Buffer Functions
 */

/**
 * StorStack I/O Command
 * copy from normal memory to DMA safe memory
 *
 * \param size data size
 * \param data data mem ptr
 * \return buf ptr ptr
 */
extern void *ss_copy_to_buf(int size, const void *data);

/**
 * StorStack I/O Command
 * copy from DMA safe memory to normal memory space
 *
 * \param buf_s DMA safe buffer ptr - data source
 * \param buf normal mem space ptr - destination
 */
extern void ss_copy_from_buf(const void *buf_s, void *buf, int size);

/**
 * StorStack I/O Command
 * init dma safe memory to get ready for data writing
 *
 * \param size buffer size
 * \return buf ptr ptr
 */
extern void *ss_buf_init(size_t size);

/**
 * StorStack I/O Command
 * free dma safe buffer space
 *
 * \param buf the buf ptr which you want to clear
 */
extern void ss_buf_clear(void *buf);

/**
 * StorStack I/O Operation Functions
 */

/**
 * StorStack I/O Command
 * open
 *
 * \param pathname
 * \param flags
 * \param mode
 * \return fd
 */
int ss_open(const char *pathname, int flags, mode_t mode);

/**
 * StorStack I/O Command
 * close
 *
 * \param fd fd
 * \return int -1 for internal error or other value for original close returns
 */
int ss_close(int fd);

/**
 * StorStack I/O Command
 * write
 *
 * \param fd
 * \param buf_raw
 * \param len
 * \return data length
 */
ssize_t ss_write(int fd, const void *buf, size_t len);

/**
 * StorStack I/O Command
 * read
 *
 * \param fd
 * \param buf_raw
 * \param len
 * \return data length
 */
ssize_t ss_read(int fd, void *buf, size_t len);

/**
 * StorStack I/O Command
 * pwrite
 *
 * \param fd
 * \param buf
 * \param len
 * \param offset
 * \return bytes of data
 */
ssize_t ss_pwrite(int fd, const void *buf, size_t len, off_t offset);

/**
 * StorStack I/O Command
 * pread
 *
 * \param fd
 * \param buf
 * \param len
 * \param offset
 * \return bytes of data
 */
ssize_t ss_pread(int fd, void *buf, size_t len, off_t offset);

/**
 * StorStack I/O Command
 * lseek64
 *
 * \param fd
 * \param offset
 * \param whence
 * \return bytes after file start or error -1
 */
int ss_lseek64(int fd, off_t offset, int whence);

/**
 * StorStack I/O Command
 * unlink
 *
 * \param pathname
 * \return exec status
 */
int ss_unlink(const char *pathname);

/**
 * StorStack I/O Command
 * fsync
 *
 * \param fd
 * \return exec status
 */
int ss_fsync(int fd);

/**
 * StorStack I/O Command
 * mkdir
 *
 * \param pathname
 * \param mode
 * \return exec status
 */
int ss_mkdir(const char *pathname, mode_t mode);

/**
 * StorStack I/O Command
 * rmdir
 *
 * \param pathname
 * \return exec status
 */
int ss_rmdir(const char *pathname);

/**
 * StorStack I/O Command
 * stat
 *
 * \param filename
 * \param stats
 * \return exec status
 */
int ss_stat(const char *filename, struct stat *stats);

/**
 * StorStack I/O Operation Latency Simulation Functions
 */

/**
 * StorStack I/O Command
 * latency simulation for normal path
 * with return value from qemu
 *
 * \return res, normally 0
 */
int ss_latency_with_ret();

/**
 * StorStack I/O Command
 * latency simulation for normal path
 * with return value of read from qemu
 *
 * \return res, normally 0
 */
int ss_latency_with_ret_r();

/**
 * StorStack I/O Command
 * latency simulation for normal path
 * with return value of write from qemu
 *
 * \return res, normally 0
 */
int ss_latency_with_ret_w();

/**
 * StorStack I/O Command
 * latency simulation for normal path
 * without return value from qemu
 *
 * \return 0
 */
int ss_latency_no_ret(uint32_t lat);

/**
 * StorStack I/O Operation Functions
 *
 * TODO: Undone funcs
 */

/**
 * StorStack I/O Command
 * fallocate
 * \note undone method!
 *
 * \param fd
 * \param mode
 * \param offset
 * \param len
 * \return exec status
 */
int ss_fallocate(int fd, int mode, off_t offset, off_t len);

/**
 * StorStack I/O Command
 * ftruncate
 *
 * \param fd
 * \param len
 * \return exec status
 * \note NEW FUNC FOR FB
 */
int ss_ftruncate(int fd, off_t len);

/**
 * StorStack I/O Command
 * rename
 * \note undone method!
 *
 * \param oldpath
 * \param newpath
 * \return exec status
 */
int ss_rename(const char *oldpath, const char *newpath);

/**
 * StorStack SPDK Functions
 */

/**
 * StorStack Ulib
 * Initialize Ulib.
 *
 * \return initialization result.
 */
int ss_init_spdk();

/**
 * StorStack Ulib
 * Finalize Ulib.
 *
 * \return finilization result.
 */
int ss_fini_spdk();

/**
 * StorStack Ulib
 * Initialize qpairs for each thread.
 *
 * \warning thread-private, should be initialized in threads.
 * \note this function should only be called when a thread starts.
 * \return initialization result.
 */
int ss_init_qpair();

/**
 * StorStack Ulib
 * Finalize qpairs for each thread.
 *
 * \warning thread-private, should be initialized in threads.
 * \note this function should only be called when a thread is ready to finalize.
 * \return finilization result.
 */
int ss_fini_qpair();

#endif