/*
  Copyright (C) CurlyMo

  This Source Code Form is subject to the terms of the Mozilla Public
  License, v. 2.0. If a copy of the MPL was not distributed with this
  file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#include <stdio.h>

unsigned int alignedbytes(unsigned int *size, unsigned int v) {
// #ifdef ESP8266
  if(*size < v) {
    while((v++ % 4) != 0);
    *size = --v;
    return v;
  } else {
    return *size;
  }
// #else
  // return v;
// #endif
}