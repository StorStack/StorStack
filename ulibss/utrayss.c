#include "utrayss.h"

/**
 * StorStack seckey temp storage
 */
static u8 seckey[SECKEY_LEN];
static void __ss_skey_cb(void *arg, const struct spdk_nvme_cpl *completion);


/********** ADMIN OPS ONLY **********/
static int ss_init_spdk();
static int ss_fini_spdk();

static int __ss_init_klib();
static int __ss_fini_klib();
static int __ss_get_token(u8 *token);
static int __ss_get_seckey(u8 *seckey);

static uint32_t __get_user_id();

struct ss_ctrlr_entry;
struct ss_ns_entry;
struct cb_ret;
struct ss_buf; // buffer structure for cmds

/**
 * StorStack struct definitions
 */
// cb_ret is used to transfer callback returns
struct cb_ret
{
    uint32_t data;    // return data
    bool isvalid;     // is valid for check
    bool ispermitted; // is permitted for uid check res
};

// delete compatibility with TAILQ
struct ss_ctrlr_entry
{
    struct spdk_nvme_ctrlr *ctrlr;
};

// entry
struct ss_ns_entry
{
    struct spdk_nvme_ctrlr *ctrlr;
    struct spdk_nvme_ns *ns;
    struct spdk_nvme_qpair *qpair; // contain all qpairs
};

// buffer structure for cmds
struct ss_buf
{
    size_t buf_size;
    void *buf;
};


static bool __probe_cb(void *cb_ret, const struct spdk_nvme_transport_id *trid, struct spdk_nvme_ctrlr_opts *opts);
static void __attach_cb(void *cb_ret, const struct spdk_nvme_transport_id *trid, struct spdk_nvme_ctrlr *ctrlr, const struct spdk_nvme_ctrlr_opts *opts);
static struct ss_buf *ss_copy_to_buf(int size, const void *data);

/**
 * StorStack global variables
 */
// spdk env opts
static struct spdk_env_opts env_opts = {};
// ctrlr and ns queue
static struct ss_ctrlr_entry *static_ctrlr_entry;
// shared ns_entry, one-time initialize, multiple time it can use
static struct ss_ns_entry *ns_entry;
// ss klib virtual file description
static int klib_fd;

/**
 * StorStack Get UID
 */
static uint32_t __get_user_id()
{
    return getuid(); // unistd.h
}

/**
 * StorStack Probe Callback Function
 *
 * \param cb_ret Opaque value passed to spdk_nvme_probe().
 * \param trid NVMe transport identifier. We use PCIe so it is useless.
 */
static bool __probe_cb(void *cb_ret, const struct spdk_nvme_transport_id *trid, struct spdk_nvme_ctrlr_opts *opts)
{
    // FIXME: nothing to do
#ifdef SS_DEBUG
    printf("Attaching to %s\n", trid->traddr);
#endif
    return true;
}

/**
 * StorStack Attach Callback Function
 *
 * \param cb_ret Opaque value passed to spdk_nvme_attach_cb().
 * \param trid NVMe transport identifier. We use PCIe so it is useless.
 * \param ctrlr Opaque handle to NVMe controller.
 * \param opts NVMe controller initialization options that were actually used.
 * Options may differ from the requested options from the attach call depending
 * on what the controller supports.
 */
