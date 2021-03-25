/*
  Copyright (C) CurlyMo

  This Source Code Form is subject to the terms of the Mozilla Public
  License, v. 2.0. If a copy of the MPL was not distributed with this
  file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#ifdef ESP8266
  #pragma GCC diagnostic warning "-fpermissive"
#endif

#ifndef ESP8266
  #include <stdio.h>
  #include <stdlib.h>
  #include <stdarg.h>
  #include <unistd.h>
  #include <fcntl.h>
  #include <limits.h>
  #include <errno.h>
  #include <time.h>
  #include <sys/time.h>
  #include <math.h>
  #include <string.h>
  #include <ctype.h>
  #include <assert.h>
  #include <stdint.h>
#else
  #include <Arduino.h>
#endif

#include "mem.h"
#include "rules.h"
#include "operator.h"
#include "function.h"

#define max(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })

#define min(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a < _b ? _a : _b; })

#ifdef ESP8266
  char out[1024];
#endif

struct vm_cache_t {
  uint8_t type;
  uint16_t step;
  uint16_t start;
} __attribute__((packed)) **vmcache;
static unsigned int nrcache = 0;

static unsigned int nrbytes;
static unsigned char *bytecode = NULL;

/*LCOV_EXCL_START*/
#ifdef DEBUG
static void print_tree(struct rules_t *obj);
static void print_bytecode(struct rules_t *obj);
#endif
/*LCOV_EXCL_STOP*/

static int lexer_peek(struct rules_t *obj, int skip, int *type);

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

void rules_gc(struct rules_t ***rules, int nrrules) {
  int i = 0;
  for(i=0;i<nrrules;i++) {
    FREE((*rules)[i]->bytecode);
    FREE((*rules)[i]);
  }
  FREE((*rules));
  (*rules) = NULL;

#ifndef ESP8266
  /*
   * Should never happen
   */
  /* LCOV_EXCL_START*/
  for(i=0;i<nrcache;i++) {
    FREE(vmcache[i]);
    vmcache[i] = NULL;
  }
  /* LCOV_EXCL_STOP*/
  FREE(vmcache);
  vmcache = NULL;
  nrcache = 0;
#endif
}

static int alignedbytes(int v) {
#ifdef ESP8266
  while((v++ % 4) != 0);
  return --v;
#else
  return v;
#endif
}

static int is_function(const char *text, int *pos, int size) {
  int i = 0, len = 0;
  for(i=0;i<nr_event_functions;i++) {
    len = strlen(event_functions[i].name);
    if(size == len && strnicmp(&text[*pos], event_functions[i].name, len) == 0) {
      return i;
    }
  }

  return -1;
}

static int is_operator(const char *text, int *pos, int size) {
  int i = 0, len = 0;
  for(i=0;i<nr_event_operators;i++) {
    len = strlen(event_operators[i].name);
    if(size == len && strnicmp(&text[*pos], event_operators[i].name, len) == 0) {
      return i;
    }
  }

  return -1;
}

static int lexer_parse_number(const char *text, int *pos) {

  int i = 0, nrdot = 0, len = strlen(text), start = *pos;

  if(isdigit(text[*pos]) || text[*pos] == '-') {
    /*
     * The dot cannot be the first character
     * and we cannot have more than 1 dot
     */
    while(*pos <= len &&
        (
          isdigit(text[*pos]) ||
          (i == 0 && text[*pos] == '-') ||
          (i > 0 && nrdot == 0 && text[*pos] == '.')
        )
      ) {
      if(text[*pos] == '.') {
        nrdot++;
      }
      (*pos)++;
      i++;
    }

    if((bytecode = (unsigned char *)REALLOC(bytecode, nrbytes+(*pos - start)+1)) == NULL) {
      OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
    }
    memcpy(&bytecode[nrbytes], &text[start], *pos - start);
    nrbytes += (*pos - start);
    bytecode[nrbytes++] = 0;
    return 0;
  } else {
    return -1;
  }
}

static int lexer_parse_string(const char *text, int *pos) {
  int len = strlen(text), start = *pos;

  while(*pos <= len &&
        text[*pos] != ' ' &&
        text[*pos] != ',' &&
        text[*pos] != ';' &&
        text[*pos] != '(' &&
        text[*pos] != ')') {
    (*pos)++;
  }
  if((bytecode = (unsigned char *)REALLOC(bytecode, nrbytes+(*pos - start)+1)) == NULL) {
    OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
  }
  memcpy(&bytecode[nrbytes], &text[start], *pos - start);
  nrbytes += (*pos - start);
  bytecode[nrbytes++] = 0;
  return 0;
}

// static int lexer_parse_quoted_string(struct rules_t *obj, struct lexer_t *lexer, int *start) {

  // int x = lexer->current_char[0];
  // if(lexer->current_char[0] == '\'' || lexer->current_char[0] == '"') {
    // lexer->text[lexer->pos] = '\0';
  // }
  // *start = ++lexer->pos;
  // if(lexer->pos < lexer->len) {

    // while(lexer->pos < lexer->len) {
      // if(x != '\'' && lexer->pos > 0 && lexer->text[lexer->pos] == '\'') {
        // /*
         // * Skip escape
         // */
        // lexer->current_char = &lexer->text[lexer->(*pos)++];
      // } else if(x == '\'' && lexer->pos > 0 && lexer->text[lexer->pos] == '\\' && lexer->text[lexer->pos+1] == '\'') {
        // /*
         // * Remove escape
         // */
        // memmove(&lexer->text[lexer->pos], &lexer->text[lexer->pos+1], lexer->len-lexer->pos-1);
        // lexer->text[lexer->len-1] = '\0';

        // lexer->current_char = &lexer->text[lexer->(*pos)++];
        // lexer->len--;
      // } else if(x == '"' && lexer->pos > 0 && lexer->text[lexer->pos] == '\\' && lexer->text[lexer->pos+1] == '"') {
        // /*
         // * Remove escape
         // */
        // memmove(&lexer->text[lexer->pos], &lexer->text[lexer->pos+1], lexer->len-lexer->pos-1);
        // lexer->text[lexer->len-1] = '\0';

        // lexer->current_char = &lexer->text[lexer->(*pos)++];
        // lexer->len--;
      // } else if(x != '"' && lexer->pos > 0 && lexer->text[lexer->pos] == '"') {
        // /*
         // * Skip escape
         // */
        // lexer->current_char = &lexer->text[lexer->(*pos)++];
      // } else if(lexer->text[lexer->pos] == x) {
        // break;
      // } else {
        // lexer->(*pos)++;
      // }
    // }

    // if(lexer->current_char[0] != '"' && lexer->current_char[0] != '\'') {
      // printf("err: %s %d\n", __FUNCTION__, __LINE__);
      // exit(-1);
    // } else if(lexer->pos <= lexer->len) {
      // lexer_isolate_token(obj, lexer, x, -1);
      // return 0;
    // } else {
      // return -1;
    // }
  // }
  // return -1;
// }

static int lexer_parse_skip_characters(const char *text, int len, int *pos) {
  while(*pos <= len &&
      (text[*pos] == ' ' ||
      text[*pos] == '\n' ||
      text[*pos] == '\t' ||
      text[*pos] == '\r')) {
    (*pos)++;
  }
  return 0;
}

