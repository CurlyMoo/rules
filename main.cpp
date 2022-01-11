/*
  Copyright (C) CurlyMo

  This Source Code Form is subject to the terms of the Mozilla Public
  License, v. 2.0. If a copy of the MPL was not distributed with this
  file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include "src/common/mem.h"
#include "src/rules/rules.h"

#ifdef ESP8266
#include <Arduino.h>
#endif

#define OUTPUT_SIZE 512

static struct rules_t **rules = NULL;
static int nrrules = 0;
static char out[OUTPUT_SIZE];

struct rule_options_t rule_options;
#ifdef ESP8266
static unsigned char *mempool = (unsigned char *)MMU_SEC_HEAP;
#else
static unsigned char *mempool = NULL;
#endif

typedef struct varstack_t {
  unsigned char *buffer = NULL;
  unsigned int nrbytes;
  unsigned int bufsize;
} varstack_t;

static struct vm_vinteger_t vinteger;

struct unittest_t {
  const char *rule;
  struct {
    const char *output;
    unsigned int bytes;
  } validate[3];
  struct {
    const char *output;
    unsigned int bytes;
  } run[3];
} unittests[] = {
  /*
   * Valid rules
   */
  { "if 3 == 3 then $a = 6; end", { { "$a = 6", 63 } }, { { "$a = 6", 63 } } },
  { "if $a == $a then $a = $a; end", { { "$a = NULL", 74 } }, { { "$a = NULL", 74 } } },
  { "if (3 == 3) then $a = 6; end", { { "$a = 6", 70 } }, { { "$a = 6", 70 } } },
  { "if 10 == 10 then $a = 10; end", { { "$a = 10", 66 } }, { { "$a = 10", 66 } } },
  { "if 100 == 100 then $a = 100; end", { { "$a = 100", 69 } }, { { "$a = 100", 69 } } },
  { "if 1000 == 1000 then $a = 1000; end", { { "$a = 1000", 69 } }, { { "$a = 1000", 69 } } },
  { "if 3.5 == 3.5 then $a = 3.5; end", { { "$a = 3.5", 69 } }, { { "$a = 3.5", 69 } } },
  { "if 33.5 == 33.5 then $a = 33.5; end", { { "$a = 33.5", 69 } }, { { "$a = 33.5", 69 } } },
  { "if 333.5 == 333.5 then $a = 333.5; end", { { "$a = 333.5", 69 } }, { { "$a = 333.5", 69 } } },
  { "if 3.335 < 33.35 then $a = 3.335; end", { { "$a = 3.335", 69 } }, { { "$a = 3.335", 69 } } },
  { "if -10 == -10 then $a = -10; end", { { "$a = -10", 69 } }, { { "$a = -10", 69 } } },
  { "if -100 == -100 then $a = -100; end", { { "$a = -100", 69 } }, { { "$a = -100", 69 } } },
  { "if NULL == 3 then $a = 6; end", { { "$a = 6", 61 } }, { { "", 54 } } },
  { "if 1.1 == 1.1 then $a = 6; end", { { "$a = 6", 67 } }, { { "$a = 6", 67 } } },
  { "if 1.1 == 1.2 then $a = 6; end", { { "$a = 6", 67 } }, { { "", 60 } } },
  { "if 4 != 3 then $a = 6; end", { { "$a = 6", 63 } }, { { "$a = 6", 63 } } },
  { "if (4 != 3) then $a = 6; end", { { "$a = 6", 70 } }, { { "$a = 6", 70 } } },
  { "if NULL != 3 then $a = 6; end", { { "$a = 6", 61 } }, { { "$a = 6", 61 } } },
  { "if NULL != NULL then $a = 6; end", { { "$a = 6", 59 } }, { { "$a = 6", 59 } } },
  { "if 1.2 != 1.1 then $a = 6; end", { { "$a = 6", 67 } }, { { "$a = 6", 67 } } },
  { "if 1.2 != 1.2 then $a = 6; end", { { "$a = 6", 67 } }, { { "", 60 } } },
  { "if 1 != 1 then $a = 6; end", { { "$a = 6", 63 } }, { { "", 56 } } },
  { "if 1.1 >= 1.2 then $a = 6; end", { { "$a = 6", 67 } }, { { "", 60 } } },
  { "if 1.1 >= 1.1 then $a = 6; end", { { "$a = 6", 67 } }, { { "$a = 6", 67 } } },
  { "if 2 >= 1.1 then $a = 6; end", { { "$a = 6", 65 } }, { { "$a = 6", 65 } } },
  { "if 2.1 >= 1 then $a = 6; end", { { "$a = 6", 65 } }, { { "$a = 6", 65 } } },
  { "if NULL >= 1.2 then $a = 6; end", { { "$a = 6", 63 } }, { { "", 56 } } },
  { "if NULL >= NULL then $a = 6; end", { { "$a = 6", 59 } }, { { "", 52 } } },
  { "if 1 > 2 then $a = 6; end", { { "$a = 6", 63} }, { { "", 56 } } },
  { "if 2 > 1 then $a = 6; end", { { "$a = 6", 63 } }, { { "$a = 6", 63 } } },
  { "if 2 > 1.1 then $a = 6; end", { { "$a = 6", 65 } }, { { "$a = 6", 65 } } },
  { "if 1.1 > 1.2 then $a = 6; end", { { "$a = 6", 67 } }, { { "", 60 } } },
  { "if 1.1 > 1.0 then $a = 6; end", { { "$a = 6", 67 } }, { { "$a = 6", 67 } } },
  { "if NULL > 1.2 then $a = 6; end", { { "$a = 6", 63 } }, { { "", 56 } } },
  { "if NULL > NULL then $a = 6; end", { { "$a = 6", 59 } }, { { "", 52 } } },
  { "if 1.2 <= 1.1 then $a = 6; end", { { "$a = 6", 67 } }, { { "", 60 } } },
  { "if 1.1 <= 1.1 then $a = 6; end", { { "$a = 6", 67 } }, { { "$a = 6", 67 } } },
  { "if 1.1 <= 2 then $a = 6; end", { { "$a = 6", 65 } }, { { "$a = 6", 65 } } },
  { "if 1 <= 2 then $a = 6; end", { { "$a = 6", 63 } }, { { "$a = 6", 63 } } },
  { "if 2 <= 1 then $a = 6; end", { { "$a = 6", 63 } }, { { "", 56 } } },
  { "if 1 <= 2.1 then $a = 6; end", { { "$a = 6", 65 } }, { { "$a = 6", 65 } } },
  { "if 1.2 <= NULL then $a = 6; end", { { "$a = 6", 63 } }, { { "", 56 } } },
  { "if NULL <= NULL then $a = 6; end", { { "$a = 6", 59 } }, { { "", 52 } } },
  { "if 2 < 1 then $a = 6; end", { { "$a = 6", 63 } }, { { "", 56 } } },
  { "if 1 < 2 then $a = 6; end", { { "$a = 6", 63 } }, { { "$a = 6", 63 } } },
  { "if 1.1 < 2 then $a = 6; end", { { "$a = 6", 65 } }, { { "$a = 6", 65 } } },
  { "if 1.2 < 1.1 then $a = 6; end", { { "$a = 6", 67 } }, { { "", 60 } } },
  { "if 1.0 < 1.1 then $a = 6; end", { { "$a = 6", 67 } }, { { "$a = 6", 67 } } },
  { "if 1.2 < NULL then $a = 6; end", { { "$a = 6", 63 } }, { { "", 56 } } },
  { "if NULL < NULL then $a = 6; end", { { "$a = 6", 59 } }, { { "", 52 } } },
  { "if NULL && NULL then $a = -6; end", { { "$a = -6", 60 } }, { { "", 53 } } },
  { "if 0 && 0 then $a = -6; end", { { "$a = -6", 64 } }, { { "", 57 } } },
  { "if 1 && 1 then $a = -6; end", { { "$a = -6", 64 } }, { { "$a = -6", 64 } } },
  { "if 1.1 && 1.2 then $a = -6; end", { { "$a = -6", 68 } }, { { "$a = -6", 68 } } },
  { "if 0.0 && 1.2 then $a = -6; end", { { "$a = -6", 68 } }, { { "", 61 } } },
  { "if -1.1 && 1.2 then $a = -6; end", { { "$a = -6", 68 } }, { { "", 61 } } },
  { "if 1.2 && 0.0 then $a = -6; end", { { "$a = -6", 68 } }, { { "", 61 } } },
  { "if 1.2 && -1.1 then $a = -6; end", { { "$a = -6", 68 } }, { { "", 61 } } },
  { "if NULL || NULL then $a = -6; end", { { "$a = -6", 60 } }, { { "", 53 } } },
  { "if 0 || 0 then $a = -6; end", { { "$a = -6", 64 } }, { { "", 57 } } },
  { "if 1 || 1 then $a = -6; end", { { "$a = -6", 64 } }, { { "$a = -6", 64 } } },
  { "if 1.1 || 1.2 then $a = -6; end", { { "$a = -6", 68 } }, { { "$a = -6", 68 } } },
  { "if -0.1 || -0.1 then $a = -6; end", { { "$a = -6", 68 } }, { { "", 61 } } },
  { "if 0.0 || 1.2 then $a = -6; end", { { "$a = -6", 68 } }, { { "$a = -6", 68 } } },
  { "if -1.1 || 1.2 then $a = -6; end", { { "$a = -6", 68 } }, { { "$a = -6", 68 } } },
  { "if 1.2 || 0.0 then $a = -6; end", { { "$a = -6", 68 } }, { { "$a = -6", 68 } } },
  { "if 1.2 || -1.1 then $a = -6; end", { { "$a = -6", 68 } }, { { "$a = -6", 68 } } },
  { "if 3 == NULL then $a = 6; end", { { "$a = 6", 61 } }, { { "", 54 } } },
  { "if (NULL == 3) then $a = 6; end", { { "$a = 6", 68 } }, { { "", 61 } } },
  { "if (3 == NULL) then $a = 6; end", { { "$a = 6", 68 } }, { { "", 61 } } },
  { "if @a == 3 then $a = 6; end", { { "$a = 6", 68 } }, { { "", 61 } } },
  { "if foo#bar == 3 then $a = 6; end", { { "$a = 6", 73 } }, { { "$a = 6", 73 } } },
  { "if 3 == 3 then @a = 6; end", { { "@a = 6", 63 } }, { { "@a = 6", 63 } } },
  { "if 3 == 3 then $a = -6; end", { { "$a = -6", 64 } }, { { "$a = -6", 64 } } },
  { "if 4 != 3 then $a = -6; end", { { "$a = -6", 64 } }, { { "$a = -6", 64 } } },
  { "if 3 == 3 then $a = 1.1; end", { { "$a = 1.1", 65 } }, { { "$a = 1.1", 65 } } },
  { "if 3 == 3 then $a = 1 - NULL; end", { { "$a = NULL", 72 } }, { { "$a = NULL", 72 } } },
  { "if 3 == 3 then $a = 1 - 0.5; end", { { "$a = 0.5", 80 } }, { { "$a = 0.5", 80 } } },
  { "if 3 == 3 then $a = 0.5 - 0.5; end", { { "$a = 0", 82 } }, { { "$a = 0", 82 } } },
  { "if 3 == 3 then $a = 0.5 - 1; end", { { "$a = -0.5", 80 } }, { { "$a = -0.5", 80 } } },
  { "if 3 == 3 then $a = 1 + 2; end", { { "$a = 3", 78 } }, { { "$a = 3", 78 } } },
  { "if 3 == 3 then $a = 1.5 + 2.5; end", { { "$a = 4", 82 } }, { { "$a = 4", 82 } } },
  { "if 3 == 3 then $a = 1 * NULL; end", { { "$a = NULL", 72 } }, { { "$a = NULL", 72 } } },
  { "if 3 == 3 then $a = 1 ^ NULL; end", { { "$a = NULL", 72 } }, { { "$a = NULL", 72 } } },
  { "if 3 == 3 then $a = 9 % 2; end", { { "$a = 1", 78 } }, { { "$a = 1", 78 } } },
  { "if 3 == 3 then $a = 9 % NULL; end", { { "$a = NULL", 72 } }, { { "$a = NULL", 72 } } },
  { "if 3 == 3 then $a = 9.5 % 1.5; end", { { "$a = 0.5", 82 } }, { { "$a = 0.5", 82 } } },
  { "if 3 == 3 then $a = 10 % 1.5; end", { { "$a = 1.5", 81 } }, { { "$a = 1.5", 81 } } },
  { "if 3 == 3 then $a = 1.5 % 10; end", { { "$a = 1", 81 } }, { { "$a = 1", 81 } } },
  { "if 1 == 1 then $a = 1 + 2 + 3; end", { { "$a = 6", 93 } }, { { "$a = 6", 93 } } },
  { "if 1 == 1 == 1 then $a = 1; end", { { "$a = 1", 78 } },  { { "$a = 1", 78 } } },
  { "if 1 == 1 == 2 then $a = 1; end", { { "$a = 1", 78 } },  { { "", 71 } } },
  { "if 1 == 1 then $a = 1; $a = $a + 2; end", { { "$a = 3", 100 } }, { { "$a = 3", 100 } } },
  { "if 1 == 1 then $a = 6; $a = $a + 2 + $a / 3; end", { { "$a = 10", 135 } }, { { "$a = 10", 135 } } },
  { "if 1 == 1 then $a = 6; $a = ($a + 2 + $a / 3); end", { { "$a = 10", 142 } }, { { "$a = 10", 142 } } },
  { "if 1 == 1 then $a = NULL / 1; end", { { "$a = NULL", 72 } }, { { "$a = NULL", 72 } } },
  { "if 12 == 1 then $a = 1 + 2 * 3; end", { { "$a = 7", 94 } }, { { "", 87 } } },
  { "if 1 == 1 then $a = 3 * 1 + 2 * 3; end", { { "$a = 9", 108 } }, { { "$a = 9", 108 } } },
  { "if 1 == 1 then $a = (3 * 1 + 2 * 3); end", { { "$a = 9", 115 } }, { { "$a = 9", 115 } } },
  { "if 1 == 1 then $a = 1 * 2 + 3; end", { { "$a = 5", 93 } }, { { "$a = 5", 93 } } },
  { "if 1 == 1 then $a = 1 * 100 ^ 2; end", { { "$a = 10000", 95 } }, { { "$a = 10000", 95 } } },
  { "if 1 == 1 then $a = 1 * 1.1 ^ 2; end", { { "$a = 1.21", 95 } }, { { "$a = 1.21", 95 } } },
  { "if 1 == 1 then $a = 1 * 1.1 ^ 1.1; end", { { "$a = 1.11053", 97 } }, { { "$a = 1.11053", 97 } } },
  { "if 1 == 1 then $a = 1 * 100 ^ 1.1; end", { { "$a = 158.489", 97 } }, { { "$a = 158.489", 97 } } },
  { "if 1 == 1 then $a = 1 + 2 * 3 / 4; end", { { "$a = 2.5", 108 } }, { { "$a = 2.5", 108 } } },
  { "if 1 == 1 then $a = 1 + 2 * 3 / 4 ^ 2; end", { { "$a = 1.375", 123 } }, { { "$a = 1.375", 123 } } },
  { "if 1 == 1 then $a = 1 + 4 ^ 2 ^ 1; end", { { "$a = 17", 108 } }, { { "$a = 17", 108 } } },
  { "if 1 == 1 then $a = (1 + 4 ^ 2 ^ 1); end", { { "$a = 17", 115 } }, { { "$a = 17", 115 } } },
  { "if 1 == 1 then $a = (1 + 2 * 3 / 4 ^ 2); end", { "$a = 1.375", 130 }, { "$a = 1.375", 130 } },
  { "if 1 == 1 then $a = (1 + 2 * 3 / 4 ^ 2 ^ 1 * 3); end", { "$a = 2.125", 160 }, { "$a = 2.125", 160 } },
  { "if 1 == 1 then $a = (1 + 2 * 3 / 4 ^ 2 ^ 1 * 3 ^ 4); end", { "$a = 31.375", 175 }, { "$a = 31.375", 175 } },
  { "if 1 == 1 then $a = 1 + 2 * 3 / 4 ^ 2 ^ 1 * 3 ^ 4; end", { "$a = 31.375", 168 }, { "$a = 31.375", 168 } },
  { "if 1 == 1 then $a = (1 + 2) * 3; end", { "$a = 9", 100 }, { "$a = 9", 100 } },
  { "if 1 == 1 then $a = (1 + 2 * 3); end", { "$a = 7", 100 }, { "$a = 7", 100 } },
  { "if 1 == 1 then $a = 3 * (1 + 2); end", { "$a = 9", 100 }, { "$a = 9", 100 } },
  { "if 1 == 1 then $a = 3 * (1 + 2) * 2; end", { "$a = 18", 115 }, { "$a = 18", 115 } },
  { "if 1 == 1 then $a = 3 * (1 + 3) ^ 2; end", { "$a = 48", 115 }, { "$a = 48", 115 } },
  { "if 1 == 1 then $a = 7 + 6 * 5 / (4 - 3) ^ 2 ^ 1; end", { "$a = 37", 160 }, { "$a = 37", 160 } },
  { "if 1 == 1 then $a = 3 * (1 + 2) + 2; end", { "$a = 11", 115 }, { "$a = 11", 115 } },
  { "if 1 == 1 then $a = 3 + (1 + 2) * 2; end", { "$a = 9", 115 }, { "$a = 9", 115 } },
  { "if 1 == 1 then $a = 3 * (1 + 2 / 2) + 3; end", { "$a = 9", 130 }, { "$a = 9", 130 } },
  { "if 1 == 1 then $a = 3 * ((1 + 2) / 2) + 2; end", { "$a = 6.5", 137 }, { "$a = 6.5", 137 } },
  { "if 1 == 1 then $a = 3 * ((1 + 2) / 2 + 3); end", { "$a = 13.5", 137 }, { "$a = 13.5", 137 } },
  { "if 1 == 1 then $a = 3 * ((1 + 2) / (2 + 3)); end", { "$a = 1.8", 144 }, { "$a = 1.8", 144 } },
  { "if 1 == 1 then $a = 3 * ((((1 + 2) / (2 + 3)))); end", { "$a = 1.8", 158 }, { "$a = 1.8", 158 } },
  { "if $a == $a then $a = $a * (((($a + $a) / ($a + $a)))); end", { "$a = NULL", 189 }, { "$a = NULL", 189 } },
  { "if 1 == 1 then $a = 3 * ((1 + 2) / (2 + 3)) + 2; end", { "$a = 3.8", 159 }, { "$a = 3.8", 159 } },
  { "if 1 == 1 then $a = (1 == 1 && 1 == 0) || 5 >= 4; end", { "$a = 1", 145 }, { "$a = 1", 145 } },
  { "if 1 == 1 then $a = (1 == 1 && 1 == 0) || 3 >= 4; end", { "$a = 0", 145 }, { "$a = 0", 145 } },
  { "if 1 == 1 then $a = 3; end", { "$a = 3", 63 }, { "$a = 3", 63 } },
  { "if 1 == 1 then $a = 3.1; $b = $a; end", { "$a = 3.1$b = 3.1", 94 }, { "$a = 3.1$b = 3.1", 94 } },
  { "if 1 == 1 then $a = $a + 1; end", { "$a = NULL", 79 }, { "$a = NULL", 79 } },
  { "if 1 == 1 then $a = max(foo#bar); end", { "$a = 3", 83 }, { "$a = 3", 83 } },
  { "if 1 == 1 then $a = coalesce($a, 0) + 1; end", { "$a = 1", 100 }, { "$a = 1", 100 } },
  { "if 1 == 1 then $a = coalesce($a, 1.1) + 1; end", { "$a = 2.1", 102 }, { "$a = 2.1", 102 } },
  { "if 1 == 1 then if $a == NULL then $a = 0; end $a = $a + 1; end", { "$a = 1", 138 }, { "$a = 1", 138 } },
  { "if 1 == 1 then $a = 1; if $a == NULL then $a = 0; end $a = $a + 1; end", { "$a = 1", 155 }, { "$a = 2", 155 } },
  { "if 1 == 1 then $a = 1; if $a == 1 then $a = NULL; end end", { "$a = NULL", 114 }, { "$a = NULL", 114 } },
  { "if 1 == 1 then $a = 1; $a = NULL; end", { "$a = NULL", 74 }, { "$a = NULL", 74 } },
  { "if 1 == 2 then $a = 3; else $a = 4; end", { "$a = 4", 84 }, { "$a = 4", 84 } },
  { "if 1 == 1 then $a = 3; else $a = 4; end", { "$a = 4", 84 }, { "$a = 3", 84 } },
  { "if (1 + 1) == 1 then $a = 3; else $a = 4; end", { "$a = 4", 106 }, { "$a = 4", 106 } },
  { "if 1 == 2 || 3 >= 4 then $a = max(1, 2); end", { "$a = 2", 110 }, { "", 103 } },
  { "if 1 == 2 || 3 >= 4 then $a = min(3, 1, 2); end", { "$a = 1", 117 }, { "", 110 } },
  { "if 1 == 2 || 3 >= 4 then $a = 1; end", { "$a = 1", 93 }, { "", 86 } },
  { "if 1 == 2 || 3 >= 4 || 5 == 6 then $a = max(1, 3, 2); else $b = 9; end", { "$a = 3$b = 9", 175 }, { "$b = 9", 168 } },
  { "if 1 == 1 then $a = 1; $b = 2; end", { "$a = 1$b = 2", 87 }, { "$a = 1$b = 2", 87 } },
  { "if 1 == 1 then $a = 1; $b = ($a + 3) * 3; end", { "$a = 1$b = 12", 129 }, { "$a = 1$b = 12", 129 } },
  { "if 1 == 1 then $a = 1; $b = $a + 3 * 3; end", { "$a = 1$b = 10", 122 }, { "$a = 1$b = 10", 122 } },
  { "if 1 == 1 then if 5 == 6 then $a = 1; end $a = 2; end", { "$a = 2", 115 }, { "$a = 2", 115 } },
  { "if 1 == 1 then if 5 == 6 then $a = 1; end if 1 == 3 then $b = 3; end $a = 2; end", { "$b = 3$a = 2", 174 }, { "$a = 2", 167 } },
  { "if 1 == 1 then $a = 1; $b = $a; end", { "$a = 1$b = 1", 92 }, { "$a = 1$b = 1", 92 } },
  { "if 1 == 1 then $a = 1; if 5 >= 4 then $a = 3; end $b = $a; end", { "$a = 3$b = 3", 144 }, { "$a = 3$b = 3", 144 } },
  { "if 1 == 1 then $a = 1; if $a == 1 then $a = 3; end end", { "$a = 3", 120 }, { "$a = 3", 120 } },
  { "if (1 == 1 && 1 == 0) || 5 >= 4 then $a = 1; end", { "$a = 1", 130 }, { "$a = 1", 130 } },
  { "if 3 == 3 then $a = max(1); end", { "$a = 1", 73 }, { "$a = 1", 73 } },
  { "if 3 == 3 then $a = max(NULL, 1); end", { "$a = 1", 78 }, { "$a = 1", 78 } },
  { "if 3 == 3 then $a = max(1, NULL); end", { "$a = 1", 78 }, { "$a = 1", 78 } },
  { "if 3 == 3 then $b = 2; $a = max($b, 1); end", { "$b = 2$a = 2", 109 }, { "$b = 2$a = 2", 109 } },
  { "if 3 == 3 then $a = max(1, 2); end", { "$a = 2", 80 }, { "$a = 2", 80 } },
  { "if 3 == 3 then $a = max(1, 2, 3, 4); end", { "$a = 4", 94 }, { "$a = 4", 94 } },
  { "if 3 == 3 then $a = max(1, 4, 5, 3, 2); end", { "$a = 5", 101 }, { "$a = 5", 101 } },
  { "if 3 == 3 then $a = max(max(1, 4), 2); end", { "$a = 4", 97 }, { "$a = 4", 97 } },
  { "if 3 == 3 then $a = max(1, 2) * 3; end", { "$a = 6", 95 }, { "$a = 6", 95 } },
  { "if 3 == 3 then $a = max(1, 2) * max(3, 4); end", { "$a = 8", 112 }, { "$a = 8", 112 } },
  { "if 3 == 3 then $a = max(max(1, 2), (1 * max(1, 3) ^ 2)); end", { "$a = 9", 151 }, { "$a = 9", 151 } },
  { "if 3 == 3 then $b = 1; $a = max($b + 1); end", { "$b = 1$a = 2", 117 }, { "$b = 1$a = 2", 117 } },
  { "if 3 == 3 then $b = 1; $a = max($b + 1) * 3; end", { "$b = 1$a = 6", 132 }, { "$b = 1$a = 6", 132 } },
  { "if max(1, 3) == 3 then $a = 1; end", { "$a = 1", 80 }, { "$a = 1", 80 } },
  { "if max(1, 3) == max(1, 3) then $a = 1; end", { "$a = 1", 97 }, { "$a = 1", 97 } },
  { "if 3 == 3 then max(1, 2); end", { "", 63 }, { "", 63 } },
  { "if 3 == 3 then $a = max(1 + 1, 2 + 2); end", { "$a = 4", 110 }, { "$a = 4", 110 } },
  { "if 1 == 1 then $a = coalesce($b, 0); end  ", { { "$b = NULL$a = 0", 88 } },  { { "$b = NULL$a = 0", 88 } } }, // FIXME
  { "if 3 == 3 then $a = max((1 + 3), 2); end", { "$a = 4", 102 }, { "$a = 4", 102 } },
  { "if 3 == 3 then $a = max((1 + (3 * 3)), 2); end", { "$a = 10", 124 }, { "$a = 10", 124 } },
  { "if 3 == 3 then $a = max(1 + 3 * 3, 3 * 4); end", { "$a = 12", 125 }, { "$a = 12", 125 } },
  { "if 3 == 3 then $a = max(((2 + 3) * 3), (3 * 4)); end", { "$a = 15", 146 }, { "$a = 15", 146 } },
  { "if 3 == 3 then $a = (max(1, 2) + 2) * 3; end", { "$a = 12", 117 }, { "$a = 12", 117 } },
  { "if 3 == 3 then $a = (max(1, 2) + 2) * 3; end", { "$a = 12", 117 }, { "$a = 12", 117 } },
  { "if 1 == 1 then $a = max(0, 1) + max(1, 2) * max(2, 3) / max(3, 4) ^ max(1, 2) ^ max(0, 1) * max(2, 3) ^ max(3, 4); end", { "$a = 31.375", 304 }, { "$a = 31.375", 304 } },
  { "if 3 == 3 then $a = 2; $b = max((($a + 3) * 3), (3 * 4)); end", { "$a = 2$b = 15", 175 }, { "$a = 2$b = 15", 175 } },
  { "if 3 == 3 then $a = 2; $b = max((max($a + 3) * 3), (3 * 4)); end", { "$a = 2$b = 15", 178 }, { "$a = 2$b = 15", 178 } },
  { "if 1 == 1 then $a = 1; else $a = 2; end", { "$a = 2", 84 }, { "$a = 1", 84 } },
  { "if 1 == 2 then $a = 1; elseif 2 == 2 then $a = 3; else $a = 2; end", { "$a = 2", 140 }, { "$a = 3", 140 } },
  { "if 1 == 1 then if 2 == 2 then $a = 1; end else $a = 2; end", { "$a = 2", 119 }, { "$a = 1", 119 } },
  { "if (1 == 1) || 5 >= 4 then $a = 1; if 6 == 5 then $a = 2; end $a = $a + 3; $b = (3 + $a) * 2; else $a = 7; end", { "$b = 16$a = 7", 276 }, { "$a = 4$b = 14", 276 } },
  { "if (1 == 1) || 5 >= 4 then $a = 1; if 6 == 5 then $a = 2; end $a = $a + 3; $b = (3 + $a) * 2; $b = 3; else $a = 7; end", { "$b = 3$a = 7", 293 }, { "$a = 4$b = 3", 293 } },
  { "if (1 == 1 && 1 == 0) || 5 >= 4 then $a = 1; if 6 == 5 then $a = 2; end $a = $a + 3; $b = (3 + $a * 5 + 3 * 1) * 2; @c = 5; else if 2 == 2 then $a = 6; else $a = 7; end end", { "$b = 62@c = 5$a = 7", 431 }, { "$a = 4$b = 52@c = 5", 431 } },
  { "if 3 == 3 then $a = 6; end if 3 == 3 then $b = 3; end  ", { { "$a = 6", 63 }, { "$b = 3", 63 } },  { { "$a = 6", 63 }, { "$b = 3" , 63 } } },
  { "   if 3 == 3 then $a = 6; end    if 3 == 3 then $b = 6; end               if 3 == 3 then $c = 6; end", { { "$a = 6", 63 }, { "$b = 6", 63 }, { "$c = 6", 63 } }, { { "$a = 6", 63 }, { "$b = 6", 63 }, { "$c = 6", 63 } } },
  { "on foo then max(1, 2); end", { "", 43 }, { "", 43 } },
  { "on foo then $a = 6; end", { "$a = 6", 43 }, { "$a = 6", 43 } },
  { "on foo then $a = 6; $b = 3; end", { "$a = 6$b = 3", 67 }, { "$a = 6$b = 3", 67 } },
  { "on foo then @a = 6; end", { { "@a = 6", 43 } }, { { "@a = 6", 43 } } },
  { "on foo then $a = 1 + 2; end", { { "$a = 3", 58 } }, { { "$a = 3", 58 } } },
  { "on foo then if 5 == 6 then $a = 1; end $a = 2; end", { "$a = 2", 95 }, { "$a = 2", 95 } },
  { "on foo then if 5 == 6 then $a = 1; end if 1 == 3 then $b = 3; end $a = 2; end", { "$b = 3$a = 2", 154 }, { "$a = 2", 147 } },
  { "on foo then bar(); end  ", { { "", 28 } },  { { "", 28 } } },
  { "on foo then $a = 6; end if 3 == 3 then $b = 3; end  ", { { "$a = 6", 43 }, { "$b = 3" , 63 } },  { { "$a = 6", 43 }, { "$b = 3" , 63 } } },
  { "on foo then $a = 6; end if 3 == 3 then foo(); $b = 3; end  ", { { "$a = 6", 43 }, { "$b = 3", 72 } },  { { "$a = 6", 43 }, { "$b = 3", 72 } } },
  { "on foo then $a = coalesce($b, 0); end  ", { { "$b = NULL$a = 0", 68 } },  { { "$b = NULL$a = 0", 68 } } }, // FIXME

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

