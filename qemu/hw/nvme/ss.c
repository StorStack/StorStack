#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "qemu/units.h"
#include "qemu/range.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "sysemu/sysemu.h"
#include "sysemu/block-backend.h"
#include "sysemu/hostmem.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie_sriov.h"
#include "migration/vmstate.h"

#include "nvme.h"
#include "dif.h"
#include "trace.h"
#include "ss.h"
#include "hmac_sha2.h"

#include <sched.h>
#include <sys/time.h>

extern uint32_t latency_sim_process_sq_ns;

// #define SS_DEBUG

// #define SS_LATENCY_TIMER

#ifdef SS_LATENCY_TIMER
extern unsigned int g_timer_count = 0;
static struct timespec g_timer[16384][10];
static inline void __ss_latency_timer(const unsigned int count, unsigned int group)
{
    // (void)clock_gettime(CLOCK_MONOTONIC, &g_timer[count][group]);
    g_timer[count][group].tv_nsec = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
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
    struct timeval endtimer;
    gettimeofday(&endtimer, NULL);
    printf("PRINT DONE with end time: %ld\n\n\n\n\n\n", endtimer.tv_usec);
}
#endif

struct BlockBackend
{
    char *name;
    int refcnt;
    BdrvChild *root;
    AioContext *ctx;
    DriveInfo *legacy_dinfo; /* null unless created by drive_new() */
    QTAILQ_ENTRY(BlockBackend)
    link; /* for block_backends */
    QTAILQ_ENTRY(BlockBackend)
    monitor_link; /* for monitor_block_backends */
    BlockBackendPublic public;

    DeviceState *dev; /* attached device model, if any */
    const BlockDevOps *dev_ops;
    void *dev_opaque;

    /* If the BDS tree is removed, some of its options are stored here (which
     * can be used to restore those options in the new BDS on insert) */
    BlockBackendRootState root_state;

    bool enable_write_cache;

    /* I/O stats (display with "info blockstats"). */
    BlockAcctStats stats;

    BlockdevOnError on_read_error, on_write_error;
    bool iostatus_enabled;
    BlockDeviceIoStatus iostatus;

    uint64_t perm;
    uint64_t shared_perm;
    bool disable_perm;

    bool allow_aio_context_change;
    bool allow_write_beyond_eof;

    /* Protected by BQL */
    NotifierList remove_bs_notifiers, insert_bs_notifiers;
    QLIST_HEAD(, BlockBackendAioNotifier)
    aio_notifiers;

    int quiesce_counter;
    CoQueue queued_requests;
    bool disable_request_queuing;

    VMChangeStateEntry *vmsh;
    bool force_allow_inactivate;

    /* Number of in-flight aio requests.  BlockDriverState also counts
     * in-flight requests but aio requests can exist even when blk->root is
     * NULL, so we cannot rely on its counter for that case.
     * Accessed with atomic ops.
     */
    unsigned int in_flight;
};

#define BLKNAME(namespace) (namespace->blkconf.blk)->root->bs->filename
#define DW2CPU(x) (uint32_t) le32_to_cpu(x)
#define CPU2DW(x) cpu_to_le32((uint32_t)(x))
#define CDW(req, id) DW2CPU(((NvmeSsCmd *)(&req->cmd))->cdw##id)
#define UID(req)     DW2CPU(((NvmeSsCmd *)(&req->cmd))->uid)

#define SECKEY_LEN  256 >> 3
#define TOKEN_LEN   256 >> 3
#define AVAIL_TOKEN_LEN 32 >> 3
static uint8_t secret_key[SECKEY_LEN];
// QCryptoHmac *nvme_ss_hmac;

typedef struct __attribute__ ((__packed__)) user_cred_s
{
    uid_t uid;
    gid_t gid;
} user_cred;

typedef union user_cred_u
{
    user_cred cred;
    char bytes[sizeof(user_cred)];
} user_cred_converter;

typedef union cdw_token_u
{
    uint32_t cdw;
    uint8_t token[AVAIL_TOKEN_LEN];
} cdw_token_converter;

#define NR_OPEN 1048576
typedef struct fstat_s
{
    uid_t uid;
    ino_t inode;
    mode_t mode;
    int flags;
    char  st;   // 0: closed; 1:open
} fstat_s;
fstat_s fstats[NR_OPEN];

