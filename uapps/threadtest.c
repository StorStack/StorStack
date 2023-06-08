#include <unistd.h>
#include <pthread.h>

#include "ulibss.h"
#include "ulibss_aio.h"

#define BFSZ 32
#define PATH  "/var/ss/thread_test.txt"
#define PATH1 "/var/ss/thread_test1.txt"
#define PATH2 "/var/ss/thread_test2.txt"

const char path[] = PATH;
const char path1[] = PATH1;
const char path2[] = PATH2;
int fd;

pthread_t t1, t2;
pthread_mutex_t outlock;

void *thread_entry(void *arg) {
    int id = (int)arg;
    int myfd;

    pthread_mutex_lock(&outlock);
    printf("t%d: started\n", id);
    pthread_mutex_unlock(&outlock);

    char *buf_w = (char*)malloc(BFSZ);
    memset(buf_w, id+48, BFSZ);
    char *buf_r = (char*)malloc(BFSZ);

    ss_pwrite(fd, buf_w, BFSZ, id*BFSZ);
    ss_pread(fd, buf_r, BFSZ, BFSZ/2);

    char *mypath = (id == 1)?path1:path2;
    char *mybuf_w = (char*)malloc(BFSZ);
    memset(mybuf_w, id+48, BFSZ);
    char *mybuf_r = (char*)malloc(BFSZ);

    myfd = ss_open(mypath, O_RDWR | O_CREAT, 0777);
    ss_write(myfd, mybuf_w, BFSZ);
    ss_lseek64(myfd, 0, SEEK_SET);
    ss_read(myfd, mybuf_r, BFSZ);
    ss_close(myfd);

    buf_r[BFSZ-1] = 0;
    mybuf_r[BFSZ-1] = 0;

    pthread_mutex_lock(&outlock);
    printf("t%d: public read %s\n", id, buf_r);
    printf("t%d: private read %s\n", id, mybuf_r);
    printf("t%d: ended\n", id);
    pthread_mutex_unlock(&outlock);

    free(buf_w);
    free(buf_r);
    free(mybuf_w);
    free(mybuf_r);
    pthread_exit(NULL);
}

int main()
{
    int ret;

    pthread_mutex_init(&outlock, NULL);

    printf("thread test init\n");
    ss_init_spdk();

    printf("thread test start\n");
    fd = ss_open(path, O_RDWR | O_CREAT, 0777);
    printf("thread test fd: %d\n", fd);

    pthread_create(&t1, NULL, thread_entry, 1);
    pthread_create(&t1, NULL, thread_entry, 2);

    char *buf = (char*)malloc(BFSZ*3);
    memset(buf, '0', BFSZ);

    ret = ss_pwrite(fd, buf, BFSZ, 0);

    pthread_join(t1, NULL);
    printf("thread test join: t1\n");
    pthread_join(t2, NULL);
    printf("thread test join: t2\n");

    ret = ss_pread(fd, buf, BFSZ*3, 0);

    printf("thread test: read all, %d: %s\n", ret, buf);

    ss_close(fd);

    ss_fini_spdk();

    free(buf);

    // pthread_exit(NULL);
}