static void __attach_cb(void *cb_ret, const struct spdk_nvme_transport_id *trid, struct spdk_nvme_ctrlr *ctrlr, const struct spdk_nvme_ctrlr_opts *opts)
{
    int nsid;
    const struct spdk_nvme_ctrlr_data *tmp_ctrlr_data;
    struct spdk_nvme_ns *ns;

    // attach ctrlr
    static_ctrlr_entry = malloc(sizeof(struct ss_ctrlr_entry));
    if (static_ctrlr_entry == NULL)
    {
        perror("Ctrlr entry init malloc");
        goto error_exit;
    }

    tmp_ctrlr_data = spdk_nvme_ctrlr_get_data(ctrlr);

#ifdef SS_DEBUG
    printf("ctrlr_data: %-20.20s (%-20.20s)\n", tmp_ctrlr_data->mn, tmp_ctrlr_data->sn);
#endif

    static_ctrlr_entry->ctrlr = ctrlr;

    // attach ns
    for (nsid = spdk_nvme_ctrlr_get_first_active_ns(ctrlr); nsid != 0; nsid = spdk_nvme_ctrlr_get_next_active_ns(ctrlr, nsid))
    {
        ns = spdk_nvme_ctrlr_get_ns(ctrlr, nsid);
        if (ns == NULL || !spdk_nvme_ns_is_active(ns))
        {
#ifdef SS_DEBUG
            printf("skip a ns\n");
#endif
            continue;
        }
        ns_entry = malloc(sizeof(struct ss_ns_entry));
        if (ns_entry == NULL)
        {
            perror("Namespace init");
            goto error_exit;
        }
        ns_entry->ctrlr = ctrlr;
        ns_entry->ns = ns;
#ifdef SS_DEBUG
        printf("Namespace ID: %d\n", spdk_nvme_ns_get_id(ns));
        printf("Size: %ju\n", spdk_nvme_ns_get_size(ns));
#endif
        // only one we need
        // break;
    }

    if (ns_entry == NULL)
    {
        printf("No namespace can be found!\n");
        goto error_exit;
    }
    return;

error_exit:
#ifdef SS_DEBUG
    printf("__attach_cb failed!\n");
#endif
    ss_fini_spdk();
    exit(-1);
}

/**
 * StorStack SPDK init Functions
 */
static int ss_init_spdk()
{
    static_ctrlr_entry = NULL;
    ns_entry = NULL;

    // Compared with ulibss.c, klibss ignored. (No seckey)

    ////////// env opts init //////////
    spdk_env_opts_init(&env_opts);
    env_opts.name = "StorStack Ulib";
    env_opts.shm_id = 1; // FIXME: TEST: All programs use the same mem (qpair)

#ifdef SS_DEBUG
    printf("spdk_env_opts_init ok!\n");
#endif

    int init_opt_ret = spdk_env_init(&env_opts);
    if (init_opt_ret < 0)
    {
        printf("env init failed: %d\n", init_opt_ret);
        goto error_exit;
    }

#ifdef SS_DEBUG
    printf("spdk_env_init ok!\n");
#endif

    // use probe to find ctrlr and ns
    int probe_ret = spdk_nvme_probe(NULL, NULL, __probe_cb, __attach_cb, NULL);
    if (probe_ret)
    {
        printf("NVMe probe failed: %d\n", probe_ret);
        goto error_exit;
    }

#ifdef SS_DEBUG
    printf("spdk_nvme_probe ok!\n");
#endif

    // Ctrlr Init
    // check after probe
    if (static_ctrlr_entry == NULL || ns_entry == NULL)
    {
        printf("Controllers not found!\n");
        goto error_exit;
    }

    // Queue Pair Init
    // TODO: Core Bindings, I think it is in spdk_nvme_ctrlr_alloc_io_qpair
    // It seems that core binding is automatically done
    // in term of active proc in ctrlr
    ns_entry->qpair = spdk_nvme_ctrlr_alloc_io_qpair(ns_entry->ctrlr, NULL, 0);
    if (ns_entry->qpair == NULL)
    {
        printf("NVMe QPair init failed\n");
        goto error_exit;
    }

#ifdef SS_DEBUG
    printf("spdk_nvme_ctrlr_alloc_io_qpair ok!\n");
#endif

    return 0;

error_exit:
#ifdef SS_DEBUG
    printf("Error exit\n");
#endif
    return -1;
}

static int ss_fini_spdk()
{
    // free qpair
    spdk_nvme_ctrlr_free_io_qpair(ns_entry->qpair);

    // only one namespace, so use free is ok.
    free(ns_entry);

    // former one
    // spdk_nvme_detach_async(static_ctrlr_entry->ctrlr, &detach_ctx);
    // only one ctrlr should be detached.
    spdk_nvme_detach(static_ctrlr_entry->ctrlr);
    free(static_ctrlr_entry);

    spdk_env_fini();
    return 0;
}

/**
 * StorStack I/O Command
 * copy from normal memory to DMA safe memory
 *
 * @param size data size
 * @param data data mem ptr
 */
