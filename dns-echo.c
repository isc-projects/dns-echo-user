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
#include <time.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>

#ifdef HAVE_LIBEVENT
#include <event.h>
#endif

#include "config.h"
#include "process.h"

/* #define BUFSIZE 32768 */

int						port = 8053;
sig_atomic_t			quit = 0;

const int				default_timeout = 100;		/* 100ms */
const struct timeval	default_timeval = { 0, 100e3 };
const struct timespec	default_timespec = { 0, 100e6 };
FILE*					output;

char*					ifname = NULL;

static int get_socket(int reuse)
{
	struct sockaddr_in addr;
	struct timeval tv = default_timeval;
#if defined(BUFSIZE)
	int bufsize = BUFSIZE;
#endif

	int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (fd < 0) {
		perror("socket");
		return -1;
	}

	if (setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) < 0) {
		perror("setsockopt(SO_REUSEPORT)");
	}

	if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
		perror("setsockopt(SO_RCVTIMEO)");
	}

#if defined(BUFSIZE)
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
		return -1;
	}

	return fd;
}

static int get_fd(void *userdata)
{
	int fd = *(int *)userdata;
	if (fd < 0) {
		fd = get_socket(1);
	}
	return fd;
}

void *count_return(uint64_t count)
{
	uint64_t *p = (uint64_t *)malloc(sizeof count);
	*p = count;
	return p;
}

static void cleaner(int f, int t, void *data)
{
	uint64_t p = *(uint64_t *)data;
	free(data);

	if (output) {
		fprintf(output, "%d\t%d\t%llu\n", f, t, p);
		fflush(output);
	}
}

void make_echo(unsigned char *buf, int len)
{
	if (len < 4) return;

	/* clear AA and TC bits */
	buf[2] &= 0xf9;

	/* clear Z bit and RCODE */
	buf[3] &= 0x00;

	/* set QR bit */
	buf[2] |= 0x80;
}

static void *blocking_loop(void *userdata) 
{
	int fd = get_fd(userdata);
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
			make_echo(buf, len);
			sendto(fd, buf, len, 0, (struct sockaddr *)&client, clientlen);
			++count;
		}
	}

	return count_return(count);
}

static void *nonblocking_loop(void *userdata) 
{
	int fd = get_fd(userdata);
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
			make_echo(buf, len);
			sendto(fd, buf, len, MSG_DONTWAIT, (struct sockaddr *)&client, clientlen);
			++count;
		}
	}

	return count_return(count);
}

#ifdef HAVE_RECVMMSG 
static void *mmsg_loop(void *userdata) 
{
	int fd = get_fd(userdata);
	int vecsize = 16;
	int bufsize = 512;
	uint64_t count = 0;
	struct iovec iovecs[vecsize];
	struct sockaddr_storage clients[vecsize];
	unsigned char buf[vecsize][bufsize];
	struct mmsghdr msgs[vecsize];
	struct timespec tv = default_timespec;
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
				make_echo(buf[i], msgs[i].msg_len);
			}
			sendmmsg(fd, msgs, n, 0);
			count += n;
		}
	}

	return count_return(count);
}
#endif /* RECV_MMSG */

static void *polling_loop(void *userdata) 
{
	int fd = get_fd(userdata);
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

		int res = poll(&fds[0], 1, default_timeout);
		if (res == 0) continue;

		clientlen = sizeof(client);
		len = recvfrom(fd, buf, size, 0, (struct sockaddr *)&client, &clientlen);
		if (len < 0) {
			if (errno != EAGAIN) break;
		} else {
			make_echo(buf, len);
			sendto(fd, buf, len, 0, (struct sockaddr *)&client, clientlen);
			++count;
		}
	}

	return count_return(count);
}

static void *select_loop(void *userdata) 
{
	int fd = get_fd(userdata);
	int size = 512;
	uint64_t count = 0;
	unsigned char buf[size];
	struct sockaddr_storage client;

	fd_set fds;

	while (!quit) {
		struct timeval tv = default_timeval;
		int res, len;

		socklen_t clientlen = sizeof(client);
		FD_ZERO(&fds);
		FD_SET(fd, &fds);

		res = select(fd + 1, &fds, NULL, NULL, &tv);
		if (res == 0) continue;
		if (res < 0) {
			perror("select");
			break;
		}

		len = recvfrom(fd, buf, size, MSG_DONTWAIT, (struct sockaddr *)&client, &clientlen);
		if (len < 0) {
			if (errno != EAGAIN) break;
		} else {
			make_echo(buf, len);
			sendto(fd, buf, len, 0, (struct sockaddr *)&client, clientlen);
			++count;
		}
	}

	return count_return(count);
}

#ifdef HAVE_LIBEVENT

/*
 * struct required to allow both the event_base and the count
 * variable to be passed to the libevent callback function
 */
struct libevent_data {
	struct		event_base *base;
	uint64_t	count;
};

