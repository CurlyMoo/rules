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
void *MMU_SEC_HEAP = NULL;
#endif

struct rule_options_t rule_options;
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
  { "if 3 == 3 then $a = 6; end", { { "$a = 6", 100 } }, { { "$a = 6", 100 } } },
  { "if $a == $a then $a = $a; end", { { "$a = NULL", 96 } }, { { "$a = NULL", 96 } } },
  { "if (3 == 3) then $a = 6; end", { { "$a = 6", 108 } }, { { "$a = 6", 108 } } },
  { "if 10 == 10 then $a = 10; end", { { "$a = 10", 100 } }, { { "$a = 10", 100 } } },
  { "if 100 == 100 then $a = 100; end", { { "$a = 100", 100 } }, { { "$a = 100", 100 } } },
  { "if 1000 == 1000 then $a = 1000; end", { { "$a = 1000", 100 } }, { { "$a = 1000", 100 } } },
  { "if 3.5 == 3.5 then $a = 3.5; end", { { "$a = 3.5", 100 } }, { { "$a = 3.5", 100 } } },
  { "if 33.5 == 33.5 then $a = 33.5; end", { { "$a = 33.5", 100 } }, { { "$a = 33.5", 100 } } },
  { "if 333.5 == 333.5 then $a = 333.5; end", { { "$a = 333.5", 100 } }, { { "$a = 333.5", 100 } } },
  { "if 3.335 < 33.35 then $a = 3.335; end", { { "$a = 3.335", 100 } }, { { "$a = 3.335", 100 } } },
  { "if -10 == -10 then $a = -10; end", { { "$a = -10", 100 } }, { { "$a = -10", 100 } } },
  { "if -100 == -100 then $a = -100; end", { { "$a = -100", 100 } }, { { "$a = -100", 100 } } },
  { "if NULL == 3 then $a = 6; end", { { "$a = 6", 96 } }, { { "", 88 } } },
  { "if 1.1 == 1.1 then $a = 6; end", { { "$a = 6", 100 } }, { { "$a = 6", 100 } } },
  { "if 1.1 == 1.2 then $a = 6; end", { { "$a = 6", 100 } }, { { "", 92 } } },
  { "if 4 != 3 then $a = 6; end", { { "$a = 6", 100 } }, { { "$a = 6", 100 } } },
  { "if (4 != 3) then $a = 6; end", { { "$a = 6", 108 } }, { { "$a = 6", 108 } } },
  { "if NULL != 3 then $a = 6; end", { { "$a = 6", 96 } }, { { "$a = 6", 96 } } },
  { "if NULL != NULL then $a = 6; end", { { "$a = 6", 92 } }, { { "$a = 6", 92 } } },
  { "if 1.2 != 1.1 then $a = 6; end", { { "$a = 6", 100 } }, { { "$a = 6", 100 } } },
  { "if 1.2 != 1.2 then $a = 6; end", { { "$a = 6", 100 } }, { { "", 92 } } },
  { "if 1 != 1 then $a = 6; end", { { "$a = 6", 100 } }, { { "", 92 } } },
  { "if 1.1 >= 1.2 then $a = 6; end", { { "$a = 6", 100 } }, { { "", 92 } } },
  { "if 1.1 >= 1.1 then $a = 6; end", { { "$a = 6", 100 } }, { { "$a = 6", 100 } } },
  { "if 2 >= 1.1 then $a = 6; end", { { "$a = 6", 100 } }, { { "$a = 6", 100 } } },
  { "if 2.1 >= 1 then $a = 6; end", { { "$a = 6", 100 } }, { { "$a = 6", 100 } } },
  { "if NULL >= 1.2 then $a = 6; end", { { "$a = 6", 96 } }, { { "", 88 } } },
  { "if NULL >= NULL then $a = 6; end", { { "$a = 6", 92 } }, { { "", 84 } } },
  { "if 1 > 2 then $a = 6; end", { { "$a = 6", 100} }, { { "", 92 } } },
  { "if 2 > 1 then $a = 6; end", { { "$a = 6", 100 } }, { { "$a = 6", 100 } } },
  { "if 2 > 1.1 then $a = 6; end", { { "$a = 6", 100 } }, { { "$a = 6", 100 } } },
  { "if 1.1 > 1.2 then $a = 6; end", { { "$a = 6", 100 } }, { { "", 92 } } },
  { "if 1.1 > 1.0 then $a = 6; end", { { "$a = 6", 104 } }, { { "$a = 6", 104 } } },
  { "if NULL > 1.2 then $a = 6; end", { { "$a = 6", 96 } }, { { "", 88 } } },
  { "if NULL > NULL then $a = 6; end", { { "$a = 6", 92 } }, { { "", 84 } } },
  { "if 1.2 <= 1.1 then $a = 6; end", { { "$a = 6", 100 } }, { { "", 92 } } },
  { "if 1.1 <= 1.1 then $a = 6; end", { { "$a = 6", 100 } }, { { "$a = 6", 100 } } },
  { "if 1.1 <= 2 then $a = 6; end", { { "$a = 6", 100 } }, { { "$a = 6", 100 } } },
  { "if 1 <= 2 then $a = 6; end", { { "$a = 6", 100 } }, { { "$a = 6", 100 } } },
  { "if 2 <= 1 then $a = 6; end", { { "$a = 6", 100 } }, { { "", 92 } } },
  { "if 1 <= 2.1 then $a = 6; end", { { "$a = 6", 100 } }, { { "$a = 6", 100 } } },
  { "if 1.2 <= NULL then $a = 6; end", { { "$a = 6", 96 } }, { { "", 88 } } },
  { "if NULL <= NULL then $a = 6; end", { { "$a = 6", 92 } }, { { "", 84 } } },
  { "if 2 < 1 then $a = 6; end", { { "$a = 6", 100 } }, { { "", 92 } } },
  { "if 1 < 2 then $a = 6; end", { { "$a = 6", 100 } }, { { "$a = 6", 100 } } },
  { "if 1.1 < 2 then $a = 6; end", { { "$a = 6", 100 } }, { { "$a = 6", 100 } } },
  { "if 1.2 < 1.1 then $a = 6; end", { { "$a = 6", 100 } }, { { "", 92 } } },
  { "if 1.0 < 1.1 then $a = 6; end", { { "$a = 6", 104 } }, { { "$a = 6", 104 } } },
  { "if 1.2 < NULL then $a = 6; end", { { "$a = 6", 96 } }, { { "", 88 } } },
  { "if NULL < NULL then $a = 6; end", { { "$a = 6", 92 } }, { { "", 84 } } },
  { "if NULL && NULL then $a = -6; end", { { "$a = -6", 92 } }, { { "", 84 } } },
  { "if 0 && 0 then $a = -6; end", { { "$a = -6", 100 } }, { { "", 92 } } },
  { "if 1 && 1 then $a = -6; end", { { "$a = -6", 100 } }, { { "$a = -6", 100 } } },
  { "if 1.1 && 1.2 then $a = -6; end", { { "$a = -6", 100 } }, { { "$a = -6", 100 } } },
  { "if 0.0 && 1.2 then $a = -6; end", { { "$a = -6", 104 } }, { { "", 96 } } },
  { "if -1.1 && 1.2 then $a = -6; end", { { "$a = -6", 100 } }, { { "", 92 } } },
  { "if 1.2 && 0.0 then $a = -6; end", { { "$a = -6", 104 } }, { { "", 96 } } },
  { "if 1.2 && -1.1 then $a = -6; end", { { "$a = -6", 100 } }, { { "$a = -6", 100 } } },
  { "if NULL || NULL then $a = -6; end", { { "$a = -6", 92 } }, { { "", 84 } } },
  { "if 0 || 0 then $a = -6; end", { { "$a = -6", 100 } }, { { "", 92 } } },
  { "if 1 || 1 then $a = -6; end", { { "$a = -6", 100 } }, { { "$a = -6", 100 } } },
  { "if 1.1 || 1.2 then $a = -6; end", { { "$a = -6", 100 } }, { { "$a = -6", 100 } } },
  { "if -0.1 || -0.1 then $a = -6; end", { { "$a = -6", 100 } }, { { "", 92 } } },
  { "if -0.5 || 1 then $a = -6; end", { { "$a = -6", 100 } }, { { "$a = -6", 100 } } },
  { "if 0.0 || 1.2 then $a = -6; end", { { "$a = -6", 104 } }, { { "$a = -6", 104 } } },
  { "if -1.1 || 1.2 then $a = -6; end", { { "$a = -6", 100 } }, { { "$a = -6", 100 } } },
  { "if 1.2 || 0.0 then $a = -6; end", { { "$a = -6", 104 } }, { { "$a = -6", 104 } } },
  { "if 1.2 || -1.1 then $a = -6; end", { { "$a = -6", 100 } }, { { "$a = -6", 100 } } },
  { "if 3 == NULL then $a = 6; end", { { "$a = 6", 96 } }, { { "", 88 } } },
  { "if (NULL == 3) then $a = 6; end", { { "$a = 6", 104 } }, { { "", 96 } } },
  { "if (3 == NULL) then $a = 6; end", { { "$a = 6", 104 } }, { { "", 96 } } },
  { "if @a == 3 then $a = 6; end", { { "$a = 6", 112 } }, { { "", 104 } } },
  { "if foo#bar == 3 then $a = 6; end", { { "$a = 6", 120 } }, { { "$a = 6", 120 } } },
  { "if 3 == 3 then @a = 6; end", { { "@a = 6", 100 } }, { { "@a = 6", 100 } } },
  { "if 3 == 3 then $a = -6; end", { { "$a = -6", 100 } }, { { "$a = -6", 100 } } },
  { "if 4 != 3 then $a = -6; end", { { "$a = -6", 100 } }, { { "$a = -6", 100 } } },
  { "if 3 == 3 then $a = 1.1; end", { { "$a = 1.1", 100 } }, { { "$a = 1.1", 100 } } },
  { "if 3 == 3 then $a = 1 - NULL; end", { { "$a = NULL", 112 } }, { { "$a = NULL", 112 } } },
  { "if 3 == 3 then $a = 1 - 0.5; end", { { "$a = 0.5", 120 } }, { { "$a = 0.5", 120 } } },
  { "if 3 == 3 then $a = 0.5 - 0.5; end", { { "$a = 0", 120 } }, { { "$a = 0", 120 } } },
  { "if 3 == 3 then $a = 0.5 - 1; end", { { "$a = -0.5", 120 } }, { { "$a = -0.5", 120 } } },
  { "if 3 == 3 then $a = 1 + 2; end", { { "$a = 3", 120 } }, { { "$a = 3", 120 } } },
  { "if 3 == 3 then $a = 1.5 + 2.5; end", { { "$a = 4", 120 } }, { { "$a = 4", 120 } } },
  { "if 3 == 3 then $a = 1 * NULL; end", { { "$a = NULL", 112 } }, { { "$a = NULL", 112 } } },
  { "if 3 == 3 then $a = 1 ^ NULL; end", { { "$a = NULL", 112 } }, { { "$a = NULL", 112 } } },
  { "if 3 == 3 then $a = 9 % 2; end", { { "$a = 1", 120 } }, { { "$a = 1", 120 } } },
  { "if 3 == 3 then $a = 9 % NULL; end", { { "$a = NULL", 112 } }, { { "$a = NULL", 112 } } },
  { "if 3 == 3 then $a = 9.5 % 1.5; end", { { "$a = 0.5", 120 } }, { { "$a = 0.5", 120 } } },
  { "if 3 == 3 then $a = 10 % 1.5; end", { { "$a = 1.5", 120 } }, { { "$a = 1.5", 120 } } },
  { "if 3 == 3 then $a = 1.5 % 10; end", { { "$a = 1", 120 } }, { { "$a = 1", 120 } } },
  { "if 1 == 1 then $a = 1 + 2 + 3; end", { { "$a = 6", 140 } }, { { "$a = 6", 140 } } },
  { "if 1 == 1 == 1 then $a = 1; end", { { "$a = 1", 120 } },  { { "$a = 1", 120 } } },
  { "if 1 == 1 == 2 then $a = 1; end", { { "$a = 1", 120 } },  { { "", 112 } } },
  { "if 1 == 1 then $a = 1; $a = $a + 2; end", { { "$a = 3", 140 } }, { { "$a = 3", 140 } } },
  { "if 1 == 1 then $a = 6; $a = $a + 2 + $a / 3; end", { { "$a = 10", 180 } }, { { "$a = 10", 180 } } },
  { "if 1 == 1 then $a = 6; $a = ($a + 2 + $a / 3); end", { { "$a = 10", 188 } }, { { "$a = 10", 188 } } },
  { "if 1 == 1 then $a = NULL / 1; end", { { "$a = NULL", 112 } }, { { "$a = NULL", 112 } } },
  { "if 1 == 1 then $a = 2.5 / 7.5; end", { { "$a = 0.333333", 120 } }, { { "$a = 0.333333", 120 } } },
  { "if 1 == 1 then $a = 5 / 2.5; end", { { "$a = 2", 120 } }, { { "$a = 2", 120 } } },
  { "if 1 == 1 then $a = 2.5 / 5; end", { { "$a = 0.5", 120 } }, { { "$a = 0.5", 120 } } },
  { "if 1 == 1 then $a = 2.5 * 2.5; end", { { "$a = 6.25", 120 } }, { { "$a = 6.25", 120 } } },
  { "if 12 == 1 then $a = 1 + 2 * 3; end", { { "$a = 7", 140 } }, { { "", 132 } } },
  { "if 1 == 1 then $a = 3 * 1 + 2 * 3; end", { { "$a = 9", 160 } }, { { "$a = 9", 160 } } },
  { "if 1 == 1 then $a = (3 * 1 + 2 * 3); end", { { "$a = 9", 168 } }, { { "$a = 9", 168 } } },
  { "if 1 == 1 then $a = 1 * 2 + 3; end", { { "$a = 5", 140 } }, { { "$a = 5", 140 } } },
  { "if 1 == 1 then $a = 1 * 100 ^ 2; end", { { "$a = 10000", 140 } }, { { "$a = 10000", 140 } } },
  { "if 1 == 1 then $a = 1 * 1.1 ^ 2; end", { { "$a = 1.21", 140 } }, { { "$a = 1.21", 140 } } },
  { "if 1 == 1 then $a = 1 * 1.1 ^ 1.1; end", { { "$a = 1.11053", 140 } }, { { "$a = 1.11053", 140 } } },
  { "if 1 == 1 then $a = 1 * 100 ^ 1.1; end", { { "$a = 158.489", 140 } }, { { "$a = 158.489", 140 } } },
  { "if 1 == 1 then $a = 1 + 2 * 3 / 4; end", { { "$a = 2.5", 160 } }, { { "$a = 2.5", 160 } } },
  { "if 1 == 1 then $a = 1 + 2 * 3 / 4 ^ 2; end", { { "$a = 1.375", 180 } }, { { "$a = 1.375", 180 } } },
  { "if 1 == 1 then $a = 1 + 4 ^ 2 ^ 1; end", { { "$a = 17", 160 } }, { { "$a = 17", 160 } } },
  { "if 1 == 1 then $a = (1 + 4 ^ 2 ^ 1); end", { { "$a = 17", 168 } }, { { "$a = 17", 168 } } },
  { "if 1 == 1 then $a = (1 + 2 * 3 / 4 ^ 2); end", { "$a = 1.375", 188 }, { "$a = 1.375", 188 } },
  { "if 1 == 1 then $a = (1 + 2 * 3 / 4 ^ 2 ^ 1 * 3); end", { "$a = 2.125", 228 }, { "$a = 2.125", 228 } },
  { "if 1 == 1 then $a = (1 + 2 * 3 / 4 ^ 2 ^ 1 * 3 ^ 4); end", { "$a = 31.375", 248 }, { "$a = 31.375", 248 } },
  { "if 1 == 1 then $a = 1 + 2 * 3 / 4 ^ 2 ^ 1 * 3 ^ 4; end", { "$a = 31.375", 240 }, { "$a = 31.375", 240 } },
  { "if 1 == 1 then $a = (1 + 2) * 3; end", { "$a = 9", 148 }, { "$a = 9", 148 } },
  { "if 1 == 1 then $a = (1 + 2 * 3); end", { "$a = 7", 148 }, { "$a = 7", 148 } },
  { "if 1 == 1 then $a = 3 * (1 + 2); end", { "$a = 9", 148 }, { "$a = 9", 148 } },
  { "if 1 == 1 then $a = 3 * (1 + 2) * 2; end", { "$a = 18", 168 }, { "$a = 18", 168 } },
  { "if 1 == 1 then $a = 3 * (1 + 3) ^ 2; end", { "$a = 48", 168 }, { "$a = 48", 168 } },
  { "if 1 == 1 then $a = 7 + 6 * 5 / (4 - 3) ^ 2 ^ 1; end", { "$a = 37", 228 }, { "$a = 37", 228 } },
  { "if 1 == 1 then $a = 3 * (1 + 2) + 2; end", { "$a = 11", 168 }, { "$a = 11", 168 } },
  { "if 1 == 1 then $a = 3 + (1 + 2) * 2; end", { "$a = 9", 168 }, { "$a = 9", 168 } },
  { "if 1 == 1 then $a = 3 * (1 + 2 / 2) + 3; end", { "$a = 9", 188 }, { "$a = 9", 188 } },
  { "if 1 == 1 then $a = 3 * ((1 + 2) / 2) + 2; end", { "$a = 6.5", 196 }, { "$a = 6.5", 196 } },
  { "if 1 == 1 then $a = 3 * ((1 + 2) / 2 + 3); end", { "$a = 13.5", 196 }, { "$a = 13.5", 196 } },
  { "if 1 == 1 then $a = 3 * ((1 + 2) / (2 + 3)); end", { "$a = 1.8", 204 }, { "$a = 1.8", 204 } },
  { "if 1 == 1 then $a = 3 * ((((1 + 2) / (2 + 3)))); end", { "$a = 1.8", 220 }, { "$a = 1.8", 220 } },
  { "if $a == $a then $a = $a * (((($a + $a) / ($a + $a)))); end", { "$a = NULL", 216 }, { "$a = NULL", 216 } },
  { "if 1 == 1 then $a = 3 * ((1 + 2) / (2 + 3)) + 2; end", { "$a = 3.8", 224 }, { "$a = 3.8", 224 } },
  { "if 1 == 1 then $a = (1 == 1 && 1 == 0) || 5 >= 4; end", { "$a = 1", 208 }, { "$a = 1", 208 } },
  { "if 1 == 1 then $a = (1 == 1 && 1 == 0) || 3 >= 4; end", { "$a = 0", 208 }, { "$a = 0", 208 } },
  { "if 1 == 1 then $a = 3; end", { "$a = 3", 100 }, { "$a = 3", 100 } },
  { "if 1 == 1 then $a = 3.1; $b = $a; end", { "$a = 3.1$b = 3.1", 140 }, { "$a = 3.1$b = 3.1", 140 } },
  { "if 1 == 1 then $a = $a + 1; end", { "$a = NULL", 116 }, { "$a = NULL", 116 } },
  { "if 1 == 1 then $a = max(foo#bar); end", { "$a = 3", 132 }, { "$a = 3", 132 } },
  { "if 1 == 1 then $a = coalesce($a, 0) + 1; end", { "$a = 1", 144 }, { "$a = 1", 144 } },
  { "if 1 == 1 then $a = coalesce($a, 1.1) + 1; end", { "$a = 2.1", 144 }, { "$a = 2.1", 144 } },
  { "if 1 == 1 then if $a == NULL then $a = 0; end $a = $a + 1; end", { "$a = 1", 188 }, { "$a = 1", 188 } },
  { "if 1 == 1 then $a = 1; if $a == NULL then $a = 0; end $a = $a + 1; end", { "$a = 1", 204 }, { "$a = 2", 204 } },
  { "if 1 == 1 then $a = 1; if $a == 1 then $a = NULL; end end", { "$a = NULL", 164 }, { "$a = NULL", 164 } },
  { "if 1 == 1 then $a = 1; $a = NULL; end", { "$a = NULL", 112 }, { "$a = NULL", 112 } },
  { "if 1 == 2 then $a = 3; else $a = 4; end", { "$a = 4", 128 }, { "$a = 4", 128 } },
  { "if 1 == 1 then $a = 3; else $a = 4; end", { "$a = 4", 128 }, { "$a = 3", 128 } },
  { "if (1 + 1) == 1 then $a = 3; else $a = 4; end", { "$a = 4", 156 }, { "$a = 4", 156 } },
  { "if 1 == 2 || 3 >= 4 then $a = max(1, 2); end", { "$a = 2", 164 }, { "", 156 } },
  { "if 1 == 2 || 3 >= 4 then $a = min(3, 1, 2); end", { "$a = 1", 172 }, { "", 164 } },
  { "if 1 == 2 || 3 >= 4 then $a = 1; end", { "$a = 1", 140 }, { "", 132 } },
  { "if 1 == 2 || 3 >= 4 || 5 == 6 then $a = max(1, 3, 2); else $b = 9; end", { "$a = 3$b = 9", 260 }, { "$b = 9", 252 } },
  { "if 1 == 1 then $a = 1; $b = 2; end", { "$a = 1$b = 2", 140 }, { "$a = 1$b = 2", 140 } },
  { "if 1 == 1 then $a = 1; $b = ($a + 3) * 3; end", { "$a = 1$b = 12", 188 }, { "$a = 1$b = 12", 188 } },
  { "if 1 == 1 then $a = 1; $b = $a + 3 * 3; end", { "$a = 1$b = 10", 180 }, { "$a = 1$b = 10", 180 } },
  { "if 1 == 1 then if 5 == 6 then $a = 1; end $a = 2; end", { "$a = 2", 172 }, { "$a = 2", 172 } },
  { "if 1 == 1 then if 5 == 6 then $a = 1; end if 1 == 3 then $b = 3; end $a = 2; end", { "$b = 3$a = 2", 260 }, { "$a = 2", 252 } },
  { "if 1 == 1 then $a = 1; $b = $a; end", { "$a = 1$b = 1", 140 }, { "$a = 1$b = 1", 140 } },
  { "if 1 == 1 then $a = 1; if 5 >= 4 then $a = 3; end $b = $a; end", { "$a = 3$b = 3", 208 }, { "$a = 3$b = 3", 208 } },
  { "if 1 == 1 then $a = 1; if $a == 1 then $a = 3; end end", { "$a = 3", 172 }, { "$a = 3", 172 } },
  { "if (1 == 1 && 1 == 0) || 5 >= 4 then $a = 1; end", { "$a = 1", 188 }, { "$a = 1", 188 } },
  { "if 3 == 3 then $a = max(1); end", { "$a = 1", 112 }, { "$a = 1", 112 } },
  { "if 3 == 3 then $a = max(NULL, 1); end", { "$a = 1", 120 }, { "$a = 1", 120 } },
  { "if 3 == 3 then $a = max(1, NULL); end", { "$a = 1", 120 }, { "$a = 1", 120 } },
  { "if 3 == 3 then $b = 2; $a = max($b, 1); end", { "$b = 2$a = 2", 164 }, { "$b = 2$a = 2", 164 } },
  { "if 3 == 3 then $a = max(1, 2); end", { "$a = 2", 124 }, { "$a = 2", 124 } },
  { "if 3 == 3 then $a = max(1, 2, 3, 4); end", { "$a = 4", 144 }, { "$a = 4", 144 } },
  { "if 3 == 3 then $a = max(1, 4, 5, 3, 2); end", { "$a = 5", 152 }, { "$a = 5", 152 } },
  { "if 3 == 3 then $a = max(max(1, 4), 2); end", { "$a = 4", 148 }, { "$a = 4", 148 } },
  { "if 3 == 3 then $a = max(1, 2) * 3; end", { "$a = 6", 144 }, { "$a = 6", 144 } },
  { "if 3 == 3 then $a = max(1, 2) * max(3, 4); end", { "$a = 8", 168 }, { "$a = 8", 168 } },
  { "if 3 == 3 then $a = max(max(1, 2), (1 * max(1, 3) ^ 2)); end", { "$a = 9", 220}, { "$a = 9", 220 } },
  { "if 3 == 3 then $b = 1; $a = max($b + 1); end", { "$b = 1$a = 2", 172 }, { "$b = 1$a = 2", 172 } },
  { "if 3 == 3 then $b = 1; $a = max($b + 1) * 3; end", { "$b = 1$a = 6", 192 }, { "$b = 1$a = 6", 192 } },
  { "if max(1, 3) == 3 then $a = 1; end", { "$a = 1", 124 }, { "$a = 1", 124 } },
  { "if max(1, 3) == max(1, 3) then $a = 1; end", { "$a = 1", 148 }, { "$a = 1", 148 } },
  { "if 3 == 3 then max(1, 2); end", { "", 96 }, { "", 96 } },
  { "if 3 == 3 then $a = 1; $b = $a; max(1, 2); end", { "$a = 1$b = 1", 172 }, { "$a = 1$b = 1", 172 } },
  { "if 3 == 3 then $a = max(1 + 1, 2 + 2); end", { "$a = 4", 164 }, { "$a = 4", 164 } },
  { "if 1 == 1 then $a = coalesce($b, 0); end  ", { { "$b = NULL$a = 0", 140 } },  { { "$b = NULL$a = 0", 140 } } }, // FIXME
  { "if 3 == 3 then $a = max((1 + 3), 2); end", { "$a = 4", 152 }, { "$a = 4", 152 } },
  { "if 3 == 3 then $a = max((1 + (3 * 3)), 2); end", { "$a = 10", 180 }, { "$a = 10", 180 } },
  { "if 3 == 3 then $a = max(1 + 3 * 3, 3 * 4); end", { "$a = 12", 184 }, { "$a = 12", 184 } },
  { "if 3 == 3 then $a = max(((2 + 3) * 3), (3 * 4)); end", { "$a = 15", 208 }, { "$a = 15", 208 } },
  { "if 3 == 3 then $a = (max(1, 2) + 2) * 3; end", { "$a = 12", 172 }, { "$a = 12", 172 } },
  { "if 1 == 1 then $a = max(0, 1) + max(1, 2) * max(2, 3) / max(3, 4) ^ max(1, 2) ^ max(0, 1) * max(2, 3) ^ max(3, 4); end", { "$a = 31.375", 432 }, { "$a = 31.375", 432 } },
  { "if 3 == 3 then $a = 2; $b = max((($a + 3) * 3), (3 * 4)); end", { "$a = 2$b = 15", 248 }, { "$a = 2$b = 15", 248 } },
  { "if 3 == 3 then $a = 2; $b = max((max($a + 3) * 3), (3 * 4)); end", { "$a = 2$b = 15", 252 }, { "$a = 2$b = 15", 252 } },
  { "if 1 == 1 then $a = max(1 * 2, (min(5, 6) + 1) * 6); end", { { "$a = 36", 216 } }, { { "$a = 36", 216 } } },
  { "if 1 == 1 then $a = 1; else $a = 2; end", { "$a = 2", 128 }, { "$a = 1", 128 } },
  { "if 1 == 2 then $a = 1; elseif 2 == 2 then $a = 3; else $a = 2; end", { "$a = 2", 208 }, { "$a = 3", 208 } },
  { "if 1 == 2 then $a = 1; elseif 2 == 2 then $a = 2; end", { "$a = 2", 180 }, { "$a = 2", 180 } },
  { "if 1 == 2 then $a = 1; elseif 3 == 2 then $a = 2; elseif 4 == 4 then $a = 4; end", { "$a = 4", 260 }, { "$a = 4", 260} },
  { "if 1 == 2 then $a = 1; elseif 3 == 2 then if 1 > 4 then $a = 2; else $a = 1; end end", { "$a = 1", 260 }, { "", 252 } },
  { "if 1 == 2 then $a = 1; if 2 < 3 then $a = 5; end elseif 3 == 2 then if 1 > 4 then $a = 2; else $a = 1; end end", { "$a = 1", 332 }, { "", 324 } },
  { "if 1 == 1 then if 2 == 2 then $a = 1; end else $a = 2; end", { "$a = 2", 180 }, { "$a = 1", 180 } },
  { "if (1 == 1) || 5 >= 4 then $a = 1; if 6 == 5 then $a = 2; end $a = $a + 3; $b = (3 + $a) * 2; else $a = 7; end", { "$b = 16$a = 7", 372 }, { "$a = 4$b = 14", 372 } },
  { "if (1 == 1) || 5 >= 4 then $a = 1; if 6 == 5 then $a = 2; end $a = $a + 3; $b = (3 + $a) * 2; $b = 3; else $a = 7; end", { "$b = 3$a = 7", 388 }, { "$a = 4$b = 3", 388 } },
  { "if (1 == 1 && 1 == 0) || 5 >= 4 then $a = 1; if 6 == 5 then $a = 2; end $a = $a + 3; $b = (3 + $a * 5 + 3 * 1) * 2; @c = 5; else if 2 == 2 then $a = 6; else $a = 7; end end", { "$b = 62@c = 5$a = 7", 588 }, { "$a = 4$b = 52@c = 5", 588 } },
  { "if 3 == 3 then $a = 6; end if 3 == 3 then $b = 3; end  ", { { "$a = 6", 100 }, { "$b = 3", 100 } },  { { "$a = 6", 100 }, { "$b = 3" , 100 } } },
  { "   if 3 == 3 then $a = 6; end    if 3 == 3 then $b = 6; end               if 3 == 3 then $c = 6; end", { { "$a = 6", 100 }, { "$b = 6", 100 }, { "$c = 6", 100 } }, { { "$a = 6", 100 }, { "$b = 6", 100 }, { "$c = 6", 100 } } },
  { "on foo then max(1, 2); end", { "", 72 }, { "", 72 } },
  { "on foo then $a = 6; end", { "$a = 6", 76 }, { "$a = 6", 76 } },
  { "on foo then $a = 6; $b = 3; end", { "$a = 6$b = 3", 116 }, { "$a = 6$b = 3", 116 } },
  { "on foo then @a = 6; end", { { "@a = 6", 76 } }, { { "@a = 6", 76 } } },
  { "on foo then $a = 1 + 2; end", { { "$a = 3", 96 } }, { { "$a = 3", 96 } } },
  { "on foo then if 5 == 6 then $a = 1; end $a = 2; end", { "$a = 2", 148 }, { "$a = 2", 148 } },
  { "on foo then if 5 == 6 then $a = 1; end if 1 == 3 then $b = 3; end $a = 2; end", { "$b = 3$a = 2", 236 }, { "$a = 2", 228 } },
  { "on foo then $a = 6; end if 3 == 3 then $b = 3; end  ", { { "$a = 6", 76 }, { "$b = 3" , 100 } },  { { "$a = 6", 76 }, { "$b = 3" , 100 } } },
  { "on foo then $a = 6; end if 3 == 3 then foo(); $b = 3; end  ", { { "$a = 6", 76 }, { "$b = 3", 116 } },  { { "$a = 6", 76 }, { "$b = 3", 116 } } },
  { "on foo then $a = coalesce($b, 0); end  ", { { "$b = NULL$a = 0", 116 } },  { { "$b = NULL$a = 0", 116 } } }, // FIXME

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
  { "if 1 == ) then $a = 1", { { NULL, 0 } },  { { NULL, 0 } } },
  { "if 1 == ) then $a = 1 end", { { NULL, 0 } },  { { NULL, 0 } } },
  { "if 1 == 1 then $a = 1 end", { { NULL, 0 } },  { { NULL, 0 } } },
  { "elseif 1 == 1 then $a = 1; end", { { NULL, 0 } },  { { NULL, 0 } } },
  { "if (1 == 1 then $a = 1; end", { { NULL, 0 } },  { { NULL, 0 } } },
  { "if 1 == 1) then $a = 1; end", { { NULL, 0 } },  { { NULL, 0 } } },
  { "if () then $a = 1; end", { { NULL, 0 } },  { { NULL, 0 } } },
  { "if ( == ) then $a = 1; end", { { NULL, 0 } },  { { NULL, 0 } } },
  { "if 1 2 then $a = 1; end", { { NULL, 0 } },  { { NULL, 0 } } },
  { "on foo end", { { NULL, 0 } },  { { NULL, 0 } } },
  { "on foo then max(1, 2) end", { { NULL, 0 } },  { { NULL, 0 } } },
  { "on foo then bar(); end  ", { { NULL, 0 } },  { { NULL, 0 } } },
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
  { "if 1 == 2 then $a = 1; else $a = 3; elseif $a = 2; end", { { NULL, 0 } },  { { NULL, 0 } } },
  { "on foo then elseif $a = 2; else $a = 1; end", { { NULL, 0 } },  { { NULL, 0 } } },
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
  if(size == 3 &&
    (strnicmp(&text[*pos], "foo", 3) == 0 || strnicmp(&text[*pos], "bar", 3) == 0)) {
    return 0;
  }
  return -1;
}

