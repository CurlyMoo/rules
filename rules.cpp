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
  uint16_t end;
} **vmcache;

static unsigned int nrcache = 0;

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
  for(i=0;i<nrcache;i++) {
    FREE(vmcache[i]);
    vmcache[i] = NULL;
  }
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

static int is_function(struct rules_t *obj, const char *text, int *pos, int size) {
  int i = 0, len = 0;
  for(i=0;i<nr_event_functions;i++) {
    len = strlen(event_functions[i].name);
    if(size == len && strnicmp(&text[*pos], event_functions[i].name, len) == 0) {
      return i;
    }
  }

  return -1;
}

static int is_operator(struct rules_t *obj, const char *text, int *pos, int size) {
  int i = 0, len = 0;
  for(i=0;i<nr_event_operators;i++) {
    len = strlen(event_operators[i].name);
    if(size == len && strnicmp(&text[*pos], event_operators[i].name, len) == 0) {
      return i;
    }
  }

  return -1;
}

static int is_variable(struct rules_t *obj, const char *text, int *pos, int size) {
  int i = 1;
  if(text[*pos] == '$' || text[*pos] == '_' || text[*pos] == '@') {
    while(isalpha(text[*pos+i])) {
      i++;
    }
    return i;
  }
  return -1;
}

static int lexer_parse_number(struct rules_t *obj, const char *text, int *pos) {

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

    if((obj->bytecode = (unsigned char *)REALLOC(obj->bytecode, obj->nrbytes+(*pos - start)+1)) == NULL) {
      OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
    }
    memcpy(&obj->bytecode[obj->nrbytes], &text[start], *pos - start);
    obj->nrbytes += (*pos - start);
    obj->bytecode[obj->nrbytes++] = 0;
    return 0;
  } else {
    return -1;
  }
}