#define CHKOWNER(fd, _uid) fstats[fd].uid==_uid
#define CHKMODE(fd, _mode) fstats[fd].mode&_mode

void nvme_ss_init()
{
    for (int i = 0; i < NR_OPEN; i++)
    {
        fstats[i].st = 0;
    }
    
}

static int verify_user_cred(user_cred cred, uint8_t token[])
{
    int ret;
    size_t len;
    uint8_t result[TOKEN_LEN];
    user_cred_converter cvt;
    cvt.cred = cred;
    // len = TOKEN_LEN;
    // result = g_new0(uint8_t, len);

    // ret = qcrypto_hmac_bytes(nvme_ss_hmac, (const char *)cvt.bytes,
    //                              SECKEY_LEN, &result,
    //                              &len, &error_fatal);
    hmac_sha256(secret_key, SECKEY_LEN, cvt.bytes, sizeof(cvt.bytes), result, TOKEN_LEN);

    // if (ret)
    //     goto err;

#ifdef SS_DEBUG
    printf("StorStack token for u%d g%d\n", cred.uid, cred.gid);
    int i;
    printf("app token: ");
    for (i = 0; i < AVAIL_TOKEN_LEN; i++)
    {
        printf("%02x ", token[i]);
    }
    printf("\n"); 

    printf("qemu token: ");
    for (i = 0; i < TOKEN_LEN; i++)
    {
        printf("%02x ", result[i]);
    }
    printf("\n"); 

    printf("StorStack secret: ");
    for (i = 0; i < SECKEY_LEN; i++)
    {
        printf("%02x ", secret_key[i]);
    }
    printf("\n");
#endif

    for (int i = 0; i < AVAIL_TOKEN_LEN; i++)
    {
        if (token[i] != result[i]) {
            ret = i+1;
            goto err;
        }
    }

#ifdef SS_DEBUG
    printf("check passed\n");
#endif

    // g_free(result);
    return 0;
err:
    // g_free(result);
    return ret;
}

// TODO: only uid is checked now, add gid check in the future
static int verify_user_cred_by_req(NvmeRequest *req)
{
    uint32_t uid = UID(req);
    uint32_t gid = 0; 
    uint32_t token = CDW(req, 15);

    user_cred cred =  {
        .uid = uid,
        .gid = gid,
    };
    cdw_token_converter cvt;
    cvt.cdw = token; 
    return verify_user_cred(cred, cvt.token);
}

// TODO: only uid is checked now, add gid check in the future
// static int check_access_permission(uid_t uid, int fd)
// {
//     struct stat file_stat;  
//     int ret;  
//     ret = fstat (fd, &file_stat);  
//     if (ret < 0) return ret;
//     if (uid == file_stat.st_uid)
//         return 0;
//     else
//         return -1;
// }

static ino_t inode_of(int fd)
{
    struct stat file_stat;  
    fstat (fd, &file_stat);  

    return file_stat.st_ino;
}

static uid_t uid_of(int fd)
{
    struct stat file_stat;  
    fstat (fd, &file_stat);  

    return file_stat.st_uid;
}

static mode_t mode_of(int fd)
{
    struct stat file_stat;  
    fstat (fd, &file_stat);  

    return file_stat.st_mode;
}

uint16_t nvme_ss_adm_part_init(NvmeCtrl *n, NvmeRequest *req)
{
    return NVME_SUCCESS;
}

uint16_t nvme_ss_adm_skey(NvmeCtrl *n, NvmeRequest *req)
{
    // NvmeAdminCtrl
    uint16_t status = nvme_h2c(n, secret_key, SECKEY_LEN, req);
    if (status)
        goto invalid;
        
    // nvme_ss_hmac = qcrypto_hmac_new(QCRYPTO_HASH_ALG_SHA256, (const uint8_t *)secret_key,
    //                         SECKEY_LEN, &error_fatal);

#ifdef SS_DEBUG
    printf("StorStack new secret: ");
    int i;
    for (i = 0; i < SECKEY_LEN; i++)
    {
        printf("%02x ", secret_key[i]);
    }
    printf("\n");
#endif

    return NVME_SUCCESS;

invalid:
    return NVME_DNR;
}