static unsigned char *vm_value_get(struct rules_t *obj, uint16_t token) {
  struct rule_stack_t *varstack = (struct rule_stack_t *)obj->userdata;
  uint16_t ret = 0, s_out = 32;

  unsigned char out[s_out];
  memset(&out, 0, s_out);

  if(rule_token(&obj->ast, token, (unsigned char *)&out, &s_out) < 0) {
    return NULL;
  }

  if(out[0] != TVALUE) {
    return NULL;
  }

  struct vm_tvalue_t *var = (struct vm_tvalue_t *)out;

  ret = var->go;

#ifdef DEBUG
    printf(". %s %d %d\n", __FUNCTION__, __LINE__, var->go);
#endif

  if(strcmp((char *)var->token, "foo#bar") == 0) {
    memset(&vinteger, 0, sizeof(struct vm_vinteger_t));
    vinteger.type = VINTEGER;
    vinteger.value = 3;
    return (unsigned char *)&vinteger;
  } else if(var->token[0] == '@') {
    memset(&vinteger, 0, sizeof(struct vm_vinteger_t));
    vinteger.type = VINTEGER;
    vinteger.value = 5;
    return (unsigned char *)&vinteger;
  } else {
    if(var->go == 0) {
      ret = varstack->nrbytes;
      uint16_t size = varstack->nrbytes+sizeof(struct vm_vnull_t);
      if((varstack->buffer = (unsigned char *)REALLOC(varstack->buffer, size)) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      struct vm_vnull_t *value = (struct vm_vnull_t *)&varstack->buffer[ret];
      value->type = VNULL;
      value->ret = token;

      var->go = ret;

      s_out = 32;
      if(rule_token(&obj->ast, token, (unsigned char *)&out, &s_out) < 0) {
        return NULL;
      }

      varstack->nrbytes = size;
      varstack->bufsize = size;
    }

    return &varstack->buffer[ret];
  }
  return NULL;
}

static int8_t vm_value_del(struct rules_t *obj, uint16_t idx) {
  struct rule_stack_t *varstack = (struct rule_stack_t *)obj->userdata;
  uint16_t x = 0, ret = 0;

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
          uint16_t s_out = 32;
          unsigned char out[s_out];
          memset(&out, 0, s_out);

          if(rule_token(&obj->ast, node->ret, (unsigned char *)&out, &s_out) < 0) {
            return -1;
          }

          struct vm_tvalue_t *tmp = (struct vm_tvalue_t *)out;
          tmp->go = x;

          s_out = 32;
          if(rule_token(&obj->ast, node->ret, (unsigned char *)&out, &s_out) < 0) {
            return -1;
          }
        }

        x += sizeof(struct vm_vinteger_t)-1;
      } break;
      case VFLOAT: {
        struct vm_vfloat_t *node = (struct vm_vfloat_t *)&varstack->buffer[x];
        if(node->ret > 0) {
          uint16_t s_out = 32;
          unsigned char out[s_out];
          memset(&out, 0, s_out);

          if(rule_token(&obj->ast, node->ret, (unsigned char *)&out, &s_out) < 0) {
            return -1;
          }

          struct vm_tvalue_t *tmp = (struct vm_tvalue_t *)out;
          tmp->go = x;

          s_out = 32;
          if(rule_token(&obj->ast, node->ret, (unsigned char *)&out, &s_out) < 0) {
            return -1;
          }
        }
        x += sizeof(struct vm_vfloat_t)-1;
      } break;
      case VNULL: {
        struct vm_vnull_t *node = (struct vm_vnull_t *)&varstack->buffer[x];
        if(node->ret > 0) {
          uint16_t s_out = 32;
          unsigned char out[s_out];
          memset(&out, 0, s_out);

          if(rule_token(&obj->ast, node->ret, (unsigned char *)&out, &s_out) < 0) {
            return -1;
          }

          struct vm_tvalue_t *tmp = (struct vm_tvalue_t *)out;
          tmp->go = x;

          s_out = 32;
          if(rule_token(&obj->ast, node->ret, (unsigned char *)&out, &s_out) < 0) {
            return -1;
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

static int8_t vm_value_set(struct rules_t *obj, uint16_t token, uint16_t val) {
  struct rule_stack_t *varstack = (struct rule_stack_t *)obj->userdata;
  uint16_t ret = 0;

  uint16_t outAsize = 32;
  unsigned char outA[outAsize];
  memset(&outA, 0, outAsize);

  if(rule_token(&obj->ast, token, (unsigned char *)&outA, &outAsize) < 0) {
    return -1;
  }

  if(outA[0] != TVALUE) {
    return -1;
  }

  struct vm_tvalue_t *var = (struct vm_tvalue_t *)outA;

  outAsize = 32;
  if(rule_token(&obj->ast, token, (unsigned char *)&outA, &outAsize) < 0) {
    return -1;
  }

#ifdef DEBUG
  printf(". %s %d %d\n", __FUNCTION__, __LINE__, val);
#endif

  unsigned char *outB = NULL;
  uint16_t outBsize = 0;

  int8_t r = rule_token(&obj->varstack, val, NULL, &outBsize);
  if(r == -1) {
    return -1;
  } else if(r == -2) {
    outB = (unsigned char *)REALLOC(outB, outBsize);
    memset(outB, 0, outBsize);
    if(rule_token(&obj->varstack, val, (unsigned char *)outB, &outBsize) < 0) {
      FREE(outB);
      return -1;
    }
  }

  if(var->go > 0) {
    vm_value_del(obj, var->go);
  }

  ret = varstack->nrbytes;
  var->go = ret;
  if(rule_token(&obj->ast, token, (unsigned char *)&outA, &outAsize) < 0) {
    return -1;
  } else if(r == -2) {
    outB = (unsigned char *)REALLOC(outB, outBsize);
    memset(outB, 0, outBsize);
    if(rule_token(&obj->varstack, val, (unsigned char *)outB, &outBsize) < 0) {
      FREE(outB);
      return -1;
    }
  }

  switch(outB[0]) {
    case VINTEGER: {
      uint16_t size = varstack->nrbytes+sizeof(struct vm_vinteger_t);
      if((varstack->buffer = (unsigned char *)REALLOC(varstack->buffer, size)) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      struct vm_vinteger_t *cpy = (struct vm_vinteger_t *)outB;
      struct vm_vinteger_t *value = (struct vm_vinteger_t *)&varstack->buffer[ret];
      value->type = VINTEGER;
      value->ret = token;
      value->value = cpy->value;

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
      struct vm_vfloat_t *cpy = (struct vm_vfloat_t *)outB;
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
      FREE(outB);
      exit(-1);
    } break;
  }

  if(rule_token(&obj->varstack, val, (unsigned char *)outB, &outBsize) < 0) {
    FREE(outB);
    return -1;
  }

  FREE(outB);
  return 0;
}

static void vm_value_prt(struct rules_t *obj, char *out, uint16_t size) {
  struct rule_stack_t *varstack = (struct rule_stack_t *)obj->userdata;
  uint16_t x = 0, pos = 0;

  for(x=4;x<varstack->nrbytes;x++) {
    switch(varstack->buffer[x]) {
      case VINTEGER: {
        struct vm_vinteger_t *val = (struct vm_vinteger_t *)&varstack->buffer[x];

        uint16_t s_out = 32;
        unsigned char foo[s_out];
        memset(&foo, 0, s_out);

        if(rule_token(&obj->ast, val->ret, (unsigned char *)&foo, &s_out) < 0) {
          return;
        }

        switch(foo[0]) {
          case TVALUE: {
            struct vm_tvalue_t *node = (struct vm_tvalue_t *)foo;
            pos += snprintf(&out[pos], size - pos, "%s = %d", node->token, (int)val->value);
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

        uint16_t s_out = 32;
        unsigned char foo[s_out];
        memset(&foo, 0, s_out);

        if(rule_token(&obj->ast, val->ret, (unsigned char *)&foo, &s_out) < 0) {
          return;
        }

        switch(foo[0]) {
          case TVALUE: {
            struct vm_tvalue_t *node = (struct vm_tvalue_t *)foo;
            pos += snprintf(&out[pos], size - pos, "%s = %g", node->token, a);
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

        uint16_t s_out = 32;
        unsigned char foo[s_out];
        memset(&foo, 0, s_out);

        if(rule_token(&obj->ast, val->ret, (unsigned char *)&foo, &s_out) < 0) {
          return;
        }

        switch(foo[0]) {
          case TVALUE: {
            struct vm_tvalue_t *node = (struct vm_tvalue_t *)foo;
            pos += snprintf(&out[pos], size - pos, "%s = NULL", node->token);
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

static int8_t event_cb(struct rules_t *obj, char *name) {
  struct rules_t *called = NULL;
  uint32_t caller = 0;

  caller = obj->caller;

  if(caller > 0 && name == NULL) {
    called = rules[caller-1];

#ifdef ESP8266
    char str[OUTPUT_SIZE];
    /*LCOV_EXCL_START*/
    memset(&str, 0, OUTPUT_SIZE);
    snprintf((char *)&str, OUTPUT_SIZE, "- continuing with caller #%d", caller);
    Serial.println(str);
    /*LCOV_EXCL_STOP*/
#else
    printf("- continuing with caller #%d\n", caller);
#endif

    obj->caller = 0;

    return rule_run(called, 0);
  } else {
    int8_t nr = rule_by_name(rules, nrrules, name);
    if(nr == -1) {
      return -1;
    }

    called = rules[nr];
    called->caller = obj->nr;

    {
#ifdef ESP8266
      char str[OUTPUT_SIZE];
      /*LCOV_EXCL_START*/
      memset(&str, 0, OUTPUT_SIZE);
      snprintf((char *)&str, OUTPUT_SIZE, "- running event \"%s\" called from caller #%d", name, obj->nr);
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
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  if((void *)mempool >= (void *)MMU_SEC_HEAP) {
    for(y=0;y<len;y++) {
      mmu_set_uint8((void *)&(mempool[txtoffset+y]), (uint8_t)unittests[(*i)].rule[y]);
    }
  } else {
#endif
    for(y=0;y<len;y++) {
      mempool[txtoffset+y] = (uint8_t)unittests[(*i)].rule[y];
    }
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
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
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  if((void *)mempool >= (void *)MMU_SEC_HEAP) {
    for(y=0;y<len;y++) {
      cpytxt[y] = mmu_get_uint8(&((unsigned char *)input.payload)[y]);
    }
  } else {
#endif
    memcpy(cpytxt, input.payload, len);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  }
#endif
  oldoffset = txtoffset;

  while((ret = rule_initialize(&input, &rules, &nrrules, &mem, varstack)) == 0) {
    uint16_t size = 0;
    uint8_t rule_nr = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
    if((void *)mempool >= (void *)MMU_SEC_HEAP) {
      rule_nr = mmu_get_uint8(&rules[nrrules-1]->nr);
      size = mmu_get_uint16(&input.len) - oldoffset;
    } else {
#endif
      rule_nr = rules[nrrules-1]->nr;
      size = input.len - oldoffset;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
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
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
    }
#endif

    {
#ifdef ESP8266
      char str[OUTPUT_SIZE];
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
#ifdef ESP8266
      char str[OUTPUT_SIZE];
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
#ifdef ESP8266
        char str[OUTPUT_SIZE];
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
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
    if((void *)mempool >= (void *)MMU_SEC_HEAP) {
      input.payload = &mempool[mmu_get_uint16(&input.len)];
      oldoffset = mmu_get_uint16(&input.len);
      len = mmu_get_uint16(&input.tot_len);
    } else {
#endif
      input.payload = &mempool[input.len];
      oldoffset = input.len;
      len = input.tot_len;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
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
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
    if((void *)mempool >= (void *)MMU_SEC_HEAP) {
      for(y=0;y<len;y++) {
        cpytxt[y] = mmu_get_uint8(&((unsigned char *)input.payload)[y]);
      }
    } else {
#endif
      memcpy(cpytxt, input.payload, len);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
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
#ifdef ESP8266
    char str[OUTPUT_SIZE];
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

  unsigned char *mempool = (unsigned char *)MALLOC(MEMPOOL_SIZE*2);
  if(mempool == NULL) {
    fprintf(stderr, "OUT_OF_MEMORY\n");
    exit(-1);
  }
  MMU_SEC_HEAP = &mempool[MEMPOOL_SIZE];

  for(i=0;i<nrtests;i++) {
    memset(mempool, 0, MEMPOOL_SIZE*2);
    run_test(&i, &mempool[MEMPOOL_SIZE], MEMPOOL_SIZE);
  }
  FREE(mempool);

  mempool = (unsigned char *)MALLOC(MEMPOOL_SIZE*2);
  if(mempool == NULL) {
    fprintf(stderr, "OUT_OF_MEMORY\n");
    exit(-1);
  }
  MMU_SEC_HEAP = &mempool[MEMPOOL_SIZE];

  for(i=0;i<nrtests;i++) {
    memset(mempool, 0, MEMPOOL_SIZE*2);
    run_test(&i, &mempool[0], MEMPOOL_SIZE);
  }
  FREE(mempool);
}
#endif
