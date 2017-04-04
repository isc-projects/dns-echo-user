#ifndef __PROCESS_H
#define __PROCESS_H

#define FARM_AFFINITY_FORK		1
#define FARM_AFFINITY_THREAD	2

typedef void *(*handler_fn)(void *);
typedef void  (*cleaner_fn)(int, int, void *);

extern void farm(int forks, int threads, handler_fn fn, cleaner_fn cfn, void *data, int flags);

#endif /* __PROCESS_H */