// TODO: this is a test interface, remove this after development
uint16_t nvme_ss_hellow(NvmeCtrl *n, NvmeRequest *req)
{
    int cpu, node;
    getcpu(&cpu, &node);
#ifdef SS_DEBUG
    printf("pid: %d  tid: %d cpu: %d %d\n", getpid(), req->sq->core->thread->thread_id, cpu, node);
#endif
    if (verify_user_cred_by_req(req)) return NVME_SS_PERM_DENY;
    
    size_t data_len = CDW(req,10);
    char *buffer = (char *)malloc(data_len+1);

    uint16_t status = nvme_h2c(n, buffer, data_len, req);
    if (status)
        goto invalid;

    buffer[data_len] = 0;
#ifdef SS_DEBUG
    printf("hellow: %s\n", buffer);
#endif
    req->cqe.result = CPU2DW(data_len);
    
    free(buffer);
    return NVME_SUCCESS;

invalid:
    free(buffer);
    return NVME_DNR;
}

// TODO: this is a test interface, remove this after development
uint16_t nvme_ss_hellor(NvmeCtrl *n, NvmeRequest *req)
{
    int cpu, node;
    getcpu(&cpu, &node);
#ifdef SS_DEBUG
    printf("pid: %d  tid: %d cpu: %d %d\n", getpid(), req->sq->core->thread->thread_id, cpu, node);
#endif
    if (verify_user_cred_by_req(req)) return NVME_SS_PERM_DENY;

    char buffer[] = "hello world";
    size_t data_len = sizeof(buffer);

    uint16_t status = nvme_c2h(n, buffer, data_len, req);
    if (status)
        goto invalid;
#ifdef SS_DEBUG
    printf("hellor: %s\n", buffer);
#endif
    req->cqe.result = CPU2DW(data_len); 

    return NVME_SUCCESS;

invalid:
    return NVME_DNR;
}


/**
 * StorStack I/O Command for QEMU NVMe Driver
 * open file
 * 
 * \param cdw10 length of pathname
 * \param cdw11 flags
 * \param cdw12 mode
 * \param dma_buf pathname
 * \param cqe_dw0 file descriptor
 * \param cqe_dw1 file 
 * \return NvmeStatusCodes
 * \note int ss_open(const char *pathname, int flags, mode_t mode)
 */
uint16_t nvme_ss_open(NvmeCtrl *n, NvmeRequest *req)
{
    // if (verify_user_cred_by_req(req)) return NVME_SS_PERM_DENY;
#ifdef SS_DEBUG
    printf("[QEMU_SS_OPEN] uid: %d\n", UID(req)); // DEBUG: StorStack
#endif
    size_t data_len = CDW(req,10); // with '\0'
    int flags = CDW(req,11);
    int mode = CDW(req,12);
    char *buffer = (char *)malloc(sizeof(char) * data_len); // with '\0'
#ifdef SS_DEBUG
    printf("[QEMU_SS_OPEN] data_len: %d\n", (int)data_len);
    printf("[QEMU_SS_OPEN] mode: %d\n", (int)mode);
    printf("[QEMU_SS_OPEN] flags: %d\n", (int)flags);
#endif
    uint16_t status = nvme_h2c(n, buffer, data_len, req);
    if (status)
    {
        printf("[QEMU_SS_OPEN] mem transfer error: %d\n", status);
        goto invalid;
    }

#ifdef SS_DEBUG
    printf("[QEMU_SS_OPEN] buffer: '%s'\n", buffer);
#endif

    int fd = open(buffer, flags, mode);
    uid_t fuid = uid_of(fd);
    uint32_t uuid = UID(req);
    mode_t fmode = mode_of(fd);
    ino_t finode = inode_of(fd);

    // if (fuid == uuid) { 
    //     if (
    //         ((flags & O_RDONLY) && !(fmode & S_IRUSR)) || 
    //         ((flags & O_WRONLY) && !(fmode & S_IWUSR)) ||
    //         ((flags & O_RDWR) && !(fmode & (S_IRUSR | S_IWUSR)))
    //     )
    //         goto perm_deny;
    // } else {
    //     if (
    //         ((flags & O_RDONLY) && !(fmode & S_IROTH)) || 
    //         ((flags & O_WRONLY) && !(fmode & S_IWOTH)) ||
    //         ((flags & O_RDWR) && !(fmode & (S_IROTH | S_IWOTH)))
    //     )
    //         goto perm_deny;
    // }

    fstats[fd].inode = finode;
    fstats[fd].uid = fuid;
    fstats[fd].mode = fmode;
    fstats[fd].flags = flags;
    fstats[fd].st = 1;
    
    req->cqe.result = CPU2DW(fd); // File Descriptor
    req->cqe.dw1    = CPU2DW(finode); // file inode
#ifdef SS_DEBUG
    printf("[QEMU_SS_OPEN] rawfd: %d , encode: %d\n", fd, CPU2DW(fd)); // DEBUG: StorStack
#endif
    free(buffer);
    return NVME_SUCCESS;

invalid:
    free(buffer);
    return NVME_DNR;
perm_deny:
    close(fd);
    free(buffer);
    return NVME_SS_PERM_DENY;
}

