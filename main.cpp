/*
  Copyright (C) CurlyMo

  This Source Code Form is subject to the terms of the Mozilla Public
  License, v. 2.0. If a copy of the MPL was not distributed with this
  file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include "src/common/mem.h"
#include "src/common/strnicmp.h"
#include "src/common/uint32float.h"
#include "src/rules/rules.h"
#include "src/rules/stack.h"

#ifdef ESP8266
#include <Arduino.h>
#endif

#define OUTPUT_SIZE 512

static struct rules_t **rules = NULL;
static uint8_t nrrules = 0;
static char out[OUTPUT_SIZE];

#ifndef ESP8266
struct serial_t Serial;
#endif

struct rule_options_t rule_options;
static unsigned char *mempool = NULL;
static struct vm_vinteger_t vinteger;

struct unittest_t {
  const char *rule;
  struct {
    const char *output;
    uint16_t bytes;
  } validate[3];
  struct {
    const char *output;
    uint16_t bytes;
  } run[3];
} unittests[] = {
  /*
   * Valid rules
   */
  { "if 3 == 3 then $a = 6; end", { { "$a = 6", 92 } }, { { "$a = 6", 92 } } },
  { "if $a == $a then $a = $a; end", { { "$a = NULL", 100 } }, { { "$a = NULL", 100 } } },
  { "if (3 == 3) then $a = 6; end", { { "$a = 6", 100 } }, { { "$a = 6", 100 } } },
  { "if 10 == 10 then $a = 10; end", { { "$a = 10", 92 } }, { { "$a = 10", 92 } } },
  { "if 100 == 100 then $a = 100; end", { { "$a = 100", 92 } }, { { "$a = 100", 92 } } },
  { "if 1000 == 1000 then $a = 1000; end", { { "$a = 1000", 92 } }, { { "$a = 1000", 92 } } },
  { "if 3.5 == 3.5 then $a = 3.5; end", { { "$a = 3.5", 92 } }, { { "$a = 3.5", 92 } } },
  { "if 33.5 == 33.5 then $a = 33.5; end", { { "$a = 33.5", 92 } }, { { "$a = 33.5", 92 } } },
  { "if 333.5 == 333.5 then $a = 333.5; end", { { "$a = 333.5", 92 } }, { { "$a = 333.5", 92 } } },
  { "if 3.335 < 33.35 then $a = 3.335; end", { { "$a = 3.335", 92 } }, { { "$a = 3.335", 92 } } },
  { "if -10 == -10 then $a = -10; end", { { "$a = -10", 92 } }, { { "$a = -10", 92 } } },
  { "if -100 == -100 then $a = -100; end", { { "$a = -100", 92 } }, { { "$a = -100", 92 } } },
  { "if NULL == 3 then $a = 6; end", { { "$a = 6", 88 } }, { { "", 80 } } },
  { "if 1.1 == 1.1 then $a = 6; end", { { "$a = 6", 92 } }, { { "$a = 6", 92 } } },
  { "if 1.1 == 1.2 then $a = 6; end", { { "$a = 6", 92 } }, { { "", 84 } } },
  { "if 4 != 3 then $a = 6; end", { { "$a = 6", 92 } }, { { "$a = 6", 92 } } },
  { "if (4 != 3) then $a = 6; end", { { "$a = 6", 100 } }, { { "$a = 6", 100 } } },
  { "if NULL != 3 then $a = 6; end", { { "$a = 6", 88 } }, { { "$a = 6", 88 } } },
  { "if NULL != NULL then $a = 6; end", { { "$a = 6", 84 } }, { { "$a = 6", 84 } } },
  { "if 1.2 != 1.1 then $a = 6; end", { { "$a = 6", 92 } }, { { "$a = 6", 92 } } },
  { "if 1.2 != 1.2 then $a = 6; end", { { "$a = 6", 92 } }, { { "", 84 } } },
  { "if 1 != 1 then $a = 6; end", { { "$a = 6", 92 } }, { { "", 84 } } },
  { "if 1.1 >= 1.2 then $a = 6; end", { { "$a = 6", 92 } }, { { "", 84 } } },
  { "if 1.1 >= 1.1 then $a = 6; end", { { "$a = 6", 92 } }, { { "$a = 6", 92 } } },
  { "if 2 >= 1.1 then $a = 6; end", { { "$a = 6", 92 } }, { { "$a = 6", 92 } } },
  { "if 2.1 >= 1 then $a = 6; end", { { "$a = 6", 92 } }, { { "$a = 6", 92 } } },
  { "if NULL >= 1.2 then $a = 6; end", { { "$a = 6", 88 } }, { { "", 80 } } },
  { "if NULL >= NULL then $a = 6; end", { { "$a = 6", 84 } }, { { "", 76 } } },
  { "if 1 > 2 then $a = 6; end", { { "$a = 6", 92} }, { { "", 84 } } },
  { "if 2 > 1 then $a = 6; end", { { "$a = 6", 92 } }, { { "$a = 6", 92 } } },
  { "if 2 > 1.1 then $a = 6; end", { { "$a = 6", 92 } }, { { "$a = 6", 92 } } },
  { "if 1.1 > 1.2 then $a = 6; end", { { "$a = 6", 92 } }, { { "", 84 } } },
  { "if 1.1 > 1.0 then $a = 6; end", { { "$a = 6", 96 } }, { { "$a = 6", 96 } } },
  { "if NULL > 1.2 then $a = 6; end", { { "$a = 6", 88 } }, { { "", 80 } } },
  { "if NULL > NULL then $a = 6; end", { { "$a = 6", 84 } }, { { "", 76 } } },
  { "if 1.2 <= 1.1 then $a = 6; end", { { "$a = 6", 92 } }, { { "", 84 } } },
  { "if 1.1 <= 1.1 then $a = 6; end", { { "$a = 6", 92 } }, { { "$a = 6", 92 } } },
  { "if 1.1 <= 2 then $a = 6; end", { { "$a = 6", 92 } }, { { "$a = 6", 92 } } },
  { "if 1 <= 2 then $a = 6; end", { { "$a = 6", 92 } }, { { "$a = 6", 92 } } },
  { "if 2 <= 1 then $a = 6; end", { { "$a = 6", 92 } }, { { "", 84 } } },
  { "if 1 <= 2.1 then $a = 6; end", { { "$a = 6", 92 } }, { { "$a = 6", 92 } } },
  { "if 1.2 <= NULL then $a = 6; end", { { "$a = 6", 88 } }, { { "", 80 } } },
  { "if NULL <= NULL then $a = 6; end", { { "$a = 6", 84 } }, { { "", 76 } } },
  { "if 2 < 1 then $a = 6; end", { { "$a = 6", 92 } }, { { "", 84 } } },
  { "if 1 < 2 then $a = 6; end", { { "$a = 6", 92 } }, { { "$a = 6", 92 } } },
  { "if 1.1 < 2 then $a = 6; end", { { "$a = 6", 92 } }, { { "$a = 6", 92 } } },
  { "if 1.2 < 1.1 then $a = 6; end", { { "$a = 6", 92 } }, { { "", 84 } } },
  { "if 1.0 < 1.1 then $a = 6; end", { { "$a = 6", 96 } }, { { "$a = 6", 96 } } },
  { "if 1.2 < NULL then $a = 6; end", { { "$a = 6", 88 } }, { { "", 80 } } },
  { "if NULL < NULL then $a = 6; end", { { "$a = 6", 84 } }, { { "", 76 } } },
  { "if NULL && NULL then $a = -6; end", { { "$a = -6", 84 } }, { { "", 76 } } },
  { "if 0 && 0 then $a = -6; end", { { "$a = -6", 92 } }, { { "", 84 } } },
  { "if 1 && 1 then $a = -6; end", { { "$a = -6", 92 } }, { { "$a = -6", 92 } } },
  { "if 1.1 && 1.2 then $a = -6; end", { { "$a = -6", 92 } }, { { "$a = -6", 92 } } },
  { "if 0.0 && 1.2 then $a = -6; end", { { "$a = -6", 96 } }, { { "", 88 } } },
  { "if -1.1 && 1.2 then $a = -6; end", { { "$a = -6", 92 } }, { { "$a = -6", 92 } } },
  { "if 1.2 && 0.0 then $a = -6; end", { { "$a = -6", 96 } }, { { "", 88 } } },
  { "if 1.2 && -1.1 then $a = -6; end", { { "$a = -6", 92 } }, { { "$a = -6", 92 } } },
  { "if NULL || NULL then $a = -6; end", { { "$a = -6", 84 } }, { { "", 76 } } },
  { "if 0 || 0 then $a = -6; end", { { "$a = -6", 92 } }, { { "", 84 } } },
  { "if 1 || 1 then $a = -6; end", { { "$a = -6", 92 } }, { { "$a = -6", 92 } } },
  { "if 1.1 || 1.2 then $a = -6; end", { { "$a = -6", 92 } }, { { "$a = -6", 92 } } },
  { "if -0.1 || -0.1 then $a = -6; end", { { "$a = -6", 92 } }, { { "$a = -6", 92 } } },
  { "if 0.0 || 1.2 then $a = -6; end", { { "$a = -6", 96 } }, { { "$a = -6", 96 } } },
  { "if -1.1 || 1.2 then $a = -6; end", { { "$a = -6", 92 } }, { { "$a = -6", 92 } } },
  { "if 1.2 || 0.0 then $a = -6; end", { { "$a = -6", 96 } }, { { "$a = -6", 96 } } },
  { "if 1.2 || -1.1 then $a = -6; end", { { "$a = -6", 92 } }, { { "$a = -6", 92 } } },
  { "if 3 == NULL then $a = 6; end", { { "$a = 6", 88 } }, { { "", 80 } } },
  { "if (NULL == 3) then $a = 6; end", { { "$a = 6", 96 } }, { { "", 88 } } },
  { "if (3 == NULL) then $a = 6; end", { { "$a = 6", 96 } }, { { "", 88 } } },
  { "if @a == 3 then $a = 6; end", { { "$a = 6", 96 } }, { { "", 88 } } },
  { "if foo#bar == 3 then $a = 6; end", { { "$a = 6", 104 } }, { { "$a = 6", 104 } } },
  { "if 3 == 3 then @a = 6; end", { { "@a = 6", 92 } }, { { "@a = 6", 92 } } },
  { "if 3 == 3 then $a = -6; end", { { "$a = -6", 92 } }, { { "$a = -6", 92 } } },
  { "if 4 != 3 then $a = -6; end", { { "$a = -6", 92 } }, { { "$a = -6", 92 } } },
  { "if 3 == 3 then $a = 1.1; end", { { "$a = 1.1", 92 } }, { { "$a = 1.1", 92 } } },
  { "if 3 == 3 then $a = 1 - NULL; end", { { "$a = NULL", 104 } }, { { "$a = NULL", 104 } } },
  { "if 3 == 3 then $a = 1 - 0.5; end", { { "$a = 0.5", 112 } }, { { "$a = 0.5", 112 } } },
  { "if 3 == 3 then $a = 0.5 - 0.5; end", { { "$a = 0", 112 } }, { { "$a = 0", 112 } } },
  { "if 3 == 3 then $a = 0.5 - 1; end", { { "$a = -0.5", 112 } }, { { "$a = -0.5", 112 } } },
  { "if 3 == 3 then $a = 1 + 2; end", { { "$a = 3", 112 } }, { { "$a = 3", 112 } } },
  { "if 3 == 3 then $a = 1.5 + 2.5; end", { { "$a = 4", 112 } }, { { "$a = 4", 112 } } },
  { "if 3 == 3 then $a = 1 * NULL; end", { { "$a = NULL", 104 } }, { { "$a = NULL", 104 } } },
  { "if 3 == 3 then $a = 1 ^ NULL; end", { { "$a = NULL", 104 } }, { { "$a = NULL", 104 } } },
  { "if 3 == 3 then $a = 9 % 2; end", { { "$a = 1", 112 } }, { { "$a = 1", 112 } } },
  { "if 3 == 3 then $a = 9 % NULL; end", { { "$a = NULL", 104 } }, { { "$a = NULL", 104 } } },
  { "if 3 == 3 then $a = 9.5 % 1.5; end", { { "$a = 0.5", 112 } }, { { "$a = 0.5", 112 } } },
  { "if 3 == 3 then $a = 10 % 1.5; end", { { "$a = 1.5", 112 } }, { { "$a = 1.5", 112 } } },
  { "if 3 == 3 then $a = 1.5 % 10; end", { { "$a = 1", 112 } }, { { "$a = 1", 112 } } },
  { "if 1 == 1 then $a = 1 + 2 + 3; end", { { "$a = 6", 132 } }, { { "$a = 6", 132 } } },
  { "if 1 == 1 == 1 then $a = 1; end", { { "$a = 1", 112 } },  { { "$a = 1", 112 } } },
  { "if 1 == 1 == 2 then $a = 1; end", { { "$a = 1", 112 } },  { { "", 104 } } },
  { "if 1 == 1 then $a = 1; $a = $a + 2; end", { { "$a = 3", 140 } }, { { "$a = 3", 140 } } },
  { "if 1 == 1 then $a = 6; $a = $a + 2 + $a / 3; end", { { "$a = 10", 184 } }, { { "$a = 10", 184 } } },
  { "if 1 == 1 then $a = 6; $a = ($a + 2 + $a / 3); end", { { "$a = 10", 192 } }, { { "$a = 10", 192 } } },
  { "if 1 == 1 then $a = NULL / 1; end", { { "$a = NULL", 104 } }, { { "$a = NULL", 104 } } },
  { "if 12 == 1 then $a = 1 + 2 * 3; end", { { "$a = 7", 132 } }, { { "", 124 } } },
  { "if 1 == 1 then $a = 3 * 1 + 2 * 3; end", { { "$a = 9", 152 } }, { { "$a = 9", 152 } } },
  { "if 1 == 1 then $a = (3 * 1 + 2 * 3); end", { { "$a = 9", 160 } }, { { "$a = 9", 160 } } },
  { "if 1 == 1 then $a = 1 * 2 + 3; end", { { "$a = 5", 132 } }, { { "$a = 5", 132 } } },
  { "if 1 == 1 then $a = 1 * 100 ^ 2; end", { { "$a = 10000", 132 } }, { { "$a = 10000", 132 } } },
  { "if 1 == 1 then $a = 1 * 1.1 ^ 2; end", { { "$a = 1.21", 132 } }, { { "$a = 1.21", 132 } } },
  { "if 1 == 1 then $a = 1 * 1.1 ^ 1.1; end", { { "$a = 1.11053", 132 } }, { { "$a = 1.11053", 132 } } },
  { "if 1 == 1 then $a = 1 * 100 ^ 1.1; end", { { "$a = 158.489", 132 } }, { { "$a = 158.489", 132 } } },
  { "if 1 == 1 then $a = 1 + 2 * 3 / 4; end", { { "$a = 2.5", 152 } }, { { "$a = 2.5", 152 } } },
  { "if 1 == 1 then $a = 1 + 2 * 3 / 4 ^ 2; end", { { "$a = 1.375", 172 } }, { { "$a = 1.375", 172 } } },
  { "if 1 == 1 then $a = 1 + 4 ^ 2 ^ 1; end", { { "$a = 17", 152 } }, { { "$a = 17", 152 } } },
  { "if 1 == 1 then $a = (1 + 4 ^ 2 ^ 1); end", { { "$a = 17", 160 } }, { { "$a = 17", 160 } } },
  { "if 1 == 1 then $a = (1 + 2 * 3 / 4 ^ 2); end", { "$a = 1.375", 180 }, { "$a = 1.375", 180 } },
  { "if 1 == 1 then $a = (1 + 2 * 3 / 4 ^ 2 ^ 1 * 3); end", { "$a = 2.125", 220 }, { "$a = 2.125", 220 } },
  { "if 1 == 1 then $a = (1 + 2 * 3 / 4 ^ 2 ^ 1 * 3 ^ 4); end", { "$a = 31.375", 240 }, { "$a = 31.375", 240 } },
  { "if 1 == 1 then $a = 1 + 2 * 3 / 4 ^ 2 ^ 1 * 3 ^ 4; end", { "$a = 31.375", 232 }, { "$a = 31.375", 232 } },
  { "if 1 == 1 then $a = (1 + 2) * 3; end", { "$a = 9", 140 }, { "$a = 9", 140 } },
  { "if 1 == 1 then $a = (1 + 2 * 3); end", { "$a = 7", 140 }, { "$a = 7", 140 } },
  { "if 1 == 1 then $a = 3 * (1 + 2); end", { "$a = 9", 140 }, { "$a = 9", 140 } },
  { "if 1 == 1 then $a = 3 * (1 + 2) * 2; end", { "$a = 18", 160 }, { "$a = 18", 160 } },
  { "if 1 == 1 then $a = 3 * (1 + 3) ^ 2; end", { "$a = 48", 160 }, { "$a = 48", 160 } },
  { "if 1 == 1 then $a = 7 + 6 * 5 / (4 - 3) ^ 2 ^ 1; end", { "$a = 37", 220 }, { "$a = 37", 220 } },
  { "if 1 == 1 then $a = 3 * (1 + 2) + 2; end", { "$a = 11", 160 }, { "$a = 11", 160 } },
  { "if 1 == 1 then $a = 3 + (1 + 2) * 2; end", { "$a = 9", 160 }, { "$a = 9", 160 } },
  { "if 1 == 1 then $a = 3 * (1 + 2 / 2) + 3; end", { "$a = 9", 180 }, { "$a = 9", 180 } },
  { "if 1 == 1 then $a = 3 * ((1 + 2) / 2) + 2; end", { "$a = 6.5", 188 }, { "$a = 6.5", 188 } },
  { "if 1 == 1 then $a = 3 * ((1 + 2) / 2 + 3); end", { "$a = 13.5", 188 }, { "$a = 13.5", 188 } },
  { "if 1 == 1 then $a = 3 * ((1 + 2) / (2 + 3)); end", { "$a = 1.8", 196 }, { "$a = 1.8", 196 } },
  { "if 1 == 1 then $a = 3 * ((((1 + 2) / (2 + 3)))); end", { "$a = 1.8", 212 }, { "$a = 1.8", 212 } },
  { "if $a == $a then $a = $a * (((($a + $a) / ($a + $a)))); end", { "$a = NULL", 236 }, { "$a = NULL", 236 } },
  { "if 1 == 1 then $a = 3 * ((1 + 2) / (2 + 3)) + 2; end", { "$a = 3.8", 216 }, { "$a = 3.8", 216 } },
  { "if 1 == 1 then $a = (1 == 1 && 1 == 0) || 5 >= 4; end", { "$a = 1", 200 }, { "$a = 1", 200 } },
  { "if 1 == 1 then $a = (1 == 1 && 1 == 0) || 3 >= 4; end", { "$a = 0", 200 }, { "$a = 0", 200 } },
  { "if 1 == 1 then $a = 3; end", { "$a = 3", 92 }, { "$a = 3", 92 } },
  { "if 1 == 1 then $a = 3.1; $b = $a; end", { "$a = 3.1$b = 3.1", 128 }, { "$a = 3.1$b = 3.1", 128 } },
  { "if 1 == 1 then $a = $a + 1; end", { "$a = NULL", 112 }, { "$a = NULL", 112 } },
  { "if 1 == 1 then $a = max(foo#bar); end", { "$a = 3", 116 }, { "$a = 3", 116 } },
  { "if 1 == 1 then $a = coalesce($a, 0) + 1; end", { "$a = 1", 140 }, { "$a = 1", 140 } },
  { "if 1 == 1 then $a = coalesce($a, 1.1) + 1; end", { "$a = 2.1", 140 }, { "$a = 2.1", 140 } },
  { "if 1 == 1 then if $a == NULL then $a = 0; end $a = $a + 1; end", { "$a = 1", 192 }, { "$a = 1", 192 } },
  { "if 1 == 1 then $a = 1; if $a == NULL then $a = 0; end $a = $a + 1; end", { "$a = 1", 212 }, { "$a = 2", 212 } },
  { "if 1 == 1 then $a = 1; if $a == 1 then $a = NULL; end end", { "$a = NULL", 164 }, { "$a = NULL", 164 } },
  { "if 1 == 1 then $a = 1; $a = NULL; end", { "$a = NULL", 108 }, { "$a = NULL", 108 } },
  { "if 1 == 2 then $a = 3; else $a = 4; end", { "$a = 4", 124 }, { "$a = 4", 124 } },
  { "if 1 == 1 then $a = 3; else $a = 4; end", { "$a = 4", 124 }, { "$a = 3", 124 } },
  { "if (1 + 1) == 1 then $a = 3; else $a = 4; end", { "$a = 4", 152 }, { "$a = 4", 152 } },
  { "if 1 == 2 || 3 >= 4 then $a = max(1, 2); end", { "$a = 2", 156 }, { "", 148 } },
  { "if 1 == 2 || 3 >= 4 then $a = min(3, 1, 2); end", { "$a = 1", 168 }, { "", 160 } },
  { "if 1 == 2 || 3 >= 4 then $a = 1; end", { "$a = 1", 132 }, { "", 124 } },
  { "if 1 == 2 || 3 >= 4 || 5 == 6 then $a = max(1, 3, 2); else $b = 9; end", { "$a = 3$b = 9", 248 }, { "$b = 9", 240 } },
  { "if 1 == 1 then $a = 1; $b = 2; end", { "$a = 1$b = 2", 124 }, { "$a = 1$b = 2", 124 } },
  { "if 1 == 1 then $a = 1; $b = ($a + 3) * 3; end", { "$a = 1$b = 12", 176 }, { "$a = 1$b = 12", 176 } },
  { "if 1 == 1 then $a = 1; $b = $a + 3 * 3; end", { "$a = 1$b = 10", 168 }, { "$a = 1$b = 10", 168 } },
  { "if 1 == 1 then if 5 == 6 then $a = 1; end $a = 2; end", { "$a = 2", 168 }, { "$a = 2", 168 } },
  { "if 1 == 1 then if 5 == 6 then $a = 1; end if 1 == 3 then $b = 3; end $a = 2; end", { "$b = 3$a = 2", 248 }, { "$a = 2", 240 } },
  { "if 1 == 1 then $a = 1; $b = $a; end", { "$a = 1$b = 1", 128 }, { "$a = 1$b = 1", 128 } },
  { "if 1 == 1 then $a = 1; if 5 >= 4 then $a = 3; end $b = $a; end", { "$a = 3$b = 3", 200 }, { "$a = 3$b = 3", 200 } },
  { "if 1 == 1 then $a = 1; if $a == 1 then $a = 3; end end", { "$a = 3", 172 }, { "$a = 3", 172 } },
  { "if (1 == 1 && 1 == 0) || 5 >= 4 then $a = 1; end", { "$a = 1", 180 }, { "$a = 1", 180 } },
  { "if 3 == 3 then $a = max(1); end", { "$a = 1", 104 }, { "$a = 1", 104 } },
  { "if 3 == 3 then $a = max(NULL, 1); end", { "$a = 1", 112 }, { "$a = 1", 112 } },
  { "if 3 == 3 then $a = max(1, NULL); end", { "$a = 1", 112 }, { "$a = 1", 112 } },
  { "if 3 == 3 then $b = 2; $a = max($b, 1); end", { "$b = 2$a = 2", 152 }, { "$b = 2$a = 2", 152 } },
  { "if 3 == 3 then $a = max(1, 2); end", { "$a = 2", 116 }, { "$a = 2", 116 } },
  { "if 3 == 3 then $a = max(1, 2, 3, 4); end", { "$a = 4", 140 }, { "$a = 4", 140 } },
  { "if 3 == 3 then $a = max(1, 4, 5, 3, 2); end", { "$a = 5", 152 }, { "$a = 5", 152 } },
  { "if 3 == 3 then $a = max(max(1, 4), 2); end", { "$a = 4", 140 }, { "$a = 4", 140 } },
  { "if 3 == 3 then $a = max(1, 2) * 3; end", { "$a = 6", 136 }, { "$a = 6", 136 } },
  { "if 3 == 3 then $a = max(1, 2) * max(3, 4); end", { "$a = 8", 160 }, { "$a = 8", 160 } },
  { "if 3 == 3 then $a = max(max(1, 2), (1 * max(1, 3) ^ 2)); end", { "$a = 9", 212 }, { "$a = 9", 212 } },
  { "if 3 == 3 then $b = 1; $a = max($b + 1); end", { "$b = 1$a = 2", 160 }, { "$b = 1$a = 2", 160 } },
  { "if 3 == 3 then $b = 1; $a = max($b + 1) * 3; end", { "$b = 1$a = 6", 180 }, { "$b = 1$a = 6", 180 } },
  { "if max(1, 3) == 3 then $a = 1; end", { "$a = 1", 116 }, { "$a = 1", 116 } },
  { "if max(1, 3) == max(1, 3) then $a = 1; end", { "$a = 1", 140 }, { "$a = 1", 140 } },
  { "if 3 == 3 then max(1, 2); end", { "", 96 }, { "", 96 } },
  { "if 3 == 3 then $a = max(1 + 1, 2 + 2); end", { "$a = 4", 156 }, { "$a = 4", 156 } },
  { "if 1 == 1 then $a = coalesce($b, 0); end  ", { { "$b = NULL$a = 0", 124 } },  { { "$b = NULL$a = 0", 124 } } }, // FIXME
  { "if 3 == 3 then $a = max((1 + 3), 2); end", { "$a = 4", 144 }, { "$a = 4", 144 } },
  { "if 3 == 3 then $a = max((1 + (3 * 3)), 2); end", { "$a = 10", 172 }, { "$a = 10", 172 } },
  { "if 3 == 3 then $a = max(1 + 3 * 3, 3 * 4); end", { "$a = 12", 176 }, { "$a = 12", 176 } },
  { "if 3 == 3 then $a = max(((2 + 3) * 3), (3 * 4)); end", { "$a = 15", 200 }, { "$a = 15", 200 } },
  { "if 3 == 3 then $a = (max(1, 2) + 2) * 3; end", { "$a = 12", 164 }, { "$a = 12", 164 } },
  { "if 1 == 1 then $a = max(0, 1) + max(1, 2) * max(2, 3) / max(3, 4) ^ max(1, 2) ^ max(0, 1) * max(2, 3) ^ max(3, 4); end", { "$a = 31.375", 424 }, { "$a = 31.375", 424 } },
  { "if 3 == 3 then $a = 2; $b = max((($a + 3) * 3), (3 * 4)); end", { "$a = 2$b = 15", 236 }, { "$a = 2$b = 15", 236 } },
  { "if 3 == 3 then $a = 2; $b = max((max($a + 3) * 3), (3 * 4)); end", { "$a = 2$b = 15", 240 }, { "$a = 2$b = 15", 240 } },
  { "if 1 == 1 then $a = 1; else $a = 2; end", { "$a = 2", 124 }, { "$a = 1", 124 } },
  { "if 1 == 2 then $a = 1; elseif 2 == 2 then $a = 3; else $a = 2; end", { "$a = 2", 208 }, { "$a = 3", 208 } },
  { "if 1 == 1 then if 2 == 2 then $a = 1; end else $a = 2; end", { "$a = 2", 176 }, { "$a = 1", 176 } },
  { "if (1 == 1) || 5 >= 4 then $a = 1; if 6 == 5 then $a = 2; end $a = $a + 3; $b = (3 + $a) * 2; else $a = 7; end", { "$b = 16$a = 7", 376 }, { "$a = 4$b = 14", 376 } },
  { "if (1 == 1) || 5 >= 4 then $a = 1; if 6 == 5 then $a = 2; end $a = $a + 3; $b = (3 + $a) * 2; $b = 3; else $a = 7; end", { "$b = 3$a = 7", 396 }, { "$a = 4$b = 3", 396 } },
  { "if (1 == 1 && 1 == 0) || 5 >= 4 then $a = 1; if 6 == 5 then $a = 2; end $a = $a + 3; $b = (3 + $a * 5 + 3 * 1) * 2; @c = 5; else if 2 == 2 then $a = 6; else $a = 7; end end", { "$b = 62@c = 5$a = 7", 588 }, { "$a = 4$b = 52@c = 5", 588 } },
  { "if 3 == 3 then $a = 6; end if 3 == 3 then $b = 3; end  ", { { "$a = 6", 92 }, { "$b = 3", 92 } },  { { "$a = 6", 92 }, { "$b = 3" , 92 } } },
  { "   if 3 == 3 then $a = 6; end    if 3 == 3 then $b = 6; end               if 3 == 3 then $c = 6; end", { { "$a = 6", 92 }, { "$b = 6", 92 }, { "$c = 6", 92 } }, { { "$a = 6", 92 }, { "$b = 6", 92 }, { "$c = 6", 92 } } },
  { "on foo then max(1, 2); end", { "", 72 }, { "", 72 } },
  { "on foo then $a = 6; end", { "$a = 6", 68 }, { "$a = 6", 68 } },
  { "on foo then $a = 6; $b = 3; end", { "$a = 6$b = 3", 100 }, { "$a = 6$b = 3", 100 } },
  { "on foo then @a = 6; end", { { "@a = 6", 68 } }, { { "@a = 6", 68 } } },
  { "on foo then $a = 1 + 2; end", { { "$a = 3", 88 } }, { { "$a = 3", 88 } } },
  { "on foo then if 5 == 6 then $a = 1; end $a = 2; end", { "$a = 2", 144 }, { "$a = 2", 144 } },
  { "on foo then if 5 == 6 then $a = 1; end if 1 == 3 then $b = 3; end $a = 2; end", { "$b = 3$a = 2", 224 }, { "$a = 2", 216 } },
  { "on foo then bar(); end  ", { { "", 52 } },  { { "", 52 } } },
  { "on foo then $a = 6; end if 3 == 3 then $b = 3; end  ", { { "$a = 6", 68 }, { "$b = 3" , 92 } },  { { "$a = 6", 68 }, { "$b = 3" , 92 } } },
  { "on foo then $a = 6; end if 3 == 3 then foo(); $b = 3; end  ", { { "$a = 6", 68 }, { "$b = 3", 108 } },  { { "$a = 6", 68 }, { "$b = 3", 108 } } },
  { "on foo then $a = coalesce($b, 0); end  ", { { "$b = NULL$a = 0", 100 } },  { { "$b = NULL$a = 0", 100 } } }, // FIXME

  /*
   * Invalid rules
   */
  { "", { { NULL, 0 } },  { { NULL, 0 } } },
  { "foo", { { NULL, 0 } },  { { NULL, 0 } } },
  { "foo(", { { NULL, 0 } },  { { NULL, 0 } } },
  { "1 == 1", { { NULL, 0 } },  { { NULL, 0 } } },
  { "on foo do", { { NULL, 0 } },  { { NULL, 0 } } },
  { "if foo then", { { NULL, 0 } },  { { NULL, 0 } } },
  { "if 1.1.1 == 1 then", { { NULL, 0 } },  { { NULL, 0 } } },
  { "if 1 == 1 then end", { { NULL, 0 } },  { { NULL, 0 } } },
  { "if 1 == ) then $a = 1 end", { { NULL, 0 } },  { { NULL, 0 } } },
  { "if 1 == 1 then $a = 1 end", { { NULL, 0 } },  { { NULL, 0 } } },
  { "if (1 == 1 then $a = 1; end", { { NULL, 0 } },  { { NULL, 0 } } },
  { "if 1 == 1) then $a = 1; end", { { NULL, 0 } },  { { NULL, 0 } } },
  { "if () then $a = 1; end", { { NULL, 0 } },  { { NULL, 0 } } },
  { "if ( == ) then $a = 1; end", { { NULL, 0 } },  { { NULL, 0 } } },
  { "if 1 2 then $a = 1; end", { { NULL, 0 } },  { { NULL, 0 } } },
  { "on foo end", { { NULL, 0 } },  { { NULL, 0 } } },
  { "on foo then max(1, 2) end", { { NULL, 0 } },  { { NULL, 0 } } },
  { "if max(1, 2); max(1, 2); then $a = 1; end", { { NULL, 0 } },  { { NULL, 0 } } },
  { "if 1 == 1 then max(1, 2) end", { { NULL, 0 } },  { { NULL, 0 } } },
  { "on foo then $a = 1; max(1, 2) end", { { NULL, 0 } },  { { NULL, 0 } } },
  { "if 1 == 1 then $a = 1; max(1, 2) end", { { NULL, 0 } },  { { NULL, 0 } } },
  { "on foo then max(1, 2) == 1; end", { { NULL, 0 } },  { { NULL, 0 } } },
  { "if 1 == 1 then max(1, 2) == 1; end", { { NULL, 0 } },  { { NULL, 0 } } },
  { "if if == 1 then $a == 1; end", { { NULL, 0 } },  { { NULL, 0 } } },
  { "on foo then 1; end", { { NULL, 0 } },  { { NULL, 0 } } },
  { "if 1 == 1 then 1; end", { { NULL, 0 } },  { { NULL, 0 } } },
  { "if on foo then $a = 1; end", { { NULL, 0 } },  { { NULL, 0 } } },
  { "on foo then on foo then $a = 1; end end", { { NULL, 0 } },  { { NULL, 0 } } },
  { "on if 1 == 1 then $a = 1; end", { { NULL, 0 } },  { { NULL, 0 } } },
  { "if max == 1 then $a = 1; end", { { NULL, 0 } },  { { NULL, 0 } } },
  { "if 1 == 1 then $a = 1; foo() end", { { NULL, 0 } },  { { NULL, 0 } } },
  { "if 1 == 1 then max(1, 2 end", { { NULL, 0 } },  { { NULL, 0 } } },
  { "if 1 == 1 then max(1, == ); end", { { NULL, 0 } },  { { NULL, 0 } } },
  { "if (1 + 1) 1 then $a = 3; end", { { NULL, 0 } },  { { NULL, 0 } } },
  { "if 1 == 1 then NULL; end", { { NULL, 0 } },  { { NULL, 0 } } },
  { "if 1 == 1 then $a = max(1, 2)NULL; end", { { NULL, 0 } },  { { NULL, 0 } } },
  { "if 1 == 1 then $a = ); end", { { NULL, 0 } },  { { NULL, 0 } } },
  { "if 1 == 1 then $a = $a $a; end", { { NULL, 0 } },  { { NULL, 0 } } },
  { "on foo then bar() $a = 1; end", { { NULL, 0 } },  { { NULL, 0 } } },
  { "if ( ; == 1 ) then $a = 1; end", { { NULL, 0 } },  { { NULL, 0 } } },
};

