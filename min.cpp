/*
  Copyright (C) CurlyMo

  This Source Code Form is subject to the terms of the Mozilla Public
  License, v. 2.0. If a copy of the MPL was not distributed with this
  file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#ifdef ESP8266
  #pragma GCC diagnostic warning "-fpermissive"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "function.h"
#include "mem.h"
#include "rules.h"

int event_function_min_callback(struct rules_t *obj, uint16_t argc, uint16_t *argv, int *ret) {
/* LCOV_EXCL_START*/
#ifdef DEBUG
  printf("%s\n", __FUNCTION__);
#endif
/* LCOV_EXCL_STOP*/

  *ret = obj->nrbytes;

  if((obj->bytecode = (unsigned char *)REALLOC(obj->bytecode, obj->nrbytes+sizeof(struct vm_vinteger_t))) == NULL) {
    OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
  }
  struct vm_vinteger_t *out = (struct vm_vinteger_t *)&obj->bytecode[obj->nrbytes];
  out->ret = 0;
  out->type = VINTEGER;

  int i = 0;
  for(i=0;i<argc;i++) {
    switch(obj->bytecode[argv[i]]) {
      case VINTEGER: {
        struct vm_vinteger_t *val = (struct vm_vinteger_t *)&obj->bytecode[argv[i]];
        if(i == 0) {
          out->value = val->value;
        } else if(val->value < out->value) {
          out->value = val->value;
        }
      } break;
    }
  }

  obj->nrbytes += sizeof(struct vm_vinteger_t);

  return 0;
}
