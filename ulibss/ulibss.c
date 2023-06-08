#include <string.h>

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/stat.h>
#include "spdk/stdinc.h"
#include "spdk/nvme.h"
#include "spdk/nvme_spec.h"
#include "spdk/env.h"
#include "spdk/string.h"
#include "spdk/log.h"
#include "spdk/likely.h"

#include "ulibss.h"
#include "cache.h"

// FORWARD DECLARATIONS FOR PRIVATE FUNCS

static bool __probe_cb(void *cb_ret, const struct spdk_nvme_transport_id *trid, struct spdk_nvme_ctrlr_opts *opts);
static void __attach_cb(void *cb_ret, const struct spdk_nvme_transport_id *trid, struct spdk_nvme_ctrlr *ctrlr, const struct spdk_nvme_ctrlr_opts *opts);
static void __ss_operation_cb(void *arg, const struct spdk_nvme_cpl *completion);

static int __ss_init_klib();
static int __ss_fini_klib();
static void __ss_get_token(u8 *token);
static uint32_t __get_user_id();

static inline uint8_t __get_sched_qpair_id(int fd);
static void __set_sched_qpair_id(int fd, uint32_t inode);
#ifdef SS_DYNAMIC_SCHEDULING
static int __ss_init_sched();
static int __ss_fini_sched();
static int __ss_open_sched(uint32_t inode);
static int __ss_close_sched(uint32_t inode);
#endif

// PRIVATE GLOBAL VARS

static bool use_cache = true; // Use LRU Cache for StorStack?
static bool nowait = true;    // If use cache, should (p)write use aio-like strategy?
                              // FIXME: nowait is failed since qpair is thread-private
// LRU Cache with Read-ahead and aio-like (p)write strategies
static cache_attr_t cattr = {
    .blk_sz = 4096,           // Block size
    .cache_sz = 2 << 30,      // Cache maximum total size
    .ra_before = 7,           // read ahead - read before block num
    .ra_after = 8,            // read ahead - read after block num
    .ra_enable = true,        // read ahead feature
    .cache_type = WRITE_BACK, // Write strategy
};

static int ss_init_flag = 0;                                       // StorStack init threads num, for all threads but not for diff procs // FIXME: use pthread once method
static pthread_mutex_t ss_env_lock = PTHREAD_MUTEX_INITIALIZER;    // StorStack SPDK initializer mutex for NVMe Devices probe
static struct spdk_env_opts env_opts;                              // spdk env opts
static struct ss_ctrlr_entry *ctrlr_entry;                         // ctrlr and ns queue
static struct ss_ns_entry *ns_entry;                               // shared ns_entry for each apps
static struct klib_info *user_info;                                // User info for klib and ops auth
static int klib_fd;                                                // ss klib virtual fd
static uint32_t fd_inode_map[SS_INODE_MAP_SIZE];                   // fd to inode map
static uint8_t fd_qpair_map[SS_INODE_MAP_SIZE];                    // fd to core id (a.k.a qpair id) map
static off_t fd_offset_map[SS_INODE_MAP_SIZE];                     // fd to its current offset
static pthread_mutex_t fd_offset_lock = PTHREAD_MUTEX_INITIALIZER; // offset recoder for simulated read/write
__thread struct spdk_nvme_qpair *ss_qpair[SS_QPAIR_NUM];           // qpair for each thread // XXX: test availbility
#ifdef SS_DYNAMIC_SCHEDULING
static int sched_sock_fd; // ss sched virtual fd for dynamic scheduling
#endif

#ifdef SS_LATENCY_TIMER
extern unsigned int g_timer_count = 0;
static struct timespec g_timer[16384][10];
static inline void __ss_latency_timer(const unsigned int count, unsigned int group)
{
    (void)clock_gettime(CLOCK_MONOTONIC, &g_timer[count][group]);
}
void ss_latency_print(const unsigned int count, unsigned int group)
{
    int i, j;
    for (i = 0; i < count; i++)
    {
        printf("%d\t", i);
        for (j = 0; j < group; j++)
        {
            printf("%ld\t", g_timer[i][j].tv_nsec);
        }
        printf("\n");
    }
}
#endif

// Background Daemon
#ifdef PTHREAD_DAEMON
pthread_t __ss_daemon_p;
void *__ss_daemon()
{
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);

    struct ss_ns_entry *ns = get_ns_entry();
    while (1)
    {
        for (int i = 0; i < SS_QPAIR_NUM; i++)
        {
            spdk_nvme_qpair_process_completions(ns->qpair[i], 0);
        }
        pthread_testcancel();
    }
}
#endif

// LIB

struct ss_ns_entry *get_ns_entry()
{
    return ns_entry;
}

/**
 * StorStack Get UID
 * \return current user id.
 */
static inline uint32_t __get_user_id()
{
    return getuid(); // from unistd.h
}

/**
 * StorStack Klib
 * Initialize Klib.
 *
 * \return initialization result.
 */
static int __ss_init_klib()
{
    klib_fd = open(STORSTACK_DEVNAME, O_RDWR);
    if (spdk_unlikely(klib_fd < 0))
    {
        fprintf(stderr, "[ULIB___ss_init_klib] cannot open klib device: %d", klib_fd);
        return -1;
    }
    return 0;
}

/**
 * StorStack Klib
 * Finalize Klib.
 *
 * \return finalization result.
 */
static inline int __ss_fini_klib()
{
    return close(klib_fd);
}

/**
 * StorStack Klib
 * Get auth token of current user from kernel.
 *
 * \param token write token to *uint8_t* list.
 * \return get token result.
 */
static void __ss_get_token(u8 *token)
{
    // u8 token[TOKEN_LEN];
    int ret;
    if (spdk_unlikely((ret = ioctl(klib_fd, SSIOC_GENERATE_TOKEN, token)) < 0))
    {
        fprintf(stderr, "[ULIB___ss_get_token] ioctl failed: %d\n", ret);
        exit(-1);
    }

#ifdef SS_DEBUG
    printf("[ULIB___ss_get_token] StorStack token: ");
    int i;
    for (i = 0; i < TOKEN_LEN; i++)
    {
        printf("%02x ", token[i]);
    }
    printf("\n");
#endif
}

// StorStack I/O DMA Buffer Functions

void *ss_copy_to_buf(int size, const void *data)
{
    void *buf = spdk_dma_malloc(size, 0x10000, NULL);
    memcpy(buf, data, size);
    return buf;
}

void ss_copy_from_buf(const void *buf_s, void *buf, int size)
{
    if (buf_s == NULL || buf == NULL)
        return;
    memcpy(buf, buf_s, size);
}

void *ss_buf_init(size_t size)
{
    return spdk_dma_malloc(size, 0x10000, NULL);
}

void ss_buf_clear(void *buf)
{
    spdk_free(buf);
}

// SPDK NVMe Driver

/**
 * StorStack Probe Callback Function
 * \note this function is blank for future.
 *
 * \param cb_ret Opaque value passed to spdk_nvme_probe().
 * \param trid NVMe transport identifier. We use PCIe so it is useless.
 * \return probe result.
 */
static bool __probe_cb(void *cb_ret, const struct spdk_nvme_transport_id *trid, struct spdk_nvme_ctrlr_opts *opts)
{
    // nothing to do
#ifdef SS_DEBUG
    printf("[ULIB___probe_cb] Attaching to %s\n", trid->traddr);
#endif
    return true;
}

/**
 * StorStack Attach Callback Function
 *
 * \param cb_ret Opaque value passed to spdk_nvme_attach_cb().
 * \param trid NVMe transport identifier. We use PCIe so it is useless.
 * \param ctrlr Opaque handle to NVMe controller.
 * \param opts NVMe controller initialization options that were actually used. Options may differ from the requested options from the attach call depending on what the controller supports.
 */
static void __attach_cb(void *cb_ret, const struct spdk_nvme_transport_id *trid, struct spdk_nvme_ctrlr *ctrlr, const struct spdk_nvme_ctrlr_opts *opts)
{
    int nsid;
    const struct spdk_nvme_ctrlr_data *tmp_ctrlr_data;
    struct spdk_nvme_ns *ns;

    // attach ctrlr
    ctrlr_entry = malloc(sizeof(struct ss_ctrlr_entry));
    if (spdk_unlikely(ctrlr_entry == NULL))
    {
        perror("[ULIB___attach_cb] Ctrlr entry init malloc");
        goto error_exit;
    }

    tmp_ctrlr_data = spdk_nvme_ctrlr_get_data(ctrlr);

#ifdef SS_DEBUG
    printf("[ULIB___attach_cb] ctrlr_data: %-20.20s (%-20.20s)\n", tmp_ctrlr_data->mn, tmp_ctrlr_data->sn);
#endif

    ctrlr_entry->ctrlr = ctrlr;

    // attach ns
    for (nsid = spdk_nvme_ctrlr_get_first_active_ns(ctrlr); nsid != 0; nsid = spdk_nvme_ctrlr_get_next_active_ns(ctrlr, nsid))
    {
        ns = spdk_nvme_ctrlr_get_ns(ctrlr, nsid);
        if (ns == NULL || !spdk_nvme_ns_is_active(ns))
        {
#ifdef SS_DEBUG
            printf("[ULIB___attach_cb] skip a ns\n");
#endif
            continue;
        }
        ns_entry = malloc(sizeof(struct ss_ns_entry));
        if (spdk_unlikely(ns_entry == NULL))
        {
            perror("[ULIB___attach_cb] Namespace init: ");
            goto error_exit;
        }
        ns_entry->ctrlr = ctrlr;
        ns_entry->ns = ns;
#ifdef SS_DEBUG
        printf("[ULIB___attach_cb] Namespace ID: %d\n", spdk_nvme_ns_get_id(ns));
        printf("[ULIB___attach_cb] Size: %ju\n", spdk_nvme_ns_get_size(ns));
#endif
        break; // only one (the first) ctrlr and ns we need
    }

    if (ns_entry == NULL)
    {
        printf("[ULIB___attach_cb] No namespace can be found!\n");
        goto error_exit;
    }
    return;

error_exit:
    printf("[ULIB___attach_cb] __attach_cb failed!\n");
    ss_fini_spdk();
    exit(-1);
}

