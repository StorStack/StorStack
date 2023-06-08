#include "ulibss.h"
#include "klibss.h"

#include <aio.h>

struct ss_aiocb
{
    int aio_fildes;               /* File desriptor.  */
    __off64_t aio_offset;         /* File offset.  */
    volatile void *aio_buf;       /* Location of buffer. Must be allocated with spdk_dma_malloc()  */
    size_t aio_nbytes;            /* Length of transfer.  */
    int aio_reqprio;              /* Request priority offset.  */
    struct sigevent aio_sigevent; /* Signal number and value.  */

    // lio not implemented yet
    int aio_lio_opcode; /* Operation to be performed.  */

    // StorStack internals
    int __qpair_id;
    struct cb_ret __cbret;
    volatile void *__dma_buf;
    void (*__buf_cb)(struct ss_aiocb *aiocbp);
};

int ss_aio_read(struct ss_aiocb *aiocbp);
int ss_aio_write(struct ss_aiocb *aiocbp);
int ss_aio_fsync(int op, struct ss_aiocb *aiocbp);
int ss_aio_error(const struct ss_aiocb *aiocbp);
ssize_t ss_aio_return(struct ss_aiocb *aiocbp);
int ss_aio_suspend(const struct ss_aiocb *const aiocb_list[], int nitems,
                   const struct timespec *restrict timeout);
int ss_aio_cancel(int fd, struct ss_aiocb *aiocbp);
int ss_lio_listio(int mode, struct ss_aiocb *restrict const aiocb_list[restrict],
                  int nitems, struct sigevent *restrict sevp);