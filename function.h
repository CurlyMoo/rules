/*
  Copyright (C) CurlyMo

  This Source Code Form is subject to the terms of the Mozilla Public
  License, v. 2.0. If a copy of the MPL was not distributed with this
  file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#ifndef _FUNCTION_H_
#define _FUNCTION_H_

#include "rules.h" /* rewrite */

struct event_function_t {
  const char *name;
  int (*callback)(struct rules_t *obj, uint16_t argc, uint16_t *argv, int *ret);
} __attribute__((packed));

extern struct event_function_t event_functions[];
extern int nr_event_functions;

#endif
