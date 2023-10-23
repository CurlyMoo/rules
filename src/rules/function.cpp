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

#include "rules.h"
#include "function.h"

#include "functions/max.h"
#include "functions/min.h"
#include "functions/coalesce.h"
#include "functions/round.h"
#include "functions/ceil.h"
#include "functions/floor.h"

struct rule_function_t rule_functions[] = {
  { "max", rule_function_max_callback },
  { "min", rule_function_min_callback },
  { "coalesce", rule_function_coalesce_callback },
  { "round", rule_function_round_callback },
  { "floor", rule_function_floor_callback },
  { "ceil", rule_function_ceil_callback }
};

uint16_t nr_rule_functions = sizeof(rule_functions)/sizeof(rule_functions[0]);