/**
 * StorStack I/O Command
 * Return value callback function for public methods
 *
 * \param arg for cb_ret
 * \param completion raw completion struct
 */
static void __ss_operation_cb(void *arg, const struct spdk_nvme_cpl *completion)
{
#ifdef SS_DEBUG
    printf("[ULIB___ss_operation_cb] operation cb start\n");
#endif
    struct cb_ret *cb_ret = arg;
    if (spdk_nvme_cpl_is_error(completion))
    {
        fprintf(stderr, "[ULIB___ss_operation_cb] NVME CPL is error!\n");
        fprintf(stderr, "[ULIB___ss_operation_cb] cpl status: %x AND %x\n", completion->status.sc, completion->status.sct);
    }
    else
    {
        cb_ret->data = completion->cdw0;  // cdw0 is our return value
        cb_ret->inode = completion->cdw1; // cdw1 is file inode in device
        cb_ret->isvalid = true;           // info has been gotten
    }

    // if (cb_ret->__to_be_free)
    // {
    //     ss_buf_clear(cb_ret->__to_be_free);
    //     free(cb_ret);
    // }

#ifdef SS_DEBUG
    printf("[ULIB___ss_operation_cb] operation cb end\n");
#endif
}

/**
 * StorStack I/O Command
 * Return value callback function for public methods with nowait
 *
 * \note this callback func do nothing, except using raw buffer
 * \param arg for cb_ret
 * \param completion raw completion struct
 */
static void __ss_operation_nowait_cb(void *arg, const struct spdk_nvme_cpl *completion)
{
#ifdef SS_DEBUG
    printf("[ULIB___ss_operation_nowait_cb] operation nowait cb end\n");
#endif
// if the buf is allocated in ss, free it
#ifdef RAW_BUF_SUPPORT
    ss_buf_clear(arg);
#endif
    ;
}

// Scheduling

/**
 * StorStack Queue Schedule
 * get qpair id of a fd
 *
 * \param fd
 * \return qpair id
 */
static inline uint8_t __get_sched_qpair_id(int fd)
{
#ifdef SS_RANDOM_SCHEDULING
    return rand() % SS_QPAIR_NUM;
#else
    return fd_qpair_map[fd];
#endif
}

uint8_t get_qpair_id(int fd)
{
    return __get_sched_qpair_id(fd);
}

uint32_t get_inode(int fd)
{
    return fd_inode_map[fd];
}

/**
 * StorStack Queue Schedule
 * set fd's qpair id in map
 *
 * \param fd
 * \param inode
 */
static inline void __set_sched_qpair_id(int fd, uint32_t inode)
{
#if defined SS_DYNAMIC_SCHEDULING
    fd_inode_map[fd] = inode; // store qpair id
    fd_qpair_map[fd] = (uint8_t)__ss_open_sched(inode);
#elif defined SS_STATIC_SCHEDULING
    fd_inode_map[fd] = inode;                           // store inode
    fd_qpair_map[fd] = (uint8_t)(inode % SS_QPAIR_NUM); // store qpair id
#elif defined SS_RANDOM_SCHEDULING
    return;
#endif
}

#ifdef SS_DYNAMIC_SCHEDULING

/**
 * StorStack Dynamic Scheduling Initializer
 *
 * \return Initialization result. 0 is ok and -1 is error
 */
static int __ss_init_sched()
{
    int len, res;
    struct sockaddr_un addr;

    // create a local tcp socket
    sched_sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sched_sock_fd < 0)
        goto err;

    // bind local address
    bzero(&addr, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, SOCK_NAME);
    len = sizeof(addr);

    res = connect(sched_sock_fd, (struct sockaddr *)&addr, len);
    if (res < 0)
        goto err_sock;

    return 0;
err_sock:
    close(sched_sock_fd);
err:
    return -1;
}

/**
 * StorStack Dynamic Scheduling Finalizer
 *
 * \return Finalization result. 0 is ok and -1 is error
 */
static inline int __ss_fini_sched()
{
    return close(sched_sock_fd);
}

/**
 * StorStack Dynamic Scheduling
 * inode register
 *
 * \param inode
 * \return qpair id (a.k.a core id)
 */
static inline int __ss_open_sched(uint32_t inode)
{
    struct sched_trans_data data = {
        .op = 0,
        .ino = inode,
    };

    int ret = 0, ans = 0;
    ret = write(sched_sock_fd, &data, sizeof(data)); // tx
    if (ret < 0)
        goto error;

    ret = read(sched_sock_fd, &ans, sizeof(ans)); // rx
    if (res <= 0)
        goto error;

    return ans;

error:
    return -1;
}

/**
 * StorStack Dynamic Scheduling
 * inode unregister
 *
 * \param inode
 * \return status code
 */
static inline int __ss_close_sched(uint32_t inode)
{
    struct sched_trans_data data = {
        .op = 1,
        .ino = inode,
    };

    int ret = 0, ans = 0;
    ret = write(sched_sock_fd, &data, sizeof(data)); // tx
    if (ret < 0)
        goto error;

    ret = read(sched_sock_fd, &ans, sizeof(ans)); // rx
    if (res <= 0)
        goto error;

    return ans;
error:
    return -1;
}

#endif

// Use a blocking stat cmd to wait until the queues are clear
static int ss_qpair_clear(int qpair_id)
{
    int ret = -ENOMEM;
    struct cb_ret cbret = {};

    struct stat *stats = NULL;
    const char *filename = "/";

    void *dma_buf = filename;
    int len = strlen(filename) + 1;
    dma_buf = ss_copy_to_buf(len, filename);

    while (ret == -ENOMEM)
    {
        spdk_nvme_qpair_process_completions(ss_qpair[qpair_id], 0);

        struct cb_ret *rets = &cbret;
        rets->isvalid = false;
        size_t data_len = strlen(dma_buf) + 1; // path_len

        int buf_size = MAX(sizeof(char) * data_len, sizeof(struct stat)); // need to store either filename or struct stat
        // void *buf = ss_copy_to_buf(buf_size, filename);
        void *buf = dma_buf;

        struct spdk_nvme_cmd cmd = {};
        cmd.opc = SPDK_NVME_OPC_SS_STAT;
        cmd.nsid = spdk_nvme_ns_get_id(ns_entry->ns);
        cmd.uid = user_info->uid;
        cmd.cdw15_token = user_info->token_32;
        cmd.cdw10_ss_path_len = (uint32_t)data_len;

        ret = spdk_nvme_ctrlr_cmd_io_raw(ns_entry->ctrlr, ss_qpair[qpair_id], &cmd, buf, buf_size, __ss_operation_cb, rets);
    }
    if (ret < 0)
    {
        ss_buf_clear(dma_buf);
        return ret;
    }

#ifndef PTHREAD_DAEMON
    while (!cbret.isvalid)
    {
        spdk_nvme_qpair_process_completions(ss_qpair[qpair_id], 0);
    }
#else
    while (!cbret.isvalid)
    {
        ;
    }
#endif

    // #ifdef RAW_BUF_SUPPORT
    ss_buf_clear(dma_buf);
// #endif
#ifdef SS_DEBUG
    printf("[ULIB_ss_stat] ret: %d\n", cbret.data);
#endif
    return 0;
}

// Public Functions

int ss_init_spdk()
{
    srand((unsigned int)time(NULL)); // For ss_open and possibly SS_RANDOM_SCHEDULING
    pthread_mutex_lock(&ss_env_lock);
    if (ss_init_flag == 0)
    {
        ////////// klib init //////////
        int klib_ret = __ss_init_klib();
        if (klib_ret)
        {
            perror("[ULIB_ss_init_spdk] klib init: ");
            goto error_exit;
        }
        user_info = (struct klib_info *)malloc(sizeof(struct klib_info));
        user_info->uid = __get_user_id(); // init uid
        __ss_get_token(user_info->token);

#ifdef SS_DEBUG
        printf("[ULIB_ss_init_spdk] user info get user id: ");
        printf("%x ", user_info->token[0]);
        printf("%x ", user_info->token[1]);
        printf("%x ", user_info->token[2]);
        printf("%x\n", user_info->token[3]);
#endif

        user_info->token_32 = user_info->token[0] |
                              (user_info->token[1] << 8) |
                              (user_info->token[2] << 16) |
                              (user_info->token[3] << 24);
#ifdef SS_DEBUG
        printf("[ULIB_ss_init_spdk] user info get user id (32 bits): ");
        printf("%x\n", user_info->token_32);
#endif

#ifdef SS_DEBUG
        printf("[ULIB___ss_init_klib] init_klib and token ok!\n");
#endif

        ////////// env opts init //////////
        // env_opts = (struct spdk_env_opts *)malloc(sizeof(struct spdk_env_opts)); // init env opts mem
        spdk_env_opts_init(&env_opts);
        env_opts.name = "StorStack Ulib";
        env_opts.shm_id = 1; // FIXME: should all programs use the same SPDK env?

#ifdef SS_DEBUG
        printf("[ULIB_ss_init_spdk] spdk_env_opts_init ok!\n");
#endif
        int init_opt_ret = spdk_env_init(&env_opts);
        if (init_opt_ret < 0)
        {
            fprintf(stderr, "[ULIB_ss_init_spdk] env init failed: %d\n", init_opt_ret);
            goto error_exit;
        }

#ifdef SS_DEBUG
        printf("[ULIB_ss_init_spdk] spdk_env_init ok!\n");
#endif
        // XXX: Will CPU affinity affect performance? A: Yes, and we should use this func to unlock dpdk CPU bind.
        spdk_unaffinitize_thread();

        // use probe to find ctrlr and ns
        int probe_ret = spdk_nvme_probe(NULL, NULL, __probe_cb, __attach_cb, NULL);

        if (probe_ret)
        {
            fprintf(stderr, "[ULIB_ss_init_spdk] NVMe probe failed: %d\n", probe_ret);
            goto error_exit;
        }

#ifdef SS_DEBUG
        printf("[ULIB_ss_init_spdk] spdk_nvme_probe ok!\n");
#endif

        // Ctrlr Init
        // check after probe
        if (ctrlr_entry == NULL || ns_entry == NULL)
        {
            fprintf(stderr, "[ULIB_ss_init_spdk] Controllers not found!\n");
            goto error_exit;
        }

        // Cache Init
        if (use_cache)
        {
            init_cache(&cattr);
        }

        // Daemon Init
#ifdef PTHREAD_DAEMON
        pthread_create(&__ss_daemon_p, NULL, __ss_daemon, NULL);
#ifdef SS_DEBUG
        printf("[ULIB_ss_init_spdk] daemon started!\n");
#endif
#endif

        // Scheduling Init
#if defined SS_DYNAMIC_SCHEDULING
        int sched_ret = __ss_init_sched();
        if (sched_ret)
            goto error_exit;
#ifdef SS_DEBUG
        printf("[ULIB_ss_init_spdk] Init Dynamic Scheduling ok!\n");
#endif
#elif defined SS_STATIC_SCHEDULING
#ifdef SS_DEBUG
        printf("[ULIB_ss_init_spdk] Using Static Scheduling, skip sched init.\n");
#endif
#endif
    }

    ss_init_flag++; // count++ to prevent duplicate init
    pthread_mutex_unlock(&ss_env_lock);

    return 0;

error_exit:
    fprintf(stderr, "[ULIB_ss_init_spdk] Error exit\n");
    pthread_mutex_unlock(&ss_env_lock);
    return -1;
}

