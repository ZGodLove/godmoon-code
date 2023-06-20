#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <error.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/types.h>

#include "mytbf.h"

struct mytbf_st
{
    int cps;   // 速度
    int burst; // 上限
    int token; // 令牌个数
    int pos;   // 记录位置
    pthread_mutex_t mut;
    pthread_cond_t cond;
};
static struct mytbf_st *job[MYTBF_MAX];
static pthread_mutex_t mut_job = PTHREAD_MUTEX_INITIALIZER;

static pthread_once_t once_init = PTHREAD_ONCE_INIT;
static pthread_t tid;

static void *thr_alrm(void *p)
{
    struct timeval tv;
    while (1)
    {
        pthread_mutex_lock(&mut_job);
        // 循环遍历job数组,给每个任务按照速度发放令牌
        for (int i = 0; i < MYTBF_MAX; i++)
        {
            if (job[i] != NULL)
            {
                pthread_mutex_lock(&job[i]->mut);
                job[i]->token += job[i]->cps;
                if (job[i]->token > job[i]->burst)
                    job[i]->token = job[i]->burst;
                pthread_cond_broadcast(&job[i]->cond);
                pthread_mutex_unlock(&job[i]->mut);
            }
        }
        pthread_mutex_unlock(&mut_job);
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        select(0, NULL, NULL, NULL, &tv);
    }
    pthread_exit(NULL);
}
static void module_unload() // 程序卸载 -- 挂在钩子函数上面
{
    int i;
    pthread_cancel(tid);     // 因为join应该是一个死循环，所以调cancel调用他
    pthread_join(tid, NULL); // 收回线程,对状态不关心

    // 释放空间
    for (i = 0; i < MYTBF_MAX; i++)
    {
        free(job[i]);
    }
    return;
}
static void module_load() // 程序加载
{
    int err;
    err = pthread_create(&tid, NULL, thr_alrm, NULL);
    if (err)
    {
        fprintf(stderr, "pthread_create():%s", strerror(errno));
        exit(1);
    }
    // 挂钩子函数
    atexit(module_unload);
}
static int get_free_pos_unlocked()
{
    int i;
    for (i = 0; i < MYTBF_MAX; i++)
    {
        if (job[i] == NULL)
            return i;
    }
    return -1;
}

// 1.速率  2.上限
mytbf_t *mytbf_init(int cps, int burst)
{
    // 规定加载模块只运行一次
    // 令牌桶的模块只能运行一个,用户可以从上面取桶子,拿令牌
    // 也就是说只能有一个线程负责给各个令牌桶发放令牌
    struct mytbf_st *me;
    pthread_once(&once_init, module_load);
    int pos;
    me = malloc(sizeof(*me));
    if (me == NULL)
        return NULL;
    me->cps = cps;
    me->burst = burst;
    me->token = 0;
    pthread_mutex_init(&me->mut, NULL);
    pthread_cond_init(&me->cond, NULL);

    // 下面的代码要访问数组，所以要锁上
    pthread_mutex_lock(&mut_job);
    pos = get_free_pos_unlocked(); // 自定义找位置的函数
    if (pos < 0)
    {
        pthread_mutex_unlock(&mut_job);
        fprintf(stderr, "没有空闲的令牌桶可以使用.");
        free(me);
        exit(1);
    }
    me->pos = pos;
    job[me->pos] = me;
    pthread_mutex_unlock(&mut_job);
    return me;
}

static int min(int a, int b)
{
    return a < b ? a : b;
}

// 取令牌
int mytbf_fetchtoken(mytbf_t *ptr, int size)
{
    int n;
    struct mytbf_st *me = ptr;
    pthread_mutex_lock(&me->mut);
    // 如果取令牌的时候,令牌桶的令牌数量不够,那么就等待
    while (me->token <= 0)
        pthread_cond_wait(&me->cond, &me->mut); // 解锁等待,非阻塞
    n = min(me->token, size);
    me->token -= n;
    pthread_cond_broadcast(&me->cond);
    pthread_mutex_unlock(&me->mut);
    return n;
}

// 归还令牌桶
int mytbf_returntoken(mytbf_t *ptr, int size)
{
    struct mytbf_st *me = ptr;
    pthread_mutex_lock(&me->mut);
    me->token += size;
    if (me->token > me->burst)
        me->token = me->burst;
    // 发送信号
    // pthread_cond_broadcast(&me->cond);
    pthread_mutex_unlock(&me->mut);
    return 0;
}

// 销毁
int mytbf_destroy(mytbf_t *ptr)
{
    struct mytbf_st *me = ptr;
    pthread_mutex_lock(&mut_job);
    job[me->pos] = NULL;
    pthread_mutex_unlock(&mut_job);

    pthread_mutex_destroy(&me->mut);
    pthread_cond_destroy(&me->cond);
    free(ptr);
    return 0;
}

int mytbf_checktoken(mytbf_t *ptr)
{
    int token_left = 0;
    struct mytbf_st *me = ptr;
    pthread_mutex_lock(&me->mut);
    token_left = me->token;
    pthread_mutex_unlock(&me->mut);
    return token_left;
}