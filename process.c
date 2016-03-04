#define _GNU_SOURCE
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

static void make_threads(int threads, routine fn, void *data, int flags)
{
	if (threads <= 1) {
		fn(data);
	} else {
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
			pthread_join(pt[i], NULL);
		}
	}
}

void farm(int forks, int threads, routine fn, void *data, int flags)
{
	if (forks < 1) {
		make_threads(threads, fn, data, flags);
	} else {

		/* fork the desired number of children */
		for (int i = 0; i < forks; ++i) {
			pid_t pid = fork();
			if (pid == 0) {			/* child */
				make_threads(threads, fn, data, flags);
			} else if (pid < 0) {	/* error */
				perror("fork");
			} else {
				if (flags & FARM_AFFINITY_FORK) {
					cpu_set_t cpus;
					sched_getaffinity(pid, sizeof(cpus), &cpus);
					getcpu(&cpus, i);
					sched_setaffinity(pid, sizeof(cpus), &cpus);
				}
			}
		}

		/* reap children */
		while (wait(NULL) > 0);
	}
}
