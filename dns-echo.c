/*
 * Copyright (C) 2017 Internet Systems Consortium, Inc. ("ISC")
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <event.h>
#include "process.h"

int				port = 8053;
sig_atomic_t	quit = 0;

static int getsocket(int reuse)
{
	struct sockaddr_in addr;
	struct timeval timeout = { 0, 100000 };	/* 0.1s */

#if 0
	int bufsize = 32768;
#endif

	int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (fd < 0) {
		perror("socket");
		return EXIT_FAILURE;
	}

	if (setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) < 0) {
		perror("setsockopt(SO_REUSEPORT)");
	}

	if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
		perror("setsockopt(SO_RCVTIMEO)");
	}

#if 0
	if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &bufsize, sizeof(bufsize)) < 0) {
		perror("setsockopt(SO_RCVBUF)");
	}

	if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &bufsize, sizeof(bufsize)) < 0) {
		perror("setsockopt(SO_SENDBUF)");
	}
#endif

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(port);

	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind");
		return EXIT_FAILURE;
	}

	return fd;
}

static int getfd(void *userdata)
{
	int fd = *(int *)userdata;
	if (fd < 0) {
		fd = getsocket(1);
	}
	return fd;
}

static void *count_return(uint64_t count)
{
	uint64_t *p = (uint64_t *)malloc(sizeof count);
	*p = count;
	return p;
}

static void cleaner(int f, int t, void *data)
{
	uint64_t p = *(uint64_t *)data;
	free(data);

	fprintf(stderr, "%d\t%d\t%lu\n", f, t, p);
	fflush(stderr);
}

static void make_echo(unsigned char *buf)
{
	/* clear AA and TC bits */
	buf[2] &= 0xf9;

	/* clear Z bit and RCODE */
	buf[3] &= 0x00;

	/* set QR bit */
	buf[2] |= 0x80;
}

static void *blocking_loop(void *userdata) 
{
	int fd = getfd(userdata);
	int size = 512;
	uint64_t count = 0;
	unsigned char buf[size];
	struct sockaddr_storage client;

	while (!quit) {
		socklen_t clientlen = sizeof(client);
		int len = recvfrom(fd, buf, size, 0, (struct sockaddr *)&client, &clientlen);
		if (len < 0) {
			if (errno != EAGAIN) break;
		} else {
			make_echo(buf);
			sendto(fd, buf, len, 0, (struct sockaddr *)&client, clientlen);
			++count;
		}
	}

	return count_return(count);
}

static void *nonblocking_loop(void *userdata) 
{
	int fd = getfd(userdata);
	int size = 512;
	uint64_t count = 0;
	unsigned char buf[size];
	struct sockaddr_storage client;

	while (!quit) {
		socklen_t clientlen = sizeof(client);
		int len = recvfrom(fd, buf, size, MSG_DONTWAIT, (struct sockaddr *)&client, &clientlen);
		if (len < 0) {
			if (errno != EAGAIN) break;
		} else {
			make_echo(buf);
			sendto(fd, buf, len, MSG_DONTWAIT, (struct sockaddr *)&client, clientlen);
			++count;
		}
	}

	return count_return(count);
}

static void *mmsg_loop(void *userdata) 
{
	int fd = getfd(userdata);
	int vecsize = 16;
	int bufsize = 512;
	uint64_t count = 0;
	struct iovec iovecs[vecsize];
	struct sockaddr_storage clients[vecsize];
	unsigned char buf[vecsize][bufsize];
	struct mmsghdr msgs[vecsize];
	struct timespec tv = { 0, 1000L };
	int i, n;

	while (!quit) {

		/* initialise structures */
		for (i = 0; i < vecsize; ++i) {
			iovecs[i].iov_base = buf[i];
			iovecs[i].iov_len  = bufsize;
			msgs[i].msg_hdr.msg_iov = &iovecs[i];
			msgs[i].msg_hdr.msg_iovlen = 1;
			msgs[i].msg_hdr.msg_name = &clients[i];
			msgs[i].msg_hdr.msg_namelen = sizeof(clients[i]);
		}

		n = recvmmsg(fd, msgs, vecsize, 0, &tv);
		if (n < 0) {
			if (errno != EAGAIN) break;
		} else {
			for (i = 0; i < n; ++i) {
				make_echo(buf[i]);
			}
			sendmmsg(fd, msgs, n, 0);
			count += n;
		}
	}

	return count_return(count);
}

static void *polling_loop(void *userdata) 
{
	int fd = getfd(userdata);
	int size = 512;
	uint64_t count = 0;
	unsigned char buf[size];
	struct sockaddr_storage client;

	struct pollfd fds[1] = {
		{ fd, POLLIN, 0 }
	};

	while (!quit) {
		socklen_t clientlen;
		int len;

		int res = poll(&fds[0], 1, 100);
		if (res == 0) continue;

		clientlen = sizeof(client);
		len = recvfrom(fd, buf, size, 0, (struct sockaddr *)&client, &clientlen);
		if (len < 0) {
			if (errno != EAGAIN) break;
		} else {
			make_echo(buf);
			sendto(fd, buf, len, 0, (struct sockaddr *)&client, clientlen);
			++count;
		}
	}

	return count_return(count);
}

