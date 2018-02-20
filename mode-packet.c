/*
 * Copyright (C) 2017 Internet Systems Consortium, Inc. ("ISC")
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#define _GNU_SOURCE

#include "config.h"

#ifdef HAVE_LINUX_IF_PACKET_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <poll.h>

#include <net/if.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>

#include "dns-echo.h"

static int get_packet_socket(const char *ifname)
{
	struct sockaddr_ll addr;
	struct ifreq ifr;
	uint32_t fanout_arg = getppid() & 0xffff; /* use the parent's PID as the packet group */

	int fd = socket(AF_PACKET, SOCK_DGRAM, htons(ETH_P_IP));
	if (fd < 0) {
		perror("socket(AF_PACKET)");
		return -1;
	}

	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
	if (ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
		perror("ioctl(SIOCGIFINDEX)");
		return -1;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sll_family = AF_PACKET;
	addr.sll_ifindex = ifr.ifr_ifindex;
	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind");
		return -1;
	}

	fanout_arg |= ((PACKET_FANOUT_LB | PACKET_FANOUT_FLAG_ROLLOVER) << 16);
	if (setsockopt(fd, SOL_PACKET, PACKET_FANOUT, &fanout_arg, sizeof fanout_arg) < 0) {
		perror("setsockopt(PACKET_FANOUT)");
		return -1;
	}

	return fd;
}

void *packet_helper(const char *ifname, int port, int timeout)
{
	int fd = get_packet_socket(ifname);
	int size = 512;
	uint64_t count = 0;
	unsigned char buf[size];
	struct sockaddr_storage client;
	struct pollfd fds = { fd, POLLIN, 0 };
	socklen_t clientlen;

	while (!quit) {
		int res, len;

		res = poll(&fds, 1, timeout); 
		if (res == 0) continue;
		if (res < 0) {
			perror("poll");
			break;
		}

		clientlen = sizeof(client);
		len = recvfrom(fd, buf, size, 0, (struct sockaddr *)&client, &clientlen);
		if (len < 0) {
			if (errno != EAGAIN) {
				perror("recvfrom");
				break;
			}
		} else {
			uint16_t tmp16;
			uint32_t tmp32;
			struct iphdr *ip = (struct iphdr *)buf;
			struct udphdr *udp = (struct udphdr *)(buf + 4 * ip->ihl);
			unsigned char *data = ((unsigned char *)udp) + sizeof(*udp);

			/* packet too short - give up */
			if (data > buf + len) {
				continue;
			}

			/* not IPv4 */
			if (ip->protocol != IPPROTO_UDP) {
				continue;
			}

			/* not the right port */
			if (ntohs(udp->dest) != port) {
				continue;
			}

			/* swap source and dest addresses, ports - doesn't change IP checksum */
			tmp32 = ip->saddr;
			ip->saddr = ip->daddr;
			ip->daddr = tmp32;

			tmp16 = udp->source;
			udp->source = udp->dest;
			udp->dest = tmp16;

			/* no checksum */
			udp->check = 0;

			/* return packet */
			make_echo(data, len - (data - buf));
			clientlen = sizeof(client);
			if (sendto(fd, buf, len, 0, (struct sockaddr *)&client, clientlen) < 0) {
				perror("sendto");
			}

			++count;
		}
	}

	return count_return(count);
}

#endif /* HAVE_LINUX_IF_PACKET_H */