void libevent_func(int fd, short flags, void *userdata)
{
	int len, size = 512;
	struct libevent_data *data = (struct libevent_data *)userdata;
	unsigned char buf[size];
	struct sockaddr_storage client;
	socklen_t clientlen;

	(void)flags;		/* silence compiler warnings */

	clientlen = sizeof(client);
	len = recvfrom(fd, buf, size, 0, (struct sockaddr *)&client, &clientlen);
	if (len > 0) {
		make_echo(buf, len);
		sendto(fd, buf, len, 0, (struct sockaddr *)&client, clientlen);
		++(data->count);
	}

	if (quit) {
		event_base_loopbreak(data->base);
	}
}

static void *libevent_loop(void *userdata)
{
	int fd = get_fd(userdata);
	struct timeval tv = default_timeval;
	struct libevent_data data = {
		.base = event_base_new(),
		.count = 0
	};
	struct event *ev = event_new(data.base, fd, EV_READ | EV_PERSIST, libevent_func, &data);
	fcntl(fd, F_SETFL, O_NONBLOCK);
	event_add(ev, &tv);
	event_base_dispatch(data.base);

	return count_return(data.count);
}

#endif /* HAVE_LIBEVENT */

#ifdef HAVE_LINUX_IF_PACKET_H
extern void *packet_helper(const char *, int, struct timeval);

static void *packet_loop(void *userdata)
{
	(void) userdata;

	return packet_helper(ifname, port, default_timeval);
}
#endif /* HAVE_LINUX_IF_PACKET_H */

__attribute__ ((noreturn))
void usage(int ret) 
{
	fprintf(stderr, "usage: cmd [-p <port>] [-o outfile] [-a] [-r] [-m <mode>] [-f forks] [-t threads]\n");
	fprintf(stderr, "  -m : <mode> = b  (= blocking)\n");
	fprintf(stderr, "              | n  (= nonblock)\n");
	fprintf(stderr, "              | p  (= poll)\n");
	fprintf(stderr, "              | s  (= select)\n");
#ifdef HAVE_RECVMMSG
	fprintf(stderr, "              | m  (= recvmmsg)\n");
#endif
#ifdef HAVE_LIBEVENT
	fprintf(stderr, "              | l  (= libevent)\n");
#endif
#ifdef HAVE_LINUX_IF_PACKET_H
	fprintf(stderr, "              | r  (= Linux AF_PACKET mode)\n");
	fprintf(stderr, "  -i : set AF_PACKET interface\n");
#endif
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
	const char *outfile = NULL;
	handler_fn	f = NULL;

	--argc; ++argv;
	while (argc > 0 && **argv == '-') {
		char o = *++*argv;
		switch (o) {
			case 'f': --argc; ++argv; check(argv); forks = atoi(*argv); break;
			case 't': --argc; ++argv; check(argv); threads = atoi(*argv); break;
			case 'm': --argc; ++argv; check(argv); mode = *argv; break;
			case 'p': --argc; ++argv; check(argv); port = atoi(*argv); break;
			case 'o': --argc; ++argv; check(argv); outfile = strdup(*argv); break;
			case 'i': --argc; ++argv; check(argv); ifname = strdup(*argv); break;
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
#ifdef HAVE_RECVMMSG
		case 'm':
			fprintf(stderr, "mmsg mode\n");
			f = mmsg_loop;
			break;
#endif
		case 'p':
			fprintf(stderr, "polling mode\n");
			f = polling_loop;
			break;
#ifdef HAVE_LINUX_IF_PACKET_H
		case 'r':
			fprintf(stderr, "raw AF_PACKET mode\n");
			f = packet_loop;
			break;
#endif
		case 's':
			fprintf(stderr, "select mode\n");
			f = select_loop;
			break;
		case 'l':
#ifdef HAVE_LIBEVENT
			fprintf(stderr, "libevent mode (%s)\n", event_base_get_method(event_base_new()));
			f = libevent_loop;
			break;
#endif
		default:
			badargs();
	}

	if (argc) {
		badargs();
	}

	if (!reuse) {
#ifdef HAVE_LINUX_IF_PACKET_H
		if (f == packet_loop) {
			/* ignored */
		} else
#endif
		{
			fd = get_socket(0);
		}
	}

#ifdef HAVE_LINUX_IF_PACKET_H
	if (f == packet_loop && !ifname) {
		fprintf(stderr, "no interface name specified for AF_PACKET mode\n");
		return EXIT_FAILURE;
	}
#endif

	fprintf(stderr, "starting with %d forks and %d threads\n", forks, threads);

	if (affinity) {
		flags = (forks > 0) ? FARM_AFFINITY_FORK : FARM_AFFINITY_THREAD;
	}

	signal(SIGINT, stop);
	signal(SIGTERM, stop);

	if (outfile) {
		output = fopen(outfile, "a+");
		if (!output) {
			perror("fopen");
		}
	} else {
		output = stdout;
	}

	if (output) {
		char buffer[200];
		time_t now = time(NULL);
		strftime(buffer, sizeof(buffer), "%Y-%m-%d %T\n", gmtime(&now));
		fputs(buffer, output);
		fflush(output);
	}

	farm(forks, threads, f, cleaner, &fd, flags);

	if (output) {
		fclose(output);
	}
}
