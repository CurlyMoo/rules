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
#include <assert.h>

#include "mem.h"
#include "rules.h"

#ifdef ESP8266
#include <Arduino.h>
#endif

#define OUTPUT_SIZE 512

static struct rules_t **rules = NULL;
static int nrrules = 0;
static char out[OUTPUT_SIZE];

typedef struct varstack_t {
  unsigned char *buffer;
  unsigned int nrbytes;
  unsigned int bufsize;
} varstack_t;

/*
 * Global vars are stored in a global varstack
 * not associated to a single rule
 */
static struct varstack_t global_varstack;

static struct vm_vinteger_t vinteger;
static struct vm_vfloat_t vfloat;
static struct vm_vnull_t vnull;

struct rule_options_t rule_options;

/*
 * Global vars have a global scope instead of just a local
 * scope. Global vars are therefor copied across rules
 * instead of just within rules. That means that the location
 * of a global var is the combination of the position within
 * the varstack + the rule block, instead of just the
 * varstack position for local vars.
 */

typedef struct vm_gvchar_t {
  VM_GENERIC_FIELDS
  uint8_t rule;
  char value[];
} __attribute__((packed)) vm_gvchar_t;

typedef struct vm_gvnull_t {
  VM_GENERIC_FIELDS
  uint8_t rule;
} __attribute__((packed)) vm_gvnull_t;

typedef struct vm_gvinteger_t {
  VM_GENERIC_FIELDS
  uint8_t rule;
  int value;
} __attribute__((packed)) vm_gvinteger_t;

typedef struct vm_gvfloat_t {
  VM_GENERIC_FIELDS
  uint8_t rule;
  float value;
} __attribute__((packed)) vm_gvfloat_t;

/*
 * The first rule set the global var value to 1. The second
 * rule increases the global var #a by 5 setting it to 6.
 */
static const char *test = " \
  if 1 == 1 then \
    #a = 1; \
  end \
\
  if 1 == 1 then \
    #a = #a + 5; \
  end \
";


static int is_variable(char *text, unsigned int *pos, unsigned int size) {
  int i = 1, x = 0, match = 0;
  /*
   * We've defined a global var as a string prefixes
   * by a number sign.
   */
  if(text[*pos] == '#') {
    while(isalnum(text[*pos+i])) {
      i++;
    }

    return i;
  }
  return -1;
}

