/*
  Copyright (C) CurlyMo

  This Source Code Form is subject to the terms of the Mozilla Public
  License, v. 2.0. If a copy of the MPL was not distributed with this
  file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#ifndef _RULES_MIN_H_
#define _RULES_MIN_H_

#include <stdint.h>
#include "../rules.h"

int8_t rule_function_min_callback(struct rules_t *obj, uint16_t argc, uint16_t *argv, uint16_t *ret);

#endif