/**
 * StorStack I/O Command for QEMU NVMe Driver
 * close file
 * 
 * \param cdw10 fd
 * \param cqe_dw0 close ret
 * \return NvmeStatusCodes
 * \note int ss_close(int fd)
 */
uint16_t nvme_ss_close(NvmeCtrl *n, NvmeRequest *req)
{
    // FIXME: anyone can close an fd now, which can be a fault
    // if (verify_user_cred_by_req(req)) return NVME_SS_PERM_DENY;

    int fd = CDW(req,10);

    // printf("[QEMU_SS_CLOSE] fd: %d uid: %d\n", fd, UID(req)); // DEBUG: StorStack

    // if (!CHKOWNER(fd, UID(req))) return NVME_SS_PERM_DENY;

    fstats[fd].st = 0;
    uint32_t status_close = close(fd);

    req->cqe.result = CPU2DW(status_close);
#ifdef SS_DEBUG
    printf("[QEMU_ss_close] ret: %d\n", status_close);
#endif
    return NVME_SUCCESS;
}

/**
 * StorStack I/O Command for QEMU NVMe Driver
 * write file
 * 
 * \param cdw10 fd
 * \param cdw11 data length (byte)
 * \param dma_buf data
 * \param cqe_dw0 write ret
 * \return NvmeStatusCodes
 * \note int ss_write(int fd, char buf[], int length)
 */
uint16_t nvme_ss_write(NvmeCtrl *n, NvmeRequest *req)
{
    //if (verify_user_cred_by_req(req)) return NVME_SS_PERM_DENY;
    int fd = CDW(req,10);

    // printf("[QEMU_SS_WRITE] fd: %d uid: %d\n", fd, UID(req)); // DEBUG: StorStack

    // if (check_access_permission(UID(req), fd)) return NVME_SS_PERM_DENY;
    // if (fstats[fd].st == 0 || fstats[fd].flags == O_RDONLY) return NVME_SS_PERM_DENY;
    // if (CHKOWNER(fd, UID(req))) {
    //     if (!CHKMODE(fd, S_IWUSR)) return NVME_SS_PERM_DENY;
    // } else {
    //     if (!CHKMODE(fd, S_IWOTH)) return NVME_SS_PERM_DENY;
    // }
    
    size_t data_len = CDW(req,11);
    uint8_t *buffer = (uint8_t *)malloc(sizeof(uint8_t) * data_len);
    uint16_t status = nvme_h2c(n, buffer, data_len, req);
    if (status)
        goto invalid;
    uint32_t status_write = write(fd, buffer, data_len);
#ifdef SS_DEBUG
    printf("[QEMU_ss_write] bufaddr: %d\n", buffer);
#endif
    req->cqe.result = CPU2DW(status_write);
#ifdef SS_DEBUG
    printf("[QEMU_ss_write] ret: %d\n", status_write);
#endif
    free(buffer);
    return NVME_SUCCESS;

invalid:
    free(buffer);
    return NVME_DNR;
}

/**
 * StorStack I/O Command for QEMU NVMe Driver
 * read file
 * 
 * \param cdw10 fd
 * \param cdw11 data length (byte)
 * \param dma_buf data
 * \param cqe_dw0 read ret
 * \return NvmeStatusCodes
 * \note int ss_read(int fd, char buf[], int length)
 */
