/**
 * 由server将媒体信息上传到广播地址
 * 客户端连接广播地址,从广播地址接收
 * 默认一个局域网都在一个广播域
 */
#include <stdio.h>
#include <stdlib.h>
#include <proto.h>
#include <unistd.h>
#include <syslog.h>
#include <fcntl.h>
#include <error.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>

#include "server_conf.h"
#include "medialib.h"
#include "thr_channel.h"
#include "thr_list.h"
// #define DEBUG

/**
 * -M 指定多播组
 * -P 指定接收端口
 * -F 前台运行
 * -D 指定媒体库位置
 * -I 指定网卡设备 eth33
 * -H 显示帮助
 */

int serversd;
struct sockaddr_in sndaddr;
struct server_conf_st server_conf = {
    .rcvport = DEFAULT_RCVPORT,
    .media_dir = DEFAULT_MEDIADIR,
    .runmode = RUN_FOREGROUND,
    .ifname = DEFAULT_IF,
    .mgroup = DEFAULT_MGROUP};

// 这个数组指针,只在server.c中使用,所以修饰成static
// 它表示一个媒体库列表,表示媒体库ID和desc描述
static struct mlib_listentry_st *list;

static void print_help()
{
    printf("-P 指定接收端口\n");
    printf("-M 指定多播组\n");
    printf("-F 前台运行 \n");
    printf("-D 指定媒体库位置\n");
    printf("-I 指定网卡设备 eth33\n");
    printf("-H 显示帮助\n");
}

static void daemon_exit(int s) // 信号处理函数
{
    thr_list_destroy();
    thr_channel_destroyall();
    mlib_freechnlist(list);
    syslog(LOG_WARNING, "signal-%d caught, exit now.", s);
    closelog();
    exit(0);
}

// 一般都是杀死父进程，子进程变成孤儿进程，然后将子进程脱离控制终端
static int daemonize()
{
    pid_t pid;
    int fd;
    pid = fork();
    if (pid < 0)
    {
        // perror("fork()");
        // 1.写级别,一般以ERR和WORING为分界线，ERR以上进程结束，WORING以下继续运行
        // 2.要挟的内容
        syslog(LOG_ERR, "进程fork失败:%s", strerror(errno));
        return -1;
    }
    if (pid > 0) // parent
        exit(0);

    fd = open("/dev/null", O_RDWR);
    if (fd < 0)
    {
        syslog(LOG_ERR, "子进程打开/dev/null空文件设备失败:%s", strerror(errno));
        return -2;
    }
    else
    {
        /*close stdin, stdout, stderr*/
        dup2(fd, 0);
        dup2(fd, 1);
        dup2(fd, 2);
        if (fd > 2)
            close(fd);
    }
    // setsid();   // 脱离父进程的group,自己创建会话
    chdir("/"); // 把当前进程的工作路径改为一个绝对有的路径
    umask(0);   // 通常把umask值关掉
    return 0;
}

static int socket_init() // 初始化socket
{
    // 设置socket属性
    struct ip_mreqn mreq;
    // 多播组，需要点分式转大整数
    inet_pton(AF_INET, server_conf.mgroup, &mreq.imr_multiaddr);
    // 本机地址
    inet_pton(AF_INET, "0.0.0.0", &mreq.imr_address);
    // 网卡号
    mreq.imr_ifindex = if_nametoindex(server_conf.ifname); // net card

    // 创建socket
    serversd = socket(AF_INET, SOCK_DGRAM, 0);
    if (serversd < 0)
    {
        syslog(LOG_ERR, "服务器套接字创建失败:%s.", strerror(errno));
        exit(1);
    }

    // 1.套接字
    // 2.该层协议
    // 3.创建多播组
    // 4.需要的结构体
    if (setsockopt(serversd, IPPROTO_IP, IP_MULTICAST_IF, &mreq, sizeof(mreq)) < 0)
    {
        syslog(LOG_ERR, "套接字属性设置失败:%s.", strerror(errno));
        exit(1);
    }

    // 接收端地址信息的初始化也就是广播地址
    // 客户端是不需要bind的,服务器向广播域发送数据报
    // 服务器就相当于客户端,所以不需要bind
    sndaddr.sin_family = AF_INET;
    sndaddr.sin_port = htons(atoi(server_conf.rcvport));
    inet_pton(AF_INET, server_conf.mgroup, &sndaddr.sin_addr);
    // inet_pton(AF_INET, "0.0.0.0", &sndaddr.sin_addr.s_addr);

    return 0;
}

