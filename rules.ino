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

static int testnr = -2;
static int fail = 0;
#ifdef MMU_SEC_HEAP
static int heap = 0;
#endif
static unsigned int nexttime = 0;
static unsigned char *mempool = NULL;

void setupSerial() {
  Serial.begin(115200);
  Serial.flush();
}

void setup() {
  setupSerial();

  /*
   * First heap allocation
   */
  mempool = (unsigned char *)MALLOC(MEMPOOL_SIZE);

  if(mempool == NULL) {
    Serial.println("OUT_OF_MEMORY\n");
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

#ifdef MMU_SEC_HEAP
      if(testnr >= 0) {
        if(heap == 0) {
#endif
          Serial.println(F("Running from 1st heap"));
          memset(mempool, 0, MEMPOOL_SIZE);
          if(testnr >= 0) {
            run_test(&testnr, mempool, MEMPOOL_SIZE);
          }
#ifdef MMU_SEC_HEAP
        } else {
          Serial.println(F("Running from 2nd heap"));
          memset((unsigned char *)MMU_SEC_HEAP, 0, MMU_SEC_HEAP_SIZE);
          if(testnr >= 0) {
            run_test(&testnr, (unsigned char *)MMU_SEC_HEAP, MMU_SEC_HEAP_SIZE);
            testnr += heap;
          }
        }
        heap ^= 1;
      } else {
        testnr++;
      }
#else
      testnr++;
#endif

      if(testnr == -1) {
        uint8_t nrtests = 6;
        struct {
          uint16_t size[2];
          uint16_t used[2];
          uint8_t loc[2];
          int8_t ret;
        } tests[nrtests] = {
          { { 750, 500 }, { 252, 0 }, {1, 0}, 0 },
          { { 300, 300 }, { 168, 84 }, {0, 1}, 0 },
          { { 300, 300 }, { 168, 84 }, {1, 0}, 0 },
          { { 300, 300 }, { 168, 84 }, {1, 1}, 0 },
          { { 300, 300 }, { 168, 84 }, {0, 0}, 0 },
          { { 175, 175 }, { 0, 120 }, {0, 0}, -1 }
        };

        for(uint8_t i=0;i<nrtests;i++) {
          struct pbuf mem;
          struct pbuf mem1;
          memset(&mem, 0, sizeof(struct pbuf));
          memset(&mem1, 0, sizeof(struct pbuf));

#ifdef MMU_SEC_HEAP
          memset((unsigned char *)MMU_SEC_HEAP, 0, MMU_SEC_HEAP_SIZE);
#endif
          memset(mempool, 0, MEMPOOL_SIZE);


#ifdef MMU_SEC_HEAP
          if(tests[i].loc[0] == 1) {
#endif
            mem.payload = &mempool[0];
#ifdef MMU_SEC_HEAP
          } else {
            mem.payload = &((unsigned char *)MMU_SEC_HEAP)[0];
          }
#endif

          mem.len = 0;
          mem.tot_len = tests[i].size[0];

#ifdef MMU_SEC_HEAP
          if(tests[i].loc[1] == 1) {
#endif
            mem1.payload = &mempool[1000];
#ifdef MMU_SEC_HEAP
          } else {
            mem1.payload = &((unsigned char *)MMU_SEC_HEAP)[1000];
          }
#endif

          mem1.len = 0;
          mem1.tot_len = tests[i].size[1];

          mem.next = &mem1;

          int8_t ret = run_two_mempools(&mem);

          if(ret != tests[i].ret || (ret == 0 && (mem.len != tests[i].used[0] || mem1.len != tests[i].used[1]))) {
            exit(-1);
          }
        }
        testnr = 0;
      }
    }
  }
}
