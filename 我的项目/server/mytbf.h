#ifndef MYTBF_H_
#define MYTBF_H_

// 规定令牌筒的数量
#define MYTBF_MAX 1024 // 最多同时有1024个令牌桶,也就是可以最多给1024个用户使用
typedef void mytbf_t;
typedef void mytbf_t;

mytbf_t *mytbf_init(int cps, int burst);
int mytbf_fetchtoken(mytbf_t *, int);
int mytbf_checktoken(mytbf_t *); // 得到令牌桶中令牌的数量
int mytbf_returntoken(mytbf_t *, int);
int mytbf_destroy(mytbf_t *);

#endif