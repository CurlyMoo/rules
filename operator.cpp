/*
  Copyright (C) CurlyMo

  This Source Code Form is subject to the terms of the Mozilla Public
  License, v. 2.0. If a copy of the MPL was not distributed with this
  file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#ifdef ESP8266
  #pragma GCC diagnostic warning "-fpermissive"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/time.h>
#include <libgen.h>
#include <assert.h>
#include <math.h>

#include "mem.h"
#include "operator.h"

#include "eq.h"
#include "ne.h"
#include "plus.h"
#include "multiply.h"
#include "and.h"
#include "mod.h"
#include "or.h"
#include "divide.h"
#include "ge.h"
#include "gt.h"
#include "lt.h"
#include "le.h"
#include "power.h"
#include "minus.h"

struct rule_operator_t rule_operators[] = {
  { "==", 30, 1, rule_operator_eq_callback },
  { "!=", 30, 1, rule_operator_ne_callback },
  { "+", 60, 1, rule_operator_plus_callback },
  { "-", 60, 1, rule_operator_minus_callback },
  { "*", 70, 1, rule_operator_multiply_callback },
  { "%", 70, 1, rule_operator_mod_callback },
  { "&&", 20, 1, rule_operator_and_callback },
  { "||", 10, 1, rule_operator_or_callback },
  { "/", 70, 1, rule_operator_divide_callback },
  { ">=", 30, 1, rule_operator_ge_callback },
  { "<=", 30, 1, rule_operator_le_callback },
  { "<", 30, 1, rule_operator_lt_callback },
  { ">", 30, 1, rule_operator_gt_callback },
  { "^", 80, 2, rule_operator_power_callback },
};

unsigned int nr_rule_operators = sizeof(rule_operators)/sizeof(rule_operators[0]);