uint16_t nvme_ss_read(NvmeCtrl *n, NvmeRequest *req)
{
    // printf("[QEMU_SS_READ] Start nvme_ss_read.\n");
    // if (verify_user_cred_by_req(req)) return NVME_SS_PERM_DENY;
    int fd = CDW(req,10);

    // printf("[QEMU_SS_READ] fd: %d uid: %d\n", fd, UID(req)); // DEBUG: StorStack

    // if (fstats[fd].st == 0 || fstats[fd].flags == O_WRONLY) return NVME_SS_PERM_DENY;
    // if (CHKOWNER(fd, UID(req))) {
    //     if (!CHKMODE(fd, S_IRUSR)) return NVME_SS_PERM_DENY;
    // } else {
    //     if (!CHKMODE(fd, S_IROTH)) return NVME_SS_PERM_DENY;
    // }

    size_t data_len = CDW(req,11);

    uint8_t *buffer = (uint8_t *)malloc(sizeof(uint8_t) * data_len);

    uint32_t status_read = read(fd, buffer, data_len);
    // printf("[QEMU_SS_READ] read content buf: '%s'\n", (char*)buffer);

    req->cqe.result = CPU2DW(status_read);
    uint16_t status = nvme_c2h(n, buffer, data_len, req);  
    if (status)
        goto invalid;
    free(buffer);
#ifdef SS_DEBUG
    printf("[QEMU_ss_read] ret: %d\n", status_read);
#endif
    return NVME_SUCCESS;

invalid:
    free(buffer);
    return NVME_DNR;
}

/**
 * StorStack I/O Command for QEMU NVMe Driver
 * write file using offset
 * 
 * \param cdw10 fd
 * \param cdw11 data length (byte)
 * \param cdw12 offset
 * \param dma_buf data
 * \param cqe_dw0 pwrite ret
 * \return NvmeStatusCodes
 * \note int ss_pwrite(int fd, uint8_t buf[], size_t length, loff_t offset)
 */
uint16_t nvme_ss_pwrite(NvmeCtrl *n, NvmeRequest *req)
{
    // if (verify_user_cred_by_req(req)) return NVME_SS_PERM_DENY;

    int fd = CDW(req,10);
    // if (fstats[fd].st == 0 || fstats[fd].flags == O_RDONLY) return NVME_SS_PERM_DENY;
    // if (CHKOWNER(fd, UID(req))) {
    //     if (!CHKMODE(fd, S_IWUSR)) return NVME_SS_PERM_DENY;
    // } else {
    //     if (!CHKMODE(fd, S_IWOTH)) return NVME_SS_PERM_DENY;
    // }
    
    // int cpu, node;
    // getcpu(&cpu, &node);

    // printf("fd: %d inode: %d cpu: %d node: %d\n", fd, fstats[fd].inode, cpu, node);

    size_t data_len = CDW(req,11);
    loff_t offs = CDW(req,12);

    uint8_t *buffer = (uint8_t *)malloc(sizeof(uint8_t) * data_len);

    uint16_t status = nvme_h2c(n, buffer, data_len, req);
    if (status)
        goto invalid;

    uint32_t status_pwrite = pwrite(fd, buffer, data_len, offs);

    req->cqe.result = CPU2DW(status_pwrite);
#ifdef SS_DEBUG
    printf("[QEMU_ss_pwrite] ret: %d\n", status_pwrite);
#endif
    free(buffer);
    return NVME_SUCCESS;

invalid:
    free(buffer);
    return NVME_DNR;
}

/**
 * StorStack I/O Command for QEMU NVMe Driver
 * read file using offset
 * 
 * \param cdw10 fd
 * \param cdw11 data length (byte)
 * \param cdw12 offset
 * \param dma_buf data
 * \param cqe_dw0 pread ret
 * \return NvmeStatusCodes
 * \note int ss_pread(int fd, uint8_t buf[], size_t length, loff_t offset)
 */
uint16_t nvme_ss_pread(NvmeCtrl *n, NvmeRequest *req)
{
    // if (verify_user_cred_by_req(req)) return NVME_SS_PERM_DENY;

    int fd = CDW(req,10);
    // if (fstats[fd].st == 0 || fstats[fd].flags == O_WRONLY) return NVME_SS_PERM_DENY;
    // if (CHKOWNER(fd, UID(req))) {
    //     if (!CHKMODE(fd, S_IRUSR)) return NVME_SS_PERM_DENY;
    // } else {
    //     if (!CHKMODE(fd, S_IROTH)) return NVME_SS_PERM_DENY;
    // }

    size_t data_len = CDW(req,11);
    loff_t offs = CDW(req,12);

    uint8_t *buffer = (uint8_t *)malloc(sizeof(uint8_t) * data_len);

    uint32_t status_pread = pread(fd, buffer, data_len, offs);

    uint16_t status = nvme_c2h(n, buffer, data_len, req);
    if (status)
        goto invalid;

    req->cqe.result = CPU2DW(status_pread);
#ifdef SS_DEBUG
    printf("[QEMU_ss_pread] ret: %d\n", status_pread);
#endif
    free(buffer);
    return NVME_SUCCESS;

invalid:
    free(buffer);
    return NVME_DNR;
}

