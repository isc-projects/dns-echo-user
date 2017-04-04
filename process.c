#define _GNU_SOURCE
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <sched.h>
#include <wait.h>
#include <stdio.h>
#include "config.h"
#include "process.h"

static void getcpu(cpu_set_t* cpus, int n)
{
	int count = CPU_COUNT(cpus);
	n %= count;

	for (int i = 0; n >= 0; ++i) {
		if (CPU_ISSET(i, cpus)) {
			if (n-- == 0) {
				CPU_ZERO(cpus);
				CPU_SET(i, cpus);
				return;
			}
		}
	}

	fprintf(stderr, "unexpectedly ran out of CPUs");
}

static void make_threads(int childnum, int threads, handler_fn fn, cleaner_fn cfn, void *data, int flags)
{
	pthread_t		pt[threads];
	pthread_attr_t	attr;
	int ncpus = sysconf(_SC_NPROCESSORS_ONLN);

	/* start the desired number of threads */
	pthread_attr_init(&attr);
	for (int i = 0; i < threads; ++i) {
		pthread_create(&pt[i], &attr, fn, data);
		if (flags & FARM_AFFINITY_THREAD) {
			cpu_set_t	cpus;
			CPU_ZERO(&cpus);
			CPU_SET(i % ncpus, &cpus);
			pthread_setaffinity_np(pt[i], sizeof(cpus), &cpus);
		}
	}
	pthread_attr_destroy(&attr);

	/* wait for all of the threads to finish */
	for (int i = 0; i < threads; ++i) {
		void *result = NULL;
		if (pthread_join(pt[i], &result) == 0) {
			if (cfn && result) {
				cfn(childnum, i, result);
			}
		}
	}
}

void farm(int forks, int threads, handler_fn fn, cleaner_fn cfn, void *data, int flags)
{
	if (forks < 1) {
		make_threads(0, threads, fn, cfn, data, flags);
	} else {

		/* fork the desired number of children */
		for (int child = 0; child < forks; ++child) {
			pid_t pid = fork();
			if (pid == 0) {			/* child */
				make_threads(child, threads, fn, cfn, data, flags);
				break;
			} else if (pid < 0) {	/* error */
				perror("fork");
			} else {
				if (flags & FARM_AFFINITY_FORK) {
					cpu_set_t cpus;
					sched_getaffinity(pid, sizeof(cpus), &cpus);
					getcpu(&cpus, child);
					sched_setaffinity(pid, sizeof(cpus), &cpus);
				}
			}
		}

		/* reap children */
		while (wait(NULL) > 0);
	}
}
