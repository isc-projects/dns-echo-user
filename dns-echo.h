/*
 * Copyright (C) 2018 Internet Systems Consortium, Inc. ("ISC")
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef __DNS_ECHO_H
#define __DNS_ECHO_H

#include <signal.h>

extern sig_atomic_t quit;
extern void *count_return(uint64_t count);
extern void make_echo(unsigned char *buf, int len);

#endif /* __DNS_ECHO_H */
