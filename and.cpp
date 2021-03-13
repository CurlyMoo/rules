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


int event_operator_and_callback(struct rules_t *obj, int a, int b, int *ret) {
  *ret = obj->nrbytes;

  if((obj->bytecode = (unsigned char *)REALLOC(obj->bytecode, obj->nrbytes+sizeof(struct vm_vinteger_t))) == NULL) {
    OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
  }
  struct vm_vinteger_t *out = (struct vm_vinteger_t *)&obj->bytecode[obj->nrbytes];
  out->ret = 0;
  out->type = VINTEGER;

  /*
   * Values can only be equal when the type matches
   */
  switch(obj->bytecode[a]) {
    case VNULL: {
      out->value = 0;
    } break;
    case VINTEGER: {
      struct vm_vinteger_t *n = (struct vm_vinteger_t *)&obj->bytecode[a];
      if(n->value > 0) {
        out->value = 1;
      } else {
        out->value = 0;
      }

/* LCOV_EXCL_START*/
#ifdef DEBUG
      printf("%s %d\n", __FUNCTION__, n->value);
#endif
/* LCOV_EXCL_STOP*/

    } break;
    case VFLOAT: {
      struct vm_vfloat_t *n = (struct vm_vfloat_t *)&obj->bytecode[a];
      if(n->value > 0) {
        out->value = 1;
      } else {
        out->value = 0;
      }
    } break;
    /*
     * FIXME
     */
    /* LCOV_EXCL_START*/
    case VCHAR: {
      out->value = 1;
    } break;
    /* LCOV_EXCL_STOP*/
  }
  switch(obj->bytecode[b]) {
    case VNULL: {
      out->value = 0;
    } break;
    case VINTEGER: {
      struct vm_vinteger_t *n = (struct vm_vinteger_t *)&obj->bytecode[b];
      if(n->value > 0 && out->value == 1) {
        out->value = 1;
      } else {
        out->value = 0;
      }

#ifdef DEBUG
      printf("%s %d\n", __FUNCTION__, n->value);
#endif

    } break;
    case VFLOAT: {
      struct vm_vfloat_t *n = (struct vm_vfloat_t *)&obj->bytecode[b];
      if(n->value > 0 && out->value == 1) {
        out->value = 1;
      } else {
        out->value = 0;
      }
    } break;
    /*
     * FIXME
     */
    /* LCOV_EXCL_START*/
    case VCHAR: {
      out->value = 1;
    } break;
    /* LCOV_EXCL_STOP*/
  }

  obj->nrbytes += sizeof(struct vm_vinteger_t);

  return 0;
}