static void *select_loop(void *userdata) 
{
	int fd = getfd(userdata);
	int size = 512;
	uint64_t count = 0;
	unsigned char buf[size];
	struct sockaddr_storage client;

	fd_set fds;

	while (!quit) {
		struct timeval timeout = { 0, 100000 }; /* 0.1s */
		int res, len;

		socklen_t clientlen = sizeof(client);
		FD_ZERO(&fds);
		FD_SET(fd, &fds);

		res = select(fd + 1, &fds, NULL, NULL, &timeout);
		if (res == 0) continue;
		if (res < 0) {
			perror("select");
			break;
		}

		len = recvfrom(fd, buf, size, MSG_DONTWAIT, (struct sockaddr *)&client, &clientlen);
		if (len < 0) {
			if (errno != EAGAIN) break;
		} else {
			make_echo(buf);
			sendto(fd, buf, len, 0, (struct sockaddr *)&client, clientlen);
			++count;
		}
	}

	return count_return(count);
}

void libevent_func(int fd, short flags, void *userdata)
{
	int len, size = 512;
	struct event_base *base = (struct event_base *)userdata;
	unsigned char buf[size];
	struct sockaddr_storage client;
	socklen_t clientlen;

	(void)flags;		/* silence compiler warnings */

	clientlen = sizeof(client);
	len = recvfrom(fd, buf, size, 0, (struct sockaddr *)&client, &clientlen);
	if (len > 0) {
		make_echo(buf);
		sendto(fd, buf, len, 0, (struct sockaddr *)&client, clientlen);
	}

	if (quit) {
		event_base_loopbreak(base);
	}
}

static void *libevent_loop(void *userdata)
{
	struct timeval timeout = { 0, 100000 }; /* 0.1s */
	int fd = getfd(userdata);
	struct event_base *base = event_base_new();
	struct event *ev = event_new(base, fd, EV_READ | EV_PERSIST, libevent_func, base);
	fcntl(fd, F_SETFL, O_NONBLOCK);
	event_add(ev, &timeout);
	event_base_dispatch(base);

	return NULL;
}

__attribute__ ((noreturn))
void usage(int ret) 
{
	fprintf(stderr, "usage: cmd [-p <port>] [-a] [-r] [-m <block|nonblock|poll|select|mmsg|libevent>] [-f forks] [-t threads]\n");
	fprintf(stderr, "  -a : set processor affinity\n");
	fprintf(stderr, "  -r : enable SO_REUSEPORT\n");
	exit(ret);
}

void check(char **argv)
{
	if (!*argv) usage(EXIT_FAILURE);
}

__attribute__ ((noreturn))
void badargs(void)
{
	usage(EXIT_FAILURE);
}

void stop(int sig)
{
	if (!quit) {
		kill(0, sig);
	}
	quit = 1;
}

int main(int argc, char *argv[])
{
	int			fd = -1;
	int			reuse = 0;
	int			forks = 0;
	int			threads = 1;
	int			affinity = 0;
	int			flags = 0;
	const char *mode = "b";
	handler_fn	f = NULL;

	--argc; ++argv;
	while (argc > 0 && **argv == '-') {
		char o = *++*argv;
		switch (o) {
			case 'f': --argc; ++argv; check(argv); forks = atoi(*argv); break;
			case 't': --argc; ++argv; check(argv); threads = atoi(*argv); break;
			case 'm': --argc; ++argv; check(argv); mode = *argv; break;
			case 'p': --argc; ++argv; check(argv); port = atoi(*argv); break;
			case 'a': affinity = 1; break;
			case 'r': reuse = 1; break;
			case 'h': usage(EXIT_SUCCESS); break;
			default: usage(EXIT_FAILURE);
		}
		--argc; ++argv;
	}

	switch (tolower(mode[0])) {
		case 'b':
			fprintf(stderr, "blocking mode\n");
			f = blocking_loop;
			break;
		case 'n':
			fprintf(stderr, "non-blocking mode\n");
			f = nonblocking_loop;
			break;
		case 'm':
			fprintf(stderr, "mmsg mode\n");
			f = mmsg_loop;
			break;
		case 'p':
			fprintf(stderr, "polling mode\n");
			f = polling_loop;
			break;
		case 's':
			fprintf(stderr, "select mode\n");
			f = select_loop;
			break;
		case 'l':
			fprintf(stderr, "libevent mode (%s)\n", event_base_get_method(event_base_new()));
			f = libevent_loop;
			break;
		default:
			badargs();
	}

	if (argc) {
		badargs();
	}

	if (!reuse) {
		fd = getsocket(0);
	}

	fprintf(stderr, "starting with %d forks and %d threads\n", forks, threads);

	if (affinity) {
		flags = (forks > 0) ? FARM_AFFINITY_FORK : FARM_AFFINITY_THREAD;
	}

	signal(SIGINT, stop);
	signal(SIGTERM, stop);

	farm(forks, threads, f, cleaner, &fd, flags);
}
