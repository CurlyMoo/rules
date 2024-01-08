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
  int8_t dofail;
} unittests[] = {
  /*
   * Valid rules
   */
  { "if 3 == 3 then $a = 6; end", { { "$a = 6", 100 } }, { { "$a = 6", 100 } }, 0 },
  { "if $a == $a then $a = $a; end", { { "$a = NULL", 96 } }, { { "$a = NULL", 96 } }, 0 },
  { "if (3 == 3) then $a = 6; end", { { "$a = 6", 108 } }, { { "$a = 6", 108 } }, 0 },
  { "if 10 == 10 then $a = 10; end", { { "$a = 10", 100 } }, { { "$a = 10", 100 } }, 0 },
  { "if 100 == 100 then $a = 100; end", { { "$a = 100", 100 } }, { { "$a = 100", 100 } }, 0 },
  { "if 1000 == 1000 then $a = 1000; end", { { "$a = 1000", 100 } }, { { "$a = 1000", 100 } }, 0 },
  { "if 3.5 == 3.5 then $a = 3.5; end", { { "$a = 3.5", 100 } }, { { "$a = 3.5", 100 } }, 0 },
  { "if 33.5 == 33.5 then $a = 33.5; end", { { "$a = 33.5", 100 } }, { { "$a = 33.5", 100 } }, 0 },
  { "if 333.5 == 333.5 then $a = 333.5; end", { { "$a = 333.5", 100 } }, { { "$a = 333.5", 100 } }, 0 },
  { "if 3.335 < 33.35 then $a = 3.335; end", { { "$a = 3.335", 100 } }, { { "$a = 3.335", 100 } }, 0 },
  { "if -10 == -10 then $a = -10; end", { { "$a = -10", 100 } }, { { "$a = -10", 100 } }, 0 },
  { "if -100 == -100 then $a = -100; end", { { "$a = -100", 100 } }, { { "$a = -100", 100 } }, 0 },
  { "if NULL == 3 then $a = 6; end", { { "$a = 6", 96 } }, { { "", 88 } }, 0 },
  { "if 1.1 == 1.1 then $a = 6; end", { { "$a = 6", 100 } }, { { "$a = 6", 100 } }, 0 },
  { "if 1.1 == 1.2 then $a = 6; end", { { "$a = 6", 100 } }, { { "", 92 } }, 0 },
  { "if 4 != 3 then $a = 6; end", { { "$a = 6", 100 } }, { { "$a = 6", 100 } }, 0 },
  { "if (4 != 3) then $a = 6; end", { { "$a = 6", 108 } }, { { "$a = 6", 108 } }, 0 },
  { "if NULL != 3 then $a = 6; end", { { "$a = 6", 96 } }, { { "$a = 6", 96 } }, 0 },
  { "if NULL != NULL then $a = 6; end", { { "$a = 6", 92 } }, { { "$a = 6", 92 } }, 0 },
  { "if 1.2 != 1.1 then $a = 6; end", { { "$a = 6", 100 } }, { { "$a = 6", 100 } }, 0 },
  { "if 1.2 != 1.2 then $a = 6; end", { { "$a = 6", 100 } }, { { "", 92 } }, 0 },
  { "if 1 != 1 then $a = 6; end", { { "$a = 6", 100 } }, { { "", 92 } }, 0 },
  { "if 1.1 >= 1.2 then $a = 6; end", { { "$a = 6", 100 } }, { { "", 92 } }, 0 },
  { "if 1.1 >= 1.1 then $a = 6; end", { { "$a = 6", 100 } }, { { "$a = 6", 100 } }, 0 },
  { "if 2 >= 1.1 then $a = 6; end", { { "$a = 6", 100 } }, { { "$a = 6", 100 } }, 0 },
  { "if 2.1 >= 1 then $a = 6; end", { { "$a = 6", 100 } }, { { "$a = 6", 100 } }, 0 },
  { "if 9 <= -5 then $a = 6; end", { { "$a = 6", 100 } }, { { "", 92 } }, 0 },
  { "if NULL >= 1.2 then $a = 6; end", { { "$a = 6", 96 } }, { { "", 88 } }, 0 },
  { "if NULL >= NULL then $a = 6; end", { { "$a = 6", 92 } }, { { "", 84 } }, 0 },
  { "if 1 > 2 then $a = 6; end", { { "$a = 6", 100} }, { { "", 92 } }, 0 },
  { "if 2 > 1 then $a = 6; end", { { "$a = 6", 100 } }, { { "$a = 6", 100 } }, 0 },
  { "if 2 > 1.1 then $a = 6; end", { { "$a = 6", 100 } }, { { "$a = 6", 100 } }, 0 },
  { "if 1.1 > 1.2 then $a = 6; end", { { "$a = 6", 100 } }, { { "", 92 } }, 0 },
  { "if 1.1 > 1.0 then $a = 6; end", { { "$a = 6", 104 } }, { { "$a = 6", 104 } }, 0 },
  { "if NULL > 1.2 then $a = 6; end", { { "$a = 6", 96 } }, { { "", 88 } }, 0 },
  { "if NULL > NULL then $a = 6; end", { { "$a = 6", 92 } }, { { "", 84 } }, 0 },
  { "if 1.2 <= 1.1 then $a = 6; end", { { "$a = 6", 100 } }, { { "", 92 } }, 0 },
  { "if 1.1 <= 1.1 then $a = 6; end", { { "$a = 6", 100 } }, { { "$a = 6", 100 } }, 0 },
  { "if 1.1 <= 2 then $a = 6; end", { { "$a = 6", 100 } }, { { "$a = 6", 100 } }, 0 },
  { "if 1 <= 2 then $a = 6; end", { { "$a = 6", 100 } }, { { "$a = 6", 100 } }, 0 },
  { "if 2 <= 1 then $a = 6; end", { { "$a = 6", 100 } }, { { "", 92 } }, 0 },
  { "if 1 <= 2.1 then $a = 6; end", { { "$a = 6", 100 } }, { { "$a = 6", 100 } }, 0 },
  { "if 1.2 <= NULL then $a = 6; end", { { "$a = 6", 96 } }, { { "", 88 } }, 0 },
  { "if NULL <= NULL then $a = 6; end", { { "$a = 6", 92 } }, { { "", 84 } }, 0 },
  { "if 2 < 1 then $a = 6; end", { { "$a = 6", 100 } }, { { "", 92 } }, 0 },
  { "if 1 < 2 then $a = 6; end", { { "$a = 6", 100 } }, { { "$a = 6", 100 } }, 0 },
  { "if 1.1 < 2 then $a = 6; end", { { "$a = 6", 100 } }, { { "$a = 6", 100 } }, 0 },
  { "if 1.2 < 1.1 then $a = 6; end", { { "$a = 6", 100 } }, { { "", 92 } }, 0 },
  { "if 1.0 < 1.1 then $a = 6; end", { { "$a = 6", 104 } }, { { "$a = 6", 104 } }, 0 },
  { "if 1.2 < NULL then $a = 6; end", { { "$a = 6", 96 } }, { { "", 88 } }, 0 },
  { "if NULL < NULL then $a = 6; end", { { "$a = 6", 92 } }, { { "", 84 } }, 0 },
  { "if NULL && NULL then $a = -6; end", { { "$a = -6", 92 } }, { { "", 84 } }, 0 },
  { "if 0 && 0 then $a = -6; end", { { "$a = -6", 100 } }, { { "", 92 } }, 0 },
  { "if 1 && 1 then $a = -6; end", { { "$a = -6", 100 } }, { { "$a = -6", 100 } }, 0 },
  { "if 1.1 && 1.2 then $a = -6; end", { { "$a = -6", 100 } }, { { "$a = -6", 100 } }, 0 },
  { "if 0.0 && 1.2 then $a = -6; end", { { "$a = -6", 104 } }, { { "", 96 } }, 0 },
  { "if -1.1 && 1.2 then $a = -6; end", { { "$a = -6", 100 } }, { { "", 92 } }, 0 },
  { "if 1.2 && 0.0 then $a = -6; end", { { "$a = -6", 104 } }, { { "", 96 } }, 0 },
  { "if 1.2 && -1.1 then $a = -6; end", { { "$a = -6", 100 } }, { { "$a = -6", 100 } }, 0 },
  { "if NULL || NULL then $a = -6; end", { { "$a = -6", 92 } }, { { "", 84 } }, 0 },
  { "if 0 || 0 then $a = -6; end", { { "$a = -6", 100 } }, { { "", 92 } }, 0 },
  { "if 1 || 1 then $a = -6; end", { { "$a = -6", 100 } }, { { "$a = -6", 100 } }, 0 },
  { "if 1.1 || 1.2 then $a = -6; end", { { "$a = -6", 100 } }, { { "$a = -6", 100 } }, 0 },
  { "if -0.1 || -0.1 then $a = -6; end", { { "$a = -6", 100 } }, { { "", 92 } }, 0 },
  { "if -0.5 || 1 then $a = -6; end", { { "$a = -6", 100 } }, { { "$a = -6", 100 } }, 0 },
  { "if 0.0 || 1.2 then $a = -6; end", { { "$a = -6", 104 } }, { { "$a = -6", 104 } }, 0 },
  { "if -1.1 || 1.2 then $a = -6; end", { { "$a = -6", 100 } }, { { "$a = -6", 100 } }, 0 },
  { "if 1.2 || 0.0 then $a = -6; end", { { "$a = -6", 104 } }, { { "$a = -6", 104 } }, 0 },
  { "if 1.2 || -1.1 then $a = -6; end", { { "$a = -6", 100 } }, { { "$a = -6", 100 } }, 0 },
  { "if 3 == NULL then $a = 6; end", { { "$a = 6", 96 } }, { { "", 88 } }, 0 },
  { "if (NULL == 3) then $a = 6; end", { { "$a = 6", 104 } }, { { "", 96 } }, 0 },
  { "if (3 == NULL) then $a = 6; end", { { "$a = 6", 104 } }, { { "", 96 } }, 0 },
  { "if @a == 3 then $a = 6; end", { { "$a = 6", 112 } }, { { "", 104 } }, 0 },
  { "if foo#bar == 3 then $a = 6; end", { { "$a = 6", 120 } }, { { "$a = 6", 120 } }, 0 },
  { "if 3 == 3 then @a = 6; end", { { "@a = 6", 100 } }, { { "@a = 6", 100 } }, 0 },
  { "if 3 == 3 then $a = -6; end", { { "$a = -6", 100 } }, { { "$a = -6", 100 } }, 0 },
  { "if 4 != 3 then $a = -6; end", { { "$a = -6", 100 } }, { { "$a = -6", 100 } }, 0 },
  { "if 3 == 3 then $a = 1.1; end", { { "$a = 1.1", 100 } }, { { "$a = 1.1", 100 } }, 0 },
  { "if 3 == 3 then $a = 1 - NULL; end", { { "$a = NULL", 112 } }, { { "$a = NULL", 112 } }, 0 },
  { "if 3 == 3 then $a = 1 - 0.5; end", { { "$a = 0.5", 120 } }, { { "$a = 0.5", 120 } }, 0 },
  { "if 3 == 3 then $a = 0.5 - 0.5; end", { { "$a = 0", 120 } }, { { "$a = 0", 120 } }, 0 },
  { "if 3 == 3 then $a = 0.5 - 1; end", { { "$a = -0.5", 120 } }, { { "$a = -0.5", 120 } }, 0 },
  { "if 3 == 3 then $a = 1 + 2; end", { { "$a = 3", 120 } }, { { "$a = 3", 120 } }, 0 },
  { "if 3 == 3 then $a = 1.5 + 2.5; end", { { "$a = 4", 120 } }, { { "$a = 4", 120 } }, 0 },
  { "if 3 == 3 then $a = 1 * NULL; end", { { "$a = NULL", 112 } }, { { "$a = NULL", 112 } }, 0 },
  { "if 3 == 3 then $a = 1 ^ NULL; end", { { "$a = NULL", 112 } }, { { "$a = NULL", 112 } }, 0 },
  { "if 3 == 3 then $a = 9 % 2; end", { { "$a = 1", 120 } }, { { "$a = 1", 120 } }, 0 },
  { "if 3 == 3 then $a = 9 % NULL; end", { { "$a = NULL", 112 } }, { { "$a = NULL", 112 } }, 0 },
  { "if 3 == 3 then $a = 9.5 % 1.5; end", { { "$a = 0.5", 120 } }, { { "$a = 0.5", 120 } }, 0 },
  { "if 3 == 3 then $a = 10 % 1.5; end", { { "$a = 1.5", 120 } }, { { "$a = 1.5", 120 } }, 0 },
  { "if 3 == 3 then $a = 1.5 % 10; end", { { "$a = 1", 120 } }, { { "$a = 1", 120 } }, 0 },
  { "if 1 == 1 then $a = 1 + 2 + 3; end", { { "$a = 6", 140 } }, { { "$a = 6", 140 } }, 0 },
  { "if 1 == 1 == 1 then $a = 1; end", { { "$a = 1", 120 } }, { { "$a = 1", 120 } }, 0 },
  { "if 1 == 1 == 2 then $a = 1; end", { { "$a = 1", 120 } }, { { "", 112 } }, 0 },
  { "if 1 == 1 then $a = 1; $a = $a + 2; end", { { "$a = 3", 140 } }, { { "$a = 3", 140 } }, 0 },
  { "if 1 == 1 then $a = 6; $a = $a + 2 + $a / 3; end", { { "$a = 10", 180 } }, { { "$a = 10", 180 } }, 0 },
  { "if 1 == 1 then $a = 6; $a = ($a + 2 + $a / 3); end", { { "$a = 10", 188 } }, { { "$a = 10", 188 } }, 0 },
  { "if 1 == 1 then $a = NULL / 1; end", { { "$a = NULL", 112 } }, { { "$a = NULL", 112 } }, 0 },
  { "if 1 == 1 then $a = 2.5 / 7.5; end", { { "$a = 0.333333", 120 } }, { { "$a = 0.333333", 120 } }, 0 },
  { "if 1 == 1 then $a = 5 / 2.5; end", { { "$a = 2", 120 } }, { { "$a = 2", 120 } }, 0 },
  { "if 1 == 1 then $a = 2.5 / 5; end", { { "$a = 0.5", 120 } }, { { "$a = 0.5", 120 } }, 0 },
  { "if 1 == 1 then $a = 2.5 * 2.5; end", { { "$a = 6.25", 120 } }, { { "$a = 6.25", 120 } }, 0 },
  { "if 12 == 1 then $a = 1 + 2 * 3; end", { { "$a = 7", 140 } }, { { "", 132 } }, 0 },
  { "if 1 == 1 then $a = 3 * 1 + 2 * 3; end", { { "$a = 9", 160 } }, { { "$a = 9", 160 } }, 0 },
  { "if 1 == 1 then $a = (3 * 1 + 2 * 3); end", { { "$a = 9", 168 } }, { { "$a = 9", 168 } }, 0 },
  { "if 1 == 1 then $a = 1 * 2 + 3; end", { { "$a = 5", 140 } }, { { "$a = 5", 140 } }, 0 },
  { "if 1 == 1 then $a = 1 * 100 ^ 2; end", { { "$a = 10000", 140 } }, { { "$a = 10000", 140 } }, 0 },
  { "if 1 == 1 then $a = 1 * 1.1 ^ 2; end", { { "$a = 1.21", 140 } }, { { "$a = 1.21", 140 } }, 0 },
  { "if 1 == 1 then $a = 1 * 1.1 ^ 1.1; end", { { "$a = 1.11053", 140 } }, { { "$a = 1.11053", 140 } }, 0 },
  { "if 1 == 1 then $a = 1 * 100 ^ 1.1; end", { { "$a = 158.489", 140 } }, { { "$a = 158.489", 140 } }, 0 },
  { "if 1 == 1 then $a = 1 + 2 * 3 / 4; end", { { "$a = 2.5", 160 } }, { { "$a = 2.5", 160 } }, 0 },
  { "if 1 == 1 then $a = 1 + 2 * 3 / 4 ^ 2; end", { { "$a = 1.375", 180 } }, { { "$a = 1.375", 180 } }, 0 },
  { "if 1 == 1 then $a = 1 + 4 ^ 2 ^ 1; end", { { "$a = 17", 160 } }, { { "$a = 17", 160 } }, 0 },
  { "if 1 == 1 then $a = (1 + 4 ^ 2 ^ 1); end", { { "$a = 17", 168 } }, { { "$a = 17", 168 } }, 0 },
  { "if 1 == 1 then $a = (1 + 2 * 3 / 4 ^ 2); end", { "$a = 1.375", 188 }, { "$a = 1.375", 188 }, 0 },
  { "if 1 == 1 then $a = (1 + 2 * 3 / 4 ^ 2 ^ 1 * 3); end", { "$a = 2.125", 228 }, { "$a = 2.125", 228 }, 0 },
  { "if 1 == 1 then $a = (1 + 2 * 3 / 4 ^ 2 ^ 1 * 3 ^ 4); end", { "$a = 31.375", 248 }, { "$a = 31.375", 248 }, 0 },
  { "if 1 == 1 then $a = 1 + 2 * 3 / 4 ^ 2 ^ 1 * 3 ^ 4; end", { "$a = 31.375", 240 }, { "$a = 31.375", 240 }, 0 },
  { "if 1 == 1 then $a = (1 + 2) * 3; end", { "$a = 9", 148 }, { "$a = 9", 148 }, 0 },
  { "if 1 == 1 then $a = (1 + 2 * 3); end", { "$a = 7", 148 }, { "$a = 7", 148 }, 0 },
  { "if 1 == 1 then $a = 3 * (1 + 2); end", { "$a = 9", 148 }, { "$a = 9", 148 }, 0 },
  { "if 1 == 1 then $a = 3 * (1 + 2) * 2; end", { "$a = 18", 168 }, { "$a = 18", 168 }, 0 },
  { "if 1 == 1 then $a = 3 * (1 + 3) ^ 2; end", { "$a = 48", 168 }, { "$a = 48", 168 }, 0 },
  { "if 1 == 1 then $a = 7 + 6 * 5 / (4 - 3) ^ 2 ^ 1; end", { "$a = 37", 228 }, { "$a = 37", 228 }, 0 },
  { "if 1 == 1 then $a = 3 * (1 + 2) + 2; end", { "$a = 11", 168 }, { "$a = 11", 168 }, 0 },
  { "if 1 == 1 then $a = 3 + (1 + 2) * 2; end", { "$a = 9", 168 }, { "$a = 9", 168 }, 0 },
  { "if 1 == 1 then $a = 3 * (1 + 2 / 2) + 3; end", { "$a = 9", 188 }, { "$a = 9", 188 }, 0 },
  { "if 1 == 1 then $a = 3 * ((1 + 2) / 2) + 2; end", { "$a = 6.5", 196 }, { "$a = 6.5", 196 }, 0 },
  { "if 1 == 1 then $a = 3 * ((1 + 2) / 2 + 3); end", { "$a = 13.5", 196 }, { "$a = 13.5", 196 }, 0 },
  { "if 1 == 1 then $a = 3 * ((1 + 2) / (2 + 3)); end", { "$a = 1.8", 204 }, { "$a = 1.8", 204 }, 0 },
  { "if 1 == 1 then $a = 3 * ((((1 + 2) / (2 + 3)))); end", { "$a = 1.8", 220 }, { "$a = 1.8", 220 }, 0 },
  { "if $a == $a then $a = $a * (((($a + $a) / ($a + $a)))); end", { "$a = NULL", 216 }, { "$a = NULL", 216 }, 0 },
  { "if 1 == 1 then $a = 3 * ((1 + 2) / (2 + 3)) + 2; end", { "$a = 3.8", 224 }, { "$a = 3.8", 224 }, 0 },
  { "if 1 == 1 then $a = (1 == 1 && 1 == 0) || 5 >= 4; end", { "$a = 1", 208 }, { "$a = 1", 208 }, 0 },
  { "if 1 == 1 then $a = (1 == 1 && 1 == 0) || 3 >= 4; end", { "$a = 0", 208 }, { "$a = 0", 208 }, 0 },
  { "if 1 == 1 then $a = 3; end", { "$a = 3", 100 }, { "$a = 3", 100 }, 0 },
  { "if 1 == 1 then $a = 3.1; $b = $a; end", { "$a = 3.1$b = 3.1", 140 }, { "$a = 3.1$b = 3.1", 140 }, 0 },
  { "if 1 == 1 then $a = $a + 1; end", { "$a = NULL", 116 }, { "$a = NULL", 116 }, 0 },
  { "if 1 == 1 then $a = max(foo#bar); end", { "$a = 3", 132 }, { "$a = 3", 132 }, 0 },
  { "if 1 == 1 then $a = coalesce($a, 0) + 1; end", { "$a = 1", 144 }, { "$a = 1", 144 }, 0 },
  { "if 1 == 1 then $a = coalesce($a, 1.1) + 1; end", { "$a = 2.1", 144 }, { "$a = 2.1", 144 }, 0 },
  { "if 1 == 1 then if $a == NULL then $a = 0; end $a = $a + 1; end", { "$a = 1", 188 }, { "$a = 1", 188 }, 0 },
  { "if 1 == 1 then $a = 1; if $a == NULL then $a = 0; end $a = $a + 1; end", { "$a = 1", 204 }, { "$a = 2", 204 }, 0 },
  { "if 1 == 1 then $a = 1; if $a == 1 then $a = NULL; end end", { "$a = NULL", 164 }, { "$a = NULL", 164 }, 0 },
  { "if 1 == 1 then $a = 1; $a = NULL; end", { "$a = NULL", 112 }, { "$a = NULL", 112 }, 0 },
  { "if 1 == 2 then $a = 3; else $a = 4; end", { "$a = 4", 128 }, { "$a = 4", 128 }, 0 },
  { "if 1 == 1 then $a = 3; else $a = 4; end", { "$a = 4", 128 }, { "$a = 3", 128 }, 0 },
  { "if (1 + 1) == 1 then $a = 3; else $a = 4; end", { "$a = 4", 156 }, { "$a = 4", 156 }, 0 },
  { "if 1 == 2 || 3 >= 4 then $a = max(1, 2); end", { "$a = 2", 164 }, { "", 156 }, 0 },
  { "if 1 == 2 || 3 >= 4 then $a = min(3, 1, 2); end", { "$a = 1", 172 }, { "", 164 }, 0 },
  { "if 1 == 2 || 3 >= 4 then $a = 1; end", { "$a = 1", 140 }, { "", 132 }, 0 },
  { "if 1 == 2 || 3 >= 4 || 5 == 6 then $a = max(1, 3, 2); else $b = 9; end", { "$a = 3$b = 9", 260 }, { "$b = 9", 252 }, 0 },
  { "if 1 == 1 then $a = 1; $b = 2; end", { "$a = 1$b = 2", 140 }, { "$a = 1$b = 2", 140 }, 0 },
  { "if 1 == 1 then $a = 1; $b = ($a + 3) * 3; end", { "$a = 1$b = 12", 188 }, { "$a = 1$b = 12", 188 }, 0 },
  { "if 1 == 1 then $a = 1; $b = $a + 3 * 3; end", { "$a = 1$b = 10", 180 }, { "$a = 1$b = 10", 180 }, 0 },
  { "if 1 == 1 then if 5 == 6 then $a = 1; end $a = 2; end", { "$a = 2", 172 }, { "$a = 2", 172 }, 0 },
  { "if 1 == 1 then if 5 == 6 then $a = 1; end if 1 == 3 then $b = 3; end $a = 2; end", { "$b = 3$a = 2", 260 }, { "$a = 2", 252 }, 0 },
  { "if 1 == 1 then $a = 1; $b = $a; end", { "$a = 1$b = 1", 140 }, { "$a = 1$b = 1", 140 }, 0 },
  { "if 1 == 1 then $a = 1; if 5 >= 4 then $a = 3; end $b = $a; end", { "$a = 3$b = 3", 208 }, { "$a = 3$b = 3", 208 }, 0 },
  { "if 1 == 1 then $a = 1; if $a == 1 then $a = 3; end end", { "$a = 3", 172 }, { "$a = 3", 172 }, 0 },
  { "if (1 == 1 && 1 == 0) || 5 >= 4 then $a = 1; end", { "$a = 1", 188 }, { "$a = 1", 188 }, 0 },
  { "if 3 == 3 then $a = max(1); end", { "$a = 1", 112 }, { "$a = 1", 112 }, 0 },
  { "if 3 == 3 then $a = max(NULL, 1); end", { "$a = 1", 120 }, { "$a = 1", 120 }, 0 },
  { "if 3 == 3 then $a = max(1, NULL); end", { "$a = 1", 120 }, { "$a = 1", 120 }, 0 },
  { "if 3 == 3 then $b = 2; $a = max($b, 1); end", { "$b = 2$a = 2", 164 }, { "$b = 2$a = 2", 164 }, 0 },
  { "if 3 == 3 then $a = max(1, 2); end", { "$a = 2", 124 }, { "$a = 2", 124 }, 0 },
  { "if 3 == 3 then $a = max(1, 2, 3, 4); end", { "$a = 4", 144 }, { "$a = 4", 144 }, 0 },
  { "if 3 == 3 then $a = max(1, 4, 5, 3, 2); end", { "$a = 5", 152 }, { "$a = 5", 152 }, 0 },
  { "if 3 == 3 then $a = max(max(1, 4), 2); end", { "$a = 4", 148 }, { "$a = 4", 148 }, 0 },
  { "if 3 == 3 then $a = max(1, 2) * 3; end", { "$a = 6", 144 }, { "$a = 6", 144 }, 0 },
  { "if 3 == 3 then $a = max(1, 2) * max(3, 4); end", { "$a = 8", 168 }, { "$a = 8", 168 }, 0 },
  { "if 3 == 3 then $a = max(max(1, 2), (1 * max(1, 3) ^ 2)); end", { "$a = 9", 220}, { "$a = 9", 220 }, 0 },
  { "if 3 == 3 then $b = 1; $a = max($b + 1); end", { "$b = 1$a = 2", 172 }, { "$b = 1$a = 2", 172 }, 0 },
  { "if 3 == 3 then $b = 1; $a = max($b + 1) * 3; end", { "$b = 1$a = 6", 192 }, { "$b = 1$a = 6", 192 }, 0 },
  { "if max(1, 3) == 3 then $a = 1; end", { "$a = 1", 124 }, { "$a = 1", 124 }, 0 },
  { "if max(1, 3) == max(1, 3) then $a = 1; end", { "$a = 1", 148 }, { "$a = 1", 148 }, 0 },
  { "if max(1, 12000) == max(1, 3) then $a = 1; end", { "$a = 1", 148 }, { "", 140 }, 0 },
  { "if max(1, 12222.5555) == max(1, 3) then $a = 1; end", { "$a = 1", 148 }, { "", 140 }, 0 },
  { "if 3 == 3 then max(1, 2); end", { "", 96 }, { "", 96 }, 0 },
  { "if 3 == 3 then $a = 1; $b = $a; max(1, 2); end", { "$a = 1$b = 1", 172 }, { "$a = 1$b = 1", 172 }, 0 },
  { "if 3 == 3 then $a = max(1 + 1, 2 + 2); end", { "$a = 4", 164 }, { "$a = 4", 164 }, 0 },
  { "if 1 == 1 then $a = coalesce($b, 0); end  ", { { "$b = NULL$a = 0", 140 } }, { { "$b = NULL$a = 0", 140 } }, 0 }, // FIXME
  { "if 1 == 1 then $a = round(3.5); end  ", { { "$a = 3", 112 } }, { { "$a = 3", 112 } }, 0 },
  { "if 1 == 1 then $a = round(5); end  ", { { "$a = 5", 112 } }, { { "$a = 5", 112 } }, 0 },
  { "if 1 == 1 then $a = round(-5); end  ", { { "$a = -5", 112 } }, { { "$a = -5", 112 } }, 0 },
  { "if 1 == 1 then $a = round(-3.5); end  ", { { "$a = -3", 112 } }, { { "$a = -3", 112 } }, 0 },
  { "if 1 == 1 then $a = ceil(3.5); end  ", { { "$a = 4", 112 } }, { { "$a = 4", 112 } }, 0 },
  { "if 1 == 1 then $a = ceil(5); end  ", { { "$a = 5", 112 } }, { { "$a = 5", 112 } }, 0 },
  { "if 1 == 1 then $a = ceil(-5); end  ", { { "$a = -5", 112 } }, { { "$a = -5", 112 } }, 0 },
  { "if 1 == 1 then $a = ceil(-3.5); end  ", { { "$a = -3", 112 } }, { { "$a = -3", 112 } }, 0 },
  { "if 1 == 1 then $a = floor(3.5); end  ", { { "$a = 3", 112 } }, { { "$a = 3", 112 } }, 0 },
  { "if 1 == 1 then $a = floor(5); end  ", { { "$a = 5", 112 } }, { { "$a = 5", 112 } }, 0 },
  { "if 1 == 1 then $a = floor(-5); end  ", { { "$a = -5", 112 } }, { { "$a = -5", 112 } }, 0 },
  { "if 1 == 1 then $a = floor(-3.5); end  ", { { "$a = -4", 112 } }, { { "$a = -4", 112 } }, 0 },
  { "if 3 == 3 then $a = max((1 + 3), 2); end", { "$a = 4", 152 }, { "$a = 4", 152 }, 0 },
  { "if 3 == 3 then $a = max((1 + (3 * 3)), 2); end", { "$a = 10", 180 }, { "$a = 10", 180 }, 0 },
  { "if 3 == 3 then $a = max(1 + 3 * 3, 3 * 4); end", { "$a = 12", 184 }, { "$a = 12", 184 }, 0 },
  { "if 3 == 3 then $a = max(((2 + 3) * 3), (3 * 4)); end", { "$a = 15", 208 }, { "$a = 15", 208 }, 0 },
  { "if 3 == 3 then $a = (max(1, 2) + 2) * 3; end", { "$a = 12", 172 }, { "$a = 12", 172 }, 0 },
  { "if 1 == 1 then $a = max(0, 1) + max(1, 2) * max(2, 3) / max(3, 4) ^ max(1, 2) ^ max(0, 1) * max(2, 3) ^ max(3, 4); end", { "$a = 31.375", 432 }, { "$a = 31.375", 432 }, 0 },
  { "if 3 == 3 then $a = 2; $b = max((($a + 3) * 3), (3 * 4)); end", { "$a = 2$b = 15", 248 }, { "$a = 2$b = 15", 248 }, 0 },
  { "if 3 == 3 then $a = 2; $b = max((max($a + 3) * 3), (3 * 4)); end", { "$a = 2$b = 15", 252 }, { "$a = 2$b = 15", 252 }, 0 },
  { "if 1 == 1 then $a = max(1 * 2, (min(5, 6) + 1) * 6); end", { { "$a = 36", 216 } }, { { "$a = 36", 216 } }, 0 },
  { "if 1 == 1 then $a = 1; else $a = 2; end", { "$a = 2", 128 }, { "$a = 1", 128 }, 0 },
  { "if 1 == 2 then $a = 1; elseif 2 == 2 then $a = 3; else $a = 2; end", { "$a = 2", 208 }, { "$a = 3", 208 }, 0 },
  { "if 1 == 2 then $a = 1; elseif 2 == 2 then $a = 2; end", { "$a = 2", 180 }, { "$a = 2", 180 }, 0 },
  { "if 1 == 2 then $a = 1; elseif 3 == 2 then $a = 2; elseif 4 == 4 then $a = 4; end", { "$a = 4", 260 }, { "$a = 4", 260}, 0 },
  { "if 1 == 2 then $a = 1; elseif 3 == 2 then if 1 > 4 then $a = 2; else $a = 1; end end", { "$a = 1", 260 }, { "", 252 }, 0 },
  { "if 1 == 2 then $a = 1; if 2 < 3 then $a = 5; end elseif 3 == 2 then if 1 > 4 then $a = 2; else $a = 1; end end", { "$a = 1", 332 }, { "", 324 }, 0 },
  { "if 1 == 1 then if 2 == 2 then $a = 1; end else $a = 2; end", { "$a = 2", 180 }, { "$a = 1", 180 }, 0 },
  { "if (1 == 1) || 5 >= 4 then $a = 1; if 6 == 5 then $a = 2; end $a = $a + 3; $b = (3 + $a) * 2; else $a = 7; end", { "$b = 16$a = 7", 372 }, { "$a = 4$b = 14", 372 }, 0 },
  { "if (1 == 1) || 5 >= 4 then $a = 1; if 6 == 5 then $a = 2; end $a = $a + 3; $b = (3 + $a) * 2; $b = 3; else $a = 7; end", { "$b = 3$a = 7", 388 }, { "$a = 4$b = 3", 388 }, 0 },
  { "if (1 == 1 && 1 == 0) || 5 >= 4 then $a = 1; if 6 == 5 then $a = 2; end $a = $a + 3; $b = (3 + $a * 5 + 3 * 1) * 2; @c = 5; else if 2 == 2 then $a = 6; else $a = 7; end end", { "$b = 62@c = 5$a = 7", 588 }, { "$a = 4$b = 52@c = 5", 588 }, 0 },
  { "if 3 == 3 then $a = 6; end if 3 == 3 then $b = 3; end  ", { { "$a = 6", 100 }, { "$b = 3", 100 } }, { { "$a = 6", 100 }, { "$b = 3" , 100 } }, 0 },
  { "   if 3 == 3 then $a = 6; end    if 3 == 3 then $b = 6; end               if 3 == 3 then $c = 6; end", { { "$a = 6", 100 }, { "$b = 6", 100 }, { "$c = 6", 100 } }, { { "$a = 6", 100 }, { "$b = 6", 100 }, { "$c = 6", 100 } }, 0 },
  { "on foo then max(1, 2); end", { "", 72 }, { "", 72 }, 0 },
  { "on foo then $a = 6; end", { "$a = 6", 76 }, { "$a = 6", 76 }, 0 },
  { "on foo then $a = 6; $b = 3; end", { "$a = 6$b = 3", 116 }, { "$a = 6$b = 3", 116 }, 0 },
  { "on foo then @a = 6; end", { { "@a = 6", 76 } }, { { "@a = 6", 76 } }, 0 },
  { "on foo then $a = 1 + 2; end", { { "$a = 3", 96 } }, { { "$a = 3", 96 } }, 0 },
  { "on foo then if 5 == 6 then $a = 1; end $a = 2; end", { "$a = 2", 148 }, { "$a = 2", 148 }, 0 },
  { "on foo then if 5 == 6 then $a = 1; end if 1 == 3 then $b = 3; end $a = 2; end", { "$b = 3$a = 2", 236 }, { "$a = 2", 228 }, 0 },
  { "on foo then $a = 6; end if 3 == 3 then $b = 3; end  ", { { "$a = 6", 76 }, { "$b = 3" , 100 } }, { { "$a = 6", 76 }, { "$b = 3" , 100 } }, 0 },
  { "on foo then $a = 6; end if 3 == 3 then foo(); $b = 3; end  ", { { "$a = 6", 76 }, { "$b = 3", 120 } }, { { "$a = 6", 76 }, { "$b = 3", 120 } }, 0 },
  { "on foo then $a = coalesce($b, 0); end  ", { { "$b = NULL$a = 0", 116 } }, { { "$b = NULL$a = 0", 116 } }, 0 }, // FIXME
  { "on foo then if 1 == 2 then $a = 1; elseif 2 == 2 then $a = 3; else $a = 2; end end", { "$a = 2", 236 }, { "$a = 3", 236 }, 0 },
  { "on foo then if 2 == 2 then $c = 1; elseif 3 == 3 then $b = max(1); end end", { "$c = 1$b = 1", 240 }, { "$c = 1", 232 } },{ "if 3 == 3 then $a = 6; end if 3 == 3 then $b = 3; end  ", { { "$a = 6", 100 }, { "$b = 3", 100 } }, { { "$a = 6", 100 }, { "$b = 3" , 100 } }, 0 },
  { "on foo then if 3 == 3 then $a = 6; elseif 3 == 3 then $b = 1; end end on bar then if 3 == 3 then $b = 3; end end", { { "$a = 6$b = 1", 228 }, { "$b = 3", 128 } }, { { "$a = 6", 220 }, { "$b = 3" , 128 } }, 0 },

  /*
   * Invalid rules
   */
  { "", { { NULL, 0 } }, { { NULL, 0 } }, 1 },
  { "foo", { { NULL, 0 } }, { { NULL, 0 } }, -1 },
  { "foo(", { { NULL, 0 } }, { { NULL, 0 } }, -1 },
  { "1 == 1", { { NULL, 0 } }, { { NULL, 0 } }, -1 },
  { "on foo do", { { NULL, 0 } }, { { NULL, 0 } }, -1 },
  { "if foo then", { { NULL, 0 } }, { { NULL, 0 } }, -1 },
  { "if 1.1.1 == 1 then", { { NULL, 0 } }, { { NULL, 0 } }, -1 },
  { "if 1 == 1 then end", { { NULL, 0 } }, { { NULL, 0 } }, -1 },
  { "if 1 == ) then $a = 1", { { NULL, 0 } }, { { NULL, 0 } }, -1 },
  { "if 1 == ) then $a = 1 end", { { NULL, 0 } }, { { NULL, 0 } }, -1 },
  { "if 1 == 1 then $a = 1 end", { { NULL, 0 } }, { { NULL, 0 } }, -1 },
  { "elseif 1 == 1 then $a = 1; end", { { NULL, 0 } }, { { NULL, 0 } }, -1 },
  { "if (1 == 1 then $a = 1; end", { { NULL, 0 } }, { { NULL, 0 } }, -1 },
  { "if 1 == 1) then $a = 1; end", { { NULL, 0 } }, { { NULL, 0 } }, -1 },
  { "if () then $a = 1; end", { { NULL, 0 } }, { { NULL, 0 } }, -1 },
  { "if ( == ) then $a = 1; end", { { NULL, 0 } }, { { NULL, 0 } }, -1 },
  { "if 1 2 then $a = 1; end", { { NULL, 0 } }, { { NULL, 0 } }, -1 },
  { "on foo end", { { NULL, 0 } }, { { NULL, 0 } }, -1 },
  { "on foo then max(1, 2) end", { { NULL, 0 } }, { { NULL, 0 } }, -1 },
  { "on foo then bar(); end  ", { { NULL, 0 } }, { { NULL, 0 } }, -1 },
  { "if max(1, 2); max(1, 2); then $a = 1; end", { { NULL, 0 } }, { { NULL, 0 } }, -1 },
  { "if 1 == 1 then max(1, 2) end", { { NULL, 0 } }, { { NULL, 0 } }, -1 },
  { "on foo then $a = 1; max(1, 2) end", { { NULL, 0 } }, { { NULL, 0 } }, -1 },
  { "if 1 == 1 then $a = 1; max(1, 2) end", { { NULL, 0 } }, { { NULL, 0 } }, -1 },
  { "on foo then max(1, 2) == 1; end", { { NULL, 0 } }, { { NULL, 0 } }, -1 },
  { "if 1 == 1 then max(1, 2) == 1; end", { { NULL, 0 } }, { { NULL, 0 } }, -1 },
  { "if if == 1 then $a == 1; end", { { NULL, 0 } }, { { NULL, 0 } }, -1 },
  { "on foo then 1; end", { { NULL, 0 } }, { { NULL, 0 } }, -1 },
  { "if 1 == 1 then 1; end", { { NULL, 0 } }, { { NULL, 0 } }, -1 },
  { "if on foo then $a = 1; end", { { NULL, 0 } }, { { NULL, 0 } }, -1 },
  { "on foo then on foo then $a = 1; end end", { { NULL, 0 } }, { { NULL, 0 } }, -1 },
  { "on foo then foo then $a = 1; end end", { { NULL, 0 } }, { { NULL, 0 } }, -1 },
  { "on foo then on foo $a = 1; end end", { { NULL, 0 } }, { { NULL, 0 } }, -1 },
  { "on foo then on then $a = 1; end end", { { NULL, 0 } }, { { NULL, 0 } }, -1 },
  { "on if 1 == 1 then $a = 1; end", { { NULL, 0 } }, { { NULL, 0 } }, -1 },
  { "if max == 1 then $a = 1; end", { { NULL, 0 } }, { { NULL, 0 } }, -1 },
  { "if 1 == 1 then $a = 1; foo() end", { { NULL, 0 } }, { { NULL, 0 } }, -1 },
  { "if 1 == 1 then max(1, 2 end", { { NULL, 0 } }, { { NULL, 0 } }, -1 },
  { "if 1 == 1 then max(1, == ); end", { { NULL, 0 } }, { { NULL, 0 } }, -1 },
  { "if (1 + 1) 1 then $a = 3; end", { { NULL, 0 } }, { { NULL, 0 } }, -1 },
  { "if 1 == 1 then NULL; end", { { NULL, 0 } }, { { NULL, 0 } }, -1 },
  { "if 1 == 1 then $a = max(1, 2)NULL; end", { { NULL, 0 } }, { { NULL, 0 } }, -1 },
  { "if 1 == 1 then $a = ); end", { { NULL, 0 } }, { { NULL, 0 } }, -1 },
  { "if 1 == 1 then $a = $a $a; end", { { NULL, 0 } }, { { NULL, 0 } }, -1 },
  { "on foo then bar() $a = 1; end", { { NULL, 0 } }, { { NULL, 0 } }, -1 },
  { "if ( ; == 1 ) then $a = 1; end", { { NULL, 0 } }, { { NULL, 0 } }, -1 },
  { "if 1 == 2 then $a = 1; else $a = 3; elseif $a = 2; end", { { NULL, 0 } }, { { NULL, 0 } }, -1 },
  { "on foo then elseif $a = 2; else $a = 1; end", { { NULL, 0 } }, { { NULL, 0 } }, -1 },
  { "on foo then if 3 == 3 then $a = 6; elseif 3 == 3 then $b = 1; end end on bar then if 3 == 3 then foo(); end end", { { "$a = 6$b = 1", 228 }, { "$b = 3", 0 } }, { { "$a = 6", 220 }, { "$b = 3" , 0 } }, -1 },

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

static int8_t is_event(char *text, uint16_t size) {
  if(size == 3 &&
    (strnicmp(text, "foo", 3) == 0 || strnicmp(text, "bar", 3) == 0)) {
    return 0;
  }
  return -1;
}

static unsigned char *vm_value_get(struct rules_t *obj, uint16_t token) {
  struct rule_stack_t *varstack = (struct rule_stack_t *)obj->userdata;
  uint16_t ret = 0;

  unsigned char *out = NULL;

  if(rule_token(&obj->ast, token, &out) < 0) {
    return NULL;
  }

  if(out[0] != TVALUE) {
    FREE(out);
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
    FREE(out);
    return (unsigned char *)&vinteger;
  } else if(var->token[0] == '@') {
    memset(&vinteger, 0, sizeof(struct vm_vinteger_t));
    vinteger.type = VINTEGER;
    vinteger.value = 5;
    FREE(out);
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

      if(rule_token(&obj->ast, token, &out) < 0) {
        FREE(out);
        return NULL;
      }

      varstack->nrbytes = size;
      varstack->bufsize = size;
    }

    FREE(out);
    return &varstack->buffer[ret];
  }

  FREE(out);
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
          unsigned char *out = NULL;

          if(rule_token(&obj->ast, node->ret, &out) < 0) {
            return -1;
          }

          struct vm_tvalue_t *tmp = (struct vm_tvalue_t *)out;
          tmp->go = x;

          if(rule_token(&obj->ast, node->ret, &out) < 0) {
            FREE(out);
            return -1;
          }
          FREE(out);
        }

        x += sizeof(struct vm_vinteger_t)-1;
      } break;
      case VFLOAT: {
        struct vm_vfloat_t *node = (struct vm_vfloat_t *)&varstack->buffer[x];
        if(node->ret > 0) {
          unsigned char *out = NULL;

          if(rule_token(&obj->ast, node->ret, &out) < 0) {
            return -1;
          }

          struct vm_tvalue_t *tmp = (struct vm_tvalue_t *)out;
          tmp->go = x;

          if(rule_token(&obj->ast, node->ret, &out) < 0) {
            FREE(out);
            return -1;
          }
          FREE(out);
        }
        x += sizeof(struct vm_vfloat_t)-1;
      } break;
      case VNULL: {
        struct vm_vnull_t *node = (struct vm_vnull_t *)&varstack->buffer[x];
        if(node->ret > 0) {
          unsigned char *out = NULL;

          if(rule_token(&obj->ast, node->ret, &out) < 0) {
            return -1;
          }

          struct vm_tvalue_t *tmp = (struct vm_tvalue_t *)out;
          tmp->go = x;

          if(rule_token(&obj->ast, node->ret, &out) < 0) {
            FREE(out);
            return -1;
          }
          FREE(out);
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

  unsigned char *outA = NULL;

  if(rule_token(&obj->ast, token, &outA) < 0) {
    return -1;
  }

  if(outA[0] != TVALUE) {
    FREE(outA);
    return -1;
  }

  struct vm_tvalue_t *var = (struct vm_tvalue_t *)outA;

#ifdef DEBUG
  printf(". %s %d %d\n", __FUNCTION__, __LINE__, val);
#endif

  unsigned char *outB = NULL;

  if(rule_token(obj->varstack, val, &outB) < 0) {
    return -1;
  }

  if(var->go > 0) {
    vm_value_del(obj, var->go);
  }

  ret = varstack->nrbytes;
  var->go = ret;

  if(rule_token(&obj->ast, token, &outA) < 0) {
    FREE(outA);
    FREE(outB);
    return -1;
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
      FREE(outA);
      FREE(outB);
      exit(-1);
    } break;
  }

  FREE(outA);
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

        unsigned char *foo = NULL;

        if(rule_token(&obj->ast, val->ret, &foo) < 0) {
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
        FREE(foo);
        x += sizeof(struct vm_vinteger_t)-1;
      } break;
      case VFLOAT: {
        struct vm_vfloat_t *val = (struct vm_vfloat_t *)&varstack->buffer[x];
        float a = 0;
        uint322float(val->value, &a);

        unsigned char *foo = NULL;

        if(rule_token(&obj->ast, val->ret, &foo) < 0) {
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
        FREE(foo);
        x += sizeof(struct vm_vfloat_t)-1;
      } break;
      case VNULL: {
        struct vm_vnull_t *val = (struct vm_vnull_t *)&varstack->buffer[x];

        unsigned char *foo = NULL;

        if(rule_token(&obj->ast, val->ret, &foo) < 0) {
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
        FREE(foo);
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
  int8_t nr = rule_by_name(rules, nrrules, name);
  if(nr == -1) {
    return -1;
  }

  obj->ctx.go = rules[nr];
  rules[nr]->ctx.ret = obj;

  return 1;
}

void run_test(int *i, unsigned char *mempool, uint16_t size) {

  if(*i == 0) {
#ifdef ESP8266
  Serial.printf("[ %-*s                    %-*s ]\n", 24, " ", 25, " ");
  Serial.printf("[ %-*s Running regular test %-*s ]\n", 22, " ", 23, " ");
  Serial.printf("[ %-*s                    %-*s ]\n", 24, " ", 25, " ");
#else
  printf("[ %-*s                    %-*s ]\n", 24, " ", 25, " ");
  printf("[ %-*s Running regular test %-*s ]\n", 22, " ", 25, " ");
  printf("[ %-*s                    %-*s ]\n", 24, " ", 25, " ");
#endif
  }

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
      assert(mmu_get_uint16(&rules[nrrules-1]->varstack->nrbytes) >= 0);
      assert(mmu_get_uint16(&rules[nrrules-1]->varstack->bufsize) >= 0);
      assert(mmu_get_uint16(&rules[nrrules-1]->varstack->nrbytes) >= 0);
      assert(mmu_get_uint16(&rules[nrrules-1]->varstack->bufsize) >= 0);
      assert(mmu_get_uint16(&rules[nrrules-1]->varstack->nrbytes) <= mmu_get_uint16(&rules[nrrules-1]->varstack->bufsize));
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
      assert(rules[nrrules-1]->varstack->nrbytes >= 0);
      assert(rules[nrrules-1]->varstack->bufsize >= 0);
      assert(rules[nrrules-1]->varstack->nrbytes >= 0);
      assert(rules[nrrules-1]->varstack->bufsize >= 0);
      assert(rules[nrrules-1]->varstack->nrbytes <= rules[nrrules-1]->varstack->bufsize);
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
    printf("AST stack is\t%3d bytes, local stack is\t%3d bytes, variable stack is\t%3d bytes\n", rules[nrrules-1]->ast.nrbytes, rules[nrrules-1]->varstack->nrbytes, (varstack->nrbytes));
    printf("AST bufsize is\t%3d bytes, local bufsize is\t%3d bytes, variable bufsize is\t%3d bytes\n", rules[nrrules-1]->ast.bufsize, rules[nrrules-1]->varstack->bufsize, (varstack->bufsize));
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
      clock_gettime(CLOCK_MONOTONIC, &rules[nrrules-1]->timestamp->first);
      printf("bytecode is %d bytes\n", rules[nrrules-1]->ast.nrbytes);
#endif
      rule_call(nrrules-1);
      while(1) {
        int8_t ret = 0;
        uint8_t nr = 0;
        ret = rules_loop(rules, nrrules, &nr);
        if(ret == -1) {
          exit(-1);
        }
        if(ret == 0) {
          break;
        }
      }
#if defined(DEBUG) && !defined(ESP8266)
      clock_gettime(CLOCK_MONOTONIC, &rules[nrrules-1]->timestamp->second);

      printf("rule #%d was executed in %.6f seconds\n", rules[nrrules-1]->nr,
        ((double)rules[nrrules-1]->timestamp->second.tv_sec + 1.0e-9*rules[nrrules-1]->timestamp->second.tv_nsec) -
        ((double)rules[nrrules-1]->timestamp->first.tv_sec + 1.0e-9*rules[nrrules-1]->timestamp->first.tv_nsec));

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

  FREE(varstack);

  if(unittests[(*i)].dofail != ret) {
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
  }
  for(x=0;x<nrrules;x++) {
    struct rule_stack_t *node = (struct rule_stack_t *)rules[x]->userdata;
    FREE(node->buffer);
    FREE(node);
  }

  FREE(cpytxt);
  FREE(varstack);
  for(int i = 0;i<nrrules;i++) {
    FREE(rules[i]->timestamp);
  }
  FREE(rules);
  nrrules = 0;
}

void check_rule_name(int *i, unsigned char *mempool, uint16_t size) {
  memset(&rule_options, 0, sizeof(struct rule_options_t));
  rule_options.is_token_cb = is_variable;
  rule_options.is_event_cb = is_event;
  rule_options.set_token_val_cb = vm_value_set;
  rule_options.get_token_val_cb = vm_value_get;
  rule_options.prt_token_val_cb = vm_value_prt;
  rule_options.clr_token_val_cb = vm_value_del;
  rule_options.event_cb = event_cb;

#ifdef ESP8266
  Serial.printf("[ %-*s                    %-*s ]\n", 24, " ", 25, " ");
  Serial.printf("[ %-*s Retrieving rule name %-*s ]\n", 23, " ", 24, " ");
  Serial.printf("[ %-*s                    %-*s ]\n", 24, " ", 25, " ");
#else
  printf("[ %-*s                    %-*s ]\n", 24, " ", 25, " ");
  printf("[ %-*s Retrieving rule name %-*s ]\n", 23, " ", 24, " ");
  printf("[ %-*s                    %-*s ]\n", 24, " ", 25, " ");
#endif

  const char *rule = "on foo then $a = 1; end";

  int len = strlen(rule);
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
      mmu_set_uint8((void *)&(mempool[txtoffset+y]), (uint8_t)rule[y]);
    }
  } else {
#endif
    for(y=0;y<len;y++) {
      mempool[txtoffset+y] = (uint8_t)rule[y];
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

  while((ret = rule_initialize(&input, &rules, &nrrules, &mem, varstack)) == 0);
  assert(ret == 1);

  char *cpy = rule_by_nr(rules, nrrules, 0);
  if(strcmp(cpy, "foo") != 0) {
    exit(-1);
  }
  FREE(cpy);

  struct rule_stack_t *node = (struct rule_stack_t *)rules[0]->userdata;
  FREE(node->buffer);
  FREE(node);
  for(int i = 0;i<nrrules;i++) {
    FREE(rules[i]->timestamp);
  }
  FREE(rules);

  FREE(cpytxt);
  nrrules = 0;
}

void run_async(int *i, unsigned char *mempool, uint16_t size) {
  memset(&rule_options, 0, sizeof(struct rule_options_t));
  rule_options.is_token_cb = is_variable;
  rule_options.is_event_cb = is_event;
  rule_options.set_token_val_cb = vm_value_set;
  rule_options.get_token_val_cb = vm_value_get;
  rule_options.prt_token_val_cb = vm_value_prt;
  rule_options.clr_token_val_cb = vm_value_del;
  rule_options.event_cb = event_cb;

#ifdef ESP8266
  Serial.printf("[ %-*s                    %-*s ]\n", 24, " ", 25, " ");
  Serial.printf("[ %-*s Running async test %-*s ]\n", 24, " ", 25, " ");
  Serial.printf("[ %-*s                    %-*s ]\n", 24, " ", 25, " ");
#else
  printf("[ %-*s                    %-*s ]\n", 24, " ", 25, " ");
  printf("[ %-*s Running async test %-*s ]\n", 24, " ", 25, " ");
  printf("[ %-*s                    %-*s ]\n", 24, " ", 25, " ");
#endif

  const char *rule = "if 3 == 3 then if 1 == 1 then $a = 3; end sync if 1 == 1 then $c = 3; end $b = 3; end";

  int len = strlen(rule);
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
      mmu_set_uint8((void *)&(mempool[txtoffset+y]), (uint8_t)rule[y]);
    }
  } else {
#endif
    for(y=0;y<len;y++) {
      mempool[txtoffset+y] = (uint8_t)rule[y];
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

  while((ret = rule_initialize(&input, &rules, &nrrules, &mem, varstack)) == 0);
  assert(ret == 1);

  {
    int size = strlen(rule);
#ifdef ESP8266
    char str[OUTPUT_SIZE];
    /*LCOV_EXCL_START*/
    memset(&str, 0, OUTPUT_SIZE);
    if(size > 49) {
      size = MIN(size, 45);
      snprintf((char *)&str, OUTPUT_SIZE, "Rule %.2d.%d / %.2d: [ %.*s ... %-*s ]", 0, 1, 0, size, rule, 46-size, " ");
    } else {
      size = MIN(size, 50);
      snprintf((char *)&str, OUTPUT_SIZE, "Rule %.2d.%d / %.2d: [ %-*s %-*s ]", 0, 1, 0, size, rule, 50-size, " ");
    }
    Serial.println(str);
    /*LCOV_EXCL_STOP*/
#else
    if(size > 49) {
      size = MIN(size, 45);
      printf("Rule %.2d.%d / %.2d: [ %.*s ... %-*s ]\n", 0, 1, 0, size, rule, 46-size, " ");
    } else {
      size = MIN(size, 50);
      printf("Rule %.2d.%d / %.2d: [ %.*s %-*s ]\n", 0, 1, 0, size, rule, 50-size, " ");
    }
#endif
  }

#if defined(DEBUG) && !defined(ESP8266)
  clock_gettime(CLOCK_MONOTONIC, &rules[nrrules-1]->timestamp->first);
  printf("bytecode is %d bytes\n", rules[nrrules-1]->ast.nrbytes);
#endif
  {
    uint8_t x = 0;
    int results[] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 0};
    while((ret = rule_run(rules[0], 0)) > 0) {
      if(ret != results[x++]) {
        exit(-1);
      }
    }
    if(ret != results[x++]) {
      exit(-1);
    }
  }

#if defined(DEBUG) && !defined(ESP8266)
  clock_gettime(CLOCK_MONOTONIC, &rules[nrrules-1]->timestamp->second);

  printf("rule #%d was executed in %.6f seconds\n", 0,
    ((double)rules[0]->timestamp->second.tv_sec + 1.0e-9*rules[0]->timestamp->second.tv_nsec) -
    ((double)rules[0]->timestamp->first.tv_sec + 1.0e-9*rules[0]->timestamp->first.tv_nsec));

  printf("bytecode is %d bytes\n", rules[0]->ast.nrbytes);
#endif

  struct rule_stack_t *node = (struct rule_stack_t *)rules[0]->userdata;
  FREE(node->buffer);
  FREE(node);
  for(int i = 0;i<nrrules;i++) {
    FREE(rules[i]->timestamp);
  }
  FREE(rules);

  FREE(cpytxt);
  nrrules = 0;
}

int8_t run_two_mempools(struct pbuf *mem) {
  memset(&rule_options, 0, sizeof(struct rule_options_t));
  rule_options.is_token_cb = is_variable;
  rule_options.is_event_cb = is_event;
  rule_options.set_token_val_cb = vm_value_set;
  rule_options.get_token_val_cb = vm_value_get;
  rule_options.prt_token_val_cb = vm_value_prt;
  rule_options.clr_token_val_cb = vm_value_del;
  rule_options.event_cb = event_cb;

#ifdef ESP8266
  Serial.printf("[ %-*s                    %-*s ]\n", 24, " ", 25, " ");
  Serial.printf("[ %-*s Running two mempools test %-*s ]\n", 20, " ", 22, " ");
  Serial.printf("[ %-*s                    %-*s ]\n", 24, " ", 25, " ");
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  if((void *)mem->payload >= (void *)MMU_SEC_HEAP) {
    Serial.printf("[ %-*s Mempool 1 in second heap %-*s ]\n", 20, " ", 23, " ");
  } else {
    Serial.printf("[ %-*s Mempool 1 in first heap %-*s ]\n", 20, " ", 24, " ");
#else
    Serial.printf("[ %-*s Mempool 1 in first heap %-*s ]\n", 20, " ", 24, " ");
#endif
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  }
#endif
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  if((void *)mem->next->payload >= (void *)MMU_SEC_HEAP) {
    Serial.printf("[ %-*s Mempool 2 in second heap %-*s ]\n", 20, " ", 23, " ");
  } else {
    Serial.printf("[ %-*s Mempool 2 in first heap %-*s ]\n", 20, " ", 24, " ");
#else
    Serial.printf("[ %-*s Mempool 2 in first heap %-*s ]\n", 20, " ", 24, " ");
#endif
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  }
#endif
#else
  printf("[ %-*s                    %-*s ]\n", 24, " ", 25, " ");
  printf("[ %-*s Running two mempools test %-*s ]\n", 20, " ", 22, " ");
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  if((void *)mem->payload >= (void *)MMU_SEC_HEAP) {
    printf("[ %-*s Mempool 1 in second heap %-*s ]\n", 20, " ", 23, " ");
  } else {
    printf("[ %-*s Mempool 1 in first heap %-*s ]\n", 20, " ", 24, " ");
#else
    printf("[ %-*s Mempool 1 in first heap %-*s ]\n", 20, " ", 24, " ");
#endif
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  }
#endif
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  if((void *)mem->next->payload >= (void *)MMU_SEC_HEAP) {
    printf("[ %-*s Mempool 2 in second heap %-*s ]\n", 20, " ", 23, " ");
  } else {
    printf("[ %-*s Mempool 2 in first heap %-*s ]\n", 20, " ", 24, " ");
#else
    printf("[ %-*s Mempool 2 in first heap %-*s ]\n", 20, " ", 24, " ");
#endif
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  }
#endif
  printf("[ %-*s                    %-*s ]\n", 24, " ", 25, " ");
#endif

  const char *rule = "on foo then sync if 1 == 1 then $a = 1; $b = 1.25; $c = 10; $d = 100; else $a = 1; end end on bar then $e = NULL; $f = max(1, 2); $g = 1 + 1.25; foo(); end";

  int len = strlen(rule);
  struct rule_stack_t *varstack = (struct rule_stack_t *)MALLOC(sizeof(struct rule_stack_t));
  if(varstack == NULL) {
    OUT_OF_MEMORY
  }
  varstack->buffer = NULL;
  varstack->nrbytes = 4;
  varstack->bufsize = 4;

  int ret = 0;
  struct pbuf input;
  memset(&input, 0, sizeof(struct pbuf));

  uint8_t y = 0;
  uint16_t txtoffset = alignedbuffer(mem->tot_len-len-5);

#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  if((void *)mem->payload >= (void *)MMU_SEC_HEAP) {
    for(y=0;y<len;y++) {
      mmu_set_uint8((void *)&(((unsigned char *)mem->payload)[txtoffset+y]), (uint8_t)rule[y]);
    }
  } else {
#endif
    for(y=0;y<len;y++) {
      ((unsigned char *)mem->payload)[txtoffset+y] = (uint8_t)rule[y];
    }
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  }
#endif

  input.payload = &((unsigned char *)mem->payload)[txtoffset];
  input.len = txtoffset;
  input.tot_len = len;

  unsigned char *cpytxt = (unsigned char *)MALLOC(len+1);
  if(cpytxt == NULL) {
    OUT_OF_MEMORY
  }
  memset(cpytxt, 0, len+1);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  if((void *)mem->payload >= (void *)MMU_SEC_HEAP) {
    for(y=0;y<len;y++) {
      cpytxt[y] = mmu_get_uint8(&((unsigned char *)input.payload)[y]);
    }
  } else {
#endif
    memcpy(cpytxt, input.payload, len);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  }
#endif

  while((ret = rule_initialize(&input, &rules, &nrrules, mem, varstack)) == 0) {
    varstack = (struct rule_stack_t *)MALLOC(sizeof(struct rule_stack_t));
    memset(varstack, 0, sizeof(struct rule_stack_t));
    if(varstack == NULL) {
      OUT_OF_MEMORY
    }
    varstack->buffer = NULL;
    varstack->nrbytes = 4;
    varstack->bufsize = 4;

    input.payload = &((unsigned char *)mem->payload)[input.len];
  }

  if(ret == 1 && nrrules == 2) {
    char *cpy = rule_by_nr(rules, nrrules, 0);
    if(strcmp(cpy, "foo") != 0) {
      exit(-1);
    }
    FREE(cpy);
  }

  for(uint8_t i=0;i<nrrules;i++) {
    vm_clear_values(rules[i]);
  }

  FREE(varstack);

  {
    int size = strlen(rule);
#ifdef ESP8266
    char str[OUTPUT_SIZE];
    /*LCOV_EXCL_START*/
    memset(&str, 0, OUTPUT_SIZE);
    if(size > 49) {
      size = MIN(size, 45);
      snprintf((char *)&str, OUTPUT_SIZE, "Rule %.2d.%d / %.2d: [ %.*s ... %-*s ]", 0, 1, 0, size, rule, 46-size, " ");
    } else {
      size = MIN(size, 50);
      snprintf((char *)&str, OUTPUT_SIZE, "Rule %.2d.%d / %.2d: [ %-*s %-*s ]", 0, 1, 0, size, rule, 50-size, " ");
    }
    Serial.println(str);
    /*LCOV_EXCL_STOP*/
#else
    if(size > 49) {
      size = MIN(size, 45);
      printf("Rule %.2d.%d / %.2d: [ %.*s ... %-*s ]\n", 0, 1, 0, size, rule, 46-size, " ");
    } else {
      size = MIN(size, 50);
      printf("Rule %.2d.%d / %.2d: [ %.*s %-*s ]\n", 0, 1, 0, size, rule, 50-size, " ");
    }
#endif
  }

#if defined(DEBUG) && !defined(ESP8266)
  clock_gettime(CLOCK_MONOTONIC, &rules[nrrules-1]->timestamp->first);
  printf("bytecode is %d bytes\n", rules[nrrules-1]->ast.nrbytes);
#endif

  if(ret == 1) {
    rule_call(nrrules-1);
    uint8_t x = 0, nr = 0;
    int results[] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0};
    int rulenr[] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1};
    while(1) {
      ret = rules_loop(rules, nrrules, &nr);

      if(rulenr[x] != nr || results[x] != ret) {
        exit(-1);
      }
      if(ret == -1) {
        exit(-1);
      }
      if(ret == 0) {
        break;
      }
      x++;
    }
    x++;

    if(rulenr[x] != nr || results[x++] != ret) {
      exit(-1);
    }
  }