/**
 * StorStack I/O Command for QEMU NVMe Driver
 * seek location using offset
 * 
 * \param cdw10 fd
 * \param cdw12 offset
 * \param cdw13 whence
 * \param cqe_dw0 lseek location ptr
 * \return NvmeStatusCodes
 * \note int ss_lseek(int fd, loff_t offset, int whence)
 */
uint16_t nvme_ss_lseek(NvmeCtrl *n, NvmeRequest *req)
{
    // if (verify_user_cred_by_req(req)) return NVME_SS_PERM_DENY;

    int fd = CDW(req,10);
    // FIXME: anyone may perform this operation
    // if (fstats[fd].st == 0) return NVME_SS_PERM_DENY;

    loff_t offs = CDW(req,12);
    int whence = CDW(req,13);

    loff_t status_lseek = lseek(fd, offs, whence);

    req->cqe.result = CPU2DW(status_lseek);
#ifdef SS_DEBUG
    printf("[QEMU_ss_lseek] ret: %d\n", status_lseek);
#endif
    return NVME_SUCCESS;
}

/**
 * StorStack I/O Command for QEMU NVMe Driver
 * unlink file
 * 
 * \param cdw10 fd
 * \param cqe_dw0 unlink ret
 * \return NvmeStatusCodes
 * \note int ss_unlink(int fd)
 */
uint16_t nvme_ss_unlink(NvmeCtrl *n, NvmeRequest *req)
{
    // if (verify_user_cred_by_req(req)) return NVME_SS_PERM_DENY;

    size_t data_len = CDW(req,10);
    // FIXME: anyone may perform this operation
    // if (fstats[fd].st == 0) return NVME_SS_PERM_DENY;

    char *buffer = (char *)malloc(sizeof(char) * data_len);
    uint16_t status = nvme_h2c(n, buffer, data_len, req);
    if (status)
    {
        printf("[QEMU_SS_UNLINK] mem transfer error: %d\n", status);
        goto invalid;
    }

    int status_unlink = unlink(buffer);

    req->cqe.result = CPU2DW(status_unlink);
#ifdef SS_DEBUG
    printf("[QEMU_ss_unlink] ret: %d\n", status_unlink);
#endif

    free(buffer);
    return NVME_SUCCESS;

invalid:
    free(buffer);
    return NVME_DNR;
}

/**
 * StorStack I/O Command for QEMU NVMe Driver
 * sync changes
 * 
 * \param cdw10 fd
 * \param cqe_dw0 fsync ret
 * \return NvmeStatusCodes
 * \note int ss_fsync(int fd)
 */
uint16_t nvme_ss_fsync(NvmeCtrl *n, NvmeRequest *req)
{
    // if (verify_user_cred_by_req(req)) return NVME_SS_PERM_DENY;

    int fd = CDW(req,10);
    // FIXME: anyone may perform this operation
    // if (fstats[fd].st == 0) return NVME_SS_PERM_DENY;

    int status_fsync = fsync(fd);

    req->cqe.result = CPU2DW(status_fsync);
#ifdef SS_DEBUG
    printf("[QEMU_ss_fsync] ret: %d\n", status_fsync);
#endif
    return NVME_SUCCESS;
}

/**
 * StorStack I/O Command for QEMU NVMe Driver
 * get file attributes
 * 
 * \param cdw10 MAX(filename.length, sizeof(struct stats))
 * \param dma_buf (vm->host) filename | (host->vm) struct stats
 * \param cqe_dw0 fstat ret
 * \return NvmeStatusCodes
 * \note int ss_stat(const char * file)
 */