static int lexer_parse_string(struct rules_t *obj, const char *text, int *pos) {
  int len = strlen(text), start = *pos;

  while(*pos <= len &&
        text[*pos] != ' ' &&
        text[*pos] != ',' &&
        text[*pos] != ';' &&
        text[*pos] != '(' &&
        text[*pos] != ')') {
    (*pos)++;
  }
  if((obj->bytecode = (unsigned char *)REALLOC(obj->bytecode, obj->nrbytes+(*pos - start)+1)) == NULL) {
    OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
  }
  memcpy(&obj->bytecode[obj->nrbytes], &text[start], *pos - start);
  obj->nrbytes += (*pos - start);
  obj->bytecode[obj->nrbytes++] = 0;
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

static int rule_prepare(const char *text, int *pos, struct rules_t *obj) {
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
      if((obj->bytecode = (unsigned char *)REALLOC(obj->bytecode, obj->nrbytes+1)) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      obj->bytecode[obj->nrbytes++] = TNUMBER;
      ret = lexer_parse_number(obj, text, pos);
    } else if(strnicmp((char *)&text[*pos], "if", 2) == 0) {
      if((obj->bytecode = (unsigned char *)REALLOC(obj->bytecode, obj->nrbytes+1)) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      obj->bytecode[obj->nrbytes++] = TIF;
      (*pos)+=2;
      nrblocks++;
    } else if(strnicmp(&text[*pos], "on", 2) == 0) {
      if((obj->bytecode = (unsigned char *)REALLOC(obj->bytecode, obj->nrbytes+1)) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      obj->bytecode[obj->nrbytes++] = TEVENT;
      *pos+=2;
      lexer_parse_skip_characters(text, len, pos);
      ret = lexer_parse_string(obj, text, pos);
      nrblocks++;
    } else if(strnicmp((char *)&text[*pos], "else", 4) == 0) {
      if((obj->bytecode = (unsigned char *)REALLOC(obj->bytecode, obj->nrbytes+1)) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      obj->bytecode[obj->nrbytes++] = TELSE;
      (*pos)+=4;
    } else if(strnicmp((char *)&text[*pos], "then", 4) == 0) {
      if((obj->bytecode = (unsigned char *)REALLOC(obj->bytecode, obj->nrbytes+1)) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      obj->bytecode[obj->nrbytes++] = TTHEN;
      (*pos)+=4;
    } else if(strnicmp((char *)&text[*pos], "end", 3) == 0) {
      if((obj->bytecode = (unsigned char *)REALLOC(obj->bytecode, obj->nrbytes+1)) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      obj->bytecode[obj->nrbytes++] = TEND;
      (*pos)+=3;
      nrblocks--;
    } else if(strnicmp((char *)&text[*pos], "NULL", 4) == 0) {
      if((obj->bytecode = (unsigned char *)REALLOC(obj->bytecode, obj->nrbytes+1)) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      obj->bytecode[obj->nrbytes++] = VNULL;
      (*pos)+=4;
    } else if(text[*pos] == ',') {
      if((obj->bytecode = (unsigned char *)REALLOC(obj->bytecode, obj->nrbytes+1)) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      obj->bytecode[obj->nrbytes++] = TCOMMA;
      (*pos)++;
    } else if(text[*pos] == '(') {
      if((obj->bytecode = (unsigned char *)REALLOC(obj->bytecode, obj->nrbytes+1)) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      obj->bytecode[obj->nrbytes++] = LPAREN;
      (*pos)++;
    } else if(text[*pos] == ')') {
      if((obj->bytecode = (unsigned char *)REALLOC(obj->bytecode, obj->nrbytes+1)) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      obj->bytecode[obj->nrbytes++] = RPAREN;
      (*pos)++;
    } else if(text[*pos] == '=' && *pos < len && text[(*pos)+1] != '=') {
      if((obj->bytecode = (unsigned char *)REALLOC(obj->bytecode, obj->nrbytes+1)) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      obj->bytecode[obj->nrbytes++] = TASSIGN;
      (*pos)++;
    } else if(text[*pos] == ';') {
      if((obj->bytecode = (unsigned char *)REALLOC(obj->bytecode, obj->nrbytes+1)) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      obj->bytecode[obj->nrbytes++] = TSEMICOLON;
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

      if((len1 = is_function(obj, text, pos, b)) > -1) {
        if((obj->bytecode = (unsigned char *)REALLOC(obj->bytecode, obj->nrbytes+2)) == NULL) {
          OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
        }
        obj->bytecode[obj->nrbytes++] = TFUNCTION;
        obj->bytecode[obj->nrbytes++] = len1;
        *pos += b;
      } else if((len1 = is_operator(obj, text, pos, b)) > -1) {
        if((obj->bytecode = (unsigned char *)REALLOC(obj->bytecode, obj->nrbytes+2)) == NULL) {
          OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
        }
        obj->bytecode[obj->nrbytes++] = TOPERATOR;
        obj->bytecode[obj->nrbytes++] = len1;
        *pos += b;
      } else if(rule_options.is_token_cb != NULL && (len1 = rule_options.is_token_cb(obj, text, pos, b)) > -1) {
        if((obj->bytecode = (unsigned char *)REALLOC(obj->bytecode, obj->nrbytes+len1+2)) == NULL) {
          OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
        }
        obj->bytecode[obj->nrbytes++] = TVAR;
        memcpy(&obj->bytecode[obj->nrbytes], &text[*pos], len1);
        obj->nrbytes += len1;
        obj->bytecode[obj->nrbytes++] = 0;
        *pos += len1;
      } else if(rule_options.is_event_cb != NULL && (len1 = rule_options.is_event_cb(obj, text, pos, b)) > -1) {
        if((*pos)+b < len && text[(*pos)+b] == '(' && (*pos)+b+1 < len && text[(*pos)+b+1] == ')') {
          if((obj->bytecode = (unsigned char *)REALLOC(obj->bytecode, obj->nrbytes+2)) == NULL) {
            OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
          }
          obj->bytecode[obj->nrbytes++] = TCEVENT;
          lexer_parse_skip_characters(text, len, pos);
          ret = lexer_parse_string(obj, text, pos);
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

  if((obj->bytecode = (unsigned char *)REALLOC(obj->bytecode, obj->nrbytes+2)) == NULL) {
    OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
  }
  obj->bytecode[obj->nrbytes++] = 0;
  obj->bytecode[obj->nrbytes++] = 0;

  obj->pos.parsed = alignedbytes(obj->nrbytes);

#ifdef DEBUG
  printf("last parse byte: %d\n", obj->pos.parsed); /*LCOV_EXCL_LINE*/
#endif
  return ret;
}

static int lexer_bytecode_pos(struct rules_t *obj, int skip, int *pos) {
  int nr = 0, i = 0;
  for(i=0;i<obj->nrbytes;i++) {
    if(obj->bytecode[i] == TIF || obj->bytecode[i] == TTHEN ||
       obj->bytecode[i] == TEND || obj->bytecode[i] == TASSIGN ||
       obj->bytecode[i] == LPAREN || obj->bytecode[i] == RPAREN ||
       obj->bytecode[i] == TELSE || obj->bytecode[i] == TSEMICOLON ||
       obj->bytecode[i] == TCOMMA || obj->bytecode[i] == VNULL ||
       obj->bytecode[i] == TEND) {
      *pos = i;
      nr++;
    } else if(obj->bytecode[i] == TFUNCTION) {
      *pos = i;
      i++;
      nr++;
    } else if(obj->bytecode[i] == TOPERATOR) {
      *pos = i;
      i++;
      nr++;
    } else if(obj->bytecode[i] == TVAR ||
              obj->bytecode[i] == TSTRING ||
              obj->bytecode[i] == TNUMBER ||
              obj->bytecode[i] == TCEVENT ||
              obj->bytecode[i] == TEVENT) {
      *pos = i;
      while(obj->bytecode[++i] != 0);
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
    fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
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
    /*LCOV_EXCL_START*/
    case TNUMBER: {
      size = alignedbytes(ret+sizeof(struct vm_tnumber_t));
      if((obj->bytecode = (unsigned char *)REALLOC(obj->bytecode, size)) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      memset(&obj->bytecode[ret], 0, sizeof(struct vm_tnumber_t));

      lexer_bytecode_pos(obj, val, &pos);

      struct vm_tnumber_t *node = (struct vm_tnumber_t *)&obj->bytecode[ret];
      node->type = type;
      node->ret = 0;
      node->token = pos;

      obj->nrbytes = size;
    } break;
    /*LCOV_EXCL_STOP*/
    case TFALSE:
    case TTRUE: {
      size = alignedbytes(ret+sizeof(struct vm_ttrue_t)+(sizeof(uint16_t)*val)+1);
      if((obj->bytecode = (unsigned char *)REALLOC(obj->bytecode, size)) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      memset(&obj->bytecode[ret], 0, sizeof(struct vm_ttrue_t)+(sizeof(uint16_t)*val)+1);

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
      size = alignedbytes(ret+sizeof(struct vm_tfunction_t)+(sizeof(uint16_t)*val)+1);
      if((obj->bytecode = (unsigned char *)REALLOC(obj->bytecode, size)) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      memset(&obj->bytecode[ret], 0, sizeof(struct vm_tfunction_t)+(sizeof(uint16_t)*val)+1);

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
      size = alignedbytes(ret+sizeof(struct vm_tcevent_t)+(sizeof(uint16_t)*val)+1);
      if((obj->bytecode = (unsigned char *)REALLOC(obj->bytecode, size)) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      memset(&obj->bytecode[ret], 0, sizeof(struct vm_tcevent_t)+(sizeof(uint16_t)*val)+1);

      lexer_bytecode_pos(obj, val, &pos);

      struct vm_tcevent_t *node = (struct vm_tcevent_t *)&obj->bytecode[ret];
      node->token = pos;
      node->type = type;
      node->ret = 0;

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
      size = alignedbytes(ret+sizeof(struct vm_tvar_t));
      if((obj->bytecode = (unsigned char *)REALLOC(obj->bytecode, size)) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      memset(&obj->bytecode[ret], 0, sizeof(struct vm_tvar_t));

      lexer_bytecode_pos(obj, val, &pos);

      struct vm_tvar_t *node = (struct vm_tvar_t *)&obj->bytecode[ret];
      node->type = type;
      node->ret = 0;
      node->token = pos;
      node->go = 0;
      node->value = 0;

      obj->nrbytes = size;
    } break;
    case TEVENT: {
      size = alignedbytes(ret+sizeof(struct vm_tevent_t));
      if((obj->bytecode = (unsigned char *)REALLOC(obj->bytecode, size)) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      memset(&obj->bytecode[ret], 0, sizeof(struct vm_tevent_t));

      lexer_bytecode_pos(obj, val, &pos);

      struct vm_tevent_t *node = (struct vm_tevent_t *)&obj->bytecode[ret];
      node->type = type;
      node->ret = 0;
      node->token = pos;
      node->go = 0;

      obj->nrbytes = size;
    } break;
    case TOPERATOR: {
      size = alignedbytes(ret+sizeof(struct vm_toperator_t));
      if((obj->bytecode = (unsigned char *)REALLOC(obj->bytecode, size)) == NULL) {
        OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
      }
      memset(&obj->bytecode[ret], 0, sizeof(struct vm_toperator_t));

      lexer_bytecode_pos(obj, val, &pos);

      struct vm_toperator_t *node = (struct vm_toperator_t *)&obj->bytecode[ret];

      node->type = type;
      node->ret = 0;
      node->token = pos;
      node->left = 0;
      node->right = 0;
      node->value = 0;

      obj->nrbytes = size;
    } break;
  }
  return ret;
}

static int lexer_peek(struct rules_t *obj, int skip, int *type) {
  int i = 0, nr = 0, tmp = 0;
  for(i=0;i<obj->nrbytes;i++) {
    if(obj->bytecode[i] == TIF || obj->bytecode[i] == TTHEN ||
       obj->bytecode[i] == TEND || obj->bytecode[i] == TASSIGN ||
       obj->bytecode[i] == LPAREN || obj->bytecode[i] == RPAREN ||
       obj->bytecode[i] == TELSE || obj->bytecode[i] == TSEMICOLON ||
       obj->bytecode[i] == TCOMMA || obj->bytecode[i] == VNULL ||
       obj->bytecode[i] == TEND) {
      tmp = obj->bytecode[i];
    } else if(obj->bytecode[i] == TOPERATOR) {
      tmp = obj->bytecode[i];
      i++;
    } else if(obj->bytecode[i] == TFUNCTION) {
      tmp = obj->bytecode[i];
      i++;
    } else if(obj->bytecode[i] == TVAR ||
              obj->bytecode[i] == TNUMBER ||
              obj->bytecode[i] == TSTRING ||
              obj->bytecode[i] == TCEVENT ||
              obj->bytecode[i] == TEVENT) {
      tmp = obj->bytecode[i];
      while(obj->bytecode[i] != 0) {
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

static void vm_cache_add(int type, int step, int start, int end) {
  if((vmcache = (struct vm_cache_t **)REALLOC(vmcache, sizeof(struct vm_cache_t)*((nrcache)+1))) == NULL) {
    OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
  }
  if((vmcache[nrcache] = (struct vm_cache_t *)MALLOC(sizeof(struct vm_cache_t))) == NULL) {
    OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
  }
  vmcache[nrcache]->type = type;
  vmcache[nrcache]->step = step;
  vmcache[nrcache]->start = start;
  vmcache[nrcache]->end = end;
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
    if((vmcache = (struct vm_cache_t **)REALLOC(vmcache, sizeof(struct vm_cache_t)*nrcache)) == NULL) {
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

static int lexer_parse_math_order(struct rules_t *obj, int type, int *pos, int *step_out, int source) {
  int b = 0, c = 0, step = 0, val = 0, first = 1;

  while(1) {
    int right = 0;
    if(lexer_peek(obj, (*pos)+1, &b) < 0 || b != TOPERATOR) {
      break;
    }
    step = vm_parent(obj, b, (*pos)+1);

    if(lexer_peek(obj, (*pos)+2, &c) == 0) {
      switch(c) {
        case LPAREN: {
          int oldpos = (*pos)+2;
          struct vm_cache_t *x = vm_cache_get(LPAREN, -1, (*pos) + 2);
          /* LCOV_EXCL_START*/
          if(x == NULL) {
            fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
            return -1;
          }
          /* LCOV_EXCL_STOP*/
          (*pos) = x->end;
          right = x->step;
          vm_cache_del(oldpos);
        } break;
        case TFUNCTION: {
          int oldpos = (*pos)+2;
          struct vm_cache_t *x = vm_cache_get(TFUNCTION, -1, (*pos) + 2);
          /* LCOV_EXCL_START*/
          if(x == NULL) {
            fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
            return -1;
          }
          /* LCOV_EXCL_STOP*/
          (*pos) = x->end;
          right = x->step;
          vm_cache_del(oldpos);
        } break;
        case TVAR: {
          right = vm_parent(obj, c, (*pos)+2);
          (*pos) += 2;
        } break;
        case TNUMBER: {
          lexer_bytecode_pos(obj, (*pos) + 2, &val);
          right = val;
          (*pos) += 2;
        } break;
        case VNULL: {
          right = vm_parent(obj, c, (*pos)+2);
          (*pos) += 2;
        } break;
        default: {
          int val = 0;
          lexer_bytecode_pos(obj, (*pos)+1, &val);
          fprintf(stderr, "ERROR: Expected an parenthesis block, function, number or variable at position #%d\n", val+1);
          return -1;
        } break;
      }
    }

    if(right > obj->pos.parsed) {
      struct vm_tgeneric_t *node = (struct vm_tgeneric_t *)&obj->bytecode[right];
      node->ret = step;
    }

    struct vm_toperator_t *op2 = (struct vm_toperator_t *)&obj->bytecode[step];
    op2->left = *step_out;
    op2->right = right;

    if(*step_out > obj->pos.parsed) {
      switch(obj->bytecode[*step_out]) {
        /*
         * Number are not seperate nodes so
         * this block is never triggered.
         */
        /*
        case TNUMBER: {
          struct vm_tnumber_t *node = (struct vm_tnumber_t *)&obj->bytecode[*step_out];
          node->ret = step;
        } break;
        */
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
    }

    struct vm_toperator_t *op1 = NULL;

    if(type == LPAREN) {
      if(first == 1 && *step_out > obj->pos.parsed) {
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

    if(*step_out > obj->pos.parsed && (obj->bytecode[step]) == TOPERATOR && (obj->bytecode[*step_out]) == TOPERATOR) {
      struct vm_toperator_t *op3 = NULL;
      op1 = (struct vm_toperator_t *)&obj->bytecode[*step_out];

      int idx1 = obj->bytecode[op1->token+1];
      int idx2 = obj->bytecode[op2->token+1];

      int x = event_operators[idx1].precedence;
      int y = event_operators[idx2].precedence;
      int a = event_operators[idx2].associativity;

      if(y > x || (x == y && a == 2)) {
        if(a == 1) {
          op2->left = op1->right;
          if(op2->left > obj->pos.parsed) {
            op3 = (struct vm_toperator_t *)&obj->bytecode[op2->left];
            op3->ret = step;
          }
          op1->right = step;
          op2->ret = *step_out;
        } else {
          /*
           * Find the last operator with an operator
           * as the last factor.
           */
          int tmp = op1->right;
          if((obj->bytecode[tmp]) != LPAREN && (obj->bytecode[tmp]) != TFUNCTION) {
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

          if(tmp > obj->pos.parsed) {
            op3 = (struct vm_toperator_t *)&obj->bytecode[tmp];
            int tright = op3->right;
            op3->right = step;

            if(tright > obj->pos.parsed) {
              switch((obj->bytecode[tright])) {
                /*
                 * Number are not seperate nodes so
                 * this block is never triggered.
                 */
                /*
                case TNUMBER: {
                  struct vm_tnumber_t *node = (struct vm_tnumber_t *)&obj->bytecode[tright];
                  node->ret = step;
                } break;
                */
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
            }
            op2->left = tright;
            op2->ret = tmp;
          } else {
            op1->right = step;
            op2->ret = *step_out;
            op2->left = tmp;
          }
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
  int loop = 1, start = 0, r_rewind = -1;

  /* LCOV_EXCL_START*/
  if(lexer_peek(obj, 0, &type) < 0) {
    fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
    return -1;
  }
  /* LCOV_EXCL_STOP*/

  if(type != TIF && type != TEVENT) {
    fprintf(stderr, "ERROR: Expected an 'if' or an 'on' statement at position #1\n");
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
          if(lexer_peek(obj, pos, &type) < 0) {
            /* LCOV_EXCL_START*/
            fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
            return -1;
            /* LCOV_EXCL_STOP*/
          }

          if((step_out == -1 || (obj->bytecode[step_out]) == TTRUE) && type != TTHEN) {
            if(type == TEVENT) {
              step_out = vm_parent(obj, TEVENT, pos);
              struct vm_tevent_t *a = (struct vm_tevent_t *)&obj->bytecode[step_out];

              if(pos == 0) {
                struct vm_tstart_t *b = (struct vm_tstart_t *)&obj->bytecode[start];
                b->go = step_out;
                a->ret = start;
              }

              pos++;

              lexer_peek(obj, pos, &type);

              if(lexer_peek(obj, pos, &type) == 0 && type == TTHEN) {
                /*
                 * Predict how many go slots we need to
                 * reserve for the TRUE / FALSE nodes.
                 */
                int y = pos, nrexpressions = 0;
                while(lexer_peek(obj, y++, &type) == 0) {
                  if(type == TEOF) {
                    break;
                  }
                  if(type == TIF) {
                    struct vm_cache_t *cache = vm_cache_get(TIF, -1, y-1);
                    nrexpressions++;
                    y = cache->end + 1;
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
                  int val = 0;
                  lexer_bytecode_pos(obj, pos+1, &val);
                  fprintf(stderr, "ERROR: On block without body at position #%d\n", val+1);
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
                pos++;
              } else {
                int val = 0;
                lexer_bytecode_pos(obj, pos, &val);
                fprintf(stderr, "ERROR: Expected a 'then' token at position #%d\n", val+1);
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
                  if(lexer_peek(obj, x->end + 1, &type) == 0) {
                    switch(type) {
                      case TSEMICOLON: {
                        pos = x->end + 2;

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
                        int val = 0;
                        lexer_bytecode_pos(obj, pos+1, &val);
                        fprintf(stderr, "ERROR: Expected an semicolon or operator at position #%d\n", val+1);
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

                  if(lexer_peek(obj, cache->end + 1, &type) < 0) {
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
                  pos = cache->end + 1;

                  /*
                   * After an cached IF block has been linked
                   * it can be removed from cache
                   */
                  vm_cache_del(cache->start);

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
                  if(has_on > 0) {
                    int val = 0;
                    lexer_bytecode_pos(obj, has_on, &val);
                    fprintf(stderr, "ERROR: On block inside if block at position #%d\n", val+1);
                    return -1;
                  }
                  if(has_on == 0) {
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
                  int val = 0;
                  lexer_bytecode_pos(obj, pos+1, &val);
                  fprintf(stderr, "ERROR: Unexpected token at position #%d\n", val);
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
          if(lexer_peek(obj, pos, &type) < 0) {
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
              if(lexer_peek(obj, cache->end + 1, &type) < 0) {
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
              pos = cache->end + 1;

              /*
               * After an cached IF block has been linked
               * it can be removed from cache
               */
              vm_cache_del(cache->start);

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
              if(lexer_peek(obj, pos, &type) == 0 && type == TIF) {
                go = TIF;
                continue;
              }
            }

            if(type == TIF) {
              step = vm_parent(obj, type, -1);
              struct vm_tif_t *a = (struct vm_tif_t *)&obj->bytecode[step];

              if(pos == 0) {
                struct vm_tstart_t *b = (struct vm_tstart_t *)&obj->bytecode[start];
                b->go = step;
                a->ret = start;
              }

              /*
               * If this is a nested IF block,
               * cache it for linking further on
               */
              if(has_if > 0) {
                vm_cache_add(TIF, step, has_if, -1);
              }

              pos++;

              if(lexer_peek(obj, pos+1, &type) == 0 && type == TOPERATOR) {
                go = TOPERATOR;
                step_out = step;
                continue;
              } else if(lexer_peek(obj, pos, &type) == 0 && type == LPAREN) {
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
                 // if (1 + 1) == 1 then $a = 3; else $a = 4; end
                if(lexer_peek(obj, cache->end + 1, &type) < 0) {
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
                  int val = 0;
                  lexer_bytecode_pos(obj, cache->end - 1, &val);
                  fprintf(stderr, "ERROR: Unexpected token at position #%d\n", val);
                  return -1;
                }

                int oldpos = pos;
                pos = cache->end + 1;
                vm_cache_del(oldpos);
                continue;
              } else if(lexer_peek(obj, pos, &type) == 0 && type == TFUNCTION) {
                step_out = step;

                /*
                 * Link cached parenthesis blocks
                 */
                struct vm_cache_t *cache = vm_cache_get(TFUNCTION, -1, pos);
                /* LCOV_EXCL_START*/
                if(cache == NULL) {
                  fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
                  return -1;
                }
                /* LCOV_EXCL_STOP*/
 
                /*
                 * If this function is part of a operator
                 * let the operator handle it.
                 */
                if(lexer_peek(obj, cache->end + 1, &type) == 0 && type == TOPERATOR) {
                  go = TOPERATOR;
                  step_out = step;
                  continue;
                } else {
                  int val = 0;
                  lexer_bytecode_pos(obj, cache->end, &val);
                  fprintf(stderr, "ERROR: Unexpected token at position #%d\n", val);
                  return -1;
                }

                int oldpos = pos;
                pos = cache->end + 1;
                vm_cache_del(oldpos);
                continue;
              } else {
                int val = 0;
                lexer_bytecode_pos(obj, pos+1, &val);
                fprintf(stderr, "ERROR: Expected a parenthesis block, function or operator at position #%d\n", val+1);
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
                  if(lexer_peek(obj, x->end + 1, &type) == 0) {
                    switch(type) {
                      case TSEMICOLON: {
                        pos = x->end + 2;

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
                      } break;
                      case TOPERATOR: {
                        go = type;
                      } break;
                      default: {
                        int val = 0;
                        lexer_bytecode_pos(obj, pos+1, &val);
                        fprintf(stderr, "ERROR: Expected an semicolon or operator at position #%d\n", val+1);
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

                    cache->end = pos;

                    r_rewind = cache->start;
                  }
                  if(has_if == 0) {
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
                  int val = 0;
                  lexer_bytecode_pos(obj, pos-1, &val);
                  fprintf(stderr, "ERROR: Unexpected token at position #%d\n", val+1);
                  return -1;
                } break;
              }
            }
          } else {
            int t = 0;

            if(lexer_peek(obj, pos, &type) < 0) {
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
            while(lexer_peek(obj, y++, &type) == 0) {
              if(type == TEOF) {
                break;
              }
              if(type == TIF) {
                struct vm_cache_t *cache = vm_cache_get(TIF, -1, y-1);
                nrexpressions++;
                y = cache->end + 1;
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
              int val = 0;
              lexer_bytecode_pos(obj, pos+1, &val);
              fprintf(stderr, "ERROR: If block without body at position #%d\n", val+1);
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
            pos++;
          }
        } break;
        case TOPERATOR: {
          int a = -1, val = 0;
          int source = step_out;

          if(lexer_peek(obj, pos, &a) == 0) {
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
                pos = x->end;
                step_out = x->step;
                vm_cache_del(oldpos);
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
                pos = x->end;
                step_out = x->step;
                vm_cache_del(oldpos);
              } break;
              case TVAR: {
                step_out = vm_parent(obj, a, pos);
              } break;
              case TNUMBER: {
                lexer_bytecode_pos(obj, pos, &val);
                step_out = val;
              } break;
              case VNULL: {
                step_out = vm_parent(obj, a, pos);
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
          int step = lexer_parse_math_order(obj, TOPERATOR, &pos, &step_out, source);
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
                int val = 0;
                lexer_bytecode_pos(obj, pos, &val);
                fprintf(stderr, "ERROR: Unexpected token at position #%d\n", val+1);
                return -1;
              } break;
            }
          }
          if(lexer_peek(obj, pos+1, &type) == 0) {
            pos++;
            switch(type) {
              case TTHEN: {
                go = TIF;
              } break;
              case TSEMICOLON: {
                go = TVAR;
              } break;
              default: {
                int val = 0;
                lexer_bytecode_pos(obj, pos, &val);
                fprintf(stderr, "ERROR: Unexpected token at position #%d\n", val+1);
                return -1;
              } break;
            }
          }
        } break;
        /*
         * In case numbers are seperate nodes
         */
        /*
        case TNUMBER: {
          struct vm_tnumber_t *node = NULL;
          if(lexer_peek(obj, pos, &type) < 0) {
            fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
            return -1;
          }

          step = vm_parent(obj, TNUMBER, pos);

          pos++;

          switch((obj->bytecode[step_out])) {
            case TVAR: {
              struct vm_tvar_t *tmp = (struct vm_tvar_t *)&obj->bytecode[step_out];
              tmp->go = step;
            } break;
            default: {
              printf("err: %s %d\n", __FUNCTION__, __LINE__);
              exit(-1);
            } break;
          }

          node = (struct vm_tnumber_t *)&obj->bytecode[step];
          node->ret = step_out;

          int tmp = vm_rewind2(obj, step_out, TVAR, TOPERATOR);
          go = (obj->bytecode[tmp]);
          step_out = step;
        } break;
        */
        case VNULL: {
          struct vm_vnull_t *node = NULL;
          if(lexer_peek(obj, pos, &type) < 0) {
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

            pos++;

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

            if(lexer_peek(obj, pos++, &type) < 0 || type != TASSIGN) {
              /* LCOV_EXCL_START*/
              fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
              return -1;
              /* LCOV_EXCL_STOP*/
            }

            if(lexer_peek(obj, pos+1, &type) == 0 && type == TOPERATOR) {
              switch(type) {
                case TOPERATOR: {
                  go = TOPERATOR;
                  step_out = step;
                  continue;
                } break;
              }
            } else if(lexer_peek(obj, pos, &type) == 0) {
              switch(type) {
                case TNUMBER: {
                  int val = 0;
                  lexer_bytecode_pos(obj, pos, &val);
                  node->go = val;
                  go = TVAR;
                  pos++;
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
                  if(lexer_peek(obj, x->end + 1, &type) == 0) {
                    switch(type) {
                      case TSEMICOLON: {
                        pos = x->end + 2;

                        struct vm_tfunction_t *f = (struct vm_tfunction_t *)&obj->bytecode[x->step];
                        struct vm_tvar_t *v = (struct vm_tvar_t *)&obj->bytecode[step_out];
                        f->ret = step;
                        v->go = x->step;

                        int tmp = vm_rewind2(obj, step_out, TTRUE, TFALSE);
                        int tmp1 = vm_rewind2(obj, tmp, TIF, TEVENT);

                        go = obj->bytecode[tmp1];
                        step_out = tmp;
                        vm_cache_del(x->start);
                      } break;
                      case TOPERATOR: {
                        go = type;
                      } break;
                      default: {
                        int val = 0;
                        lexer_bytecode_pos(obj, x->end - 1, &val);
                        fprintf(stderr, "ERROR: Expected an semicolon or operator at position #%d\n", val);
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
                  if(lexer_peek(obj, x->end + 1, &type) == 0) {
                    if(type == TSEMICOLON) {
                      pos = x->end + 2;

                      struct vm_lparen_t *l = (struct vm_lparen_t *)&obj->bytecode[x->step];
                      struct vm_tvar_t *v = (struct vm_tvar_t *)&obj->bytecode[step_out];
                      l->ret = step;
                      v->go = x->step;

                      int tmp = vm_rewind2(obj, step_out, TTRUE, TFALSE);

                      go = TIF;
                      step_out = tmp;

                      vm_cache_del(oldpos);
                    } else {
                      go = type;
                    }
                  }
                } break;
                default: {
                  int val = 0;
                  lexer_bytecode_pos(obj, pos, &val);
                  fprintf(stderr, "ERROR: Unexpected token at position #%d\n", val);
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
          } else if((obj->bytecode[step_out]) == TVAR && lexer_peek(obj, pos, &type) == 0 && type != TSEMICOLON) {
            if(lexer_peek(obj, pos, &type) == 0 && type == TVAR) {
              step = vm_parent(obj, TVAR, pos++);

              struct vm_tvar_t *in = (struct vm_tvar_t *)&obj->bytecode[step];
              struct vm_tvar_t *out = (struct vm_tvar_t *)&obj->bytecode[step_out];
              in->ret = step_out;
              out->go = step;

              if(lexer_peek(obj, pos, &type) == 0) {
                switch(type) {
                  case TSEMICOLON: {
                    go = TVAR;
                  } break;
                  default: {
                    int val = 0;
                    lexer_bytecode_pos(obj, pos-1, &val);
                    fprintf(stderr, "ERROR: Expected a semicolon at position #%d\n", val+1);
                    return -1;
                  } break;
                }
              }
            }
          } else {
            int tmp = step_out;
            pos++;
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
          if(lexer_peek(obj, pos, &type) < 0 || type != TCEVENT) {
            fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
            return -1;
          }
          /* LCOV_EXCL_STOP*/

          step = vm_parent(obj, TCEVENT, pos);

          node = (struct vm_tcevent_t *)&obj->bytecode[step];

          pos++;

          if(lexer_peek(obj, pos, &type) < 0) {
            /* LCOV_EXCL_START*/
            fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
            return -1;
            /* LCOV_EXCL_STOP*/
          }

          if(type != TSEMICOLON) {
            int val = 0;
            lexer_bytecode_pos(obj, pos-1, &val);
            fprintf(stderr, "ERROR: Expected a semicolon at position #%d\n", val);
            return -1;
          }

          pos++;

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

          if(lexer_peek(obj, has_paren+1, &a) == 0) {
            switch(a) {
              case LPAREN: {
                struct vm_cache_t *x = vm_cache_get(LPAREN, -1, has_paren+1);
                /* LCOV_EXCL_START*/
                if(x == NULL) {
                  fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
                  return -1;
                }
                /* LCOV_EXCL_STOP*/
                pos = x->end;
                step_out = x->step;
                vm_cache_del(has_paren+1);
              } break;
              case TFUNCTION: {
                struct vm_cache_t *x = vm_cache_get(TFUNCTION, -1, has_paren+1);
                /* LCOV_EXCL_START*/
                if(x == NULL) {
                  fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
                  return -1;
                }
                /* LCOV_EXCL_STOP*/
                pos = x->end;
                step_out = x->step;
                vm_cache_del(has_paren+1);
              } break;
              case TVAR: {
                step_out = vm_parent(obj, a, has_paren+1);
                pos = has_paren + 1;
              } break;
              case TNUMBER: {
                lexer_bytecode_pos(obj, has_paren + 1, &val);
                step_out = val;
                pos = has_paren + 1;
              } break;
              case VNULL: {
                step_out = vm_parent(obj, a, pos);
                pos = has_paren + 1;
              } break;
              case RPAREN: {
                int val = 0;
                lexer_bytecode_pos(obj, pos+1, &val);
                fprintf(stderr, "ERROR: Empty parenthesis block at position #%d\n", val+1);
                return -1;
              } break;
              case TOPERATOR: {
                int val = 0;
                lexer_bytecode_pos(obj, pos+1, &val);
                fprintf(stderr, "ERROR: Unexpected operator at position #%d\n", val+1);
                return -1;
              } break;
              default: {
                int val = 0;
                lexer_bytecode_pos(obj, pos+1, &val);
                fprintf(stderr, "ERROR: Unexpected token at position #%d\n", val+1);
                return -1;
              }
            }
          }

          /*
           * The return value is the positition
           * of the root operator
           */
          int step = lexer_parse_math_order(obj, LPAREN, &pos, &step_out, 0);

          if(lexer_peek(obj, pos+1, &b) < 0) {
            /* LCOV_EXCL_START*/
            fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
            return -1;
            /* LCOV_EXCL_STOP*/
          }

          if(b != RPAREN) {
            int val = 0;
            lexer_bytecode_pos(obj, pos, &val);
            fprintf(stderr, "ERROR: Expected a closing parenthesis at position #%d\n", val+1);
            return -1;
          }

          {
            struct vm_tgeneric_t *node = (struct vm_tgeneric_t *)&obj->bytecode[step_out];
            node->ret = 0;
          }

          if(lexer_peek(obj, has_paren, &type) < 0 || type != LPAREN) {
            /* LCOV_EXCL_START*/
            fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
            return -1;
            /* LCOV_EXCL_STOP*/
          }

          while(lexer_peek(obj, ++pos, &type) == 0 && type != RPAREN);

          step = vm_parent(obj, LPAREN, has_paren);
          struct vm_lparen_t *node = (struct vm_lparen_t *)&obj->bytecode[step];
          node->go = step_out;

          vm_cache_add(LPAREN, step, has_paren, pos);

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
            pos = has_function;
            if(lexer_peek(obj, has_function, &type) < 0) {
              /* LCOV_EXCL_START*/
              fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
              return -1;
              /* LCOV_EXCL_STOP*/
            }

            /*
             * Determine how many arguments this
             * function has.
             */
            int y = pos + 2, nrargs = 0;
            while(lexer_peek(obj, y++, &type) == 0) {
              if(type == TEOF || type == RPAREN) {
                break;
              }
              if(type == LPAREN) {
                struct vm_cache_t *cache = vm_cache_get(LPAREN, -1, y-1);
                y = cache->end + 1;
                continue;
              }
              if(type == TFUNCTION) {
                struct vm_cache_t *cache = vm_cache_get(TFUNCTION, -1, y-1);
                y = cache->end + 1;
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

            lexer_bytecode_pos(obj, pos++, &val);
            node->token = val;

            /* LCOV_EXCL_START*/
            if(lexer_peek(obj, pos++, &type) < 0 || type != LPAREN) {
                fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
                return -1;
            }
            /* LCOV_EXCL_STOP*/
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
              if(lexer_peek(obj, pos, &type) < 0) {
                /* LCOV_EXCL_START*/
                fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
                return -1;
                /* LCOV_EXCL_STOP*/
              }

              if(type == TCOMMA) {
                pos++;
                break;
              }
              if(type == RPAREN) {
                break;
              }
              pos++;
            }
            lexer_peek(obj, pos, &type);
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
              if(lexer_peek(obj, pos + 1, &type) < 0) {
                /* LCOV_EXCL_START*/
                fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
                return -1;
                /* LCOV_EXCL_STOP*/
              } else if(type == TOPERATOR) {
                go = TOPERATOR;
                step_out = step;
                break;
              }

              if(lexer_peek(obj, pos, &type) < 0) {
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
                  pos = cache->end + 1;
                  node->go[arg++] = cache->step;
                  paren->ret = step;
                  vm_cache_del(oldpos);
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
                  pos = cache->end + 1;

                  node->go[arg++] = cache->step;
                  func->ret = step;
                  vm_cache_del(oldpos);
                } break;
                case TNUMBER: {
                  lexer_bytecode_pos(obj, pos++, &val);
                  node->go[arg++] = val;
                } break;
                case VNULL: {
                  int a = vm_parent(obj, VNULL, pos++);
                  node = (struct vm_tfunction_t *)&obj->bytecode[step];
                  node->go[arg++] = a;
                  struct vm_vnull_t *tmp = (struct vm_vnull_t *)&obj->bytecode[a];
                  tmp->ret = step;
                } break;
                case TVAR: {
                  int a = vm_parent(obj, TVAR, pos++);
                  node = (struct vm_tfunction_t *)&obj->bytecode[step];
                  node->go[arg++] = a;
                  struct vm_tvar_t *tmp = (struct vm_tvar_t *)&obj->bytecode[a];
                  tmp->ret = step;
                } break;
                default: {
                  int val = 0;
                  lexer_bytecode_pos(obj, pos, &val);
                  fprintf(stderr, "ERROR: Unexpected token at position #%d\n", val);
                  return -1;
                } break;
              }

              if(lexer_peek(obj, pos++, &type) < 0) {
                /* LCOV_EXCL_START*/
                fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
                return -1;
                /* LCOV_EXCL_STOP*/
              }

              /*
               * A right parenthesis means we've
               * reached the end of the argument list
               */
              if(type == RPAREN) {
                break;
              } else if(type != TCOMMA) {
                int val = 0;
                lexer_bytecode_pos(obj, pos-1, &val);
                fprintf(stderr, "ERROR: Expected a closing parenthesis at position #%d\n", val-1);
                return -1;
              }
            }
          } else {
            pos++;
          }

          /*
           * When we're back to the function
           * root, cache it for further linking.
           */
          if(go == TFUNCTION) {
            vm_cache_add(TFUNCTION, step, has_function, pos - 1);

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
        if(lexer_peek(obj, pos++, &type) < 0) {
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
        if(r_rewind > 0 && lexer_peek(obj, --r_rewind, &type) < 0) {
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
      } else if(type == LPAREN && lexer_peek(obj, pos-2, &type1) == 0 && type1 != TFUNCTION) {
        has_paren = pos-1;
      }
      if(has_function != -1 || has_paren != -1 || has_if != -1 || has_on != -1) {
        if(type == TEOF || r_rewind > -1) {
          if(max(has_function, max(has_paren, max(has_if, has_on))) == has_function) {
            pos = has_function;
            go = TFUNCTION;
            continue;
          } else if(max(has_function, max(has_paren, max(has_if, has_on))) == has_if) {
            pos = has_if;
            go = TIF;
            continue;
          } else if(max(has_function, max(has_paren, max(has_if, has_on))) == has_on) {
            pos = has_on;
            go = TEVENT;
            continue;
          } else {
            pos = has_paren;
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
  if(obj->pos.parsed == 0) {
    fprintf(stderr, "FATAL: Internal error in %s #%d\n", __FUNCTION__, __LINE__);
    return -1;
  }
  struct vm_tstart_t *node = (struct vm_tstart_t *)&obj->bytecode[obj->pos.parsed];
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

  for(i=0;i<obj->nrbytes;i++) {
    if(obj->bytecode[i] == 0 && obj->bytecode[i+1] == 0) {
      x = i+2;
      break;
    }
  }
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
        i+=sizeof(struct vm_ttrue_t)+(sizeof(uint16_t)*node->nrgo);
      } break;
      case TFUNCTION: {
        int x = 0;
        struct vm_tfunction_t *node = (struct vm_tfunction_t *)&obj->bytecode[i];
        printf("\"%d\"[label=\"%s\"]\n", i, event_functions[obj->bytecode[node->token+1]].name);
        for(x=0;x<node->nrgo;x++) {
          printf("\"%d\" -> \"%d\"\n", i, node->go[x]);
        }
        // printf("\"%d\" -> \"%d\"\n", i, node->ret);
        i+=sizeof(struct vm_tfunction_t)+(sizeof(uint16_t)*node->nrgo);
      } break;
      case TVAR: {
        struct vm_tvar_t *node = (struct vm_tvar_t *)&obj->bytecode[i];
        printf("\"%d\"[label=\"%s\"]\n", i, &obj->bytecode[node->token+1]);
        if(node->go > 0) {
          printf("\"%d\" -> \"%d\"\n", i, node->go);
        }
        // printf("\"%d\" -> \"%d\"\n", i, node->ret);
        i+=sizeof(struct vm_tvar_t)-1;
      } break;
      case TEVENT: {
        struct vm_tevent_t *node = (struct vm_tevent_t *)&obj->bytecode[i];
        printf("\"%d\"[label=\"%s\"]\n", i, &obj->bytecode[node->token+1]);
        // printf("\"%i\" -> \"%i\"\n", i, node->ret);
        if(node->go > 0) {
          printf("\"%d\" -> \"%d\"\n", i, node->go);
        }
        i+=sizeof(struct vm_tevent_t)-1;
      } break;
      case TCEVENT: {
        struct vm_tcevent_t *node = (struct vm_tcevent_t *)&obj->bytecode[i];
        printf("\"%d\"[label=\"%s\"]\n", i, &obj->bytecode[node->token+1]);
        // printf("\"%i\" -> \"%i\"\n", i, node->ret);
        i+=sizeof(struct vm_tcevent_t)-1;
      } break;
      case TOPERATOR: {
        struct vm_toperator_t *node = (struct vm_toperator_t *)&obj->bytecode[i];

        printf("\"%d\"[label=\"%s\"]\n", i, event_operators[obj->bytecode[node->token+1]].name);
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

  for(i=0;i<obj->nrbytes;i++) {
    if(obj->bytecode[i] == 0 && obj->bytecode[i+1] == 0) {
      x = i+2;
      break;
    }
  }
  for(i=x;alignedbytes(i)<obj->nrbytes;i++) {
    i = alignedbytes(i);
    switch(obj->bytecode[i]) {
      case TSTART: {
        struct vm_tstart_t *node = (struct vm_tstart_t *)&obj->bytecode[i];
        printf("\"%p-1\"[label=\"%d\" shape=square]\n", node, i);
        printf("\"%p-2\"[label=\"START\"]\n", node);
        printf("\"%p-3\"[label=\"%d\" shape=square]\n", node, node->ret);
        printf("\"%p-4\"[label=\"%d\" shape=square]\n", node, node->go);
        printf("\"%p-1\" -> \"%p-2\"\n", node, node);
        printf("\"%p-2\" -> \"%p-3\"\n", node, node);
        printf("\"%p-2\" -> \"%p-4\"\n", node, node);
        i+=sizeof(struct vm_tstart_t)-1;
      } break;
      case TEOF: {
        struct vm_teof_t *node = (struct vm_teof_t *)&obj->bytecode[i];
        printf("\"%p-1\"[label=\"%d\" shape=square]\n", node, i);
        printf("\"%p-2\"[label=\"EOF\"]\n", node);
        printf("\"%p-1\" -> \"%p-2\"\n", node, node);
        i+=sizeof(struct vm_teof_t)-1;
      } break;
      case VNULL: {
        struct vm_vnull_t *node = (struct vm_vnull_t *)&obj->bytecode[i];
        printf("\"%p-1\"[label=\"%d\" shape=square]\n", node, i);
        printf("\"%p-2\"[label=\"NULL\"]\n", node);
        printf("\"%p-2\" -> \"%p-3\"\n", node, node);
        printf("\"%p-3\"[label=\"%d\" shape=diamond]\n", node, node->ret);
        printf("\"%p-1\" -> \"%p-2\"\n", node, node);
        i+=sizeof(struct vm_vnull_t)-1;
      } break;
      case TIF: {
        struct vm_tif_t *node = (struct vm_tif_t *)&obj->bytecode[i];
        printf("\"%p-2\"[label=\"IF\"]\n", node);
        printf("\"%p-2\" -> \"%p-4\"\n", node, node);
        if(node->true_ > 0) {
          printf("\"%p-2\" -> \"%p-6\"\n", node, node);
        }
        if(node->false_ > 0) {
          printf("\"%p-2\" -> \"%p-7\"\n", node, node);
        }
        printf("\"%p-1\" -> \"%p-2\"\n", node, node);
        printf("\"%p-2\" -> \"%p-3\"\n", node, node);
        if(node->true_ > 0) {
          printf("\"%p-6\"[label=\"%d\" shape=square]\n", node, node->true_);
        }
        if(node->false_ > 0) {
          printf("\"%p-7\"[label=\"%d\" shape=square]\n", node, node->false_);
        }
        printf("\"%p-1\"[label=\"%d\" shape=square]\n", node, i);
        printf("\"%p-3\"[label=\"%d\" shape=diamond]\n", node, node->ret);
        printf("\"%p-4\"[label=\"%d\" shape=square]\n", node, node->go);
        i+=sizeof(struct vm_tif_t)-1;
      } break;
      case LPAREN: {
        struct vm_lparen_t *node = (struct vm_lparen_t *)&obj->bytecode[i];
        printf("\"%p-1\"[label=\"%d\" shape=square]\n", node, i);
        printf("\"%p-2\"[label=\"(\"]\n", node);
        printf("\"%p-2\" -> \"%p-4\"\n", node, node);
        printf("\"%p-2\" -> \"%p-3\"\n", node, node);
        printf("\"%p-3\"[label=\"%d\" shape=diamond]\n", node, node->ret);
        printf("\"%p-4\"[label=\"%d\" shape=square]\n", node, node->go);
        printf("\"%p-1\" -> \"%p-2\"\n", node, node);
        i+=sizeof(struct vm_lparen_t)-1;
      } break;
      case TFALSE:
      case TTRUE: {
        int x = 0;
        struct vm_ttrue_t *node = (struct vm_ttrue_t *)&obj->bytecode[i];
        printf("\"%p-1\"[label=\"%d\" shape=square]\n", node, i);
        if((obj->bytecode[i]) == TFALSE) {
          printf("\"%p-2\"[label=\"FALSE\"]\n", node);
        } else {
          printf("\"%p-2\"[label=\"TRUE\"]\n", node);
        }
        for(x=0;x<node->nrgo;x++) {
          printf("\"%p-2\" -> \"%p-%d\"\n", node, node, x+3);
          printf("\"%p-%d\"[label=\"%d\" shape=square]\n", node, x+3, node->go[x]);
        }
        printf("\"%p-2\" -> \"%p-%d\"\n", node, node, x+4);
        printf("\"%p-%d\"[label=\"%d\" shape=diamond]\n", node, x+4, node->ret);
        printf("\"%p-1\" -> \"%p-2\"\n", node, node);
        i+=sizeof(struct vm_ttrue_t)+(sizeof(uint16_t)*node->nrgo);
      } break;
      case TFUNCTION: {
        int x = 0;
        struct vm_tfunction_t *node = (struct vm_tfunction_t *)&obj->bytecode[i];
        printf("\"%p-1\"[label=\"%d\" shape=square]\n", node, i);
        printf("\"%p-2\"[label=\"%s\"]\n", node, event_functions[obj->bytecode[node->token+1]].name);
        for(x=0;x<node->nrgo;x++) {
          printf("\"%p-2\" -> \"%p-%d\"\n", node, node, x+3);
          printf("\"%p-%d\"[label=\"%d\" shape=square]\n", node, x+3, node->go[x]);
        }
        printf("\"%p-2\" -> \"%p-%d\"\n", node, node, x+4);
        printf("\"%p-%d\"[label=\"%d\" shape=diamond]\n", node, x+4, node->ret);
        printf("\"%p-1\" -> \"%p-2\"\n", node, node);
        i+=sizeof(struct vm_tfunction_t)+(sizeof(uint16_t)*node->nrgo);
      } break;
      case TCEVENT: {
        struct vm_tcevent_t *node = (struct vm_tcevent_t *)&obj->bytecode[i];
        printf("\"%p-1\"[label=\"%d\" shape=square]\n", node, i);
        printf("\"%p-2\"[label=\"%s()\"]\n", node, &obj->bytecode[node->token+1]);
        printf("\"%p-2\" -> \"%p-3\"\n", node, node);
        printf("\"%p-3\"[label=\"%d\" shape=diamond]\n", node, node->ret);
        printf("\"%p-1\" -> \"%p-2\"\n", node, node);
        i+=sizeof(struct vm_tcevent_t)-1;
      } break;
      case TVAR: {
        struct vm_tvar_t *node = (struct vm_tvar_t *)&obj->bytecode[i];
        printf("\"%p-1\"[label=\"%d\" shape=square]\n", node, i);
        printf("\"%p-2\"[label=\"%s\"]\n", node, &obj->bytecode[node->token+1]);
        if(node->go > 0) {
          printf("\"%p-2\" -> \"%p-4\"\n", node, node);
        }
        printf("\"%p-2\" -> \"%p-3\"\n", node, node);
        printf("\"%p-3\"[label=\"%d\" shape=diamond]\n", node, node->ret);
        if(node->go > 0) {
          printf("\"%p-4\"[label=\"%d\" shape=square]\n", node, node->go);
        }
        printf("\"%p-1\" -> \"%p-2\"\n", node, node);
        i+=sizeof(struct vm_tvar_t)-1;
      } break;
      case TEVENT: {
        struct vm_tevent_t *node = (struct vm_tevent_t *)&obj->bytecode[i];
        printf("\"%p-1\"[label=\"%d\" shape=square]\n", node, i);
        printf("\"%p-2\"[label=\"%s\"]\n", node, &obj->bytecode[node->token+1]);
        if(node->go > 0) {
          printf("\"%p-2\" -> \"%p-4\"\n", node, node);
        }
        printf("\"%p-2\" -> \"%p-3\"\n", node, node);
        printf("\"%p-3\"[label=\"%d\" shape=diamond]\n", node, node->ret);
        if(node->go > 0) {
          printf("\"%p-4\"[label=\"%d\" shape=square]\n", node, node->go);
        }
        printf("\"%p-1\" -> \"%p-2\"\n", node, node);
        i+=sizeof(struct vm_tevent_t)-1;
      } break;
      case TNUMBER: {
        struct vm_tnumber_t *node = (struct vm_tnumber_t *)&obj->bytecode[i];
        printf("\"%p-1\"[label=\"%d\" shape=square]\n", node, i);
        printf("\"%p-2\"[label=\"%s\"]\n", node, &obj->bytecode[node->token+1]);
        printf("\"%p-1\" -> \"%p-2\"\n", node, node);
        printf("\"%p-3\"[label=\"%d\" shape=diamond]\n", node, node->ret);
        printf("\"%p-2\" -> \"%p-3\"\n", node, node);
        i+=sizeof(struct vm_tnumber_t)-1;
      } break;
      case TOPERATOR: {
        struct vm_toperator_t *node = (struct vm_toperator_t *)&obj->bytecode[i];

        printf("\"%p-1\"[label=\"%d\" shape=square]\n", node, i);
        printf("\"%p-3\"[label=\"%d\" shape=square]\n", node, node->left);
        printf("\"%p-4\"[label=\"%d\" shape=square]\n", node, node->right);
        printf("\"%p-2\"[label=\"%s\"]\n", node, event_operators[obj->bytecode[node->token+1]].name);
        printf("\"%p-1\" -> \"%p-2\"\n", node, &obj->bytecode[i]);
        printf("\"%p-5\"[label=\"%d\" shape=diamond]\n", node, node->ret);
        printf("\"%p-2\" -> \"%p-3\"\n", node, node);
        printf("\"%p-2\" -> \"%p-4\"\n", node, node);
        printf("\"%p-2\" -> \"%p-5\"\n", node, node);
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

  int start = alignedbytes(obj->pos.parsed);

  switch(obj->bytecode[step]) {
    case TNUMBER: {
      float var = 0;
      if(step > start) {
        /*
         * If numbers are seperate nodes
         */
        /*LCOV_EXCL_START*/
        struct vm_tnumber_t *node = (struct vm_tnumber_t *)&obj->bytecode[step];
        var = atof((char *)&obj->bytecode[node->token+1]);
        /*LCOV_EXCL_STOP*/
      } else {
        var = atof((char *)&obj->bytecode[step+1]);

      }
      float nr = 0;

      if(modff(var, &nr) == 0) {
        if((obj->bytecode = (unsigned char *)REALLOC(obj->bytecode, alignedbytes(obj->nrbytes)+sizeof(struct vm_vinteger_t))) == NULL) {
          OUT_OF_MEMORY /*LCOV_EXCL_LINE*/
        }

        struct vm_vinteger_t *value = (struct vm_vinteger_t *)&obj->bytecode[obj->nrbytes];
        value->type = VINTEGER;
        value->ret = ret;
        value->value = (int)var;
        obj->nrbytes = alignedbytes(obj->nrbytes) + sizeof(struct vm_vinteger_t);
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
  for(i=0;i<obj->nrbytes;i++) {
    if(obj->bytecode[i] == 0 && obj->bytecode[i+1] == 0) {
      x = i+2;
      break;
    }
  }
  for(i=x;alignedbytes(i)<obj->nrbytes;i++) {
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
        i+=sizeof(struct vm_ttrue_t)+(sizeof(uint16_t)*node->nrgo);
      } break;
      case TFUNCTION: {
        struct vm_tfunction_t *node = (struct vm_tfunction_t *)&obj->bytecode[i];
        node->value = 0;
        i+=sizeof(struct vm_tfunction_t)+(sizeof(uint16_t)*node->nrgo);
      } break;
      case TCEVENT: {
        i+=sizeof(struct vm_tcevent_t)-1;
      } break;
      case TVAR: {
        struct vm_tvar_t *node = (struct vm_tvar_t *)&obj->bytecode[i];
        if(rule_options.clr_token_val_cb != NULL) {
          rule_options.clr_token_val_cb(obj, i);
        } else {
          node->value = 0;
        }
        i+=sizeof(struct vm_tvar_t)-1;
      } break;
      case TEVENT: {
        struct vm_tevent_t *node = (struct vm_tevent_t *)&obj->bytecode[i];
        i+=sizeof(struct vm_tevent_t)-1;
      } break;
      /*
       * Number is not a seperate node
       */
      /*
      case TNUMBER: {
        struct vm_tnumber_t *node = (struct vm_tnumber_t *)&obj->bytecode[i];
        i+=sizeof(struct vm_tnumber_t)-1;
      } break;
      */
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
  go = start = obj->pos.parsed;

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
        return rule_options.event_cb(obj, (const char *)&obj->bytecode[node->token+1]);
      } break;
      case TFUNCTION: {
        struct vm_tfunction_t *node = (struct vm_tfunction_t *)&obj->bytecode[go];

        int match = 0, tmp = go;

        for(i=0;i<node->nrgo;i++) {
          if(node->go[i] == ret) {
            match = 1;
            if(i+1 < node->nrgo) {
              switch(obj->bytecode[node->go[i+1]]) {
                case TNUMBER: {
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
          int idx = obj->bytecode[node->token+1], c = 0, i = 0, shift = 0;
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
              case TNUMBER: {
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

        if(node->right == ret || (node->left < start && node->right < start)) {
          int a = 0, b = 0, c = 0, step = 0, next = 0, out = 0;
          step = node->left;

          switch(obj->bytecode[step]) {
            case TNUMBER: {
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
            case TNUMBER: {
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
          int idx = obj->bytecode[node->token+1];

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
        } else if(node->left == ret || node->left < obj->pos.parsed) {
          ret = go;
          go = node->right;
        } else {
          ret = go;
          go = node->left;
        }
      } break;
      case TNUMBER: {
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
          ret = node->ret;
          go = node->go[0];
        }

        if(go == 0) {
          go = node->ret;
          ret = tmp;
        }
      } break;
      case TVAR: {
        struct vm_tvar_t *node = (struct vm_tvar_t *)&obj->bytecode[go];

        if(go < obj->pos.parsed) {
          int tmp = ret;
          ret = go;
          go = tmp;
        } else if(node->go == 0) {
          if(rule_options.cpy_token_val_cb != NULL) {
            rule_options.cpy_token_val_cb(obj, go);
          }

          /*
           * Reassign node due to various (unsigned char *)REALLOC's
           */
          node = (struct vm_tvar_t *)&obj->bytecode[go];

          ret = go;
          go = node->ret;
        } else if(go > obj->pos.parsed) {
          /*
           * When we can find the value in the
           * prepared rule and not as a separate
           * node.
           */
          if(node->go < obj->pos.parsed || node->go == ret) {
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
              case TNUMBER: {
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
  int step = 0, i = 0;
  for(i=0;i<obj->nrbytes;i++) {
    if(obj->bytecode[i] == 0 && obj->bytecode[i+1] == 0) {
      printf(". . ");
      i+=2;
      step = 1;
    }
    if(i == obj->nrbytes) {
      printf("\n");
    }
    if(step == 0) {
      if(obj->bytecode[i] == TIF || obj->bytecode[i] == TTHEN ||
         obj->bytecode[i] == TEND || obj->bytecode[i] == TASSIGN ||
         obj->bytecode[i] == TELSE || obj->bytecode[i] == TSEMICOLON ||
         obj->bytecode[i] == TEND || obj->bytecode[i] == TSTART ||
         obj->bytecode[i] == LPAREN || obj->bytecode[i] == RPAREN ||
         obj->bytecode[i] == TCOMMA || obj->bytecode[i] == VNULL ||
         obj->bytecode[i] == TEOF) {
        printf("%d ", obj->bytecode[i]);
      } else if(obj->bytecode[i] == TOPERATOR) {
        printf("%d ", obj->bytecode[i]);
        printf("%d ", obj->bytecode[++i]);
      } else if(obj->bytecode[i] == TFUNCTION) {
        printf("%d ", obj->bytecode[i]);
        printf("%d ", obj->bytecode[++i]);
      } else if(obj->bytecode[i] == TVAR ||
                obj->bytecode[i] == TNUMBER ||
                obj->bytecode[i] == TSTRING ||
                obj->bytecode[i] == TCEVENT ||
                obj->bytecode[i] == TEVENT) {
        printf("%d ", obj->bytecode[i]);
        while(obj->bytecode[i] != 0) {
          printf("%c", obj->bytecode[++i]);
        }
        printf(". ");
      } else {
        printf(".");
      }
    } else if(i < obj->nrbytes) {
      switch(obj->bytecode[i]) {
        case TIF: {
          struct vm_tif_t *node = (struct vm_tif_t *)&obj->bytecode[i];
          printf("%d ", node->type);
          printf("%d ", node->ret);
          printf("%d ", node->go);
          printf("%d ", node->true_);
          printf("%d ", node->false_);
          i += sizeof(struct vm_tif_t)-1;
        } break;
        case LPAREN: {
          struct vm_lparen_t *node = (struct vm_lparen_t *)&obj->bytecode[i];
          printf("%d ", node->type);
          printf("%d ", node->ret);
          printf("%d ", node->go);
          printf("%d ", node->value);
          i += sizeof(struct vm_lparen_t)-1;
        } break;
        case TVAR: {
          struct vm_tvar_t *node = (struct vm_tvar_t *)&obj->bytecode[i];
          printf("%d ", node->type);
          printf("%d ", node->ret);
          printf("%d ", node->token);
          printf("%d ", node->go);
          printf("%d ", node->value);
          i += sizeof(struct vm_tvar_t)-1;
        } break;
        case TEVENT: {
          struct vm_tevent_t *node = (struct vm_tevent_t *)&obj->bytecode[i];
          printf("%d ", node->type);
          printf("%d ", node->ret);
          printf("%d ", node->token);
          printf("%d ", node->go);
          i += sizeof(struct vm_tevent_t)-1;
        } break;
        case TCEVENT: {
          struct vm_tevent_t *node = (struct vm_tevent_t *)&obj->bytecode[i];
          printf("%d ", node->type);
          printf("%d ", node->ret);
          printf("%d ", node->token);
          i += sizeof(struct vm_tevent_t)-1;
        } break;
        case TSTART: {
          struct vm_tstart_t *node = (struct vm_tstart_t *)&obj->bytecode[i];
          printf("%d ", node->type);
          printf("%d ", node->ret);
          printf("%d ", node->go);
          i += sizeof(struct vm_tstart_t)-1;
        } break;
        case TNUMBER: {
          struct vm_tnumber_t *node = (struct vm_tnumber_t *)&obj->bytecode[i];
          printf("%d ", node->type);
          printf("%d ", node->ret);
          printf("%d ", node->token);
          i += sizeof(struct vm_tnumber_t)-1;
        } break;
        case TFALSE:
        case TTRUE: {
          struct vm_ttrue_t *node = (struct vm_ttrue_t *)&obj->bytecode[i];
          printf("%d ", node->type);
          printf("%d ", node->ret);
          printf("%d ", node->nrgo);
          int x = 0;
          for(x=0;x<node->nrgo;x++) {
            printf("%d ", node->go[x]);
          }
          i += sizeof(struct vm_ttrue_t)+(sizeof(uint16_t)*node->nrgo);
        } break;
        case TOPERATOR: {
          struct vm_toperator_t *node = (struct vm_toperator_t *)&obj->bytecode[i];
          printf("%d ", node->type);
          printf("%d ", node->ret);
          printf("%d ", node->left);
          printf("%d ", node->right);
          printf("%d ", node->value);
          i += sizeof(struct vm_toperator_t)-1;
        } break;
        case TEOF: {
          struct vm_teof_t *node = (struct vm_teof_t *)&obj->bytecode[i];
          printf("%d ", node->type);
          i += sizeof(struct vm_teof_t)-1;
        } break;
        case VNULL: {
          struct vm_vnull_t *node = (struct vm_vnull_t *)&obj->bytecode[i];
          printf("%d ", node->type);
          printf("%d ", node->ret);
          i += sizeof(struct vm_vnull_t)-1;
        } break;
        default: {
          printf(". ");
        }
      }
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

  if(rule_prepare(text, pos, obj) == -1) {
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
    return -1;
  }

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