#if defined(DEBUG) && !defined(ESP8266)
  clock_gettime(CLOCK_MONOTONIC, &rules[nrrules-1]->timestamp->second);

  printf("rule #%d was executed in %.6f seconds\n", 0,
    ((double)rules[0]->timestamp->second.tv_sec + 1.0e-9*rules[0]->timestamp->second.tv_nsec) -
    ((double)rules[0]->timestamp->first.tv_sec + 1.0e-9*rules[0]->timestamp->first.tv_nsec));

  printf("bytecode is %d bytes\n", rules[0]->ast.nrbytes);
#endif

  for(int i = 0;i<nrrules;i++) {
    struct rule_stack_t *node = (struct rule_stack_t *)rules[i]->userdata;
    FREE(node->buffer);
    FREE(node);
    FREE(rules[i]->timestamp);
  }
  FREE(rules);
  FREE(cpytxt);
  nrrules = 0;

  return ret;
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

  memset(mempool, 0, MEMPOOL_SIZE*2);
  run_async(&i, &mempool[MEMPOOL_SIZE], MEMPOOL_SIZE);

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

  memset(mempool, 0, MEMPOOL_SIZE*2);
  check_rule_name(&i, &mempool[0], MEMPOOL_SIZE);

  FREE(mempool);

  {
    uint8_t nrtests = 6;
    struct {
      uint16_t size[2];
      uint16_t used[2];
      uint8_t loc[2];
      int8_t ret;
    } tests[nrtests] = {
      { { 750, 500 }, { 580, 0 }, {1, 0}, 0 },
      { { 500, 400 }, { 388, 192 }, {0, 1}, 0 },
      { { 500, 400 }, { 388, 192 }, {1, 0}, 0 },
      { { 500, 400 }, { 388, 192 }, {1, 1}, 0 },
      { { 500, 400 }, { 388, 192 }, {0, 0}, 0 },
      { { 250, 350 }, { 144, 208 }, {0, 0}, -1 }
    };

    for(uint8_t i=0;i<nrtests;i++) {
      struct pbuf mem;
      struct pbuf mem1;
      memset(&mem, 0, sizeof(struct pbuf));
      memset(&mem1, 0, sizeof(struct pbuf));

      unsigned char *mempool = (unsigned char *)MALLOC(1000*5);
      if(mempool == NULL) {
        fprintf(stderr, "OUT_OF_MEMORY\n");
        exit(-1);
      }
      memset(mempool, 0, 1000*5);

      MMU_SEC_HEAP = &mempool[2000];

      if(tests[i].loc[0] == 1) {
        mem.payload = &mempool[3000];
      } else {
        mem.payload = &mempool[0];
      }

      mem.len = 0;
      mem.tot_len = tests[i].size[0];

      if(tests[i].loc[1] == 1) {
        mem1.payload = &mempool[2000];
      } else {
        mem1.payload = &mempool[1000];
      }

      mem1.len = 0;
      mem1.tot_len = tests[i].size[1];

      mem.next = &mem1;

      int8_t ret = run_two_mempools(&mem);

      if(ret != tests[i].ret || (ret == 0 && (mem.len != tests[i].used[0] || mem1.len != tests[i].used[1]))) {
        exit(-1);
      }

      FREE(mempool);
    }
  }
}
#endif