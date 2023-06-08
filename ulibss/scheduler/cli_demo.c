#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#define SOCK_NAME "/tmp/ss_scheduler"
#define MAX_PROC 65536

int sock_fd;

typedef struct sched_trans_data
{
    int op;     // 0: open; 1: close
    int ino;    // inode num
} sched_trans_data;

int init()
{
    int len, res;
    struct sockaddr_un addr;

    /* SOCKET INIT */
    
    // create a local tcp socket
    sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock_fd < 0) goto err;
    
    // bind local address
    bzero(&addr, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, SOCK_NAME);
    len = sizeof(addr);

    res = connect(sock_fd, (struct sockaddr*)&addr, len);
    if (res < 0) goto err_sock;

    return 0;
err_sock:
    close(sock_fd);
err:
    return -1;
}

int fini()
{
    close(sock_fd);
    printf("Client closed.\n");
    return 0;
}

void pr_help()
{
    printf(
        "h\thelp\n"
        "q\tquit\n"
        "o <ino>\tquery the core index for an open file\n"
        "c <ino>\tclose an open file\n"
    );
}

int handle_cmd()
{
    char c;
    int cmd, arg, ret, res;
    /**
     * 0    drop all empty chars and read first char cmd
     * 1    read int argument for previous char cmd
    */
    int f = 0;
    while (1)
    {
        if (f == 0)
        {
            scanf("%c", &c);
            switch (c)
            {
            case 'h':   //help
                pr_help();
                break;
            case 'q':   //quit
                return 0;
                break;
            case 'o':   //open
                cmd = 0; 
                f=1;
                break;
            case 'c':   //close
                cmd = 1;
                f=1;
                break;
            default:
                break;
            }
        }
        else if (f == 1)    //send and recv
        {
            scanf("%d", &arg);

                    // build the transferred data
                    sched_trans_data data = {
                        .op = cmd, // open=0, close=1
                        .ino = arg
                    };
                    // send operation to scheduler
                    res = write(sock_fd, &data, sizeof(data)); // 
                    if (res < 0)
                    {
                        printf("Connection closed.\n");
                        return -1;
                    }
                    // get response from scheduler
                    res = read(sock_fd, &ret, sizeof(ret));
                    if (res <= 0)
                    {
                        printf("Connection closed.\n");
                        return -1;
                    }
                    // cmd=0: response for open, the core index is returned
                    if (cmd == 0)
                    {
                        printf("core of file %d: %d\n", arg, ret);
                    }
                    // cmd=1: response for close, get 0 if success
                    else
                    {
                        if (!ret)
                        {
                            printf("file %d closed\n", arg);
                        }
                        else
                        {
                            printf("redundant close for file %d\n", arg);
                        }
                    }
            
            f=0;
        }
        
    }
}

int main()
{
    printf("Demo client running.\n");
    if (init() < 0)
    {
        perror("Init error.\n");
        exit(-1);
    }
    pr_help();
    printf("Connection to scheduler established.\n");

    handle_cmd();

    fini();
    return 0;
}