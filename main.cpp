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

#include "mem.h"
#include "rules.h"

#ifdef ESP8266
#include <Arduino.h>
#endif

struct rules_t **rules = NULL;

struct unittest_t {
  const char *rule;
  struct {
    const char *output;
    int bytes;
  } validate;
  struct {
    const char *output;
    int bytes;
  } run;
} unittests[] = {
  /*
   * Valid rules
   */
  { "if 3 == 3 then $a = 6; end", { "$a = 6", 78 }, { "$a = 6", 78 } },
  { "if 3 == 3 then $a = -6; end", { "$a = -6", 79 }, { "$a = -6", 79 } },
  { "if 3 == 3 then $a = 1 + 2; end", { "$a = 3", 95 }, { "$a = 3", 95 } },
  { "if 1 == 1 then $a = 1 + 2 + 3; end", { "$a = 6", 112 }, { "$a = 6", 112 } },
  { "if 12 == 1 then $a = 1 + 2 * 3; end", { "$a = 7", 113 }, { "$a = 7", 113 } },
  { "if 1 == 1 then $a = 3 * 1 + 2 * 3; end", { "$a = 9", 129 }, { "$a = 9", 129 } },
  { "if 1 == 1 then $a = (3 * 1 + 2 * 3); end", { "$a = 9", 139 }, { "$a = 9", 139 } },
  { "if 1 == 1 then $a = 1 * 2 + 3; end", { "$a = 5", 112 }, { "$a = 5", 112 } },
  { "if 1 == 1 then $a = 1 + 2 * 3 / 4; end", { "$a = 2.5", 129 }, { "$a = 2.5", 129 } },
  { "if 1 == 1 then $a = 1 + 2 * 3 / 4 ^ 2; end", { "$a = 1.375", 146 }, { "$a = 1.375", 146 } },
  { "if 1 == 1 then $a = 1 + 4 ^ 2 ^ 1; end", { "$a = 17", 129 }, { "$a = 17", 129 } },
  { "if 1 == 1 then $a = (1 + 4 ^ 2 ^ 1); end", { "$a = 17", 139 }, { "$a = 17", 139 } },
  { "if 1 == 1 then $a = (1 + 2 * 3 / 4 ^ 2); end", { "$a = 1.375", 156 }, { "$a = 1.375", 156 } },
  { "if 1 == 1 then $a = (1 + 2 * 3 / 4 ^ 2 ^ 1 * 3); end", { "$a = 2.125", 190 }, { "$a = 2.125", 190 } },
  { "if 1 == 1 then $a = (1 + 2 * 3 / 4 ^ 2 ^ 1 * 3 ^ 4); end", { "$a = 31.375", 207 }, { "$a = 31.375", 207 } },
  { "if 1 == 1 then $a = 1 + 2 * 3 / 4 ^ 2 ^ 1 * 3 ^ 4; end", { "$a = 31.375", 197 }, { "$a = 31.375", 197 } },
  { "if 1 == 1 then $a = (1 + 2) * 3; end", { "$a = 9", 122 }, { "$a = 9", 122 } },
  { "if 1 == 1 then $a = (1 + 2 * 3); end", { "$a = 7", 122 }, { "$a = 7", 122 } },
  { "if 1 == 1 then $a = 3 * (1 + 2); end", { "$a = 9", 122 }, { "$a = 9", 122 } },
  { "if 1 == 1 then $a = 3 * (1 + 2) * 2; end", { "$a = 18", 139 }, { "$a = 18", 139 } },
  { "if 1 == 1 then $a = 3 * (1 + 3) ^ 2; end", { "$a = 48", 139 }, { "$a = 48", 139 } },
  { "if 1 == 1 then $a = 7 + 6 * 5 / (4 - 3) ^ 2 ^ 1; end", { "$a = 37", 190 }, { "$a = 37", 190 } },
  { "if 1 == 1 then $a = 3 * (1 + 2) + 2; end", { "$a = 11", 139 }, { "$a = 11", 139 } },
  { "if 1 == 1 then $a = 3 + (1 + 2) * 2; end", { "$a = 9", 139 }, { "$a = 9", 139 } },
  { "if 1 == 1 then $a = 3 * (1 + 2 / 2) + 3; end", { "$a = 9", 156 }, { "$a = 9", 156 } },
  { "if 1 == 1 then $a = 3 * ((1 + 2) / 2) + 2; end", { "$a = 6.5", 166 }, { "$a = 6.5", 166 } },
  { "if 1 == 1 then $a = 3 * ((1 + 2) / 2 + 3); end", { "$a = 13.5", 166 }, { "$a = 13.5", 166 } },
  { "if 1 == 1 then $a = 3 * ((1 + 2) / (2 + 3)); end", { "$a = 1.8", 176 }, { "$a = 1.8", 176 } },
  { "if 1 == 1 then $a = 3 * ((1 + 2) / (2 + 3)) + 2; end", { "$a = 3.8", 193 }, { "$a = 3.8", 193 } },
  { "if 1 == 1 then $a = (1 == 1 && 1 == 0) || 5 >= 4; end", { "$a = 1", 173 }, { "$a = 1", 173 } },
  { "if 1 == 1 then $a = (1 == 1 && 1 == 0) || 3 >= 4; end", { "$a = 0", 173 }, { "$a = 0", 173 } },
  { "if 1 == 1 then $a = 3; end", { "$a = 3", 78 }, { "$a = 3", 78 } },
  { "if 1 == 2 then $a = 3; else $a = 4; end", { "$a = 4", 107 }, { "$a = 4", 107 } },
  { "if 1 == 1 then $a = 3; else $a = 4; end", { "$a = 4", 107 }, { "$a = 3", 107 } },
  { "if (1 + 1) == 1 then $a = 3; else $a = 4; end", { "$a = 4", 134 }, { "$a = 4", 134 } },
  { "if 1 == 2 || 3 >= 4 then $a = max(1, 2); end", { "$a = 2", 135 }, { "$a = 2", 135 } },
  { "if 1 == 2 || 3 >= 4 then $a = min(3, 1, 2); end", { "$a = 1", 141 }, { "$a = 1", 141 } },
  { "if 1 == 2 || 3 >= 4 then $a = 1; end", { "$a = 1", 112 }, { "$a = 1", 112 } },
  { "if 1 == 2 || 3 >= 4 || 5 == 6 then $a = max(1, 3, 2); else $b = 9; end", { "$a = 3$b = 9", 212 }, { "$a = 3$b = 9", 212 } },
  { "if 1 == 1 then $a = 1; $b = 2; end", { "$a = 1$b = 2", 107 }, { "$a = 1$b = 2", 107 } },
  { "if 1 == 1 then $a = 1; $b = ($a + 3) * 3; end", { "$a = 1$b = 12", 152 }, { "$a = 1$b = 12", 152 } },
  { "if 1 == 1 then $a = 1; $b = $a + 3 * 3; end", { "$a = 1$b = 10", 142 }, { "$a = 1$b = 10", 142 } },
  { "if 1 == 1 then if 5 == 6 then $a = 1; end $a = 2; end", { "$a = 2", 141 }, { "$a = 2", 141 } },
  { "if 1 == 1 then if 5 == 6 then $a = 1; end if 1 == 3 then $b = 3; end $a = 2; end", { "$b = 3$a = 2", 212 }, { "$b = 3$a = 2", 212 } },
  { "if 1 == 1 then $a = 1; $b = $a; end", { "$a = 1$b = 1", 118 }, { "$a = 1$b = 1", 118 } },
  { "if 1 == 1 then $a = 1; if 5 >= 4 then $a = 3; end $b = $a; end", { "$a = 3$b = 3", 181 }, { "$a = 3$b = 3", 181 } },
  { "if (1 == 1 && 1 == 0) || 5 >= 4 then $a = 1; end", { "$a = 1", 156 }, { "$a = 1", 156 } },
  { "if 3 == 3 then $a = max(1); end", { "$a = 1", 95 }, { "$a = 1", 95 } },
  { "if 3 == 3 then $a = max(1, 2); end", { "$a = 2", 101 }, { "$a = 2", 101 } },
  { "if 3 == 3 then $a = max(1, 2, 3, 4); end", { "$a = 4", 113 }, { "$a = 4", 113 } },
  { "if 3 == 3 then $a = max(1, 4, 5, 3, 2); end", { "$a = 5", 119 }, { "$a = 5", 119 } },
  { "if 3 == 3 then $a = max(max(1, 4), 2); end", { "$a = 4", 124 }, { "$a = 4", 124 } },
  { "if 3 == 3 then $a = max(1, 2) * 3; end", { "$a = 6", 118 }, { "$a = 6", 118 } },
  { "if 3 == 3 then $a = max(1, 2) * max(3, 4); end", { "$a = 8", 141 }, { "$a = 8", 141 } },
  { "if 3 == 3 then $a = max(max(1, 2), (1 * max(1, 3) ^ 2)); end", { "$a = 9", 191 }, { "$a = 9", 191 } },
  { "if 3 == 3 then $b = 1; $a = max($b + 1); end", { "$b = 1$a = 2", 142 }, { "$b = 1$a = 2", 142 } },
  { "if 3 == 3 then $b = 1; $a = max($b + 1) * 3; end", { "$b = 1$a = 6", 159 }, { "$b = 1$a = 6", 159 } },
  { "if max(1, 3) == 3 then $a = 1; end", { "$a = 1", 101 }, { "$a = 1", 101 } },
  { "if max(1, 3) == max(1, 3) then $a = 1; end", { "$a = 1", 124 }, { "$a = 1", 124 } },
  { "if 3 == 3 then $a = max(1 + 1, 2 + 2); end", { "$a = 4", 135 }, { "$a = 4", 135 } },
  { "if 3 == 3 then $a = max((1 + 3), 2); end", { "$a = 4", 128 }, { "$a = 4", 128 } },
  { "if 3 == 3 then $a = max((1 + (3 * 3)), 2); end", { "$a = 10", 155 }, { "$a = 10", 155 } },
  { "if 3 == 3 then $a = max(1 + 3 * 3, 3 * 4); end", { "$a = 12", 152 }, { "$a = 12", 152 } },
  { "if 3 == 3 then $a = max(((2 + 3) * 3), (3 * 4)); end", { "$a = 15", 182 }, { "$a = 15", 182 } },
  { "if 3 == 3 then $a = (max(1, 2) + 2) * 3; end", { "$a = 12", 145 }, { "$a = 12", 145 } },
  { "if 3 == 3 then $a = (max(1, 2) + 2) * 3; end", { "$a = 12", 145 }, { "$a = 12", 145 } },
  { "if 1 == 1 then $a = max(0, 1) + max(1, 2) * max(2, 3) / max(3, 4) ^ max(1, 2) ^ max(0, 1) * max(2, 3) ^ max(3, 4); end", { "$a = 31.375", 381 }, { "$a = 31.375", 381 } },
  { "if 3 == 3 then $a = 2; $b = max((($a + 3) * 3), (3 * 4)); end", { "$a = 2$b = 15", 212 }, { "$a = 2$b = 15", 212 } },
  { "if 3 == 3 then $a = 2; $b = max((max($a + 3) * 3), (3 * 4)); end", { "$a = 2$b = 15", 219 }, { "$a = 2$b = 15", 219 } },
  { "if 1 == 1 then $a = 1; else $a = 2; end", { "$a = 2", 107 }, { "$a = 1", 107 } },
  { "if 1 == 2 then $a = 1; elseif 2 == 2 then $a = 3; else $a = 2; end", { "$a = 2", 177 }, { "$a = 3", 177 } },
  { "if (1 == 1) || 5 >= 4 then $a = 1; if 6 == 5 then $a = 2; end $a = $a + 3; $b = (3 + $a) * 2; else $a = 7; end", { "$b = 16$a = 7", 327 }, { "$a = 4$b = 14", 327 } },
  { "if (1 == 1) || 5 >= 4 then $a = 1; if 6 == 5 then $a = 2; end $a = $a + 3; $b = (3 + $a) * 2; $b = 3; else $a = 7; end", { "$b = 3$a = 7", 348 }, { "$a = 4$b = 3", 348 } },
  { "if (1 == 1 && 1 == 0) || 5 >= 4 then $a = 1; if 6 == 5 then $a = 2; end $a = $a + 3; $b = (3 + $a * 5 + 3 * 1) * 2; @c = 5; else if 2 == 2 then $a = 6; else $a = 7; end end", { "$b = 62@c = 5$a = 7", 512 }, { "$a = 4$b = 52@c = 5", 512 } },
};

