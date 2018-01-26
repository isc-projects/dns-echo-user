/*
 * Copyright (C) 2017 Internet Systems Consortium, Inc. ("ISC")
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef __PROCESS_H
#define __PROCESS_H

#define FARM_AFFINITY_FORK		1
#define FARM_AFFINITY_THREAD	2

typedef void *(*handler_fn)(void *);
typedef void  (*cleaner_fn)(int, int, void *);

extern void farm(unsigned int forks, unsigned int threads, handler_fn fn, cleaner_fn cfn, void *data, int flags);

#endif /* __PROCESS_H */
