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
#include "main.h"

static int testnr = 0;
static unsigned int nexttime = 0;

void setupSerial() {
  Serial.begin(115200);
  Serial.flush();
}

void setup() {
  setupSerial();
  Serial.println(ESP.getFreeHeap(),DEC);
}

void loop() {
  ESP.wdtFeed();
  if(millis() > nexttime) {
    ESP.wdtFeed();
    nexttime = millis() + (1000 * 1);
    Serial.println(ESP.getFreeHeap(), DEC);

    ESP.wdtFeed();
    run_test(&testnr);
    testnr++;
  }
}