static void vm_value_set(struct rules_t *obj, uint16_t token, uint16_t val) {
  struct varstack_t *varstack = NULL;
  struct vm_tvar_t *var = (struct vm_tvar_t *)&obj->ast.buffer[token];
  int ret = 0, x = 0, loop = 1;

  /*
   * Inside the set function, you can differentiate between
   * various types of variables. This example just shows how
   * global variables work, so we just check for the number
   * sign as the first character of the var token.
   */
  if(var->token[0] == '#') {
    varstack = &global_varstack;

    /*
     * Print the value positioned at the 'val' location
     * being set to this variable at the 'token' location.
     *
     * Values are first set on the local stack. These values
     * follow the regular variable types and not our custom
     * global ones.
     *
     */
    const char *key = (char *)var->token;
    switch(obj->varstack.buffer[val]) {
      case VINTEGER: {
        struct vm_vinteger_t *na = (struct vm_vinteger_t *)&obj->varstack.buffer[val];
        sprintf((char *)&out, ".. %s %d %s = %d", __FUNCTION__, __LINE__, key, (int)na->value);
#ifdef ESP8266
        Serial.println(out);
#else
        printf("%s\n", out);
#endif
      } break;
      case VFLOAT: {
        struct vm_vfloat_t *na = (struct vm_vfloat_t *)&obj->varstack.buffer[val];
        sprintf((char *)&out, ".. %s %d %s = %g", __FUNCTION__, __LINE__, key, na->value);
#ifdef ESP8266
        Serial.println(out);
#else
        printf("%s\n", out);
#endif
      } break;
      case VCHAR: {
        struct vm_vchar_t *na = (struct vm_vchar_t *)&obj->varstack.buffer[val];
        sprintf((char *)&out, ".. %s %d %s = %s", __FUNCTION__, __LINE__, key, na->value);
#ifdef ESP8266
        Serial.println(out);
#else
        printf("%s\n", out);
#endif
      } break;
      case VNULL: {
        struct vm_vnull_t *na = (struct vm_vnull_t *)&obj->varstack.buffer[val];
        sprintf((char *)&out, ".. %s %d %s = NULL", __FUNCTION__, __LINE__, key);
#ifdef ESP8266
        Serial.println(out);
#else
        printf("%s\n", out);
#endif
      } break;
    }

    var = (struct vm_tvar_t *)&obj->ast.buffer[token];

    int move = 0;
    /*
     * Each value on the global stack is already associated to
     * a variable in one of the rules. If the variable associated
     * is the same as the variable called, we need to pop that
     * associated variable from the stack reinsert it again
     * and associate it again to the new rule.
     *
     * If we pop a variable from the stack we need to move the
     * remaining variables.
     *
     */
    for(x=4;alignedbytes(x)<varstack->nrbytes;x++) {
      x = alignedbytes(x);
      switch(varstack->buffer[x]) {
        case VINTEGER: {
          struct vm_gvinteger_t *val = (struct vm_gvinteger_t *)&varstack->buffer[x];
          /*
           * The associated var related to this value
           */
          struct vm_tvar_t *foo = (struct vm_tvar_t *)&rules[val->rule-1]->ast.buffer[val->ret];

          /*
           * Compare the token of the stack var to the input var.
           */
          if(strcmp((char *)foo->token, (char *)var->token) == 0) {
            move = 1;

            /*
             * Remove the old association from the stack
             * and decrease the size of the stack.
             */
            ret = alignedbytes(sizeof(struct vm_gvinteger_t));
            memmove(&varstack->buffer[x], &varstack->buffer[x+ret], varstack->nrbytes-x-ret);
            if((varstack->buffer = (unsigned char *)REALLOC(varstack->buffer, varstack->nrbytes-ret)) == NULL) {
              OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
            }
            varstack->nrbytes -= ret;
          }
        } break;
        case VFLOAT: {
          struct vm_gvfloat_t *val = (struct vm_gvfloat_t *)&varstack->buffer[x];
          struct vm_tvar_t *foo = (struct vm_tvar_t *)&rules[val->rule-1]->ast.buffer[val->ret];

          if(strcmp((char *)foo->token, (char *)var->token) == 0) {
            move = 1;

            ret = alignedbytes(sizeof(struct vm_gvfloat_t));
            memmove(&varstack->buffer[x], &varstack->buffer[x+ret], varstack->nrbytes-x-ret);
            if((varstack->buffer = (unsigned char *)REALLOC(varstack->buffer, varstack->nrbytes-ret)) == NULL) {
              OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
            }
            varstack->nrbytes -= ret;
          }
        } break;
        case VNULL: {
          struct vm_gvnull_t *val = (struct vm_gvnull_t *)&varstack->buffer[x];
          struct vm_tvar_t *foo = (struct vm_tvar_t *)&rules[val->rule-1]->ast.buffer[val->ret];

          if(strcmp((char *)foo->token, (char *)var->token) == 0) {
            move = 1;

            ret = alignedbytes(sizeof(struct vm_gvnull_t));
            memmove(&varstack->buffer[x], &varstack->buffer[x+ret], varstack->nrbytes-x-ret);
            if((varstack->buffer = (unsigned char *)REALLOC(varstack->buffer, varstack->nrbytes-ret)) == NULL) {
              OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
            }
            varstack->nrbytes -= ret;
          }
        } break;
        default: {
          printf("err: %s %d\n", __FUNCTION__, __LINE__);
          exit(-1);
        } break;
      }
      if(x == varstack->nrbytes) {
        break;
      }

      /*
       * Move the remaining vars on the stack
       */
      switch(varstack->buffer[x]) {
        case VINTEGER: {
          if(move == 1 && x < varstack->nrbytes) {
            struct vm_gvinteger_t *node = (struct vm_gvinteger_t *)&varstack->buffer[x];
            if(node->ret > 0) {
              struct vm_tvar_t *tmp = (struct vm_tvar_t *)&rules[node->rule-1]->ast.buffer[node->ret];
              tmp->value = x;
            }
          }
          x += sizeof(struct vm_gvinteger_t)-1;
        } break;
        case VFLOAT: {
          if(move == 1 && x < varstack->nrbytes) {
            struct vm_gvfloat_t *node = (struct vm_gvfloat_t *)&varstack->buffer[x];
            if(node->ret > 0) {
              struct vm_tvar_t *tmp = (struct vm_tvar_t *)&rules[node->rule-1]->ast.buffer[node->ret];
              tmp->value = x;
            }
          }
          x += sizeof(struct vm_gvfloat_t)-1;
        } break;
        case VNULL: {
          if(move == 1 && x < varstack->nrbytes) {
            struct vm_gvnull_t *node = (struct vm_gvnull_t *)&varstack->buffer[x];
            if(node->ret > 0) {
              struct vm_tvar_t *tmp = (struct vm_tvar_t *)&rules[node->rule-1]->ast.buffer[node->ret];
              tmp->value = x;
            }
          }
          x += sizeof(struct vm_gvnull_t)-1;
        } break;
      }
    }

    /*
     * Associate the newly inserted value to the new
     * variable instance.
     */

    ret = varstack->nrbytes;
    var->value = ret;

    /*
     * Check the value type from the local stack
     */
    switch(obj->varstack.buffer[val]) {
      case VINTEGER: {
        /*
         * Calculate and create new size for the stack
         */
        unsigned int size = alignedbytes(varstack->nrbytes + sizeof(struct vm_gvinteger_t));
        if((varstack->buffer = (unsigned char *)REALLOC(varstack->buffer, size)) == NULL) {
          OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
        }
        /*
         * Retrieve the value from the local stack
         */
        struct vm_vinteger_t *cpy = (struct vm_vinteger_t *)&obj->varstack.buffer[val];
        /*
         * Cast the new location on the global stack
         * and set the values of the new variable
         */
        struct vm_gvinteger_t *value = (struct vm_gvinteger_t *)&varstack->buffer[ret];
        value->type = VINTEGER;
        value->ret = token;
        value->value = (int)cpy->value;
        value->rule = obj->nr;

        sprintf((char *)&out, ".. %s %d %s = %d", __FUNCTION__, __LINE__, var->token, cpy->value);
#ifdef ESP8266
        Serial.println(out);
#else
        printf("%s\n", out);
#endif

        varstack->nrbytes = size;
      } break;
      case VFLOAT: {
        unsigned int size = alignedbytes(varstack->nrbytes + sizeof(struct vm_gvfloat_t));
        if((varstack->buffer = (unsigned char *)REALLOC(varstack->buffer, size)) == NULL) {
          OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
        }
        struct vm_vfloat_t *cpy = (struct vm_vfloat_t *)&obj->varstack.buffer[val];
        struct vm_gvfloat_t *value = (struct vm_gvfloat_t *)&varstack->buffer[ret];
        value->type = VFLOAT;
        value->ret = token;
        value->value = cpy->value;
        value->rule = obj->nr;

        sprintf((char *)&out, ".. %s %d %s = %g", __FUNCTION__, __LINE__, var->token, cpy->value);
#ifdef ESP8266
        Serial.println(out);
#else
        printf("%s\n", out);
#endif

        varstack->nrbytes = size;
      } break;
      case VNULL: {
        unsigned int size = alignedbytes(varstack->nrbytes + sizeof(struct vm_gvnull_t));
        if((varstack->buffer = (unsigned char *)REALLOC(varstack->buffer, size)) == NULL) {
          OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
        }
        struct vm_gvnull_t *value = (struct vm_gvnull_t *)&varstack->buffer[ret];
        value->type = VNULL;
        value->ret = token;
        value->rule = obj->nr;

        sprintf((char *)&out, ".. %s %d %s = NULL", __FUNCTION__, __LINE__, var->token);
#ifdef ESP8266
        Serial.println(out);
#else
        printf("%s\n", out);
#endif

        varstack->nrbytes = size;
      } break;
      default: {
        exit(-1);
      } break;
    }
  }
}

