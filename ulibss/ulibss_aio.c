#include "ulibss_aio.h"

#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <signal.h>
#include <pthread.h>

extern struct ss_ns_entry *get_ns_entry();
extern uint8_t get_qpair_id(int fd);

extern __thread struct spdk_nvme_qpair *ss_qpair[SS_QPAIR_NUM];

int ss_pread_raw(int fd, void *buf, size_t len, off_t offset, struct cb_ret *rets, int qpair_id, spdk_nvme_cmd_cb cbfn);
int ss_pwrite_raw(int fd, const void *buf, size_t len, off_t offset, struct cb_ret *rets, int qpair_id, spdk_nvme_cmd_cb cbfn);
int ss_fsync_raw(int fd, struct cb_ret *rets, int qpair_id, spdk_nvme_cmd_cb cbfn);

void siggen(sigevent_t *sigev)
{
    pthread_t t;
    if (sigev == NULL)
        return;
    switch (sigev->sigev_notify)
    {
    case SIGEV_NONE:
        return;
    case SIGEV_SIGNAL:
        sigqueue(getpid(), sigev->sigev_signo, sigev->sigev_value);
        break;
    case SIGEV_THREAD:
        pthread_create(&t, sigev->sigev_notify_attributes, sigev->sigev_notify_function, (void *)(sigev->sigev_value.sival_ptr));
        break;
    case SIGEV_THREAD_ID:
        return;
    default:
        break;
    }
}

static void __ss_aio_cb(void *arg, const struct spdk_nvme_cpl *completion)
{
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

        if (cb_ret->__aiocbp->__buf_cb)
        {
            // do buffer copy or clean up for RAW buf support.
            cb_ret->__aiocbp->__buf_cb(cb_ret->__aiocbp);
        }
        
        siggen(&cb_ret->__aiocbp->aio_sigevent);
    }
}

static void __rd_dma_cb(struct ss_aiocb *aiocbp)
{
    ss_copy_from_buf(aiocbp->__dma_buf, aiocbp->aio_buf, aiocbp->aio_nbytes);
    ss_buf_clear(aiocbp->__dma_buf);
}

static void __wr_dma_cb(struct ss_aiocb *aiocbp)
{
    ss_buf_clear(aiocbp->__dma_buf);
}

int ss_aio_read(struct ss_aiocb *aiocbp) {
    int ret;
    int qpair_id = get_qpair_id(aiocbp->aio_fildes);
    aiocbp->__qpair_id = qpair_id;
    aiocbp->__cbret.__aiocbp = aiocbp;
    aiocbp->__dma_buf = aiocbp->aio_buf;
    aiocbp->__buf_cb = NULL;
#ifdef RAW_BUF_SUPPORT
    aiocbp->__dma_buf = ss_buf_init(aiocbp->aio_nbytes);
    aiocbp->__buf_cb = __rd_dma_cb;
#endif
    ret = ss_pread_raw(aiocbp->aio_fildes, aiocbp->__dma_buf, aiocbp->aio_nbytes, aiocbp->aio_offset, &aiocbp->__cbret, qpair_id, __ss_aio_cb);
    if (ret < 0) {
        errno = EAGAIN;
        return -1;
    }
    return 0;
}

int ss_aio_write(struct ss_aiocb *aiocbp)
{
    int ret;
    int qpair_id = get_qpair_id(aiocbp->aio_fildes);
    aiocbp->__qpair_id = qpair_id;
    aiocbp->__cbret.__aiocbp = aiocbp;
    aiocbp->__dma_buf = aiocbp->aio_buf;
    aiocbp->__buf_cb = NULL;
#ifdef RAW_BUF_SUPPORT
    aiocbp->__dma_buf = ss_copy_to_buf(aiocbp->aio_nbytes, aiocbp->aio_buf);
    aiocbp->__buf_cb = __wr_dma_cb;
#endif
    // TODO: handle file with O_APPEND
    ret = ss_pwrite_raw(aiocbp->aio_fildes, aiocbp->__dma_buf, aiocbp->aio_nbytes, aiocbp->aio_offset, &aiocbp->__cbret, qpair_id, __ss_aio_cb);
    if (ret < 0) {
        errno = EAGAIN;
        return -1;
    }
    return 0;
}

// FIXME: op is useless only if func is opaque
// FIXME: EBDAF should be implemented
int ss_aio_fsync(int op, struct ss_aiocb *aiocbp)
{
    int ret;
    int qpair_id = get_qpair_id(aiocbp->aio_fildes);
    aiocbp->__qpair_id = qpair_id;
    aiocbp->__cbret.__aiocbp = aiocbp;
    aiocbp->__buf_cb = NULL;
    ret = ss_fsync_raw(aiocbp->aio_fildes, &aiocbp->__cbret, qpair_id, __ss_aio_cb);
    if (ret < 0) {
        errno = EAGAIN;
        return -1;
    }
    return 0;
}

int ss_aio_error(const struct ss_aiocb *aiocbp) {
#ifndef PTHREAD_DAEMON
    // process the cq of the request once
    spdk_nvme_qpair_process_completions(ss_qpair[aiocbp->__qpair_id], 0);
#endif

    if (aiocbp->__cbret.isvalid) // proc complete
    {
        // siggen(&aiocbp->aio_sigevent);
        return 0;
    }

    return EINPROGRESS;
}

