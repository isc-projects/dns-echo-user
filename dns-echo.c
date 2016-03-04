/*
 * $Id: $
 *
 * Copyright (c) 2015, Internet Systems Consortium, Inc. (ISC)
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of ISC nor the names of its contributors may
 *       be used to endorse or promote products derived from this software
 *       without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY ISC ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL ISC BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
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
#include <errno.h>
#include <fcntl.h>
#include <event.h>
#include "process.h"

int		port = 8053;

static int getsocket(int reuse)
{
	struct sockaddr_in addr;

	int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (fd < 0) {
		perror("socket");
		return EXIT_FAILURE;
	}

	if (setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) < 0) {
		perror("setsockopt");
	}

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

static int valid(unsigned char *buf, int len)
{
	return (len >= 12) && ((buf[2] & 0xf8) == 0);
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

static void *blocking_loop(void* userdata) 
{
	int fd = getfd(userdata);
	int size = 512;
	unsigned char buf[size];
	struct sockaddr_storage client;

	while (1) {
		socklen_t clientlen = sizeof(client);
		int len = recvfrom(fd, buf, size, 0, (struct sockaddr *)&client, &clientlen);
		if (len < 0 && errno != EAGAIN) break;

		if (!valid(buf, len)) continue;
		make_echo(buf);
		sendto(fd, buf, len, 0, (struct sockaddr *)&client, clientlen);
	}

	if (errno) {
		perror("recvfrom");
	}

	return NULL;
}

static void *mmsg_loop(void* userdata) 
{
	int fd = getfd(userdata);
	int vecsize = 64;
	int bufsize = 512;
	struct iovec iovecs[vecsize];
	struct sockaddr_storage clients[vecsize];
	unsigned char buf[vecsize][bufsize];
	struct mmsghdr msgs[vecsize];
	int i, n;

	// initialise structures
	for (i = 0; i < vecsize; ++i) {
		iovecs[i].iov_base = buf[i];
		iovecs[i].iov_len  = bufsize;
		msgs[i].msg_hdr.msg_iov = &iovecs[i];
		msgs[i].msg_hdr.msg_iovlen = 1;
		msgs[i].msg_hdr.msg_name = &clients[i];
		msgs[i].msg_hdr.msg_namelen = sizeof(clients[i]);
	}

	while (1) {

		n = recvmmsg(fd, msgs, vecsize, MSG_WAITFORONE, NULL);
		if (n < 0 && errno != EAGAIN) break;

		for (i = 0; i < n; ++i) {
			// if (!valid(buf, len)) continue;
			make_echo(buf[i]);
		}

		sendmmsg(fd, msgs, n, 0);
	}

	if (errno) {
		perror("recvfrom");
	}

	return NULL;
}

static void *polling_loop(void* userdata) 
{
	int fd = getfd(userdata);
	int size = 512;
	unsigned char buf[size];
	struct sockaddr_storage client;

	struct pollfd fds[1] = {
		{ fd, POLLIN, 0 }
	};

	while (1) {
		socklen_t clientlen;
		int len;

		int res = poll(&fds[0], 1, -1);
		if (res == 0) continue;

		clientlen = sizeof(client);
		len = recvfrom(fd, buf, size, 0, (struct sockaddr *)&client, &clientlen);

		if (len < 0 && errno != EAGAIN) break;
		if (!valid(buf, len)) continue;
		make_echo(buf);
		sendto(fd, buf, len, MSG_DONTWAIT, (struct sockaddr *)&client, clientlen);
	}

	if (errno) {
		perror("recvfrom");
	}

	return NULL;
}

static void *select_loop(void* userdata) 
{
	int fd = getfd(userdata);
	int size = 512;
	unsigned char buf[size];
	struct sockaddr_storage client;

	fd_set fds;

	while (1) {
		int res, len;

		socklen_t clientlen = sizeof(client);
		FD_ZERO(&fds);
		FD_SET(fd, &fds);

		res = select(fd + 1, &fds, NULL, NULL, NULL);
		if (res == 0) continue;
		if (res < 0) {
			perror("select");
			break;
		}

		len = recvfrom(fd, buf, size, MSG_DONTWAIT, (struct sockaddr *)&client, &clientlen);
		if (len < 0 && errno != EAGAIN) break;
		if (!valid(buf, len)) continue;
		make_echo(buf);
		sendto(fd, buf, len, MSG_DONTWAIT, (struct sockaddr *)&client, clientlen);
	}

	return NULL;
}

void libevent_func(int fd, short flags, void *userdata)
{
	int len, size = 512;
	unsigned char buf[size];
	struct sockaddr_storage client;
	socklen_t clientlen;

	(void)flags;		// silence compiler warnings
	(void)userdata;

	clientlen = sizeof(client);
	len = recvfrom(fd, buf, size, 0, (struct sockaddr *)&client, &clientlen);
	if (len < 0 && errno != EAGAIN) return;
	if (!valid(buf, len)) return;
	make_echo(buf);
	sendto(fd, buf, len, 0, (struct sockaddr *)&client, clientlen);
}

static void *libevent_loop(void *userdata)
{
	int fd = getfd(userdata);
	struct event_base *base = event_base_new();
	struct event *ev = event_new(base, fd, EV_READ | EV_PERSIST, libevent_func, 0);
	event_add(ev, NULL);
	event_base_dispatch(base);

	return NULL;
}

__attribute__ ((noreturn))
void usage(int ret) 
{
	fprintf(stderr, "usage: cmd [-a] [-r] [-m <block|poll|select|libevent>] [-f forks] [-t threads]\n");
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

int main(int argc, char *argv[])
{
	int			fd = -1;
	int			reuse = 0;
	int			forks = 0;
	int			threads = 1;
	int			affinity = 0;
	int			flags = 0;
	const char *mode = "b";
	routine		f = NULL;

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

	farm(forks, threads, f, &fd, flags);
}
