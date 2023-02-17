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

#include "../../common/uint32float.h"
#include "../rules.h"

int8_t rule_operator_ne_callback(struct rules_t *obj, uint16_t a, uint16_t b, uint16_t *ret) {
  struct vm_vinteger_t out;
  out.ret = 0;
  out.type = VINTEGER;

  unsigned char nodeA[MAX_VARSTACK_NODE_SIZE+1], nodeB[MAX_VARSTACK_NODE_SIZE+1];
  rule_stack_pull(&obj->varstack, a, nodeA);
  rule_stack_pull(&obj->varstack, b, nodeB);

  /*
   * Values can only be equal when the type matches
   */
  if(nodeA[0] != nodeB[0]) {
    out.value = 1;
  } else {
    switch(nodeA[0]) {
      case VNULL: {
        out.value = 1;

/* LCOV_EXCL_START*/
#ifdef DEBUG
        printf("%s NULL\n", __FUNCTION__);
#endif
/* LCOV_EXCL_STOP*/
      } break;
      case VINTEGER: {
        struct vm_vinteger_t *na = (struct vm_vinteger_t *)&nodeA[0];
        struct vm_vinteger_t *nb = (struct vm_vinteger_t *)&nodeB[0];
        if(na->value == nb->value) {
          out.value = 0;
        } else {
          out.value = 1;
        }

/* LCOV_EXCL_START*/
#ifdef DEBUG
        printf("%s %d %d\n", __FUNCTION__, na->value, nb->value);
#endif
/* LCOV_EXCL_STOP*/

      } break;
      case VFLOAT: {
        struct vm_vfloat_t *na = (struct vm_vfloat_t *)&nodeA[0];
        struct vm_vfloat_t *nb = (struct vm_vfloat_t *)&nodeB[0];
        if(fabs((float)na->value-(float)nb->value) < EPSILON) {
          out.value = 0;
        } else {
          out.value = 1;
        }

/* LCOV_EXCL_START*/
#ifdef DEBUG
        float av = 0.0, bv = 0.0;
        uint322float(na->value, &av);
        uint322float(nb->value, &bv);
        printf("%s %g %g\n", __FUNCTION__, av, bv);
#endif
/* LCOV_EXCL_STOP*/
      } break;
      /*
       * FIXME
       */
      /* LCOV_EXCL_START*/
      case VCHAR: {
      } break;
      /* LCOV_EXCL_STOP*/
    }
  }

  *ret = rule_stack_push(&obj->varstack, &out);

  return 0;
}