static int rule_prepare(const char *text, int *pos) {
  int ret = 0, len = strlen(text);
  int nrblocks = 0;
  while(*pos < len) {
    lexer_parse_skip_characters(text, len, pos);

    // if(lexer->current_char[0] == '\'' || lexer->current_char[0] == '"') {
      // ret = lexer_parse_quoted_string(obj, lexer, &tpos);
      // type = TSTRING;
      // lexer->(*pos)++;
      // if(lexer->text[lexer->pos] == ' ') {
        // lexer->text[lexer->pos] = '\0';
      // } else {
        // printf("err: %s %d\n", __FUNCTION__, __LINE__);
        // exit(-1);
      // }
    // } else
    if(isdigit(text[*pos]) || (text[*pos] == '-' && *pos < len && isdigit(text[(*pos)+1]))) {
      if((bytecode = (unsigned char *)REALLOC(bytecode, nrbytes+1)) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      bytecode[nrbytes++] = TNUMBER;
      ret = lexer_parse_number(text, pos);
    } else if(strnicmp((char *)&text[*pos], "if", 2) == 0) {
      if((bytecode = (unsigned char *)REALLOC(bytecode, nrbytes+1)) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      bytecode[nrbytes++] = TIF;
      (*pos)+=2;
      nrblocks++;
    } else if(strnicmp(&text[*pos], "on", 2) == 0) {
      if((bytecode = (unsigned char *)REALLOC(bytecode, nrbytes+1)) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      bytecode[nrbytes++] = TEVENT;
      *pos+=2;
      lexer_parse_skip_characters(text, len, pos);
      ret = lexer_parse_string(text, pos);
      nrblocks++;
    } else if(strnicmp((char *)&text[*pos], "else", 4) == 0) {
      if((bytecode = (unsigned char *)REALLOC(bytecode, nrbytes+1)) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      bytecode[nrbytes++] = TELSE;
      (*pos)+=4;
    } else if(strnicmp((char *)&text[*pos], "then", 4) == 0) {
      if((bytecode = (unsigned char *)REALLOC(bytecode, nrbytes+1)) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      bytecode[nrbytes++] = TTHEN;
      (*pos)+=4;
    } else if(strnicmp((char *)&text[*pos], "end", 3) == 0) {
      if((bytecode = (unsigned char *)REALLOC(bytecode, nrbytes+1)) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      bytecode[nrbytes++] = TEND;
      (*pos)+=3;
      nrblocks--;
    } else if(strnicmp((char *)&text[*pos], "NULL", 4) == 0) {
      if((bytecode = (unsigned char *)REALLOC(bytecode, nrbytes+1)) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      bytecode[nrbytes++] = VNULL;
      (*pos)+=4;
    } else if(text[*pos] == ',') {
      if((bytecode = (unsigned char *)REALLOC(bytecode, nrbytes+1)) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      bytecode[nrbytes++] = TCOMMA;
      (*pos)++;
    } else if(text[*pos] == '(') {
      if((bytecode = (unsigned char *)REALLOC(bytecode, nrbytes+1)) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      bytecode[nrbytes++] = LPAREN;
      (*pos)++;
    } else if(text[*pos] == ')') {
      if((bytecode = (unsigned char *)REALLOC(bytecode, nrbytes+1)) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      bytecode[nrbytes++] = RPAREN;
      (*pos)++;
    } else if(text[*pos] == '=' && *pos < len && text[(*pos)+1] != '=') {
      if((bytecode = (unsigned char *)REALLOC(bytecode, nrbytes+1)) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      bytecode[nrbytes++] = TASSIGN;
      (*pos)++;
    } else if(text[*pos] == ';') {
      if((bytecode = (unsigned char *)REALLOC(bytecode, nrbytes+1)) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      bytecode[nrbytes++] = TSEMICOLON;
      (*pos)++;
    } else {
      int a = 0;
      int b = len-(*pos)-1, len1 = 0;
      for(a=(*pos);a<len;a++) {
        if(text[a] == ' ' || text[a] == '(' || text[a] == ',') {
          b = a-(*pos);
          break;
        }
      }

      if((len1 = is_function(text, pos, b)) > -1) {
        if((bytecode = (unsigned char *)REALLOC(bytecode, nrbytes+2)) == NULL) {
          OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
        }
        bytecode[nrbytes++] = TFUNCTION;
        bytecode[nrbytes++] = len1;
        *pos += b;
      } else if((len1 = is_operator(text, pos, b)) > -1) {
        if((bytecode = (unsigned char *)REALLOC(bytecode, nrbytes+2)) == NULL) {
          OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
        }
        bytecode[nrbytes++] = TOPERATOR;
        bytecode[nrbytes++] = len1;
        *pos += b;
      } else if(rule_options.is_token_cb != NULL && (len1 = rule_options.is_token_cb(text, pos, b)) > -1) {
        if((bytecode = (unsigned char *)REALLOC(bytecode, nrbytes+len1+2)) == NULL) {
          OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
        }
        bytecode[nrbytes++] = TVAR;
        memcpy(&bytecode[nrbytes], &text[*pos], len1);
        nrbytes += len1;
        bytecode[nrbytes++] = 0;
        *pos += len1;
      } else if(rule_options.is_event_cb != NULL && (len1 = rule_options.is_event_cb(text, pos, b)) > -1) {
        if((*pos)+b < len && text[(*pos)+b] == '(' && (*pos)+b+1 < len && text[(*pos)+b+1] == ')') {
          if((bytecode = (unsigned char *)REALLOC(bytecode, nrbytes+2)) == NULL) {
            OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
          }
          bytecode[nrbytes++] = TCEVENT;
          lexer_parse_skip_characters(text, len, pos);
          ret = lexer_parse_string(text, pos);
          *pos += 2;
        } else {
          if((len - *pos) > 5) {
            fprintf(stderr, "ERROR: unknown token '%.5s...'\n", &text[*pos]);
          } else {
            fprintf(stderr, "ERROR: unknown token '%.5s'\n", &text[*pos]);
          }
          return -1;
        }
      } else {
        if((len - *pos) > 5) {
          fprintf(stderr, "ERROR: unknown token '%.5s...'\n", &text[*pos]);
        } else {
          fprintf(stderr, "ERROR: unknown token '%.5s'\n", &text[*pos]);
        }
        return -1;
      }
    }

    lexer_parse_skip_characters(text, len, pos);

    if(nrblocks == 0) {
      break;
    }
  }

  if((bytecode = (unsigned char *)REALLOC(bytecode, nrbytes+2)) == NULL) {
    OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
  }
  bytecode[nrbytes++] = 0;
  bytecode[nrbytes++] = 0;

  return ret;
}

static int lexer_bytecode_pos(int skip, int *pos) {
  int nr = 0, i = 0;
  for(i=0;i<nrbytes;i++) {
    if(bytecode[i] == TIF || bytecode[i] == TTHEN ||
       bytecode[i] == TEND || bytecode[i] == TASSIGN ||
       bytecode[i] == LPAREN || bytecode[i] == RPAREN ||
       bytecode[i] == TELSE || bytecode[i] == TSEMICOLON ||
       bytecode[i] == TCOMMA || bytecode[i] == VNULL ||
       bytecode[i] == TEND) {
      *pos = i;
      nr++;
    } else if(bytecode[i] == TFUNCTION) {
      *pos = i;
      i++;
      nr++;
    } else if(bytecode[i] == TOPERATOR) {
      *pos = i;
      i++;
      nr++;
    } else if(bytecode[i] == TVAR ||
              bytecode[i] == TSTRING ||
              bytecode[i] == TNUMBER ||
              bytecode[i] == TCEVENT ||
              bytecode[i] == TEVENT) {
      *pos = i;
      while(bytecode[++i] != 0);
      nr++;
    }

    if(skip == nr-1) {
      return 0;
    }
  }
  /*LCOV_EXCL_START*/
  if(skip > nr) {
    fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
    return -1;
  }
  if(skip == nr) {
    return 0;
  }

  fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
  return -1;
  /*LCOV_EXCL_STOP*/
}

static int vm_parent(struct rules_t *obj, int type, int val) {
  int ret = alignedbytes(obj->nrbytes), size = 0, pos = -1, i = 0;

  switch(type) {
    case TSTART: {
      size = alignedbytes(ret+sizeof(struct vm_tstart_t));
      if((obj->bytecode = (unsigned char *)REALLOC(obj->bytecode, size)) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      memset(&obj->bytecode[ret], 0, sizeof(struct vm_tstart_t));

      struct vm_tstart_t *node = (struct vm_tstart_t *)&obj->bytecode[ret];
      node->type = type;
      node->go = 0;
      node->ret = 0;

      obj->nrbytes = size;
    } break;
    case TEOF: {
      size = alignedbytes(ret+sizeof(struct vm_teof_t));
      if((obj->bytecode = (unsigned char *)REALLOC(obj->bytecode, size)) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      memset(&obj->bytecode[ret], 0, sizeof(struct vm_teof_t));

      struct vm_teof_t *node = (struct vm_teof_t *)&obj->bytecode[ret];
      node->type = type;

      obj->nrbytes = size;
    } break;
    case TIF: {
      size = alignedbytes(ret+sizeof(struct vm_tif_t));
      if((obj->bytecode = (unsigned char *)REALLOC(obj->bytecode, size)) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      memset(&obj->bytecode[ret], 0, sizeof(struct vm_tif_t));

      struct vm_tif_t *node = (struct vm_tif_t *)&obj->bytecode[ret];
      node->type = type;
      node->ret = 0;
      node->go = 0;
      node->true_ = 0;
      node->false_ = 0;

      obj->nrbytes = size;
    } break;
    case LPAREN: {
      size = alignedbytes(ret+sizeof(struct vm_lparen_t));
      if((obj->bytecode = (unsigned char *)REALLOC(obj->bytecode, size)) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      memset(&obj->bytecode[ret], 0, sizeof(struct vm_lparen_t));

      struct vm_lparen_t *node = (struct vm_lparen_t *)&obj->bytecode[ret];
      node->type = type;
      node->ret = 0;
      node->go = 0;
      node->value = 0;

      obj->nrbytes = size;
    } break;
    case TNUMBER: {
      lexer_bytecode_pos(val, &pos);

      int l = strlen((char *)&bytecode[pos+1]);
      float var = atof((char *)&bytecode[pos+1]);
      float nr = 0;

      if(modff(var, &nr) == 0) {
        /*
         * This range of integers
         * take less bytes when stored
         * as ascii characters.
         */
        if(var < 100 && var > -9) {
          size = alignedbytes(ret+sizeof(struct vm_tnumber_t)+l+1);
          if((obj->bytecode = (unsigned char *)REALLOC(obj->bytecode, size)) == NULL) {
            OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
          }
          memset(&obj->bytecode[ret], 0, sizeof(struct vm_tnumber_t)+l+1);

          struct vm_tnumber_t *node = (struct vm_tnumber_t *)&obj->bytecode[ret];
          node->type = type;
          node->ret = 0;
          memcpy(node->token, &bytecode[pos+1], l+1);

          obj->nrbytes = size;
        } else {
          if((obj->bytecode = (unsigned char *)REALLOC(obj->bytecode, alignedbytes(obj->nrbytes)+sizeof(struct vm_vinteger_t))) == NULL) {
            OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
          }

          struct vm_vinteger_t *value = (struct vm_vinteger_t *)&obj->bytecode[obj->nrbytes];
          value->type = VINTEGER;
          value->ret = ret;
          value->value = (int)var;
          obj->nrbytes = alignedbytes(obj->nrbytes) + sizeof(struct vm_vinteger_t);
        }
      } else {
        if((obj->bytecode = (unsigned char *)REALLOC(obj->bytecode, alignedbytes(obj->nrbytes)+sizeof(struct vm_vfloat_t))) == NULL) {
          OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
        }

        struct vm_vfloat_t *value = (struct vm_vfloat_t *)&obj->bytecode[obj->nrbytes];
        value->type = VFLOAT;
        value->ret = ret;
        value->value = var;
        obj->nrbytes = alignedbytes(obj->nrbytes) + sizeof(struct vm_vfloat_t);
      }
    } break;
    case TFALSE:
    case TTRUE: {
      size = alignedbytes(ret+sizeof(struct vm_ttrue_t)+(sizeof(uint16_t)*val));
      if((obj->bytecode = (unsigned char *)REALLOC(obj->bytecode, size)) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      memset(&obj->bytecode[ret], 0, sizeof(struct vm_ttrue_t)+(sizeof(uint16_t)*val));

      struct vm_ttrue_t *node = (struct vm_ttrue_t *)&obj->bytecode[ret];
      node->type = type;
      node->ret = 0;
      node->nrgo = val;
      for(i=0;i<val;i++) {
        node->go[i] = 0;
      }

      obj->nrbytes = size;
    } break;
    case TFUNCTION: {
      size = alignedbytes(ret+sizeof(struct vm_tfunction_t)+(sizeof(uint16_t)*val));
      if((obj->bytecode = (unsigned char *)REALLOC(obj->bytecode, size)) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      memset(&obj->bytecode[ret], 0, sizeof(struct vm_tfunction_t)+(sizeof(uint16_t)*val));

      struct vm_tfunction_t *node = (struct vm_tfunction_t *)&obj->bytecode[ret];
      node->token = 0;
      node->type = type;
      node->ret = 0;
      node->nrgo = val;
      for(i=0;i<val;i++) {
        node->go[i] = 0;
      }
      node->value = 0;

      obj->nrbytes = size;
    } break;
    case TCEVENT: {
      lexer_bytecode_pos(val, &pos);

      int l = strlen((char *)&bytecode[pos+1]);

      size = alignedbytes(ret+sizeof(struct vm_tcevent_t)+(sizeof(uint16_t)*val)+l+1);
      if((obj->bytecode = (unsigned char *)REALLOC(obj->bytecode, size)) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      memset(&obj->bytecode[ret], 0, sizeof(struct vm_tcevent_t)+(sizeof(uint16_t)*val)+l+1);

      struct vm_tcevent_t *node = (struct vm_tcevent_t *)&obj->bytecode[ret];

      node->type = type;
      node->ret = 0;
      memcpy(node->token, &bytecode[pos+1], l+1);

      obj->nrbytes = size;
    } break;
    case VNULL: {
      size = alignedbytes(ret+sizeof(struct vm_vnull_t));
      if((obj->bytecode = (unsigned char *)REALLOC(obj->bytecode, size)) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      memset(&obj->bytecode[ret], 0, sizeof(struct vm_vnull_t));

      struct vm_vnull_t *node = (struct vm_vnull_t *)&obj->bytecode[ret];
      node->type = type;
      node->ret = 0;

      obj->nrbytes = size;
    } break;
    case TVAR: {
      lexer_bytecode_pos(val, &pos);

      int l = strlen((char *)&bytecode[pos+1]);

      size = alignedbytes(ret+sizeof(struct vm_tvar_t)+l+1);
      if((obj->bytecode = (unsigned char *)REALLOC(obj->bytecode, size)) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      memset(&obj->bytecode[ret], 0, sizeof(struct vm_tvar_t)+l+1);

      struct vm_tvar_t *node = (struct vm_tvar_t *)&obj->bytecode[ret];
      node->type = type;
      node->ret = 0;
      node->go = 0;
      node->value = 0;

      memcpy(node->token, &bytecode[pos+1], l+1);
      obj->nrbytes = size;
    } break;
    case TEVENT: {
      lexer_bytecode_pos(val, &pos);

      int l = strlen((char *)&bytecode[pos+1]);

      size = alignedbytes(ret+sizeof(struct vm_tevent_t)+l+1);
      if((obj->bytecode = (unsigned char *)REALLOC(obj->bytecode, size)) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      memset(&obj->bytecode[ret], 0, sizeof(struct vm_tevent_t)+l+1);

      struct vm_tevent_t *node = (struct vm_tevent_t *)&obj->bytecode[ret];
      node->type = type;
      node->ret = 0;
      node->go = 0;

      memcpy(node->token, &bytecode[pos+1], l+1);
      obj->nrbytes = size;
    } break;
    case TOPERATOR: {
      size = alignedbytes(ret+sizeof(struct vm_toperator_t));
      if((obj->bytecode = (unsigned char *)REALLOC(obj->bytecode, size)) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      memset(&obj->bytecode[ret], 0, sizeof(struct vm_toperator_t));

      lexer_bytecode_pos(val, &pos);

      struct vm_toperator_t *node = (struct vm_toperator_t *)&obj->bytecode[ret];

      node->type = type;
      node->ret = 0;
      node->token = bytecode[pos+1];
      node->left = 0;
      node->right = 0;
      node->value = 0;

      obj->nrbytes = size;
    } break;
  }
  return ret;
}

static int lexer_peek(int skip, int *type) {
  int i = 0, nr = 0, tmp = 0, val = 0;

  for(i=0;i<nrbytes;i++) {
    if(bytecode[i] == TIF || bytecode[i] == TTHEN ||
       bytecode[i] == TEND || bytecode[i] == TASSIGN ||
       bytecode[i] == LPAREN || bytecode[i] == RPAREN ||
       bytecode[i] == TELSE || bytecode[i] == TSEMICOLON ||
       bytecode[i] == TCOMMA || bytecode[i] == VNULL ||
       bytecode[i] == TEND) {
      tmp = bytecode[i];
    } else if(bytecode[i] == TOPERATOR) {
      tmp = bytecode[i];
      i++;
    } else if(bytecode[i] == TFUNCTION) {
      tmp = bytecode[i];
      i++;
    } else if(bytecode[i] == TVAR ||
              bytecode[i] == TNUMBER ||
              bytecode[i] == TSTRING ||
              bytecode[i] == TCEVENT ||
              bytecode[i] == TEVENT) {
      tmp = bytecode[i];
      while(bytecode[i] != 0) {
        i++;
      }
    } else {
      tmp = TEOF;
    }
    if(skip == nr++) {
      *type = tmp;
      break;
    }
  }

  if(skip > nr) {
    return -1;
  }
  /*LCOV_EXCL_START*/
  if(skip == nr) {
    *type = TEOF;
    fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
    return 0;
  }
  /*LCOV_EXCL_STOP*/


  *type = tmp;
  return 0;
}

static int lexer_eat(int offset, int skip) {
  int i = 0, nr = 0, tmp = 0;
  int val = 0, val1 = 0;

  lexer_bytecode_pos(offset, &val1);
  lexer_bytecode_pos(skip + 1, &val);

  memmove(&bytecode[val1], &bytecode[val1+(val-val1)], nrbytes-(val1+(val-val1)));
  nrbytes -= (val-val1);
  bytecode[nrbytes] = 0;

  for(i=0;i<nrcache;i++) {
    if(vmcache[i]->start >= offset) {
      vmcache[i]->start -= ((skip-offset)+1);
    }
  }

  return 0;
}

static int vm_rewind2(struct rules_t *obj, int step, int type, int type2) {
  int tmp = step;
  while(1) {
    if((obj->bytecode[tmp]) == type || (type2 > -1 && (obj->bytecode[tmp]) == type2)) {
      return tmp;
    } else {
      switch(obj->bytecode[tmp]) {
        case TSTART: {
          return 0;
        } break;
        case TIF: {
          struct vm_tif_t *node = (struct vm_tif_t *)&obj->bytecode[tmp];
          tmp = node->ret;
        } break;
        case TEVENT: {
          struct vm_tevent_t *node = (struct vm_tevent_t *)&obj->bytecode[tmp];
          tmp = node->ret;
        } break;
        /*
         * These are never seen and therefor
         * interfering with code coverage.
         */
        /*
        case TCEVENT: {
          struct vm_tcevent_t *node = (struct vm_tcevent_t *)&obj->bytecode[tmp];
          tmp = node->ret;
        } break;
        case TNUMBER: {
          struct vm_tnumber_t *node = (struct vm_tnumber_t *)&obj->bytecode[tmp];
          tmp = node->ret;
        } break;
        case VNULL: {
          struct vm_vnull_t *node = (struct vm_vnull_t *)&obj->bytecode[tmp];
          tmp = node->ret;
        } break;
        */
        case LPAREN: {
          struct vm_lparen_t *node = (struct vm_lparen_t *)&obj->bytecode[tmp];
          tmp = node->ret;
        } break;
        case TFALSE:
        case TTRUE: {
          struct vm_ttrue_t *node = (struct vm_ttrue_t *)&obj->bytecode[tmp];
          tmp = node->ret;
        } break;
        case TVAR: {
          struct vm_tvar_t *node = (struct vm_tvar_t *)&obj->bytecode[tmp];
          tmp = node->ret;
        } break;
        case TOPERATOR: {
          struct vm_toperator_t *node = (struct vm_toperator_t *)&obj->bytecode[tmp];
          tmp = node->ret;
        } break;
        /* LCOV_EXCL_START*/
        default: {
          fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
          return -1;
        } break;
        /* LCOV_EXCL_STOP*/
      }
    }
  }
  return 0;
}

static int vm_rewind(struct rules_t *obj, int step, int type) {
  return vm_rewind2(obj, step, type, -1);
}

static void vm_cache_add(int type, int step, int start) {
  if((vmcache = (struct vm_cache_t **)REALLOC(vmcache, sizeof(struct vm_cache_t *)*((nrcache)+1))) == NULL) {
    OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
  }
  if((vmcache[nrcache] = (struct vm_cache_t *)MALLOC(sizeof(struct vm_cache_t))) == NULL) {
    OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
  }
  vmcache[nrcache]->type = type;
  vmcache[nrcache]->step = step;
  vmcache[nrcache]->start = start;

  nrcache++;
#ifdef DEBUG
  printf("cache entries: %d\n", nrcache); /*LCOV_EXCL_LINE*/
#endif
}

static void vm_cache_del(int start) {
  int x = 0, y = 0;
  for(x=0;x<nrcache;x++) {
    if(vmcache[x]->start == start) {
      /*LCOV_EXCL_START*/
      /*
       * The cache should be popped in a lifo manner.
       * Therefor, moving element after the current
       * one popped should never occur.
       */
      for(y=x;y<nrcache-1;y++) {
        memcpy(vmcache[y], vmcache[y+1], sizeof(struct vm_cache_t));
      }
      /*LCOV_EXCL_STOP*/
      FREE(vmcache[nrcache-1]);
      nrcache--;
      break;
    }
  }
  if(nrcache == 0) {
    FREE(vmcache);
    vmcache = NULL;
  } else {
    if((vmcache = (struct vm_cache_t **)REALLOC(vmcache, sizeof(struct vm_cache_t *)*nrcache)) == NULL) {
      OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
    }
  }
#ifdef DEBUG
  printf("cache entries: %d\n", nrcache); /*LCOV_EXCL_LINE*/
#endif
}

static struct vm_cache_t *vm_cache_get(int type, int step, int start) {
  int x = 0;
  for(x=0;x<nrcache;x++) {
    if(
      (
        (type > -1 && vmcache[x]->type == type) || (type == -1)
      ) && (
        (start > -1 && vmcache[x]->start == start) || (start == -1)
      ) && (
        (step > -1 && vmcache[x]->step == step) || (step == -1)
      )
    ) {
      return vmcache[x];
    }
  }
  return NULL;
}

static int lexer_parse_math_order(struct rules_t *obj, int type, int *pos, int *step_out, int offset, int source) {
  int b = 0, c = 0, step = 0, val = 0, first = 1;

  while(1) {
    int right = 0;
    lexer_peek((*pos), &b);

    if(lexer_peek((*pos), &b) < 0 || b != TOPERATOR) {
      break;
    }

    step = vm_parent(obj, b, (*pos));
    lexer_eat(offset, *pos);

    if(lexer_peek((*pos), &c) == 0) {
      switch(c) {
        case LPAREN: {
          int oldpos = (*pos);
          struct vm_cache_t *x = vm_cache_get(LPAREN, -1, (*pos));
          /* LCOV_EXCL_START*/
          if(x == NULL) {
            fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
            return -1;
          }
          /* LCOV_EXCL_STOP*/
          (*pos) = x->start + 1;
          right = x->step;
          vm_cache_del(oldpos);
          lexer_eat(offset, (*pos));
          (*pos) -= 1;
        } break;
        case TFUNCTION: {
          int oldpos = (*pos);
          struct vm_cache_t *x = vm_cache_get(TFUNCTION, -1, (*pos));
          /* LCOV_EXCL_START*/
          if(x == NULL) {
            fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
            return -1;
          }
          /* LCOV_EXCL_STOP*/
          (*pos) = x->start;
          right = x->step;
          vm_cache_del(oldpos);
          lexer_eat(offset, (*pos));
        } break;
        case TVAR: {
          right = vm_parent(obj, c, (*pos));
          lexer_eat(offset, (*pos));
        } break;
        case TNUMBER: {
          right = vm_parent(obj, c, (*pos));
          lexer_eat(offset, (*pos));
        } break;
        case VNULL: {
          right = vm_parent(obj, c, (*pos));
          lexer_eat(offset, (*pos));
        } break;
        default: {
          fprintf(stderr, "ERROR: Expected a parenthesis block, function, number or variable\n");
          return -1;
        } break;
      }
    }

    struct vm_tgeneric_t *node = (struct vm_tgeneric_t *)&obj->bytecode[right];
    node->ret = step;

    struct vm_toperator_t *op2 = (struct vm_toperator_t *)&obj->bytecode[step];
    op2->left = *step_out;
    op2->right = right;

    switch(obj->bytecode[*step_out]) {
      case TNUMBER: {
        struct vm_tnumber_t *node = (struct vm_tnumber_t *)&obj->bytecode[*step_out];
        node->ret = step;
      } break;
      case VINTEGER: {
        struct vm_vinteger_t *node = (struct vm_vinteger_t *)&obj->bytecode[*step_out];
        node->ret = step;
      } break;
      case VFLOAT: {
        struct vm_vfloat_t *node = (struct vm_vfloat_t *)&obj->bytecode[*step_out];
        node->ret = step;
      } break;
      case LPAREN: {
        struct vm_lparen_t *node = (struct vm_lparen_t *)&obj->bytecode[*step_out];
        node->ret = step;
      } break;
      case TOPERATOR: {
        struct vm_toperator_t *node = (struct vm_toperator_t *)&obj->bytecode[*step_out];
        node->ret = step;
      } break;
      case TVAR: {
        struct vm_tvar_t *node = (struct vm_tvar_t *)&obj->bytecode[*step_out];
        node->ret = step;
      } break;
      case TFUNCTION: {
        struct vm_tfunction_t *node = (struct vm_tfunction_t *)&obj->bytecode[*step_out];
        node->ret = step;
      } break;
      case VNULL: {
        struct vm_vnull_t *node = (struct vm_vnull_t *)&obj->bytecode[*step_out];
        node->ret = step;
      } break;
      /* LCOV_EXCL_START*/
      default: {
        fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
        return -1;
      } break;
      /* LCOV_EXCL_STOP*/
    }

    struct vm_toperator_t *op1 = NULL;

    if(type == LPAREN) {
      if(first == 1/* && *step_out > obj->pos.parsed*/) {
        struct vm_tgeneric_t *node = (struct vm_tgeneric_t *)&obj->bytecode[*step_out];
        node->ret = step;
      }
    } else if(type == TOPERATOR) {
      if(first == 1) {
        struct vm_tgeneric_t *node = (struct vm_tgeneric_t *)&obj->bytecode[step];
        node->ret = source;

        op2->ret = step;
        if((obj->bytecode[source]) == TIF) {
          struct vm_tif_t *node = (struct vm_tif_t *)&obj->bytecode[source];
          node->go = step;
        }
      }
    }

    if((obj->bytecode[step]) == TOPERATOR && (obj->bytecode[*step_out]) == TOPERATOR) {
      struct vm_toperator_t *op3 = NULL;
      op1 = (struct vm_toperator_t *)&obj->bytecode[*step_out];

      int idx1 = op1->token;
      int idx2 = op2->token;

      int x = event_operators[idx1].precedence;
      int y = event_operators[idx2].precedence;
      int a = event_operators[idx2].associativity;

      if(y > x || (x == y && a == 2)) {
        if(a == 1) {
          op2->left = op1->right;
          op3 = (struct vm_toperator_t *)&obj->bytecode[op2->left];
          op3->ret = step;
          op1->right = step;
          op2->ret = *step_out;
        } else {
          /*
           * Find the last operator with an operator
           * as the last factor.
           */
          int tmp = op1->right;
          if((obj->bytecode[tmp]) != LPAREN &&
             (obj->bytecode[tmp]) != TFUNCTION &&
             (obj->bytecode[tmp]) != TNUMBER &&
             (obj->bytecode[tmp]) != VFLOAT &&
             (obj->bytecode[tmp]) != VINTEGER &&
             (obj->bytecode[tmp]) != VNULL) {
            while((obj->bytecode[tmp]) == TOPERATOR) {
              if((obj->bytecode[((struct vm_toperator_t *)&obj->bytecode[tmp])->right]) == TOPERATOR) {
                tmp = ((struct vm_toperator_t *)&obj->bytecode[tmp])->right;
              } else {
                break;
              }
            }
          } else {
            tmp = *step_out;
          }

          op3 = (struct vm_toperator_t *)&obj->bytecode[tmp];
          int tright = op3->right;
          op3->right = step;

          switch((obj->bytecode[tright])) {
            case TNUMBER: {
              struct vm_tnumber_t *node = (struct vm_tnumber_t *)&obj->bytecode[*step_out];
              node->ret = step;
            } break;
            case VINTEGER: {
              struct vm_vinteger_t *node = (struct vm_vinteger_t *)&obj->bytecode[*step_out];
              node->ret = step;
            } break;
            case VFLOAT: {
              struct vm_vfloat_t *node = (struct vm_vfloat_t *)&obj->bytecode[*step_out];
              node->ret = step;
            } break;
            case TFUNCTION: {
              struct vm_tfunction_t *node = (struct vm_tfunction_t *)&obj->bytecode[tright];
              node->ret = step;
            } break;
            case LPAREN: {
              struct vm_lparen_t *node = (struct vm_lparen_t *)&obj->bytecode[tright];
              node->ret = step;
            } break;
            /* LCOV_EXCL_START*/
            default: {
              fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
              return -1;
            } break;
            /* LCOV_EXCL_STOP*/
          }

          op2->left = tright;
          op2->ret = tmp;
        }
        step = *step_out;
      } else {
        op1->ret = step;
        *step_out = step;
      }
    }

    *step_out = step;

    first = 0;
  }

  return step;
}

static int rule_parse(struct rules_t *obj) {
  int type = 0, type1 = 0, go = -1, step_out = -1, step = 0, pos = 0;
  int has_paren = -1, has_function = -1, has_if = -1, has_on = -1;
  int loop = 1, start = 0, r_rewind = -1, offset = 0;

  /* LCOV_EXCL_START*/
  if(lexer_peek(0, &type) < 0) {
    fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
    return -1;
  }
  /* LCOV_EXCL_STOP*/

  if(type != TIF && type != TEVENT) {
    fprintf(stderr, "ERROR: Expected an 'if' or an 'on' statement\n");
    return -1;
  }

  start = vm_parent(obj, TSTART, -1);

  while(loop) {
    if(go > -1) {
      switch(go) {
        /* LCOV_EXCL_START*/
        case TSTART: {
        /*
         * This should never be reached, see
         * the go = TSTART comment below.
         */
        // loop = 0;
        fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
        return -1;
        /* LCOV_EXCL_STOP*/
        } break;
        case TEVENT: {
          if(lexer_peek(pos, &type) < 0) {
            /* LCOV_EXCL_START*/
            fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
            return -1;
            /* LCOV_EXCL_STOP*/
          }

          if((step_out == -1 || (obj->bytecode[step_out]) == TTRUE) && type != TTHEN) {
            if(type == TEVENT) {
              if(offset > 0) {
                fprintf(stderr, "ERROR: nested 'on' block\n");
                return -1;
              }

              step_out = vm_parent(obj, TEVENT, pos);
              struct vm_tevent_t *a = (struct vm_tevent_t *)&obj->bytecode[step_out];

              lexer_eat(offset, pos);

              if(lexer_peek(pos, &type) == 0 && type == TTHEN) {
                /*
                 * Predict how many go slots we need to
                 * reserve for the TRUE / FALSE nodes.
                 */
                int y = pos, nrexpressions = 0;
                while(lexer_peek(y++, &type) == 0) {
                  if(type == TEOF) {
                    break;
                  }
                  if(type == TIF) {
                    struct vm_cache_t *cache = vm_cache_get(TIF, -1, y-1);
                    nrexpressions++;
                    y = cache->start + 1;
                    continue;
                  }
                  if(type == TEND) {
                    break;
                  }
                  if(type == TSEMICOLON) {
                    nrexpressions++;
                  }
                }

                if(nrexpressions == 0) {
                  fprintf(stderr, "ERROR: On block without body\n");
                  return -1;
                }
#ifdef DEBUG
                printf("nrexpressions: %d\n", nrexpressions);/*LCOV_EXCL_LINE*/
#endif

                /*
                 * The last parameter is used for
                 * for the number of operations the
                 * TRUE of FALSE will forward to.
                 */
                step = vm_parent(obj, TTRUE, nrexpressions);
                struct vm_ttrue_t *a = (struct vm_ttrue_t *)&obj->bytecode[step];
                struct vm_tevent_t *b = (struct vm_tevent_t *)&obj->bytecode[step_out];

                b->go = step;
                a->ret = step_out;

                go = TEVENT;

                step_out = step;
                lexer_eat(offset, pos);
              } else {
                fprintf(stderr, "ERROR: Expected a 'then' token\n");
                return -1;
              }
            } else {
              switch(type) {
                case TVAR: {
                  go = TVAR;
                  continue;
                } break;
                case TCEVENT: {
                  go = TCEVENT;
                  continue;
                } break;
                case TFUNCTION: {
                  struct vm_cache_t *x = vm_cache_get(TFUNCTION, -1, pos);
                  /* LCOV_EXCL_START*/
                  if(x == NULL) {
                    fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
                    return -1;
                  }
                  /* LCOV_EXCL_STOP*/
                  if(lexer_peek(x->start + 1, &type) == 0) {
                    switch(type) {
                      case TSEMICOLON: {
                        pos = x->start + 2;

                        int tmp = vm_rewind2(obj, step_out, TTRUE, TFALSE);
                        struct vm_tfunction_t *f = (struct vm_tfunction_t *)&obj->bytecode[x->step];
                        struct vm_ttrue_t *t = (struct vm_ttrue_t *)&obj->bytecode[step_out];
                        f->ret = tmp;

                        int i = 0;
                        for(i=0;i<t->nrgo;i++) {
                          if(t->go[i] == 0) {
                            t->go[i] = x->step;
                            break;
                          }
                        }

                        /* LCOV_EXCL_START*/
                        if(i == t->nrgo) {
                          fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
                          return -1;
                        }
                        /* LCOV_EXCL_STOP*/

                        go = TEVENT;
                        step_out = tmp;
                        vm_cache_del(x->start);
                      } break;
                      case TOPERATOR: {
                        go = type;
                      } break;
                      default: {
                        fprintf(stderr, "ERROR: Expected an semicolon or operator\n");
                        return -1;
                      } break;
                    }
                  }
                  continue;
                } break;
                case TIF: {
                  struct vm_cache_t *cache = vm_cache_get(TIF, -1, pos);
                  /* LCOV_EXCL_START*/
                  if(cache == NULL) {
                    fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
                    return -1;
                  }
                  /* LCOV_EXCL_STOP*/
                  step = cache->step;

                  if(lexer_peek(cache->start + 1, &type) < 0) {
                    /* LCOV_EXCL_START*/
                    fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
                    return -1;
                    /* LCOV_EXCL_STOP*/
                  }

                  struct vm_tif_t *i = (struct vm_tif_t *)&obj->bytecode[step];

                  /*
                   * Attach IF block to TRUE / FALSE node
                   */
                  int tmp = vm_rewind2(obj, step_out, TTRUE, TFALSE);

                  i->ret = step_out;
                  pos = cache->start + 1;

                  /*
                   * After an cached IF block has been linked
                   * it can be removed from cache
                   */
                  vm_cache_del(cache->start);
                  lexer_eat(offset, pos - 1);
                  pos -= 1;

                  switch(obj->bytecode[step_out]) {
                    case TFALSE:
                    case TTRUE: {
                      int x = 0;
                      struct vm_ttrue_t *t = (struct vm_ttrue_t *)&obj->bytecode[step_out];
                      /*
                       * Attach the IF block to an empty
                       * TRUE / FALSE go slot.
                       */
                      for(x=0;x<t->nrgo;x++) {
                        if(t->go[x] == 0) {
                          t->go[x] = step;
                          break;
                        }
                      }
                      /* LCOV_EXCL_START*/
                      if(x == t->nrgo) {
                        fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
                        return -1;
                      }
                      /* LCOV_EXCL_STOP*/
                      continue;
                    } break;
                    /* LCOV_EXCL_START*/
                    default: {
                      fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
                      return -1;
                    } break;
                    /* LCOV_EXCL_STOP*/
                  }
                  /* LCOV_EXCL_START*/
                  fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
                  return -1;
                  /* LCOV_EXCL_STOP*/
                } break;
                case TEOF:
                case TEND: {
                  go = -1;

                  int tmp = vm_rewind(obj, step_out, TEVENT);
                  /*
                   * Should never happen
                   */
                  /* LCOV_EXCL_START*/
                  if(has_on > 0) {
                    fprintf(stderr, "ERROR: On block inside on block\n");
                    return -1;
                  }
                  /* LCOV_EXCL_START*/

                  if(has_on == 0) {
                    struct vm_tstart_t *b = (struct vm_tstart_t *)&obj->bytecode[start];
                    struct vm_tif_t *a = (struct vm_tif_t *)&obj->bytecode[tmp];
                    b->go = tmp;
                    a->ret = start;

                    step = vm_parent(obj, TEOF, -1);
#ifdef DEBUG
                    printf("nr steps: %d\n", step);/*LCOV_EXCL_LINE*/
#endif
                    tmp = vm_rewind(obj, tmp, TSTART);

                    struct vm_tstart_t *node = (struct vm_tstart_t *)&obj->bytecode[tmp];
                    node->ret = step;
                  }

                  /*
                   * If we are the root IF block
                   */
                  if(has_on == 0) {
                    loop = 0;
                    obj->pos.vars = obj->nrbytes;
                  }

                  pos = 0;
                  has_paren = -1;
                  has_function = -1;
                  has_if = -1;
                  has_on = -1;
                  step_out = -1;

                  continue;
                } break;
                default: {
                  fprintf(stderr, "ERROR: Unexpected token\n");
                  return -1;
                } break;
              }
            }
          } else {
            /* LCOV_EXCL_START*/
            fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
            return -1;
            /* LCOV_EXCL_STOP*/
          }
        } break;
        case TIF: {
          if(lexer_peek(pos, &type) < 0) {
            /* LCOV_EXCL_START*/
            fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
            return -1;
            /* LCOV_EXCL_STOP*/
          }

          if(
              (
                step_out == -1 ||
                (obj->bytecode[step_out]) == TTRUE ||
                (obj->bytecode[step_out]) == TFALSE
              ) && (type != TTHEN && type != TELSE)
            ) {
            /*
             * Link cached IF blocks together
             */
            struct vm_cache_t *cache = vm_cache_get(TIF, -1, pos);
            if(cache != NULL) {
              step = cache->step;
              if(lexer_peek(cache->start + 1, &type) < 0) {
                /* LCOV_EXCL_START*/
                fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
                return -1;
                /* LCOV_EXCL_STOP*/
              }

              struct vm_tif_t *i = (struct vm_tif_t *)&obj->bytecode[step];

              /*
               * Attach IF block to TRUE / FALSE node
               */
              int tmp = vm_rewind2(obj, step_out, TTRUE, TFALSE);

              i->ret = step_out;
              pos = cache->start;


              /*
               * After an cached IF block has been linked
               * it can be removed from cache
               */
              vm_cache_del(cache->start);
              lexer_eat(offset, pos);
              switch(obj->bytecode[step_out]) {
                case TFALSE:
                case TTRUE: {
                  int x = 0;
                  struct vm_ttrue_t *t = (struct vm_ttrue_t *)&obj->bytecode[step_out];
                  /*
                   * Attach the IF block to an empty
                   * TRUE / FALSE go slot.
                   */
                  for(x=0;x<t->nrgo;x++) {
                    if(t->go[x] == 0) {
                      t->go[x] = step;
                      break;
                    }
                  }
                  /* LCOV_EXCL_START*/
                  if(x == t->nrgo) {
                    fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
                    return -1;
                  }
                  /* LCOV_EXCL_STOP*/
                } break;
                /* LCOV_EXCL_START*/
                default: {
                  fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
                  return -1;
                } break;
                /* LCOV_EXCL_STOP*/
              }

              /*
               * An IF block directly followed by another IF block
               */
              if(lexer_peek(pos, &type) == 0 && type == TIF) {
                go = TIF;
                continue;
              }
            }

            if(type == TIF) {
              step = vm_parent(obj, type, -1);
              struct vm_tif_t *a = (struct vm_tif_t *)&obj->bytecode[step];

              /*
               * If this is a nested IF block,
               * cache it for linking further on
               */
              if(has_if > 0) {
                vm_cache_add(TIF, step, has_if);
                pos++;
              } else {
                lexer_eat(offset, pos);
              }

              if(lexer_peek(pos+1, &type) == 0 && type == TOPERATOR) {
                go = TOPERATOR;
                step_out = step;
                continue;
              } else if(lexer_peek(pos, &type) == 0 && type == LPAREN) {
                step_out = step;

                /*
                 * Link cached parenthesis blocks
                 */
                struct vm_cache_t *cache = vm_cache_get(LPAREN, -1, pos);
                /* LCOV_EXCL_START*/
                if(cache == NULL) {
                  fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
                  return -1;
                }
                /* LCOV_EXCL_STOP*/

                struct vm_lparen_t *c = (struct vm_lparen_t *)&obj->bytecode[cache->step];
                /*
                 * If this parenthesis is part of a operator
                 * let the operator handle it.
                 */
                if(lexer_peek(cache->start + 2, &type) < 0) {
                  /* LCOV_EXCL_START*/
                  fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
                  return -1;
                  /* LCOV_EXCL_STOP*/
                }

                if(type == TOPERATOR) {
                  go = TOPERATOR;
                  step_out = step;
                  continue;
                } else if(type == TTHEN) {
                  step_out = c->go;
                  go = TIF;
                  a->go = cache->step;
                  c->ret = step;
                } else {
                  fprintf(stderr, "ERROR: Unexpected token\n");
                  return -1;
                }

                lexer_eat(offset, cache->start + 1);
                vm_cache_del(cache->start);
                continue;
              } else if(lexer_peek(pos, &type) == 0 && type == TFUNCTION) {
                fprintf(stderr, "ERROR: Function without operator in if condition\n");
                return -1;
              } else {
                fprintf(stderr, "ERROR: Expected a parenthesis block, function or operator\n");
                return -1;
              }
            } else {
              switch(type) {
                case TVAR: {
                  go = TVAR;
                  continue;
                } break;
                case TCEVENT: {
                  go = TCEVENT;
                  continue;
                } break;
                case TELSE: {
                  go = TIF;
                  continue;
                } break;
                case TFUNCTION: {
                  struct vm_cache_t *x = vm_cache_get(TFUNCTION, -1, pos);
                  /* LCOV_EXCL_START*/
                  if(x == NULL) {
                    fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
                    return -1;
                  }
                  /* LCOV_EXCL_STOP*/

                  if(lexer_peek(x->start + 1, &type) == 0) {
                    switch(type) {
                      case TSEMICOLON: {
                        pos = x->start + 1;

                        int tmp = vm_rewind2(obj, step_out, TTRUE, TFALSE);
                        struct vm_tfunction_t *f = (struct vm_tfunction_t *)&obj->bytecode[x->step];
                        struct vm_ttrue_t *t = (struct vm_ttrue_t *)&obj->bytecode[step_out];
                        f->ret = tmp;

                        int i = 0;
                        for(i=0;i<t->nrgo;i++) {
                          if(t->go[i] == 0) {
                            t->go[i] = x->step;
                            break;
                          }
                        }

                        if(i == t->nrgo) {
                          /* LCOV_EXCL_START*/
                          fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
                          return -1;
                          /* LCOV_EXCL_STOP*/
                        }

                        go = TIF;
                        step_out = tmp;
                        vm_cache_del(x->start);
                        lexer_eat(offset, pos);
                      } break;
                      case TOPERATOR: {
                        go = type;
                      } break;
                      default: {
                        fprintf(stderr, "ERROR: Expected an semicolon or operator\n");
                        return -1;
                      } break;
                    }
                  }
                  continue;
                } break;
                case TEOF:
                case TEND: {
                  go = -1;

                  int tmp = vm_rewind(obj, step_out, TIF);
                  if(has_if > 0) {
                    struct vm_cache_t *cache = vm_cache_get(TIF, tmp, -1);
                    /* LCOV_EXCL_START*/
                    if(cache == NULL) {
                      fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
                      return -1;
                    }
                    /* LCOV_EXCL_STOP*/

                    r_rewind = cache->start;

                    lexer_eat(offset, pos);

                  }
                  if(has_if == 0) {
                    struct vm_tstart_t *b = (struct vm_tstart_t *)&obj->bytecode[start];
                    struct vm_tif_t *a = (struct vm_tif_t *)&obj->bytecode[tmp];
                    b->go = tmp;
                    a->ret = start;

                    step = vm_parent(obj, TEOF, -1);
#ifdef DEBUG
                    printf("nr steps: %d\n", step);/*LCOV_EXCL_LINE*/
#endif
                    tmp = vm_rewind(obj, tmp, TSTART);

                    struct vm_tstart_t *node = (struct vm_tstart_t *)&obj->bytecode[tmp];
                    node->ret = step;
                  }

                  /*
                   * If we are the root IF block
                   */
                  if(has_if == 0) {
                    loop = 0;
                    obj->pos.vars = obj->nrbytes;
                  }

                  pos = 0;
                  has_paren = -1;
                  has_function = -1;
                  has_if = -1;
                  has_on = -1;
                  step_out = -1;

                  continue;
                } break;
                default: {
                  fprintf(stderr, "ERROR: Unexpected token\n");
                  return -1;
                } break;
              }
            }
          } else {
            int t = 0;

            if(lexer_peek(pos, &type) < 0) {
              /* LCOV_EXCL_START*/
              fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
              return -1;
              /* LCOV_EXCL_STOP*/
            }

            if(type == TTHEN) {
              t = TTRUE;
            } else if(type == TELSE) {
              t = TFALSE;
            }

            /*
             * Predict how many go slots we need to
             * reserve for the TRUE / FALSE nodes.
             */
            int y = pos, nrexpressions = 0;

            while(lexer_peek(y++, &type) == 0) {
              if(type == TEOF) {
                break;
              }
              if(type == TIF) {
                struct vm_cache_t *cache = vm_cache_get(TIF, -1, y-1);
                nrexpressions++;
                y = cache->start + 1;
                continue;
              }
              if(type == TEND) {
                break;
              }
              if(type == TSEMICOLON) {
                nrexpressions++;
              }
              if(t == TTRUE && type == TELSE) {
                break;
              }
            }

            if(nrexpressions == 0) {
              fprintf(stderr, "ERROR: If block without body\n");
              return -1;
            }
#ifdef DEBUG
            printf("nrexpressions: %d\n", nrexpressions);/*LCOV_EXCL_LINE*/
#endif

            /*
             * If we came from further in the script
             * make sure we hook our 'then' and 'else'
             * to the last condition.
             */
            step_out = vm_rewind(obj, step_out, TIF);

            /*
             * The last parameter is used for
             * for the number of operations the
             * TRUE of FALSE will forward to.
             */
            step = vm_parent(obj, t, nrexpressions);
            struct vm_ttrue_t *a = (struct vm_ttrue_t *)&obj->bytecode[step];
            struct vm_tif_t *b = (struct vm_tif_t *)&obj->bytecode[step_out];

            if(t == TTRUE) {
              b->true_ = step;
            } else {
              b->false_ = step;
            }
            a->ret = step_out;

            go = TIF;

            step_out = step;
            lexer_eat(offset, pos);
          }
        } break;
        case TOPERATOR: {
          int a = -1, val = 0;
          int source = step_out;

          if(lexer_peek(pos, &a) == 0) {
            switch(a) {
              case LPAREN: {
                struct vm_cache_t *x = vm_cache_get(LPAREN, -1, pos);
                /* LCOV_EXCL_START*/
                if(x == NULL) {
                  fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
                  return -1;
                }
                /* LCOV_EXCL_STOP*/
                int oldpos = pos;
                pos = x->start + 1;
                step_out = x->step;
                vm_cache_del(oldpos);
                lexer_eat(offset, pos);
                pos -= 1;
              } break;
              case TFUNCTION: {
                struct vm_cache_t *x = vm_cache_get(TFUNCTION, -1, pos);
                /* LCOV_EXCL_START*/
                if(x == NULL) {
                  fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
                  return -1;
                }
                /* LCOV_EXCL_STOP*/
                int oldpos = pos;
                pos = x->start;
                step_out = x->step;
                vm_cache_del(oldpos);
                lexer_eat(offset, pos);
              } break;
              case TVAR: {
                step_out = vm_parent(obj, a, pos);
                lexer_eat(offset, pos);
              } break;
              case TNUMBER: {
                step_out = vm_parent(obj, a, pos);
                lexer_eat(offset, pos);
              } break;
              case VNULL: {
                step_out = vm_parent(obj, a, pos);
                lexer_eat(offset, pos);
              } break;
              /* LCOV_EXCL_START*/
              default: {
                fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
                return -1;
              } break;
              /* LCOV_EXCL_STOP*/
            }
          }

          /*
           * The return value is the positition
           * of the root operator
           */
          int step = lexer_parse_math_order(obj, TOPERATOR, &pos, &step_out, offset, source);
          if(step == -1) {
            return -1;
          }

          {
            struct vm_tgeneric_t *node = (struct vm_tgeneric_t *)&obj->bytecode[step_out];
            node->ret = source;

            switch(obj->bytecode[source]) {
              case TIF: {
                struct vm_tif_t *node = (struct vm_tif_t *)&obj->bytecode[source];
                node->go = step;
              } break;
              case TVAR: {
                struct vm_tvar_t *node = (struct vm_tvar_t *)&obj->bytecode[source];
                node->go = step;
              } break;
              /*
               * If this operator block is a function
               * argument, attach it to a empty go slot
               */
              case TFUNCTION: {
                struct vm_tfunction_t *node = (struct vm_tfunction_t *)&obj->bytecode[source];
                int i = 0;
                for(i=0;i<node->nrgo;i++) {
                  if(node->go[i] == 0) {
                    node->go[i] = step;
                    break;
                  }
                }
                go = TFUNCTION;
                step_out = step;
                continue;
              } break;
              default: {
                fprintf(stderr, "ERROR: Unexpected token\n");
                return -1;
              } break;
            }
          }
          if(lexer_peek(pos, &type) == 0) {
            switch(type) {
              case TTHEN: {
                go = TIF;
              } break;
              case TSEMICOLON: {
                go = TVAR;
              } break;
              default: {
                fprintf(stderr, "ERROR: Unexpected token\n");
                return -1;
              } break;
            }
          }
        } break;
        case VNULL: {
          struct vm_vnull_t *node = NULL;
          if(lexer_peek(pos, &type) < 0) {
            /* LCOV_EXCL_START*/
            fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
            return -1;
            /* LCOV_EXCL_STOP*/
          }

          step = vm_parent(obj, VNULL, pos);

          pos++;

          switch((obj->bytecode[step_out])) {
            case TVAR: {
              struct vm_tvar_t *tmp = (struct vm_tvar_t *)&obj->bytecode[step_out];
              tmp->go = step;
            } break;
            /*
             * FIXME: Think of a rule that can trigger this
             */
            /* LCOV_EXCL_START*/
            default: {
              fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
              return -1;
            } break;
            /* LCOV_EXCL_STOP*/
          }

          node = (struct vm_vnull_t *)&obj->bytecode[step];
          node->ret = step_out;

          int tmp = vm_rewind2(obj, step_out, TVAR, TOPERATOR);
          go = (obj->bytecode[tmp]);
          step_out = step;
        } break;
        /* LCOV_EXCL_START*/
        case TSEMICOLON: {
          fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
          return -1;
        } break;
        /* LCOV_EXCL_STOP*/
        case TVAR: {
          /*
           * We came from the TRUE node from the IF root,
           * which means we start parsing the variable name.
           */
          if((obj->bytecode[step_out]) == TTRUE || (obj->bytecode[step_out]) == TFALSE) {
            struct vm_tvar_t *node = NULL;
            struct vm_ttrue_t *node1 = NULL;

            step = vm_parent(obj, TVAR, pos);

            node = (struct vm_tvar_t *)&obj->bytecode[step];
            node1 = (struct vm_ttrue_t *)&obj->bytecode[step_out];

            lexer_eat(offset, pos);

            int x = 0;
            for(x=0;x<node1->nrgo;x++) {
              if(node1->go[x] == 0) {
                node1->go[x] = step;
                break;
              }
            }

            /* LCOV_EXCL_START*/
            if(x == node1->nrgo) {
              fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
              return -1;
            }
            /* LCOV_EXCL_STOP*/
            node->ret = step_out;

            step_out = step;

            if(lexer_peek(pos, &type) < 0 || type != TASSIGN) {
              /* LCOV_EXCL_START*/
              fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
              return -1;
              /* LCOV_EXCL_STOP*/
            }
            lexer_eat(offset, pos);

            if(lexer_peek(pos+1, &type) == 0 && type == TOPERATOR) {
              switch(type) {
                case TOPERATOR: {
                  go = TOPERATOR;
                  step_out = step;
                  continue;
                } break;
              }
            } else if(lexer_peek(pos, &type) == 0) {
              switch(type) {
                case TNUMBER: {
                  int foo = vm_parent(obj, type, pos);
                  struct vm_tgeneric_t *a = (struct vm_tgeneric_t *)&obj->bytecode[foo];
                  node = (struct vm_tvar_t *)&obj->bytecode[step];
                  node->go = foo;
                  a->ret = step;

                  go = TVAR;
                  lexer_eat(offset, pos);
                } break;
                case TVAR: {
                  go = TVAR;
                  step_out = step;
                } break;
                case VNULL: {
                  go = VNULL;
                  step_out = step;
                } break;
                case TFUNCTION: {
                  struct vm_cache_t *x = vm_cache_get(TFUNCTION, -1, pos);
                  /* LCOV_EXCL_START*/
                  if(x == NULL) {
                    fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
                    return -1;
                  }
                  /* LCOV_EXCL_STOP*/
                  if(lexer_peek(x->start + 1, &type) == 0) {
                    switch(type) {
                      case TSEMICOLON: {
                        pos = x->start + 1;

                        struct vm_tfunction_t *f = (struct vm_tfunction_t *)&obj->bytecode[x->step];
                        struct vm_tvar_t *v = (struct vm_tvar_t *)&obj->bytecode[step_out];
                        f->ret = step;
                        v->go = x->step;

                        int tmp = vm_rewind2(obj, step_out, TTRUE, TFALSE);
                        int tmp1 = vm_rewind2(obj, tmp, TIF, TEVENT);

                        go = obj->bytecode[tmp1];
                        step_out = tmp;
                        vm_cache_del(x->start);
                        lexer_eat(offset, pos);
                        pos -= 1;
                      } break;
                      case TOPERATOR: {
                        go = type;
                      } break;
                      default: {
                        fprintf(stderr, "ERROR: Expected an semicolon or operator\n");
                        return -1;
                      } break;
                    }
                  }
                } break;
                case LPAREN: {
                  int oldpos = pos;
                  struct vm_cache_t *x = vm_cache_get(LPAREN, -1, pos);
                  /* LCOV_EXCL_START*/
                  if(x == NULL) {
                    fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
                    return -1;
                  }
                  /* LCOV_EXCL_STOP*/
                  if(lexer_peek(x->start + 2, &type) == 0) {
                    if(type == TSEMICOLON) {
                      pos = x->start + 2;

                      struct vm_lparen_t *l = (struct vm_lparen_t *)&obj->bytecode[x->step];
                      struct vm_tvar_t *v = (struct vm_tvar_t *)&obj->bytecode[step_out];
                      l->ret = step;
                      v->go = x->step;

                      int tmp = vm_rewind2(obj, step_out, TTRUE, TFALSE);

                      go = TIF;
                      step_out = tmp;

                      vm_cache_del(oldpos);
                      lexer_eat(offset, pos);
                    } else {
                      go = type;
                    }
                  }
                } break;
                default: {
                  fprintf(stderr, "ERROR: Unexpected token\n");
                  return -1;
                } break;
              }
            } else {
              /* LCOV_EXCL_START*/
              fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
              return -1;
              /* LCOV_EXCL_STOP*/
            }
          /*
           * The variable has been called as a value
           */
          } else if((obj->bytecode[step_out]) == TVAR && lexer_peek(pos, &type) == 0 && type != TSEMICOLON) {
            if(lexer_peek(pos, &type) == 0 && type == TVAR) {
              step = vm_parent(obj, TVAR, pos);
              lexer_eat(offset, pos);

              struct vm_tvar_t *in = (struct vm_tvar_t *)&obj->bytecode[step];
              struct vm_tvar_t *out = (struct vm_tvar_t *)&obj->bytecode[step_out];
              in->ret = step_out;
              out->go = step;

              if(lexer_peek(pos, &type) == 0) {
                switch(type) {
                  case TSEMICOLON: {
                    go = TVAR;
                  } break;
                  default: {
                    fprintf(stderr, "ERROR: Expected a semicolon\n");
                    return -1;
                  } break;
                }
              }
            }
          } else {
            int tmp = step_out;
            lexer_eat(offset, pos);
            while(1) {
              if((obj->bytecode[tmp]) != TTRUE &&
                 (obj->bytecode[tmp]) != TFALSE) {
                struct vm_tgeneric_t *node = (struct vm_tgeneric_t *)&obj->bytecode[tmp];
                tmp = node->ret;
              } else {
                int tmp1 = vm_rewind2(obj, tmp, TIF, TEVENT);
                go = obj->bytecode[tmp1];
                step_out = tmp;
                break;
              }
            }
          }
        } break;
        case TCEVENT: {
          struct vm_tcevent_t *node = NULL;
          /* LCOV_EXCL_START*/
          if(lexer_peek(pos, &type) < 0 || type != TCEVENT) {
            fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
            return -1;
          }
          /* LCOV_EXCL_STOP*/

          step = vm_parent(obj, TCEVENT, pos);

          node = (struct vm_tcevent_t *)&obj->bytecode[step];

          lexer_eat(offset, pos);

          if(lexer_peek(pos, &type) < 0) {
            /* LCOV_EXCL_START*/
            fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
            return -1;
            /* LCOV_EXCL_STOP*/
          }

          if(type != TSEMICOLON) {
            fprintf(stderr, "ERROR: Expected a semicolon\n");
            return -1;
          }

          lexer_eat(offset, pos);

          int tmp = vm_rewind2(obj, step_out, TTRUE, TFALSE);
          struct vm_ttrue_t *node1 = (struct vm_ttrue_t *)&obj->bytecode[tmp];

          int x = 0;
          for(x=0;x<node1->nrgo;x++) {
            if(node1->go[x] == 0) {
              node1->go[x] = step;
              break;
            }
          }

          /* LCOV_EXCL_START*/
          if(x == node1->nrgo) {
            fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
            return -1;
          }
          /* LCOV_EXCL_STOP*/
          node->ret = step_out;

          int tmp1 = vm_rewind2(obj, step_out, TIF, TEVENT);
          go = obj->bytecode[tmp1];
          step_out = tmp;
        } break;
        case LPAREN: {
          int a = -1, b = -1, val = 0;

          if(lexer_peek(has_paren+1, &a) == 0) {
            switch(a) {
              case LPAREN: {
                struct vm_cache_t *x = vm_cache_get(LPAREN, -1, has_paren+1);
                /* LCOV_EXCL_START*/
                if(x == NULL) {
                  fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
                  return -1;
                }
                /* LCOV_EXCL_STOP*/
                pos = x->start + 1;
                step_out = x->step;
                vm_cache_del(has_paren+1);
                lexer_eat(offset, pos);
                pos -= 1;
              } break;
              case TFUNCTION: {
                struct vm_cache_t *x = vm_cache_get(TFUNCTION, -1, has_paren+1);
                /* LCOV_EXCL_START*/
                if(x == NULL) {
                  fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
                  return -1;
                }
                /* LCOV_EXCL_STOP*/
                pos = x->start;
                step_out = x->step;
                vm_cache_del(has_paren+1);
                lexer_eat(offset, pos);
              } break;
              case TVAR: {
                step_out = vm_parent(obj, a, has_paren+1);
                pos = has_paren + 1;
                lexer_eat(offset, pos);
              } break;
              case TNUMBER: {
                step_out = vm_parent(obj, a, has_paren+1);
                pos = has_paren + 1;
                lexer_eat(offset, pos);
              } break;
              case VNULL: {
                step_out = vm_parent(obj, a, has_paren+1);
                pos = has_paren + 1;
                lexer_eat(offset, pos);
              } break;
              case RPAREN: {
                fprintf(stderr, "ERROR: Empty parenthesis block\n");
                return -1;
              } break;
              case TOPERATOR: {
                fprintf(stderr, "ERROR: Unexpected operator\n");
                return -1;
              } break;
              default: {
                fprintf(stderr, "ERROR: Unexpected token\n");
                return -1;
              }
            }
          }

          /*
           * The return value is the positition
           * of the root operator
           */
          int step = lexer_parse_math_order(obj, LPAREN, &pos, &step_out, offset, 0);

          if(lexer_peek(pos, &b) < 0) {
            /* LCOV_EXCL_START*/
            fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
            return -1;
            /* LCOV_EXCL_STOP*/
          }

          if(b != RPAREN) {
            fprintf(stderr, "ERROR: Expected a closing parenthesis\n");
            return -1;
          }

          {
            struct vm_tgeneric_t *node = (struct vm_tgeneric_t *)&obj->bytecode[step_out];
            node->ret = 0;
          }

          if(lexer_peek(has_paren, &type) < 0 || type != LPAREN) {
            /* LCOV_EXCL_START*/
            fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
            return -1;
            /* LCOV_EXCL_STOP*/
          }

          step = vm_parent(obj, LPAREN, has_paren);
          struct vm_lparen_t *node = (struct vm_lparen_t *)&obj->bytecode[step];
          node->go = step_out;

          vm_cache_add(LPAREN, step, has_paren);

          r_rewind = has_paren;

          struct vm_tgeneric_t *node1 = (struct vm_tgeneric_t *)&obj->bytecode[step_out];
          node1->ret = step;

          go = -1;

          pos = 0;

          has_paren = -1;
          has_function = -1;
          has_if = -1;
          has_on = -1;
          step_out = -1;
        } break;
        case TFUNCTION: {
          struct vm_tfunction_t *node = NULL;
          int val = 0, arg = 0;

          /*
           * We've entered the function name,
           * so we start by creating a proper
           * root node.
           */
          if(step_out == -1) {
            pos = has_function+1;
            if(lexer_peek(has_function, &type) < 0) {
              /* LCOV_EXCL_START*/
              fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
              return -1;
              /* LCOV_EXCL_STOP*/
            }

            /*
             * Determine how many arguments this
             * function has.
             */
            int y = pos + 1, nrargs = 0;
            while(lexer_peek(y++, &type) == 0) {
              if(type == TEOF || type == RPAREN) {
                break;
              }
              if(type == LPAREN) {
                struct vm_cache_t *cache = vm_cache_get(LPAREN, -1, y-1);
                y = cache->start + 2;
                continue;
              }
              if(type == TFUNCTION) {
                struct vm_cache_t *cache = vm_cache_get(TFUNCTION, -1, y-1);
                y = cache->start + 1;
                continue;
              }
              if(type == TCOMMA) {
                nrargs++;
              }
            }
#ifdef DEBUG
            printf("nrarguments: %d\n", nrargs + 1);/*LCOV_EXCL_LINE*/
#endif
            step = vm_parent(obj, TFUNCTION, nrargs + 1);

            node = (struct vm_tfunction_t *)&obj->bytecode[step];

            lexer_bytecode_pos(has_function, &val);

            node->token = bytecode[val+1];

            lexer_peek(pos, &type);

            /* LCOV_EXCL_START*/
            if(lexer_peek(pos, &type) < 0 || type != LPAREN) {
              fprintf(stderr, "ERROR: Expected a closing parenthesis\n");
              return -1;
            }
            /* LCOV_EXCL_STOP*/

            lexer_eat(offset, pos);

          /*
           * When the root node has been created
           * we parse the different arguments.
           */
          } else {
            int i = 0;
            step = vm_rewind(obj, step_out, TFUNCTION);

            node = (struct vm_tfunction_t *)&obj->bytecode[step];

            /*
             * Find the argument we've just
             * looked up.
             */
            for(i=0;i<node->nrgo;i++) {
              if(node->go[i] == step_out) {
                arg = i+1;
                break;
              }
            }

            /*
             * Look for the next token in the list
             * of arguments
             */
            while(1) {
              if(lexer_peek(pos, &type) < 0) {
                /* LCOV_EXCL_START*/
                fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
                return -1;
                /* LCOV_EXCL_STOP*/
              }

              if(type == TCOMMA) {
                lexer_eat(offset, pos);
                break;
              }
              if(type == RPAREN) {
                break;
              }
            }

            lexer_peek(pos, &type);
          }

          /*
           * When this token is a semicolon
           * we're done looking up the argument
           * nodes. In the other case, we try to
           * lookup the other arguments in the
           * list.
           */
          if(type != RPAREN) {
            while(1) {
              if(lexer_peek(pos + 1, &type) < 0) {
                /* LCOV_EXCL_START*/
                fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
                return -1;
                /* LCOV_EXCL_STOP*/
              } else if(type == TOPERATOR) {
                go = TOPERATOR;
                step_out = step;
                break;
              }

              if(lexer_peek(pos, &type) < 0) {
                /* LCOV_EXCL_START*/
                fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
                return -1;
                /* LCOV_EXCL_STOP*/
              }

              switch(type) {
                /*
                 * If the arguments are a parenthesis or
                 * another function, link those together.
                 */
                case LPAREN: {
                  struct vm_cache_t *cache = vm_cache_get(LPAREN, -1, pos);
                  struct vm_lparen_t *paren = (struct vm_lparen_t *)&obj->bytecode[cache->step];
                  /* LCOV_EXCL_START*/
                  if(cache == NULL) {
                    fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
                    return -1;
                  }
                  /* LCOV_EXCL_STOP*/

                  int oldpos = pos;
                  pos = cache->start + 1;
                  node->go[arg++] = cache->step;
                  paren->ret = step;
                  vm_cache_del(oldpos);
                  lexer_eat(offset, pos);
                  pos -= 1;
                } break;
                case TFUNCTION: {
                  struct vm_cache_t *cache = vm_cache_get(TFUNCTION, -1, pos);
                  struct vm_tfunction_t *func = (struct vm_tfunction_t *)&obj->bytecode[cache->step];
                  /* LCOV_EXCL_START*/
                  if(cache == NULL) {
                    fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
                    return -1;
                  }
                  /* LCOV_EXCL_STOP*/
                  int oldpos = pos;
                  pos = cache->start;

                  node->go[arg++] = cache->step;
                  func->ret = step;
                  vm_cache_del(oldpos);
                  lexer_eat(offset, pos);
                } break;
                case TNUMBER: {
                  int a = vm_parent(obj, TNUMBER, pos);
                  lexer_eat(offset, pos);

                  node = (struct vm_tfunction_t *)&obj->bytecode[step];
                  node->go[arg++] = a;
                } break;
                case VNULL: {
                  int a = vm_parent(obj, VNULL, pos);

                  node = (struct vm_tfunction_t *)&obj->bytecode[step];
                  node->go[arg++] = a;

                  struct vm_vnull_t *tmp = (struct vm_vnull_t *)&obj->bytecode[a];
                  tmp->ret = step;

                  lexer_eat(offset, pos);
                } break;
                case TVAR: {
                  int a = vm_parent(obj, TVAR, pos);

                  node = (struct vm_tfunction_t *)&obj->bytecode[step];
                  node->go[arg++] = a;

                  struct vm_tvar_t *tmp = (struct vm_tvar_t *)&obj->bytecode[a];
                  tmp->ret = step;
                  lexer_eat(offset, pos);
                } break;
                default: {
                  fprintf(stderr, "ERROR: Unexpected token\n");
                  return -1;
                } break;
              }


              if(lexer_peek(pos, &type) < 0) {
                /* LCOV_EXCL_START*/
                fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
                return -1;
                /* LCOV_EXCL_STOP*/
              }

              lexer_eat(offset, pos);

              /*
               * A right parenthesis means we've
               * reached the end of the argument list
               */
              if(type == RPAREN) {
                break;
              } else if(type != TCOMMA) {
                fprintf(stderr, "ERROR: Expected a closing parenthesis\n");
                return -1;
              }
            }
          } else {
            lexer_eat(offset, pos);
          }

          /*
           * When we're back to the function
           * root, cache it for further linking.
           */
          if(go == TFUNCTION) {
            vm_cache_add(TFUNCTION, step, has_function);

            r_rewind = has_function;

            go = -1;

            pos = 0;

            has_paren = -1;
            has_function = -1;
            has_if = -1;
            has_on = -1;
            step_out = -1;
          }
        } break;
      }
    } else {
      /*
       * Start looking for the furthest
       * nestable token, so we can cache
       * it first.
       */
      if(r_rewind == -1) {
        if(lexer_peek(pos++, &type) < 0) {
          /* LCOV_EXCL_START*/
          fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
          return -1;
          /* LCOV_EXCL_STOP*/
        }
      /*
       * If we found the furthest, rewind back step
       * by step until we are at the beginning again.
       */
      } else {
        if(r_rewind > 0 && lexer_peek(--r_rewind, &type) < 0) {
          go = -1;
        }
        pos = r_rewind+1;
      }
      if(type == TFUNCTION) {
        has_function = pos-1;
      } else if(type == TIF) {
        has_if = pos-1;
      } else if(type == TEVENT) {
        has_on = pos-1;
      } else if(type == LPAREN && lexer_peek(pos-2, &type1) == 0 && type1 != TFUNCTION) {
        has_paren = pos-1;
      }
      if(has_function != -1 || has_paren != -1 || has_if != -1 || has_on != -1) {
        if(type == TEOF || r_rewind > -1) {
          if(max(has_function, max(has_paren, max(has_if, has_on))) == has_function) {
            offset = (pos = has_function)+1;
            go = TFUNCTION;
            continue;
          } else if(max(has_function, max(has_paren, max(has_if, has_on))) == has_if) {
            offset = (pos = has_if);
            if(has_if > 0) {
              offset++;
            }
            go = TIF;
            continue;
          } else if(max(has_function, max(has_paren, max(has_if, has_on))) == has_on) {
            offset = (pos = has_on);
            go = TEVENT;
            if(has_on > 0) {
              offset++;
            }
            continue;
          } else {
            offset = (pos = has_paren)+1;
            go = LPAREN;
            continue;
          }
          has_paren = -1;
          has_if = -1;
          has_on = -1;
          has_function = -1;
          step_out = -1;
          pos = 0;
        }
      } else if(r_rewind <= 0) {
        /*
         * This should never be reached, because at
         * r_rewind = 0 either an on or if token
         * should be found, already parsing the full
         * rule.
         */
        /* LCOV_EXCL_START*/
        /*
          go = TSTART;
          pos = 0;
          continue;
        */
        fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
        return -1;
        /* LCOV_EXCL_STOP*/
      }
    }
  }

/* LCOV_EXCL_START*/
  struct vm_tstart_t *node = (struct vm_tstart_t *)&obj->bytecode[0];
  if(node->go == 0) {
    fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
    return -1;
  }
/* LCOV_EXCL_STOP*/
  return 0;
}

/*LCOV_EXCL_START*/
#ifdef DEBUG
static void print_ast(struct rules_t *obj) {
  int i = 0, x = 0;

  for(i=x;alignedbytes(i)<obj->nrbytes;i++) {
    i = alignedbytes(i);
    switch(obj->bytecode[i]) {
      case TSTART: {
        struct vm_tstart_t *node = (struct vm_tstart_t *)&obj->bytecode[i];
        printf("\"%d\"[label=\"START\"]\n", i);
        printf("\"%d\" -> \"%d\"\n", i, node->go);
        printf("\"%d\" -> \"%d\"\n", i, node->ret);
        i+=sizeof(struct vm_tstart_t)-1;
      } break;
      case TEOF: {
        struct vm_teof_t *node = (struct vm_teof_t *)&obj->bytecode[i];
        printf("\"%d\"[label=\"EOF\"]\n", i);
        i+=sizeof(struct vm_teof_t)-1;
      } break;
      case VNULL: {
        struct vm_vnull_t *node = (struct vm_vnull_t *)&obj->bytecode[i];
        printf("\"%d\"[label=\"NULL\"]\n", i);
        i+=sizeof(struct vm_vnull_t)-1;
      } break;
      case TIF: {
        struct vm_tif_t *node = (struct vm_tif_t *)&obj->bytecode[i];
        printf("\"%d\"[label=\"IF\"]\n", i);
        printf("\"%d\" -> \"%d\"\n", i, node->go);
        // printf("\"%i\" -> \"%i\"\n", i, node->ret);
        if(node->true_ > 0) {
          printf("\"%d\" -> \"%d\"\n", i, node->true_);
        }
        if(node->false_ > 0) {
          printf("\"%d\" -> \"%d\"\n", i, node->false_);
        }
        printf("{ rank=same edge[style=invis]");
        if(node->true_ > 0) {
          printf(" \"%d\" -> \"%d\";", node->go, node->true_);
        }
        if(node->false_ > 0) {
          printf(" \"%d\" -> \"%d\";", node->true_, node->false_);
        }
        printf(" rankdir = LR}\n");
        i+=sizeof(struct vm_tif_t)-1;
      } break;
      case LPAREN: {
        struct vm_lparen_t *node = (struct vm_lparen_t *)&obj->bytecode[i];
        printf("\"%d\"[label=\"(\"]\n", i);
        printf("\"%d\" -> \"%d\"\n", i, node->go);
        // printf("\"%d\" -> \"%d\"\n", i, node->ret);
        i+=sizeof(struct vm_lparen_t)-1;
      } break;
      case TFALSE:
      case TTRUE: {
        int x = 0;
        struct vm_ttrue_t *node = (struct vm_ttrue_t *)&obj->bytecode[i];
        if((obj->bytecode[i]) == TFALSE) {
          printf("\"%d\"[label=\"FALSE\"]\n", i);
        } else {
          printf("\"%d\"[label=\"TRUE\"]\n", i);
        }
        for(x=0;x<node->nrgo;x++) {
          printf("\"%d\" -> \"%d\"\n", i, node->go[x]);
        }
        printf("{ rank=same edge[style=invis] ");
        for(x=0;x<node->nrgo-1;x++) {
          printf("\"%d\" -> \"%d\"", node->go[x], node->go[x+1]);
          if(x+1 < node->nrgo-1) {
            printf(" -> ");
          }
        }
        printf(" rankdir = LR}\n");
        i+=sizeof(struct vm_ttrue_t)+(sizeof(uint16_t)*node->nrgo)-1;
      } break;
      case TFUNCTION: {
        int x = 0;
        struct vm_tfunction_t *node = (struct vm_tfunction_t *)&obj->bytecode[i];
        printf("\"%d\"[label=\"%s\"]\n", i, event_functions[node->token].name);
        for(x=0;x<node->nrgo;x++) {
          printf("\"%d\" -> \"%d\"\n", i, node->go[x]);
        }
        // printf("\"%d\" -> \"%d\"\n", i, node->ret);
        i+=sizeof(struct vm_tfunction_t)+(sizeof(uint16_t)*node->nrgo)-1;
      } break;
      case TVAR: {
        struct vm_tvar_t *node = (struct vm_tvar_t *)&obj->bytecode[i];
        printf("\"%d\"[label=\"%s\"]\n", i, node->token);
        if(node->go > 0) {
          printf("\"%d\" -> \"%d\"\n", i, node->go);
        }
        // printf("\"%d\" -> \"%d\"\n", i, node->ret);
        i+=sizeof(struct vm_tvar_t)+strlen((char *)node->token);
      } break;
      case TNUMBER: {
        struct vm_tnumber_t *node = (struct vm_tnumber_t *)&obj->bytecode[i];
        printf("\"%d\"[label=\"%s\"]\n", i, node->token);
        i+=sizeof(struct vm_tnumber_t)+strlen((char *)node->token);
      } break;
      case VINTEGER: {
        struct vm_vinteger_t *node = (struct vm_vinteger_t *)&obj->bytecode[i];
        printf("\"%d\"[label=\"%d\"]\n", i, node->value);
        i+=sizeof(struct vm_vinteger_t)-1;
      } break;
      case VFLOAT: {
        struct vm_vfloat_t *node = (struct vm_vfloat_t *)&obj->bytecode[i];
        printf("\"%d\"[label=\"%g\"]\n", i, node->value);
        i+=sizeof(struct vm_vfloat_t)-1;
      } break;
      case TEVENT: {
        struct vm_tevent_t *node = (struct vm_tevent_t *)&obj->bytecode[i];
        printf("\"%d\"[label=\"%s\"]\n", i, node->token);
        // printf("\"%i\" -> \"%i\"\n", i, node->ret);
        if(node->go > 0) {
          printf("\"%d\" -> \"%d\"\n", i, node->go);
        }
        i+=sizeof(struct vm_tevent_t)+strlen((char *)node->token);
      } break;
      case TCEVENT: {
        struct vm_tcevent_t *node = (struct vm_tcevent_t *)&obj->bytecode[i];
        printf("\"%d\"[label=\"%s\"]\n", i, node->token);
        // printf("\"%i\" -> \"%i\"\n", i, node->ret);
        i+=sizeof(struct vm_tcevent_t)+strlen((char *)node->token);
      } break;
      case TOPERATOR: {
        struct vm_toperator_t *node = (struct vm_toperator_t *)&obj->bytecode[i];

        printf("\"%d\"[label=\"%s\"]\n", i, event_operators[node->token].name);
        printf("\"%d\" -> \"%d\"\n", i, node->right);
        printf("\"%d\" -> \"%d\"\n", i, node->left);
        printf("{ rank=same edge[style=invis] \"%d\" -> \"%d\" rankdir = LR}\n", node->left, node->right);
        // printf("\"%d\" -> \"%d\"\n", i, node->ret);
        i+=sizeof(struct vm_toperator_t)-1;
      } break;
      default: {
      } break;
    }
  }
}

static void print_steps(struct rules_t *obj) {
  int i = 0, x = 0;

  for(i=x;alignedbytes(i)<obj->nrbytes;i++) {
    i = alignedbytes(i);
    switch(obj->bytecode[i]) {
      case TSTART: {
        struct vm_tstart_t *node = (struct vm_tstart_t *)&obj->bytecode[i];
        printf("\"%d-1\"[label=\"%d\" shape=square]\n", i, i);
        printf("\"%d-2\"[label=\"START\"]\n", i);
        printf("\"%d-3\"[label=\"%d\" shape=square]\n", i, node->ret);
        printf("\"%d-4\"[label=\"%d\" shape=square]\n", i, node->go);
        printf("\"%d-1\" -> \"%d-2\"\n", i, i);
        printf("\"%d-2\" -> \"%d-3\"\n", i, i);
        printf("\"%d-2\" -> \"%d-4\"\n", i, i);
        i+=sizeof(struct vm_tstart_t)-1;
      } break;
      case TEOF: {
        struct vm_teof_t *node = (struct vm_teof_t *)&obj->bytecode[i];
        printf("\"%d-1\"[label=\"%d\" shape=square]\n", i, i);
        printf("\"%d-2\"[label=\"EOF\"]\n", i);
        printf("\"%d-1\" -> \"%d-2\"\n", i, i);
        i+=sizeof(struct vm_teof_t)-1;
      } break;
      case VNULL: {
        struct vm_vnull_t *node = (struct vm_vnull_t *)&obj->bytecode[i];
        printf("\"%d-1\"[label=\"%d\" shape=square]\n", i, i);
        printf("\"%d-2\"[label=\"NULL\"]\n", i);
        printf("\"%d-2\" -> \"%d-3\"\n", i, i);
        printf("\"%d-3\"[label=\"%d\" shape=diamond]\n", i, node->ret);
        printf("\"%d-1\" -> \"%d-2\"\n", i, i);
        i+=sizeof(struct vm_vnull_t)-1;
      } break;
      case TIF: {
        struct vm_tif_t *node = (struct vm_tif_t *)&obj->bytecode[i];
        printf("\"%d-2\"[label=\"IF\"]\n", i);
        printf("\"%d-2\" -> \"%d-4\"\n", i, i);
        if(node->true_ > 0) {
          printf("\"%d-2\" -> \"%d-6\"\n", i, i);
        }
        if(node->false_ > 0) {
          printf("\"%d-2\" -> \"%d-7\"\n", i, i);
        }
        printf("\"%d-1\" -> \"%d-2\"\n", i, i);
        printf("\"%d-2\" -> \"%d-3\"\n", i, i);
        if(node->true_ > 0) {
          printf("\"%d-6\"[label=\"%d\" shape=square]\n", i, node->true_);
        }
        if(node->false_ > 0) {
          printf("\"%d-7\"[label=\"%d\" shape=square]\n", i, node->false_);
        }
        printf("\"%d-1\"[label=\"%d\" shape=square]\n", i, i);
        printf("\"%d-3\"[label=\"%d\" shape=diamond]\n", i, node->ret);
        printf("\"%d-4\"[label=\"%d\" shape=square]\n", i, node->go);
        i+=sizeof(struct vm_tif_t)-1;
      } break;
      case LPAREN: {
        struct vm_lparen_t *node = (struct vm_lparen_t *)&obj->bytecode[i];
        printf("\"%d-1\"[label=\"%d\" shape=square]\n", i, i);
        printf("\"%d-2\"[label=\"(\"]\n", i);
        printf("\"%d-2\" -> \"%d-4\"\n", i, i);
        printf("\"%d-2\" -> \"%d-3\"\n", i, i);
        printf("\"%d-3\"[label=\"%d\" shape=diamond]\n", i, node->ret);
        printf("\"%d-4\"[label=\"%d\" shape=square]\n", i, node->go);
        printf("\"%d-1\" -> \"%d-2\"\n", i, i);
        i+=sizeof(struct vm_lparen_t)-1;
      } break;
      case TFALSE:
      case TTRUE: {
        int x = 0;
        struct vm_ttrue_t *node = (struct vm_ttrue_t *)&obj->bytecode[i];
        printf("\"%d-1\"[label=\"%d\" shape=square]\n", i, i);
        if((obj->bytecode[i]) == TFALSE) {
          printf("\"%d-2\"[label=\"FALSE\"]\n", i);
        } else {
          printf("\"%d-2\"[label=\"TRUE\"]\n", i);
        }
        for(x=0;x<node->nrgo;x++) {
          printf("\"%d-2\" -> \"%d-%d\"\n", i, i, x+3);
          printf("\"%d-%d\"[label=\"%d\" shape=square]\n", i, x+3, node->go[x]);
        }
        printf("\"%d-2\" -> \"%d-%d\"\n", i, i, x+4);
        printf("\"%d-%d\"[label=\"%d\" shape=diamond]\n", i, x+4, node->ret);
        printf("\"%d-1\" -> \"%d-2\"\n", i, i);
        i+=sizeof(struct vm_ttrue_t)+(sizeof(uint16_t)*node->nrgo)-1;
      } break;
      case TFUNCTION: {
        int x = 0;
        struct vm_tfunction_t *node = (struct vm_tfunction_t *)&obj->bytecode[i];
        printf("\"%d-1\"[label=\"%d\" shape=square]\n", i, i);
        printf("\"%d-2\"[label=\"%s\"]\n", i, event_functions[node->token].name);
        for(x=0;x<node->nrgo;x++) {
          printf("\"%d-2\" -> \"%d-%d\"\n", i, i, x+3);
          printf("\"%d-%d\"[label=\"%d\" shape=square]\n", i, x+3, node->go[x]);
        }
        printf("\"%d-2\" -> \"%d-%d\"\n", i, i, x+4);
        printf("\"%d-%d\"[label=\"%d\" shape=diamond]\n", i, x+4, node->ret);
        printf("\"%d-1\" -> \"%d-2\"\n", i, i);
        i+=sizeof(struct vm_tfunction_t)+(sizeof(uint16_t)*node->nrgo)-1;
      } break;
      case TCEVENT: {
        struct vm_tcevent_t *node = (struct vm_tcevent_t *)&obj->bytecode[i];
        printf("\"%d-1\"[label=\"%d\" shape=square]\n", i, i);
        printf("\"%d-2\"[label=\"%s()\"]\n", i, node->token);
        printf("\"%d-2\" -> \"%d-3\"\n", i, i);
        printf("\"%d-3\"[label=\"%d\" shape=diamond]\n", i, node->ret);
        printf("\"%d-1\" -> \"%d-2\"\n", i, i);
        i+=sizeof(struct vm_tcevent_t)+strlen((char *)node->token);
      } break;
      case TVAR: {
        struct vm_tvar_t *node = (struct vm_tvar_t *)&obj->bytecode[i];
        printf("\"%d-1\"[label=\"%d\" shape=square]\n", i, i);
        printf("\"%d-2\"[label=\"%s\"]\n", i, node->token);
        if(node->go > 0) {
          printf("\"%d-2\" -> \"%d-4\"\n", i, i);
        }
        printf("\"%d-2\" -> \"%d-3\"\n", i, i);
        printf("\"%d-3\"[label=\"%d\" shape=diamond]\n", i, node->ret);
        if(node->go > 0) {
          printf("\"%d-4\"[label=\"%d\" shape=square]\n", i, node->go);
        }
        printf("\"%d-1\" -> \"%d-2\"\n", i, i);
        i+=sizeof(struct vm_tvar_t)+strlen((char *)node->token);
      } break;
      case TEVENT: {
        struct vm_tevent_t *node = (struct vm_tevent_t *)&obj->bytecode[i];
        printf("\"%d-1\"[label=\"%d\" shape=square]\n", i, i);
        printf("\"%d-2\"[label=\"%s\"]\n", i, node->token);
        if(node->go > 0) {
          printf("\"%d-2\" -> \"%d-4\"\n", i, i);
        }
        printf("\"%d-2\" -> \"%d-3\"\n", i, i);
        printf("\"%d-3\"[label=\"%d\" shape=diamond]\n", i, node->ret);
        if(node->go > 0) {
          printf("\"%d-4\"[label=\"%d\" shape=square]\n", i, node->go);
        }
        printf("\"%d-1\" -> \"%d-2\"\n", i, i);
        i+=sizeof(struct vm_tevent_t)+strlen((char *)node->token);
      } break;
      case TNUMBER: {
        struct vm_tnumber_t *node = (struct vm_tnumber_t *)&obj->bytecode[i];
        printf("\"%d-1\"[label=\"%d\" shape=square]\n", i, i);
        printf("\"%d-2\"[label=\"%s\"]\n", i, node->token);
        printf("\"%d-1\" -> \"%d-2\"\n", i, i);
        printf("\"%d-3\"[label=\"%d\" shape=diamond]\n", i, node->ret);
        printf("\"%d-2\" -> \"%d-3\"\n", i, i);
        i+=sizeof(struct vm_tnumber_t)+strlen((char *)node->token)-1;
      } break;
      case VINTEGER: {
        struct vm_vinteger_t *node = (struct vm_vinteger_t *)&obj->bytecode[i];
        printf("\"%d-1\"[label=\"%d\" shape=square]\n", i, i);
        printf("\"%d-2\"[label=\"%d\"]\n", i, node->value);
        printf("\"%d-1\" -> \"%d-2\"\n", i, i);
        printf("\"%d-3\"[label=\"%d\" shape=diamond]\n", i, node->ret);
        printf("\"%d-2\" -> \"%d-3\"\n", i, i);
        i+=sizeof(struct vm_vinteger_t)-1;
      } break;
      case VFLOAT: {
        struct vm_vfloat_t *node = (struct vm_vfloat_t *)&obj->bytecode[i];
        printf("\"%d-1\"[label=\"%d\" shape=square]\n", i, i);
        printf("\"%d-2\"[label=\"%g\"]\n", i, node->value);
        printf("\"%d-1\" -> \"%d-2\"\n", i, i);
        printf("\"%d-3\"[label=\"%d\" shape=diamond]\n", i, node->ret);
        printf("\"%d-2\" -> \"%d-3\"\n", i, i);
        i+=sizeof(struct vm_vfloat_t)-1;
      } break;
      case TOPERATOR: {
        struct vm_toperator_t *node = (struct vm_toperator_t *)&obj->bytecode[i];
        printf("\"%d-1\"[label=\"%d\" shape=square]\n", i, i);
        printf("\"%d-3\"[label=\"%d\" shape=square]\n", i, node->left);
        printf("\"%d-4\"[label=\"%d\" shape=square]\n", i, node->right);
        printf("\"%d-2\"[label=\"%s\"]\n", i, event_operators[node->token].name);
        printf("\"%d-1\" -> \"%d-2\"\n", i, i);
        printf("\"%d-5\"[label=\"%d\" shape=diamond]\n", i, node->ret);
        printf("\"%d-2\" -> \"%d-3\"\n", i, i);
        printf("\"%d-2\" -> \"%d-4\"\n", i, i);
        printf("\"%d-2\" -> \"%d-5\"\n", i, i);
        i+=sizeof(struct vm_toperator_t)-1;
      } break;
      default: {
      } break;
    }
  }
}

static void print_tree(struct rules_t *obj) {
  print_steps(obj);
}
#endif
/*LCOV_EXCL_STOP*/

static int vm_value_set(struct rules_t *obj, int step, int ret) {
  int out = obj->nrbytes;

  int start = alignedbytes(0);

  switch(obj->bytecode[step]) {
    case TNUMBER: {
      float var = 0;
      float nr = 0;
      struct vm_tnumber_t *node = (struct vm_tnumber_t *)&obj->bytecode[step];
      var = atof((char *)node->token);

      if((obj->bytecode = (unsigned char *)REALLOC(obj->bytecode, alignedbytes(obj->nrbytes)+sizeof(struct vm_vinteger_t))) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }

      struct vm_vinteger_t *value = (struct vm_vinteger_t *)&obj->bytecode[obj->nrbytes];
      value->type = VINTEGER;
      value->ret = ret;
      value->value = (int)var;
      obj->nrbytes = alignedbytes(obj->nrbytes) + sizeof(struct vm_vinteger_t);
    } break;
    case VFLOAT: {
      if((obj->bytecode = (unsigned char *)REALLOC(obj->bytecode, alignedbytes(obj->nrbytes)+sizeof(struct vm_vfloat_t))) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }

      struct vm_vfloat_t *value = (struct vm_vfloat_t *)&obj->bytecode[obj->nrbytes];
      struct vm_vfloat_t *cpy = (struct vm_vfloat_t *)&obj->bytecode[step];
      value->type = VFLOAT;
      value->ret = ret;
      value->value = cpy->value;
      obj->nrbytes = alignedbytes(obj->nrbytes) + sizeof(struct vm_vfloat_t);
    } break;
    case VINTEGER: {
      if((obj->bytecode = (unsigned char *)REALLOC(obj->bytecode, alignedbytes(obj->nrbytes)+sizeof(struct vm_vinteger_t))) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }

      struct vm_vinteger_t *value = (struct vm_vinteger_t *)&obj->bytecode[obj->nrbytes];
      struct vm_vinteger_t *cpy = (struct vm_vinteger_t *)&obj->bytecode[step];
      value->type = VINTEGER;
      value->ret = ret;
      value->value = cpy->value;
      obj->nrbytes = alignedbytes(obj->nrbytes) + sizeof(struct vm_vinteger_t);
    } break;
    case VNULL: {
      if((obj->bytecode = (unsigned char *)REALLOC(obj->bytecode, alignedbytes(obj->nrbytes)+sizeof(struct vm_vnull_t))) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }

      struct vm_vnull_t *value = (struct vm_vnull_t *)&obj->bytecode[obj->nrbytes];
      value->type = VNULL;
      value->ret = ret;
      obj->nrbytes = alignedbytes(obj->nrbytes) + sizeof(struct vm_vnull_t);
    } break;
    /* LCOV_EXCL_START*/
    default: {
      fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
      return -1;
    } break;
    /* LCOV_EXCL_STOP*/
  }

  return out;
}

static int vm_value_upd_pos(struct rules_t *obj, int val, int step) {
  switch(obj->bytecode[step]) {
    case TOPERATOR: {
      struct vm_toperator_t *node = (struct vm_toperator_t *)&obj->bytecode[step];
      node->value = val;
    } break;
    case TVAR: {
      struct vm_tvar_t *node = (struct vm_tvar_t *)&obj->bytecode[step];
      node->value = val;
    } break;
    case TFUNCTION: {
      struct vm_tfunction_t *node = (struct vm_tfunction_t *)&obj->bytecode[step];
      node->value = val;
    } break;
    case VNULL: {
    } break;
    /* LCOV_EXCL_START*/
    default: {
      fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
      return -1;
    } break;
    /* LCOV_EXCL_STOP*/
  }
  return 0;
}

static int vm_value_src(struct rules_t *obj, int *start, int type) {
  int x = 0;

  for(x=alignedbytes(*start);alignedbytes(x)<obj->nrbytes;x++) {
    x = alignedbytes(x);
    switch(obj->bytecode[x]) {
      case VINTEGER: {
        struct vm_vinteger_t *node = (struct vm_vinteger_t *)&obj->bytecode[x];
        if((obj->bytecode[node->ret]) == type) {
          (*start) = x + sizeof(struct vm_vinteger_t);
          return x;
        }
        x += sizeof(struct vm_vinteger_t)-1;
      } break;
      case VFLOAT: {
        struct vm_vfloat_t *node = (struct vm_vfloat_t *)&obj->bytecode[x];
        if((obj->bytecode[node->ret]) == type) {
          (*start) = x + sizeof(struct vm_vfloat_t);
          return x;
        }
        x += sizeof(struct vm_vfloat_t)-1;
      } break;
      /* LCOV_EXCL_START*/
      default: {
        fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
        return -1;
      } break;
      /* LCOV_EXCL_STOP*/
    }
  }
  return -1;
}

static int vm_value_clone(struct rules_t *obj, unsigned char *val) {
  int ret = obj->nrbytes;

  switch(val[0]) {
    case VINTEGER: {
      if((obj->bytecode = (unsigned char *)REALLOC(obj->bytecode, alignedbytes(obj->nrbytes)+sizeof(struct vm_vinteger_t))) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      struct vm_vinteger_t *cpy = (struct vm_vinteger_t *)&val[0];
      struct vm_vinteger_t *value = (struct vm_vinteger_t *)&obj->bytecode[obj->nrbytes];
      value->type = VINTEGER;
      value->ret = 0;
      value->value = (int)cpy->value;
      obj->nrbytes = alignedbytes(obj->nrbytes) + sizeof(struct vm_vinteger_t);
    } break;
    case VFLOAT: {
      if((obj->bytecode = (unsigned char *)REALLOC(obj->bytecode, alignedbytes(obj->nrbytes)+sizeof(struct vm_vfloat_t))) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      struct vm_vfloat_t *cpy = (struct vm_vfloat_t *)&val[0];
      struct vm_vfloat_t *value = (struct vm_vfloat_t *)&obj->bytecode[obj->nrbytes];
      value->type = VFLOAT;
      value->ret = 0;
      value->value = cpy->value;
      obj->nrbytes = alignedbytes(obj->nrbytes) + sizeof(struct vm_vfloat_t);
    } break;
    case VNULL: {
      if((obj->bytecode = (unsigned char *)REALLOC(obj->bytecode, alignedbytes(obj->nrbytes)+sizeof(struct vm_vnull_t))) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      struct vm_vnull_t *value = (struct vm_vnull_t *)&obj->bytecode[obj->nrbytes];
      value->type = VNULL;
      value->ret = 0;
      obj->nrbytes = alignedbytes(obj->nrbytes) + sizeof(struct vm_vnull_t);
    } break;
    /* LCOV_EXCL_START*/
    default: {
      fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
      return -1;
    } break;
    /* LCOV_EXCL_STOP*/
  }
  return ret;
}

static int vm_value_del(struct rules_t *obj, int idx) {
  int x = 0, ret = 0;

  if(idx == obj->nrbytes) {
    return -1;
  }

  switch(obj->bytecode[idx]) {
    case VINTEGER: {
      ret = sizeof(struct vm_vinteger_t);
      memmove(&obj->bytecode[idx], &obj->bytecode[idx+ret], obj->nrbytes-idx-ret);
      if((obj->bytecode = (unsigned char *)REALLOC(obj->bytecode, obj->nrbytes-ret)) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      obj->nrbytes -= ret;
    } break;
    case VFLOAT: {
      ret = sizeof(struct vm_vfloat_t);
      memmove(&obj->bytecode[idx], &obj->bytecode[idx+ret], obj->nrbytes-idx-ret);
      if((obj->bytecode = (unsigned char *)REALLOC(obj->bytecode, obj->nrbytes-ret)) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      obj->nrbytes -= ret;
    } break;
    case VNULL: {
      ret = sizeof(struct vm_vnull_t);
      memmove(&obj->bytecode[idx], &obj->bytecode[idx+ret], obj->nrbytes-idx-ret);
      if((obj->bytecode = (unsigned char *)REALLOC(obj->bytecode, obj->nrbytes-ret)) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      obj->nrbytes -= ret;
    } break;
    /* LCOV_EXCL_START*/
    default: {
      fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
      return -1;
    } break;
    /* LCOV_EXCL_STOP*/
  }

  /*
   * Values are linked back to their root node,
   * by their absolute position in the bytecode.
   * If a value is deleted, these positions changes,
   * so we need to update all nodes.
   */
  for(x=idx;alignedbytes(x)<obj->nrbytes;x++) {
    x = alignedbytes(x);
    switch(obj->bytecode[x]) {
      case VINTEGER: {
        struct vm_vinteger_t *node = (struct vm_vinteger_t *)&obj->bytecode[x];
        if(node->ret > 0) {
          vm_value_upd_pos(obj, x, node->ret);
        }
        x += sizeof(struct vm_vinteger_t)-1;
      } break;
      case VFLOAT: {
        struct vm_vfloat_t *node = (struct vm_vfloat_t *)&obj->bytecode[x];
        if(node->ret > 0) {
          vm_value_upd_pos(obj, x, node->ret);
        }
        x += sizeof(struct vm_vfloat_t)-1;
      } break;
      case VNULL: {
        struct vm_vnull_t *node = (struct vm_vnull_t *)&obj->bytecode[x];
        if(node->ret > 0) {
          vm_value_upd_pos(obj, x, node->ret);
        }
        x += sizeof(struct vm_vnull_t)-1;
      } break;
/* LCOV_EXCL_START*/
      default: {
        fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
        return -1;
      } break;
/* LCOV_EXCL_STOP*/
    }
  }
  return ret;
}

void valprint(struct rules_t *obj, char *out, int size) {
  int x = 0, pos = 0;
  memset(out, 0, size);
  /*
   * This is only used for debugging purposes
   */
  /*
  for(x=alignedbytes(obj->pos.vars);alignedbytes(x)<obj->nrbytes;x++) {
    if(alignedbytes(x) < obj->nrbytes) {
      x = alignedbytes(x);
      switch(obj->bytecode[x]) {
        case VINTEGER: {
          struct vm_vinteger_t *val = (struct vm_vinteger_t *)&obj->bytecode[x];
          switch(obj->bytecode[val->ret]) {
            case TVAR: {
              struct vm_tvar_t *node = (struct vm_tvar_t *)&obj->bytecode[val->ret];
              pos += snprintf(&out[pos], size - pos, "%s = %d", &obj->bytecode[node->token+1], val->value);
            } break;
            case TFUNCTION: {
              struct vm_tfunction_t *node = (struct vm_tfunction_t *)&obj->bytecode[val->ret];
              pos += snprintf(&out[pos], size - pos, "%s = %d", event_functions[obj->bytecode[node->token+1]].name, val->value);
            } break;
            case TOPERATOR: {
              struct vm_toperator_t *node = (struct vm_toperator_t *)&obj->bytecode[val->ret];
              pos += snprintf(&out[pos], size - pos, "%s = %d", event_operators[obj->bytecode[node->token+1]].name, val->value);
            } break;
            default: {
              // printf("err: %s %d %d\n", __FUNCTION__, __LINE__, obj->bytecode[val->ret]);
              // exit(-1);
            } break;
          }
          x += sizeof(struct vm_vinteger_t)-1;
        } break;
        case VFLOAT: {
          struct vm_vfloat_t *val = (struct vm_vfloat_t *)&obj->bytecode[x];
          switch(obj->bytecode[val->ret]) {
            case TVAR: {
              struct vm_tvar_t *node = (struct vm_tvar_t *)&obj->bytecode[val->ret];
              pos += snprintf(&out[pos], size - pos, "%s = %g", &obj->bytecode[node->token+1], val->value);
            } break;
            case TFUNCTION: {
              struct vm_tfunction_t *node = (struct vm_tfunction_t *)&obj->bytecode[val->ret];
              pos += snprintf(&out[pos], size - pos, "%s = %g", event_functions[obj->bytecode[node->token+1]].name, val->value);
            } break;
            case TOPERATOR: {
              struct vm_toperator_t *node = (struct vm_toperator_t *)&obj->bytecode[val->ret];
              pos += snprintf(&out[pos], size - pos, "%s = %g", event_operators[obj->bytecode[node->token+1]].name, val->value);
            } break;
            default: {
              // printf("err: %s %d\n", __FUNCTION__, __LINE__);
              // exit(-1);
            } break;
          }
          x += sizeof(struct vm_vfloat_t)-1;
        } break;
        case VNULL: {
          struct vm_vnull_t *val = (struct vm_vnull_t *)&obj->bytecode[x];
          switch(obj->bytecode[val->ret]) {
            case TVAR: {
              struct vm_tvar_t *node = (struct vm_tvar_t *)&obj->bytecode[val->ret];
              pos += snprintf(&out[pos], size - pos, "%s = NULL", &obj->bytecode[node->token+1]);
            } break;
            case TFUNCTION: {
              struct vm_tfunction_t *node = (struct vm_tfunction_t *)&obj->bytecode[val->ret];
              pos += snprintf(&out[pos], size - pos, "%s = NULL", event_functions[obj->bytecode[node->token+1]].name);
            } break;
            case TOPERATOR: {
              struct vm_toperator_t *node = (struct vm_toperator_t *)&obj->bytecode[val->ret];
              pos += snprintf(&out[pos], size - pos, "%s = NULL", event_operators[obj->bytecode[node->token+1]].name);
            } break;
            default: {
              // printf("err: %s %d\n", __FUNCTION__, __LINE__);
              // exit(-1);
            } break;
          }
          x += sizeof(struct vm_vnull_t)-1;
        } break;
        default: {
          fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
        } break;
      }
    }
  }
  */
  if(rule_options.prt_token_val_cb != NULL) {
    rule_options.prt_token_val_cb(obj, out, size);
  }
}

static void vm_clear_values(struct rules_t *obj) {
  int i = 0, x = 0;
  for(i=0;alignedbytes(i)<obj->nrbytes;i++) {

    i = alignedbytes(i);
    switch(obj->bytecode[i]) {
      case TSTART: {
        i+=sizeof(struct vm_tstart_t)-1;
      } break;
      case TEOF: {
        i+=sizeof(struct vm_teof_t)-1;
      } break;
      case VNULL: {
        i+=sizeof(struct vm_vnull_t)-1;
      } break;
      case TIF: {
        i+=sizeof(struct vm_tif_t)-1;
      } break;
      case LPAREN: {
        struct vm_lparen_t *node = (struct vm_lparen_t *)&obj->bytecode[i];
        node->value = 0;
        i+=sizeof(struct vm_lparen_t)-1;
      } break;
      case TFALSE:
      case TTRUE: {
        struct vm_ttrue_t *node = (struct vm_ttrue_t *)&obj->bytecode[i];
        i+=sizeof(struct vm_ttrue_t)+(sizeof(uint16_t)*node->nrgo)-1;
      } break;
      case TFUNCTION: {
        struct vm_tfunction_t *node = (struct vm_tfunction_t *)&obj->bytecode[i];
        node->value = 0;
        i+=sizeof(struct vm_tfunction_t)+(sizeof(uint16_t)*node->nrgo)-1;
      } break;
      case TCEVENT: {
        struct vm_tcevent_t *node = (struct vm_tcevent_t *)&obj->bytecode[i];
        i+=sizeof(struct vm_tcevent_t)+strlen((char *)node->token);
      } break;
      case TVAR: {
        struct vm_tvar_t *node = (struct vm_tvar_t *)&obj->bytecode[i];
        if(rule_options.clr_token_val_cb != NULL) {
          rule_options.clr_token_val_cb(obj, i);
        } else {
          node->value = 0;
        }
        i+=sizeof(struct vm_tvar_t)+strlen((char *)node->token);
      } break;
      case TEVENT: {
        struct vm_tevent_t *node = (struct vm_tevent_t *)&obj->bytecode[i];
        i += sizeof(struct vm_tevent_t)+strlen((char *)node->token);
      } break;
      case TNUMBER: {
        struct vm_tnumber_t *node = (struct vm_tnumber_t *)&obj->bytecode[i];
        i+=sizeof(struct vm_tnumber_t)+strlen((char *)node->token);
      } break;
      case VINTEGER: {
        struct vm_vinteger_t *node = (struct vm_vinteger_t *)&obj->bytecode[i];
        i+=sizeof(struct vm_vinteger_t)-1;
      } break;
      case VFLOAT: {
        struct vm_vfloat_t *node = (struct vm_vfloat_t *)&obj->bytecode[i];
        i+=sizeof(struct vm_vfloat_t)-1;
      } break;
      case TOPERATOR: {
        struct vm_toperator_t *node = (struct vm_toperator_t *)&obj->bytecode[i];
        node->value = 0;
        i+=sizeof(struct vm_toperator_t)-1;
      } break;
      default: {
      } break;
    }
  }
}

int rule_run(struct rules_t *obj, int validate) {
#ifdef DEBUG
  printf("----------\n");
  printf("%s %d\n", __FUNCTION__, obj->nr);
  printf("----------\n");
#endif

  int go = 0, ret = -1, i = -1, end = -1, start = -1;
  go = start = 0;

  while(go != -1) {
#ifdef ESP8266
    ESP.wdtFeed();
#endif

/*LCOV_EXCL_START*/
#ifdef DEBUG
    printf("goto: %d, ret: %d, bytes: %d\n", go, ret, obj->nrbytes);
    printf("bytecode is %d bytes\n", obj->nrbytes);
    {
      char out[1024];
      valprint(obj, (char *)&out, 1024);
      printf("%s\n", out);
    }
#endif
/*LCOV_EXCL_STOP*/

    switch(obj->bytecode[go]) {
      case TSTART: {
        struct vm_tstart_t *node = (struct vm_tstart_t *)&obj->bytecode[go];
        if(ret > -1) {
          go = -1;
        } else {
          end = node->ret;
          if(obj->cont.go > 0) {
            go = obj->cont.go;
            ret = obj->cont.ret;
            obj->cont.go = 0;
            obj->cont.ret = 0;
          } else {
            vm_clear_values(obj);
            go = node->go;
          }
        }
      } break;
      case TEVENT: {
        struct vm_tevent_t *node = (struct vm_tevent_t *)&obj->bytecode[go];
        if(node->go == ret) {
          ret = go;
          go = node->ret;
        } else {
          go = node->go;
          ret = go;
        }
      } break;
      case TIF: {
        struct vm_tif_t *node = (struct vm_tif_t *)&obj->bytecode[go];
        int idx = 0, val = -1;
        if(ret > -1) {
          switch(obj->bytecode[ret]) {
            case TOPERATOR: {
              struct vm_toperator_t *op = (struct vm_toperator_t *)&obj->bytecode[ret];
              struct vm_vinteger_t *tmp = (struct vm_vinteger_t *)&obj->bytecode[op->value];

              val = tmp->value;
              vm_value_del(obj, op->value);

              /*
               * Reassign node due to various (unsigned char *)REALLOC's
               */
              node = (struct vm_tif_t *)&obj->bytecode[go];
            } break;
            case LPAREN: {
              struct vm_lparen_t *op = (struct vm_lparen_t *)&obj->bytecode[ret];
              struct vm_vinteger_t *tmp = (struct vm_vinteger_t *)&obj->bytecode[op->value];

              val = tmp->value;
              vm_value_del(obj, op->value);

              /*
               * Reassign node due to various (unsigned char *)REALLOC's
               */
              node = (struct vm_tif_t *)&obj->bytecode[go];
            } break;
            case TTRUE:
            case TFALSE:
            case TIF:
            case TEVENT:
            break;
            /* LCOV_EXCL_START*/
            default: {
              fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
              return -1;
            } break;
            /* LCOV_EXCL_STOP*/
          }
        }

        if(node->false_ == ret && node->false_ > 0) {
          ret = go;
          go = node->ret;
        } else if(node->true_ == ret) {
          if(node->false_ != 0 && (val == 0 || validate == 1)) {
            go = node->false_;
            ret = go;
          } else {
            ret = go;
            go = node->ret;
          }
        } else if(node->go == ret) {
          if(node->false_ != 0 && val == 0 && validate == 0) {
            go = node->false_;
            ret = go;
          } else if(val == 1 || validate == 1) {
            ret = go;
            go = node->true_;
          } else {
            ret = go;
            go = node->ret;
          }
        } else {
          go = node->go;
        }
      } break;
      case TCEVENT: {
        struct vm_tcevent_t *node = (struct vm_tcevent_t *)&obj->bytecode[go];

        if(rule_options.event_cb == NULL) {
          /* LCOV_EXCL_START*/
          fprintf(stderr, "FATAL: No 'event_cb' set to handle events\n");
          return -1;
          /* LCOV_EXCL_STOP*/
        }

        obj->cont.ret = go;
        obj->cont.go = node->ret;

        /*
         * Tail recursive
         */
        return rule_options.event_cb(obj, (const char *)node->token);
      } break;
      case TFUNCTION: {
        struct vm_tfunction_t *node = (struct vm_tfunction_t *)&obj->bytecode[go];

        int match = 0, tmp = go;

        for(i=0;i<node->nrgo;i++) {
          if(node->go[i] == ret) {
            match = 1;
            if(i+1 < node->nrgo) {
              switch(obj->bytecode[node->go[i+1]]) {
                case TNUMBER:
                case VINTEGER:
                case VFLOAT: {
                  ret = go;
                  go = node->go[i+1];
                } break;
                case VNULL: {
                  ret = go;
                  go = node->go[i+1];
                } break;
                case LPAREN: {
                  ret = go;
                  go = node->go[i+1];
                } break;
                case TOPERATOR: {
                  ret = go;
                  go = node->go[i+1];
                } break;
                /* LCOV_EXCL_START*/
                default: {
                  fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
                  return -1;
                } break;
                /* LCOV_EXCL_STOP*/
              }
            } else {
              go = 0;
            }
            break;
          }
        }
        if(match == 0) {
          ret = go;
          go = node->go[0];
        }

        if(go == 0) {
          go = tmp;
          int idx = node->token, c = 0, i = 0, shift = 0;
          uint16_t values[node->nrgo];
          memset(&values, 0, node->nrgo);

          /* LCOV_EXCL_START*/
          if(idx > nr_event_functions) {
            fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
            return -1;
          }
          /* LCOV_EXCL_STOP*/

          for(i=0;i<node->nrgo;i++) {
            switch(obj->bytecode[node->go[i]]) {
              case TNUMBER:
              case VINTEGER:
              case VFLOAT: {
                values[i] = vm_value_set(obj, node->go[i], 0);

                /*
                 * Reassign node due to possible reallocs
                 */
                node = (struct vm_tfunction_t *)&obj->bytecode[go];
              } break;
              case LPAREN: {
                struct vm_lparen_t *tmp = (struct vm_lparen_t *)&obj->bytecode[node->go[i]];
                struct vm_tgeneric_t *val = (struct vm_tgeneric_t *)&obj->bytecode[tmp->value];
                values[i] = tmp->value;
                tmp->value = 0;
                val->ret = 0;
              } break;
              case TOPERATOR: {
                struct vm_toperator_t *tmp = (struct vm_toperator_t *)&obj->bytecode[node->go[i]];
                struct vm_tgeneric_t *val = (struct vm_tgeneric_t *)&obj->bytecode[tmp->value];
                values[i] = tmp->value;
                tmp->value = 0;
                val->ret = 0;
              } break;
              case TFUNCTION: {
                struct vm_tfunction_t *tmp = (struct vm_tfunction_t *)&obj->bytecode[node->go[i]];
                struct vm_tgeneric_t *val = (struct vm_tgeneric_t *)&obj->bytecode[tmp->value];
                values[i] = tmp->value;
                tmp->value = 0;
                val->ret = 0;
              } break;
              case VNULL: {
                values[i] = vm_value_set(obj, node->go[i], node->go[i]);
                /*
                * Reassign node due to possible reallocs
                */
                node = (struct vm_tfunction_t *)&obj->bytecode[go];
              } break;
              case TVAR: {
                if(rule_options.get_token_val_cb != NULL && rule_options.cpy_token_val_cb != NULL) {
                  rule_options.cpy_token_val_cb(obj, node->go[i]); // TESTME
                  unsigned char *val = rule_options.get_token_val_cb(obj, node->go[i]);
                  /* LCOV_EXCL_START*/
                  if(val == NULL) {
                    fprintf(stderr, "FATAL: 'get_token_val_cb' did not return a value\n");
                    return -1;
                  }
                  /* LCOV_EXCL_STOP*/
                  values[i] = vm_value_clone(obj, val);

                  /*
                   * Reassign node due to possible reallocs
                   */
                  node = (struct vm_tfunction_t *)&obj->bytecode[go];
                } else {
                  /* LCOV_EXCL_START*/
                  fprintf(stderr, "FATAL: No '[get|cpy]_token_val_cb' set to handle variables\n");
                  return -1;
                  /* LCOV_EXCL_STOP*/
                }
              } break;
              /* LCOV_EXCL_START*/
              default: {
                fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
                return -1;
              } break;
              /* LCOV_EXCL_STOP*/
            }
          }

          if(event_functions[idx].callback(obj, node->nrgo, values, &c) != 0) {
            /* LCOV_EXCL_START*/
            fprintf(stderr, "FATAL: function call '%s' failed\n", event_functions[idx].name);
            return -1;
            /* LCOV_EXCL_STOP*/
          }

          /*
           * Reassign node due to possible reallocs
           */
          node = (struct vm_tfunction_t *)&obj->bytecode[go];
          if(c > 0) {
            switch(obj->bytecode[c]) {
              case VINTEGER: {
                struct vm_vinteger_t *tmp = (struct vm_vinteger_t *)&obj->bytecode[c];
                tmp->ret = go;
                node->value = c;
              } break;
              case VNULL: {
                struct vm_vnull_t *tmp = (struct vm_vnull_t *)&obj->bytecode[c];
                tmp->ret = go;
                node->value = c;
              } break;
              case VFLOAT: {
                struct vm_vfloat_t *tmp = (struct vm_vfloat_t *)&obj->bytecode[c];
                tmp->ret = go;
                node->value = c;
              } break;
              /* LCOV_EXCL_START*/
              default: {
                fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
                return -1;
              } break;
              /* LCOV_EXCL_STOP*/
            }
          } else {
            node->value = 0;
          }

          for(i=0;i<node->nrgo;i++) {
            switch(obj->bytecode[values[i] - shift]) {
              case VFLOAT:
              case VNULL:
              case VINTEGER: {
                shift += vm_value_del(obj, values[i] - shift);

                /*
                 * Reassign node due to possible reallocs
                 */
                node = (struct vm_tfunction_t *)&obj->bytecode[go];
              } break;
              /* LCOV_EXCL_START*/
              default: {
                fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
                return -1;
              } break;
              /* LCOV_EXCL_STOP*/
            }
          }

          ret = go;
          go = node->ret;
        }
      } break;
      case TOPERATOR: {
        struct vm_toperator_t *node = (struct vm_toperator_t *)&obj->bytecode[go];
        int val = -1;

        if(node->right == ret ||
            (
              (
                obj->bytecode[node->left] == VFLOAT ||
                obj->bytecode[node->left] == VINTEGER ||
                obj->bytecode[node->left] == TNUMBER
              ) &&
              (
                obj->bytecode[node->right] == VFLOAT ||
                obj->bytecode[node->right] == VINTEGER ||
                obj->bytecode[node->right] == TNUMBER
              )
            )
           ) {
          int a = 0, b = 0, c = 0, step = 0, next = 0, out = 0;
          step = node->left;

          switch(obj->bytecode[step]) {
            case TNUMBER:
            case VFLOAT:
            case VINTEGER: {
              a = vm_value_set(obj, step, step);
              /*
               * Reassign node due to possible reallocs
               */
              node = (struct vm_toperator_t *)&obj->bytecode[go];
            } break;
            case TOPERATOR: {
              struct vm_toperator_t *tmp = (struct vm_toperator_t *)&obj->bytecode[step];
              a = tmp->value;
              tmp->value = 0;
            } break;
            case LPAREN: {
              struct vm_lparen_t *tmp = (struct vm_lparen_t *)&obj->bytecode[step];
              a = tmp->value;
              tmp->value = 0;
            } break;
            case TFUNCTION: {
              struct vm_tfunction_t *tmp = (struct vm_tfunction_t *)&obj->bytecode[step];
              a = tmp->value;
              tmp->value = 0;
            } break;
            case VNULL: {
              a = vm_value_set(obj, step, step);
              /*
               * Reassign node due to possible reallocs
               */
              node = (struct vm_toperator_t *)&obj->bytecode[go];
            } break;
            case TVAR: {
              /*
               * If vars are seperate steps
               */
              // struct vm_tvar_t *tmp = (struct vm_tvar_t *)&obj->bytecode[step];
              // a = tmp->value;

              if(rule_options.get_token_val_cb != NULL && rule_options.cpy_token_val_cb != NULL) {
                rule_options.cpy_token_val_cb(obj, step); // TESTME
                unsigned char *val = rule_options.get_token_val_cb(obj, step);
                /* LCOV_EXCL_START*/
                if(val == NULL) {
                  fprintf(stderr, "FATAL: 'get_token_val_cb' did not return a value\n");
                  return -1;
                }
                /* LCOV_EXCL_STOP*/
                a = vm_value_clone(obj, val);
                /*
                 * Reassign node due to possible reallocs
                 */
                node = (struct vm_toperator_t *)&obj->bytecode[go];
              } else {
                /* LCOV_EXCL_START*/
                fprintf(stderr, "FATAL: No '[get|cpy]_token_val_cb' set to handle variables\n");
                return -1;
                /* LCOV_EXCL_STOP*/
              }
            } break;
            /* LCOV_EXCL_START*/
            default: {
              fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
              return -1;
            } break;
            /* LCOV_EXCL_STOP*/
          }

          step = node->right;
          switch(obj->bytecode[step]) {
            case TNUMBER:
            case VFLOAT:
            case VINTEGER: {
              b = vm_value_set(obj, step, step);
              /*
               * Reassign node due to possible reallocs
               */
              node = (struct vm_toperator_t *)&obj->bytecode[go];
            } break;
            case TOPERATOR: {
              struct vm_toperator_t *tmp = (struct vm_toperator_t *)&obj->bytecode[step];
              b = tmp->value;
              tmp->value = 0;
            } break;
            case LPAREN: {
              struct vm_lparen_t *tmp = (struct vm_lparen_t *)&obj->bytecode[step];
              b = tmp->value;
              tmp->value = 0;
            } break;
            case TFUNCTION: {
              struct vm_tfunction_t *tmp = (struct vm_tfunction_t *)&obj->bytecode[step];
              b = tmp->value;
              tmp->value = 0;
            } break;
            case VNULL: {
              b = vm_value_set(obj, step, step);
              /*
               * Reassign node due to possible reallocs
               */
              node = (struct vm_toperator_t *)&obj->bytecode[go];
            } break;
            case TVAR: {
              /*
               * If vars are seperate steps
               */
              if(rule_options.get_token_val_cb != NULL && rule_options.cpy_token_val_cb != NULL) {
                rule_options.cpy_token_val_cb(obj, step); // TESTME
                unsigned char *val = rule_options.get_token_val_cb(obj, step);
                /* LCOV_EXCL_START*/
                if(val == NULL) {
                  fprintf(stderr, "FATAL: 'get_token_val_cb' did not return a value\n");
                  return -1;
                }
                /* LCOV_EXCL_STOP*/
                b = vm_value_clone(obj, val);
                /*
                 * Reassign node due to possible reallocs
                 */
                node = (struct vm_toperator_t *)&obj->bytecode[go];
              } else {
                /* LCOV_EXCL_START*/
                fprintf(stderr, "FATAL: No '[get|cpy]_token_val_cb' set to handle variables\n");
                return -1;
                /* LCOV_EXCL_STOP*/
              }
            } break;
            /* LCOV_EXCL_START*/
            default: {
              fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
              return -1;
            } break;
            /* LCOV_EXCL_STOP*/
          }
          int idx = node->token;

          /* LCOV_EXCL_START*/
          if(idx > nr_event_operators) {
            fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
            return -1;
          }
          /* LCOV_EXCL_STOP*/

          if(event_operators[idx].callback(obj, a, b, &c) != 0) {
            /* LCOV_EXCL_START*/
            fprintf(stderr, "FATAL: operator call '%s' failed\n", event_operators[idx].name);
            return -1;
            /* LCOV_EXCL_STOP*/
          }

          /*
           * Reassign node due to possible (unsigned char *)REALLOC's
           * in the callbacks
           */
          node = (struct vm_toperator_t *)&obj->bytecode[go];

          switch(obj->bytecode[c]) {
            case VINTEGER: {
              struct vm_vinteger_t *tmp = (struct vm_vinteger_t *)&obj->bytecode[c];
              /*
               * Reassign node due to possible reallocs
               */
              node = (struct vm_toperator_t *)&obj->bytecode[go];
              tmp->ret = go;
              node->value = c;
              out = tmp->value;
            } break;
            case VFLOAT: {
              struct vm_vfloat_t *tmp = (struct vm_vfloat_t *)&obj->bytecode[c];
              /*
               * Reassign node due to possible reallocs
               */
              node = (struct vm_toperator_t *)&obj->bytecode[go];
              tmp->ret = go;
              node->value = c;
              out = tmp->value;
            } break;
            case VNULL: {
              struct vm_vnull_t *tmp = (struct vm_vnull_t *)&obj->bytecode[c];
              /*
               * Reassign node due to possible reallocs
               */
              node = (struct vm_toperator_t *)&obj->bytecode[go];
              tmp->ret = go;
              node->value = c;
              out = 0;
            } break;
            /* LCOV_EXCL_START*/
            default: {
              fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
              return -1;
            } break;
            /* LCOV_EXCL_STOP*/
          }

          vm_value_del(obj, max(a, b));

          /*
           * Reassign node due to possible reallocs
           */
          node = (struct vm_toperator_t *)&obj->bytecode[go];

          vm_value_del(obj, min(a, b));

          /*
           * Reassign node due to possible reallocs
           */
          node = (struct vm_toperator_t *)&obj->bytecode[go];

          ret = go;
          go = node->ret;
        } else if(node->left == ret/* || node->left < obj->pos.parsed*/) {
          ret = go;
          go = node->right;
        } else {
          ret = go;
          go = node->left;
        }
      } break;
      case TNUMBER:
      case VFLOAT:
      case VINTEGER: {
        int tmp = ret;
        ret = go;
        go = tmp;
      } break;
      case TSTRING: {
        int tmp = ret;
        ret = go;
        go = tmp;
      } break;
      case VNULL: {
        int tmp = ret;
        ret = go;
        go = tmp;
      } break;
      case LPAREN: {
        struct vm_lparen_t *node = (struct vm_lparen_t *)&obj->bytecode[go];
        if(node->go == ret) {
          switch(obj->bytecode[node->go]) {
            case TOPERATOR: {
              struct vm_toperator_t *tmp = (struct vm_toperator_t *)&obj->bytecode[ret];
              node->value = tmp->value;
              tmp->value = 0;
            } break;
          }
          ret = go;
          go = node->ret;
        } else {
          ret = go;
          go = node->go;
        }
      } break;
      case TFALSE:
      case TTRUE: {
        struct vm_ttrue_t *node = (struct vm_ttrue_t *)&obj->bytecode[go];
        int idx = 0;
        switch(obj->bytecode[ret]) {
          case TVAR: {
          } break;
          // case TOPERATOR: {
            // struct vm_toperator_t *node = (struct vm_toperator_t *)&obj->bytecode[ret];
            // idx = node->value;
            // node->value = 0;
          // } break;
          case TFUNCTION: {
            struct vm_tfunction_t *tmp = (struct vm_tfunction_t *)&obj->bytecode[ret];
            if(tmp->value > 0) {
              vm_value_del(obj, tmp->value);
            }
            node = (struct vm_ttrue_t *)&obj->bytecode[go];
          } break;
          case TIF:
          case TEVENT:
          case TCEVENT:
          case TTRUE:
          case TFALSE: {
          } break;
          /* LCOV_EXCL_START*/
          default: {
            fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
            return -1;
          } break;
          /* LCOV_EXCL_STOP*/
        }

        // if((obj->bytecode[ret]) == TOPERATOR) {
          // vm_value_del(obj, idx);

          // /*
           // * Reassign node due to various (unsigned char *)REALLOC's
           // */
          // node = (struct vm_ttrue_t *)&obj->bytecode[go];
        // }

        int match = 0, tmp = go;

        for(i=0;i<node->nrgo;i++) {
          if(node->go[i] == ret) {
            match = 1;
            if(i+1 < node->nrgo) {
              ret = go;
              go = node->go[i+1];
            } else {
              go = 0;
            }
            break;
          }
        }
        if(match == 0) {
          ret = go;
          go = node->go[0];
        }

        if(go == 0) {
          go = node->ret;
          ret = tmp;
        }
      } break;
      case TVAR: {
        struct vm_tvar_t *node = (struct vm_tvar_t *)&obj->bytecode[go];

        if(node->go == 0) {
          if(rule_options.cpy_token_val_cb != NULL) {
            rule_options.cpy_token_val_cb(obj, go);
          }

          /*
           * Reassign node due to various (unsigned char *)REALLOC's
           */
          node = (struct vm_tvar_t *)&obj->bytecode[go];

          ret = go;
          go = node->ret;
        } else {
          /*
           * When we can find the value in the
           * prepared rule and not as a separate
           * node.
           */
          if(node->go == ret) {
            int idx = 0, start = end+1, shift = 0;

            switch(obj->bytecode[node->go]) {
              case TOPERATOR: {
                struct vm_toperator_t *tmp = (struct vm_toperator_t *)&obj->bytecode[ret];
                struct vm_tgeneric_t *val = (struct vm_tgeneric_t *)&obj->bytecode[tmp->value];
                idx = tmp->value;

                tmp->value = 0;
                val->ret = go;
              } break;
              case TFUNCTION: {
                struct vm_tfunction_t *tmp = (struct vm_tfunction_t *)&obj->bytecode[ret];
                struct vm_tgeneric_t *val = (struct vm_tgeneric_t *)&obj->bytecode[tmp->value];
                idx = tmp->value;
                tmp->value = 0;
                val->ret = go;
              } break;
              case TNUMBER:
              case VFLOAT:
              case VINTEGER: {
                idx = vm_value_set(obj, node->go, go);

                /*
                 * Reassign node due to various (unsigned char *)REALLOC's
                 */
                node = (struct vm_tvar_t *)&obj->bytecode[go];
              } break;
              case VNULL: {
                idx = vm_value_set(obj, node->go, go);
                /*
                 * Reassign node due to various (unsigned char *)REALLOC's
                 */
                node = (struct vm_tvar_t *)&obj->bytecode[go];
              } break;
              /*
               * Clone variable value to new variable
               */
              case TVAR: {
                if(rule_options.get_token_val_cb != NULL && rule_options.cpy_token_val_cb != NULL) {
                  rule_options.cpy_token_val_cb(obj, ret); // TESTME
                  unsigned char *val = rule_options.get_token_val_cb(obj, ret);
                  /* LCOV_EXCL_START*/
                  if(val == NULL) {
                    fprintf(stderr, "FATAL: 'get_token_val_cb' did not return a value\n");
                    return -1;
                  }
                  /* LCOV_EXCL_STOP*/
                  idx = vm_value_clone(obj, val);
                } else {
                  /* LCOV_EXCL_START*/
                  fprintf(stderr, "FATAL: No '[get|cpy]_token_val_cb' set to handle variables\n");
                  return -1;
                  /* LCOV_EXCL_STOP*/
                }
                /*
                 * Reassign node due to possible reallocs
                 */
                node = (struct vm_tvar_t *)&obj->bytecode[go];
              } break;
              case LPAREN: {
                struct vm_lparen_t *tmp = (struct vm_lparen_t *)&obj->bytecode[ret];
                struct vm_tgeneric_t *val = (struct vm_tgeneric_t *)&obj->bytecode[tmp->value - shift];
                idx = tmp->value - shift;
                tmp->value = 0;
                val->ret = go;
              } break;
              /* LCOV_EXCL_START*/
              default: {
                fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
                return -1;
              } break;
              /* LCOV_EXCL_STOP*/
            }

            if(idx > -1) {
              if(rule_options.set_token_val_cb == NULL) {
                /* LCOV_EXCL_START*/
                fprintf(stderr, "FATAL: No 'set_token_val_cb' set to handle variables\n");
                return -1;
                /* LCOV_EXCL_STOP*/
              }

              rule_options.set_token_val_cb(obj, go, idx);

              vm_value_del(obj, idx);
              /*
               * Reassign node due to various (unsigned char *)REALLOC's
               */
              node = (struct vm_tvar_t *)&obj->bytecode[go];
            } else {
              node->value = 0;
            }

            ret = go;
            go = node->ret;
          } else {
            ret = go;
            go = node->go;
          }
        }
      } break;
    }
  }

  /*
   * Tail recursive
   */
  if(obj->caller > 0) {
    return rule_options.event_cb(obj, NULL);
  }

  return 0;
}

/*LCOV_EXCL_START*/
#ifdef DEBUG
void print_bytecode(struct rules_t *obj) {
  int step = 0, i = 0, x = 0;

  for(i=0;i<obj->nrbytes;i++) {
    printf("%d", i);
    switch(obj->bytecode[i]) {
      case TIF: {
        struct vm_tif_t *node = (struct vm_tif_t *)&obj->bytecode[i];
        printf("(TIF)[9][");
        printf("type: %d, ", node->type);
        printf("ret: %d, ", node->ret);
        printf("go: %d, ", node->go);
        printf("true_: %d, ", node->true_);
        printf("false: %d]\n", node->false_);
        i += sizeof(struct vm_tif_t)-1;
      } break;
      case LPAREN: {
        struct vm_lparen_t *node = (struct vm_lparen_t *)&obj->bytecode[i];
        printf("(LPAREN)[7][");
        printf("type: %d, ", node->type);
        printf("ret: %d, ", node->ret);
        printf("go: %d, ", node->go);
        printf("value: %d]\n", node->value);
        i += sizeof(struct vm_lparen_t)-1;
      } break;
      case TVAR: {
        struct vm_tvar_t *node = (struct vm_tvar_t *)&obj->bytecode[i];
        printf("(TVAR)[%lu][", 7+strlen((char *)node->token)+1);
        printf("type: %d, ", node->type);
        printf("ret: %d, ", node->ret);
        printf("go: %d, ", node->go);
        printf("value: %d, ", node->value);
        printf("token: %s]\n", node->token);
        i += sizeof(struct vm_tvar_t)+strlen((char *)node->token);
      } break;
      case TEVENT: {
        struct vm_tevent_t *node = (struct vm_tevent_t *)&obj->bytecode[i];
        printf("(TEVENT)[%lu][", 7+strlen((char *)node->token)+1);
        printf("type: %d, ", node->type);
        printf("ret: %d, ", node->ret);
        printf("go: %d, ", node->go);
        printf("token: %s]\n", node->token);
        i += sizeof(struct vm_tevent_t)+strlen((char *)node->token);
      } break;
      case TCEVENT: {
        struct vm_tcevent_t *node = (struct vm_tcevent_t *)&obj->bytecode[i];
        printf("(TCEVENT)[%lu][", 3+strlen((char *)node->token)+1);
        printf("type: %d, ", node->type);
        printf("ret: %d, ", node->ret);
        printf("token: %s]\n", node->token);
        i += sizeof(struct vm_tcevent_t)+strlen((char *)node->token);
      } break;
      case TSTART: {
        struct vm_tstart_t *node = (struct vm_tstart_t *)&obj->bytecode[i];
        printf("(TSTART)[5][");
        printf("type: %d, ", node->type);
        printf("ret: %d, ", node->ret);
        printf("go: %d]\n", node->go);
        i += sizeof(struct vm_tstart_t)-1;
      } break;
      /* LCOV_EXCL_START*/
      case TNUMBER: {
        struct vm_tnumber_t *node = (struct vm_tnumber_t *)&obj->bytecode[i];
        printf("(TNUMBER)[%lu][", 3+strlen((char *)node->token)+1);
        printf("type: %d, ", node->type);
        printf("ret: %d, ", node->ret);
        printf("token: %s]\n", node->token);
        i += sizeof(struct vm_tnumber_t)+strlen((char *)node->token);
      } break;
      /* LCOV_EXCL_STOP*/
      case VINTEGER: {
        struct vm_vinteger_t *node = (struct vm_vinteger_t *)&obj->bytecode[i];
        printf("(VINTEGER)[%lu][", sizeof(struct vm_vinteger_t));
        printf("type: %d, ", node->type);
        printf("ret: %d, ", node->ret);
        printf("value: %d]\n", node->value);
        i += sizeof(struct vm_vinteger_t)-1;
      } break;
      case VFLOAT: {
        struct vm_vfloat_t *node = (struct vm_vfloat_t *)&obj->bytecode[i];
        printf("(VFLOAT)[%lu][", sizeof(struct vm_vfloat_t));
        printf("type: %d, ", node->type);
        printf("ret: %d, ", node->ret);
        printf("value: %g]\n", node->value);
        i += sizeof(struct vm_vfloat_t)-1;
      } break;
      case TFALSE:
      case TTRUE: {
        struct vm_ttrue_t *node = (struct vm_ttrue_t *)&obj->bytecode[i];
        printf("(TTRUE)[%lu][", 4+(node->nrgo*sizeof(node->go[0])));
        printf("type: %d, ", node->type);
        printf("ret: %d, ", node->ret);
        printf("nrgo: %d, ", node->nrgo);
        printf("go[");
        int x = 0;
        for(x=0;x<node->nrgo;x++) {
          printf("%d: %d", x, node->go[x]);
          if(node->nrgo-1 > x) {
            printf(", ");
          }
        }
        printf("]]\n");
        i += sizeof(struct vm_ttrue_t)+(sizeof(node->go[0])*node->nrgo)-1;
      } break;
      case TFUNCTION: {
        struct vm_tfunction_t *node = (struct vm_tfunction_t *)&obj->bytecode[i];
        printf("(TFUNCTION)[%lu][", 4+(node->nrgo*sizeof(node->go[0])));
        printf("type: %d, ", node->type);
        printf("ret: %d, ", node->ret);
        printf("token: %d, ", node->token);
        printf("value: %d, ", node->value);
        printf("nrgo: %d, ", node->nrgo);
        printf("go[");
        int x = 0;
        for(x=0;x<node->nrgo;x++) {
          printf("%d: %d", x, node->go[x]);
          if(node->nrgo-1 > x) {
            printf(", ");
          }
        }
        printf("]]\n");
        i += sizeof(struct vm_tfunction_t)+(sizeof(node->go[0])*node->nrgo)-1;
      } break;
      case TOPERATOR: {
        struct vm_toperator_t *node = (struct vm_toperator_t *)&obj->bytecode[i];
        printf("(TOPERATOR)[10][");
        printf("type: %d, ", node->type);
        printf("ret: %d, ", node->ret);
        printf("token: %d, ", node->token);
        printf("left: %d, ", node->left);
        printf("right: %d, ", node->right);
        printf("value: %d]\n", node->value);
        i += sizeof(struct vm_toperator_t)-1;
      } break;
      case TEOF: {
        struct vm_teof_t *node = (struct vm_teof_t *)&obj->bytecode[i];
        printf("(TEOF)[1][type: %d]\n", node->type);
        i += sizeof(struct vm_teof_t)-1;
      } break;
      case VNULL: {
        struct vm_vnull_t *node = (struct vm_vnull_t *)&obj->bytecode[i];
        printf("(VNULL)[3][");
        printf("type: %d,", node->type);
        printf("ret: %d]\n", node->ret);
        i += sizeof(struct vm_vnull_t)-1;
      } break;
    }
  }
}
#endif
/*LCOV_EXCL_STOP*/

int rule_initialize(const char *text, int *pos, struct rules_t ***rules, int *nrrules, void *userdata) {
  int len = strlen(text);
  if(*pos >= len) {
    return 1;
  }

  *rules = (struct rules_t **)REALLOC(*rules, sizeof(struct rules_t *)*((*nrrules)+1));
  if(*rules == NULL) {
    OUT_OF_MEMORY
  }
  if(((*rules)[*nrrules] = (struct rules_t *)MALLOC(sizeof(struct rules_t))) == NULL) {
    OUT_OF_MEMORY
  }
  memset((*rules)[*nrrules], 0, sizeof(struct rules_t));
  (*rules)[*nrrules]->userdata = userdata;

  struct rules_t *obj = (*rules)[*nrrules];
  obj->nr = (*nrrules)+1;
  (*nrrules)++;

  obj->nrbytes = 0;
/*LCOV_EXCL_START*/
#if defined(DEBUG) or defined(ESP8266)
  #ifdef ESP8266
    obj->timestamp.first = micros();
  #else
    clock_gettime(CLOCK_MONOTONIC, &obj->timestamp.first);
  #endif
#endif
/*LCOV_EXCL_STOP*/

  if(rule_prepare(text, pos) == -1) {
    return -1;
  }

/*LCOV_EXCL_START*/
#if defined(DEBUG) or defined(ESP8266)
  #ifdef ESP8266
  obj->timestamp.second = micros();

  memset(&out, 0, 1024);
  snprintf((char *)&out, 1024, "rule #%d was prepared in %d microseconds", obj->nr, obj->timestamp.second - obj->timestamp.first);
  Serial.println(out);
  #else
  clock_gettime(CLOCK_MONOTONIC, &obj->timestamp.second);

  printf("rule #%d was prepared in %.6f seconds\n", obj->nr,
    ((double)obj->timestamp.second.tv_sec + 1.0e-9*obj->timestamp.second.tv_nsec) -
    ((double)obj->timestamp.first.tv_sec + 1.0e-9*obj->timestamp.first.tv_nsec));
  #endif
#endif
/*LCOV_EXCL_STOP*/


/*LCOV_EXCL_START*/
#if defined(DEBUG) or defined(ESP8266)
  #ifdef ESP8266
  obj->timestamp.first = micros();
  #else
  clock_gettime(CLOCK_MONOTONIC, &obj->timestamp.first);
  #endif
#endif
/*LCOV_EXCL_STOP*/

  if(rule_parse(obj) == -1) {
    FREE(bytecode);
    bytecode = NULL;
    nrbytes = 0;
    return -1;
  }

  FREE(bytecode);
  bytecode = NULL;
  nrbytes = 0;

/*LCOV_EXCL_START*/
#if defined(DEBUG) or defined(ESP8266)
  #ifdef ESP8266
  obj->timestamp.second = micros();

  memset(&out, 0, 1024);
  snprintf((char *)&out, 1024, "rule #%d was parsed in %d microseconds", obj->nr, obj->timestamp.second - obj->timestamp.first);
  Serial.println(out);

  memset(&out, 0, 1024);
  snprintf((char *)&out, 1024, "bytecode is %d bytes", obj->nrbytes);
  Serial.println(out);
  #else
  clock_gettime(CLOCK_MONOTONIC, &obj->timestamp.second);

  printf("rule #%d was parsed in %.6f seconds\n", obj->nr,
    ((double)obj->timestamp.second.tv_sec + 1.0e-9*obj->timestamp.second.tv_nsec) -
    ((double)obj->timestamp.first.tv_sec + 1.0e-9*obj->timestamp.first.tv_nsec));

  printf("bytecode is %d bytes\n", obj->nrbytes);
  #endif
#endif
/*LCOV_EXCL_STOP*/

/*LCOV_EXCL_START*/
#ifdef DEBUG
  #ifndef ESP8266
  print_bytecode(obj);
  printf("\n");
  print_tree(obj);
  #endif
#endif
/*LCOV_EXCL_STOP*/

/*LCOV_EXCL_START*/
#if defined(DEBUG) or defined(ESP8266)
  #ifdef ESP8266
    obj->timestamp.first = micros();
  #else
    clock_gettime(CLOCK_MONOTONIC, &obj->timestamp.first);
  #endif
#endif
/*LCOV_EXCL_STOP*/

  if(rule_run(obj, 1) == -1) {
    return -1;
  }

/*LCOV_EXCL_START*/
#if defined(DEBUG) or defined(ESP8266)
  #ifdef ESP8266
    obj->timestamp.second = micros();

    memset(&out, 0, 1024);
    snprintf((char *)&out, 1024, "rule #%d was executed in %d microseconds", obj->nr, obj->timestamp.second - obj->timestamp.first);
    Serial.println(out);

    memset(&out, 0, 1024);
    snprintf((char *)&out, 1024, "bytecode is %d bytes", obj->nrbytes);
    Serial.println(out);
  #else
    clock_gettime(CLOCK_MONOTONIC, &obj->timestamp.second);

    printf("rule #%d was executed in %.6f seconds\n", obj->nr,
      ((double)obj->timestamp.second.tv_sec + 1.0e-9*obj->timestamp.second.tv_nsec) -
      ((double)obj->timestamp.first.tv_sec + 1.0e-9*obj->timestamp.first.tv_nsec));

    printf("bytecode is %d bytes\n", obj->nrbytes);
  #endif
#endif
/*LCOV_EXCL_STOP*/

  return 0;
}
