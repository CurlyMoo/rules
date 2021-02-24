/*
  Copyright (C) CurlyMo

  This Source Code Form is subject to the terms of the Mozilla Public
  License, v. 2.0. If a copy of the MPL was not distributed with this
  file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>

#include "mem.h"
#include "rules.h"

#ifdef ESP8266
#include <Arduino.h>
#endif

#define max(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })

#define min(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a < _b ? _a : _b; })

static struct rules_t **rules = NULL;
static int nrrules = 0;

struct rule_options_t rule_options;

typedef struct varstack_t {
  unsigned char *stack = NULL;
  int nrbytes;
} varstack_t;

static struct vm_vinteger_t vinteger;
static struct vm_vfloat_t vfloat;

struct unittest_t {
  const char *rule;
  struct {
    const char *output;
    int bytes;
  } validate[12];
  struct {
    const char *output;
    int bytes;
  } run[12];
} unittests[] = {
  /*
   * Valid rules
   */
  { "if 3 == 3 then $a = 6; end", { { "$a = 6", 78 } }, { { "$a = 6", 78 } } },
  { "if @a == 3 then $a = 6; end", { { "$a = 6", 89 } }, { { "$a = 6", 89 } } },
  { "if 3 == 3 then @a = 6; end", { { "@a = 6", 78 } }, { { "@a = 6", 78 } } },
  { "if 3 == 3 then $a = -6; end", { { "$a = -6", 79 } }, { { "$a = -6", 79 } } },
  { "if 3 == 3 then $a = 1 + 2; end", { { "$a = 3", 95 } }, { { "$a = 3", 95 } } },
  { "if 1 == 1 then $a = 1 + 2 + 3; end", { { "$a = 6", 112 } }, { { "$a = 6", 112 } } },
  { "if 1 == 1 then $a = 6; $a = $a + 2 + $a / 3; end", { { "$a = 10", 172 } }, { { "$a = 10", 172 } } },
  { "if 12 == 1 then $a = 1 + 2 * 3; end", { { "$a = 7", 113 } }, { { "$a = 7", 113 } } },
  { "if 1 == 1 then $a = 3 * 1 + 2 * 3; end", { { "$a = 9", 129 } }, { { "$a = 9", 129 } } },
  { "if 1 == 1 then $a = (3 * 1 + 2 * 3); end", { { "$a = 9", 139 } }, { { "$a = 9", 139 } } },
  { "if 1 == 1 then $a = 1 * 2 + 3; end", { { "$a = 5", 112 } }, { { "$a = 5", 112 } } },
  { "if 1 == 1 then $a = 1 + 2 * 3 / 4; end", { { "$a = 2.5", 129 } }, { { "$a = 2.5", 129 } } },
  { "if 1 == 1 then $a = 1 + 2 * 3 / 4 ^ 2; end", { { "$a = 1.375", 146 } }, { { "$a = 1.375", 146 } } },
  { "if 1 == 1 then $a = 1 + 4 ^ 2 ^ 1; end", { { "$a = 17", 129 } }, { { "$a = 17", 129 } } },
  { "if 1 == 1 then $a = (1 + 4 ^ 2 ^ 1); end", { { "$a = 17", 139 } }, { { "$a = 17", 139 } } },
  { "if 1 == 1 then $a = (1 + 2 * 3 / 4 ^ 2); end", { "$a = 1.375", 156 }, { "$a = 1.375", 156 } },
  { "if 1 == 1 then $a = (1 + 2 * 3 / 4 ^ 2 ^ 1 * 3); end", { "$a = 2.125", 190 }, { "$a = 2.125", 190 } },
  { "if 1 == 1 then $a = (1 + 2 * 3 / 4 ^ 2 ^ 1 * 3 ^ 4); end", { "$a = 31.375", 207 }, { "$a = 31.375", 207 } },
  { "if 1 == 1 then $a = 1 + 2 * 3 / 4 ^ 2 ^ 1 * 3 ^ 4; end", { "$a = 31.375", 197 }, { "$a = 31.375", 197 } },
  { "if 1 == 1 then $a = (1 + 2) * 3; end", { "$a = 9", 122 }, { "$a = 9", 122 } },
  { "if 1 == 1 then $a = (1 + 2 * 3); end", { "$a = 7", 122 }, { "$a = 7", 122 } },
  { "if 1 == 1 then $a = 3 * (1 + 2); end", { "$a = 9", 122 }, { "$a = 9", 122 } },
  { "if 1 == 1 then $a = 3 * (1 + 2) * 2; end", { "$a = 18", 139 }, { "$a = 18", 139 } },
  { "if 1 == 1 then $a = 3 * (1 + 3) ^ 2; end", { "$a = 48", 139 }, { "$a = 48", 139 } },
  { "if 1 == 1 then $a = 7 + 6 * 5 / (4 - 3) ^ 2 ^ 1; end", { "$a = 37", 190 }, { "$a = 37", 190 } },
  { "if 1 == 1 then $a = 3 * (1 + 2) + 2; end", { "$a = 11", 139 }, { "$a = 11", 139 } },
  { "if 1 == 1 then $a = 3 + (1 + 2) * 2; end", { "$a = 9", 139 }, { "$a = 9", 139 } },
  { "if 1 == 1 then $a = 3 * (1 + 2 / 2) + 3; end", { "$a = 9", 156 }, { "$a = 9", 156 } },
  { "if 1 == 1 then $a = 3 * ((1 + 2) / 2) + 2; end", { "$a = 6.5", 166 }, { "$a = 6.5", 166 } },
  { "if 1 == 1 then $a = 3 * ((1 + 2) / 2 + 3); end", { "$a = 13.5", 166 }, { "$a = 13.5", 166 } },
  { "if 1 == 1 then $a = 3 * ((1 + 2) / (2 + 3)); end", { "$a = 1.8", 176 }, { "$a = 1.8", 176 } },
  { "if 1 == 1 then $a = 3 * ((1 + 2) / (2 + 3)) + 2; end", { "$a = 3.8", 193 }, { "$a = 3.8", 193 } },
  { "if 1 == 1 then $a = (1 == 1 && 1 == 0) || 5 >= 4; end", { "$a = 1", 173 }, { "$a = 1", 173 } },
  { "if 1 == 1 then $a = (1 == 1 && 1 == 0) || 3 >= 4; end", { "$a = 0", 173 }, { "$a = 0", 173 } },
  { "if 1 == 1 then $a = 3; end", { "$a = 3", 78 }, { "$a = 3", 78 } },
  { "if 1 == 2 then $a = 3; else $a = 4; end", { "$a = 4", 107 }, { "$a = 4", 107 } },
  { "if 1 == 1 then $a = 3; else $a = 4; end", { "$a = 4", 107 }, { "$a = 3", 107 } },
  { "if (1 + 1) == 1 then $a = 3; else $a = 4; end", { "$a = 4", 134 }, { "$a = 4", 134 } },
  { "if 1 == 2 || 3 >= 4 then $a = max(1, 2); end", { "$a = 2", 135 }, { "$a = 2", 135 } },
  { "if 1 == 2 || 3 >= 4 then $a = min(3, 1, 2); end", { "$a = 1", 141 }, { "$a = 1", 141 } },
  { "if 1 == 2 || 3 >= 4 then $a = 1; end", { "$a = 1", 112 }, { "$a = 1", 112 } },
  { "if 1 == 2 || 3 >= 4 || 5 == 6 then $a = max(1, 3, 2); else $b = 9; end", { "$a = 3$b = 9", 212 }, { "$a = 3$b = 9", 212 } },
  { "if 1 == 1 then $a = 1; $b = 2; end", { "$a = 1$b = 2", 107 }, { "$a = 1$b = 2", 107 } },
  { "if 1 == 1 then $a = 1; $b = ($a + 3) * 3; end", { "$a = 1$b = 12", 162 }, { "$a = 1$b = 12", 162 } },
  { "if 1 == 1 then $a = 1; $b = $a + 3 * 3; end", { "$a = 1$b = 10", 152 }, { "$a = 1$b = 10", 152 } },
  { "if 1 == 1 then if 5 == 6 then $a = 1; end $a = 2; end", { "$a = 2", 141 }, { "$a = 2", 141 } },
  { "if 1 == 1 then if 5 == 6 then $a = 1; end if 1 == 3 then $b = 3; end $a = 2; end", { "$b = 3$a = 2", 212 }, { "$b = 3$a = 2", 212 } },
  { "if 1 == 1 then $a = 1; $b = $a; end", { "$a = 1$b = 1", 118 }, { "$a = 1$b = 1", 118 } },
  { "if 1 == 1 then $a = 1; if 5 >= 4 then $a = 3; end $b = $a; end", { "$a = 3$b = 3", 181 }, { "$a = 3$b = 3", 181 } },
  { "if (1 == 1 && 1 == 0) || 5 >= 4 then $a = 1; end", { "$a = 1", 156 }, { "$a = 1", 156 } },
  { "if 3 == 3 then $a = max(1); end", { "$a = 1", 95 }, { "$a = 1", 95 } },
  { "if 3 == 3 then $b = 2; $a = max($b, 1); end", { "$b = 2$a = 2", 141 }, { "$b = 2$a = 2", 141 } },
  { "if 3 == 3 then $a = max(1, 2); end", { "$a = 2", 101 }, { "$a = 2", 101 } },
  { "if 3 == 3 then $a = max(1, 2, 3, 4); end", { "$a = 4", 113 }, { "$a = 4", 113 } },
  { "if 3 == 3 then $a = max(1, 4, 5, 3, 2); end", { "$a = 5", 119 }, { "$a = 5", 119 } },
  { "if 3 == 3 then $a = max(max(1, 4), 2); end", { "$a = 4", 124 }, { "$a = 4", 124 } },
  { "if 3 == 3 then $a = max(1, 2) * 3; end", { "$a = 6", 118 }, { "$a = 6", 118 } },
  { "if 3 == 3 then $a = max(1, 2) * max(3, 4); end", { "$a = 8", 141 }, { "$a = 8", 141 } },
  { "if 3 == 3 then $a = max(max(1, 2), (1 * max(1, 3) ^ 2)); end", { "$a = 9", 191 }, { "$a = 9", 191 } },
  { "if 3 == 3 then $b = 1; $a = max($b + 1); end", { "$b = 1$a = 2", 152 }, { "$b = 1$a = 2", 152 } },
  { "if 3 == 3 then $b = 1; $a = max($b + 1) * 3; end", { "$b = 1$a = 6", 169 }, { "$b = 1$a = 6", 169 } },
  { "if max(1, 3) == 3 then $a = 1; end", { "$a = 1", 101 }, { "$a = 1", 101 } },
  { "if max(1, 3) == max(1, 3) then $a = 1; end", { "$a = 1", 124 }, { "$a = 1", 124 } },
  { "if 3 == 3 then $a = max(1 + 1, 2 + 2); end", { "$a = 4", 135 }, { "$a = 4", 135 } },
  { "if 3 == 3 then $a = max((1 + 3), 2); end", { "$a = 4", 128 }, { "$a = 4", 128 } },
  { "if 3 == 3 then $a = max((1 + (3 * 3)), 2); end", { "$a = 10", 155 }, { "$a = 10", 155 } },
  { "if 3 == 3 then $a = max(1 + 3 * 3, 3 * 4); end", { "$a = 12", 152 }, { "$a = 12", 152 } },
  { "if 3 == 3 then $a = max(((2 + 3) * 3), (3 * 4)); end", { "$a = 15", 182 }, { "$a = 15", 182 } },
  { "if 3 == 3 then $a = (max(1, 2) + 2) * 3; end", { "$a = 12", 145 }, { "$a = 12", 145 } },
  { "if 3 == 3 then $a = (max(1, 2) + 2) * 3; end", { "$a = 12", 145 }, { "$a = 12", 145 } },
  { "if 1 == 1 then $a = max(0, 1) + max(1, 2) * max(2, 3) / max(3, 4) ^ max(1, 2) ^ max(0, 1) * max(2, 3) ^ max(3, 4); end", { "$a = 31.375", 381 }, { "$a = 31.375", 381 } },
  { "if 3 == 3 then $a = 2; $b = max((($a + 3) * 3), (3 * 4)); end", { "$a = 2$b = 15", 222 }, { "$a = 2$b = 15", 222 } },
  { "if 3 == 3 then $a = 2; $b = max((max($a + 3) * 3), (3 * 4)); end", { "$a = 2$b = 15", 229 }, { "$a = 2$b = 15", 229 } },
  { "if 1 == 1 then $a = 1; else $a = 2; end", { "$a = 2", 107 }, { "$a = 1", 107 } },
  { "if 1 == 2 then $a = 1; elseif 2 == 2 then $a = 3; else $a = 2; end", { "$a = 2", 177 }, { "$a = 3", 177 } },
  { "if (1 == 1) || 5 >= 4 then $a = 1; if 6 == 5 then $a = 2; end $a = $a + 3; $b = (3 + $a) * 2; else $a = 7; end", { "$b = 16$a = 7", 347 }, { "$a = 4$b = 14", 347 } },
  { "if (1 == 1) || 5 >= 4 then $a = 1; if 6 == 5 then $a = 2; end $a = $a + 3; $b = (3 + $a) * 2; $b = 3; else $a = 7; end", { "$b = 3$a = 7", 368 }, { "$a = 4$b = 3", 368 } },
  { "if (1 == 1 && 1 == 0) || 5 >= 4 then $a = 1; if 6 == 5 then $a = 2; end $a = $a + 3; $b = (3 + $a * 5 + 3 * 1) * 2; @c = 5; else if 2 == 2 then $a = 6; else $a = 7; end end", { "$b = 62@c = 5$a = 7", 532 }, { "$a = 4$b = 52@c = 5", 532 } },
  { "if 3 == 3 then $a = 6; end if 3 == 3 then $b = 3; end  ", { { "$a = 6", 78 }, { "$b = 3" , 78 } },  { { "$a = 6", 78 }, { "$b = 3" , 78 } } },
  { "on foo then $a = 6; end", { "$a = 6", 60 }, { "$a = 6", 60 } },
  { "on foo then $a = 6; $b = 3; end", { "$a = 6$b = 3", 89 }, { "$a = 6$b = 3", 89 } },
  { "on foo then @a = 6; end", { { "@a = 6", 60 } }, { { "@a = 6", 60 } } },
  { "on foo then $a = 1 + 2; end", { { "$a = 3", 77 } }, { { "$a = 3", 77 } } },
  { "on foo then if 5 == 6 then $a = 1; end $a = 2; end", { "$a = 2", 123 }, { "$a = 2", 123 } },
  { "on foo then if 5 == 6 then $a = 1; end if 1 == 3 then $b = 3; end $a = 2; end", { "$b = 3$a = 2", 194 }, { "$b = 3$a = 2", 194 } },
  { "on foo then $a = 6; end if 3 == 3 then $b = 3; end  ", { { "$a = 6", 60 }, { "$b = 3" , 78 } },  { { "$a = 6", 60 }, { "$b = 3" , 78 } } },
  { "on foo then $a = 6; end if 3 == 3 then foo(); $b = 3; end  ", { { "$a = 6", 60 }, { "$b = 3", 103 } },  { { "$a = 6", 60 }, { "$b = 3", 103 } } }
};