static int8_t is_variable(char *text, uint16_t size) {
  uint16_t i = 1;

  if(size == 7 && strncmp(text, "foo#bar", 7) == 0) {
    return 7;
  } else if(text[0] == '$' || text[0] == '@') {
    while(isalpha(text[i])) {
      i++;
    }
    return i;
  }
  return -1;
}

static int8_t is_event(char *text, uint16_t *pos, uint16_t size) {
  uint16_t len = 0;

  if(size == 3 &&
    (strnicmp(&text[*pos], "foo", len) == 0 || strnicmp(&text[*pos], "bar", len) == 0)) {
    return 0;
  }
  return -1;
}

static unsigned char *vm_value_get(struct rules_t *obj, uint16_t token) {
  struct rule_stack_t *varstack = (struct rule_stack_t *)obj->userdata;
  struct vm_tvar_t *var = (struct vm_tvar_t *)&obj->ast.buffer[token];
  uint16_t ret = 0;
  uint8_t match = 1, is_mmu = 0;

#ifndef NON32XFER_HANDLER
  if((void *)obj >= (void *)MMU_SEC_HEAP) {
    is_mmu = 1;
  } else {
#endif
    is_mmu = 0;
#ifndef NON32XFER_HANDLER
  }
#endif

  if(is_mmu == 1) {
    uint16_t a = 0, b = 0;
    char str[] = "foo#bar";

    ret = mmu_get_uint16(&var->value);

    while(
      str[a] != 0 && mmu_get_uint8(&var->token[b]) != 0 &&
      str[a] == mmu_get_uint8(&var->token[b])) {
      a++; b++;
    }
    if(str[a] != 0 || mmu_get_uint8(&var->token[b]) != 0) { match = 0; }
    if(match == 0) {
      if(mmu_get_uint8(&var->token[0]) == '@') {
        match = 2;
      } else if(mmu_get_uint16(&var->value) == 0) {
        match = 3;
      }
    }
  } else {
    ret = var->value;
    if(strcmp((char *)var->token, "foo#bar") == 0) {
      match = 1;
    } else if(var->token[0] == '@') {
      match = 2;
    } else if(var->value == 0) {
      match = 3;
    } else {
      match = 0;
    }
  }

  if(match == 1) {
    memset(&vinteger, 0, sizeof(struct vm_vinteger_t));
    vinteger.type = VINTEGER;
    vinteger.value = 3;
    return (unsigned char *)&vinteger;
  } else if(match == 2) {
    memset(&vinteger, 0, sizeof(struct vm_vinteger_t));
    vinteger.type = VINTEGER;
    vinteger.value = 5;
    return (unsigned char *)&vinteger;
  } else {
    if(match == 3) {
      ret = varstack->nrbytes;
      uint16_t size = varstack->nrbytes+sizeof(struct vm_vnull_t);
      if((varstack->buffer = (unsigned char *)REALLOC(varstack->buffer, size)) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      struct vm_vnull_t *value = (struct vm_vnull_t *)&varstack->buffer[ret];
      value->type = VNULL;
      value->ret = token;
      if(is_mmu == 1) {
        mmu_set_uint16(&var->value, ret);
      } else {
        var->value = ret;
      }
      varstack->nrbytes = size;
      varstack->bufsize = size;
    }

    return &varstack->buffer[ret];
  }
  return NULL;
}

static void vm_value_cpy(struct rules_t *obj, uint16_t token) {
  struct rule_stack_t *varstack = (struct rule_stack_t *)obj->userdata;
  struct vm_tvar_t *var = (struct vm_tvar_t *)&obj->ast.buffer[token];
  uint16_t x = 0;
  uint8_t is_mmu = 0;

#ifndef NON32XFER_HANDLER
  if((void *)obj >= (void *)MMU_SEC_HEAP) {
    is_mmu = 1;
  } else {
#endif
    is_mmu = 0;
#ifndef NON32XFER_HANDLER
  }
#endif

  for(x=4;x<varstack->nrbytes;x++) {
#ifdef DEBUG
    printf(". %s %d %d\n", __FUNCTION__, __LINE__, x);
#endif
    struct vm_tgeneric_t *val = (struct vm_tgeneric_t *)&varstack->buffer[x];
    struct vm_tvar_t *foo = (struct vm_tvar_t *)&obj->ast.buffer[val->ret];

    uint8_t match = 1;
    if(is_mmu == 1) {
      uint16_t a = 0, b = 0;
      while(
        mmu_get_uint8(&foo->token[a]) != 0 && mmu_get_uint8(&var->token[b]) != 0 &&
        mmu_get_uint8(&foo->token[a]) == mmu_get_uint8(&var->token[b])) {
        a++; b++;
      }
      if(mmu_get_uint8(&foo->token[a]) != 0 || mmu_get_uint8(&var->token[b]) != 0) { match = 0; };
    } else {
      if(strcmp((char *)foo->token, (char *)var->token) != 0) { match = 0; };
    }

    if(match == 1 && val->ret != token) {
      if(is_mmu == 1) {
        mmu_set_uint16(&var->value, mmu_get_uint16(&foo->value));
        mmu_set_uint16(&foo->value, 0);
      } else {
        var->value = foo->value;
        foo->value = 0;
      }
      val->ret = token;
#ifdef DEBUG
      switch(varstack->buffer[x]) {
        case VINTEGER: {
          struct vm_vinteger_t *val = (struct vm_vinteger_t *)&varstack->buffer[x];
          printf(". %s %d %d %d\n", __FUNCTION__, __LINE__, x, (int)val->value);
        } break;
        case VFLOAT: {
          struct vm_vfloat_t *val = (struct vm_vfloat_t *)&varstack->buffer[x];
          printf(". %s %d %d %d\n", __FUNCTION__, __LINE__, x, (double)val->value);
        } break;
        case VNULL: {
          printf(". %s %d %d NULL\n", __FUNCTION__, __LINE__, x);
        } break;
      }
#endif
      return;
    }
    switch(varstack->buffer[x]) {
      case VINTEGER: {
        x += sizeof(struct vm_vinteger_t)-1;
      } break;
      case VFLOAT: {
        x += sizeof(struct vm_vfloat_t)-1;
      } break;
      case VNULL: {
        x += sizeof(struct vm_vnull_t)-1;
      } break;
      default: {
        printf("err: %s %d\n", __FUNCTION__, __LINE__);
        exit(-1);
      } break;
    }
  }
}

static int8_t vm_value_del(struct rules_t *obj, uint16_t idx) {
  struct rule_stack_t *varstack = (struct rule_stack_t *)obj->userdata;
  uint16_t x = 0, ret = 0;
  uint8_t is_mmu = 0;

#ifndef NON32XFER_HANDLER
  if((void *)obj >= (void *)MMU_SEC_HEAP) {
    is_mmu = 1;
  } else {
#endif
    is_mmu = 0;
#ifndef NON32XFER_HANDLER
  }
#endif

  if(idx >= varstack->nrbytes) {
    return -1;
  }

#ifdef DEBUG
  printf(". %s %d %d\n", __FUNCTION__, __LINE__, idx);
#endif

  switch(varstack->buffer[idx]) {
    case VINTEGER: {
      ret = sizeof(struct vm_vinteger_t);
      memmove(&varstack->buffer[idx], &varstack->buffer[idx+ret], varstack->nrbytes-idx-ret);
      if((varstack->buffer = (unsigned char *)REALLOC(varstack->buffer, varstack->nrbytes-ret)) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      varstack->nrbytes -= ret;
      varstack->bufsize = varstack->nrbytes-ret;
    } break;
    case VFLOAT: {
      ret = sizeof(struct vm_vfloat_t);
      memmove(&varstack->buffer[idx], &varstack->buffer[idx+ret], varstack->nrbytes-idx-ret);
      if((varstack->buffer = (unsigned char *)REALLOC(varstack->buffer, varstack->nrbytes-ret)) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      varstack->nrbytes -= ret;
      varstack->bufsize = varstack->nrbytes-ret;
    } break;
    case VNULL: {
      ret = sizeof(struct vm_vnull_t);
      memmove(&varstack->buffer[idx], &varstack->buffer[idx+ret], varstack->nrbytes-idx-ret);
      if((varstack->buffer = (unsigned char *)REALLOC(varstack->buffer, varstack->nrbytes-ret)) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      varstack->nrbytes -= ret;
      varstack->bufsize = varstack->nrbytes-ret;
    } break;
    default: {
      printf("err: %s %d\n", __FUNCTION__, __LINE__);
      exit(-1);
    } break;
  }

  /*
   * Values are linked back to their root node,
   * by their absolute position in the bytecode.
   * If a value is deleted, these positions changes,
   * so we need to update all nodes.
   */
  for(x=idx;x<varstack->nrbytes;x++) {
#ifdef DEBUG
    printf(". %s %d %d\n", __FUNCTION__, __LINE__, x);
#endif
    switch(varstack->buffer[x]) {
      case VINTEGER: {
        struct vm_vinteger_t *node = (struct vm_vinteger_t *)&varstack->buffer[x];
        if(node->ret > 0) {
          struct vm_tvar_t *tmp = (struct vm_tvar_t *)&obj->ast.buffer[node->ret];
          if(is_mmu == 1) {
            mmu_set_uint16(&tmp->value, x);
          } else {
            tmp->value = x;
          }
        }
        x += sizeof(struct vm_vinteger_t)-1;
      } break;
      case VFLOAT: {
        struct vm_vfloat_t *node = (struct vm_vfloat_t *)&varstack->buffer[x];
        if(node->ret > 0) {
          struct vm_tvar_t *tmp = (struct vm_tvar_t *)&obj->ast.buffer[node->ret];
          if(is_mmu == 1) {
            mmu_set_uint16(&tmp->value, x);
          } else {
            tmp->value = x;
          }
        }
        x += sizeof(struct vm_vfloat_t)-1;
      } break;
      case VNULL: {
        struct vm_vnull_t *node = (struct vm_vnull_t *)&varstack->buffer[x];
        if(node->ret > 0) {
          struct vm_tvar_t *tmp = (struct vm_tvar_t *)&obj->ast.buffer[node->ret];
          if(is_mmu == 1) {
            mmu_set_uint16(&tmp->value, x);
          } else {
            tmp->value = x;
          }
        }
        x += sizeof(struct vm_vnull_t)-1;
      } break;
      default: {
        printf("err: %s %d\n", __FUNCTION__, __LINE__);
        exit(-1);
      } break;
    }
  }

  return ret;
}

static void vm_value_set(struct rules_t *obj, uint16_t token, uint16_t val) {
  struct rule_stack_t *varstack = (struct rule_stack_t *)obj->userdata;
  struct vm_tvar_t *var = (struct vm_tvar_t *)&obj->ast.buffer[token];
  uint16_t ret = 0;
  uint8_t is_mmu = 0;

#ifndef NON32XFER_HANDLER
  if((void *)obj >= (void *)MMU_SEC_HEAP) {
    is_mmu = 1;
  } else {
#endif
    is_mmu = 0;
#ifndef NON32XFER_HANDLER
  }
#endif

  var = (struct vm_tvar_t *)&obj->ast.buffer[token];

  // if(is_mmu == 1) {
    // if(mmu_get_uint16(&var->value) > 0) {
      // vm_value_del(obj, mmu_get_uint16(&var->value));
    // }
  // } else {
    // if(var->value > 0) {
      // vm_value_del(obj, var->value);
    // }
  // }

  var = (struct vm_tvar_t *)&obj->ast.buffer[token];

  ret = varstack->nrbytes;

  if(is_mmu == 1) {
    mmu_set_uint16(&var->value, ret);
  } else {
    var->value = ret;
  }

#ifdef DEBUG
  printf(". %s %d %d\n", __FUNCTION__, __LINE__, val);
#endif
  uint8_t valtype = 0;
  if(is_mmu == 1) {
    valtype = mmu_get_uint8(&obj->varstack.buffer[val]);
  } else {
    valtype = obj->varstack.buffer[val];
  }
  switch(valtype) {
    case VINTEGER: {
      uint16_t size = varstack->nrbytes+sizeof(struct vm_vinteger_t);
      if((varstack->buffer = (unsigned char *)REALLOC(varstack->buffer, size)) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      struct vm_vinteger_t *cpy = (struct vm_vinteger_t *)&obj->varstack.buffer[val];
      struct vm_vinteger_t *value = (struct vm_vinteger_t *)&varstack->buffer[ret];
      value->type = VINTEGER;
      value->ret = token;
      if(is_mmu == 1) {
        value->value = cpy->value;
      } else {
        value->value = (int)cpy->value;
      }

#ifdef DEBUG
      printf(". %s %d %d %d\n", __FUNCTION__, __LINE__, varstack->nrbytes, (int)cpy->value);
#endif
      varstack->nrbytes += sizeof(struct vm_vinteger_t);
      varstack->bufsize = size;
    } break;
    case VFLOAT: {
      uint16_t size = varstack->nrbytes + sizeof(struct vm_vfloat_t);
      if((varstack->buffer = (unsigned char *)REALLOC(varstack->buffer, size)) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      struct vm_vfloat_t *cpy = (struct vm_vfloat_t *)&obj->varstack.buffer[val];
      struct vm_vfloat_t *value = (struct vm_vfloat_t *)&varstack->buffer[ret];
      value->type = VFLOAT;
      value->ret = token;
      value->value = cpy->value;

#ifdef DEBUG
      float v = 0;
      uint322float(cpy->value, &v);
      printf(". %s %d %d %g\n", __FUNCTION__, __LINE__, varstack->nrbytes, v);
#endif
      varstack->nrbytes += sizeof(struct vm_vfloat_t);
      varstack->bufsize = size;
    } break;
    case VNULL: {
      uint16_t size = varstack->nrbytes + sizeof(struct vm_vnull_t);
      if((varstack->buffer = (unsigned char *)REALLOC(varstack->buffer, size)) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      struct vm_vnull_t *value = (struct vm_vnull_t *)&varstack->buffer[ret];
      value->type = VNULL;
      value->ret = token;

#ifdef DEBUG
      printf(". %s %d %d NULL\n", __FUNCTION__, __LINE__, varstack->nrbytes);
#endif
      varstack->nrbytes += sizeof(struct vm_vnull_t);
      varstack->bufsize = size;
    } break;
    default: {
      printf("err: %s %d\n", __FUNCTION__, __LINE__);
      exit(-1);
    } break;
  }
}

static void vm_value_prt(struct rules_t *obj, char *out, uint16_t size) {
  struct rule_stack_t *varstack = (struct rule_stack_t *)obj->userdata;
  uint16_t x = 0, pos = 0;
  uint8_t is_mmu = 1;

#ifndef NON32XFER_HANDLER
  if((void *)obj >= (void *)MMU_SEC_HEAP) {
    is_mmu = 1;
  } else {
#endif
    is_mmu = 0;
#ifndef NON32XFER_HANDLER
  }
#endif

  for(x=4;x<varstack->nrbytes;x++) {
    uint16_t val_ret = 0;
    uint8_t type_ret = 0;
    switch(varstack->buffer[x]) {
      case VINTEGER: {
        struct vm_vinteger_t *val = (struct vm_vinteger_t *)&varstack->buffer[x];
        if(is_mmu == 1) {
          val_ret = mmu_get_uint16(&val->ret);
          type_ret = mmu_get_uint8(&obj->ast.buffer[val_ret]);
        } else {
          val_ret = val->ret;
          type_ret = obj->ast.buffer[val_ret];
        }
        switch(type_ret) {
          case TVAR: {
            struct vm_tvar_t *node = (struct vm_tvar_t *)&obj->ast.buffer[val_ret];
            if(is_mmu == 1) {
              uint16_t i = 0;
              while(mmu_get_uint8(&node->token[i]) != 0) {
                pos += snprintf(&out[pos], size - pos, "%c", mmu_get_uint8(&node->token[i++]));
              }
              pos += snprintf(&out[pos], size - pos, " = %d", (int)val->value);
            } else {
              pos += snprintf(&out[pos], size - pos, "%s = %d", node->token, (int)val->value);
            }
          } break;
          default: {
            // printf("err: %s %d %d\n", __FUNCTION__, __LINE__, obj->ast.buffer[val->ret]);
            // exit(-1);
          } break;
        }
        x += sizeof(struct vm_vinteger_t)-1;
      } break;
      case VFLOAT: {
        struct vm_vfloat_t *val = (struct vm_vfloat_t *)&varstack->buffer[x];
        float a = 0;
        uint322float(val->value, &a);

        if(is_mmu == 1) {
          val_ret = mmu_get_uint16(&val->ret);
          type_ret = mmu_get_uint8(&obj->ast.buffer[val_ret]);
        } else {
          val_ret = val->ret;
          type_ret = obj->ast.buffer[val_ret];
        }
        switch(type_ret) {
          case TVAR: {
            struct vm_tvar_t *node = (struct vm_tvar_t *)&obj->ast.buffer[val_ret];
            if(is_mmu == 1) {
              uint16_t i = 0;
              while(mmu_get_uint8(&node->token[i]) != 0) {
                pos += snprintf(&out[pos], size - pos, "%c", mmu_get_uint8(&node->token[i++]));
              }
              pos += snprintf(&out[pos], size - pos, " = %g", a);
            } else {
              pos += snprintf(&out[pos], size - pos, "%s = %g", node->token, a);
            }
          } break;
          default: {
            // printf("err: %s %d %d\n", __FUNCTION__, __LINE__, obj->ast.buffer[val->ret]);
            // exit(-1);
          } break;
        }
        x += sizeof(struct vm_vfloat_t)-1;
      } break;
      case VNULL: {
        struct vm_vnull_t *val = (struct vm_vnull_t *)&varstack->buffer[x];
        if(is_mmu == 1) {
          val_ret = mmu_get_uint16(&val->ret);
          type_ret = mmu_get_uint8(&obj->ast.buffer[val_ret]);
        } else {
          val_ret = val->ret;
          type_ret = obj->ast.buffer[val_ret];
        }
        switch(type_ret) {
          case TVAR: {
            struct vm_tvar_t *node = (struct vm_tvar_t *)&obj->ast.buffer[val_ret];
            if(is_mmu == 1) {
              uint16_t i = 0;
              while(mmu_get_uint8(&node->token[i]) != 0) {
                pos += snprintf(&out[pos], size - pos, "%c", mmu_get_uint8(&node->token[i++]));
              }
              pos += snprintf(&out[pos], size - pos, " = NULL");
            } else {
              pos += snprintf(&out[pos], size - pos, "%s = NULL", node->token);
            }
          } break;
          default: {
            // printf("err: %s %d %d\n", __FUNCTION__, __LINE__, obj->ast.buffer[val->ret]);
            // exit(-1);
          } break;
        }
        x += sizeof(struct vm_vnull_t)-1;
      } break;
      default: {
        printf("err: %s %d\n", __FUNCTION__, __LINE__);
        exit(-1);
      } break;
    }
  }
}

static int event_cb(struct rules_t *obj, char *name) {
  struct rules_t *called = NULL;
  uint16_t caller = 0;
  uint8_t is_mmu = 0;

#ifndef NON32XFER_HANDLER
  if((void *)obj >= (void *)MMU_SEC_HEAP) {
    is_mmu = 1;
  } else {
#endif
    is_mmu = 0;
#ifndef NON32XFER_HANDLER
  }
#endif

  if(is_mmu == 1) {
    caller = mmu_get_uint8(&obj->caller);
  } else {
    caller = obj->caller;
  }

  if(caller > 0 && name == NULL) {
    called = rules[caller-1];

    char str[OUTPUT_SIZE];
#ifdef ESP8266
    /*LCOV_EXCL_START*/
    memset(&str, 0, OUTPUT_SIZE);
    if(is_mmu == 1) {
      snprintf((char *)&str, OUTPUT_SIZE, "- continuing with caller #%d at step #%d", caller, mmu_get_uint16(&called->cont.go));
    } else {
      snprintf((char *)&str, OUTPUT_SIZE, "- continuing with caller #%d at step #%d", caller, called->cont.go);
    }
    Serial.println(str);
    /*LCOV_EXCL_STOP*/
#else
    printf("- continuing with caller #%d at step #%d\n", caller, called->cont.go);
#endif

    if(is_mmu == 1) {
      mmu_set_uint8(&obj->caller, 0);
    } else {
      obj->caller = 0;
    }

    return rule_run(called, 0);
  } else {
    if(strcmp(name, "bar") == 0) {
      return rule_run(obj, 0);
    }
    int i = 0;

    for(i=0;i<nrrules;i++) {
      if(rules[i] == obj) {
        called = rules[i-1];
        break;
      }
    }

    if(is_mmu == 1) {
      mmu_set_uint8(&called->caller, mmu_get_uint8(&obj->nr));
    } else {
      called->caller = obj->nr;
    }

    {
      char str[OUTPUT_SIZE];
#ifdef ESP8266
      /*LCOV_EXCL_START*/
      memset(&str, 0, OUTPUT_SIZE);
      if(is_mmu == 1) {
        snprintf((char *)&str, OUTPUT_SIZE, "- running event \"%s\" called from caller #%d", name, mmu_get_uint8(&obj->nr));
      } else {
        snprintf((char *)&str, OUTPUT_SIZE, "- running event \"%s\" called from caller #%d", name, obj->nr);
      }
      Serial.println(str);
      /*LCOV_EXCL_STOP*/
#else
      printf("- running event \"%s\" called from caller #%d\n", name, obj->nr);
#endif
    }

    return rule_run(called, 0);
  }
}

void run_test(int *i, unsigned char *mempool, uint16_t size) {

  memset(&rule_options, 0, sizeof(struct rule_options_t));
  rule_options.is_token_cb = is_variable;
  rule_options.is_event_cb = is_event;
  rule_options.set_token_val_cb = vm_value_set;
  rule_options.get_token_val_cb = vm_value_get;
  rule_options.prt_token_val_cb = vm_value_prt;
  rule_options.clr_token_val_cb = vm_value_del;
  rule_options.cpy_token_val_cb = vm_value_cpy;
  rule_options.event_cb = event_cb;

  int nrtests = sizeof(unittests)/sizeof(unittests[0]), x = 0;

#ifdef ESP8266
  /*LCOV_EXCL_START*/
  if(*i >= nrtests) {
    delay(3);
    *i = 0;
  }
  /*LCOV_EXCL_STOP*/
#endif

  int len = strlen(unittests[(*i)].rule), oldoffset = 0;
  struct rule_stack_t *varstack = (struct rule_stack_t *)MALLOC(sizeof(struct rule_stack_t));
  if(varstack == NULL) {
    OUT_OF_MEMORY
  }
  varstack->buffer = NULL;
  varstack->nrbytes = 4;
  varstack->bufsize = 4;

  int ret = 0;

  struct pbuf mem;
  struct pbuf input;
  memset(&mem, 0, sizeof(struct pbuf));
  memset(&input, 0, sizeof(struct pbuf));

  mem.payload = mempool;
  mem.len = 0;
  mem.tot_len = size;

  uint8_t y = 0;
  uint16_t txtoffset = alignedbuffer(size-len-5);
#ifndef NON32XFER_HANDLER
  if((void *)mempool >= (void *)MMU_SEC_HEAP) {
    for(y=0;y<len;y++) {
      mmu_set_uint8((void *)&(mempool[txtoffset+y]), (uint8_t)unittests[(*i)].rule[y]);
    }
  } else {
#endif
    for(y=0;y<len;y++) {
      mempool[txtoffset+y] = (uint8_t)unittests[(*i)].rule[y];
    }
#ifndef NON32XFER_HANDLER
  }
#endif

  input.payload = &mempool[txtoffset];
  input.len = txtoffset;
  input.tot_len = len;

  unsigned char *cpytxt = (unsigned char *)MALLOC(len+1);
  if(cpytxt == NULL) {
    OUT_OF_MEMORY
  }
  memset(cpytxt, 0, len+1);
#ifndef NON32XFER_HANDLER
  if((void *)mempool >= (void *)MMU_SEC_HEAP) {
    for(y=0;y<len;y++) {
      cpytxt[y] = mmu_get_uint8(&((unsigned char *)input.payload)[y]);
    }
  } else {
#endif
    memcpy(cpytxt, input.payload, len);
#ifndef NON32XFER_HANDLER
  }
#endif
  oldoffset = txtoffset;

  while((ret = rule_initialize(&input, &rules, &nrrules, &mem, varstack)) == 0) {
    uint16_t size = 0;
    uint8_t rule_nr = 0;
#ifndef NON32XFER_HANDLER
    if((void *)mempool >= (void *)MMU_SEC_HEAP) {
      rule_nr = mmu_get_uint8(&rules[nrrules-1]->nr);
      size = mmu_get_uint16(&input.len) - oldoffset;
    } else {
#endif
      rule_nr = rules[nrrules-1]->nr;
      size = input.len - oldoffset;
#ifndef NON32XFER_HANDLER
    }

    if((void *)mempool >= (void *)MMU_SEC_HEAP) {
      assert(mmu_get_uint16(&rules[nrrules-1]->ast.nrbytes) >= 0);
      assert(mmu_get_uint16(&rules[nrrules-1]->ast.bufsize) >= 0);
      assert(mmu_get_uint16(&rules[nrrules-1]->ast.nrbytes) >= 0);
      assert(mmu_get_uint16(&rules[nrrules-1]->ast.bufsize) >= 0);
      assert(mmu_get_uint16(&rules[nrrules-1]->ast.nrbytes) <= mmu_get_uint16(&rules[nrrules-1]->ast.bufsize));
      assert(mmu_get_uint16(&rules[nrrules-1]->varstack.nrbytes) >= 0);
      assert(mmu_get_uint16(&rules[nrrules-1]->varstack.bufsize) >= 0);
      assert(mmu_get_uint16(&rules[nrrules-1]->varstack.nrbytes) >= 0);
      assert(mmu_get_uint16(&rules[nrrules-1]->varstack.bufsize) >= 0);
      assert(mmu_get_uint16(&rules[nrrules-1]->varstack.nrbytes) <= mmu_get_uint16(&rules[nrrules-1]->varstack.bufsize));
      assert(varstack->nrbytes >= 0);
      assert(varstack->bufsize >= 0);
      assert(varstack->nrbytes <= varstack->bufsize);
    } else {
#endif
      assert(rules[nrrules-1]->ast.nrbytes >= 0);
      assert(rules[nrrules-1]->ast.bufsize >= 0);
      assert(rules[nrrules-1]->ast.nrbytes >= 0);
      assert(rules[nrrules-1]->ast.bufsize >= 0);
      assert(rules[nrrules-1]->ast.nrbytes <= rules[nrrules-1]->ast.bufsize);
      assert(rules[nrrules-1]->varstack.nrbytes >= 0);
      assert(rules[nrrules-1]->varstack.bufsize >= 0);
      assert(rules[nrrules-1]->varstack.nrbytes >= 0);
      assert(rules[nrrules-1]->varstack.bufsize >= 0);
      assert(rules[nrrules-1]->varstack.nrbytes <= rules[nrrules-1]->varstack.bufsize);
      assert(varstack->nrbytes >= 0);
      assert(varstack->bufsize >= 0);
      assert(varstack->nrbytes <= varstack->bufsize);
#ifndef NON32XFER_HANDLER
    }
#endif

    {
      char str[OUTPUT_SIZE];
#ifdef ESP8266
      /*LCOV_EXCL_START*/
      memset(&str, 0, OUTPUT_SIZE);
      if(size > 49) {
        size = MIN(size, 45);
        snprintf((char *)&str, OUTPUT_SIZE, "Rule %.2d.%d / %.2d: [ %.*s ... %-*s ]", (*i)+1, rule_nr, nrtests, size, cpytxt, 46-size, " ");
      } else {
        size = MIN(size, 50);
        snprintf((char *)&str, OUTPUT_SIZE, "Rule %.2d.%d / %.2d: [ %-*s %-*s ]", (*i)+1, rule_nr, nrtests, size, cpytxt, 50-size, " ");
      }
      Serial.println(str);
      /*LCOV_EXCL_STOP*/
#else
      if(size > 49) {
        size = MIN(size, 45);
        printf("Rule %.2d.%d / %.2d: [ %.*s ... %-*s ]\n", (*i)+1, rules[nrrules-1]->nr, nrtests, size, cpytxt, 46-size, " ");
      } else {
        size = MIN(size, 50);
        printf("Rule %.2d.%d / %.2d: [ %.*s %-*s ]\n", (*i)+1, rules[nrrules-1]->nr, nrtests, size, cpytxt, 50-size, " ");
      }
#endif
    }

#ifdef DEBUG
    printf("AST stack is\t%3d bytes, local stack is\t%3d bytes, variable stack is\t%3d bytes\n", rules[nrrules-1]->ast.nrbytes, rules[nrrules-1]->varstack.nrbytes, (varstack->nrbytes));
    printf("AST bufsize is\t%3d bytes, local bufsize is\t%3d bytes, variable bufsize is\t%3d bytes\n", rules[nrrules-1]->ast.bufsize, rules[nrrules-1]->varstack.bufsize, (varstack->bufsize));
#endif

    valprint(rules[nrrules-1], (char *)&out, OUTPUT_SIZE);

    if(strcmp(out, unittests[(*i)].validate[rule_nr-1].output) != 0) {
      char str[OUTPUT_SIZE];
#ifdef ESP8266
      memset(&str, 0, OUTPUT_SIZE);
      snprintf((char *)&str, OUTPUT_SIZE, "Expected: %s\nWas: %s", unittests[(*i)].validate[rule_nr-1].output, out);
      Serial.println(str);
#else
      /*LCOV_EXCL_START*/
      printf("Expected: %s\n", unittests[(*i)].validate[rule_nr-1].output);
      printf("Was: %s\n", out);
      /*LCOV_EXCL_STOP*/
#endif
      exit(-1);
    }

#ifndef ESP8266
    /*LCOV_EXCL_START*/
    if((uint16_t)(rules[nrrules-1]->ast.nrbytes + (varstack->nrbytes - 4)) != unittests[(*i)].validate[rules[nrrules-1]->nr-1].bytes) {
      printf("Expected: %d\n", unittests[(*i)].validate[rules[nrrules-1]->nr-1].bytes);
      printf("Was: %d\n", rules[nrrules-1]->ast.nrbytes + (varstack->nrbytes - 4));

      exit(-1);
    }
    /*LCOV_EXCL_STOP*/
#endif

    for(x=0;x<5;x++) {
      varstack->nrbytes = 4;
      varstack->bufsize = 4;
      varstack->buffer = (unsigned char *)REALLOC(varstack->buffer, 4);
      memset(varstack->buffer, 0, 4);

#if defined(DEBUG) && !defined(ESP8266)
      clock_gettime(CLOCK_MONOTONIC, &rules[nrrules-1]->timestamp.first);
      printf("bytecode is %d bytes\n", rules[nrrules-1]->ast.nrbytes);
#endif
      rule_run(rules[nrrules-1], 0);
#if defined(DEBUG) && !defined(ESP8266)
      clock_gettime(CLOCK_MONOTONIC, &rules[nrrules-1]->timestamp.second);

      printf("rule #%d was executed in %.6f seconds\n", rules[nrrules-1]->nr,
        ((double)rules[nrrules-1]->timestamp.second.tv_sec + 1.0e-9*rules[nrrules-1]->timestamp.second.tv_nsec) -
        ((double)rules[nrrules-1]->timestamp.first.tv_sec + 1.0e-9*rules[nrrules-1]->timestamp.first.tv_nsec));

      printf("bytecode is %d bytes\n", rules[nrrules-1]->ast.nrbytes);
#endif

#ifndef ESP8266
      /*LCOV_EXCL_START*/
      if((uint16_t)(rules[nrrules-1]->ast.nrbytes + (varstack->nrbytes - 4)) != unittests[(*i)].run[rules[nrrules-1]->nr-1].bytes) {
        printf("Expected: %d\n", unittests[(*i)].run[rules[nrrules-1]->nr-1].bytes);
        printf("Was: %d\n", rules[nrrules-1]->ast.nrbytes + (varstack->nrbytes - 4));

        exit(-1);
      }
      /*LCOV_EXCL_STOP*/
#endif

      valprint(rules[nrrules-1], (char *)&out, OUTPUT_SIZE);
      if(strcmp(out, unittests[(*i)].run[rule_nr-1].output) != 0) {
        char str[OUTPUT_SIZE];
#ifdef ESP8266
        /*LCOV_EXCL_START*/
        memset(&str, 0, OUTPUT_SIZE);
        snprintf((char *)&str, OUTPUT_SIZE, "Expected: %s\nWas: %s", unittests[(*i)].run[rule_nr-1].output, out);
        Serial.println(str);
        /*LCOV_EXCL_STOP*/
#else
        printf("Expected: %s\n", unittests[(*i)].run[rule_nr-1].output);
        printf("Was: %s\n", out);
#endif
        exit(-1);
      }
      fflush(stdout);
    }

    varstack = (struct rule_stack_t *)MALLOC(sizeof(struct rule_stack_t));
    if(varstack == NULL) {
      OUT_OF_MEMORY
    }
    varstack->buffer = NULL;
    varstack->nrbytes = 4;
    varstack->bufsize = 4;

    FREE(cpytxt);
#ifndef NON32XFER_HANDLER
    if((void *)mempool >= (void *)MMU_SEC_HEAP) {
      input.payload = &mempool[mmu_get_uint16(&input.len)];
      oldoffset = mmu_get_uint16(&input.len);
      len = mmu_get_uint16(&input.tot_len);
    } else {
#endif
      input.payload = &mempool[input.len];
      oldoffset = input.len;
      len = input.tot_len;
#ifndef NON32XFER_HANDLER
    }
#endif

    if(len == 0) {
      break;
    }
    cpytxt = (unsigned char *)MALLOC(len+1);
    if(cpytxt == NULL) {
      OUT_OF_MEMORY
    }
    memset(cpytxt, 0, len+1);
#ifndef NON32XFER_HANDLER
    if((void *)mempool >= (void *)MMU_SEC_HEAP) {
      for(y=0;y<len;y++) {
        cpytxt[y] = mmu_get_uint8(&((unsigned char *)input.payload)[y]);
      }
    } else {
#endif
      memcpy(cpytxt, input.payload, len);
#ifndef NON32XFER_HANDLER
    }
#endif
  }

  if(unittests[(*i)].run[0].bytes > 0 && ret == -1) {
    /*LCOV_EXCL_START*/
    exit(-1);
    /*LCOV_EXCL_STOP*/
  }

  if(ret == -1) {
    const char *rule = unittests[(*i)].rule;
    int size = strlen(unittests[(*i)].rule);
    char str[OUTPUT_SIZE];
#ifdef ESP8266
    /*LCOV_EXCL_START*/
    memset(&str, 0, OUTPUT_SIZE);
    if(size > 49) {
      size = MIN(size, 45);
      snprintf((char *)&str, OUTPUT_SIZE, "Rule %.2d.%d / %.2d: [ %.*s ... %-*s ]", (*i)+1, 1, nrtests, size, rule, 46-size, " ");
    } else {
      size = MIN(size, 50);
      snprintf((char *)&str, OUTPUT_SIZE, "Rule %.2d.%d / %.2d: [ %-*s %-*s ]", (*i)+1, 1, nrtests, size, rule, 50-size, " ");
    }
    Serial.println(str);
    /*LCOV_EXCL_STOP*/
#else
    if(size > 49) {
      size = MIN(size, 45);
      printf("Rule %.2d.%d / %.2d: [ %.*s ... %-*s ]\n", (*i)+1, 1, nrtests, size, rule, 46-size, " ");
    } else {
      size = MIN(size, 50);
      printf("Rule %.2d.%d / %.2d: [ %.*s %-*s ]\n", (*i)+1, 1, nrtests, size, rule, 50-size, " ");
    }
#endif
  } else {
    for(x=0;x<nrrules;x++) {
      struct rule_stack_t *node = (struct rule_stack_t *)rules[x]->userdata;
      FREE(node->buffer);
      FREE(node);
    }
  }

  FREE(cpytxt);
  FREE(varstack);
  nrrules = 0;
}

#ifndef ESP8266
int main(int argc, char **argv) {
  int nrtests = sizeof(unittests)/sizeof(unittests[0]), i = 0;

  unsigned char *mempool = (unsigned char *)MALLOC(MEMPOOL_SIZE);
  if(mempool == NULL) {
    fprintf(stderr, "OUT_OF_MEMORY\n");
    exit(-1);
  }

  for(i=0;i<nrtests;i++) {
    memset(mempool, 0, MEMPOOL_SIZE);
    run_test(&i, mempool, MEMPOOL_SIZE);
  }
  FREE(mempool);
}
#endif
