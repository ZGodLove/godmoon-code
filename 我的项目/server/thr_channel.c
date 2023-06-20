#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <syslog.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <proto.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>

#include "server_conf.h"
#include "medialib.h"

static int tid_nextpos = 0;
// each线程号和record频道号对应,每一个线程负责一个频道
struct thr_channel_ent_st
{
    chnid_t chnid;
    pthread_t tid;
};
// thr_channel是一个结构体数组，每一个元素表示一个结构体，结构题内容：频道号，处理该频道线程号
struct thr_channel_ent_st thr_channel[CHANNR];

// 对应频道的线程的处理函数
static void *thr_channel_snder(void *ptr)
{
    struct msg_channel_st *sbufp;
    int len;
    struct mlib_listentry_st *ent = ptr; // chnid, desc
    sbufp = malloc(MSG_CHANNEL_MAX);
    if (sbufp == NULL)
    {
        syslog(LOG_ERR, "thr_channel_snder中malloc()函数失败:%s", strerror(errno));
        exit(1);
    }
    memset(sbufp, 0, MSG_CHANNEL_MAX);
    sbufp->chnid = ent->chnid; // 频道号处理
    // 频道内容读取
    while (1)
    {
        len = mlib_readchn(ent->chnid, sbufp->data, MAX_DATA);
        // 自定义一个函数
        // 读
        // len = mlib_readchn(ent->chnid, sbufp->data, 128 * 1024 / 8);
        // if(ent->chnid == 3)
        syslog(LOG_DEBUG, "mlib_readchnl()读到的字节数:%d", len);
        // len+sizeof(chnid_t)表示需要发送的数据的长度
        if (sendto(serversd, sbufp, len + sizeof(chnid_t), 0, (void *)&sndaddr, sizeof(sndaddr)) < 0)
        {
            syslog(LOG_ERR, "thr_channel(%d)中sendto()函数失败:%s", ent->chnid, strerror(errno));
            break;
        }
        memset(sbufp->data, 0, MSG_CHANNEL_MAX - sizeof(sbufp->chnid));
        sched_yield(); // 主动出让调度器,当循环阻塞时
    }
    pthread_exit(NULL);
}

// 为每一个频道创建一个线程
// 这个函数在外围应该被循环调用
int thr_channel_create(struct mlib_listentry_st *ptr)
{
    int err;
    err = pthread_create(&thr_channel[tid_nextpos].tid, NULL, thr_channel_snder, ptr);
    if (err)
    {
        syslog(LOG_WARNING, "thr_channel_create中pthread_create()函数失败:%s", strerror(err));
        return -err;
    }
    thr_channel[tid_nextpos].chnid = ptr->chnid; // 填写频道信息
    tid_nextpos++;
    return 0;
}

// 销毁对应频道线程
int thr_channel_destroy(struct mlib_listentry_st *ptr)
{
    for (int i = 0; i < CHANNR; i++)
    {
        if (thr_channel[i].chnid == ptr->chnid)
        {
            if (pthread_cancel(thr_channel[i].tid) < 0)
            {
                syslog(LOG_ERR, "pthread_cancel()函数失败,线程频道号:%d", ptr->chnid);
                return -ESRCH;
            }
            pthread_join(thr_channel[i].tid, NULL);
            thr_channel[i].chnid = -1;
            break;
        }
    }
    return 0;
}
// 销毁全部频道线程
int thr_channel_destroyall(void)
{
    for (int i = 0; i < CHANNR; i++)
    {
        if (thr_channel[i].chnid > 0)
        {
            if (pthread_cancel(thr_channel[i].tid) < 0)
            {
                syslog(LOG_ERR, "pthread_cancel():函数失败,线程频道号:%ld", thr_channel[i].tid);
                return -ESRCH;
            }
            pthread_join(thr_channel[i].tid, NULL);
            thr_channel[i].chnid = -1;
        }
    }
    return 0;
}