int ss_init_qpair()
{

    // Queue Pair Init
    // qpair is thread-private, threads should init their own qpairs
    int i;
    for (i = 0; i < SS_QPAIR_NUM; i++)
    {
        #ifdef SS_DEBUG
        printf("[ULIB_ss_init_spdk] qpair create No.%d\n", i);
        #endif
        struct spdk_nvme_io_qpair_opts qpair_opt;
        spdk_nvme_ctrlr_get_default_io_qpair_opts(ns_entry->ctrlr, &qpair_opt, sizeof(struct spdk_nvme_io_qpair_opts));
        qpair_opt.io_queue_size = 2048; // XXX: Set io_queue_size to UINT16_MAX, NVMe driver will then
                                        // reduce this to MQES to maximize the io_queue_size as much as possible.
        qpair_opt.reserved66[0] = i;    // XXX: Core Bindings, it is in spdk_nvme_ctrlr_alloc_io_qpair
                                        // It seems that core binding is automatically done
                                        // in term of active proc in ctrlr

        ss_qpair[i] = spdk_nvme_ctrlr_alloc_io_qpair(ns_entry->ctrlr, &qpair_opt, sizeof(struct spdk_nvme_io_qpair_opts));

        if (ss_qpair[i] == NULL)
        {
            fprintf(stderr, "[ULIB_ss_init_spdk] NVMe QPair init failed\n");
            return -1;
        }
        // FIXME: nowait is failed
        // if (nowait)
        // {
        //     ss_qpair_clear(i);
        // }
    }

#ifdef SS_DEBUG
    printf("[ULIB_ss_init_spdk] spdk_nvme_ctrlr_alloc_io_qpair ok!\n");
#endif

    return 0;
}

int ss_fini_qpair()
{
    for (int i = 0; i < SS_QPAIR_NUM; i++)
    {
        // clear the qpair contents         // FIXME: nowait is failed
        // if (nowait)
        // {
        //     ss_qpair_clear(i);
        // }

#ifdef SS_DEBUG
        printf("[ULIB_ss_fini_spdk] before free qp %d.\n", i);
#endif
        // XXX: qpairs should be empty before free them.
        // ss_qpair_clear(i);
        spdk_nvme_ctrlr_free_io_qpair(ss_qpair[i]);
#ifdef SS_DEBUG
        printf("[ULIB_ss_fini_spdk] after free qp %d.\n", i);
#endif
    }

#ifdef SS_DEBUG
    printf("[ULIB_ss_fini_spdk] Qpairs removed.\n");
#endif

    return 0;
}

int ss_fini_spdk()
{
#ifdef SS_DEBUG
    printf("[ULIB_ss_fini_spdk] Finalizer.\n");
#endif
    pthread_mutex_lock(&ss_env_lock); // env finalize lock
    ss_init_flag--;
    if (ss_init_flag == 0) // XXX: only the last running thread should do this
    {
#ifdef SS_DEBUG
        printf("[ULIB_ss_fini_spdk] Finalizing ss.\n");
#endif
#ifdef SS_DYNAMIC_SCHEDULING
        __ss_fini_sched(); // Scheduler Fini
#endif

#ifdef PTHREAD_DAEMON
        pthread_cancel(__ss_daemon_p);
        pthread_join(__ss_daemon_p, NULL);
#ifdef SS_DEBUG
        printf("[ULIB_ss_fini_spdk] Daemon stopped.\n");
#endif
#endif

        if (use_cache)
        {
            fini_cache();
        }

        if (ns_entry != NULL)
        {
            free(ns_entry); // only one namespace, so use free once is ok.
            ns_entry = NULL;
        }

        // former one
        // spdk_nvme_detach_async(static_ctrlr_entry->ctrlr, &detach_ctx);
        // only one ctrlr should be detached.
        spdk_nvme_detach(ctrlr_entry->ctrlr);
        if (ctrlr_entry != NULL)
        {
            free(ctrlr_entry);
            ctrlr_entry = NULL;
        }
        if (user_info != NULL)
        {
            free(user_info);
            user_info = NULL;
        }
        // if (env_opts != NULL)
        // {
        //     free(env_opts);
        //     env_opts = NULL;
        // }

#ifdef SS_DYNAMIC_SCHEDULING
        __ss_fini_sched(); // Scheduler Fini
#endif
        __ss_fini_klib();
        spdk_env_fini();
    }
    pthread_mutex_unlock(&ss_env_lock);
#ifdef SS_DEBUG
    printf("[ULIB_ss_fini_spdk] Finished.\n");
#endif
    return 0;
}

/**
 * StorStack I/O Command
 * open
 *
 * \param pathname, assume that pathname is allocated by spdk_dma_malloc()
 * \param flags
 * \param mode
 * \return fd
 */
int ss_open_raw(const char *pathname, int flags, mode_t mode, struct cb_ret *rets, int qpair_id, spdk_nvme_cmd_cb cbfn)
{
    rets->isvalid = false;
    int buf_size = sizeof(char) * (strlen(pathname) + 1);
    void *buf = pathname;
    if (cbfn == NULL)
    {
        cbfn = __ss_operation_cb;
    }

    struct spdk_nvme_cmd cmd = {
        .opc = SPDK_NVME_OPC_SS_OPEN,
        .nsid = spdk_nvme_ns_get_id(ns_entry->ns),
        .uid = user_info->uid,
        .cdw15_token = user_info->token_32,
        .cdw10_ss_path_len = (uint32_t)(strlen(pathname) + 1), // with '\0'
        .cdw11_ss_open_flags = (uint32_t)flags,
        .cdw12_ss_file_mode = (uint32_t)mode,
    };

#ifdef SS_DEBUG
    printf("[ULIB_ss_open] uid: %d, token32: %x\n", (int)cmd.uid, (int)cmd.cdw15_token);
    printf("[ULIB_ss_open] pathname: '%s', strlen(except  \\0): %d\n", pathname, (int)strlen(pathname));
    printf("[ULIB_ss_open] OPEN rand() qpair id = %d\n", qpair_id);
#endif

    int cmd_ret = spdk_nvme_ctrlr_cmd_io_raw(ns_entry->ctrlr, ss_qpair[qpair_id], &cmd, buf, buf_size, cbfn, rets);
    
#ifdef SS_DEBUG
    printf("[ULIB_ss_open] OPEN io raw ok\n");
    if (cmd_ret)
    {
        printf("[ULIB_ss_open] ss_open cmd io ret < 0!\n");
    }
#endif
    // ss_buf_clear(buf);
    return cmd_ret;
}

/**
 * StorStack I/O Command
 * close
 *
 * \param fd fd
 * \return int -1 for internal error or other value for original close returns
 */
int ss_close_raw(int fd, struct cb_ret *rets, int qpair_id, spdk_nvme_cmd_cb cbfn)
{
    rets->isvalid = false;
    if (cbfn == NULL)
    {
        cbfn = __ss_operation_cb;
    }

    struct spdk_nvme_cmd cmd = {
        .opc = SPDK_NVME_OPC_SS_CLOSE,
        .nsid = spdk_nvme_ns_get_id(ns_entry->ns),
        .uid = user_info->uid,
        .cdw15_token = user_info->token_32,
        .cdw10_ss_file_fd = (uint32_t)fd,
    };

    // int qpair_id = __get_sched_qpair_id(fd);

#ifdef SS_DEBUG
    printf("[ULIB_ss_close] ss_close fd: %d\n", fd);
#endif

    int cmd_ret = spdk_nvme_ctrlr_cmd_io_raw(ns_entry->ctrlr, ss_qpair[qpair_id], &cmd, NULL, 0, cbfn, rets);
#ifdef SS_DEBUG
    if (cmd_ret)
    {
        printf("[ULIB_ss_close] ss_close cmd ret < 0\n");
    }
#endif
    return cmd_ret;
}