static int alignedbytes(int v) {
#ifdef ESP8266
  while((v++ % 4) != 0);
  return --v;
#else
  return v;
#endif
}

static int strnicmp(char const *a, char const *b, size_t len) {
  int i = 0;

  if(a == NULL || b == NULL) {
    return -1;
  }
  if(len == 0) {
    return 0;
  }

  for(;i++<len; a++, b++) {
    int d = tolower(*a) - tolower(*b);
    if(d != 0 || !*a || i == len) {
      return d;
    }
  }
  return -1;
}

static int is_variable(struct rules_t *obj, const char *text, int *pos, int size) {
  int i = 1;
  if(text[*pos] == '$' || text[*pos] == '@') {
    while(isalpha(text[*pos+i])) {
      i++;
    }
    return i;
  }
  return -1;
}

static int is_event(struct rules_t *obj, const char *text, int *pos, int size) {
  int i = 0, len = 0;
  if(size == 3 && strnicmp(&text[*pos], "foo", len) == 0) {
    return 0;
  }
  return -1;
}

static unsigned char *vm_value_get(struct rules_t *obj, uint16_t token) {
  struct varstack_t *varstack = (struct varstack_t *)obj->userdata;
  struct vm_tvar_t *var = (struct vm_tvar_t *)&obj->bytecode[token];

  if(obj->bytecode[var->token + 1] == '@') {
    memset(&vinteger, 0, sizeof(struct vm_vinteger_t));
    vinteger.type = VINTEGER;
    vinteger.value = 5;
    return (unsigned char *)&vinteger;
  } else {

    return &varstack->stack[var->value];
  }
  return NULL;
}