static unsigned char *vm_value_get(struct rules_t *obj, uint16_t token) {
  struct vm_tvar_t *node = (struct vm_tvar_t *)&obj->ast.buffer[token];
  int i = 0;

  if(node->token[0] == '#') {
    struct varstack_t *varstack = &global_varstack;

    /*
     * If there is no value associated to the
     * variable, the value is set to NULL.
     */
    if(node->value == 0) {
      int ret = varstack->nrbytes;

      /*
       * In order to set the value NULL to this variable
       * the NULL most be inserted on the stack.
       */
      unsigned int size = alignedbytes(varstack->nrbytes + sizeof(struct vm_gvnull_t));
      if((varstack->buffer = (unsigned char *)REALLOC(varstack->buffer, size)) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      struct vm_gvnull_t *value = (struct vm_gvnull_t *)&varstack->buffer[ret];
      value->type = VNULL;
      value->ret = token;
      value->rule = obj->nr;
      node->value = ret;

      varstack->nrbytes = size;
    }

    /*
     * When values are get from the stack it will be set on the
     * stack again for further usage. In this proces the original
     * pointer to the value changes. Therefor we must temporarily
     * cache the value. In this case, we a global version of each
     * value type is created that can be used for this cache.
     */
    const char *key = (char *)node->token;
    switch(varstack->buffer[node->value]) {
      case VINTEGER: {
        struct vm_gvinteger_t *na = (struct vm_gvinteger_t *)&varstack->buffer[node->value];

        memset(&vinteger, 0, sizeof(struct vm_vinteger_t));
        vinteger.type = VINTEGER;
        vinteger.value = (int)na->value;

        sprintf((char *)&out, ".. %s %d %s = %d", __FUNCTION__, node->value, key, (int)na->value);
#ifdef ESP8266
        Serial.println(out);
#else
        printf("%s\n", out);
#endif

        return (unsigned char *)&vinteger;
      } break;
      case VFLOAT: {
        struct vm_gvfloat_t *na = (struct vm_gvfloat_t *)&varstack->buffer[node->value];

        memset(&vfloat, 0, sizeof(struct vm_vfloat_t));
        vfloat.type = VFLOAT;
        vfloat.value = na->value;

        sprintf((char *)&out, ".. %s %d %s = %g", __FUNCTION__, node->value, key, na->value);
#ifdef ESP8266
        Serial.println(out);
#else
        printf("%s\n", out);
#endif

        return (unsigned char *)&vfloat;
      } break;
      case VNULL: {
        struct vm_gvnull_t *na = (struct vm_gvnull_t *)&varstack->buffer[node->value];

        memset(&vnull, 0, sizeof(struct vm_vnull_t));
        vnull.type = VNULL;

        sprintf((char *)&out, ".. %s %d %s = NULL", __FUNCTION__, node->value, key);
#ifdef ESP8266
        Serial.println(out);
#else
        printf("%s\n", out);
#endif

        return (unsigned char *)&vnull;
      } break;
      case VCHAR: {
        exit(-1);
      } break;
    }

    exit(-1);
  }

  return NULL;
}

static void vm_value_cpy(struct rules_t *obj, uint16_t token) {
  struct varstack_t *varstack = NULL;
  struct vm_tvar_t *var = (struct vm_tvar_t *)&obj->ast.buffer[token];
  int x = 0;

  if(var->token[0] == '#') {
    varstack = &global_varstack;

    for(x=4;alignedbytes(x)<varstack->nrbytes;x++) {
      x = alignedbytes(x);
      switch(varstack->buffer[x]) {
        case VINTEGER: {
          struct vm_gvinteger_t *val = (struct vm_gvinteger_t *)&varstack->buffer[x];
          struct vm_tvar_t *foo = (struct vm_tvar_t *)&rules[val->rule-1]->ast.buffer[val->ret];

          /*
           * Change the var association of this value
           * to the new variable, without removing the
           * value from the stack.
           */
          if(strcmp((char *)foo->token, (char *)var->token) == 0 && val->ret != token) {
            var->value = x;
            val->ret = token;
            val->rule = obj->nr;
            return;
          }
          x += sizeof(struct vm_gvinteger_t)-1;
        } break;
        case VFLOAT: {
          struct vm_gvfloat_t *val = (struct vm_gvfloat_t *)&varstack->buffer[x];
          struct vm_tvar_t *foo = (struct vm_tvar_t *)&rules[val->rule-1]->ast.buffer[val->ret];

          if(strcmp((char *)foo->token, (char *)var->token) == 0 && val->ret != token) {
            var->value = x;
            val->ret = token;
            val->rule = obj->nr;
            return;
          }
          x += sizeof(struct vm_gvfloat_t)-1;
        } break;
        case VNULL: {
          struct vm_gvnull_t *val = (struct vm_gvnull_t *)&varstack->buffer[x];
          struct vm_tvar_t *foo = (struct vm_tvar_t *)&rules[val->rule-1]->ast.buffer[val->ret];

          if(strcmp((char *)foo->token, (char *)var->token) == 0 && val->ret != token) {
            var->value = x;
            val->ret = token;
            val->rule = obj->nr;
            return;
          }
          x += sizeof(struct vm_gvnull_t)-1;
        } break;
        default: {
          exit(-1);
        } break;
      }
    }
  }
}

static void vm_global_value_prt(char *out, int size) {
  struct varstack_t *varstack = &global_varstack;
  int x = 0, pos = 0;

  for(x=4;alignedbytes(x)<varstack->nrbytes;x++) {
    x = alignedbytes(x);
    switch(varstack->buffer[x]) {
      case VINTEGER: {
        struct vm_gvinteger_t *val = (struct vm_gvinteger_t *)&varstack->buffer[x];
        switch(rules[val->rule-1]->ast.buffer[val->ret]) {
          case TVAR: {
            struct vm_tvar_t *node = (struct vm_tvar_t *)&rules[val->rule-1]->ast.buffer[val->ret];
            pos += snprintf(&out[pos], size - pos, "%d %s = %d\n", x, node->token, val->value);
          } break;
          default: {
            // printf("err: %s %d %d\n", __FUNCTION__, __LINE__, obj->ast.buffer[val->ret]);
            // exit(-1);
          } break;
        }
        x += sizeof(struct vm_gvinteger_t)-1;
      } break;
      case VFLOAT: {
        struct vm_gvfloat_t *val = (struct vm_gvfloat_t *)&varstack->buffer[x];
        switch(rules[val->rule-1]->ast.buffer[val->ret]) {
          case TVAR: {
            struct vm_tvar_t *node = (struct vm_tvar_t *)&rules[val->rule-1]->ast.buffer[val->ret];
            pos += snprintf(&out[pos], size - pos, "%d %s = %g\n", x, node->token, val->value);
          } break;
          default: {
            // printf("err: %s %d\n", __FUNCTION__, __LINE__);
            // exit(-1);
          } break;
        }
        x += sizeof(struct vm_gvfloat_t)-1;
      } break;
      case VNULL: {
        struct vm_gvnull_t *val = (struct vm_gvnull_t *)&varstack->buffer[x];
        switch(rules[val->rule-1]->ast.buffer[val->ret]) {
          case TVAR: {
            struct vm_tvar_t *node = (struct vm_tvar_t *)&rules[val->rule-1]->ast.buffer[val->ret];
            pos += snprintf(&out[pos], size - pos, "%d %s = NULL\n", x, node->token);
          } break;
          default: {
            // printf("err: %s %d\n", __FUNCTION__, __LINE__);
            // exit(-1);
          } break;
        }
        x += sizeof(struct vm_gvnull_t)-1;
      } break;
      default: {
        exit(-1);
      } break;
    }
  }
}

void run_test() {
  memset(&rule_options, 0, sizeof(struct rule_options_t));
  rule_options.is_token_cb = is_variable;
  rule_options.set_token_val_cb = vm_value_set;
  rule_options.get_token_val_cb = vm_value_get;
  rule_options.cpy_token_val_cb = vm_value_cpy;

  global_varstack.buffer = NULL;
  global_varstack.nrbytes = 4;
  global_varstack.bufsize = 4;

  int len = strlen(test), ret = 0;

  char *rule = STRDUP(test);
  if(rule == NULL) {
    OUT_OF_MEMORY
  }

  while((ret = rule_initialize(&rule, &rules, &nrrules, NULL)) == 0) {
#ifdef ESP8266
    Serial.println("\n>>> global variables");
#else
    printf("\n>>> global variables\n");
#endif
    memset(&out, 0, OUTPUT_SIZE);
    vm_global_value_prt((char *)&out, OUTPUT_SIZE);
#ifdef ESP8266
    Serial.println(out);
#else
    printf("%s\n",out);
#endif
  }

  rules_gc(&rules, nrrules);

  FREE(global_varstack.buffer);
  global_varstack.buffer = NULL;
  global_varstack.nrbytes = 4;
  global_varstack.bufsize = 4;
  FREE(rule);
}

#ifndef ESP8266
int main(int argc, char **argv) {
  run_test();
}
#endif
