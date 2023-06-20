#include <stdio.h>
#include <stdlib.h>
#include <glob.h>
#include <syslog.h>
#include <string.h>
#include <errno.h>
#include <error.h>
#include <fcntl.h>
#include <proto.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "server_conf.h"
#include "medialib.h"
// #include "../include/proto.h"
#include "mytbf.h"

#define DEBUG

#define PATHSIZE 1024
#define LINEBUFSIZE 1024
#define MP3_BITRATE (128 * 1024) // correct bps:128*1024
// 媒体库最完整的结构体
// 解析目录结构，有多少种mp3,有多少txt文件
// 获取当前节目单的内容

struct channel_context_st
{
    chnid_t chnid;  // 频道号
    char *desc;     // 描述
    glob_t mp3glob; // 解析出来的所有的mp3,可以存目录,也可以存具体的文件(带全路径)
    int pos;        // 播放的这首歌，要有下标表示
    int fd;         // 打开这个文件所用到的文件描述符
    off_t offset;   // 数据是一段一段发出去的，要记录offset
    mytbf_t *tbf;   // 流量控制，一定要进行流量控制，不然就是cat这个文件，一股脑的全发出去
};

// 声明结构体数组
static struct channel_context_st channel[MAXCHIND + 1];

// 解析出来的结果就会填充到channel数组中,这就是所有的频道信息
// 流媒体是以频道为单位的,客户端进入广播,具体是在播放该频道下的哪首歌是不确定的
// 所以,只要能顺序循环播放当前频道的歌曲不停止就可以了,或者只播放一变.
static struct channel_context_st *path2entry(const char *path)
{
    // 这个函数被循环调用,一次只解析一个频道
    // 解析path/desc.txt path/*.mp3
    syslog(LOG_INFO, "current path :%s\n", path);
    char pathstr[PATHSIZE] = {'\0'};
    char linebuf[LINEBUFSIZE];
    FILE *fp;
    struct channel_context_st *me;
    // static修饰,多次调用,只分配一次内存,值可以累计
    static chnid_t curr_id = MINCHNID;