static struct ss_buf *ss_copy_to_buf(int size, const void *data)
{
    struct ss_buf *buffer = NULL;
    buffer = malloc(sizeof(struct ss_buf));

    buffer->buf_size = size;
    buffer->buf = spdk_dma_malloc(size, 0, NULL); // TODO: is align == 0 ok?????

    memcpy(buffer->buf, data, size);
    return buffer;
}

/**
 * StorStack Klib Functions
 */
static int __ss_init_klib()
{
    klib_fd = open(STORSTACK_DEVNAME, O_RDWR);
    if (klib_fd < 0)
    {
        printf("cannot open klib device: %d\n", klib_fd);
        return -1;
    }
    return 0;
}

static int __ss_fini_klib()
{
    return close(klib_fd);
}

static int __ss_get_token(u8 *token)
{
    // u8 token[TOKEN_LEN];
    int ret;
    if ((ret = ioctl(klib_fd, SSIOC_GENERATE_TOKEN, token)) < 0)
    {
        printf("ioctl failed: %d\n", ret);
        exit(-1);
    }
#ifdef SS_DEBUG
    printf("StorStack token: ");
    int i;
    for (i = 0; i < TOKEN_LEN; i++)
    {
        printf("%02x ", token[i]);
    }
    printf("\n");
#endif
}

static int __ss_gen_seckey(u8 *seckey)
{
    // u8 seckey[SECKEY_LEN];
    int ret;
    if ((ret = ioctl(klib_fd, SSIOC_UPDATE_SECKEY, seckey)) < 0)
    {
        printf("ioctl failed: %d\n", ret);
        exit(-1);
    }

#ifdef SS_DEBUG
    printf("StorStack new secret: ");
    int i;
    for (i = 0; i < SECKEY_LEN; i++)
    {
        printf("%02x ", seckey[i]);
    }
    printf("\n");
#endif
}

/********** ADMIN OPS END **********/


/**
 * StorStack secret key callback function
 */
static void __ss_skey_cb(void *arg, const struct spdk_nvme_cpl *completion)
{
    struct cb_ret *cb_ret = arg;
    if (spdk_nvme_cpl_is_error(completion))
    {
        fprintf(stderr, "[ERROR] ssklib_admin_op : NVME CPL is error!\n");
    }
    else
    {
        cb_ret->data = completion->cdw0; // cdw0 is our return value
        cb_ret->isvalid = true;
    }
}

/**
 * StorStack generate secret key from trusty kernel
 */
static void generate_seckey()
{
    __ss_init_klib();
    __ss_gen_seckey(seckey);
}

/**
 * StorStack send secret key to storage device
 *
 * \return status
 */
static int send_seckey_to_dev()
{
    ss_init_spdk();
    struct cb_ret rets = {};
    rets.isvalid = false;

#ifdef SS_DEBUG
    printf("send_seckey_to_dev part 1 ok!\n");
#endif

    struct ss_buf *buffer = ss_copy_to_buf(256, seckey);

#ifdef SS_DEBUG
    printf("send_seckey_to_dev part 2 ok!\n");
#endif

    struct spdk_nvme_cmd cmd = {};
    cmd.opc = SPDK_NVME_OPC_SS_SKEY;

    int ret = spdk_nvme_ctrlr_cmd_admin_raw(ns_entry->ctrlr, &cmd, buffer->buf, buffer->buf_size, __ss_skey_cb, &rets);
    if (ret)
    {
        printf("Send Secret key to device failed!\n");
        return SEND_ERROR;
    }
#ifdef SS_DEBUG
    printf("send_seckey_to_dev part 3 ok!\n");
#endif
    while (!rets.isvalid)
    {
        spdk_nvme_ctrlr_process_admin_completions(ns_entry->ctrlr);
    }
#ifdef SS_DEBUG
    printf("send_seckey_to_dev part 4 ok!\n");
#endif
    return SUCCESS;
}

int main()
{
    // The program must be run by root, otherwise security risks exist.
    if (__get_user_id() != 0)
    {
        printf("Secret Key must be created and sent by root!\n");
        return NO_ROOT;
    }

    generate_seckey();
    int send_ret = send_seckey_to_dev();
    if (send_ret)
        return SEND_ERROR;

    printf("Init Secret Key done!\n");

    return 0;
}