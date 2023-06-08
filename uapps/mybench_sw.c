#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>

#include "ulibss.h"
#include "ulibss_aio.h"

#include "../ulibss/profile_utils.h"

#define PATH "/tmpss/mytest1"
#define ROUND_SZ 1073741824

#define LONG_MAX 1000000000L

pthread_t threads[255];
pthread_rwlock_t ready_lock;
volatile int readys[255];

volatile int stop_now = 0;

volatile unsigned long long bytes[255];

int trd_cnt;
int blk_sz;
int run_time;

// void cleanup_close(void *arg)
// {
//     printf("--cleanup-- closing\n");
//     ss_close((int)arg);
//     printf("--cleanup-- closed\n");
// }

// void cleanup_free(void *arg)
// {
//     printf("--cleanup-- freeing\n");
//     ss_buf_clear(arg);
//     printf("--cleanup-- freed\n");
// }

void get_rand_ops(int num)
{
    
}

void *thread_entry_sread(void *arg)
{
    // pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    // pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);

    ss_init_qpair();

    int me = (int)arg;
    readys[me] = 1;
    void *buf = ss_buf_init(sizeof(char) * blk_sz);
    // pthread_cleanup_push(cleanup_free, buf);
    printf("thread %d start ss_open\n", me);
    int fd = ss_open(PATH, O_RDWR | O_CREAT, 0777);
    printf("thread %d ok ss_open\n", me);
    // pthread_cleanup_push(cleanup_close, fd);

    int offset = 0;

    // wait until the main thread allow me to start
    printf("thread %d start rwlock\n", me);
    pthread_rwlock_rdlock(&ready_lock);
    pthread_rwlock_unlock(&ready_lock);
    printf("thread %d end rwlock\n", me);


    // TODO: do the bench work
    while (!stop_now)
    {
        // offset %= ROUND_SZ;
        // ss_pread(fd, buf, blk_sz, offset);
        ss_write(fd, buf, blk_sz);
        bytes[me] += blk_sz;
        // offset += blk_sz;
        // pthread_testcancel();
    }

    printf("thread stop %d\n", me);
    ss_close(fd);
    ss_buf_clear(buf);

    ss_fini_qpair();
}

// mybench trd_cnt blk_sz run_time
int main(int argc, char **argv)
{
    trd_cnt = atoi(argv[1]);
    blk_sz = atoi(argv[2]);
    run_time = atoi(argv[3]);

    printf("mybench seqread %d threads %d Bytes %d seconds\n", trd_cnt, blk_sz, run_time);

    struct timespec start, end;

    ss_init_spdk();

    INIT_TIME_REC;

    memset(readys, 0, 255);
    memset(bytes, 0, 255);

    // lock first, all threads should wait now
    pthread_rwlock_init(&ready_lock, NULL);
    pthread_rwlock_wrlock(&ready_lock);

    // start all threads
    for (int i = 0; i < trd_cnt; i++)
    {
        pthread_create(&threads[i], NULL, thread_entry_sread, i);
    }

    // wait for all threads to be ready
    int flag;
    do
    {
        flag = 0;
        for (int i = 0; i < trd_cnt; i++)
        {
            if (!readys[i])
            {
                flag++;
            }
        }
    } while (flag);

    clock_gettime(CLOCK_MONOTONIC, &start);

    // allow all threads to do their works
    pthread_rwlock_unlock(&ready_lock);

    // sleep
    sleep(run_time);

    printf("--main-- before cancel\n");
    // time's up
    stop_now = 1;
    // for (int i = 0; i < trd_cnt; i++)
    // {
    //     pthread_cancel(threads[i]);
    // }

    // wait all threads to stop
    for (int i = 0; i < trd_cnt; i++)
    {
        printf("--main-- joining %d ready\n", i);
        pthread_join(threads[i], NULL);
        printf("--main-- joining %d ok\n", i);
    }
    printf("--main-- after join\n");
    clock_gettime(CLOCK_MONOTONIC, &end);

    long delta_s, delta_ns, total_ns, total_s;

    delta_s = end.tv_sec - start.tv_sec;
    delta_ns = LONG_MAX - start.tv_nsec + end.tv_nsec;
    total_ns = delta_s * LONG_MAX + delta_ns - LONG_MAX;
    total_s = total_ns / LONG_MAX;

    printf("time cost: nanosec %lld; sec %lld\n", total_ns, total_s);

    unsigned long total_bytes = 0;
    for (int i = 0; i < trd_cnt; i++)
    {
        total_bytes += bytes[i];
    }

    double speed;
    speed = (double)(total_bytes / 1000000L) / (double)total_s;
    printf("total: %ld MB, speed: %.4lfMB/s\n", total_bytes / 1000000L, speed); // TODO: precise time

    printf("mybench test: done\n");

    TIME_REC_PRINT;
    TIME_REC_SAVE_ALL;

    ss_fini_spdk();
    return 0;
}