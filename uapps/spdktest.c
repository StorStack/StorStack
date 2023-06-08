#include <string.h>
#include <unistd.h>

#include "ulibss.h"
#include "klibss.h"
#include "../ulibss/spdk/lib/nvme/nvme_internal.h"

#ifdef RAW_BUF_SUPPORT
#define RAW_BUF_SUPPORT_TEST
#endif

// #include "spdk/nvme_spec.h"

int main(int argc, int **argv)
{
    ss_init_spdk();
    printf("SPDK init OK!:\n");

    ss_init_qpair();
    printf("qpair init OK!:\n");

    printf("OPEN test:\n");
    // const char path[] = "/var/ss/test.txt";
    const char path[] = "/tmp/aio_test.txt";
    int fd = ss_open(path, O_RDWR | O_CREAT, 0777);
    printf("OPEN ret: %d\n\n", fd);

    printf("CLOSE test:\n");
    int close_ret = ss_close(fd);
    printf("CLOSE ret: %d\n\n", close_ret);

    printf("READ test:\n");
    fd = ss_open(path, O_RDWR | O_CREAT, 0777);

    char *buf = (char *)malloc(sizeof(char) * 32);
    ss_read(fd, buf, sizeof(char) * 32);
    printf("Content: %s", buf);
    free(buf);
    ss_close(fd);

    // printf("MKDIR start!\n");
    // int mkdir_ret = ss_mkdir("/var/ss/123", 0777);
    // printf("MKDIR ret: %d\n", mkdir_ret);

    printf("STAT start!\n");
    struct stat stats = {};
    int stat_ret = ss_stat(path, &stats);
    printf("STAT stats: %d, %d, %d, %d\n", stats.st_blksize, stats.st_size, stats.st_uid, stats.st_ino);

    printf("WRITE start!\n");
    fd = ss_open(path, O_RDWR, 0777);
    char* test = ss_buf_init(sizeof(char)* 1048576);
    int write_ret = ss_write(fd, test, sizeof(char)*(1048576));
    printf("WRITE ret: %d\n", write_ret);

    ss_fini_qpair();
    printf("qpair finish OK!\n");

    ss_fini_spdk();
    printf("SPDK finish OK!\n");

    return 0;
}