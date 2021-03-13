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

int event_operator_ge_callback(struct rules_t *obj, int a, int b, int *ret) {
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
  if(
      (
        obj->bytecode[a] == VNULL || obj->bytecode[a] == VCHAR ||
        obj->bytecode[b] == VNULL || obj->bytecode[b] == VCHAR
      )
    &&
      (obj->bytecode[a] != obj->bytecode[b])
    ) {
    out->value = 0;
  } else {
    switch(obj->bytecode[a]) {
      case VNULL: {
        out->value = 0;

/* LCOV_EXCL_START*/
#ifdef DEBUG
        printf("%s NULL\n", __FUNCTION__);
#endif
/* LCOV_EXCL_STOP*/
      } break;
      case VINTEGER: {
        float av = 0.0;
        float bv = 0.0;
        if(obj->bytecode[a] == VINTEGER) {
          struct vm_vinteger_t *na = (struct vm_vinteger_t *)&obj->bytecode[a];
          av = (float)na->value;
        }
        if(obj->bytecode[b] == VFLOAT) {
          struct vm_vfloat_t *nb = (struct vm_vfloat_t *)&obj->bytecode[b];
          bv = nb->value;
        }
        if(obj->bytecode[b] == VINTEGER) {
          struct vm_vinteger_t *nb = (struct vm_vinteger_t *)&obj->bytecode[b];
          bv = (float)nb->value;
        }
        if(av > bv || abs(av-bv) < EPSILON) {
          out->value = 1;
        } else {
          out->value = 0;
        }

/* LCOV_EXCL_START*/
#ifdef DEBUG
        printf("%s %g %g\n", __FUNCTION__, av, bv);
#endif
/* LCOV_EXCL_STOP*/
      } break;
      case VFLOAT: {
        float av = 0.0;
        float bv = 0.0;
        if(obj->bytecode[a] == VFLOAT) {
          struct vm_vfloat_t *na = (struct vm_vfloat_t *)&obj->bytecode[a];
          av = na->value;
        }
        if(obj->bytecode[b] == VFLOAT) {
          struct vm_vfloat_t *nb = (struct vm_vfloat_t *)&obj->bytecode[b];
          bv = nb->value;
        }
        if(obj->bytecode[b] == VINTEGER) {
          struct vm_vinteger_t *nb = (struct vm_vinteger_t *)&obj->bytecode[b];
          bv = (float)nb->value;
        }
        if(av > bv || abs(av-bv) < EPSILON) {
          out->value = 1;
        } else {
          out->value = 0;
        }

/* LCOV_EXCL_START*/
#ifdef DEBUG
        printf("%s %g %g\n", __FUNCTION__, av, bv);
#endif
/* LCOV_EXCL_STOP*/
      } break;
      /*
       * FIXME
       */
      /* LCOV_EXCL_START*/
      case VCHAR: {
        struct vm_vchar_t *na = (struct vm_vchar_t *)&obj->bytecode[a];
        struct vm_vchar_t *nb = (struct vm_vchar_t *)&obj->bytecode[b];
        if(na->value >= nb->value) {
          out->value = 1;
        } else {
          out->value = 0;
        }
      } break;
      /* LCOV_EXCL_STOP*/
    }
  }

  obj->nrbytes += sizeof(struct vm_vinteger_t);

  return 0;
}