int main(int argc, char *const argv[])
{
    /*variable*/
    int c;

    /*设置信号*/
    struct sigaction sa;
    /*signal content*/
    /*daemon process receive these signal, go to daemon_exit function*/
    sa.sa_handler = daemon_exit;
    sigemptyset(&sa.sa_mask);
    sigaddset(&sa.sa_mask, SIGINT);
    sigaddset(&sa.sa_mask, SIGQUIT);
    sigaddset(&sa.sa_mask, SIGTERM);

    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);
    /*写系统日志*/
    // 1.指定人物
    // 2.还需要稍带的信息
    // 3.当前的消息来源
    openlog("netradio", LOG_PID | LOG_PERROR, LOG_DAEMON);
#ifdef DEBUG
    fprintf(stdout, "here1!\n");
#endif
    /*命令行分析*/
    while (1)
    {

        c = getopt(argc, argv, "M:P:FD:I:H"); //:-->has parameter
#ifdef DEBUG
        fprintf(stdout, "here2!\n");
#endif
        printf("get command c:%c\n", c);
        if (c < 0)
            break;
        switch (c)
        {
        case 'M':
            server_conf.mgroup = optarg;
            break;
        case 'P':
            server_conf.rcvport = optarg;
            break;
        case 'F':
#ifdef DEBUG
            fprintf(stdout, "here3!\n");
#endif
            // server_conf.runmode = RUN_FOREGROUND;
            break;
        case 'D':
            server_conf.media_dir = optarg;
            break;
        case 'I':
            server_conf.ifname = optarg;
            break;
        case 'H':
            print_help();
            exit(0);
            break;
        default:
        {
            printf("参数错误\n");
            abort();
        }
        break;
        }
        break;
    }
#ifdef DEBUG
    printf("server_conf.runmode:%d", server_conf.runmode);
#endif
    /*守护进程实现*/

    // 守护进程必须由信号杀死，所以要选择我们想要的信号和处理函数
    if (server_conf.runmode == RUN_DAEMON)
    {
        if (daemonize() != 0) // 自定义守护进程函数
        {
            perror("守护进程实现失败!\n");
        }
        else if (server_conf.runmode == RUN_FOREGROUND)
        {
            syslog(LOG_DEBUG, "守护进程实现成功.");
        }
        else
        {
            // fprintf(stderr, "EINVAL\n");
            syslog(LOG_ERR, "运行模式参数非法.");
            exit(1);
        }
    }

    /*socket初始化*/
    // 调用自定义函数
    socket_init();
    syslog(LOG_DEBUG, "socket初始化成功.");

    // 先处理每一个频道的信息,再发送节目单和各个频道信息
    /*获取频道信息*/
    int list_size;
    int err;
    // list 频道的描述信息
    // list_size有几个频道
    err = mlib_getchnlist(&list, &list_size);
    if (err)
    {
        syslog(LOG_ERR, "mlib_getchnlist()函数返回失败:%s", strerror(errno));
        exit(1);
    }
    /*创建节目单线程*/
    if (thr_list_create(list, list_size))
    {
        syslog(LOG_ERR, "thr_list_create()函数返回失败.");
        exit(1);
    }

    // 创建频道线程
    // 一个频道对应一个线程
    int i = 0;
    for (i = 0; i < list_size; i++)
    {
        err = thr_channel_create(list + i);
        if (err)
        {
            fprintf(stderr, "thr_channel_create()函数返回失败:%s\n", strerror(err));
            exit(1);
        }
    }
    syslog(LOG_DEBUG, "%d个频道线程被创建.主进程阻塞等待.", i);
    // 服务器进程不能结束
    while (1)
        pause();
}
