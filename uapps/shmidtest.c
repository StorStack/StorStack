#include <string.h>
#include <unistd.h>

#include "ulibss.h"
#include "klibss.h"

int main()
{
    ss_init_spdk();
    int fd = ss_open("test/shmid.test",0,O_RDWR);
    if(fd<0)
    {
        perror("open failed: ");
        exit(-1);
    }
    size_t len = sizeof(char)<<3;
    char* buffer = (char*)malloc(len);
    int ret = ss_read(fd,buffer, len);
    if (ret)
    {
        perror("read error:");
        exit(-1);
    }

    // TODO: process 2022.12.01

}