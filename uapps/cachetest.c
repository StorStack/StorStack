#include <string.h>
#include <unistd.h>
// #include <signal.h>

#include "ulibss.h"
// #include "klibss.h"
#include "ulibss_aio.h"

#define BFSZ 128
#define PATH "/var/ss/cache_test.txt"
#define PATH2 "/var/ss/cache_test2.txt"

#ifdef RAW_BUF_SUPPORT
#define RAW_BUF_SUPPORT_TEST
#endif

#ifdef SS_DEBUG
void print_cache();
#endif


int main() {
    const char path[] = PATH;
    const char path1[] = PATH;
    const char path2[] = PATH2;
    int fd, fd1, fd2;
    int ret;

    printf("cache test init\n");
    ss_init_spdk();

    fd = ss_open(path, O_RDWR | O_CREAT, 0777);
    printf("OPEN ret: %d\n\n", fd);

    void *buf = ss_buf_init(1048576);
    int write_ret = ss_write(fd, buf, sizeof(char)*(1048576));

    ret = ss_pread(fd, buf, 4096, 5*4096);
    TIME_REC_NEWLN;

    ret = ss_pread(fd, buf, 16384, 100*4096);
    TIME_REC_NEWLN;

    ret = ss_pwrite(fd, buf, 1024, 92*4096);

    ret = ss_pread(fd, buf, 1024, 18*4096);
    TIME_REC_NEWLN;

    fd1 = ss_open(path1, O_RDWR | O_CREAT, 0777);

    ret = ss_pwrite(fd1, buf, 1024, 24*4096);

    int close_ret = ss_close(fd);

    fd2 = ss_open(path2, O_RDWR | O_CREAT, 0777);

    ret = ss_pwrite(fd2, buf, 1024, 100*4096);

    ret = ss_pread(fd2, buf, 1024, 2*4096);
    TIME_REC_NEWLN;

    ret = ss_pwrite(fd2, buf, 1024, 0*4096);

    #ifdef SS_DEBUG
    print_cache();
    #endif

    close_ret = ss_close(fd1);
    close_ret = ss_close(fd2);
    printf("CLOSE ret: %d\n\n", close_ret);

    ss_fini_spdk();
    printf("cache test: done\n");
    TIME_REC_PRINT;
    return 0;
}