uint16_t nvme_ss_stat(NvmeCtrl *n, NvmeRequest *req)
{
    size_t data_len = CDW(req,10);
    char * buffer = (char *)malloc(sizeof(char) * data_len);

    struct stat *stats = (struct stat*)malloc(sizeof(struct stat));
    
    uint16_t status = nvme_h2c(n, buffer, data_len, req);
    if (status)
    {
        printf("[QEMU_SS_STAT] mem transfer error 1: %d\n", status);
        goto invalid;
    }
    sync(); // FIXME: is this necessary?
    int status_stat = stat(buffer, stats);
    status = nvme_c2h(n, stats, sizeof(struct stat), req);
    if (status)
    {
        printf("[QEMU_SS_STAT] mem transfer error 2: %d\n", status);
        goto invalid;
    }

    free(buffer);
    free(stats);
#ifdef SS_DEBUG
    printf("[QEMU_ss_stat] ret: %d\n", status_stat);
#endif
    req->cqe.result = CPU2DW(status_stat);
    return NVME_SUCCESS;

invalid:
    free(buffer);
    free(stats);
    return NVME_DNR;
}

/**
 * StorStack I/O Command for QEMU NVMe Driver
 * make dirs
 * 
 * \param cdw10 filename.length
 * \param cdw12 mode
 * \param dma_buf filename
 * \param cqe_dw0 mkdir ret
 * \return NvmeStatusCodes
 * \note int ss_mkdir(const char * path, mode_t mode)
 */
uint16_t nvme_ss_mkdir(NvmeCtrl *n, NvmeRequest *req)
{
    int data_len = CDW(req, 10);
    mode_t mode = CDW(req, 12);
    char *buffer = (char*)malloc(sizeof(char) * data_len);

    uint16_t status = nvme_h2c(n, buffer, data_len, req);
    if (status)
    {
        printf("[QEMU_SS_MKDIR] mem transfer error: %d\n", status);
        goto invalid;
    }

    int status_mkdir = mkdir(buffer, mode);
    req->cqe.result = CPU2DW(status_mkdir);
#ifdef SS_DEBUG
    printf("[QEMU_ss_mkdir] ret: %d\n", status_mkdir);
#endif
    free(buffer);
    return NVME_SUCCESS;

invalid:
    free(buffer);
    return NVME_DNR;
}

/**
 * StorStack I/O Command for QEMU NVMe Driver
 * remove dirs
 * 
 * \param cdw10 filename.length
 * \param dma_buf filename
 * \param cqe_dw0 mkdir ret
 * \return NvmeStatusCodes
 * \note int ss_rmdir(const char * path)
 */
uint16_t nvme_ss_rmdir(NvmeCtrl *n, NvmeRequest *req)
{
    int data_len = CDW(req, 10);
    char *buffer = (char*)malloc(sizeof(char) * data_len);

    uint16_t status = nvme_h2c(n, buffer, data_len, req);
    if (status)
    {
        printf("[QEMU_SS_RMDIR] mem transfer error: %d\n", status);
        goto invalid;
    }

    int status_rmdir = rmdir(buffer);
    req->cqe.result = CPU2DW(status_rmdir);
#ifdef SS_DEBUG
    printf("[QEMU_ss_rmdir] ret: %d\n", status_rmdir);
#endif
    free(buffer);
    return NVME_SUCCESS;

invalid:
    free(buffer);
    return NVME_DNR;
}

/**
 * StorStack I/O Command for QEMU NVMe Driver
 * allocate
 * 
 * \param cdw10 fd
 * \param cdw12 offset
 * \param cdw13 mode
 * \param cdw14 space length
 * \param cqe_dw0 fallocate ret
 * \return NvmeStatusCodes
 * \note int ss_fallocate(int fd, int mode, loff_t offset, loff_t len)
 */
uint16_t nvme_ss_fallocate(NvmeCtrl *n, NvmeRequest *req)
{
    // if (verify_user_cred_by_req(req)) return NVME_SS_PERM_DENY;

    int fd = CDW(req,10);
    // FIXME: anyone may perform this operation
    // if (fstats[fd].st == 0) return NVME_SS_PERM_DENY;

    int mode = CDW(req,13);
    loff_t offs = CDW(req,12);
    loff_t len = CDW(req,14);

    int status_fallocate = fallocate(fd, mode, offs, len);

    req->cqe.result = CPU2DW(status_fallocate);

    return NVME_SUCCESS;
}

/**
 * StorStack I/O Command for QEMU NVMe Driver
 * change file size
 * 
 * \param cdw10 fd
 * \param cdw13 file length
 * \param cqe_dw0 ftruncate ret
 * \return NvmeStatusCodes
 * \note int ss_ftruncate(int fd, off_t length)
 */
