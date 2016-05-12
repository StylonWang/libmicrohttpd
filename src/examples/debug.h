#ifndef __MY_DEBUG_H__
#define __MY_DEBUG_H__

#include <stdio.h>
#include <time.h>

#define dbgprint(fmt, ...) do { \
            printf("[%s:%d %ld] " fmt, __FUNCTION__, __LINE__, (unsigned long)time(NULL)%10000, ##__VA_ARGS__);\
        } while(0)

#endif //__MY_DEBUG_H__
