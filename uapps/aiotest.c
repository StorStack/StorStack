#include <string.h>
#include <unistd.h>
#include <signal.h>

#include "ulibss.h"
// #include "klibss.h"
#include "ulibss_aio.h"

#define BFSZ 128
#define PATH "/tmp/aio_test.txt"

#ifdef RAW_BUF_SUPPORT
#define RAW_BUF_SUPPORT_TEST
#endif


void *sig_handler(int sig, siginfo_t *info, void *ctx) {
    struct ss_aiocb *req;
    int ret;
    if (info->si_signo == SIGIO)
    {
        req = (struct ss_aiocb*)info->si_value.sival_ptr;
        if (ss_aio_error(req) == 0)
        {
            ret = ss_aio_return(req);
            printf("aio sig test: ret: %d\n", ret);
            ((char*)req->aio_buf)[BFSZ / 2-1] = 0;
            printf("aio sig test: buf: %s\n", req->aio_buf);
#ifndef RAW_BUF_SUPPORT_TEST
            ss_buf_clear(req->aio_buf);
#else
            free(req->aio_buf);
#endif
            printf("aio sig test: buf clear\n");
        }
        else {
            perror("aio sig test: failed 2\n");
        }
    }
    
}

int main() {
    const char path[] = PATH;
    int fd;
    int ret;

    printf("aio test init\n");
    ss_init_spdk();

    printf("aio test start\n");
    fd = ss_open(path, O_RDWR | O_CREAT, 0777);
    printf("aio test fd: %d\n", fd);

    // aio write test
    struct ss_aiocb wr;
    bzero((void*)&wr, sizeof(wr));
#ifdef RAW_BUF_SUPPORT_TEST
    wr.aio_buf = malloc(BFSZ);
#else
    wr.aio_buf = ss_buf_init(BFSZ);
#endif
    memset(wr.aio_buf, 'a', BFSZ);
    wr.aio_fildes = fd;
    wr.aio_nbytes = BFSZ;
    wr.aio_offset = 0;
    ret = ss_aio_write(&wr);
    if (ret < 0)
    {
        perror("aio write test: failed 1\n");
    }
    printf("aio write test: started\n");
    while (ss_aio_error(&wr) == EINPROGRESS);
    printf("aio write test: progress done\n");
    ret = ss_aio_return(&wr);
    if (ret > 0) {
        printf("aio write test: ret val %d\n", ret);
    } else {
        perror("aio write test: failed 2\n");
    }
#ifndef RAW_BUF_SUPPORT_TEST
    ss_buf_clear(wr.aio_buf);
#else
    free(wr.aio_buf);
#endif

    // aio read test
    struct ss_aiocb rd;
    bzero((void*)&rd, sizeof(rd));
#ifndef RAW_BUF_SUPPORT_TEST
    rd.aio_buf = ss_buf_init(BFSZ);
#else
    rd.aio_buf = malloc(BFSZ);
#endif
    rd.aio_fildes = fd;
    rd.aio_nbytes = BFSZ / 2;
    rd.aio_offset = 0;
    ret = ss_aio_read(&rd);
    if (ret < 0)
    {
        perror("aio read test: failed 1\n");
    }
    printf("aio read test: started\n");
    while (ss_aio_error(&rd) == EINPROGRESS);
    printf("aio read test: progress done\n");
    ret = ss_aio_return(&rd);
    if (ret > 0) {
        printf("aio read test: ret val %d\n", ret);
        ((char*)rd.aio_buf)[BFSZ-1] = 0;
        printf("aio read test: buf: %s\n", rd.aio_buf);
    } else {
        perror("aio read test: failed 2\n");
    }
#ifndef RAW_BUF_SUPPORT_TEST
    ss_buf_clear(rd.aio_buf);
#else
    free(rd.aio_buf);
#endif

    // aio suspend test
    struct ss_aiocb *cbs[5];
    bzero((void*)cbs, sizeof(cbs));
    struct ss_aiocb cb1, cb2, cb3;

    bzero((void*)&cb1, sizeof(cb1));
#ifndef RAW_BUF_SUPPORT_TEST
    cb1.aio_buf = ss_buf_init(BFSZ/2);
#else
    cb1.aio_buf = malloc(BFSZ/2);
#endif
    cb1.aio_fildes = fd;
    cb1.aio_nbytes = BFSZ / 2;
    cb1.aio_offset = 0;
    ret = ss_aio_read(&cb1);
    if (ret < 0)
    {
        perror("aio suspend test: failed 1\n");
    }

    bzero((void*)&cb2, sizeof(cb2));
#ifndef RAW_BUF_SUPPORT_TEST
    cb2.aio_buf = ss_buf_init(BFSZ/2);
#else
    cb2.aio_buf = malloc(BFSZ/2);
#endif
    memset(cb2.aio_buf, 'b', BFSZ / 2);
    cb2.aio_fildes = fd;
    cb2.aio_nbytes = BFSZ / 2;
    cb2.aio_offset = BFSZ / 4;
    ret = ss_aio_write(&cb2);
    if (ret < 0)
    {
        perror("aio suspend test: failed 2\n");
    }

    bzero((void*)&cb3, sizeof(cb3));
#ifndef RAW_BUF_SUPPORT_TEST
    cb3.aio_buf = ss_buf_init(BFSZ);
#else
    cb3.aio_buf = malloc(BFSZ/2);
#endif
    cb3.aio_fildes = fd;
    cb3.aio_nbytes = BFSZ / 2;
    cb3.aio_offset = 0;
    ret = ss_aio_read(&cb3);
    if (ret < 0)
    {
        perror("aio suspend test: failed 3\n");
    }

    cbs[0] = &cb1;
    cbs[1] = &cb2;
    cbs[2] = &cb3;
    printf("aio suspend test: suspending\n");
    int unfinished;
    do
    {
        unfinished = 0;
        ret = ss_aio_suspend(cbs, 5, NULL);
        printf("aio suspend test: once\n");
        for (int i = 0; i < 3; i++)
        {
            ret = ss_aio_error(cbs[i]);
            if (ret == 0)
            {
                ret = ss_aio_return(cbs[i]);
                if (ret < 0)
                {
                    fprintf(stderr, "aio suspend test: failed 4@%d\n", i);
                } else
                {
                    printf("aio suspend test: %d ret val %d\n", i, ret);
                }
                
            } else if(ret == EINPROGRESS) unfinished++;
            else perror("aio suspend test: failed 5\n");
        }
    } while (unfinished);
    
    ((char*)cb1.aio_buf)[BFSZ / 2-1] = 0;
    ((char*)cb3.aio_buf)[BFSZ / 2-1] = 0;
    printf("aio suspend test: buf1: %s\n", cb1.aio_buf);
    printf("aio suspend test: buf3: %s\n", cb3.aio_buf);
#ifndef RAW_BUF_SUPPORT_TEST
    ss_buf_clear(cb1.aio_buf);
    ss_buf_clear(cb2.aio_buf);
    ss_buf_clear(cb3.aio_buf);
#else
    free(cb1.aio_buf);
    free(cb2.aio_buf);
    free(cb3.aio_buf);
#endif

    // aio signal test
    struct sigaction act;
    printf("aio sig test: set handler\n");
    act.sa_sigaction = sig_handler;
    sigemptyset(&act.sa_mask);
    act.sa_flags = SA_SIGINFO;
    sigaction(SIGIO, &act, NULL);

    struct ss_aiocb my_aiocb;
    printf("aio sig test: set aiocb\n");
    bzero(&my_aiocb, sizeof(struct ss_aiocb));
    my_aiocb.aio_fildes = fd;
#ifndef RAW_BUF_SUPPORT_TEST
    my_aiocb.aio_buf = ss_buf_init(BFSZ / 2);
#else
    my_aiocb.aio_buf = malloc(BFSZ/2);
#endif
    // memset(my_aiocb.aio_buf, 'c', 64);
    my_aiocb.aio_nbytes = BFSZ / 2;
    my_aiocb.aio_offset = 0;

    my_aiocb.aio_sigevent.sigev_notify = SIGEV_SIGNAL;
    my_aiocb.aio_sigevent.sigev_signo = SIGIO;
    my_aiocb.aio_sigevent.sigev_value.sival_ptr = &my_aiocb;

    printf("aio sig test: start\n");
    ret = ss_aio_read(&my_aiocb);
    if (ret < 0)
    {
        perror("aio sig test: failed 1\n");
    }
    sleep(10);

    printf("aio sig test: done\n");

    ss_fini_spdk();
    return 0;
}