/**
 * StorStack I/O Command
 * write
 *
 * \param fd
 * \param buf_raw
 * \param len
 * \return data length
 */
int ss_write_raw(int fd, const void *buf, size_t len, struct cb_ret *rets, int qpair_id, spdk_nvme_cmd_cb cbfn)
{
    rets->isvalid = false;
    int buf_size = len;

    void *cbarg = (void *)rets;
    if (cbfn == NULL)
    {
        cbfn = __ss_operation_cb;
    }
    else if (cbfn == __ss_operation_nowait_cb)
    {
        // for async calls, just clear the buffer. the return value is dropped
        cbarg = buf;
    }

    struct spdk_nvme_cmd cmd = {
        .opc = SPDK_NVME_OPC_SS_WRITE,
        .nsid = spdk_nvme_ns_get_id(ns_entry->ns),
        .uid = user_info->uid,
        .cdw15_token = user_info->token_32,
        .cdw10_ss_file_fd = (uint32_t)fd,
        .cdw11_ss_buffer_len = (uint32_t)len,
    };

#ifdef SS_DEBUG
    printf("[ULIB_ss_write] fd: %d, buflen: %ld\n", fd, len);
#endif

    int cmd_ret = spdk_nvme_ctrlr_cmd_io_raw(ns_entry->ctrlr, ss_qpair[qpair_id], &cmd, buf, buf_size, cbfn, cbarg);
#ifdef SS_DEBUG
    if (cmd_ret)
    {
        printf("[ULIB_ss_write] ss_write cmd ret < 0\n");
    }
#endif
#ifdef SS_DEBUG
    printf("[ULIB_ss_write] write cmd issued.\n");
#endif
    return cmd_ret;
}

/**
 * StorStack I/O Command
 * read
 *
 * \param fd
 * \param buf_raw
 * \param len
 * \return data length
 */
int ss_read_raw(int fd, void *buf, size_t len, struct cb_ret *rets, int qpair_id, spdk_nvme_cmd_cb cbfn)
{
    rets->isvalid = false;
    int buf_size = len;
    if (cbfn == NULL)
    {
        cbfn = __ss_operation_cb;
    }

    struct spdk_nvme_cmd cmd = {
        .opc = SPDK_NVME_OPC_SS_READ,
        .nsid = spdk_nvme_ns_get_id(ns_entry->ns),
        .uid = user_info->uid,
        .cdw10_ss_file_fd = (uint32_t)fd,
        .cdw11_ss_buffer_len = (uint32_t)len,
    };

#ifdef SS_DEBUG
    printf("[ULIB_ss_read] fd: %d, buflen: %ld\n", fd, len);
#endif

    int cmd_ret = spdk_nvme_ctrlr_cmd_io_raw(ns_entry->ctrlr, ss_qpair[qpair_id], &cmd, buf, buf_size, cbfn, rets);
#ifdef SS_DEBUG
    if (cmd_ret)
    {
        printf("[ULIB_ss_read] ss_read cmd ret < 0\n");
    }
#endif
    return cmd_ret;
}

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
int ss_pwrite_raw(int fd, const void *buf, size_t len, off_t offset, struct cb_ret *rets, int qpair_id, spdk_nvme_cmd_cb cbfn)
{
    rets->isvalid = false;
    int buf_size = len;

    void *cbarg = (void *)rets;
    if (cbfn == NULL)
    {
        cbfn = __ss_operation_cb;
    }
    else if (cbfn == __ss_operation_nowait_cb)
    {
        // for async calls, just clear the buffer. the return value is dropped
        cbarg = buf;
    }

    struct spdk_nvme_cmd cmd = {
        .opc = SPDK_NVME_OPC_SS_PWRITE,
        .nsid = spdk_nvme_ns_get_id(ns_entry->ns),
        .uid = user_info->uid,
        .cdw15_token = user_info->token_32,
        .cdw10_ss_file_fd = (uint32_t)fd,
        .cdw11_ss_buffer_len = (uint32_t)len,
        .cdw12_ss_offset = (uint32_t)offset,
    };

#ifdef SS_DEBUG
    printf("[ULIB_ss_pwrite] ss_pwrite start IO (INTO LOCK)\n");
#endif

    int cmd_ret = spdk_nvme_ctrlr_cmd_io_raw(ns_entry->ctrlr, ss_qpair[qpair_id], &cmd, buf, buf_size, cbfn, cbarg);
#ifdef SS_DEBUG
    printf("[ULIB_ss_pwrite] ss_pwrite IO ended (OUT LOCK)\n");
#endif
#ifdef SS_DEBUG
    if (cmd_ret)
    {
        printf("[ULIB_ss_pwrite] ss_pwrite cmd ret < 0\n");
    }
#endif
    return cmd_ret;
}

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
int ss_pread_raw(int fd, void *buf, size_t len, off_t offset, struct cb_ret *rets, int qpair_id, spdk_nvme_cmd_cb cbfn)
{
    rets->isvalid = false;
    int buf_size = len;
    if (cbfn == NULL)
    {
        cbfn = __ss_operation_cb;
    }

    struct spdk_nvme_cmd cmd = {
        .opc = SPDK_NVME_OPC_SS_PREAD,
        .nsid = spdk_nvme_ns_get_id(ns_entry->ns),
        .uid = user_info->uid,
        .cdw15_token = user_info->token_32,
        .cdw10_ss_file_fd = (uint32_t)fd,
        .cdw11_ss_buffer_len = (uint32_t)len,
        .cdw12_ss_offset = (uint32_t)offset,
    };

#ifdef SS_DEBUG
    printf("[ULIB_ss_pread] ss_pread start IO (INTO LOCK)\n");
#endif

    int cmd_ret = spdk_nvme_ctrlr_cmd_io_raw(ns_entry->ctrlr, ss_qpair[qpair_id], &cmd, buf, buf_size, cbfn, rets);
#ifdef SS_DEBUG
    printf("[ULIB_ss_pread] ss_pread start IO (OUT LOCK)\n");
#endif
#ifdef SS_DEBUG
    if (cmd_ret)
    {
        printf("[ULIB_ss_pread] ss_pread cmd ret < 0\n");
    }
#endif
    return cmd_ret;
}

/**
 * StorStack I/O Command
 * lseek64
 *
 * \param fd
 * \param offset
 * \param whence
 * \return bytes after file start or error -1
 */
int ss_lseek64_raw(int fd, off_t offset, int whence, struct cb_ret *rets, int qpair_id, spdk_nvme_cmd_cb cbfn)
{
    // struct cb_ret rets = {};
    rets->isvalid = false;
    if (cbfn == NULL)
    {
        cbfn = __ss_operation_cb;
    }

    struct spdk_nvme_cmd cmd = {
        .opc = SPDK_NVME_OPC_SS_LSEEK,
        .nsid = spdk_nvme_ns_get_id(ns_entry->ns),
        .uid = user_info->uid,
        .cdw15_token = user_info->token_32,
        .cdw10_ss_file_fd = (uint32_t)fd,
        .cdw12_ss_offset = (uint32_t)offset,
        .cdw13_ss_whence = (uint32_t)whence,
    };

    int cmd_ret = spdk_nvme_ctrlr_cmd_io_raw(ns_entry->ctrlr, ss_qpair[qpair_id], &cmd, NULL, 0, cbfn, rets);
#ifdef SS_DEBUG
    if (cmd_ret)
    {
        printf("[ULIB_ss_lseek64] ss_lseek64 cmd ret < 0\n");
    }
#endif
    return cmd_ret;
}

/**
 * StorStack I/O Command
 * unlink
 *
 * \param pathname
 * \return exec status
 */
int ss_unlink_raw(const char *pathname, struct cb_ret *rets, int qpair_id, spdk_nvme_cmd_cb cbfn)
{
    // struct cb_ret rets = {};
    rets->isvalid = false;
    int buf_size = sizeof(*pathname) * (strlen(pathname) + 1);
    // void *buf = ss_copy_to_buf(buf_size, pathname);
    void *buf = pathname;
    if (cbfn == NULL)
    {
        cbfn = __ss_operation_cb;
    }

    struct spdk_nvme_cmd cmd = {
        .opc = SPDK_NVME_OPC_SS_UNLINK,
        .nsid = spdk_nvme_ns_get_id(ns_entry->ns),
        .uid = user_info->uid,
        .cdw15_token = user_info->token_32,
        .cdw10_ss_path_len = (uint32_t)(strlen(pathname) + 1),
    };

    int cmd_ret = spdk_nvme_ctrlr_cmd_io_raw(ns_entry->ctrlr, ss_qpair[qpair_id], &cmd, buf, buf_size, cbfn, rets);
#ifdef SS_DEBUG
    if (cmd_ret)
    {
        printf("[ULIB_ss_unlink] ss_unlink cmd ret < 0\n");
    }
#endif
    // ss_buf_clear(buf);
    return cmd_ret;
}

/**
 * StorStack I/O Command
 * fsync
 *
 * \param fd
 * \return exec status
 */
int ss_fsync_raw(int fd, struct cb_ret *rets, int qpair_id, spdk_nvme_cmd_cb cbfn)
{
    // struct cb_ret rets = {};
    rets->isvalid = false;
    if (cbfn == NULL)
    {
        cbfn = __ss_operation_cb;
    }

    struct spdk_nvme_cmd cmd = {
        .opc = SPDK_NVME_OPC_SS_FSYNC,
        .nsid = spdk_nvme_ns_get_id(ns_entry->ns),
        .uid = user_info->uid,
        .cdw15_token = user_info->token_32,
        .cdw10_ss_file_fd = (uint32_t)fd,
    };

    int cmd_ret = spdk_nvme_ctrlr_cmd_io_raw(ns_entry->ctrlr, ss_qpair[qpair_id], &cmd, NULL, 0, cbfn, rets);
#ifdef SS_DEBUG
    if (cmd_ret)
    {
        printf("[ULIB_ss_fsync] ss_fsync cmd ret < 0\n");
    }
#endif
    return cmd_ret;
}

