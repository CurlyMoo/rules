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

int event_operator_divide_callback(struct rules_t *obj, int a, int b, int *ret) {
  *ret = obj->nrbytes;

  if((obj->bytecode[a]) == VNULL || (obj->bytecode[b]) == VNULL) {
    if((obj->bytecode = (unsigned char *)REALLOC(obj->bytecode, obj->nrbytes+sizeof(struct vm_vnull_t))) == NULL) {
      OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
    }
    struct vm_vnull_t *out = (struct vm_vnull_t *)&obj->bytecode[obj->nrbytes];

    out->ret = 0;
    out->type = VNULL;

/* LCOV_EXCL_START*/
#ifdef DEBUG
    printf("%s NULL\n", __FUNCTION__);
#endif
/* LCOV_EXCL_STOP*/

    obj->nrbytes += sizeof(struct vm_vnull_t);
  } else if((obj->bytecode[a]) == VCHAR || (obj->bytecode[b]) == VCHAR) {
  } else if((obj->bytecode[a]) == VFLOAT || (obj->bytecode[b]) == VFLOAT) {
  } else {
    struct vm_vinteger_t *na = (struct vm_vinteger_t *)&obj->bytecode[a];
    struct vm_vinteger_t *nb = (struct vm_vinteger_t *)&obj->bytecode[b];

/* LCOV_EXCL_START*/
#ifdef DEBUG
    printf("%s %d %d\n", __FUNCTION__, na->value, nb->value);
#endif
/* LCOV_EXCL_STOP*/

    float var = (float)na->value / (float)nb->value;
    float nr = 0;

    if(modff(var, &nr) == 0) {
      if((obj->bytecode = (unsigned char *)REALLOC(obj->bytecode, obj->nrbytes+sizeof(struct vm_vinteger_t))) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      struct vm_vinteger_t *out = (struct vm_vinteger_t *)&obj->bytecode[obj->nrbytes];

      out->ret = 0;
      out->type = VINTEGER;
      out->value = (int)var;

      obj->nrbytes += sizeof(struct vm_vinteger_t);
    } else {
      if((obj->bytecode = (unsigned char *)REALLOC(obj->bytecode, obj->nrbytes+sizeof(struct vm_vfloat_t))) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      struct vm_vfloat_t *out = (struct vm_vfloat_t *)&obj->bytecode[obj->nrbytes];

      out->ret = 0;
      out->type = VFLOAT;
      out->value = var;

      obj->nrbytes += sizeof(struct vm_vfloat_t);
    }
  }

  return 0;
}
