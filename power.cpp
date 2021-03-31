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

int event_operator_power_callback(struct rules_t *obj, int a, int b, int *ret) {
  *ret = obj->nrbytes;

  if((obj->bytecode[a]) == VNULL || (obj->bytecode[b]) == VNULL) {
    unsigned int size = obj->nrbytes+sizeof(struct vm_vnull_t);
    if((obj->bytecode = (unsigned char *)REALLOC(obj->bytecode, alignedbytes(&obj->bufsize, size))) == NULL) {
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

    obj->nrbytes = size;
  } else if((obj->bytecode[a]) == VCHAR || (obj->bytecode[b]) == VCHAR) {
  } else if((obj->bytecode[a]) == VFLOAT || (obj->bytecode[b]) == VFLOAT) {
    float f = 0;
    int i = 0;
    unsigned int size = obj->nrbytes+sizeof(struct vm_vfloat_t);
    if((obj->bytecode = (unsigned char *)REALLOC(obj->bytecode, alignedbytes(&obj->bufsize, size))) == NULL) {
      OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
    }

    struct vm_vfloat_t *out = (struct vm_vfloat_t *)&obj->bytecode[obj->nrbytes];

    if((obj->bytecode[a]) == VFLOAT) {
      struct vm_vfloat_t *na = (struct vm_vfloat_t *)&obj->bytecode[a];
      f = na->value;
    } else if((obj->bytecode[a]) == VINTEGER) {
      struct vm_vinteger_t *na = (struct vm_vinteger_t *)&obj->bytecode[a];
      i = na->value;
    }
    if((obj->bytecode[b]) == VFLOAT) {
      struct vm_vfloat_t *nb = (struct vm_vfloat_t *)&obj->bytecode[b];
      if((obj->bytecode[a]) == VFLOAT) {
        out->value = pow(f, nb->value);
/* LCOV_EXCL_START*/
#ifdef DEBUG
        printf("1 %s %g %g\n", __FUNCTION__,f, nb->value);
#endif
/* LCOV_EXCL_STOP*/
      } else if((obj->bytecode[a]) == VINTEGER) {
        out->value = pow(i, nb->value);
/* LCOV_EXCL_START*/
#ifdef DEBUG
        printf("2 %s %d %g\n", __FUNCTION__,i, nb->value);
#endif
/* LCOV_EXCL_STOP*/
      }
    } else if((obj->bytecode[b]) == VINTEGER) {
      struct vm_vinteger_t *nb = (struct vm_vinteger_t *)&obj->bytecode[b];
      out->value = pow(f, nb->value);
/* LCOV_EXCL_START*/
#ifdef DEBUG
      printf("3 %s %g %d\n", __FUNCTION__,f, nb->value);
#endif
/* LCOV_EXCL_STOP*/
    }

    out->ret = 0;
    out->type = VFLOAT;

/* LCOV_EXCL_START*/
#ifdef DEBUG
    printf("%s %g %d\n", __FUNCTION__, f, i);
#endif
/* LCOV_EXCL_STOP*/

    obj->nrbytes = size;
  } else if((obj->bytecode[a]) == VINTEGER && (obj->bytecode[b]) == VINTEGER) {
    unsigned int size = obj->nrbytes+sizeof(struct vm_vinteger_t);
    if((obj->bytecode = (unsigned char *)REALLOC(obj->bytecode, alignedbytes(&obj->bufsize, size))) == NULL) {
      OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
    }
    struct vm_vinteger_t *out = (struct vm_vinteger_t *)&obj->bytecode[obj->nrbytes];

    struct vm_vinteger_t *na = (struct vm_vinteger_t *)&obj->bytecode[a];
    struct vm_vinteger_t *nb = (struct vm_vinteger_t *)&obj->bytecode[b];
    out->ret = 0;
    out->type = VINTEGER;
    out->value = pow(na->value, nb->value);

/* LCOV_EXCL_START*/
#ifdef DEBUG
    printf("4 %s %d %d\n", __FUNCTION__, na->value, nb->value);
#endif
/* LCOV_EXCL_STOP*/
    obj->nrbytes = size;
  }


  return 0;
}
