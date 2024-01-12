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
#include "../function.h"
#include "../rules.h"

int8_t rule_function_round_callback(struct rules_t *obj, uint16_t argc, uint16_t *argv, uint16_t *ret) {
/* LCOV_EXCL_START*/
#ifdef DEBUG
  printf("%s\n", __FUNCTION__);
#endif
/* LCOV_EXCL_STOP*/

  unsigned char nodeA[rule_max_var_bytes()];
  unsigned char nodeB[rule_max_var_bytes()];

  if(argc > 2 || argc < 0) {
    return -1;
  }
  if(argc <= 2) {
    rule_stack_pull(obj->varstack, argv[0], nodeA);

    switch(nodeA[0]) {
      case VINTEGER: {
        if(argc == 1) {
          struct vm_vinteger_t out;
          out.ret = 0;
          out.type = VINTEGER;
          out.value = 0;

          struct vm_vinteger_t *val = (struct vm_vinteger_t *)nodeA;
          out.value = val->value;

          *ret = rule_stack_push(obj->varstack, &out);
        } else {
          return -1;
        }
      } break;
      case VFLOAT: {
        if(argc == 2) {
          rule_stack_pull(obj->varstack, argv[1], nodeB);

          switch(nodeB[0]) {
            case VINTEGER: {
              struct vm_vinteger_t *val = (struct vm_vinteger_t *)nodeB;
              if(val->value <= 0) {
                return -1;
              } else {
                struct vm_vfloat_t out;
                out.ret = 0;
                out.type = VFLOAT;
                out.value = 0.0;

                struct vm_vfloat_t *f = (struct vm_vfloat_t *)nodeA;
                float v = 0.0;
                uint322float(f->value, &v);

                uint8_t size = snprintf(NULL, 0, "%.*f", val->value, v)+1;
                char buf[size] = { '\0' };
                snprintf((char *)&buf, size, "%.*f", val->value, v);

                float2uint32(atof(buf), &out.value);

                *ret = rule_stack_push(obj->varstack, &out);
              }
            } break;
            default: {
              return -1;
            } break;
          }
        } else {
          struct vm_vinteger_t out;
          out.ret = 0;
          out.type = VINTEGER;
          out.value = 0;

          struct vm_vfloat_t *val = (struct vm_vfloat_t *)nodeA;
          float v = 0.0;
          uint322float(val->value, &v);
          out.value = (int)v;

          *ret = rule_stack_push(obj->varstack, &out);
        }
      } break;
    }
  }

  return 0;
}
