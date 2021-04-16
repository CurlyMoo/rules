/*
  Copyright (C) CurlyMo

  This Source Code Form is subject to the terms of the Mozilla Public
  License, v. 2.0. If a copy of the MPL was not distributed with this
  file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#ifdef ESP8266
  #pragma GCC diagnostic warning "-fpermissive"
#endif

#ifdef ESP8266
#include <Arduino.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "function.h"
#include "mem.h"
#include "rules.h"

int rule_function_pinmode_callback(struct rules_t *obj, uint16_t argc, uint16_t *argv, int *ret) {
/* LCOV_EXCL_START*/
#ifdef DEBUG
  printf("%s\n", __FUNCTION__);
#endif
/* LCOV_EXCL_STOP*/

  *ret = obj->varstack.nrbytes;

  if(argc != 2) {
    return -1;
  }

  unsigned int size = alignedbytes(obj->varstack.nrbytes+sizeof(struct vm_vnull_t));
  if((obj->varstack.buffer = (unsigned char *)REALLOC(obj->varstack.buffer, alignedbuffer(size))) == NULL) {
    OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
  }
  struct vm_vnull_t *out = (struct vm_vnull_t *)&obj->varstack.buffer[obj->varstack.nrbytes];
  out->ret = 0;
  out->type = VNULL;

  if(obj->varstack.buffer[argv[0]] != VINTEGER) {
    return -1;
  }

  if(obj->varstack.buffer[argv[1]] != VINTEGER) {
    return -1;
  }

  struct vm_vinteger_t *val = (struct vm_vinteger_t *)&obj->varstack.buffer[argv[0]];
  int gpio = val->value;

  val = (struct vm_vinteger_t *)&obj->varstack.buffer[argv[1]];

  pinMode(gpio, val->value);

  obj->varstack.nrbytes = size;

  return 0;
}
