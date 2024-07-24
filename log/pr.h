#ifndef __PR_H__
#define __PR_H__

#include <stdio.h>
#include <sstream>
#include <thread>
#include <string>

using namespace std;

#define PR_LEVEL_ERROR 0
#define PR_LEVEL_WARN 1
#define PR_LEVEL_INFO 2
#define PR_LEVEL_DEBUG 3

extern int pr_level;
///日志输出函数，level为日志等级，val为日志等级对应的字符串，fmt为输出格式，...为可变参数
#define PR(level, val, fmt, ...)  \
    do {                                                    \
            if( level <= pr_level )                         \
                printf("[%-5s]" "[%s:%d] " fmt, val,         \
                    __FUNCTION__, __LINE__, ##__VA_ARGS__);   \
    } while(0)    ///这里不用添加;  因为使用日志时调用会在代码后习惯添加;

#define PR_DEBUG(fmt, ...)  \
    PR(PR_LEVEL_DEBUG, "debug", fmt, ##__VA_ARGS__)

#define PR_INFO(fmt, ...)  \
    PR(PR_LEVEL_INFO, "info", fmt, ##__VA_ARGS__)

#define PR_WARN(fmt, ...)  \
    PR(PR_LEVEL_WARN, "warn", fmt, ##__VA_ARGS__)

#define PR_ERROR(fmt, ...)  \
    PR(PR_LEVEL_ERROR, "error", fmt, ##__VA_ARGS__)

int pr_set_level(int level);

string tid_to_string(const thread::id& tid);

long long tid_to_ll(const thread::id& tid);

#endif          