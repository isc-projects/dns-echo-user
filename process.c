/*
 * Copyright (C) 2017 Internet Systems Consortium, Inc. ("ISC")
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifdef __linux__
#define _GNU_SOURCE
#endif

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <sched.h>

#if defined(HAVE_SYS_WAIT_H)
#include <sys/wait.h>
#elif defined(HAVE_WAIT_H)
#include <wait.h>
#endif

#include "process.h"

#if defined(HAVE_SCHED_SETAFFINITY) || defined(HAVE_PTHREAD_SETAFFINITY_NP)
/*
 * takes a cpu_set_t and modifies is so that
 * only the nth CPU (modulo the number of CPUs
 * in the original set) is returned
 */
static void set_one_cpu(cpu_set_t* cpus, int n)
{
	int count = CPU_COUNT(cpus);
	n %= count;

	for (unsigned int i = 0; n >= 0; ++i) {
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
#endif

/*
 * makes a collection of 'threads' threads, calling the
 * specified function 'fn', optionally calling the
 * cleaner function 'cfn' on termination
 */
static void make_threads(unsigned int childnum, unsigned int threads, handler_fn fn, cleaner_fn cfn, void *data, int flags)
{
	pthread_t		pt[threads];
	pthread_attr_t	attr;

	/* start the desired number of threads */
	pthread_attr_init(&attr);
	for (unsigned int thread = 0; thread < threads; ++thread) {
		pthread_create(&pt[thread], &attr, fn, data);
#ifdef HAVE_PTHREAD_SETAFFINITY_NP
		if (flags & FARM_AFFINITY_THREAD) {
			cpu_set_t	cpus;
			pthread_getaffinity_np(pt[thread], sizeof(cpus), &cpus);
			set_one_cpu(&cpus, thread);
			pthread_setaffinity_np(pt[thread], sizeof(cpus), &cpus);
		}
#endif /* HAVE_PTHREAD_SETAFFINITY_NP */

#if defined(HAVE_PTHREAD_SETNAME_NP) && defined(__GNU_SOURCE)
		/* set thread name */
		{
			char name[16];
			snprintf(name, sizeof(name), "process-%02d-%02d", childnum % 100, thread % 100);
			pthread_setname_np(pt[thread], name);
		}
#endif
	}
	pthread_attr_destroy(&attr);

	/* wait for all of the threads to finish */
	for (unsigned int thread = 0; thread < threads; ++thread) {
		void *result = NULL;
		if (pthread_join(pt[thread], &result) == 0) {
			if (cfn && result) {
				cfn(childnum, thread, result);
			}
		}
	}
}

/*
 * makes a collection of 'forks' subprocesses each of which
 * will spawn 'threads' threads
 */
void farm(unsigned int forks, unsigned int threads, handler_fn fn, cleaner_fn cfn, void *data, int flags)
{
	/* create our own process group */
	setpgrp();

	if (forks < 1) {
		/* don't fork if we don't have to */
		make_threads(0, threads, fn, cfn, data, flags);
	} else {

		/* fork the desired number of children */
		for (unsigned int child = 0; child < forks; ++child) {
			pid_t pid = fork();
			if (pid == 0) {			/* child */
				make_threads(child, threads, fn, cfn, data, flags);
				exit(0);			/* children shouldn't run the loop */
			} else if (pid < 0) {	/* error */
				perror("fork");
			} else {
#ifdef HAVE_SCHED_SETAFFINITY
				/* optionally set this sub-process's CPU affinity */
				if (flags & FARM_AFFINITY_FORK) {
					cpu_set_t cpus;
					sched_getaffinity(pid, sizeof(cpus), &cpus);
					set_one_cpu(&cpus, child);
					sched_setaffinity(pid, sizeof(cpus), &cpus);
				}
#endif /* HAVE_SCHED_SETAFFINITY */
			}
		}

		/* reap children */
		while (wait(NULL) > 0);
	}
}
