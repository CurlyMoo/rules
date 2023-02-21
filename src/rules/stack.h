/*
	Copyright (C) 2013 - 2016 CurlyMo

  This Source Code Form is subject to the terms of the Mozilla Public
  License, v. 2.0. If a copy of the MPL was not distributed with this
  file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#ifndef _RULE_STACK_T_
#define _RULE_STACK_T_

/*
 * max(sizeof(vm_vfloat_t), sizeof(vm_vinteger_t), sizeof(vm_vnull_t))
 */
#define MAX_VARSTACK_NODE_SIZE 7
#define MAX_TOKEN_SIZE 32

typedef struct rule_stack_t {
  uint16_t nrbytes;
  uint16_t bufsize;

	unsigned char *buffer;
} __attribute__((aligned(4))) rule_stack_t;

uint16_t rule_stack_push(struct rule_stack_t *stack, void *in);
int8_t rule_stack_pull(struct rule_stack_t *stack, uint16_t idx, unsigned char val[MAX_VARSTACK_NODE_SIZE+1]);

#endif