/**
 * StorStack I/O Command
 * mkdir
 *
 * \param pathname
 * \param mode
 * \return exec status
 */
int ss_mkdir_raw(const char *pathname, mode_t mode, struct cb_ret *rets, int qpair_id, spdk_nvme_cmd_cb cbfn)
{
    // struct cb_ret rets = {};
    rets->isvalid = false;
    int buf_size = sizeof(*pathname) * (strlen(pathname) + 1);
    // void *buf = ss_copy_to_buf(buf_size, pathname);
    void *buf = pathname;
    if (cbfn == NULL)
    {
        cbfn = __ss_operation_cb;
    }

    struct spdk_nvme_cmd cmd = {
        .opc = SPDK_NVME_OPC_SS_MKDIR,
        .nsid = spdk_nvme_ns_get_id(ns_entry->ns),
        .uid = user_info->uid,
        .cdw15_token = user_info->token_32,
        .cdw10_ss_path_len = (uint32_t)(strlen(pathname) + 1),
        .cdw12_ss_file_mode = (uint32_t)mode,
    };

    int cmd_ret = spdk_nvme_ctrlr_cmd_io_raw(ns_entry->ctrlr, ss_qpair[qpair_id], &cmd, buf, buf_size, cbfn, rets);
#ifdef SS_DEBUG
    if (cmd_ret)
    {
        printf("[ULIB_ss_mkdir] ss_mkdir cmd ret < 0\n");
    }
#endif
    // ss_buf_clear(buf);
    return cmd_ret;
}

/**
 * StorStack I/O Command
 * rmdir
 *
 * \param pathname
 * \return exec status
 */
int ss_rmdir_raw(const char *pathname, struct cb_ret *rets, int qpair_id, spdk_nvme_cmd_cb cbfn)
{
    // struct cb_ret rets = {};
    rets->isvalid = false;
    int buf_size = sizeof(*pathname) * (strlen(pathname) + 1);
    // void *buf = ss_copy_to_buf(buf_size, pathname);
    void *buf = pathname;
    if (cbfn == NULL)
    {
        cbfn = __ss_operation_cb;
    }

    struct spdk_nvme_cmd cmd = {
        .opc = SPDK_NVME_OPC_SS_RMDIR,
        .nsid = spdk_nvme_ns_get_id(ns_entry->ns),
        .uid = user_info->uid,
        .cdw15_token = user_info->token_32,
        .cdw10_ss_path_len = (uint32_t)(strlen(pathname) + 1),
    };

    int cmd_ret = spdk_nvme_ctrlr_cmd_io_raw(ns_entry->ctrlr, ss_qpair[qpair_id], &cmd, buf, buf_size, cbfn, rets);
#ifdef SS_DEBUG
    if (cmd_ret)
    {
        printf("[ULIB_ss_rmdir] ss_rmdir cmd ret < 0\n");
    }
#endif
    // ss_buf_clear(buf);
    return cmd_ret;
}

/**
 * StorStack I/O Command
 * stat
 *
 * \param filename
 * \param stats
 * \return exec status
 */
int ss_stat_raw(const char *filename, struct stat *stats, struct cb_ret *rets, int qpair_id, spdk_nvme_cmd_cb cbfn)
{
#ifdef SS_DEBUG
    printf("[ULIB_ss_stat] stat size: %d, stat64 size: %d\n", sizeof(struct stat), sizeof(struct stat64));
#endif

    // struct cb_ret rets = {};
    rets->isvalid = false;
    size_t data_len = strlen(filename) + 1; // path_len

    int buf_size = MAX(sizeof(char) * data_len, sizeof(struct stat)); // need to store either filename or struct stat
    // void *buf = ss_copy_to_buf(buf_size, filename);
    void *buf = filename;
    if (cbfn == NULL)
    {
        cbfn = __ss_operation_cb;
    }

    struct spdk_nvme_cmd cmd = {
        .opc = SPDK_NVME_OPC_SS_STAT,
        .nsid = spdk_nvme_ns_get_id(ns_entry->ns),
        .uid = user_info->uid,
        .cdw15_token = user_info->token_32,
        .cdw10_ss_path_len = (uint32_t)data_len,
    };

    int cmd_ret = spdk_nvme_ctrlr_cmd_io_raw(ns_entry->ctrlr, ss_qpair[qpair_id], &cmd, buf, buf_size, cbfn, rets);
#ifdef SS_DEBUG
    if (cmd_ret)
    {
        printf("[ULIB_ss_stat] ss_stat cmd ret < 0\n");
    }
#endif
    // ss_buf_clear(buf);
    return cmd_ret;
}

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
int ss_fallocate_raw(int fd, int mode, off_t offset, off_t len, struct cb_ret *rets, int qpair_id, spdk_nvme_cmd_cb cbfn)
{
    // struct cb_ret rets = {};
    rets->isvalid = false;
    if (cbfn == NULL)
    {
        cbfn = __ss_operation_cb;
    }

    struct spdk_nvme_cmd cmd = {
        .opc = SPDK_NVME_OPC_SS_FALLOCATE,
        .nsid = spdk_nvme_ns_get_id(ns_entry->ns),
        .uid = user_info->uid,
        .cdw15_token = user_info->token_32,
        .cdw10_ss_file_fd = (uint32_t)fd,
        .cdw12_ss_offset = (uint32_t)offset,
        .cdw13_ss_allocate_mode = (uint32_t)mode,
        .cdw14_ss_space_len = (uint32_t)len,
    };

    int cmd_ret = spdk_nvme_ctrlr_cmd_io_raw(ns_entry->ctrlr, ss_qpair[qpair_id], &cmd, NULL, 0, cbfn, rets);
#ifdef SS_DEBUG
    if (cmd_ret)
    {
        printf("[ULIB_ss_fallocate] ss_fallocate cmd ret < 0\n");
    }
#endif
    return cmd_ret;
}

/**
 * StorStack I/O Command
 * ftruncate
 *
 * \param fd
 * \param len
 * \return exec status
 * \note NEW FUNC FOR FB
 */
int ss_ftruncate_raw(int fd, off_t len, struct cb_ret *rets, int qpair_id, spdk_nvme_cmd_cb cbfn)
{
    // struct cb_ret rets = {};
    rets->isvalid = false;
    if (cbfn == NULL)
    {
        cbfn = __ss_operation_cb;
    }

    struct spdk_nvme_cmd cmd = {
        .opc = SPDK_NVME_OPC_SS_FTRUNCATE,
        .nsid = spdk_nvme_ns_get_id(ns_entry->ns),
        .uid = user_info->uid,
        .cdw15_token = user_info->token_32,
        .cdw10_ss_file_fd = (uint32_t)fd,
        .cdw13_ss_file_len = (uint32_t)len,
    };

    // int qpair_id = __get_sched_qpair_id(fd);

    int cmd_ret = spdk_nvme_ctrlr_cmd_io_raw(ns_entry->ctrlr, ss_qpair[qpair_id], &cmd, NULL, 0, cbfn, rets);
#ifdef SS_DEBUG
    if (cmd_ret)
    {
        printf("[ULIB_ss_ftruncate] ss_ftruncate cmd ret < 0\n");
    }
#endif
    return cmd_ret;
}

/**
 * StorStack I/O Command
 * rename
 * \note undone method!
 *
 * \param oldpath
 * \param newpath
 * \return exec status
 */
int ss_rename_raw(const char *oldpath, const char *newpath, struct cb_ret *rets, int qpair_id, spdk_nvme_cmd_cb cbfn)
{
    // TODO: complete this func
    return -11;
}

int ss_open(const char *pathname, int flags, mode_t mode)
{
    int ret;
    struct cb_ret cbret = {};

    void *dma_buf;
    size_t len = strlen(pathname) + 1;
    dma_buf = ss_copy_to_buf(len * sizeof(char), pathname);

    int qpair_id = rand() % SS_QPAIR_NUM;
    // if ((ret = ss_open_raw(dma_buf, flags, mode, &cbret, qpair_id, NULL)) < 0)
    // {
    //     return ret;
    // }
    // the queue may be full and thus -ENOMEM is returned
    while ((ret = ss_open_raw(dma_buf, flags, mode, &cbret, qpair_id, NULL)) == -ENOMEM)
    {
        // clear the queue and retry

        spdk_nvme_qpair_process_completions(ss_qpair[qpair_id], 0);
    }
    if (ret < 0)
    {
        // #ifdef RAW_BUF_SUPPORT
        ss_buf_clear(dma_buf);
        // #endif
        return ret;
    }

#ifndef PTHREAD_DAEMON

    while (!cbret.isvalid)
    {

        spdk_nvme_qpair_process_completions(ss_qpair[qpair_id], 0);
    }

#else
    while (!cbret.isvalid)
    {
        ;
    }
#endif

    // #ifdef RAW_BUF_SUPPORT
    ss_buf_clear(dma_buf);
// #endif
#ifdef SS_DEBUG
    printf("[ULIB_ss_open] open rets from main dev: %d, %d\n", cbret.data, cbret.inode);
#endif

    __set_sched_qpair_id(cbret.data, cbret.inode); // get and store data

    pthread_mutex_lock(&fd_offset_lock);
    fd_offset_map[cbret.data] = 0;
    pthread_mutex_unlock(&fd_offset_lock);
    if (use_cache)
    {
        open_file_cache(cbret.data);
    }

    return cbret.data;
}

