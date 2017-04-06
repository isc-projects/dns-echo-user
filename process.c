/*
 * Copyright (C) 2017 Internet Systems Consortium, Inc. ("ISC")
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#define _GNU_SOURCE

#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <sched.h>
#include <wait.h>
#include <stdio.h>
#include "config.h"
#include "process.h"

/*
 * takes a cpu_set_t and modifies is so that
 * only the nth CPU (modulo the number of CPUs
 * in the original set) is returned
 */
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

/*
 * makes a collection of 'threads' threads, calling the
 * specified function 'fn', optionally calling the
 * cleaner function 'cfn' on termination
 */
static void make_threads(int childnum, int threads, handler_fn fn, cleaner_fn cfn, void *data, int flags)
{
	pthread_t		pt[threads];
	pthread_attr_t	attr;

	/* start the desired number of threads */
	pthread_attr_init(&attr);
	for (int thread = 0; thread < threads; ++thread) {
		pthread_create(&pt[thread], &attr, fn, data);
		if (flags & FARM_AFFINITY_THREAD) {
			cpu_set_t	cpus;
			pthread_getaffinity_np(pt[thread], sizeof(cpus), &cpus);
			getcpu(&cpus, thread);
			pthread_setaffinity_np(pt[thread], sizeof(cpus), &cpus);
		}
	}
	pthread_attr_destroy(&attr);

	/* wait for all of the threads to finish */
	for (int thread = 0; thread < threads; ++thread) {
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
void farm(int forks, int threads, handler_fn fn, cleaner_fn cfn, void *data, int flags)
{
	/* create our own process group */
	setpgrp();

	if (forks < 1) {
		/* don't fork if we don't have to */
		make_threads(0, threads, fn, cfn, data, flags);
	} else {

		/* fork the desired number of children */
		for (int child = 0; child < forks; ++child) {
			pid_t pid = fork();
			if (pid == 0) {			/* child */
				make_threads(child, threads, fn, cfn, data, flags);
				exit(0);			/* children shouldn't run the loop */
			} else if (pid < 0) {	/* error */
				perror("fork");
			} else {
				/* optionally set this sub-process's CPU affinity */
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
