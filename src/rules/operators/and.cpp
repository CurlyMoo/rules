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

int8_t rule_operator_and_callback(struct rules_t *obj, uint16_t a, uint16_t b, uint16_t *ret) {
  struct vm_vinteger_t out;
  out.ret = 0;
  out.type = VINTEGER;

  unsigned char nodeA[MAX_VARSTACK_NODE_SIZE+1], nodeB[MAX_VARSTACK_NODE_SIZE+1];
  rule_stack_pull(&obj->varstack, a, nodeA);
  rule_stack_pull(&obj->varstack, b, nodeB);

  /*
   * Values can only be equal when the type matches
   */
  switch(nodeA[0]) {
    case VNULL: {
      out.value = 0;
    } break;
    case VINTEGER: {
      struct vm_vinteger_t *n = (struct vm_vinteger_t *)&nodeA[0];
      if(n->value > 0) {
        out.value = 1;
      } else {
        out.value = 0;
      }

/* LCOV_EXCL_START*/
#ifdef DEBUG
      printf("%s %d\n", __FUNCTION__, n->value);
#endif
/* LCOV_EXCL_STOP*/

    } break;
    case VFLOAT: {
      struct vm_vfloat_t *n = (struct vm_vfloat_t *)&nodeA[0];
      float av = 0.0;
      uint322float(n->value, &av);

      if(av > 0) {
        out.value = 1;
      } else {
        out.value = 0;
      }

/* LCOV_EXCL_START*/
#ifdef DEBUG
      printf("%s %g\n", __FUNCTION__, av);
#endif
    } break;
    /*
     * FIXME
     */
    /* LCOV_EXCL_START*/
    case VCHAR: {
    } break;
    /* LCOV_EXCL_STOP*/
  }
  switch(nodeB[0]) {
    case VNULL: {
      out.value = 0;
    } break;
    case VINTEGER: {
      struct vm_vinteger_t *n = (struct vm_vinteger_t *)&nodeB[0];
      if(n->value > 0 && out.value == 1) {
        out.value = 1;
      } else {
        out.value = 0;
      }

#ifdef DEBUG
      printf("%s %d\n", __FUNCTION__, n->value);
#endif

    } break;
    case VFLOAT: {
      struct vm_vfloat_t *n = (struct vm_vfloat_t *)&nodeB[0];
      float av = 0.0;
      uint322float(n->value, &av);

      if(n->value > 0 && out.value == 1) {
        out.value = 1;
      } else {
        out.value = 0;
      }
/* LCOV_EXCL_START*/
#ifdef DEBUG
      printf("%s %g\n", __FUNCTION__, av);
#endif
    } break;
    /*
     * FIXME
     */
    /* LCOV_EXCL_START*/
    case VCHAR: {
    } break;
    /* LCOV_EXCL_STOP*/
  }

  *ret = rule_stack_push(&obj->varstack, &out);

  return 0;
}