/*
  Copyright (C) CurlyMo

  This Source Code Form is subject to the terms of the Mozilla Public
  License, v. 2.0. If a copy of the MPL was not distributed with this
  file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#ifndef _EVENT_OPERATOR_H_
#define _EVENT_OPERATOR_H_

#include "rules.h" /* rewrite */

struct event_operator_t {
  const char *name;
  int precedence;
  int associativity;
  int (*callback)(struct rules_t *obj, int a, int b, int *ret);
} __attribute__((packed));

extern struct event_operator_t event_operators[];
extern int nr_event_operators;

#endif