uint16_t nvme_ss_ftruncate(NvmeCtrl *n, NvmeRequest *req)
{
    // if (verify_user_cred_by_req(req)) return NVME_SS_PERM_DENY;

    int fd = CDW(req,10);
    // FIXME: anyone may perform this operation
    // if (fstats[fd].st == 0) return NVME_SS_PERM_DENY;

    off_t file_len = CDW(req,13);

    int status_ftruncate = ftruncate(fd, file_len);

    req->cqe.result = CPU2DW(status_ftruncate);

    return NVME_SUCCESS;
}

/**
 * StorStack I/O Command for QEMU NVMe Driver
 * file rename
 * 
 * \param cdw10 old filename length
 * \param cdw11 new filename length
 * \param dma_buf old file path with \0 && new file path with \0
 * \param cqe_dw0 rename ret
 * \return NvmeStatusCodes
 * \note int ss_rename(const char *oldpath, const char *newpath)
 * \bug unused, untested
 */
uint16_t nvme_ss_rename(NvmeCtrl *n, NvmeRequest *req)
{
    if (verify_user_cred_by_req(req)) return NVME_SS_PERM_DENY;
    // FIXME: anyone may perform this operation

    size_t old_file_name_len = CDW(req,10); // with '\0'
    size_t new_file_name_len = CDW(req,11); // with '\0'
    uint32_t total_len = old_file_name_len + new_file_name_len;

    char *buffer = (char *)malloc(sizeof(char) * total_len);

    uint16_t status = nvme_h2c(n, buffer, total_len, req);
    if (status)
        goto invalid;

    int status_rename = rename(buffer, buffer + old_file_name_len);
    req->cqe.result = CPU2DW(status_rename);

    free(buffer);
    return NVME_SUCCESS;

invalid:
    free(buffer);
    return NVME_DNR;
}


/**
 * StorStack I/O Command for QEMU NVMe Driver
 * latency simulation with return value but no operations
 * 
 * \param cqe_dw0 0
 * \return NvmeStatusCodes
 * \note int ss_lat_with_ret()
 */
uint16_t nvme_ss_lat_with_ret(NvmeCtrl *n, NvmeRequest *req)
{
#ifdef SS_LATENCY_TIMER
    ss_latency_print(g_timer_count, 7); // XXX: latency timer printer
    g_timer_count = 0; // Reset ptr for next usage
#endif
    req->cqe.result = CPU2DW(0);
    return NVME_SUCCESS;
}

/**
 * StorStack I/O Command for QEMU NVMe Driver
 * latency simulation with return value read
 * 
 * \param cqe_dw0 0
 * \return NvmeStatusCodes
 * \note int ss_lat_with_ret_r()
 */
uint16_t nvme_ss_lat_with_ret_r(NvmeCtrl *n, NvmeRequest *req)
{
    int buf_len = CDW(req,11); 
    uint8_t buf[buf_len];
    uint16_t status = nvme_c2h(n, buf, buf_len, req);
    req->cqe.result = CPU2DW(0);
    return NVME_SUCCESS;
}

/**
 * StorStack I/O Command for QEMU NVMe Driver
 * latency simulation with return value write
 * 
 * \param cqe_dw0 0
 * \return NvmeStatusCodes
 * \note int ss_lat_with_ret_w()
 */
uint16_t nvme_ss_lat_with_ret_w(NvmeCtrl *n, NvmeRequest *req)
{
    int buf_len = CDW(req,11); 
    uint8_t buf[buf_len];
    uint16_t status = nvme_h2c(n, buf, buf_len, req);
    req->cqe.result = CPU2DW(0);
    return NVME_SUCCESS;
}

/**
 * StorStack I/O Command for QEMU NVMe Driver
 * latency simulation no return value
 * 
 * \param cqe_dw0 0
 * \return NvmeStatusCodes
 * \note int ss_lat_no_ret()
 */
uint16_t nvme_ss_lat_no_ret(NvmeCtrl *n, NvmeRequest *req)
{
    int lat = CDW(req,11); // XXX: tmp for latency simulation switch
    latency_sim_process_sq_ns = lat;
    
    printf("[QEMU_latency_switch] latency set to: %d\n", latency_sim_process_sq_ns);

    req->cqe.result = CPU2DW(0);
    return NVME_SUCCESS;
}