int ss_close(int fd)
{
    int ret;
    struct cb_ret cbret = {};

    if (use_cache)
    {
        close_file_cache(fd);
    }

    int qpair_id = __get_sched_qpair_id(fd);
    // if ((ret = ss_close_raw(fd, &cbret, qpair_id, NULL)) < 0)
    // {
    //     return ret;
    // }
    // the queue may be full and thus -ENOMEM is returned
    while ((ret = ss_close_raw(fd, &cbret, qpair_id, NULL)) == -ENOMEM)
    {
        // clear the queue and retry
#ifdef SS_DEBUG
        printf("[ULIB_ss_close] ss_close in completions cycle\n");
#endif

#ifdef SS_DEBUG
        printf("[ULIB_ss_close] ss_close in completions cycle IN LOCK\n");
#endif
        spdk_nvme_qpair_process_completions(ss_qpair[qpair_id], 0);
#ifdef SS_DEBUG
        printf("[ULIB_ss_close] ss_close in completions cycle OUT LOCK\n");
#endif
    }
    if (ret < 0)
    {
        return ret;
    }

#ifndef PTHREAD_DAEMON

    while (!cbret.isvalid)
    {
#ifdef SS_DEBUG
        printf("[ULIB_ss_close] ss_close in completions cycle\n");
#endif

#ifdef SS_DEBUG
        printf("[ULIB_ss_close] ss_close in completions cycle IN LOCK\n");
#endif
        spdk_nvme_qpair_process_completions(ss_qpair[qpair_id], 0);
#ifdef SS_DEBUG
        printf("[ULIB_ss_close] ss_close in completions cycle OUT LOCK\n");
#endif
    }

#else
    while (!cbret.isvalid)
    {
        ;
    }
#endif

#ifdef SS_DYNAMIC_SCHEDULING
    int sched_ret = __ss_close_sched(fd_inode_map[fd]);
#ifdef SS_DEBUG
    printf("[ULIB_ss_close] ss_close cmd sched exec res: %d\n", sched_ret);
#endif
#endif

    return cbret.data;
}

ssize_t ss_write_nocache(int fd, const void *buf, size_t len, bool wait)
{
    int ret;
    struct cb_ret cbret = {};
    void *dma_buf = buf; // the buf should be allocated by spdk_dma_malloc()
#ifdef RAW_BUF_SUPPORT
#ifdef SS_DEBUG
    printf("[ULIB_ss_write] buf: %x\n", buf);
#endif
    dma_buf = ss_copy_to_buf(len, buf);
#ifdef SS_DEBUG
    printf("[ULIB_ss_write] dma_buf: %x\n", dma_buf);
#endif
#endif
    int qpair_id = __get_sched_qpair_id(fd);

    // nowait
    if (nowait && !wait)
    {
        // the queue may be full and thus -ENOMEM is returned
        while ((ret = ss_write_raw(fd, dma_buf, len, &cbret, qpair_id, __ss_operation_nowait_cb)) == -ENOMEM)
        {
            // clear the queue and retry

            spdk_nvme_qpair_process_completions(ss_qpair[qpair_id], 0);
        }
        if (ret < 0)
        {
#ifdef RAW_BUF_SUPPORT
            ss_buf_clear(dma_buf);
#endif
            return ret;
        }
    }
    // wait
    else
    {
        // the queue may be full and thus -ENOMEM is returned
        while ((ret = ss_write_raw(fd, dma_buf, len, &cbret, qpair_id, __ss_operation_cb)) == -ENOMEM)
        {
            // clear the queue and retry

            spdk_nvme_qpair_process_completions(ss_qpair[qpair_id], 0);
        }
        if (ret < 0)
        {
#ifdef RAW_BUF_SUPPORT
            ss_buf_clear(dma_buf);
#endif
            return ret;
        }
    }

    if (nowait && !wait)
    {
        return len;
    }

#ifndef PTHREAD_DAEMON

    while (!cbret.isvalid)
    {

        spdk_nvme_qpair_process_completions(ss_qpair[qpair_id], 0);
    }

#else
    while (!cbret.isvalid)
    {
        ;
    }
#endif

#ifdef RAW_BUF_SUPPORT
    ss_buf_clear(dma_buf);
#endif

#ifdef SS_DEBUG
    printf("[ULIB_ss_write] ret: %d\n", cbret.data);
#endif
    int res = cbret.data;
    return res;
}

ssize_t ss_read_nocache(int fd, void *buf, size_t len)
{
    int ret;
    struct cb_ret cbret = {};

    void *dma_buf = buf;
#ifdef RAW_BUF_SUPPORT
    dma_buf = ss_buf_init(len);
#endif

    int qpair_id = __get_sched_qpair_id(fd);

    //     if ((ret = ss_read_raw(fd, dma_buf, len, &cbret, qpair_id, NULL)) < 0)
    //     {
    // #ifdef RAW_BUF_SUPPORT
    //         ss_buf_clear(dma_buf);
    // #endif
    //         return ret;
    //     }
    // the queue may be full and thus -ENOMEM is returned
    while ((ret = ss_read_raw(fd, dma_buf, len, &cbret, qpair_id, NULL)) == -ENOMEM)
    {
        // clear the queue and retry

        spdk_nvme_qpair_process_completions(ss_qpair[qpair_id], 0);
    }
    if (ret < 0)
    {
#ifdef RAW_BUF_SUPPORT
        ss_buf_clear(dma_buf);
#endif
        return ret;
    }

#ifndef PTHREAD_DAEMON

    while (!cbret.isvalid)
    {

        spdk_nvme_qpair_process_completions(ss_qpair[qpair_id], 0);
    }

#else
    while (!cbret.isvalid)
    {
        ;
    }
#endif

#ifdef RAW_BUF_SUPPORT
    ss_copy_from_buf(dma_buf, buf, len);
    ss_buf_clear(dma_buf);
#endif
#ifdef SS_DEBUG
    printf("[ULIB_ss_read] ret: %d\n", cbret.data);
#endif
    return cbret.data;
}

ssize_t ss_write(int fd, const void *buf, size_t len)
{
    int ret;

    pthread_mutex_lock(&fd_offset_lock);
    off_t off = fd_offset_map[fd];
    fd_offset_map[fd] += len;
    pthread_mutex_unlock(&fd_offset_lock);

    if (use_cache)
    {
        ret = write_cache(fd, buf, len, off);
    }
    else
    {
        ret = ss_write_nocache(fd, buf, len, true);
    }
    // assert(ret == len);
    return ret;
}

ssize_t ss_read(int fd, void *buf, size_t len)
{
    int ret;
    pthread_mutex_lock(&fd_offset_lock);
    off_t off = fd_offset_map[fd];
    fd_offset_map[fd] += len;
    pthread_mutex_unlock(&fd_offset_lock);

    if (use_cache)
    {
        ret = read_cache(fd, buf, len, off);
    }
    else
    {
        ret = ss_read_nocache(fd, buf, len);
    }

    // assert(ret == len);
    return ret;
}
// int jjj = 0;
ssize_t ss_pwrite_nocache(int fd, const void *buf, size_t len, off_t offset, bool wait)
{
#ifdef SS_DEBUG
    printf("pwrite_nocache: fd %d buf %x len %ld off %ld wait %d\n", fd, buf, len, offset, wait);
#endif
    int ret;
    struct cb_ret cbret = {};

    void *dma_buf = buf;
#ifdef RAW_BUF_SUPPORT
    dma_buf = ss_copy_to_buf(len, buf);
#endif
    int qpair_id = __get_sched_qpair_id(fd);

    // spdk_nvme_cmd_cb cb = __ss_operation_cb;
    // if (!wait)
    // {
    //     cb = __ss_operation_nowait_cb;
    // }

    // nowait
    if (nowait && !wait)
    {
        // int iii = 0;
        // jjj++;
        // the queue may be full and thus -ENOMEM is returned
        while ((ret = ss_pwrite_raw(fd, dma_buf, len, offset, &cbret, qpair_id, __ss_operation_nowait_cb)) == -ENOMEM)
        {
            // clear the queue and retry
            // printf("--%d while %d--\n", jjj, iii++);
            spdk_nvme_qpair_process_completions(ss_qpair[qpair_id], 0);
        }
        if (ret < 0)
        {
#ifdef RAW_BUF_SUPPORT
            ss_buf_clear(dma_buf);
#endif
            return ret;
        }
    }
    // wait
    else
    {
        // the queue may be full and thus -ENOMEM is returned
        while ((ret = ss_pwrite_raw(fd, dma_buf, len, offset, &cbret, qpair_id, __ss_operation_cb)) == -ENOMEM)
        {
            // clear the queue and retry

            spdk_nvme_qpair_process_completions(ss_qpair[qpair_id], 0);
        }
        if (ret < 0)
        {
#ifdef RAW_BUF_SUPPORT
            ss_buf_clear(dma_buf);
#endif
            return ret;
        }
    }

    if (nowait && !wait)
    {
        return len;
    }

#ifndef PTHREAD_DAEMON

    while (!cbret.isvalid)
    {

        spdk_nvme_qpair_process_completions(ss_qpair[qpair_id], 0);
    }

#else
    while (!cbret.isvalid)
    {
        ;
    }
#endif

#ifdef RAW_BUF_SUPPORT
    ss_buf_clear(dma_buf);
#endif
#ifdef SS_DEBUG
    printf("[ULIB_ss_pwrite] ret: %d\n", cbret.data);
#endif
    int res = cbret.data;
    return res;
}

