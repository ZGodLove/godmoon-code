/**
 * 节目单线程只创建一个,所以只运行一次
 */
#ifndef THR_LIST_H_
#define THR_LIST_H

#include "medialib.h"

// 这个是线程创建函数,服务器的接口
int thr_list_create(struct mlib_listentry_st *, int);

// 销毁函数
int thr_list_destroy();

#endif