void run_test(int *i) {
  char out[1024];
  int nrtests = sizeof(unittests)/sizeof(unittests[0]), x = 0;

#ifdef ESP8266
  if(*i >= nrtests) {
    delay(3);
    *i = 0;
  }
#endif

  struct rules_t rule;
  memset(&rule, 0, sizeof(struct rules_t));
  rule.text = STRDUP(unittests[(*i)].rule);
  rule.nr = (*i)+1;

#ifdef ESP8266
  memset(&out, 0, 1024);
  if(strlen(rule.text) > 50) {
    snprintf((char *)&out, 1024, "Rule %.2d / %d: [ %.46s ... ]", rule.nr, nrtests, rule.text);
  } else {
    snprintf((char *)&out, 1024, "Rule %.2d / %d: [ %-50s ]", rule.nr, nrtests, rule.text);
  }
  Serial.println(out);
#else
  if(strlen(rule.text) > 50) {
    printf("Rule %.2d / %d: [ %.46s ... ]\n", rule.nr, nrtests, rule.text);
  } else {
    printf("Rule %.2d / %d: [ %-50s ]\n", rule.nr, nrtests, rule.text);
  }
#endif

  rule_initialize(rule.text, &rule, 0, 1);
#ifdef DEBUG
  printf("bytecode is %d bytes\n", rule.nrbytes);
#endif
  valprint(&rule, (char *)&out, 1024);

  if(strcmp(out, unittests[(*i)].validate.output) != 0) {
#ifdef ESP8266
    memset(&out, 0, 1024);
    snprintf((char *)&out, 1024, "Expected: %s\nWas: %s", unittests[(*i)].validate.output, out);
    Serial.println(out);
#else
    printf("Expected: %s\n", unittests[(*i)].validate.output);
    printf("Was: %s\n", out);
#endif
    exit(-1);
  }
#ifndef ESP8266
  if(rule.nrbytes != unittests[(*i)].validate.bytes) {
    // memset(&out, 0, 1024);
    // snprintf((char *)&out, 1024, "Expected: %d\nWas: %d", unittests[(*i)].validate.bytes, rule.nrbytes);
    // Serial.println(out);
// #else
    printf("Expected: %d\n", unittests[(*i)].validate.bytes);
    printf("Was: %d\n", rule.nrbytes);

    exit(-1);
  }
#endif

  for(x=0;x<5;x++) {
#ifdef DEBUG
    clock_gettime(CLOCK_MONOTONIC, &rule.timestamp.first);
    printf("bytecode is %d bytes\n", rule.nrbytes);
#endif
    rule_run(&rule, 0);
#ifdef DEBUG
    clock_gettime(CLOCK_MONOTONIC, &rule.timestamp.second);

    printf("rule #%d was executed in %.6f seconds\n", rule.nr,
      ((double)rule.timestamp.second.tv_sec + 1.0e-9*rule.timestamp.second.tv_nsec) -
      ((double)rule.timestamp.first.tv_sec + 1.0e-9*rule.timestamp.first.tv_nsec));

    printf("bytecode is %d bytes\n", rule.nrbytes);
#endif

#ifndef ESP8266
    if(rule.nrbytes != unittests[(*i)].run.bytes) {
      // memset(&out, 0, 1024);
      // snprintf((char *)&out, 1024, "Expected: %d\nWas: %d", unittests[(*i)].run.bytes, rule.nrbytes);
      // Serial.println(out);
// #else
      printf("Expected: %d\n", unittests[(*i)].run.bytes);
      printf("Was: %d\n", rule.nrbytes);

      exit(-1);
    }
#endif

    valprint(&rule, (char *)&out, 1024);
    if(strcmp(out, unittests[(*i)].run.output) != 0) {
#ifdef ESP8266
      memset(&out, 0, 1024);
      snprintf((char *)&out, 1024, "Expected: %s\nWas: %s", unittests[(*i)].run.output, out);
      Serial.println(out);
#else
      printf("Expected: %s\n", unittests[(*i)].run.output);
      printf("Was: %s\n", out);
#endif
      exit(-1);
    }
    fflush(stdout);
  }
  FREE(rule.text);
  FREE(rule.bytecode);
  rule_gc();
}

#ifndef ESP8266
int main(int argc, char **argv) {
  int nrtests = sizeof(unittests)/sizeof(unittests[0]), i = 0;

  for(i=0;i<nrtests;i++) {
    run_test(&i);
  }
}
#endif