    // 先解析desc文件--频道描述文件
    strcat(pathstr, path);
    strcat(pathstr, "/desc.txt");
    fp = fopen(pathstr, "r");
    syslog(LOG_INFO, "channel dir:%s\n", pathstr);
    if (fp == NULL)
    {
        syslog(LOG_INFO, "%s:不是频道目录,没有desc文件.", path);
        return NULL;
    }
    if (fgets(linebuf, LINEBUFSIZE, fp) == NULL) // 已经打开了
    {
        syslog(LOG_INFO, "%s:不是频道目录,没有desc文件或desc文件为空.", path);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    // linebuf中就是desc.txt中的内容,把它拷贝到me->desc中

    me = malloc(sizeof(*me));
    if (me == NULL)
    {
        syslog(LOG_ERR, "malloc():%s", strerror(errno));
        return NULL;
    }

    // 初始化令牌桶
    me->tbf = mytbf_init(MP3_BITRATE / 8, MP3_BITRATE / 8 * 5);
    if (me->tbf == NULL)
    {
        syslog(LOG_ERR, "mytbf_init()令牌桶初始化失败:%s.", strerror(errno));
        free(me);
        return NULL;
    }
    me->desc = strdup(linebuf);

    strncpy(pathstr, path, PATHSIZE);
    strncat(pathstr, "/*.mp3", PATHSIZE - 1);
    pathstr[PATHSIZE - 1] = 0;
    if (glob(pathstr, 0, NULL, &me->mp3glob) != 0)
    {
        curr_id++;
        syslog(LOG_ERR, "%s:这个目录中没有mp3文件或者glob函数出错.", path);
        free(me);
        return NULL;
    }
    me->pos = 0;    // 当前播放的是第一个
    me->offset = 0; // 刚开始播放的歌曲的偏移量是0
    me->fd = open(me->mp3glob.gl_pathv[me->pos], O_RDONLY);
    if (me->fd < 0)
    {
        syslog(LOG_WARNING, "%s:无法打开mp3文件.", me->mp3glob.gl_pathv[me->pos]);
        free(me);
        return NULL;
    }
    me->chnid = curr_id;
    curr_id++;
    return me;
}
int mlib_getchnlist(struct mlib_listentry_st **result, int *resnum)
{
    int num = 0;
    int i = 0;
    char path[PATHSIZE];
    glob_t globres;
    struct mlib_listentry_st *ptr;  // 给用户的目录,数组指针
    struct channel_context_st *res; // 每一个频道的具体信息(频道是指ch1等目录)

    // 初始化channel数组的内容
    for (int i = 0; i < MAXCHIND; i++)
    {
        channel[i].chnid = -1; // -1表示未启用
    }

    // 检查媒体库的位置
    // 1.往哪里输出 2.大小 3.格式(解析成一个新的串) 4.来源
    snprintf(path, PATHSIZE, "%s/*", server_conf.media_dir);
#ifdef DEBUG
    printf("current path:%s\n", path);
#endif
    // 1.路径 2.特殊要求 3.出错路径不关心 4.解析结果
    // globres里面解析出来的就是路径加个数
    if (glob(path, 0, NULL, &globres))
    {
        return -1;
    }
#ifdef DEBUG
    printf("globres.gl_pathv[0]:%s\n", globres.gl_pathv[0]);
    printf("globres.gl_pathv[1]:%s\n", globres.gl_pathv[1]);
    printf("globres.gl_pathv[2]:%s\n", globres.gl_pathv[2]);
#endif
    // 对每一个目录再解析,拿到每一个mp3和txt文件
    ptr = malloc(sizeof(struct mlib_listentry_st) * globres.gl_pathc);
    if (ptr == NULL)
    {
        syslog(LOG_ERR, "mlibptr malloc函数失败.");
        exit(1);
    }
    for (i = 0; i < globres.gl_pathc; i++)
    {
        // 现在拿到的每一个名字就是 "/var/media/ch1" 这样的一个目录
        // 要从这个目录下面拿到mp3和txt文件
        // 将这个目录下的mp3文件和desc文件信息存放到channelptr中
        res = path2entry(globres.gl_pathv[i]); // 自定义函数把路径变成每一条记录
        if (res != NULL)
        {
            syslog(LOG_ERR, "path2entry() return : %d %s.", res->chnid, res->desc);
            // 把channelptr添加到channel数组中,频道号就是下标
            memcpy(channel + res->chnid, res, sizeof(*res));
            ptr[num].chnid = res->chnid;
            ptr[num].desc = strdup(res->desc);
            num++;
        }
    }
    // 回填result和resnum,把mlibptr的内存复制到result中
    *result = realloc(ptr, sizeof(struct mlib_listentry_st) * num);
    if (*result == NULL)
    {
        syslog(LOG_ERR, "realloc函数失败.");
    }
    *resnum = num;
    return 0;
}

int mlib_freechnlist(struct mlib_listentry_st *ptr)
{
    free(ptr);
    return 0;
}
// 当前是失败了或者已经读取完毕才会调用open_next
static int open_next(chnid_t chnid)
{
    // 加入循环是为了防止每一首歌都打开失败，尽量都试着打开一次
    for (int i = 0; i < channel[chnid].mp3glob.gl_pathc; i++)
    {
        channel[chnid].pos++;
        // can open any file in channel[chnid].mp3glob.gl_pathv
        // 所有的歌曲都没有打开
        if (channel[chnid].pos == channel[chnid].mp3glob.gl_pathc)
        {
            channel[chnid].pos = 0; // 再来一次
            return -1;
            break; // 最后一首歌已经打开完毕，结束
        }
        close(channel[chnid].fd);
        channel[chnid].fd = open(channel[chnid].mp3glob.gl_pathv[channel[chnid].pos], O_RDONLY); // 对应频道的歌名
        // 如果打开还是失败
        if (channel[chnid].fd < 0)
        {
            syslog(LOG_WARNING, "open(%s):%s", channel[chnid].mp3glob.gl_pathv[channel[chnid].pos], strerror(errno));
        }
        else // success
        {
            channel[chnid].offset = 0;
            return 0;
        }
    }
    syslog(LOG_ERR, "None of mp3 in channel %d id available.", chnid);
    return 0;
}
// 从每个频道中读取内容，将当前播放的歌曲待发送的数据写到buf，实际大小为size
// 这个环节进行了流量控制
ssize_t mlib_readchn(chnid_t chnid, void *buf, size_t size)
{
    int tbfsize;
    int len;
    // get token number
    tbfsize = mytbf_fetchtoken(channel[chnid].tbf, size);
    syslog(LOG_INFO, "current tbf():%d", mytbf_checktoken(channel[chnid].tbf));

    while (1)
    {
        // 从offset偏移量开始读,读tbfsize个,因为只有这么多令牌同
        len = pread(channel[chnid].fd, buf, tbfsize, channel[chnid].offset);
        /*current song open failed*/
        if (len < 0)
        {
            // 当前这首歌可能有问题，错误不至于退出，读取下一首歌
            syslog(LOG_WARNING, "media file %s pread():%s", channel[chnid].mp3glob.gl_pathv[channel[chnid].pos], strerror(errno));
            open_next(chnid);
        }
        else if (len == 0)
        {
            syslog(LOG_DEBUG, "media %s file is over", channel[chnid].mp3glob.gl_pathv[channel[chnid].pos]);
#ifdef DEBUG
            printf("current chnid :%d\n", chnid);
#endif
            open_next(chnid);
            break;
        }
        else /*len > 0*/ // 真正读取到了数据
        {
            channel[chnid].offset += len;
            syslog(LOG_DEBUG, "epoch : %f", (channel[chnid].offset) / (16 * 1000 * 1.024));
            break;
        }
    }
    // 消耗了len个令牌,把剩余的归还
    if (tbfsize - len > 0)
        mytbf_returntoken(channel[chnid].tbf, tbfsize - len);
    return len; // 返回读取到的长度
}