static int strnicmp(char const *a, char const *b, size_t len) {
  unsigned int i = 0;

  if(a == NULL || b == NULL) {
    return -1;
  }
  if(len == 0) {
    return 0;
  }

  for(;i++<len; a++, b++) {
    int d = tolower(*a) - tolower(*b);
    if(d != 0 || !*a || i == len) {
      return d;
    }
  }
  return -1;
}

static int is_variable(char *text, unsigned int *pos, unsigned int size) {
  int i = 1;
  if(size == 7 && strncmp(&text[*pos], "foo#bar", 7) == 0) {
    return 7;
  } else if(text[*pos] == '$' || text[*pos] == '@') {
    while(isalpha(text[*pos+i])) {
      i++;
    }
    return i;
  }
  return -1;
}

static int is_event(char *text, unsigned int *pos, unsigned int size) {
  int len = 0;

  if(size == 3 &&
    (strnicmp(&text[*pos], "foo", len) == 0 || strnicmp(&text[*pos], "bar", len) == 0)) {
    return 0;
  }
  return -1;
}

static unsigned char *vm_value_get(struct rules_t *obj, uint16_t token) {
  struct varstack_t *varstack = (struct varstack_t *)obj->userdata;
  struct vm_tvar_t *var = (struct vm_tvar_t *)&obj->ast.buffer[token];

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
    if(obj->valstack.buffer[var->value] == 0) {
      int ret = varstack->nrbytes;
      unsigned int size = alignedbytes(varstack->nrbytes+sizeof(struct vm_vnull_t));
      if((varstack->buffer = (unsigned char *)REALLOC(varstack->buffer, alignedbuffer(size))) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      struct vm_vnull_t *value = (struct vm_vnull_t *)&varstack->buffer[ret];
      value->type = VNULL;
      value->ret = token;
      obj->valstack.buffer[var->value] = ret;
      varstack->nrbytes = size;
      varstack->bufsize = alignedbuffer(size);
    }

    return &varstack->buffer[obj->valstack.buffer[var->value]];
  }
  return NULL;
}