static void vm_value_cpy(struct rules_t *obj, uint16_t token) {
  struct varstack_t *varstack = (struct varstack_t *)obj->userdata;
  struct vm_tvar_t *var = (struct vm_tvar_t *)&obj->bytecode[token];

  int x = 0;
  for(x=4;alignedbytes(x)<varstack->nrbytes;x++) {
    x = alignedbytes(x);
    switch(varstack->stack[x]) {
      case VINTEGER: {
        struct vm_vinteger_t *val = (struct vm_vinteger_t *)&varstack->stack[x];
        struct vm_tvar_t *foo = (struct vm_tvar_t *)&obj->bytecode[val->ret];
        if(strcmp((char *)&obj->bytecode[foo->token+1], (char *)&obj->bytecode[var->token+1]) == 0 && val->ret != token) {
          var->value = foo->value;
          val->ret = token;
          foo->value = 0;
          return;
        }
        x += sizeof(struct vm_vinteger_t)-1;
      } break;
      case VFLOAT: {
        struct vm_vfloat_t *val = (struct vm_vfloat_t *)&varstack->stack[x];
        struct vm_tvar_t *foo = (struct vm_tvar_t *)&obj->bytecode[val->ret];
        if(strcmp((char *)&obj->bytecode[foo->token+1], (char *)&obj->bytecode[var->token+1]) == 0 && val->ret != token) {
          var->value = foo->value;
          val->ret = token;
          foo->value = 0;
          return;
        }
        x += sizeof(struct vm_vfloat_t)-1;
      } break;
      default: {
        printf("err: %s %d\n", __FUNCTION__, __LINE__);
        exit(-1);
      } break;
    }
  }
}

