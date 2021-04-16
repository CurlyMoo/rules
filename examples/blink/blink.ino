/*
  Copyright (C) 2013 - 2016 CurlyMo

  This Source Code Form is subject to the terms of the Mozilla Public
  License, v. 2.0. If a copy of the MPL was not distributed with this
  file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#pragma GCC diagnostic warning "-fpermissive"

#include <Arduino.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>

#include "mem.h"
#include "rules.h"

static struct rules_t **rules = NULL;
static int nrrules = 0;
static int state = 0;
static unsigned int nexttime = 0;

struct rule_options_t rule_options;

/*
 * The first rule set the global var value to 1. The second
 * rule increases the global var #a by 5 setting it to 6.
 *
 * Pinmode value 1 is set for setting the LED to output mode.
 * A %d placeholder is created to be replaced by the BUILTIN_LED
 * constant value.
 */
static const char *test = " \
  on System#Boot then \
    pinMode(%d, 1); \
  end \
\
  on ledOn then \
    digitalWrite(%d, 1); \
  end \
\
  on ledOff then \
    digitalWrite(%d, 0); \
  end \
";

/*
 * The allowed events are hardcoded in this example. You can
 * choose yourself in what form an event can be named.
 */
static int is_event(char *text, unsigned int *pos, unsigned int size) {
  if(strncmp(&text[*pos], "ledOn", size) == 0 ||
     strncmp(&text[*pos], "ledOff", size) == 0 ||
     strncmp(&text[*pos], "System#Boot", size) == 0) {
    return 0;
  }
  return -1;
}

void setupSerial() {
  Serial.begin(115200);
  Serial.flush();
}

void setup() {
  setupSerial();
  Serial.println(ESP.getFreeHeap(),DEC);

  memset(&rule_options, 0, sizeof(struct rule_options_t));
  rule_options.is_event_cb = is_event;

  int len = strlen(test), ret = 0, i = 0;

  /*
   * To prove that the pinMode is actually
   * changed to OUTPUT from the System#Boot
   * event.
   */
  pinMode(BUILTIN_LED, INPUT);

  char *rule = STRDUP(test);
  sprintf(rule, rule, BUILTIN_LED, BUILTIN_LED, BUILTIN_LED);
  if(rule == NULL) {
    OUT_OF_MEMORY
  }

  while((ret = rule_initialize(&rule, &rules, &nrrules, NULL)) == 0);

  for(i=0;i<nrrules;i++) {
    struct vm_tstart_t *start = (struct vm_tstart_t *)&rules[i]->ast.buffer[0];
    if(rules[i]->ast.buffer[start->go] == TEVENT) {
      struct vm_tevent_t *event = (struct vm_tevent_t *)&rules[i]->ast.buffer[start->go];
      if(strcmp(event->token, "System#Boot") == 0) {
        Serial.println("System#Boot");
        rule_run(rules[i], 0);
      }
    }
  }
}

void loop() {
  ESP.wdtFeed();
  int i = 0;
  if(millis() > nexttime) {
    for(i=0;i<nrrules;i++) {
      struct vm_tstart_t *start = (struct vm_tstart_t *)&rules[i]->ast.buffer[0];
      if(rules[i]->ast.buffer[start->go] == TEVENT) {
        struct vm_tevent_t *event = (struct vm_tevent_t *)&rules[i]->ast.buffer[start->go];
        if(state == 0) {
          if(strcmp(event->token, "ledOn") == 0) {
            Serial.println("ledOn");
            rule_run(rules[i], 0);
          }
        } else {
          if(strcmp(event->token, "ledOff") == 0) {
            Serial.println("ledOff");
            rule_run(rules[i], 0);
          }
        }
      }
    }
    state ^= 1;
    nexttime = millis() + (1000 * 1);
  }
}
