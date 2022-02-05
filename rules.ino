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

#include "src/rules/rules.h"
#include "src/common/mem.h"
#include "main.h"

static int testnr = 0;
static int fail = 0;
static int heap = 0;
static unsigned int nexttime = 0;
static unsigned char *mempool = NULL;

void setupSerial() {
  Serial.begin(115200);
  Serial.flush();
}

void setup() {
  setupSerial();

  mempool = (unsigned char *)MALLOC(MEMPOOL_SIZE);

  if(mempool == NULL) {
    fprintf(stderr, "OUT_OF_MEMORY\n");
    exit(-1);
  }

  rst_info *resetInfo = ESP.getResetInfoPtr();
  Serial.printf(PSTR("Reset reason: %d, exception cause: %d\n"), resetInfo->reason, resetInfo->exccause);

  if(resetInfo->reason > 0 && resetInfo->reason <= 4) {
    Serial.println("Unittests failed");
    fail = 1;
  }
}

void loop() {
  if(fail == 0) {
    ESP.wdtFeed();
    if(millis() > nexttime) {
      ESP.wdtFeed();
      nexttime = millis() + (100 * 1);

      ESP.wdtFeed();

      if(heap == 0) {
        Serial.println(F("Running from 1st heap"));
        memset(mempool, 0, MEMPOOL_SIZE);
        run_test(&testnr, mempool, MEMPOOL_SIZE);
      } else {
        Serial.println(F("Running from 2nd heap"));
        memset((unsigned char *)MMU_SEC_HEAP, 0, MEMPOOL_SIZE);
        run_test(&testnr, (unsigned char *)MMU_SEC_HEAP, MEMPOOL_SIZE);
      }

      testnr += heap;
      heap ^= 1;
    }
  }
}
