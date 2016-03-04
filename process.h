#ifndef __PROCESS_H
#define __PROCESS_H

#define FARM_AFFINITY_FORK		1
#define FARM_AFFINITY_THREAD	2

typedef void *(*routine)(void *) ;

extern void farm(int forks, int threads, routine fn, void *data, int flags);

#endif /* __PROCESS_H */