ssize_t ss_aio_return(struct ss_aiocb *aiocbp)
{
    if (!aiocbp->__cbret.isvalid) // proc inprogress
    {
        return -1;
    }

    return aiocbp->__cbret.data;
}

int ss_aio_suspend(const struct ss_aiocb *const aiocb_list[], int nitems, const struct timespec *restrict timeout)
{
    struct ss_aiocb *aiocbp;
    struct timespec expire, now;
    int unfinished;

    if (timeout)
    {
        clock_gettime(CLOCK_MONOTONIC, &expire);
        expire.tv_nsec += timeout->tv_nsec;
        expire.tv_sec += timeout->tv_sec + expire.tv_nsec / 1000000000;
        expire.tv_nsec %= 1000000000;
    }

    // avoid endless loop when all pointers are set to NULL
    do
    {
        unfinished = 0;
        for (int i = 0; i < nitems; i++)
        {
            aiocbp = aiocb_list[i];
            if (aiocbp == NULL) continue;
#ifndef PTHREAD_DAEMON
            spdk_nvme_qpair_process_completions(ss_qpair[aiocbp->__qpair_id], 0);
#endif
            if (aiocbp->__cbret.isvalid)
            {
                // siggen(&aiocbp->aio_sigevent);
                // if (aiocbp->aio_sigevent.sigev_notify == SIGEV_SIGNAL)
                //     errno = EINTR;
                
                return 0;
            }
            else unfinished++;
        }
        if (timeout)
        {
            clock_gettime(CLOCK_MONOTONIC, &now);
            if ((now.tv_sec > expire.tv_sec) ||
                (now.tv_sec == expire.tv_sec && now.tv_nsec > expire.tv_nsec))
            {
                errno = EAGAIN;
                return -1;
            }
        }
    } while(unfinished);
    // return ENOSYS;
}

int ss_aio_cancel(int fd, struct ss_aiocb *aiocbp)
{
    return AIO_NOTCANCELED;
}

int ss_lio_listio(int mode, struct ss_aiocb *restrict const aiocb_list[restrict], int nitems, struct sigevent *restrict sevp)
{
    int ret;
    int qpair_id;
    int err;
    int unfinished;
    struct ss_aiocb *aiocbp;

    err = 0;
    for (int i = 0; i < nitems; i++)
    {
        aiocbp = aiocb_list[i];
        if (aiocbp == NULL || aiocbp->aio_lio_opcode == LIO_NOP)
            continue;

        qpair_id = get_qpair_id(aiocbp->aio_fildes);
        aiocbp->__qpair_id = qpair_id;
        aiocbp->__dma_buf = aiocbp->aio_buf;
        aiocbp->__buf_cb = NULL;

        switch (aiocbp->aio_lio_opcode)
        {
        case LIO_READ:
#ifdef RAW_BUF_SUPPORT
            aiocbp->__dma_buf = ss_buf_init(aiocbp->aio_nbytes);
            aiocbp->__buf_cb = __rd_dma_cb;
#endif
            ret = ss_pread_raw(aiocbp->aio_fildes, aiocbp->__dma_buf, aiocbp->aio_nbytes, aiocbp->aio_offset, &aiocbp->__cbret, qpair_id, __ss_aio_cb);
            break;
        case LIO_WRITE:
#ifdef RAW_BUF_SUPPORT
            aiocbp->__dma_buf = ss_copy_to_buf(aiocbp->aio_nbytes, aiocbp->aio_buf);
            aiocbp->__buf_cb = __wr_dma_cb;
#endif
            ret = ss_pwrite_raw(aiocbp->aio_fildes, aiocbp->__dma_buf, aiocbp->aio_nbytes, aiocbp->aio_offset, &aiocbp->__cbret, qpair_id, __ss_aio_cb);
            break;
        default:
            continue;
        }

        if (ret < 0)
            err++;
    }
    
    if (mode == LIO_NOWAIT) {
        goto out;
    }
    else if (mode != LIO_WAIT)
    {
        errno = EINVAL;
        return -1;
    }

    do
    {
        unfinished = 0;
        for (int i = 0; i < nitems; i++)
        {
            aiocbp = aiocb_list[i];
            if (aiocbp == NULL || aiocbp->aio_lio_opcode == LIO_NOP)
                continue;

            if (aiocbp->__cbret.isvalid) continue;
#ifndef PTHREAD_DAEMON
            // not ready, process cq
            spdk_nvme_qpair_process_completions(ss_qpair[aiocbp->__qpair_id], 0);
#endif
            if (aiocbp->__cbret.isvalid)
                // siggen(&aiocbp->aio_sigevent);
                ;
            else
                unfinished++;

            // // check if the request is complete
            // if (!aiocbp->__cbret.isvalid)
            // {
            //     unfinished++;
            //     // not complete, process cq and wait for the next round's check
            //     spdk_nvme_qpair_process_completions(ss_qpair[aiocbp->__qpair_id], 0);
            // }
        }
    } while (unfinished);

out:
    if (err)
    {
        errno = EIO;
        return -1;
    }
    return 0;
}