static int vm_value_del(struct rules_t *obj, uint16_t idx) {
  struct varstack_t *varstack = (struct varstack_t *)obj->userdata;
  int x = 0, ret = 0;

  if(idx == varstack->nrbytes) {
    return -1;
  }

  switch(varstack->stack[idx]) {
    case VINTEGER: {
      ret = sizeof(struct vm_vinteger_t);
      memmove(&varstack->stack[idx], &varstack->stack[idx+ret], varstack->nrbytes-idx-ret);
      if((varstack->stack = (unsigned char *)REALLOC(varstack->stack, varstack->nrbytes-ret)) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      varstack->nrbytes -= ret;
    } break;
    case VFLOAT: {
      ret = sizeof(struct vm_vfloat_t);
      memmove(&varstack->stack[idx], &varstack->stack[idx+ret], varstack->nrbytes-idx-ret);
      if((varstack->stack = (unsigned char *)REALLOC(varstack->stack, varstack->nrbytes-ret)) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      varstack->nrbytes -= ret;
    } break;
    default: {
      printf("err: %s %d\n", __FUNCTION__, __LINE__);
      exit(-1);
    } break;
  }

  /*
   * Values are linked back to their root node,
   * by their absolute position in the bytecode.
   * If a value is deleted, these positions changes,
   * so we need to update all nodes.
   */
  for(x=idx;alignedbytes(x)<varstack->nrbytes;x++) {
    x = alignedbytes(x);
    switch(varstack->stack[x]) {
      case VINTEGER: {
        struct vm_vinteger_t *node = (struct vm_vinteger_t *)&varstack->stack[x];
        if(node->ret > 0) {
          struct vm_tvar_t *tmp = (struct vm_tvar_t *)&obj->bytecode[node->ret];
          tmp->value = x;
        }
        x += sizeof(struct vm_vinteger_t)-1;
      } break;
      case VFLOAT: {
        struct vm_vfloat_t *node = (struct vm_vfloat_t *)&varstack->stack[x];
        if(node->ret > 0) {
          struct vm_tvar_t *tmp = (struct vm_tvar_t *)&obj->bytecode[node->ret];
          tmp->value = x;
        }
        x += sizeof(struct vm_vfloat_t)-1;
      } break;
      default: {
        printf("err: %s %d\n", __FUNCTION__, __LINE__);
        exit(-1);
      } break;
    }
  }

  return ret;
}

static void vm_value_set(struct rules_t *obj, uint16_t token, uint16_t val) {
  struct varstack_t *varstack = (struct varstack_t *)obj->userdata;
  struct vm_tvar_t *var = (struct vm_tvar_t *)&obj->bytecode[token];
  int ret = 0, x = 0, loop = 1;

  /*
   * Remove previous value linked to
   * the variable being set.
   */

  for(x=4;alignedbytes(x)<varstack->nrbytes && loop == 1;x++) {
    x = alignedbytes(x);
    switch(varstack->stack[x]) {
      case VINTEGER: {
        struct vm_vinteger_t *node = (struct vm_vinteger_t *)&varstack->stack[x];
        struct vm_tvar_t *tmp = (struct vm_tvar_t *)&obj->bytecode[node->ret];
        if(strcmp((char *)&obj->bytecode[var->token+1], (char *)&obj->bytecode[tmp->token+1]) == 0) {
          var->value = 0;
          vm_value_del(obj, x);
          loop = 0;
          break;
        }
        x += sizeof(struct vm_vinteger_t)-1;
      } break;
      case VFLOAT: {
        struct vm_vfloat_t *node = (struct vm_vfloat_t *)&varstack->stack[x];
        struct vm_tvar_t *tmp = (struct vm_tvar_t *)&obj->bytecode[node->ret];
        if(strcmp((char *)&obj->bytecode[var->token+1], (char *)&obj->bytecode[tmp->token+1]) == 0) {
          var->value = 0;
          vm_value_del(obj, x);
          loop = 0;
          break;
        }
        x += sizeof(struct vm_vfloat_t)-1;
      } break;
      default: {
        printf("err: %s %d\n", __FUNCTION__, __LINE__);
        exit(-1);
      } break;
    }
  }

  var = (struct vm_tvar_t *)&obj->bytecode[token];
  if(var->value > 0) {
    vm_value_del(obj, var->value);
  }
  var = (struct vm_tvar_t *)&obj->bytecode[token];

  ret = varstack->nrbytes;

  var->value = ret;

  switch(obj->bytecode[val]) {
    case VINTEGER: {
      if((varstack->stack = (unsigned char *)REALLOC(varstack->stack, alignedbytes(varstack->nrbytes)+sizeof(struct vm_vinteger_t))) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      struct vm_vinteger_t *cpy = (struct vm_vinteger_t *)&obj->bytecode[val];
      struct vm_vinteger_t *value = (struct vm_vinteger_t *)&varstack->stack[ret];
      value->type = VINTEGER;
      value->ret = token;
      value->value = (int)cpy->value;
      varstack->nrbytes = alignedbytes(varstack->nrbytes) + sizeof(struct vm_vinteger_t);
    } break;
    case VFLOAT: {
      if((varstack->stack = (unsigned char *)REALLOC(varstack->stack, alignedbytes(varstack->nrbytes)+sizeof(struct vm_vfloat_t))) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      struct vm_vfloat_t *cpy = (struct vm_vfloat_t *)&obj->bytecode[val];
      struct vm_vfloat_t *value = (struct vm_vfloat_t *)&varstack->stack[ret];
      value->type = VFLOAT;
      value->ret = token;
      value->value = cpy->value;
      varstack->nrbytes = alignedbytes(varstack->nrbytes) + sizeof(struct vm_vfloat_t);
    } break;
    default: {
      printf("err: %s %d\n", __FUNCTION__, __LINE__);
      exit(-1);
    } break;
  }
}

static void vm_value_prt(struct rules_t *obj, char *out, int size) {
  struct varstack_t *varstack = (struct varstack_t *)obj->userdata;
  int x = 0, pos = 0;

  for(x=4;alignedbytes(x)<varstack->nrbytes;x++) {
    if(alignedbytes(x) < varstack->nrbytes) {
      x = alignedbytes(x);
      switch(varstack->stack[x]) {
        case VINTEGER: {
          struct vm_vinteger_t *val = (struct vm_vinteger_t *)&varstack->stack[x];
          switch(obj->bytecode[val->ret]) {
            case TVAR: {
              struct vm_tvar_t *node = (struct vm_tvar_t *)&obj->bytecode[val->ret];
              pos += snprintf(&out[pos], size - pos, "%s = %d", &obj->bytecode[node->token+1], val->value);
            } break;
            default: {
              // printf("err: %s %d %d\n", __FUNCTION__, __LINE__, obj->bytecode[val->ret]);
              // exit(-1);
            } break;
          }
          x += sizeof(struct vm_vinteger_t)-1;
        } break;
        case VFLOAT: {
          struct vm_vfloat_t *val = (struct vm_vfloat_t *)&varstack->stack[x];
          switch(obj->bytecode[val->ret]) {
            case TVAR: {
              struct vm_tvar_t *node = (struct vm_tvar_t *)&obj->bytecode[val->ret];
              pos += snprintf(&out[pos], size - pos, "%s = %g", &obj->bytecode[node->token+1], val->value);
            } break;
            default: {
              // printf("err: %s %d\n", __FUNCTION__, __LINE__);
              // exit(-1);
            } break;
          }
          x += sizeof(struct vm_vfloat_t)-1;
        } break;
        default: {
          printf("err: %s %d\n", __FUNCTION__, __LINE__);
          exit(-1);
        } break;
      }
    }
  }
}

static int event_cb(struct rules_t *obj, const char *name) {
  struct rules_t *called = NULL;
  char out[1024];

  if(obj->caller > 0) {
    called = rules[obj->caller-1];

#ifdef ESP8266
    memset(&out, 0, 1024);
    snprintf((char *)&out, 1024, "- continuing with caller #%d at step #%d", obj->caller, called->cont.go);
    Serial.println(out);
#else
    printf("- continuing with caller #%d at step #%d\n", obj->caller, called->cont.go);
#endif

    obj->caller = 0;

    return rule_run(called, 0);
  } else {
    int i = 0;
    for(i=0;i<nrrules;i++) {
      if(rules[i] == obj) {
        called = rules[i-1];
        break;
      }
    }

    called->caller = obj->nr;

#ifdef ESP8266
    memset(&out, 0, 1024);
    snprintf((char *)&out, 1024, "- running event \"%s\" called from caller #%d", name, obj->nr);
    Serial.println(out);
#else
    printf("- running event \"%s\" called from caller #%d\n", name, obj->nr);
#endif

    return rule_run(called, 0);
  }
}

void run_test(int *i) {
  memset(&rule_options, 0, sizeof(struct rule_options_t));
  rule_options.is_token_cb = is_variable;
  rule_options.is_event_cb = is_event;
  rule_options.set_token_val_cb = vm_value_set;
  rule_options.get_token_val_cb = vm_value_get;
  rule_options.prt_token_val_cb = vm_value_prt;
  rule_options.cpy_token_val_cb = vm_value_cpy;
  rule_options.event_cb = event_cb;

  char out[1024];
  int nrtests = sizeof(unittests)/sizeof(unittests[0]), x = 0;

#ifdef ESP8266
  if(*i >= nrtests) {
    delay(3);
    *i = 0;
  }
#endif

  int pos = 0, ppos = 0, len = strlen(unittests[(*i)].rule);
  struct varstack_t *varstack = (struct varstack_t *)MALLOC(sizeof(struct varstack_t));
  if(varstack == NULL) {
    OUT_OF_MEMORY
  }
  varstack->stack = NULL;
  varstack->nrbytes = 4;

  while(rule_initialize(unittests[(*i)].rule, &pos, &rules, &nrrules, varstack) == 0) {
    const char *rule = &unittests[(*i)].rule[ppos];
    int size = (pos-ppos);
#ifdef ESP8266
    memset(&out, 0, 1024);
    if(size > 49) {
      size = min(size, 45);
      snprintf((char *)&out, 1024, "Rule %.2d.%d / %.2d: [ %.*s ... %-*s ]", (*i)+1, rules[nrrules-1]->nr, nrtests, size, rule, 46-size, " ");
    } else {
      size = min(size, 50);
      snprintf((char *)&out, 1024, "Rule %.2d.%d / %.2d: [ %-*s %-*s ]", (*i)+1, rules[nrrules-1]->nr, nrtests, size, rule, 50-size, " ");
    }
    Serial.println(out);
#else
    if(size > 49) {
      size = min(size, 45);
      printf("Rule %.2d.%d / %.2d: [ %.*s ... %-*s ]\n", (*i)+1, rules[nrrules-1]->nr, nrtests, size, rule, 46-size, " ");
    } else {
      size = min(size, 50);
      printf("Rule %.2d.%d / %.2d: [ %.*s %-*s ]\n", (*i)+1, rules[nrrules-1]->nr, nrtests, size, rule, 50-size, " ");
    }
#endif

#ifdef DEBUG
    printf("bytecode is %d bytes\n", rules[nrrules-1]->nrbytes + (varstack->nrbytes - 1));
#endif
    valprint(rules[nrrules-1], (char *)&out, 1024);

    if(strcmp(out, unittests[(*i)].validate[rules[nrrules-1]->nr-1].output) != 0) {
#ifdef ESP8266
      memset(&out, 0, 1024);
      snprintf((char *)&out, 1024, "Expected: %s\nWas: %s", unittests[(*i)].validate[rules[nrrules-1]->nr-1].output, out);
      Serial.println(out);
#else
      printf("Expected: %s\n", unittests[(*i)].validate[rules[nrrules-1]->nr-1].output);
      printf("Was: %s\n", out);
#endif
      exit(-1);
    }

#ifndef ESP8266
    if((rules[nrrules-1]->nrbytes + (varstack->nrbytes - 4)) != unittests[(*i)].validate[rules[nrrules-1]->nr-1].bytes) {
      // memset(&out, 0, 1024);
      // snprintf((char *)&out, 1024, "Expected: %d\nWas: %d", unittests[(*i)].validate.bytes, rule.nrbytes);
      // Serial.println(out);
  // #else
      printf("Expected: %d\n", unittests[(*i)].validate[rules[nrrules-1]->nr-1].bytes);
      printf("Was: %d\n", rules[nrrules-1]->nrbytes + (varstack->nrbytes - 4));

      exit(-1);
    }
#endif

    for(x=0;x<5;x++) {
#ifdef DEBUG
      clock_gettime(CLOCK_MONOTONIC, &rules[nrrules-1]->timestamp.first);
      printf("bytecode is %d bytes\n", rules[nrrules-1]->nrbytes);
#endif
      rule_run(rules[nrrules-1], 0);
#ifdef DEBUG
      clock_gettime(CLOCK_MONOTONIC, &rules[nrrules-1]->timestamp.second);

      printf("rule #%d was executed in %.6f seconds\n", rules[nrrules-1]->nr,
        ((double)rules[nrrules-1]->timestamp.second.tv_sec + 1.0e-9*rules[nrrules-1]->timestamp.second.tv_nsec) -
        ((double)rules[nrrules-1]->timestamp.first.tv_sec + 1.0e-9*rules[nrrules-1]->timestamp.first.tv_nsec));

      printf("bytecode is %d bytes\n", rules[nrrules-1]->nrbytes);
#endif

      if((rules[nrrules-1]->nrbytes + (varstack->nrbytes - 4)) != unittests[(*i)].run[rules[nrrules-1]->nr-1].bytes) {
#ifndef ESP8266
        // memset(&out, 0, 1024);
        // snprintf((char *)&out, 1024, "Expected: %d\nWas: %d", unittests[(*i)].run.bytes, rule.nrbytes);
        // Serial.println(out);
#else
        printf("Expected: %d\n", unittests[(*i)].run[rules[nrrules-1]->nr-1].bytes);
        printf("Was: %d\n", rules[nrrules-1]->nrbytes + (varstack->nrbytes - 1));

        exit(-1);
#endif
      }

      valprint(rules[nrrules-1], (char *)&out, 1024);
      if(strcmp(out, unittests[(*i)].run[rules[nrrules-1]->nr-1].output) != 0) {
#ifdef ESP8266
        memset(&out, 0, 1024);
        snprintf((char *)&out, 1024, "Expected: %s\nWas: %s", unittests[(*i)].run[rules[nrrules-1]->nr-1].output, out);
        Serial.println(out);
#else
        printf("Expected: %s\n", unittests[(*i)].run[rules[nrrules-1]->nr-1].output);
        printf("Was: %s\n", out);
#endif
        exit(-1);
      }
      fflush(stdout);
    }

    ppos = pos;

    varstack = (struct varstack_t *)MALLOC(sizeof(struct varstack_t));
    if(varstack == NULL) {
      OUT_OF_MEMORY
    }
    varstack->stack = NULL;
    varstack->nrbytes = 4;
  }

  if(nrrules > 0) {
    for(x=0;x<nrrules;x++) {
      struct varstack_t *node = (struct varstack_t *)rules[x]->userdata;
      FREE(node->stack);
      FREE(node);
    }
    rules_gc(&rules, nrrules);
  }
  nrrules = 0;

  FREE(varstack);
}

#ifndef ESP8266
int main(int argc, char **argv) {
  int nrtests = sizeof(unittests)/sizeof(unittests[0]), i = 0;

  for(i=0;i<nrtests;i++) {
    run_test(&i);
  }
}
#endif