ssize_t ss_pread_nocache(int fd, void *buf, size_t len, off_t offset)
{
#ifdef SS_DEBUG
    printf("pread_nocache: fd %d buf %x len %ld off %ld\n", fd, buf, len, offset);
#endif
    int ret;
    struct cb_ret cbret = {};
#ifdef SS_DEBUG
    printf("[ULIB_ss_pread] noo cache start\n");
#endif
    void *dma_buf = buf;
#ifdef RAW_BUF_SUPPORT
    dma_buf = ss_buf_init(len);
#endif

    int qpair_id = __get_sched_qpair_id(fd);

    //     if ((ret = ss_pread_raw(fd, dma_buf, len, offset, &cbret, qpair_id, NULL)) < 0)
    //     {
    // #ifdef RAW_BUF_SUPPORT
    //         ss_buf_clear(dma_buf);
    // #endif
    //         return ret;
    //     }

    // the queue may be full and thus -ENOMEM is returned
    while ((ret = ss_pread_raw(fd, dma_buf, len, offset, &cbret, qpair_id, NULL)) == -ENOMEM)
    {
        // clear the queue and retry
        spdk_nvme_qpair_process_completions(ss_qpair[qpair_id], 0);
    }
    if (ret < 0)
    {
#ifdef RAW_BUF_SUPPORT
        ss_buf_clear(dma_buf);
#endif
        return ret;
    }
#ifdef SS_DEBUG
    printf("[ULIB_ss_pread] cmd issued\n");
#endif

#ifndef PTHREAD_DAEMON

    while (!cbret.isvalid)
    {
        spdk_nvme_qpair_process_completions(ss_qpair[qpair_id], 0);
    }

#else
    while (!cbret.isvalid)
    {
        ;
    }
#endif

#ifdef RAW_BUF_SUPPORT
    ss_copy_from_buf(dma_buf, buf, len);
    ss_buf_clear(dma_buf);
#endif
#ifdef SS_DEBUG
    printf("[ULIB_ss_pread] ret: %d\n", cbret.data);
#endif
    return cbret.data;
}

ssize_t ss_pwrite(int fd, const void *buf, size_t len, off_t offset)
{
    if (use_cache)
    {
        return write_cache(fd, buf, len, offset);
    }
    else
    {
        return ss_pwrite_nocache(fd, buf, len, offset, true);
    }
}

ssize_t ss_pread(int fd, void *buf, size_t len, off_t offset)
{
#ifdef SS_DEBUG
    printf("[ULIB_ss_pread] pread fd: %d\n", fd);
#endif

    if (use_cache)
    {
#ifdef SS_DEBUG
        printf("[ULIB_ss_pread] use cache\n");
#endif
        return read_cache(fd, buf, len, offset);
    }
    else
    {
#ifdef SS_DEBUG
        printf("[ULIB_ss_pread] noo cache\n");
#endif
        return ss_pread_nocache(fd, buf, len, offset);
    }
}

int ss_lseek64(int fd, off_t offset, int whence)
{
    int ret;
    struct cb_ret cbret = {};

    int qpair_id = __get_sched_qpair_id(fd);

    if (use_cache)
    {
        pthread_mutex_lock(&fd_offset_lock);
        switch (whence)
        {
        case SEEK_SET:
            fd_offset_map[fd] = offset;
            break;
        case SEEK_CUR:
            fd_offset_map[fd] = fd_offset_map[fd] + offset;
            break;
        // SEEK_END not supported
        default:
            break;
        }
        pthread_mutex_unlock(&fd_offset_lock);

        return 0;
    }

    // if ((ret = ss_lseek64_raw(fd, offset, whence, &cbret, qpair_id, NULL)) < 0)
    // {
    //     // #ifdef RAW_BUF_SUPPORT
    //     //         ss_buf_clear(dma_buf);
    //     // #endif
    //     return ret;
    // }

    // the queue may be full and thus -ENOMEM is returned
    while ((ret = ss_lseek64_raw(fd, offset, whence, &cbret, qpair_id, NULL)) == -ENOMEM)
    {
        // clear the queue and retry

        spdk_nvme_qpair_process_completions(ss_qpair[qpair_id], 0);
    }
    if (ret < 0)
    {
        return ret;
    }

#ifndef PTHREAD_DAEMON

    while (!cbret.isvalid)
    {

        spdk_nvme_qpair_process_completions(ss_qpair[qpair_id], 0);
    }

#else
    while (!cbret.isvalid)
    {
        ;
    }
#endif

    return cbret.data;
}

int ss_unlink(const char *pathname)
{
    int ret;
    struct cb_ret cbret = {};

    void *dma_buf = pathname;
    int len = strlen(pathname) + 1;
    dma_buf = ss_copy_to_buf(len, pathname);

    int qpair_id = rand() % SS_QPAIR_NUM;

    // if ((ret = ss_unlink_raw(dma_buf, &cbret, qpair_id, NULL)) < 0)
    // {
    //     // #ifdef RAW_BUF_SUPPORT
    //     //         ss_buf_clear(dma_buf);
    //     // #endif
    //     return ret;
    // }
    // the queue may be full and thus -ENOMEM is returned
    while ((ret = ss_unlink_raw(dma_buf, &cbret, qpair_id, NULL)) == -ENOMEM)
    {
        // clear the queue and retry

        spdk_nvme_qpair_process_completions(ss_qpair[qpair_id], 0);
    }
    if (ret < 0)
    {
        ss_buf_clear(dma_buf);
        return ret;
    }

#ifndef PTHREAD_DAEMON

    while (!cbret.isvalid)
    {

        spdk_nvme_qpair_process_completions(ss_qpair[qpair_id], 0);
    }

#else
    while (!cbret.isvalid)
    {
        ;
    }
#endif

    // #ifdef RAW_BUF_SUPPORT
    ss_buf_clear(dma_buf);
// #endif
#ifdef SS_DEBUG
    printf("[ULIB_ss_unlink] ret: %d\n", cbret.data);
#endif
    return cbret.data;
}

int ss_fsync(int fd)
{
    int ret;
    struct cb_ret cbret = {};

    int qpair_id = __get_sched_qpair_id(fd);
    // if ((ret = ss_fsync_raw(fd, &cbret, qpair_id, NULL)) < 0)
    // {
    //     return ret;
    // }
    // the queue may be full and thus -ENOMEM is returned
    while ((ret = ss_fsync_raw(fd, &cbret, qpair_id, NULL)) == -ENOMEM)
    {
        // clear the queue and retry

        spdk_nvme_qpair_process_completions(ss_qpair[qpair_id], 0);
    }
    if (ret < 0)
    {
        return ret;
    }

#ifndef PTHREAD_DAEMON

    while (!cbret.isvalid)
    {

        spdk_nvme_qpair_process_completions(ss_qpair[qpair_id], 0);
    }

#else
    while (!cbret.isvalid)
    {
        ;
    }
#endif

#ifdef SS_DEBUG
    printf("[ULIB_ss_fsync] ret: %d\n", cbret.data);
#endif
    return cbret.data;
}

int ss_mkdir(const char *pathname, mode_t mode)
{
    int ret;
    struct cb_ret cbret = {};

    void *dma_buf = pathname;
    int len = strlen(pathname) + 1;
    dma_buf = ss_copy_to_buf(len, pathname);

    int qpair_id = rand() % SS_QPAIR_NUM;

    // if ((ret = ss_mkdir_raw(dma_buf, mode, &cbret, qpair_id, NULL)) < 0)
    // {
    //     return ret;
    // }
    while ((ret = ss_mkdir_raw(dma_buf, mode, &cbret, qpair_id, NULL)) == -ENOMEM)
    {
        // clear the queue and retry

        spdk_nvme_qpair_process_completions(ss_qpair[qpair_id], 0);
    }
    if (ret < 0)
    {
        ss_buf_clear(dma_buf);
        return ret;
    }

#ifndef PTHREAD_DAEMON

    while (!cbret.isvalid)
    {

        spdk_nvme_qpair_process_completions(ss_qpair[qpair_id], 0);
    }

#else
    while (!cbret.isvalid)
    {
        ;
    }
#endif

    // #ifdef RAW_BUF_SUPPORT
    ss_buf_clear(dma_buf);
// #endif
#ifdef SS_DEBUG
    printf("[ULIB_ss_mkdir] ret: %d\n", cbret.data);
#endif
    return cbret.data;
}

int ss_rmdir(const char *pathname)
{
    int ret;
    struct cb_ret cbret = {};

    void *dma_buf = pathname;
    int len = strlen(pathname) + 1;
    dma_buf = ss_copy_to_buf(len, pathname);

    int qpair_id = rand() % SS_QPAIR_NUM;

    // if ((ret = ss_rmdir_raw(dma_buf, &cbret, qpair_id, NULL)) < 0)
    // {
    //     return ret;
    // }
    while ((ret = ss_rmdir_raw(dma_buf, &cbret, qpair_id, NULL)) == -ENOMEM)
    {
        // clear the queue and retry

        spdk_nvme_qpair_process_completions(ss_qpair[qpair_id], 0);
    }
    if (ret < 0)
    {
        ss_buf_clear(dma_buf);
        return ret;
    }

#ifndef PTHREAD_DAEMON

    while (!cbret.isvalid)
    {

        spdk_nvme_qpair_process_completions(ss_qpair[qpair_id], 0);
    }

#else
    while (!cbret.isvalid)
    {
        ;
    }
#endif

    // #ifdef RAW_BUF_SUPPORT
    ss_buf_clear(dma_buf);
// #endif
#ifdef SS_DEBUG
    printf("[ULIB_ss_rmdir] ret: %d\n", cbret.data);
#endif
    return cbret.data;
}

