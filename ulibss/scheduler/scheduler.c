#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/un.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

// #define DEBUG

#define CORE_NUM 4
#define MAX_FD 65536

int core_usage[CORE_NUM];

typedef struct open_entry
{
    int ino_num;    // unused since we use a linear list to index entries
    int core_idx;
    int ref_cnt;
} open_entry;
open_entry open_files[MAX_FD];

typedef struct sched_trans_data
{
    int op;     // 0: open; 1: close
    int ino;    // inode num
} sched_trans_data;


#define SOCK_NAME "/tmp/ss_scheduler"
#define MAX_PROC 65536

int sock_fd;
int sig_fd;
int epoll_fd;

int init()
{
    int len, res;
    struct sockaddr_un addr;
    struct epoll_event e;

    /* SOCKET INIT */
    
    // create a local tcp socket
    sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock_fd < 0) goto err;
    
    // bind local address
    bzero(&addr, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, SOCK_NAME);
    len = sizeof(addr);

    unlink(SOCK_NAME);
    res = bind(sock_fd, (struct sockaddr*)&addr, len);
    if (res < 0) goto err_sock;

    // set listen queue
    res = listen(sock_fd, MAX_PROC);
    if (res < 0) goto err_sock;

    /* EPOLL INIT */

    // create epoll
    epoll_fd = epoll_create(MAX_PROC+2);
    if (epoll_fd < 0) goto err_sock;

    // listen to sock_fd
    e.data.fd = sock_fd;
    e.events = EPOLLIN;
    res = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sock_fd, &e);
    if (res < 0) goto err_epoll;

    // listen to SIGINT
    sigset_t sigint;
    sigemptyset(&sigint);
    sigaddset(&sigint, SIGINT);
    sigprocmask(SIG_BLOCK, &sigint, NULL);
    sig_fd = signalfd(-1, &sigint, 0);
    if (sig_fd < 0) goto err_epoll;

    e.data.fd = sig_fd;
    e.events = EPOLLIN;
    res = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sig_fd, &e);
    if (res < 0) goto err_sig;

    /* SCHEDULER TABLE INIT */

    bzero(core_usage, sizeof(core_usage));
    bzero(open_files, sizeof(open_files));

    return 0;

err_sig:
    close(sig_fd);
err_epoll:
    close(epoll_fd);
err_sock:
    close(sock_fd);
err:
    return -1;
}

int fini()
{
    close(sig_fd);
    close(epoll_fd);
    close(sock_fd);
    unlink(SOCK_NAME);
    printf("Scheduler closed.\n");
    return 0;
}

void pr_tables()    // for debug
{
    printf("open files table:\n");
    for (int i = 0; i < MAX_FD; i++)
    {
        if (open_files[i].ref_cnt != 0)
        {
            printf("%d\t%d\t%d\n", 
                   open_files[i].ino_num,
                   open_files[i].core_idx,
                   open_files[i].ref_cnt
            );
        }
    }
    printf("\ncore usage table:\n");
    for (int i = 0; i < CORE_NUM; i++)
    {
        printf("%d\t%d\n", i, core_usage[i]);
    }
    printf("\n");
}

int open_file(int inode)
{
    if (open_files[inode].ref_cnt == 0)
    {
        open_files[inode].ino_num = inode;
        int core = 0;
        for (int i = 1; i < CORE_NUM; i++)
        {
            if (core_usage[i] < core_usage[core])
            {
                core = i;
            }
        }
        open_files[inode].core_idx = core;
    }
    open_files[inode].ref_cnt++;

    core_usage[open_files[inode].core_idx]++;

    return open_files[inode].core_idx;
}

int close_file(int inode)
{
    if (open_files[inode].ref_cnt <= 0)
    {
        return -1;  // redundant close
    }
    
    open_files[inode].ref_cnt--;
    core_usage[open_files[inode].core_idx]--;

    if (open_files[inode].ref_cnt == 0)
    {
        open_files[inode].ino_num = 0;  // clear that entry, useless here
        open_files[inode].core_idx = 0;
    }
    
    return 0;
}

int handle_events()
{
    int n, res, conn_fd, len;
    int should_close;
    sched_trans_data recv_data;
    int ret_data;
    struct sockaddr_un cli_addr;
    struct epoll_event events[1024], e;
    should_close = 0;
    
    while (1)
    {
        int n = epoll_wait(epoll_fd, events, 1024, -1);
        for (int i = 0; i < n; i++)
        {
            // capture SIGINT, exit gracefully
            if (events[i].data.fd == sig_fd)
            {
                printf("Ctrl+C captured, exit.\n");
                goto out;
            }

            // new connection
            else if (events[i].data.fd == sock_fd)
            {
                conn_fd = accept(sock_fd, (struct sockaddr*)&cli_addr, &len);
                if (conn_fd < 0)
                {
                    perror("Failed to establish connection.\n");
                    goto err;
                }
                e.data.fd = conn_fd;
                e.events=EPOLLIN;
                res = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, conn_fd, &e);
                if (res < 0)
                {
                    perror("Failed to add event for new connection.\n");
                    goto err;
                }
            }

            // recv from connection
            else
            {
                res = read(events[i].data.fd, &recv_data, sizeof(recv_data));
                if (res < 0)
                {
                    perror("Failed to read from fd.\n");
                    should_close = 1;
                }
                else if (res == 0)  // closed by peer
                {
                    should_close = 1;
                }
                
                if (!should_close)
                {
#ifdef DEBUG
                    printf("recv: %s %d\n", recv_data.op?"close":"open", recv_data.ino);
#endif

                    switch (recv_data.op)
                    {
                    case 0:
                        ret_data = open_file(recv_data.ino);
                        break;
                    case 1:
                        ret_data = close_file(recv_data.ino);
                        break;
                    default:
                        goto err;
                        break;
                    }
#ifdef DEBUG
                    pr_tables();
#endif
                    
                    res = write(events[i].data.fd, &ret_data, sizeof(ret_data));
                    if (res < 0)    // peer broke down
                    {
                        perror("Failed to write to fd.\n");
                        should_close = 1;
                    }
                }

                // close this connection
                if (should_close)
                {
                    res = epoll_ctl(epoll_fd, EPOLL_CTL_DEL, events[i].data.fd, NULL);
                    if (res < 0) goto err;
                    close(events[i].data.fd);
                    printf("Connection to a client closed.\n");
                    should_close = 0;
                }
                
            }
            
        }
    }
    
out:
    return 0;
err:
    return -1;
}

int main()
{
    printf("StorStack file scheduler running.\n");
    if (init() < 0)
    {
        perror("Init error.\n");
        exit(-1);
    }

    handle_events();

    fini();
    return 0;
}