static void vm_value_cpy(struct rules_t *obj, uint16_t token) {
  struct varstack_t *varstack = (struct varstack_t *)obj->userdata;
  struct vm_tvar_t *var = (struct vm_tvar_t *)&obj->ast.buffer[token];

  int x = 0;
  for(x=4;alignedbytes(x)<varstack->nrbytes;x++) {
    x = alignedbytes(x);
#ifdef DEBUG
  printf(". %s %d %d\n", __FUNCTION__, __LINE__, x);
#endif
    switch(varstack->buffer[x]) {
      case VINTEGER: {
        struct vm_vinteger_t *val = (struct vm_vinteger_t *)&varstack->buffer[x];
        struct vm_tvar_t *foo = (struct vm_tvar_t *)&obj->ast.buffer[val->ret];
        if(strcmp((char *)foo->token, (char *)var->token) == 0 && val->ret != token) {
          obj->valstack.buffer[var->value] = obj->valstack.buffer[foo->value];
          val->ret = token;
#ifdef DEBUG
          printf(". %s %d %d %d\n", __FUNCTION__, __LINE__, x, (int)val->value);
#endif
          obj->valstack.buffer[foo->value] = 0;
          return;
        }
        x += sizeof(struct vm_vinteger_t)-1;
      } break;
      case VFLOAT: {
        struct vm_vfloat_t *val = (struct vm_vfloat_t *)&varstack->buffer[x];
        struct vm_tvar_t *foo = (struct vm_tvar_t *)&obj->ast.buffer[val->ret];
        if(strcmp((char *)foo->token, (char *)var->token) == 0 && val->ret != token) {
          obj->valstack.buffer[var->value] = obj->valstack.buffer[foo->value];
          val->ret = token;
#ifdef DEBUG
          printf(". %s %d %d %g\n", __FUNCTION__, __LINE__, x, val->value);
#endif
          obj->valstack.buffer[foo->value] = 0;
          return;
        }
        x += sizeof(struct vm_vfloat_t)-1;
      } break;
      case VNULL: {
        struct vm_vnull_t *val = (struct vm_vnull_t *)&varstack->buffer[x];
        struct vm_tvar_t *foo = (struct vm_tvar_t *)&obj->ast.buffer[val->ret];
        if(strcmp((char *)foo->token, (char *)var->token) == 0 && val->ret != token) {
          obj->valstack.buffer[var->value] = obj->valstack.buffer[foo->value];
          val->ret = token;

#ifdef DEBUG
          printf(". %s %d %d NULL\n", __FUNCTION__, __LINE__, x);
#endif
          obj->valstack.buffer[foo->value] = 0;
          return;
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

static int vm_value_del(struct rules_t *obj, uint16_t idx) {
  struct varstack_t *varstack = (struct varstack_t *)obj->userdata;
  int x = 0, ret = 0;

  if(idx == varstack->nrbytes) {
    return -1;
  }

  switch(varstack->buffer[idx]) {
    case VINTEGER: {
      ret = alignedbytes(sizeof(struct vm_vinteger_t));
      memmove(&varstack->buffer[idx], &varstack->buffer[idx+ret], varstack->nrbytes-idx-ret);
      if((varstack->buffer = (unsigned char *)REALLOC(varstack->buffer, alignedbuffer(varstack->nrbytes-ret))) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      varstack->nrbytes -= ret;
      varstack->bufsize = alignedbuffer(varstack->nrbytes-ret);
    } break;
    case VFLOAT: {
      ret = alignedbytes(sizeof(struct vm_vfloat_t));
      memmove(&varstack->buffer[idx], &varstack->buffer[idx+ret], varstack->nrbytes-idx-ret);
      if((varstack->buffer = (unsigned char *)REALLOC(varstack->buffer, alignedbuffer(varstack->nrbytes-ret))) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      varstack->nrbytes -= ret;
      varstack->bufsize = alignedbuffer(varstack->nrbytes-ret);
    } break;
    case VNULL: {
      ret = alignedbytes(sizeof(struct vm_vnull_t));
      memmove(&varstack->buffer[idx], &varstack->buffer[idx+ret], varstack->nrbytes-idx-ret);
      if((varstack->buffer = (unsigned char *)REALLOC(varstack->buffer, alignedbuffer(varstack->nrbytes-ret))) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      varstack->nrbytes -= ret;
      varstack->bufsize = alignedbuffer(varstack->nrbytes-ret);
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
  for(x=idx;alignedbytes(x)<varstack->nrbytes;x++) {
    x = alignedbytes(x);
#ifdef DEBUG
    printf(". %s %d %d\n", __FUNCTION__, __LINE__, x);
#endif
    switch(varstack->buffer[x]) {
      case VINTEGER: {
        struct vm_vinteger_t *node = (struct vm_vinteger_t *)&varstack->buffer[x];
        if(node->ret > 0) {
          struct vm_tvar_t *tmp = (struct vm_tvar_t *)&obj->ast.buffer[node->ret];
          obj->valstack.buffer[tmp->value] = x;
        }
        x += sizeof(struct vm_vinteger_t)-1;
      } break;
      case VFLOAT: {
        struct vm_vfloat_t *node = (struct vm_vfloat_t *)&varstack->buffer[x];
        if(node->ret > 0) {
          struct vm_tvar_t *tmp = (struct vm_tvar_t *)&obj->ast.buffer[node->ret];
          obj->valstack.buffer[tmp->value] = x;
        }
        x += sizeof(struct vm_vfloat_t)-1;
      } break;
      case VNULL: {
        struct vm_vnull_t *node = (struct vm_vnull_t *)&varstack->buffer[x];
        if(node->ret > 0) {
          struct vm_tvar_t *tmp = (struct vm_tvar_t *)&obj->ast.buffer[node->ret];
          obj->valstack.buffer[tmp->value] = x;
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
  struct varstack_t *varstack = (struct varstack_t *)obj->userdata;
  struct vm_tvar_t *var = (struct vm_tvar_t *)&obj->ast.buffer[token];
  int ret = 0, x = 0, loop = 1;

  /*
   * Remove previous value linked to
   * the variable being set.
   */

  for(x=4;alignedbytes(x)<varstack->nrbytes && loop == 1;x++) {
    x = alignedbytes(x);
#ifdef DEBUG
    printf(". %s %d %d\n", __FUNCTION__, __LINE__, x);
#endif
    switch(varstack->buffer[x]) {
      case VINTEGER: {
        struct vm_vinteger_t *node = (struct vm_vinteger_t *)&varstack->buffer[x];
        struct vm_tvar_t *tmp = (struct vm_tvar_t *)&obj->ast.buffer[node->ret];
        if(strcmp((char *)var->token, (char *)tmp->token) == 0) {
          obj->valstack.buffer[var->value] = 0;
          vm_value_del(obj, x);
          loop = 0;
          break;
        }
        x += sizeof(struct vm_vinteger_t)-1;
      } break;
      case VFLOAT: {
        struct vm_vfloat_t *node = (struct vm_vfloat_t *)&varstack->buffer[x];
        struct vm_tvar_t *tmp = (struct vm_tvar_t *)&obj->ast.buffer[node->ret];
        if(strcmp((char *)var->token, (char *)tmp->token) == 0) {
          obj->valstack.buffer[var->value] = 0;
          vm_value_del(obj, x);
          loop = 0;
          break;
        }
        x += sizeof(struct vm_vfloat_t)-1;
      } break;
      case VNULL: {
        struct vm_vnull_t *node = (struct vm_vnull_t *)&varstack->buffer[x];
        struct vm_tvar_t *tmp = (struct vm_tvar_t *)&obj->ast.buffer[node->ret];
        if(strcmp((char *)var->token, (char *)tmp->token) == 0) {
          obj->valstack.buffer[var->value] = 0;
          vm_value_del(obj, x);
          loop = 0;
          break;
        }
        x += sizeof(struct vm_vnull_t)-1;
      } break;
      default: {
        printf("err: %s %d\n", __FUNCTION__, __LINE__);
        exit(-1);
      } break;
    }
  }


  var = (struct vm_tvar_t *)&obj->ast.buffer[token];

  if(obj->valstack.buffer[var->value] > 0) {
    vm_value_del(obj, obj->valstack.buffer[var->value]);
  }
  var = (struct vm_tvar_t *)&obj->ast.buffer[token];

  ret = varstack->nrbytes;

  obj->valstack.buffer[var->value] = ret;

#ifdef DEBUG
  printf(". %s %d %d\n", __FUNCTION__, __LINE__, val);
#endif
  switch(obj->varstack.buffer[val]) {
    case VINTEGER: {
      unsigned int size = alignedbytes(varstack->nrbytes+sizeof(struct vm_vinteger_t));
      if((varstack->buffer = (unsigned char *)REALLOC(varstack->buffer, alignedbuffer(size))) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      struct vm_vinteger_t *cpy = (struct vm_vinteger_t *)&obj->varstack.buffer[val];
      struct vm_vinteger_t *value = (struct vm_vinteger_t *)&varstack->buffer[ret];
      value->type = VINTEGER;
      value->ret = token;
      value->value = (int)cpy->value;

#ifdef DEBUG
      printf(". %s %d %d %d\n", __FUNCTION__, __LINE__, x, (int)cpy->value);
#endif
      varstack->nrbytes = alignedbytes(varstack->nrbytes + sizeof(struct vm_vinteger_t));
      varstack->bufsize = alignedbuffer(size);
    } break;
    case VFLOAT: {
      unsigned int size = varstack->nrbytes+sizeof(struct vm_vfloat_t);
      if((varstack->buffer = (unsigned char *)REALLOC(varstack->buffer, alignedbuffer(size))) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      struct vm_vfloat_t *cpy = (struct vm_vfloat_t *)&obj->varstack.buffer[val];
      struct vm_vfloat_t *value = (struct vm_vfloat_t *)&varstack->buffer[ret];
      value->type = VFLOAT;
      value->ret = token;
      value->value = cpy->value;

#ifdef DEBUG
      printf(". %s %d %d %g\n", __FUNCTION__, __LINE__, x, cpy->value);
#endif
      varstack->nrbytes = alignedbytes(varstack->nrbytes + sizeof(struct vm_vfloat_t));
      varstack->bufsize = alignedbuffer(size);
    } break;
    case VNULL: {
      unsigned int size = varstack->nrbytes+sizeof(struct vm_vnull_t);
      if((varstack->buffer = (unsigned char *)REALLOC(varstack->buffer, alignedbuffer(size))) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      struct vm_vnull_t *value = (struct vm_vnull_t *)&varstack->buffer[ret];
      value->type = VNULL;
      value->ret = token;

#ifdef DEBUG
      printf(". %s %d %d NULL\n", __FUNCTION__, __LINE__, x);
#endif
      varstack->nrbytes = alignedbytes(varstack->nrbytes + sizeof(struct vm_vnull_t));
      varstack->bufsize = alignedbuffer(size);
    } break;
    default: {
      printf("err: %s %d\n", __FUNCTION__, __LINE__);
      exit(-1);
    } break;
  }
}

static void vm_value_prt(struct rules_t *obj, char *out, int size) {
  struct varstack_t *varstack = (struct varstack_t *)obj->userdata;
  int x = 0, pos = 0;

  for(x=4;alignedbytes(x)<varstack->nrbytes;x++) {
    if(alignedbytes(x) < varstack->nrbytes) {
      x = alignedbytes(x);
      switch(varstack->buffer[x]) {
        case VINTEGER: {
          struct vm_vinteger_t *val = (struct vm_vinteger_t *)&varstack->buffer[x];
          switch(obj->ast.buffer[val->ret]) {
            case TVAR: {
              struct vm_tvar_t *node = (struct vm_tvar_t *)&obj->ast.buffer[val->ret];
              pos += snprintf(&out[pos], size - pos, "%s = %d", node->token, val->value);
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
          switch(obj->ast.buffer[val->ret]) {
            case TVAR: {
              struct vm_tvar_t *node = (struct vm_tvar_t *)&obj->ast.buffer[val->ret];
              pos += snprintf(&out[pos], size - pos, "%s = %g", node->token, val->value);
            } break;
            default: {
              // printf("err: %s %d\n", __FUNCTION__, __LINE__);
              // exit(-1);
            } break;
          }
          x += sizeof(struct vm_vfloat_t)-1;
        } break;
        case VNULL: {
          struct vm_vnull_t *val = (struct vm_vnull_t *)&varstack->buffer[x];
          switch(obj->ast.buffer[val->ret]) {
            case TVAR: {
              struct vm_tvar_t *node = (struct vm_tvar_t *)&obj->ast.buffer[val->ret];
              pos += snprintf(&out[pos], size - pos, "%s = NULL", node->token);
            } break;
            default: {
              // printf("err: %s %d\n", __FUNCTION__, __LINE__);
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
}

static int event_cb(struct rules_t *obj, char *name) {
  struct rules_t *called = NULL;

  if(obj->caller > 0 && name == NULL) {
    called = rules[obj->caller-1];

#ifdef ESP8266
    memset(&out, 0, OUTPUT_SIZE);
    snprintf((char *)&out, OUTPUT_SIZE, "- continuing with caller #%d at step #%d", obj->caller, called->cont.go);
    Serial.println(out);
#else
    printf("- continuing with caller #%d at step #%d\n", obj->caller, called->cont.go);
#endif

    obj->caller = 0;

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

    called->caller = obj->nr;

#ifdef ESP8266
    memset(&out, 0, OUTPUT_SIZE);
    snprintf((char *)&out, OUTPUT_SIZE, "- running event \"%s\" called from caller #%d", name, obj->nr);
    Serial.println(out);
#else
    printf("- running event \"%s\" called from caller #%d\n", name, obj->nr);
#endif

    return rule_run(called, 0);
  }
}

void run_test(int *i) {
  memset(&rule_options, 0, sizeof(struct rule_options_t));
  rule_options.is_token_cb = is_variable;
  rule_options.is_event_cb = is_event;
  rule_options.set_token_val_cb = vm_value_set;
  rule_options.get_token_val_cb = vm_value_get;
  rule_options.prt_token_val_cb = vm_value_prt;
  rule_options.cpy_token_val_cb = vm_value_cpy;
  rule_options.event_cb = event_cb;

  int nrtests = sizeof(unittests)/sizeof(unittests[0]), x = 0;

#ifdef ESP8266
  if(*i >= nrtests) {
    delay(3);
    *i = 0;
  }
#endif

  int len = strlen(unittests[(*i)].rule), oldoffset = 0;
  struct varstack_t *varstack = (struct varstack_t *)MALLOC(sizeof(struct varstack_t));
  if(varstack == NULL) {
    OUT_OF_MEMORY
  }
  varstack->buffer = NULL;
  varstack->nrbytes = 4;
  varstack->bufsize = 4;

  int ret = 0;

#ifndef ESP8266
  mempool = (unsigned char *)MALLOC(MEMPOOL_SIZE);
#endif

  memset(mempool, 0, MEMPOOL_SIZE);

  unsigned int txtoffset = alignedbuffer(MEMPOOL_SIZE-len-5);
  memcpy(&mempool[alignedbuffer(MEMPOOL_SIZE-len-5)], unittests[(*i)].rule, len);
  char *text = (char *)&mempool[txtoffset];
  char *cpytxt = STRDUP(text);
  unsigned int memoffset = 0;

  oldoffset = txtoffset;
  while((ret = rule_initialize(&text, &txtoffset, &rules, &nrrules, (unsigned char *)mempool, &memoffset, varstack)) == 0) {
    int size = txtoffset - oldoffset;

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

#ifdef ESP8266
    memset(&out, 0, OUTPUT_SIZE);
    if(size > 49) {
      size = MIN(size, 45);
      snprintf((char *)&out, OUTPUT_SIZE, "Rule %.2d.%d / %.2d: [ %.*s ... %-*s ]", (*i)+1, rules[nrrules-1]->nr, nrtests, size, cpytxt, 46-size, " ");
    } else {
      size = MIN(size, 50);
      snprintf((char *)&out, OUTPUT_SIZE, "Rule %.2d.%d / %.2d: [ %-*s %-*s ]", (*i)+1, rules[nrrules-1]->nr, nrtests, size, cpytxt, 50-size, " ");
    }
    Serial.println(out);
#else
    if(size > 49) {
      size = MIN(size, 45);
      printf("Rule %.2d.%d / %.2d: [ %.*s ... %-*s ]\n", (*i)+1, rules[nrrules-1]->nr, nrtests, size, cpytxt, 46-size, " ");
    } else {
      size = MIN(size, 50);
      printf("Rule %.2d.%d / %.2d: [ %.*s %-*s ]\n", (*i)+1, rules[nrrules-1]->nr, nrtests, size, cpytxt, 50-size, " ");
    }
#endif

#ifdef DEBUG
    printf("AST stack is   %d bytes, local stack is   %d bytes, variable stack is   %d bytes\n", rules[nrrules-1]->ast.nrbytes, rules[nrrules-1]->varstack.nrbytes, (varstack->nrbytes));
    printf("AST bufsize is %d bytes, local bufsize is %d bytes, variable bufsize is %d bytes\n", rules[nrrules-1]->ast.bufsize, rules[nrrules-1]->varstack.bufsize, (varstack->bufsize));
#endif

    valprint(rules[nrrules-1], (char *)&out, OUTPUT_SIZE);

    if(strcmp(out, unittests[(*i)].validate[rules[nrrules-1]->nr-1].output) != 0) {
#ifdef ESP8266
      memset(&out, 0, OUTPUT_SIZE);
      snprintf((char *)&out, OUTPUT_SIZE, "Expected: %s\nWas: %s", unittests[(*i)].validate[rules[nrrules-1]->nr-1].output, out);
      Serial.println(out);
#else
      printf("Expected: %s\n", unittests[(*i)].validate[rules[nrrules-1]->nr-1].output);
      printf("Was: %s\n", out);
#endif
      exit(-1);
    }

#ifndef ESP8266
    if((rules[nrrules-1]->ast.nrbytes + (varstack->nrbytes - 4)) != unittests[(*i)].validate[rules[nrrules-1]->nr-1].bytes) {
      // memset(&out, 0, OUTPUT_SIZE);
      // snprintf((char *)&out, OUTPUT_SIZE, "Expected: %d\nWas: %d", unittests[(*i)].validate.bytes, rule.nrbytes);
      // Serial.println(out);
// #else
      printf("Expected: %d\n", unittests[(*i)].validate[rules[nrrules-1]->nr-1].bytes);
      printf("Was: %d\n", rules[nrrules-1]->ast.nrbytes + (varstack->nrbytes - 4));

      exit(-1);
    }
#endif

    for(x=0;x<5;x++) {
      varstack->nrbytes = 4;
      varstack->bufsize = 4;
      varstack->buffer = (unsigned char *)REALLOC(varstack->buffer, 4);
      memset(varstack->buffer, 0, 4);

#ifdef DEBUG
      clock_gettime(CLOCK_MONOTONIC, &rules[nrrules-1]->timestamp.first);
      printf("bytecode is %d bytes\n", rules[nrrules-1]->ast.nrbytes);
#endif
      rule_run(rules[nrrules-1], 0);
#ifdef DEBUG
      clock_gettime(CLOCK_MONOTONIC, &rules[nrrules-1]->timestamp.second);

      printf("rule #%d was executed in %.6f seconds\n", rules[nrrules-1]->nr,
        ((double)rules[nrrules-1]->timestamp.second.tv_sec + 1.0e-9*rules[nrrules-1]->timestamp.second.tv_nsec) -
        ((double)rules[nrrules-1]->timestamp.first.tv_sec + 1.0e-9*rules[nrrules-1]->timestamp.first.tv_nsec));

      printf("bytecode is %d bytes\n", rules[nrrules-1]->ast.nrbytes);
#endif

#ifndef ESP8266
      if((rules[nrrules-1]->ast.nrbytes + (varstack->nrbytes - 4)) != unittests[(*i)].run[rules[nrrules-1]->nr-1].bytes) {
        // memset(&out, 0, OUTPUT_SIZE);
        // snprintf((char *)&out, OUTPUT_SIZE, "Expected: %d\nWas: %d", unittests[(*i)].run.bytes, rule.nrbytes);
        // Serial.println(out);
// #else
        printf("Expected: %d\n", unittests[(*i)].run[rules[nrrules-1]->nr-1].bytes);
        printf("Was: %d\n", rules[nrrules-1]->ast.nrbytes + (varstack->nrbytes - 4));

        exit(-1);
      }
#endif

      valprint(rules[nrrules-1], (char *)&out, OUTPUT_SIZE);
      if(strcmp(out, unittests[(*i)].run[rules[nrrules-1]->nr-1].output) != 0) {
#ifdef ESP8266
        memset(&out, 0, OUTPUT_SIZE);
        snprintf((char *)&out, OUTPUT_SIZE, "Expected: %s\nWas: %s", unittests[(*i)].run[rules[nrrules-1]->nr-1].output, out);
        Serial.println(out);
#else
        printf("Expected: %s\n", unittests[(*i)].run[rules[nrrules-1]->nr-1].output);
        printf("Was: %s\n", out);
#endif
        exit(-1);
      }
      fflush(stdout);
    }

    varstack = (struct varstack_t *)MALLOC(sizeof(struct varstack_t));
    if(varstack == NULL) {
      OUT_OF_MEMORY
    }
    varstack->buffer = NULL;
    varstack->nrbytes = 4;
    varstack->bufsize = 4;

    FREE(cpytxt);
    text = (char *)&mempool[txtoffset];
    oldoffset = txtoffset;
    cpytxt = STRDUP(text);
  }
  if(unittests[(*i)].run[0].bytes > 0 && ret == -1) {
    exit(-1);
  }

  if(ret == -1) {
    const char *rule = unittests[(*i)].rule;
    int size = strlen(unittests[(*i)].rule);
#ifdef ESP8266
    memset(&out, 0, OUTPUT_SIZE);
    if(size > 49) {
      size = MIN(size, 45);
      snprintf((char *)&out, OUTPUT_SIZE, "Rule %.2d.%d / %.2d: [ %.*s ... %-*s ]", (*i)+1, 1, nrtests, size, rule, 46-size, " ");
    } else {
      size = MIN(size, 50);
      snprintf((char *)&out, OUTPUT_SIZE, "Rule %.2d.%d / %.2d: [ %-*s %-*s ]", (*i)+1, 1, nrtests, size, rule, 50-size, " ");
    }
    Serial.println(out);
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
      struct varstack_t *node = (struct varstack_t *)rules[x]->userdata;
      FREE(node->buffer);
      FREE(node);
    }
  }

  if(nrrules > 0) {
    rules_gc(&rules, nrrules);
  }
  FREE(cpytxt);
  FREE(varstack);
#ifndef ESP8266
  FREE(mempool);
#endif
  nrrules = 0;
}

#ifndef ESP8266
int main(int argc, char **argv) {
  int nrtests = sizeof(unittests)/sizeof(unittests[0]), i = 0;

  for(i=0;i<nrtests;i++) {
    run_test(&i);
  }
}
#endif