int ss_stat(const char *filename, struct stat *stats)
{
    int ret;
    struct cb_ret cbret = {};

    void *dma_buf = filename;
    int len = strlen(filename) + 1;
    dma_buf = ss_copy_to_buf(len, filename);

    int qpair_id = rand() % SS_QPAIR_NUM;

    // if ((ret = ss_stat_raw(dma_buf, stats, &cbret, qpair_id, NULL)) < 0)
    // {
    //     return ret;
    // }
    while ((ret = ss_stat_raw(dma_buf, stats, &cbret, qpair_id, NULL)) == -ENOMEM)
    {
        // clear the queue and retry

        spdk_nvme_qpair_process_completions(ss_qpair[qpair_id], 0);
    }
    if (ret < 0)
    {
        ss_buf_clear(dma_buf);
        return ret;
    }

#ifndef PTHREAD_DAEMON

    while (!cbret.isvalid)
    {

        spdk_nvme_qpair_process_completions(ss_qpair[qpair_id], 0);
    }

#else
    while (!cbret.isvalid)
    {
        ;
    }
#endif

    // #ifdef RAW_BUF_SUPPORT
    ss_buf_clear(dma_buf);
// #endif
#ifdef SS_DEBUG
    printf("[ULIB_ss_stat] ret: %d\n", cbret.data);
#endif
    return cbret.data;
}

int ss_fallocate(int fd, int mode, off_t offset, off_t len)
{
    int ret;
    struct cb_ret cbret = {};

    int qpair_id = __get_sched_qpair_id(fd);

    // if ((ret = ss_fallocate_raw(fd, mode, offset, len, &cbret, qpair_id, NULL)) < 0)
    // {
    //     return ret;
    // }
    while ((ret = ss_fallocate_raw(fd, mode, offset, len, &cbret, qpair_id, NULL)) == -ENOMEM)
    {
        // clear the queue and retry

        spdk_nvme_qpair_process_completions(ss_qpair[qpair_id], 0);
    }
    if (ret < 0)
    {
        return ret;
    }

#ifndef PTHREAD_DAEMON

    while (!cbret.isvalid)
    {

        spdk_nvme_qpair_process_completions(ss_qpair[qpair_id], 0);
    }

#else
    while (!cbret.isvalid)
    {
        ;
    }
#endif

    // ss_buf_clear(buf);
#ifdef SS_DEBUG
    printf("[ULIB_ss_fallocate] ret: %d\n", cbret.data);
#endif
    return cbret.data;
}

int ss_ftruncate(int fd, off_t len)
{
    int ret;
    struct cb_ret cbret = {};

    int qpair_id = __get_sched_qpair_id(fd);

    // if ((ret = ss_ftruncate_raw(fd, len, &cbret, qpair_id, NULL)) < 0)
    // {
    //     return ret;
    // }
    while ((ret = ss_ftruncate_raw(fd, len, &cbret, qpair_id, NULL)) == -ENOMEM)
    {
        // clear the queue and retry

        spdk_nvme_qpair_process_completions(ss_qpair[qpair_id], 0);
    }
    if (ret < 0)
    {
        return ret;
    }

#ifndef PTHREAD_DAEMON

    while (!cbret.isvalid)
    {

        spdk_nvme_qpair_process_completions(ss_qpair[qpair_id], 0);
    }

#else
    while (!cbret.isvalid)
    {
        ;
    }
#endif

    // ss_buf_clear(buf);
#ifdef SS_DEBUG
    printf("[ULIB_ss_ftruncate] ret: %d\n", cbret.data);
#endif
    return cbret.data;
}

// TODO: Undone
int ss_rename(const char *oldpath, const char *newpath)
{
    int ret;
    struct cb_ret cbret = {};

    void *dma_buf1 = oldpath;
    // #ifdef RAW_BUF_SUPPORT
    int len1 = strlen(oldpath) + 1;
    dma_buf1 = ss_copy_to_buf(len1, oldpath);
    // #endif

    void *dma_buf2 = newpath;
    // #ifdef RAW_BUF_SUPPORT
    int len2 = strlen(newpath) + 1;
    dma_buf2 = ss_copy_to_buf(len2, newpath);
    // #endif

    int qpair_id = rand() % SS_QPAIR_NUM;

    // if ((ret = ss_rename_raw(dma_buf1, dma_buf2, &cbret, qpair_id, NULL)) < 0)
    // {
    //     return ret;
    // }
    while ((ret = ss_rename_raw(dma_buf1, dma_buf2, &cbret, qpair_id, NULL)) == -ENOMEM)
    {
        // clear the queue and retry

        spdk_nvme_qpair_process_completions(ss_qpair[qpair_id], 0);
    }
    if (ret < 0)
    {
        ss_buf_clear(dma_buf1);
        ss_buf_clear(dma_buf2);
        return ret;
    }

#ifndef PTHREAD_DAEMON

    while (!cbret.isvalid)
    {

        spdk_nvme_qpair_process_completions(ss_qpair[qpair_id], 0);
    }

#else
    while (!cbret.isvalid)
    {
        ;
    }
#endif

    // #ifdef RAW_BUF_SUPPORT
    ss_buf_clear(dma_buf1);
    ss_buf_clear(dma_buf2);
// #endif
#ifdef SS_DEBUG
    printf("[ULIB_ss_rename] ret: %d\n", cbret.data);
#endif
    return cbret.data;
}

/**
 * StorStack I/O Command
 * latency simulation for normal path
 * with return value from qemu
 *
 * \return res, normally 0
 * \note FIXME: untested!!!
 */
int ss_latency_with_ret()
{
    struct cb_ret rets = {};
    rets.isvalid = false;

    struct spdk_nvme_cmd cmd = {};
    cmd.opc = SPDK_NVME_OPC_SS_LAT_WITH_RET;
    cmd.nsid = spdk_nvme_ns_get_id(ns_entry->ns);
    cmd.uid = user_info->uid;
    cmd.cdw15_token = user_info->token_32;

    int qpair_id = rand() % SS_QPAIR_NUM; // randomly allocate a qpair id for tmp use

    int cmd_ret = spdk_nvme_ctrlr_cmd_io_raw(ns_entry->ctrlr, ss_qpair[qpair_id], &cmd, NULL, 0, __ss_operation_cb, &rets);
    if (cmd_ret)
    {
        fprintf(stderr, "[ULIB_ss_latency] ss_latency_with_ret cmd io failed!\n");
        return -1;
    }

#ifndef PTHREAD_DAEMON

    while (!rets.isvalid)
    {

        spdk_nvme_qpair_process_completions(ss_qpair[qpair_id], 0);
    }

#else
    while (!rets.isvalid)
    {
        ;
    }
#endif

    return rets.data;
}

int ss_latency_with_ret_r(int size)
{
    struct cb_ret rets = {};
    rets.isvalid = false;
    int buf_size = size;
    void *buf = ss_buf_init(buf_size);

    struct spdk_nvme_cmd cmd = {};
    cmd.opc = SPDK_NVME_OPC_SS_LAT_WITH_RET_R;
    cmd.nsid = spdk_nvme_ns_get_id(ns_entry->ns);
    cmd.uid = user_info->uid;
    cmd.cdw11_ss_buffer_len = size;
    cmd.cdw15_token = user_info->token_32;

    int qpair_id = rand() % SS_QPAIR_NUM; // randomly allocate a qpair id for tmp use

    int cmd_ret = spdk_nvme_ctrlr_cmd_io_raw(ns_entry->ctrlr, ss_qpair[qpair_id], &cmd, buf, buf_size, __ss_operation_cb, &rets);
    if (cmd_ret)
    {
        fprintf(stderr, "[ULIB_ss_latency] ss_latency_with_ret cmd io failed!\n");
        return -1;
    }

#ifndef PTHREAD_DAEMON

    while (!rets.isvalid)
    {
        spdk_nvme_qpair_process_completions(ss_qpair[qpair_id], 0);
    }

#else
    while (!rets.isvalid)
    {
        ;
    }
#endif

    return rets.data;
}

int ss_latency_with_ret_w(int size)
{
    struct cb_ret rets = {};
    rets.isvalid = false;
    int buf_size = size;
    void *buf = ss_buf_init(buf_size);

    struct spdk_nvme_cmd cmd = {};
    cmd.opc = SPDK_NVME_OPC_SS_LAT_WITH_RET_W;
    cmd.nsid = spdk_nvme_ns_get_id(ns_entry->ns);
    cmd.uid = user_info->uid;
    cmd.cdw11_ss_buffer_len = size;
    cmd.cdw15_token = user_info->token_32;

    int qpair_id = rand() % SS_QPAIR_NUM; // randomly allocate a qpair id for tmp use

    int cmd_ret = spdk_nvme_ctrlr_cmd_io_raw(ns_entry->ctrlr, ss_qpair[qpair_id], &cmd, buf, size, __ss_operation_cb, &rets);
    if (cmd_ret)
    {
        fprintf(stderr, "[ULIB_ss_latency] ss_latency_with_ret cmd io failed!\n");
        return -1;
    }

#ifndef PTHREAD_DAEMON

    while (!rets.isvalid)
    {
        spdk_nvme_qpair_process_completions(ss_qpair[qpair_id], 0);
    }

#else
    while (!rets.isvalid)
    {
        ;
    }
#endif

    return rets.data;
}

int ss_latency_no_ret(uint32_t lat)
{
    struct cb_ret rets = {};
    rets.isvalid = false;

    struct spdk_nvme_cmd cmd = {};
    cmd.opc = SPDK_NVME_OPC_SS_LAT_NO_RET;
    cmd.nsid = spdk_nvme_ns_get_id(ns_entry->ns);
    cmd.uid = user_info->uid;
    cmd.cdw11 = lat;
    cmd.cdw15_token = user_info->token_32;

    int qpair_id = rand() % SS_QPAIR_NUM; // randomly allocate a qpair id for tmp use

    int cmd_ret = spdk_nvme_ctrlr_cmd_io_raw(ns_entry->ctrlr, ss_qpair[qpair_id], &cmd, NULL, 0, __ss_operation_cb, &rets);

#ifndef PTHREAD_DAEMON

    while (!rets.isvalid)
    {

        spdk_nvme_qpair_process_completions(ss_qpair[qpair_id], 0);
    }

#else
    while (!rets.isvalid)
    {
        ;
    }
#endif

    return 0;
}