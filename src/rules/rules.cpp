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
  #include <stdint.h>
#else
  #include <Arduino.h>
#endif
#undef NDEBUG
#include <assert.h>

#include "../common/mem.h"
#include "../common/log.h"
#include "../common/mem.h"
#include "../common/log.h"
#include "../common/uint32float.h"
#include "../common/strnicmp.h"
#include "rules.h"
#include "operator.h"
#include "function.h"

#define MAXQUEUESIZE 10

static struct rulequeue_t {
  uint8_t nr;
} rulequeue[MAXQUEUESIZE];

static uint8_t queuestart = 0;
static uint8_t queueend = 0;
static uint8_t queuenrel = 0;

static struct vm_cache_t {
  uint8_t type;
  uint16_t step;
  uint16_t start;
  uint16_t end;
} __attribute__((aligned(4))) **vmcache;

static uint16_t nrcache = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
static uint8_t is_mmu_input = 0;
#endif

/*LCOV_EXCL_START*/
#ifdef DEBUG
static void print_tree(struct rules_t *obj);
static void print_bytecode(struct rules_t *obj);
#endif
/*LCOV_EXCL_STOP*/

#ifndef ESP8266
uint8_t mmu_set_uint8(void *ptr, uint8_t src) { *(uint8_t *)ptr = src; return src; }
uint8_t mmu_get_uint8(void *ptr) { return *(uint8_t *)ptr; }
uint16_t mmu_set_uint16(void *ptr, uint16_t src) { *(uint16_t *)ptr = src; return src; }
uint16_t mmu_get_uint16(void *ptr) { return (*(uint16_t *)ptr); }
#endif

static uint32_t align(uint32_t p, uint8_t b) {
  return (p + b) - ((p + b) % b);
}

int8_t rule_max_var_bytes(void) {
  return MAX(sizeof(struct vm_vinteger_t), MAX(sizeof(struct vm_vfloat_t), sizeof(struct vm_vnull_t)));
}

char *rule_by_nr(struct rules_t **rules, uint8_t nrrules, uint8_t nr) {
  uint16_t go = 0, type = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  uint8_t is_mmu = 0;
#endif

  if(nr > nrrules) {
    return NULL;
  }
  struct rules_t *obj = rules[nr];
  struct vm_tstart_t *start = (struct vm_tstart_t *)&obj->ast.buffer[0];

#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  if(obj->ast.buffer >= (void *)MMU_SEC_HEAP) {
    is_mmu = 1;
  }
#endif

  if(start == NULL) {
    return NULL;
  }
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  if(is_mmu == 1) {
    go = mmu_get_uint16(&start->go);
    type = mmu_get_uint8(&obj->ast.buffer[go]);
  } else {
#endif
    go = start->go;
    type = obj->ast.buffer[go];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  }
#endif
  if(type != TEVENT) {
    return NULL;
  } else {
    char *cpy = NULL;
    uint16_t len = 0, x = 0;
    struct vm_tevent_t *ev = (struct vm_tevent_t *)&obj->ast.buffer[go];

#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
    if(is_mmu == 1) {
      while(mmu_get_uint8(&ev->token[++len]) != 0);
      if((cpy = (char *)MALLOC(len+1)) == NULL) {
        return NULL;
      }
      memset(cpy, 0, len+1);
      for(x=0;x<len;x++) {
        cpy[x] = mmu_get_uint8(&ev->token[x]);
      }
      return cpy;
    } else {
#endif
      while(ev->token[++len] != 0);
      if((cpy = (char *)MALLOC(len+1)) == NULL) {
        return NULL;
      }
      memset(cpy, 0, len+1);
      for(x=0;x<len;x++) {
        cpy[x] = ev->token[x];
      }

      return cpy;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
    }
#endif
  }
  return NULL;
}

int8_t rule_by_name(struct rules_t **rules, uint8_t nrrules, char *name) {
  uint16_t go = 0, type = 0;
  uint8_t a = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  uint8_t is_mmu = 0;
#endif

  for(a=0;a<nrrules;a++) {
    struct rules_t *obj = rules[a];
    struct vm_tstart_t *start = (struct vm_tstart_t *)&obj->ast.buffer[0];

#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
    if((void *)&obj->ast >= (void *)MMU_SEC_HEAP) {
      is_mmu = 1;
    }

    if(is_mmu == 1) {
      go = mmu_get_uint16(&start->go);
      type = mmu_get_uint8(&obj->ast.buffer[go]);
    } else {
#endif
      go = start->go;
      type = obj->ast.buffer[go];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
    }
#endif
    if(type != TEVENT) {
      return -1;
    } else {
      uint16_t len = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      uint16_t x = 0;
#endif

      struct vm_tevent_t *ev = (struct vm_tevent_t *)&obj->ast.buffer[go];

#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      if(is_mmu == 1) {
        while(mmu_get_uint8(&ev->token[++len]) != 0);
        char cpy[len+1];
        memset(&cpy, 0, len+1);
        for(x=0;x<len;x++) {
          cpy[x] = mmu_get_uint8(&ev->token[x]);
        }

        if(len == strlen(name) && strnicmp(name, cpy, len) == 0) {
          return a;
        }
      } else {
#endif
        len = strlen((char *)ev->token);
        char cpy[len+1];
        memset(&cpy, 0, len+1);
        strcpy(cpy, (char *)ev->token);
        if(len == strlen(name) && strnicmp(name, cpy, len) == 0) {
          return a;
        }
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
#endif
    }
  }
  return -1;
}

static int8_t is_function(char *text, uint16_t *pos, uint16_t size) {
  uint16_t i = 0, len = 0;
  for(i=0;i<nr_rule_functions;i++) {
    len = strlen(rule_functions[i].name);
    if(size == len) {
      uint16_t x = 0;
      for(x=0;x<len;x++) {
        char cpy = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        if(is_mmu_input == 1) {
          cpy = mmu_get_uint8(&text[*pos+x]);
        } else {
#endif
          cpy = text[*pos+x];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        }
#endif
        if(tolower(cpy) != tolower(rule_functions[i].name[x])) {
          break;
        }
      }
      if(x == len) {
        return i;
      }
    }
  }

  return -1;
}

static int8_t is_operator(char *text, uint16_t *pos, uint16_t size) {
  uint16_t i = 0, len = 0;
  for(i=0;i<nr_rule_operators;i++) {
    len = strlen(rule_operators[i].name);
    if(size == len) {
      uint16_t x = 0;
      for(x=0;x<len;x++) {
        char cpy = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        if(is_mmu_input == 1) {
          cpy = mmu_get_uint8(&text[*pos+x]);
        } else {
#endif
          cpy = text[*pos+x];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        }
#endif
        if(tolower(cpy) != tolower(rule_operators[i].name[x])) {
          break;
        }
      }
      if(x == len) {
        return i;
      }
    }
  }

  return -1;
}

static int8_t lexer_parse_number(char *text, uint16_t len, uint16_t *pos) {
  uint16_t i = 0, nrdot = 0;
  char current = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  if(is_mmu_input == 1) {
    current = mmu_get_uint8(&text[*pos]);
  } else {
#endif
    current = text[*pos];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  }
#endif

  if(isdigit(current) || current == '-') {
    /*
     * The dot cannot be the first character
     * and we cannot have more than 1 dot
     */
    while(*pos <= len &&
        (
          isdigit(current) ||
          (i == 0 && current == '-') ||
          (i > 0 && nrdot == 0 && current == '.')
        )
      ) {
      if(current == '.') {
        nrdot++;
      }
      (*pos)++;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      if(is_mmu_input == 1) {
        current = mmu_get_uint8(&text[*pos]);
      } else {
#endif
        current = text[*pos];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
#endif
      i++;
    }

    return 0;
  } else {
    return -1;
  }
}

static uint16_t lexer_parse_string(char *text, uint16_t len, uint16_t *pos) {
  char current = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  if(is_mmu_input == 1) {
    current = mmu_get_uint8(&text[*pos]);
  } else {
#endif
    current = text[*pos];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  }
#endif
  while(*pos <= len &&
      (current != ' ' &&
      current != ',' &&
      current != ';' &&
      current != '(' &&
      current != ')')) {
    (*pos)++;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
    if(is_mmu_input == 1) {
      current = mmu_get_uint8(&text[*pos]);
    } else {
#endif
      current = text[*pos];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
    }
#endif
  }

  return 0;
}

static int8_t lexer_parse_skip_characters(char *text, uint16_t len, uint16_t *pos) {
  char current = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  if(is_mmu_input == 1) {
    current = mmu_get_uint8(&text[*pos]);
  } else {
#endif
    current = text[*pos];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  }
#endif
  while(*pos <= len &&
      (current == ' ' ||
      current == '\n' ||
      current == '\t' ||
      current == '\r')) {
    (*pos)++;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
    if(is_mmu_input == 1) {
      current = mmu_get_uint8(&text[*pos]);
    } else {
#endif
      current = text[*pos];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
    }
#endif
  }

  return 0;
}

static int16_t lexer_peek(char **text, uint16_t skip, uint8_t *type, uint16_t *start, uint16_t *len) {
  uint16_t i = 0, nr = 0;
  uint8_t loop = 1;

  while(loop) {
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
    if(is_mmu_input == 1) {
      *type = mmu_get_uint8(&(*text)[i]);
    } else {
#endif
      *type = (*text)[i];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
    }
#endif
    *start = i;
    *len = 0;
    switch(*type) {
      case TELSEIF:
      case TIF:{
        i += 2;
      } break;
      case VNULL:
      case TSEMICOLON:
      case TEND:
      case TASSIGN:
      case RPAREN:
      case TCOMMA:
      case LPAREN:
      case TELSE:
      case TTHEN: {
        i += 1;
      } break;
      case TNUMBER1: {
        *len = 1;
        i += 2;
      } break;
      case TNUMBER2: {
        *len = 2;
        i += 3;
      } break;
      case TNUMBER3: {
        *len = 3;
        i += 4;
      } break;
      case VINTEGER: {
        /*
         * Sizeof should reflect
         * sizeof of vm_vinteger_t
         * value
         */
        i += 1+sizeof(uint32_t);
      } break;
      case VFLOAT: {
        /*
         * Sizeof should reflect
         * sizeof of vm_vfloat_t
         * value
         */
        i += 1+sizeof(uint32_t);
      } break;
      case TFUNCTION:
      case TOPERATOR: {
        i += 2;
      } break;
      case TEOF: {
        i += 1;
        loop = 0;
      } break;
      case TEVENT:
      case TVAR: {
        i++;
        uint8_t current = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        if(is_mmu_input == 1) {
          current = mmu_get_uint8(&(*text)[i]);
        } else {
#endif
          current = (*text)[i];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        }
#endif
        /*
         * Consider tokens above 31 as regular characters
         */
        while(current >= 32 && current < 126) {
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          if(is_mmu_input == 1) {
            current = mmu_get_uint8(&(*text)[++i]);
          } else {
#endif
            current = (*text)[++i];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          }
#endif
        }
        *len = i - *start - 1;
      } break;
      /* LCOV_EXCL_START*/
      default: {
        logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
        return -1;
      } break;
      /* LCOV_EXCL_STOP*/
    }
    if(skip == nr++) {
      return i;
    }
  }
  return -1;
}

static int8_t rule_prepare(char **text, uint16_t *nrbytes, uint16_t *len) {
  uint16_t pos = 0, nrblocks = 0, tpos = 0, trueslots = 0;
  uint16_t funcslots = 0, nrtokens = 0;
  char current = 0, next = 0;

  *nrbytes = sizeof(struct vm_tstart_t);
#ifdef DEBUG
  printf("TSTART: %lu\n", sizeof(struct vm_tstart_t));
#endif

  while(pos < *len) {
    lexer_parse_skip_characters(*text, *len, &pos);

    next = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
    if(is_mmu_input == 1) {
      current = mmu_get_uint8(&(*text)[pos]);
      if(pos < *len) {
        next = mmu_get_uint8(&(*text)[pos+1]);
      }
    } else {
#endif
      current = (*text)[pos];
      if(pos < *len) {
        next = (*text)[pos+1];
      }
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
    }
#endif

    if(isdigit(current) || (current == '-' && pos < *len && isdigit(next))) {
      uint16_t newlen = 0;
      nrtokens++;
      lexer_parse_number(&(*text)[pos], *len-pos, &newlen);

      char tmp = 0;
      float var = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      if(is_mmu_input == 1) {
        tmp = mmu_get_uint8(&(*text)[pos+newlen]);
        mmu_set_uint8(&(*text)[pos+newlen], 0);
        char cpy[newlen+1];
        memset(&cpy, 0, newlen+1);
        for(uint16_t x=0;x<newlen;x++) {
          cpy[x] = mmu_get_uint8(&(*text)[pos+x]);
        }
        var = atof(cpy);
      } else {
#endif
        tmp = (*text)[pos+newlen];
        (*text)[pos+newlen] = 0;
        var = atof((char *)&(*text)[pos]);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
#endif

      float nr = 0;

      if(modff(var, &nr) == 0) {
        /*
         * This range of integers
         * take less bytes when stored
         * as ascii characters.
         */
        if(var < 100 && var > -9) {
#ifdef DEBUG
          printf("TNUMBER: %lu\n", sizeof(struct vm_tnumber_t)+align(newlen+1, 4));
#endif
          *nrbytes += sizeof(struct vm_tnumber_t)+align(newlen+1, 4);
        } else {
#ifdef DEBUG
          printf("TNUMBER: %lu\n", sizeof(struct vm_vinteger_t));
#endif
          *nrbytes += sizeof(struct vm_vinteger_t);
        }
      } else {
#ifdef DEBUG
        printf("TNUMBER: %lu\n", sizeof(struct vm_vfloat_t));
#endif
        *nrbytes += sizeof(struct vm_vfloat_t);
      }

      if(newlen < 4) {
        switch(newlen) {
          case 1: {
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            if(is_mmu_input == 1) {
              mmu_set_uint8(&(*text)[tpos++], TNUMBER1);
            } else {
#endif
              (*text)[tpos++] = TNUMBER1;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            }
#endif
          } break;
          case 2: {
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            if(is_mmu_input == 1) {
              mmu_set_uint8(&(*text)[tpos++], TNUMBER2);
            } else {
#endif
              (*text)[tpos++] = TNUMBER2;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            }
#endif
          } break;
          case 3: {
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            if(is_mmu_input == 1) {
              mmu_set_uint8(&(*text)[tpos++], TNUMBER3);
            } else {
#endif
              (*text)[tpos++] = TNUMBER3;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            }
#endif
          } break;
        }

#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        if(is_mmu_input == 1) {
          for(uint16_t x=0;x<newlen;x++) {
            mmu_set_uint8(&(*text)[tpos+x], mmu_get_uint8(&(*text)[pos+x]));
          }
        } else {
#endif
          memcpy(&(*text)[tpos], &(*text)[pos], newlen);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        }
#endif
        tpos += newlen;
      } else {
        uint32_t x = 0;
        if(modff(var, &nr) == 0) {
          /*
           * This range of integers
           * take less bytes when stored
           * as ascii characters.
           */
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          if(is_mmu_input == 1) {
            mmu_set_uint8(&(*text)[tpos++], VINTEGER);
          } else {
#endif
            (*text)[tpos++] = VINTEGER;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          }
#endif
          x = (uint32_t)var;
        } else {
          float2uint32(var, &x);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          if(is_mmu_input == 1) {
            mmu_set_uint8(&(*text)[tpos++], VFLOAT);
          } else {
#endif
            (*text)[tpos++] = VFLOAT;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          }
#endif
        }
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        if(is_mmu_input == 1) {
          mmu_set_uint8(&(*text)[tpos++], (x >> 24) & 0xFF);
          mmu_set_uint8(&(*text)[tpos++], (x >> 16) & 0xFF);
          mmu_set_uint8(&(*text)[tpos++], (x >> 8) & 0xFF);
          mmu_set_uint8(&(*text)[tpos++], x & 0xFF);
        } else {
#endif
          memcpy(&(*text)[tpos], &x, sizeof(uint32_t));
          tpos += sizeof(uint32_t);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        }
#endif
      }

      if(tmp == ' ' || tmp == '\n' || tmp != '\t' || tmp != '\r') {
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        if(is_mmu_input == 1) {
          mmu_set_uint8(&(*text)[pos+newlen], tmp);
        } else {
#endif
          (*text)[pos+newlen] = tmp;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        }
#endif
        pos += newlen;
      } else {
        uint16_t x = 0;
        while(tmp != ' ' && tmp != '\n'  && tmp != '\t' && tmp != '\r') {
          char tmp1 = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          if(is_mmu_input == 1) {
            tmp = mmu_get_uint8(&(*text)[pos+newlen+1]);
            mmu_set_uint8(&(*text)[pos+newlen+1], tmp);
          } else {
#endif
            tmp = (*text)[pos+newlen+1];
            (*text)[pos+newlen+1] = tmp;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          }
#endif
          tmp = tmp1;
          newlen += 1;
          x++;
        }

        char cpy = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        if(is_mmu_input == 1) {
          cpy = mmu_get_uint8(&(*text)[pos+newlen]);
        } else {
#endif
          cpy = (*text)[pos+newlen];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        }
#endif
        if(cpy == ' ' || cpy == '\t' || cpy == '\r' || cpy == '\n') {
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          if(is_mmu_input == 1) {
            mmu_set_uint8(&(*text)[pos+newlen], tmp);
          } else {
#endif
            (*text)[pos+newlen] = tmp;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          }
#endif
        }
        pos += newlen-x+1;
      }
    } else if(tolower(current) == 's' && tolower(next) == 'y' &&
              pos+5 < *len &&
              (
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                (is_mmu_input == 1 &&
                  tolower(mmu_get_uint8(&(*text)[pos+2])) == 'n' &&
                  tolower(mmu_get_uint8(&(*text)[pos+3])) == 'c' &&
                  tolower(mmu_get_uint8(&(*text)[pos+4])) == ' ' &&
                  tolower(mmu_get_uint8(&(*text)[pos+5])) == 'i' &&
                  tolower(mmu_get_uint8(&(*text)[pos+6])) == 'f'
                )
              ||
                (is_mmu_input == 0 &&
#else
                (
#endif
                  tolower((*text)[pos+2]) == 'n' &&
                  tolower((*text)[pos+3]) == 'c' &&
                  tolower((*text)[pos+4]) == ' ' &&
                  tolower((*text)[pos+5]) == 'i' &&
                  tolower((*text)[pos+6]) == 'f'
                )
              )
            ) {
      nrtokens++;
      *nrbytes += sizeof(struct vm_tif_t);
      *nrbytes += sizeof(struct vm_ttrue_t);

#ifdef DEBUG
      printf("TIF: %lu\n", sizeof(struct vm_tif_t));
      printf("TTRUE: %lu\n", sizeof(struct vm_ttrue_t));
#endif

      /*
       * An additional TTRUE slot
       */
      trueslots++;
#ifdef DEBUG
      printf("TTRUE: %lu\n", sizeof(uint16_t));
#endif
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      if(is_mmu_input == 1) {
        mmu_set_uint8(&(*text)[tpos++], TIF);
        mmu_set_uint8(&(*text)[tpos++], 1);
      } else {
#endif
        (*text)[tpos++] = TIF;
        (*text)[tpos++] = 1;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
#endif

      pos += 7;
      nrblocks++;
    } else if(tolower(current) == 'i' && tolower(next) == 'f') {
      nrtokens++;
      *nrbytes += sizeof(struct vm_tif_t);
      *nrbytes += sizeof(struct vm_ttrue_t);

#ifdef DEBUG
      printf("TIF: %lu\n", sizeof(struct vm_tif_t));
      printf("TTRUE: %lu\n", sizeof(struct vm_ttrue_t));
#endif

      /*
       * An additional TTRUE slot
       */
      trueslots++;
#ifdef DEBUG
      printf("TTRUE: %lu\n", sizeof(uint16_t));
#endif
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      if(is_mmu_input == 1) {
        mmu_set_uint8(&(*text)[tpos++], TIF);
        mmu_set_uint8(&(*text)[tpos++], 0);
      } else {
#endif
        (*text)[tpos++] = TIF;
        (*text)[tpos++] = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
#endif

      pos += 2;
      nrblocks++;
    } else if(tolower(current) == 'e' && tolower(next) == 'l' &&
              pos+4 < *len &&
              (
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                (is_mmu_input == 1 &&
                  tolower(mmu_get_uint8(&(*text)[pos+2])) == 's' &&
                  tolower(mmu_get_uint8(&(*text)[pos+3])) == 'e' &&
                  tolower(mmu_get_uint8(&(*text)[pos+4])) == 'i' &&
                  tolower(mmu_get_uint8(&(*text)[pos+5])) == 'f'
                )
              ||
                (is_mmu_input == 0 &&
#else
                (
#endif
                  tolower((*text)[pos+2]) == 's' &&
                  tolower((*text)[pos+3]) == 'e' &&
                  tolower((*text)[pos+4]) == 'i' &&
                  tolower((*text)[pos+5]) == 'f'
                )
              )
            ) {
      *nrbytes += sizeof(struct vm_tif_t);
      *nrbytes += sizeof(struct vm_ttrue_t);
      /*
       * ELSEIF enforces a false statement to the previous
       * IF block (which shouldn't already be there if
       * the syntax is correct).
       */
      *nrbytes += sizeof(struct vm_ttrue_t);
#ifdef DEBUG
      printf("TELSEIF: %lu\n", sizeof(struct vm_tif_t));
      printf("TTRUE: %lu\n", sizeof(struct vm_ttrue_t));
#endif

      /*
       * An additional TTRUE slot
       */
       *nrbytes += sizeof(struct vm_ttrue_t);
#ifdef DEBUG
      printf("TTRUE: %lu\n", sizeof(uint16_t));
#endif
      // printf("TELSEIF: %d\n", tpos);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      if(is_mmu_input == 1) {
        mmu_set_uint8(&(*text)[tpos++], TELSEIF);
        mmu_set_uint8(&(*text)[tpos++], 0);
      } else {
#endif
        (*text)[tpos++] = TELSEIF;
        (*text)[tpos++] = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
#endif

      pos+=6;
    } else if(tolower(current) == 'o' && tolower(next) == 'n') {
      nrtokens++;
      pos+=2;
      if(pos < *len) {
        lexer_parse_skip_characters((*text), *len, &pos);
      }
      uint16_t s = pos;
      lexer_parse_string((*text), *len, &pos);

      {
        uint16_t len = pos - s;
        *nrbytes += sizeof(struct vm_tevent_t)+align(len+1, 4);
        *nrbytes += sizeof(struct vm_ttrue_t);
#ifdef DEBUG
        printf("TEVENT: %lu\n", sizeof(struct vm_tevent_t)+len+1);
        printf("TTRUE: %lu\n", sizeof(struct vm_ttrue_t));
#endif
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        if(is_mmu_input == 1) {
          uint16_t x = 0;
          mmu_set_uint8(&(*text)[tpos++], TEVENT);
          for(x=0;x<len;x++) {
            mmu_set_uint8(&(*text)[tpos+x], mmu_get_uint8(&(*text)[s+x]));
          }
        } else {
#endif
          (*text)[tpos++] = TEVENT;
          memcpy(&(*text)[tpos], &(*text)[s], len);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        }
#endif
        tpos += len;

        /*
         * An additional TTRUE slot
         */
        trueslots++;
#ifdef DEBUG
        printf("TTRUE: %lu\n", sizeof(uint16_t));
#endif
      }
      nrblocks++;
    } else if(tolower(current) == 'e' && tolower(next) == 'l' &&
              pos+2 < *len &&
              (
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                (is_mmu_input == 1 &&
                  tolower(mmu_get_uint8(&(*text)[pos+2])) == 's' &&
                  tolower(mmu_get_uint8(&(*text)[pos+3])) == 'e'
                )
              ||
                (is_mmu_input == 0 &&
#else
                (
#endif
                  tolower((*text)[pos+2]) == 's' &&
                  tolower((*text)[pos+3]) == 'e'
                )
              )
            ) {
      nrtokens++;
      *nrbytes += sizeof(struct vm_ttrue_t);
#ifdef DEBUG
      printf("TTRUE: %lu\n", sizeof(struct vm_ttrue_t));
#endif
      pos+=4;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      if(is_mmu_input == 1) {
        mmu_set_uint8(&(*text)[tpos++], TELSE);
      } else {
#endif
        (*text)[tpos++] = TELSE;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
#endif
    } else if(tolower(current) == 't' && tolower(next) == 'h' &&
              pos+2 < *len &&
              (
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                (is_mmu_input == 1 &&
                  tolower(mmu_get_uint8(&(*text)[pos+2])) == 'e' &&
                  tolower(mmu_get_uint8(&(*text)[pos+3])) == 'n'
                )
              ||
                (is_mmu_input == 0 &&
#else
                (
#endif
                  tolower((*text)[pos+2]) == 'e' &&
                  tolower((*text)[pos+3]) == 'n'
                )
              )
            ) {
      nrtokens++;
      pos+=4;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      if(is_mmu_input == 1) {
        mmu_set_uint8(&(*text)[tpos++], TTHEN);
      } else {
#endif
        (*text)[tpos++] = TTHEN;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
#endif
    } else if(tolower(current) == 'e' && tolower(next) == 'n' &&
              pos+1 < *len &&
                (
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                  (is_mmu_input == 1 && tolower(mmu_get_uint8(&(*text)[pos+2])) == 'd') ||
                  (is_mmu_input == 0 &&
#else
                  (
#endif
                    tolower((*text)[pos+2]) == 'd')
                )
              ) {
      nrtokens++;
      pos+=3;
      nrblocks--;
      mmu_set_uint8(&(*text)[tpos++], TEND);
    } else if(tolower(current) == 'n' && tolower(next) == 'u' &&
              pos+2 < *len &&
              (
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                (
                  is_mmu_input == 1 &&
                  tolower(mmu_get_uint8(&(*text)[pos+2])) == 'l' &&
                  tolower(mmu_get_uint8(&(*text)[pos+3])) == 'l'
                )
              ||
                (
                  is_mmu_input == 0 &&
#else
                (
#endif
                  tolower((*text)[pos+2]) == 'l' &&
                  tolower((*text)[pos+3]) == 'l'
                )
              )
            ) {
      nrtokens++;
      *nrbytes += sizeof(struct vm_vnull_t);
#ifdef DEBUG
      printf("VNULL: %lu\n", sizeof(struct vm_vnull_t));
#endif
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      if(is_mmu_input == 1) {
        mmu_set_uint8(&(*text)[tpos++], VNULL);
      } else {
#endif
        (*text)[tpos++] = VNULL;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
#endif
      pos+=4;
    } else if(current == ',') {
      nrtokens++;
      /*
       * An additional function argument slot
       */
      funcslots++;
#ifdef DEBUG
      printf("TFUNCTION: %lu\n", sizeof(uint16_t));
#endif
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      if(is_mmu_input == 1) {
        mmu_set_uint8(&(*text)[tpos++], TCOMMA);
      } else {
#endif
        (*text)[tpos++] = TCOMMA;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
#endif
      pos++;
    } else if(current == '(') {
      nrtokens++;
      *nrbytes += sizeof(struct vm_lparen_t);
#ifdef DEBUG
      printf("LPAREN: %lu\n", sizeof(struct vm_lparen_t));
#endif
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      if(is_mmu_input == 1) {
        mmu_set_uint8(&(*text)[tpos++], LPAREN);
      } else {
#endif
        (*text)[tpos++] = LPAREN;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
#endif
      pos++;
    } else if(current == ')') {
      nrtokens++;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      if(is_mmu_input == 1) {
        mmu_set_uint8(&(*text)[tpos++], RPAREN);
      } else {
#endif
        (*text)[tpos++] = RPAREN;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
#endif
      pos++;
    } else if(current == '=' && next != '=') {
      nrtokens++;
      pos++;
      mmu_set_uint8(&(*text)[tpos++], TASSIGN);
    } else if(current == ';') {
      nrtokens++;
      /*
       * An additional TTRUE slot
       */
      trueslots++;
#ifdef DEBUG
      printf("TTRUE: %lu\n", sizeof(uint16_t));
#endif
      pos++;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      if(is_mmu_input == 1) {
        mmu_set_uint8(&(*text)[tpos++], TSEMICOLON);
      } else {
#endif
        (*text)[tpos++] = TSEMICOLON;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
#endif
    } else {
      uint16_t a = 0, b = *len-(pos)-1;
      char chr = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      if(is_mmu_input == 1) {
        chr = mmu_get_uint8(&(*text)[a]);
      } else {
#endif
        chr = (*text)[a];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
#endif
      int16_t len1 = 0;
      for(a=(pos);a<*len;a++) {
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        if(is_mmu_input == 1) {
          chr = mmu_get_uint8(&(*text)[a]);
        } else {
#endif
          chr = (*text)[a];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        }
#endif
        if(chr == ' ' || chr == '(' || chr == ')' || chr == ',' || chr == ';') {
          b = a-(pos);
          break;
        }
      }
      char cpy[b+1];
      memset(&cpy, 0, b+1);
      for(a=0;a<b;a++) {
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        if(is_mmu_input == 1) {
          cpy[a] = mmu_get_uint8(&(*text)[pos+a]);
        } else {
#endif
          cpy[a] = (*text)[pos+a];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        }
#endif
      }

      if((len1 = is_function((*text), &pos, b)) > -1) {
        nrtokens++;
        *nrbytes += sizeof(struct vm_tfunction_t);
        if(sizeof(struct vm_tfunction_t) % 4 == 0) {
          *nrbytes += sizeof(uint32_t);
        } else {
          *nrbytes += sizeof(uint16_t);
        }
        *nrbytes -= sizeof(struct vm_lparen_t);

#ifdef DEBUG
        printf("TFUNCTION: %lu\n", sizeof(struct vm_tfunction_t)+sizeof(uint16_t));
#endif

#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
       if(is_mmu_input == 1) {
          mmu_set_uint8(&(*text)[tpos++], TFUNCTION);
          mmu_set_uint8(&(*text)[tpos++], len1);
        } else {
#endif
          (*text)[tpos++] = TFUNCTION;
          (*text)[tpos++] = len1;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        }
#endif
        pos += b;
      } else if((len1 = is_operator((*text), &pos, b)) > -1) {
        nrtokens++;
        *nrbytes += sizeof(struct vm_toperator_t);

#ifdef DEBUG
        printf("TOPERATOR: %lu\n", sizeof(struct vm_toperator_t));
#endif
        pos += b;

#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        if(is_mmu_input == 1) {
          mmu_set_uint8(&(*text)[tpos++], TOPERATOR);
          mmu_set_uint8(&(*text)[tpos++], len1);
        } else {
#endif
          (*text)[tpos++] = TOPERATOR;
          (*text)[tpos++] = len1;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        }
#endif
      } else if(rule_options.is_token_cb != NULL && (len1 = rule_options.is_token_cb((char *)&cpy, b)) > -1) {
        *nrbytes += sizeof(struct vm_tvar_t);
        /*
         * Check for double vars
         */

        {
          uint16_t y = 0, start = 0, len = 0;
          uint8_t type = 0, match = 0;
          /*
           * Check if space for this variable
           * was already accounted for. However,
           * don't look past the tokens not
           * already parsed, because that is where
           * the actual unparsed rule lives. This
           * is exactly why we count the nr of
           * tokens while parsing.
           *
           */
          while(lexer_peek(text, y, &type, &start, &len) >= 0 && ++y < nrtokens && match == 0) {
            if(type == TVAR) {
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              if(is_mmu_input == 1) {
                uint16_t a = 0;
                if(len == len1) {
                  for(a=0;a<len;a++) {
                    if(mmu_get_uint8(&(*text)[start+1+a]) != mmu_get_uint8(&(*text)[pos+a])) {
                      break;
                    }
                  }
                  if(a == len1) {
                    match = 1;
                  }
                }
              } else {
#endif
                if(len == len1 && strncmp(&(*text)[start+1], &(*text)[pos], len) == 0) {
                  match = 1;
                  break;
                }
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              }
#endif
            }
          }
          if(match == 0) {
            *nrbytes += sizeof(struct vm_tvalue_t)+align(len1+1, 4);
          }
        }
#ifdef DEBUG
        printf("TVAR: %lu\n", sizeof(struct vm_tvar_t)+align(len1+1, 4));
        printf("TVALUE: %lu\n", sizeof(struct vm_tvar_t)+align(len1+1, 4));
#endif
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        if(is_mmu_input == 1) {
          mmu_set_uint8(&(*text)[tpos++], TVAR);
          uint16_t a = 0;
          for(a=0;a<len1;a++) {
            mmu_set_uint8(&(*text)[tpos+a], mmu_get_uint8(&(*text)[pos+a]));
          }
        } else {
#endif
          (*text)[tpos++] = TVAR;
          uint16_t a = 0;
          for(a=0;a<len1;a++) {
            (*text)[tpos+a] = (*text)[pos+a];
          }
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        }
#endif

        nrtokens++;
        tpos += len1;
        pos += len1;
      } else if(rule_options.is_event_cb != NULL && (len1 = rule_options.is_event_cb(cpy, b)) > -1) {
        nrtokens++;
        char start = 0, end = 0;
        if(pos+b+1 < *len) {
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          if(is_mmu_input == 1) {
            start = mmu_get_uint8(&(*text)[(pos)+b]);
            end = mmu_get_uint8(&(*text)[(pos)+b+1]);
          } else {
#endif
            start = (*text)[(pos)+b];
            end = (*text)[(pos)+b+1];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          }
#endif
        }
        if(start == '(' && end == ')') {
          if(pos < *len) {
            lexer_parse_skip_characters((*text), *len, &pos);
          }
          int16_t s = pos;
          lexer_parse_string((*text), *len, &pos);

          {
            uint16_t len = pos - s;

            *nrbytes += sizeof(struct vm_tevent_t)+align(len+1, 4);
#ifdef DEBUG
            printf("TEVENT: %lu\n", sizeof(struct vm_tevent_t)+len+1);
#endif
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            if(is_mmu_input == 1) {
              mmu_set_uint8(&(*text)[tpos++], TEVENT);
              uint16_t a = 0;
              for(a=0;a<len;a++) {
                mmu_set_uint8(&(*text)[tpos+a], mmu_get_uint8(&(*text)[s+a]));
              }
            } else {
#endif
              (*text)[tpos++] = TEVENT;
              memcpy(&(*text)[tpos], &(*text)[s], len);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            }
#endif

            tpos += len;
          }
          pos += 2;
        } else {
          if((*len - pos) > 5) {
            logprintf_P(F("ERROR: unknown token '%.5s...'"), &(*text)[pos]);
          } else {
            logprintf_P(F("ERROR: unknown token '%.5s'"), &(*text)[pos]);
          }
          return -1;
        }
      } else {
        if((*len - pos) > 5) {
          logprintf_P(F("ERROR: unknown token '%.5s...'"), &(*text)[pos]);
        } else {
          logprintf_P(F("ERROR: unknown token '%.5s'"), &(*text)[pos]);
        }
        return -1;
      }
    }

    if(nrblocks == 0) {
      nrtokens++;
      /*
       * Remove one go slot because of the root
       * if or on block
       *
       * However, this breaks 4 byte alignment
       */
      trueslots--;
      if((sizeof(struct vm_ttrue_t) % 4) == 0) {
        *nrbytes += trueslots*sizeof(uint32_t);
        *nrbytes += funcslots*sizeof(uint32_t);
      } else {
        *nrbytes += trueslots*sizeof(uint16_t);
        *nrbytes += funcslots*sizeof(uint16_t);
      }
      *nrbytes += sizeof(struct vm_teof_t);
#ifdef DEBUG
      printf("TEOF: %lu\n", sizeof(struct vm_teof_t));
#endif

#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      if(is_mmu_input == 1) {
        mmu_set_uint8(&(*text)[tpos++], TEOF);
      } else {
#endif
        (*text)[tpos++] = TEOF;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
#endif
      uint16_t oldpos = pos;
      if(pos < *len) {
        lexer_parse_skip_characters((*text), *len, &pos);
      }
      if(*len == pos) {
        return 0;
      }

      *len = oldpos;
      return 0;
    }
    if(pos < *len) {
      lexer_parse_skip_characters((*text), *len, &pos);
    }
  }

  /*
   * Remove one go slot because of the root
   * if or on block
   *
   * However, this breaks 4 byte alignment
   */
  trueslots--;
  if((sizeof(struct vm_ttrue_t) % 4) == 0) {
    *nrbytes += trueslots*sizeof(uint32_t);
    *nrbytes += funcslots*sizeof(uint32_t);
  } else {
    *nrbytes += trueslots*sizeof(uint16_t);
    *nrbytes += funcslots*sizeof(uint16_t);
  }
  *nrbytes += sizeof(struct vm_teof_t);
#ifdef DEBUG
  printf("TEOF: %lu\n", sizeof(struct vm_teof_t));
#endif
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  if(is_mmu_input == 1) {
    mmu_set_uint8(&(*text)[tpos++], TEOF);
  } else {
#endif
    (*text)[tpos++] = TEOF;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  }
#endif
  return 0;
}

static uint16_t rule_next(struct rule_stack_t *stack, uint16_t *i) {
  uint16_t x = *i, nrbytes = 0;
  uint8_t type = 0;

#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  uint8_t is_mmu = 0;

  if(stack->buffer >= (void *)MMU_SEC_HEAP) {
    is_mmu = 1;
  }

  if(stack >= (void *)MMU_SEC_HEAP) {
    nrbytes = mmu_get_uint16(&stack->nrbytes);
  } else {
#endif
    nrbytes = stack->nrbytes;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  }
#endif

  if(*i >= nrbytes) {
    return 0;
  } else {
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
    if(is_mmu == 1) {
      type = mmu_get_uint8(&stack->buffer[*i]);
    } else {
#endif
      type = stack->buffer[*i];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
    }
#endif
    switch(type) {
      case TSTART: {
        (*i) += sizeof(struct vm_tstart_t);
      } break;
      case TEOF: {
        (*i) += sizeof(struct vm_teof_t);
      } break;
      case VNULL: {
        (*i) += sizeof(struct vm_vnull_t);
      } break;
      case TELSEIF:
      case TIF: {
        (*i) += sizeof(struct vm_tif_t);
      } break;
      case LPAREN: {
        (*i) += sizeof(struct vm_lparen_t);
      } break;
      case TFALSE:
      case TTRUE: {
        struct vm_ttrue_t *node = (struct vm_ttrue_t *)&stack->buffer[*i];
        uint8_t nrgo = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        if(is_mmu == 1) {
          nrgo = mmu_get_uint8(&node->nrgo);
        } else {
#endif
          nrgo = node->nrgo;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        }
#endif
        (*i) += sizeof(struct vm_ttrue_t)+align((sizeof(node->go[0])*nrgo), 4);
      } break;
      case TFUNCTION: {
        struct vm_tfunction_t *node = (struct vm_tfunction_t *)&stack->buffer[*i];
        uint8_t nrgo = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        if(is_mmu == 1) {
          nrgo = mmu_get_uint8(&node->nrgo);
        } else {
#endif
          nrgo = node->nrgo;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        }
#endif
        (*i) += sizeof(struct vm_tfunction_t)+align((sizeof(node->go[0])*nrgo), 4);
      } break;
      case TVAR: {
        (*i) += sizeof(struct vm_tvar_t);
      } break;
      case TVALUE: {
        struct vm_tvalue_t *node = (struct vm_tvalue_t *)&stack->buffer[*i];
        uint16_t len = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        if(is_mmu == 1) {
          while(mmu_get_uint8(&node->token[++len]) != 0);
        } else {
#endif
          len = strlen((char *)node->token);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        }
#endif
        (*i) += sizeof(struct vm_tvalue_t)+align(len+1, 4);
      } break;
      case TEVENT: {
        struct vm_tevent_t *node = (struct vm_tevent_t *)&stack->buffer[*i];
        uint16_t len = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        if(is_mmu == 1) {
          while(mmu_get_uint8(&node->token[++len]) != 0);
        } else {
#endif
          len = strlen((char *)node->token);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        }
#endif
        (*i) += sizeof(struct vm_tevent_t)+align(len+1, 4);
      } break;
      case TNUMBER: {
        struct vm_tnumber_t *node = (struct vm_tnumber_t *)&stack->buffer[*i];
        uint16_t len = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        if(is_mmu == 1) {
          while(mmu_get_uint8(&node->token[++len]) != 0);
        } else {
#endif
          len = strlen((char *)node->token);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        }
#endif
        (*i) += sizeof(struct vm_tnumber_t)+align(len+1, 4);
      } break;
      case VINTEGER: {
        (*i) += sizeof(struct vm_vinteger_t);
      } break;
      case VFLOAT: {
        (*i) += sizeof(struct vm_vfloat_t);
      } break;
      case TOPERATOR: {
        (*i) += sizeof(struct vm_toperator_t);
      } break;
      default: {
      } break;
    }
  }
  uint8_t ret = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  if(is_mmu == 1) {
    ret = mmu_get_uint8(&stack->buffer[x]);
  } else {
#endif
    ret = stack->buffer[x];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  }
#endif
  return ret;
}

static int16_t vm_parent(char **text, struct rules_t *obj, uint8_t type, uint16_t start, uint16_t len, uint16_t opt) {
  uint16_t ret = 0, size = 0, i = 0;

#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  uint8_t is_mmu_ast = 0, is_mmu_buf = 0;

  if((void *)&obj->ast >= (void *)MMU_SEC_HEAP) {
    is_mmu_ast = 1;
  }
  if((void *)obj->ast.buffer >= (void *)MMU_SEC_HEAP) {
    is_mmu_buf = 1;
  }

  if(is_mmu_ast == 1) {
     ret = mmu_get_uint16(&obj->ast.nrbytes);
  } else {
#endif
     ret = obj->ast.nrbytes;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  }
#endif

  switch(type) {
    case TSTART: {
      size = ret+sizeof(struct vm_tstart_t);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      if(is_mmu_ast == 1) {
        assert(size <= mmu_get_uint16(&obj->ast.bufsize));
      } else {
#endif
        assert(size <= obj->ast.bufsize);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
#endif
      struct vm_tstart_t *node = (struct vm_tstart_t *)&obj->ast.buffer[ret];

#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      if(is_mmu_buf == 1) {
        mmu_set_uint8(&node->type, type);
        mmu_set_uint16(&node->go, 0);
        mmu_set_uint16(&node->ret, 0);
      } else {
#endif
        node->type = type;
        node->go = 0;
        node->ret = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
      if(is_mmu_ast == 1) {
        mmu_set_uint16(&obj->ast.nrbytes, size);
      } else {
#endif
        obj->ast.nrbytes = size;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
#endif
    } break;
    case TELSEIF:
    case TIF: {
      size = ret+sizeof(struct vm_tif_t);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      if(is_mmu_ast == 1) {
        assert(size <= mmu_get_uint16(&obj->ast.bufsize));
      } else {
#endif
        assert(size <= obj->ast.bufsize);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
#endif
      struct vm_tif_t *node = (struct vm_tif_t *)&obj->ast.buffer[ret];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      if(is_mmu_buf == 1) {
        mmu_set_uint8(&node->type, type);
        mmu_set_uint16(&node->go, 0);
        mmu_set_uint16(&node->ret, 0);
        mmu_set_uint16(&node->true_, 0);
        mmu_set_uint16(&node->false_, 0);
      } else {
#endif
        node->type = type;
        node->go = 0;
        node->ret = 0;
        node->true_ = 0;
        node->false_ = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
      if(is_mmu_input == 1 && is_mmu_buf == 1) {
        mmu_set_uint8(&node->sync, mmu_get_uint8(&(*text)[start+1]));
      } else if(is_mmu_input == 0 && is_mmu_buf == 1) {
        mmu_set_uint8(&node->sync, (*text)[start+1]);
      } else if(is_mmu_input == 1 && is_mmu_buf == 0) {
        node->sync = mmu_get_uint8(&(*text)[start+1]);
      } else if(is_mmu_input == 0 && is_mmu_buf == 0) {
#endif
        node->sync = (*text)[start+1];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
      if(is_mmu_ast == 1) {
        mmu_set_uint16(&obj->ast.nrbytes, size);
      } else {
#endif
        obj->ast.nrbytes = size;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
#endif
    } break;
    case LPAREN: {
      size = ret+sizeof(struct vm_lparen_t);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      if(is_mmu_ast == 1) {
        assert(size <= mmu_get_uint16(&obj->ast.bufsize));
      } else {
#endif
        assert(size <= obj->ast.bufsize);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
#endif
      struct vm_lparen_t *node = (struct vm_lparen_t *)&obj->ast.buffer[ret];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      if(is_mmu_buf == 1) {
        mmu_set_uint8(&node->type, type);
        mmu_set_uint16(&node->go, 0);
        mmu_set_uint16(&node->ret, 0);
        mmu_set_uint16(&node->value, 0);
      } else {
#endif
        node->type = type;
        node->ret = 0;
        node->go = 0;
        node->value = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
      if(is_mmu_ast == 1) {
        mmu_set_uint16(&obj->ast.nrbytes, size);
      } else {
#endif
        obj->ast.nrbytes = size;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
#endif
    } break;
    case TOPERATOR: {
      size = ret+sizeof(struct vm_toperator_t);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      if(is_mmu_ast == 1) {
        assert(size <= mmu_get_uint16(&obj->ast.bufsize));
      } else {
#endif
        assert(size <= obj->ast.bufsize);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
#endif
      struct vm_toperator_t *node = (struct vm_toperator_t *)&obj->ast.buffer[ret];

#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      if(is_mmu_buf == 1) {
        mmu_set_uint8(&node->type, type);
        mmu_set_uint16(&node->ret, 0);
        mmu_set_uint16(&node->left, 0);
        mmu_set_uint16(&node->right, 0);
        mmu_set_uint16(&node->value, 0);
      } else {
#endif
        node->type = type;
        node->ret = 0;
        node->left = 0;
        node->right = 0;
        node->value = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }

      if(is_mmu_input == 1 && is_mmu_buf == 1) {
        mmu_set_uint8(&node->token, mmu_get_uint8(&(*text)[start+1]));
      } else if(is_mmu_input == 0 && is_mmu_buf == 1) {
        mmu_set_uint8(&node->token, (*text)[start+1]);
      } else if(is_mmu_input == 1 && is_mmu_buf == 0) {
        node->token = mmu_get_uint8(&(*text)[start+1]);
      } else if(is_mmu_input == 0 && is_mmu_buf == 0) {
#endif
        node->token = (*text)[start+1];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
      if(is_mmu_ast == 1) {
        mmu_set_uint16(&obj->ast.nrbytes, size);
      } else {
#endif
        obj->ast.nrbytes = size;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
#endif
    } break;
    case VINTEGER: {
      size = ret+sizeof(struct vm_vinteger_t);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      if(is_mmu_ast == 1) {
        assert(size <= mmu_get_uint16(&obj->ast.bufsize));
      } else {
#endif
        assert(size <= obj->ast.bufsize);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
#endif

      uint32_t tmp = 0;
      struct vm_vinteger_t *value = (struct vm_vinteger_t *)&obj->ast.buffer[ret];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      if(is_mmu_buf == 1) {
        mmu_set_uint8(&value->type, type);
        mmu_set_uint16(&value->ret, 0);
      } else {
#endif
        value->type = type;
        value->ret = ret;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }

      if(is_mmu_input == 1) {
        tmp |= (unsigned char)mmu_get_uint8(&(*text)[start+1]) << 24;
        tmp |= (unsigned char)mmu_get_uint8(&(*text)[start+2]) << 16;
        tmp |= (unsigned char)mmu_get_uint8(&(*text)[start+3]) << 8;
        tmp |= (unsigned char)mmu_get_uint8(&(*text)[start+4]);
      } else {
#endif
        memcpy(&tmp, &(*text)[start+1], sizeof(uint32_t));
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
      if(is_mmu_buf == 1) {
        value->value = tmp;
      } else {
#endif
        memcpy(&value->value, &tmp, sizeof(uint32_t));
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
      if(is_mmu_ast == 1) {
        mmu_set_uint16(&obj->ast.nrbytes, size);
      } else {
#endif
        obj->ast.nrbytes = size;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
#endif
    } break;
    case VFLOAT: {
      size = ret+sizeof(struct vm_vfloat_t);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      if(is_mmu_ast == 1) {
        assert(size <= mmu_get_uint16(&obj->ast.bufsize));
      } else {
#endif
        assert(size <= obj->ast.bufsize);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
#endif

      uint32_t tmp = 0;
      struct vm_vfloat_t *value = (struct vm_vfloat_t *)&obj->ast.buffer[ret];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      if(is_mmu_buf == 1) {
        mmu_set_uint8(&value->type, type);
        mmu_set_uint16(&value->ret, 0);
      } else {
#endif
        value->type = type;
        value->ret = ret;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
      if(is_mmu_input == 1) {
        tmp |= (unsigned char)mmu_get_uint8(&(*text)[start+1]) << 24;
        tmp |= (unsigned char)mmu_get_uint8(&(*text)[start+2]) << 16;
        tmp |= (unsigned char)mmu_get_uint8(&(*text)[start+3]) << 8;
        tmp |= (unsigned char)mmu_get_uint8(&(*text)[start+4]);
      } else {
#endif
        memcpy(&tmp, &(*text)[start+1], sizeof(uint32_t));
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
      if(is_mmu_buf == 1) {
        value->value = tmp;
      } else {
#endif
        memcpy(&value->value, &tmp, sizeof(uint32_t));
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
      if(is_mmu_ast == 1) {
        mmu_set_uint16(&obj->ast.nrbytes, size);
      } else {
#endif
        obj->ast.nrbytes = size;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
#endif
    } break;
    case TNUMBER1:
    case TNUMBER2:
    case TNUMBER3: {
      char tmp = 0;
      float var = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      if(is_mmu_input == 1) {
        tmp = mmu_get_uint8(&(*text)[start+1+len]);
        mmu_set_uint8(&(*text)[start+1+len], 0);
        char cpy[len+1];
        memset(&cpy, 0, len+1);
        for(uint16_t x=0;x<len;x++) {
          cpy[x] = mmu_get_uint8(&(*text)[start+1+x]);
        }
        var = atof(cpy);
      } else {
#endif
        tmp = (*text)[start+1+len];
        (*text)[start+1+len] = 0;
        var = atof((char *)&(*text)[start+1]);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
#endif
      float nr = 0;
      if(modff(var, &nr) == 0) {
        /*
         * This range of integers
         * take less bytes when stored
         * as ascii characters.
         */
        if(var < 100 && var > -9) {
          size = ret+sizeof(struct vm_tnumber_t)+align(len+1, 4);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          if(is_mmu_ast == 1) {
            assert(size <= mmu_get_uint16(&obj->ast.bufsize));
          } else {
#endif
            assert(size <= obj->ast.bufsize);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          }
#endif
          struct vm_tnumber_t *node = (struct vm_tnumber_t *)&obj->ast.buffer[ret];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          if(is_mmu_buf == 1) {
            mmu_set_uint8(&node->type, TNUMBER);
            mmu_set_uint16(&node->ret, 0);
          } else {
#endif
            node->type = TNUMBER;
            node->ret = ret;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          }
          if(is_mmu_input == 1 && is_mmu_buf == 1) {
            for(uint16_t x=0;x<len;x++) {
              mmu_set_uint8(&node->token[x], mmu_get_uint8(&(*text)[start+1+x]));
            }
          } else if(is_mmu_input == 0 && is_mmu_buf == 1) {
            for(uint16_t x=0;x<len;x++) {
              mmu_set_uint8(&node->token[x], (*text)[start+1+x]);
            }
          } else if(is_mmu_input == 1 && is_mmu_buf == 0) {
            for(uint16_t x=0;x<len;x++) {
              node->token[x] = mmu_get_uint8(&(*text)[start+1+x]);
            }
          } else if(is_mmu_input == 0 && is_mmu_buf == 0) {
#endif
            memcpy(node->token, &(*text)[start+1], len);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          }
          if(is_mmu_ast == 1) {
            mmu_set_uint16(&obj->ast.nrbytes, size);
          } else {
#endif
            obj->ast.nrbytes = size;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          }
#endif
        } else {
          size = ret+sizeof(struct vm_vinteger_t);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          if(is_mmu_buf == 1) {
            assert(size <= mmu_get_uint16(&obj->ast.bufsize));
          } else {
#endif
            assert(size <= obj->ast.bufsize);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          }
#endif
          struct vm_vinteger_t *node = (struct vm_vinteger_t *)&obj->ast.buffer[ret];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          if(is_mmu_buf == 1) {
            mmu_set_uint8(&node->type, VINTEGER);
            mmu_set_uint16(&node->ret, ret);
          } else {
#endif
            node->type = VINTEGER;
            node->ret = ret;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          }
#endif
          node->value = var;
        }
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        if(is_mmu_ast == 1) {
          mmu_set_uint16(&obj->ast.nrbytes, size);
        } else {
#endif
          obj->ast.nrbytes = size;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        }
#endif
      } else {
        size = ret+sizeof(struct vm_vfloat_t);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        if(is_mmu_ast == 1) {
          assert(size <= mmu_get_uint16(&obj->ast.bufsize));
        } else {
#endif
          assert(size <= obj->ast.bufsize);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        }
#endif
        struct vm_vfloat_t *node = (struct vm_vfloat_t *)&obj->ast.buffer[ret];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        if(is_mmu_buf == 1) {
          mmu_set_uint8(&node->type, VFLOAT);
          mmu_set_uint16(&node->ret, ret);
        } else {
#endif
          node->type = VFLOAT;
          node->ret = ret;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        }
#endif

        float2uint32(var, &node->value);

#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        if(is_mmu_ast == 1) {
          mmu_set_uint16(&obj->ast.nrbytes, size);
        } else {
#endif
          obj->ast.nrbytes = size;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        }
#endif
      }
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      if(is_mmu_input == 1) {
        mmu_set_uint8(&(*text)[start+1+len], tmp);
      } else {
#endif
        (*text)[start+1+len] = tmp;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
#endif
    } break;
    case TFALSE:
    case TTRUE: {
      struct vm_ttrue_t *node = (struct vm_ttrue_t *)&obj->ast.buffer[ret];
      size = ret+sizeof(struct vm_ttrue_t)+align((sizeof(node->go[0])*opt), 4);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      if(is_mmu_ast == 1) {
        assert(size <= mmu_get_uint16(&obj->ast.bufsize));
      } else {
#endif
        assert(size <= obj->ast.bufsize);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
      if(is_mmu_buf == 1) {
        mmu_set_uint8(&node->type, type);
        mmu_set_uint16(&node->ret, ret);
        mmu_set_uint8(&node->nrgo, opt);
        for(i=0;i<opt;i++) {
          mmu_set_uint16(&node->go[i], 0);
        }
      } else {
#endif
        node->type = type;
        node->ret = ret;
        node->nrgo = opt;
        for(i=0;i<opt;i++) {
          node->go[i] = 0;
        }
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
      if(is_mmu_ast == 1) {
        mmu_set_uint16(&obj->ast.nrbytes, size);
      } else {
#endif
        obj->ast.nrbytes = size;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
#endif
    } break;
    case TFUNCTION: {
      struct vm_tfunction_t *node = (struct vm_tfunction_t *)&obj->ast.buffer[ret];
      size = ret+sizeof(struct vm_tfunction_t)+align((sizeof(node->go[0])*opt), 4);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      if(is_mmu_ast == 1) {
        assert(size <= mmu_get_uint16(&obj->ast.bufsize));
      } else {
#endif
        assert(size <= obj->ast.bufsize);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
      if(is_mmu_buf == 1) {
        mmu_set_uint8(&node->type, type);
        mmu_set_uint16(&node->ret, ret);
        mmu_set_uint8(&node->nrgo, opt);
        for(i=0;i<opt;i++) {
          mmu_set_uint16(&node->go[i], 0);
        }
      } else {
#endif
        node->type = type;
        node->ret = ret;
        node->nrgo = opt;
        for(i=0;i<opt;i++) {
          node->go[i] = 0;
        }
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
      if(is_mmu_input == 1 && is_mmu_buf == 1) {
        mmu_set_uint8(&node->token, mmu_get_uint8(&(*text)[start+1]));
      } else if(is_mmu_input == 0 && is_mmu_buf == 1) {
        mmu_set_uint8(&node->token, (*text)[start+1]);
      } else if(is_mmu_input == 1 && is_mmu_buf == 0) {
        node->token = mmu_get_uint8(&(*text)[start+1]);
      } else if(is_mmu_input == 0 && is_mmu_buf == 0) {
#endif
        node->token = (*text)[start+1];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
      if(is_mmu_ast == 1) {
        mmu_set_uint16(&obj->ast.nrbytes, size);
      } else {
#endif
        obj->ast.nrbytes = size;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
#endif
    } break;
    case VNULL: {
      size = ret+sizeof(struct vm_vnull_t);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      if(is_mmu_ast == 1) {
        assert(size <= mmu_get_uint16(&obj->ast.bufsize));
      } else {
#endif
        assert(size <= obj->ast.bufsize);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
#endif
      struct vm_vnull_t *node = (struct vm_vnull_t *)&obj->ast.buffer[ret];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      if(is_mmu_buf == 1) {
        mmu_set_uint8(&node->type, type);
        mmu_set_uint8(&node->ret, 0);
      } else {
#endif
        node->type = type;
        node->ret = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
      if(is_mmu_ast == 1) {
        mmu_set_uint16(&obj->ast.nrbytes, size);
      } else {
#endif
        obj->ast.nrbytes = size;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
#endif
    } break;
    case TVAR: {
      size = ret+sizeof(struct vm_tvar_t);

#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      if(is_mmu_ast == 1) {
        assert(size <= mmu_get_uint16(&obj->ast.bufsize));
      } else {
#endif
        assert(size <= obj->ast.bufsize);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
#endif
      struct vm_tvar_t *node = (struct vm_tvar_t *)&obj->ast.buffer[ret];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      if(is_mmu_buf == 1) {
        mmu_set_uint8(&node->type, type);
        mmu_set_uint16(&node->ret, 0);
        mmu_set_uint16(&node->go, 0);
        mmu_set_uint16(&node->value, 0);
      } else {
#endif
        node->type = type;
        node->ret = ret;
        node->go = 0;
        node->value = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
      if(is_mmu_ast == 1) {
        mmu_set_uint16(&obj->ast.nrbytes, size);
      } else {
#endif
        obj->ast.nrbytes = size;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
#endif
    } break;
    case TVALUE: {
      {
        uint16_t i = 0, x = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        uint16_t a = 0;
#endif
        uint8_t type = 0;
        while((type = rule_next(&obj->ast, &i)) > 0) {
          if(type == TVALUE) {
            struct vm_tvalue_t *node = (struct vm_tvalue_t *)&obj->ast.buffer[x];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            if(is_mmu_input == 1 && is_mmu_buf == 1) {
              for(a=0;a<len;a++) {
                if(mmu_get_uint8(&node->token[a]) != mmu_get_uint8(&(*text)[start+1+a])) {
                  break;
                }
              }
              if(a == len) {
                return x;
              }              
            } else if(is_mmu_input == 0 && is_mmu_buf == 1) {
              for(a=0;a<len;a++) {
                if(mmu_get_uint8(&node->token[a]) != (*text)[start+1+a]) {
                  break;
                }
              }
              if(a == len) {
                return x;
              }
            } else if(is_mmu_input == 1 && is_mmu_buf == 0) {
              for(a=0;a<len;a++) {
                if(node->token[a] != mmu_get_uint8(&(*text)[start+1+a])) {
                  break;
                }
              }
              if(a == len) {
                return x;
              }
            } else if(is_mmu_input == 0 && is_mmu_buf == 0) {
#endif
              if(strlen((char *)node->token) == len && strncmp((char *)node->token, &(*text)[start+1], len) == 0) {
                return x;
              }
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            }
#endif
          }
          x = i;
        }
      }
      size = ret+sizeof(struct vm_tvalue_t)+align(len+1, 4);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      if(is_mmu_ast == 1) {
        assert(size <= mmu_get_uint16(&obj->ast.bufsize));
      } else {
#endif
        assert(size <= obj->ast.bufsize);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
#endif
      struct vm_tvalue_t *node = (struct vm_tvalue_t *)&obj->ast.buffer[ret];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      if(is_mmu_buf == 1) {
        mmu_set_uint8(&node->type, type);
        mmu_set_uint16(&node->ret, 0);
        mmu_set_uint16(&node->go, 0);
      } else {
#endif
        node->type = type;
        node->ret = ret;
        node->go = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }

      if(is_mmu_input == 1 && is_mmu_buf == 1) {
        for(uint16_t x=0;x<len;x++) {
          mmu_set_uint8(&node->token[x], mmu_get_uint8(&(*text)[start+1+x]));
        }
      } else if(is_mmu_input == 0 && is_mmu_buf == 1) {
        for(uint16_t x=0;x<len;x++) {
          mmu_set_uint8(&node->token[x], (*text)[start+1+x]);
        }
      } else if(is_mmu_input == 1 && is_mmu_buf == 0) {
        for(uint16_t x=0;x<len;x++) {
          node->token[x] = mmu_get_uint8(&(*text)[start+1+x]);
        }
      } else if(is_mmu_input == 0 && is_mmu_buf == 0) {
#endif
        memcpy(node->token, &(*text)[start+1], len);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
      if(is_mmu_ast == 1) {
        mmu_set_uint16(&obj->ast.nrbytes, size);
      } else {
#endif
        obj->ast.nrbytes = size;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
#endif
    } break;
    case TEVENT: {
      size = ret+sizeof(struct vm_tevent_t)+align(len+1, 4);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      if(is_mmu_ast == 1) {
        assert(size <= mmu_get_uint16(&obj->ast.bufsize));
      } else {
#endif
        assert(size <= obj->ast.bufsize);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
#endif
      struct vm_tevent_t *node = (struct vm_tevent_t *)&obj->ast.buffer[ret];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      if(is_mmu_buf == 1) {
        mmu_set_uint8(&node->type, type);
        mmu_set_uint16(&node->ret, 0);
        mmu_set_uint16(&node->go, 0);
      } else {
#endif
        node->type = type;
        node->ret = 0;
        node->go = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }

      if(is_mmu_input == 1 && is_mmu_buf == 1) {
        for(uint16_t x=0;x<len;x++) {
          mmu_set_uint8(&node->token[x], mmu_get_uint8(&(*text)[start+1+x]));
        }
      } else if(is_mmu_input == 0 && is_mmu_buf == 1) {
        for(uint16_t x=0;x<len;x++) {
          mmu_set_uint8(&node->token[x], (*text)[start+1+x]);
        }
      } else if(is_mmu_input == 1 && is_mmu_buf == 0) {
        for(uint16_t x=0;x<len;x++) {
          node->token[x] = mmu_get_uint8(&(*text)[start+1+x]);
        }
      } else if(is_mmu_input == 0 && is_mmu_buf == 0) {
#endif
        memcpy(node->token, &(*text)[start+1], len);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
      if(is_mmu_ast == 1) {
        mmu_set_uint16(&obj->ast.nrbytes, size);
      } else {
#endif
        obj->ast.nrbytes = size;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
#endif
    } break;
    case TEOF: {
      size = ret+sizeof(struct vm_teof_t);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      if(is_mmu_ast == 1) {
        assert(size <= mmu_get_uint16(&obj->ast.bufsize));
      } else {
#endif
        assert(size <= obj->ast.bufsize);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
#endif
      struct vm_teof_t *node = (struct vm_teof_t *)&obj->ast.buffer[ret];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      if(is_mmu_buf == 1) {
        mmu_set_uint8(&node->type, type);
      } else {
#endif
        node->type = type;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
      if(is_mmu_ast == 1) {
        mmu_set_uint16(&obj->ast.nrbytes, size);
      } else {
#endif
        obj->ast.nrbytes = size;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
#endif
    } break;
    default: {
      return -1;
    } break;
  }

  return ret;
}

static int16_t vm_rewind3(struct rules_t *obj, int16_t step, uint8_t type, uint8_t type2, uint8_t type3) {
  uint16_t tmp = step;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  uint8_t is_mmu = 0;

  if(obj->ast.buffer >= (void *)MMU_SEC_HEAP) {
    is_mmu = 1;
  }
#endif
  while(1) {
    uint8_t tmp_type = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
    if(is_mmu == 1) {
      tmp_type = mmu_get_uint8(&obj->ast.buffer[tmp]);
    } else {
#endif
      tmp_type = obj->ast.buffer[tmp];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
    }
#endif
    if(tmp_type == type || (type2 > -1 && tmp_type == type2) || (type3 > -1 && tmp_type == type3)) {
      return tmp;
    } else {
      switch(tmp_type) {
        case TSTART: {
          return 0;
        } break;
        case TELSEIF:
        case TIF:
        case TEVENT:
        case LPAREN:
        case TFALSE:
        case TTRUE:
        case TVAR:
        case TOPERATOR: {
          struct vm_tgeneric_t *node = (struct vm_tgeneric_t *)&obj->ast.buffer[tmp];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          if(is_mmu == 1) {
            tmp = mmu_get_uint16(&node->ret);
          } else {
#endif
            tmp = node->ret;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          }
#endif
        } break;
        /* LCOV_EXCL_START*/
        case TNUMBER1:
        case TNUMBER2:
        case TNUMBER3:
        case VINTEGER:
        case VFLOAT:
        case VNULL:
        default: {
          logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
          return -1;
        } break;
        /* LCOV_EXCL_STOP*/
      }
    }
  }

  return 0;
}

static int16_t vm_rewind2(struct rules_t *obj, int16_t step, uint8_t type, uint8_t type2) {
  return vm_rewind3(obj, step, type, type2, -1);
}

static int16_t vm_rewind(struct rules_t *obj, int16_t step, uint8_t type) {
  return vm_rewind2(obj, step, type, -1);
}

static void vm_cache_add(int type, int step, int start, int end) {
  if((vmcache = (struct vm_cache_t **)REALLOC(vmcache, sizeof(struct vm_cache_t *)*((nrcache)+1))) == NULL) {
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
  uint16_t x = 0, y = 0;
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

static struct vm_cache_t *vm_cache_get(int type, int start) {
  uint16_t x = 0;
  for(x=0;x<nrcache;x++) {
    if(vmcache[x]->type == type && vmcache[x]->start == start) {
      return vmcache[x];
    }
  }
  return NULL;
}

static void vm_cache_gc(void) {
  uint16_t i = 0;
  /* LCOV_EXCL_START*/
  for(i=0;i<nrcache;i++) {
    FREE(vmcache[i]);
    vmcache[i] = NULL;
  }
  /* LCOV_EXCL_STOP*/
  FREE(vmcache);
  vmcache = NULL;
  nrcache = 0;
}

static int16_t lexer_parse_math_order(char **text, struct rules_t *obj, int16_t type, uint16_t *pos, int16_t *step_out, int16_t offset, int16_t source) {
  uint16_t start = 0, len = 0;
  int16_t step = 0;
  uint8_t first = 1, b = 0, c = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  uint8_t is_mmu = 0;

  if(obj->ast.buffer >= (void *)MMU_SEC_HEAP) {
    is_mmu = 1;
  }
#endif

  while(1) {
    int right = 0;
    if(lexer_peek(text, (*pos), &b, &start, &len) < 0 || b != TOPERATOR) {
      break;
    }
    step = vm_parent(text, obj, b, start, len, 0);
    (*pos)++;
    if(lexer_peek(text, (*pos), &c, &start, &len) >= 0) {
      switch(c) {
        case LPAREN: {
          int oldpos = (*pos);
          struct vm_cache_t *x = vm_cache_get(LPAREN, (*pos));
          /* LCOV_EXCL_START*/
          if(x == NULL) {
            logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
            return -1;
          }
          /* LCOV_EXCL_STOP*/
          (*pos) = x->end;
          right = x->step;
          vm_cache_del(oldpos);
        } break;
        case TFUNCTION: {
          int oldpos = (*pos);
          struct vm_cache_t *x = vm_cache_get(TFUNCTION, (*pos));
          /* LCOV_EXCL_START*/
          if(x == NULL) {
            logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
            return -1;
          }
          /* LCOV_EXCL_STOP*/
          (*pos) = x->end;
          right = x->step;
          vm_cache_del(oldpos);
        } break;
        case TVAR:
        case VNULL:
        case TNUMBER1:
        case TNUMBER2:
        case TNUMBER3:
        case VINTEGER:
        case VFLOAT: {
          right = vm_parent(text, obj, c, start, len, 0);
          (*pos)++;

          if(c == TVAR) {
            uint16_t ptr = vm_parent(text, obj, TVALUE, start, len, 0);
            struct vm_tvar_t *node = (struct vm_tvar_t *)&obj->ast.buffer[right];

#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            if(is_mmu == 1) {
              mmu_set_uint16(&node->value, ptr);
            } else {
#endif
              node->value = ptr;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            }
#endif
          }
        } break;
        default: {
          logprintf_P(F("ERROR: Expected a parenthesis block, function, number or variable"));
          return -1;
        } break;
      }
    }

    struct vm_tgeneric_t *node = (struct vm_tgeneric_t *)&obj->ast.buffer[right];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
    if(is_mmu == 1) {
      mmu_set_uint16(&node->ret, step);
    } else {
#endif
      node->ret = step;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
    }
#endif

    struct vm_toperator_t *op2 = (struct vm_toperator_t *)&obj->ast.buffer[step];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
    if(is_mmu == 1) {
      mmu_set_uint16(&op2->left, *step_out);
      mmu_set_uint16(&op2->right, right);
    } else {
#endif
      op2->left = *step_out;
      op2->right = right;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
    }
#endif

    {
      uint8_t type = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      if(is_mmu == 1) {
        type = mmu_get_uint8(&obj->ast.buffer[*step_out]);
      } else {
#endif
        type = obj->ast.buffer[*step_out];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
#endif
      switch(type) {
        case TNUMBER:
        case TNUMBER1:
        case TNUMBER2:
        case TNUMBER3:
        case VINTEGER:
        case VFLOAT:
        case LPAREN:
        case TOPERATOR:
        case TVAR:
        case TFUNCTION:
        case VNULL: {
          struct vm_tgeneric_t *node = (struct vm_tgeneric_t *)&obj->ast.buffer[*step_out];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          if(is_mmu == 1) {
            mmu_set_uint16(&node->ret, step);
          } else {
#endif
            node->ret = step;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          }
#endif
        } break;
        /* LCOV_EXCL_START*/
        default: {
          logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
          return -1;
        } break;
        /* LCOV_EXCL_STOP*/
      }
    }

    struct vm_toperator_t *op1 = NULL;

    if(type == LPAREN) {
      if(first == 1) {
        struct vm_tgeneric_t *node = (struct vm_tgeneric_t *)&obj->ast.buffer[*step_out];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        if(is_mmu == 1) {
          mmu_set_uint16(&node->ret, step);
        } else {
#endif
          node->ret = step;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        }
#endif
      }
    } else if(type == TOPERATOR) {
      if(first == 1) {
        struct vm_tgeneric_t *node = (struct vm_tgeneric_t *)&obj->ast.buffer[step];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        if(is_mmu == 1) {
          mmu_set_uint16(&node->ret, source);
        } else {
#endif
          node->ret = source;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        }

        if(is_mmu == 1) {
          mmu_set_uint16(&op2->ret, step);
        } else {
#endif
          op2->ret = step;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        }
#endif

        {
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          if(is_mmu == 1) {
            type = mmu_get_uint8(&obj->ast.buffer[source]);
          } else {
#endif
            type = obj->ast.buffer[source];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          }
#endif
          if(type == TIF) {
            struct vm_tif_t *node = (struct vm_tif_t *)&obj->ast.buffer[source];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            if(is_mmu == 1) {
              mmu_set_uint16(&node->ret, step);
            } else {
#endif
              node->ret = step;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            }
#endif
          }
        }
      }
    }

    uint8_t step_type = 0;
    uint8_t step_out_type = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
    if(is_mmu == 1) {
      step_type = mmu_get_uint8(&obj->ast.buffer[step]);
      step_out_type = mmu_get_uint8(&obj->ast.buffer[*step_out]);
    } else {
#endif
      step_type = obj->ast.buffer[step];
      step_out_type = obj->ast.buffer[*step_out];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
    }
#endif

    if(step_type == TOPERATOR && step_out_type == TOPERATOR) {
      struct vm_toperator_t *op3 = NULL;
      op1 = (struct vm_toperator_t *)&obj->ast.buffer[*step_out];

      uint8_t idx1 = 0;
      uint8_t idx2 = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      if(is_mmu == 1) {
        idx1 = mmu_get_uint8(&op1->token);
        idx2 = mmu_get_uint8(&op2->token);
      } else {
#endif
        idx1 = op1->token;
        idx2 = op2->token;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
#endif

      uint8_t x = rule_operators[idx1].precedence;
      uint8_t y = rule_operators[idx2].precedence;
      uint8_t a = rule_operators[idx2].associativity;

      if(y > x || (x == y && a == 2)) {
        if(a == 1) {
          uint16_t left = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          if(is_mmu == 1) {
            mmu_set_uint16(&op2->left, mmu_get_uint16(&op1->right));
            left = mmu_get_uint16(&op2->left);
          } else {
#endif
            op2->left = op1->right;
            left = op2->left;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          }
#endif

          op3 = (struct vm_toperator_t *)&obj->ast.buffer[left];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          if(is_mmu == 1) {
            mmu_set_uint16(&op3->ret, step);
            mmu_set_uint16(&op1->right, step);
            mmu_set_uint16(&op2->ret, *step_out);
          } else {
#endif
            op3->ret = step;
            op1->right = step;
            op2->ret = *step_out;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          }
#endif
        } else {
          /*
           * Find the last operator with an operator
           * as the last factor.
           */
          uint16_t tmp = 0;
          uint8_t tmp_type = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          if(is_mmu == 1) {
            tmp = mmu_get_uint16(&op1->right);
            tmp_type = mmu_get_uint8(&obj->ast.buffer[tmp]);
          } else {
#endif
            tmp = op1->right;
            tmp_type = obj->ast.buffer[tmp];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          }
#endif

          if(tmp_type != LPAREN &&
             tmp_type != TFUNCTION &&
             tmp_type != TNUMBER &&
             tmp_type != VFLOAT &&
             tmp_type != VINTEGER &&
             tmp_type != VNULL) {
            while(tmp_type == TOPERATOR) {
              uint8_t right_type = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              if(is_mmu == 1) {
                uint16_t right = mmu_get_uint16(&((struct vm_toperator_t *)&obj->ast.buffer[tmp])->right);
                right_type = mmu_get_uint8(&obj->ast.buffer[right]);
              } else {
#endif
                right_type = (obj->ast.buffer[((struct vm_toperator_t *)&obj->ast.buffer[tmp])->right]);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              }
#endif

              if(right_type == TOPERATOR) {
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                if(is_mmu == 1) {
                  tmp = mmu_get_uint16(&((struct vm_toperator_t *)&obj->ast.buffer[tmp])->right);
                  tmp_type = mmu_get_uint8(&obj->ast.buffer[tmp]);
                } else {
#endif
                  tmp = ((struct vm_toperator_t *)&obj->ast.buffer[tmp])->right;
                  tmp_type = obj->ast.buffer[tmp];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                }
#endif
              } else {
                break;
              }
            }
          } else {
            tmp = *step_out;
          }

          op3 = (struct vm_toperator_t *)&obj->ast.buffer[tmp];
          uint16_t tright = 0;
          uint8_t tright_type = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          if(is_mmu == 1) {
            tright = mmu_get_uint16(&op3->right);
            mmu_set_uint16(&op3->right, step);
            tright_type = mmu_get_uint8(&obj->ast.buffer[tright]);
          } else {
#endif
            tright = op3->right;
            op3->right = step;
            tright_type = obj->ast.buffer[tright];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          }
#endif

          switch(tright_type) {
            case TNUMBER:
            case VINTEGER:
            case VFLOAT:
            case TFUNCTION:
            case LPAREN: {
              struct vm_tgeneric_t *node = (struct vm_tgeneric_t *)&obj->ast.buffer[tright];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              if(is_mmu == 1) {
                mmu_set_uint16(&node->ret, step);
              } else {
#endif
                node->ret = step;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              }
#endif
            } break;
            /* LCOV_EXCL_START*/
            default: {
              logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
              return -1;
            } break;
            /* LCOV_EXCL_STOP*/
          }
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          if(is_mmu == 1) {
            mmu_set_uint16(&op2->left, tright);
            mmu_set_uint16(&op2->ret, tmp);
          } else {
#endif
            op2->left = tright;
            op2->ret = tmp;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          }
#endif
        }
        step = *step_out;
      } else {
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        if(is_mmu == 1) {
          mmu_set_uint16(&op1->ret, step);
        } else {
#endif
          op1->ret = step;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        }
#endif
        *step_out = step;
      }
    }

    *step_out = step;

    first = 0;
  }

  return step;
}

static int16_t rule_parse(char **text, struct rules_t *obj) {
  uint16_t step = 0, start = 0, len = 0, pos = 0;
  int16_t go = -1, step_out = -1, loop = 1, startnode = 0, r_rewind = -1;
  int16_t has_elseif = -1, offset = 0, has_paren = -1, has_function = -1;
  int16_t has_if = -1, has_on = -1;
  uint8_t type = 0, type1 = 0;

#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  uint8_t is_mmu = 0;

  if((void *)obj->ast.buffer >= (void *)MMU_SEC_HEAP) {
    is_mmu = 1;
  }
#endif

  /* LCOV_EXCL_START*/
  if(lexer_peek(text, 0, &type, &start, &len) < 0) {
    logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
    return -1;
  }
  /* LCOV_EXCL_STOP*/

  if(type != TIF && type != TEVENT) {
    logprintf_P(F("ERROR: Expected an 'if' or an 'on' statement"));
    return -1;
  }

  startnode = vm_parent(text, obj, TSTART, 0, 0, 0);

  while(loop) {
#ifdef ESP8266
    delay(0);
#endif
    if(go > -1) {
#ifdef DEBUG
      printf("%s %d %d\n", __FUNCTION__, __LINE__, go);
#endif
      switch(go) {
        /* LCOV_EXCL_START*/
        case TSTART: {
        /*
         * This should never be reached, see
         * the go = TSTART comment below.
         */
        // loop = 0;
        logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
        return -1;
        /* LCOV_EXCL_STOP*/
        } break;
        case TEVENT: {
          uint16_t start1 = 0, len1 = 0;
          if(lexer_peek(text, pos, &type, &start, &len) < 0) {
            /* LCOV_EXCL_START*/
            logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
            return -1;
            /* LCOV_EXCL_STOP*/
          }

          uint16_t step_out_type = 0;
          if(step_out >= 0) {
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            if(is_mmu == 1) {
              step_out_type = mmu_get_uint8(&obj->ast.buffer[step_out]);
            } else {
#endif
              step_out_type = obj->ast.buffer[step_out];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            }
#endif
          }
          if((step_out == -1 || step_out_type == TTRUE) && type != TTHEN) {
            if(type == TEVENT) {
              if(offset > 0) {
                logprintf_P(F("ERROR: nested 'on' block"));
                return -1;
              }

              if(lexer_peek(text, pos+1, &type, &start1, &len1) >= 0 && type == TTHEN) {
                pos++;
                step_out = vm_parent(text, obj, TEVENT, start, len, 0);
                pos++;

                /*
                 * Predict how many go slots we need to
                 * reserve for the TRUE / FALSE nodes.
                 */
                int16_t y = pos, nrexpressions = 0;
                while(lexer_peek(text, y++, &type, &start, &len) >= 0) {
                  if(type == TEOF) {
                    break;
                  }
                  if(type == TIF) {
                    struct vm_cache_t *cache = vm_cache_get(TIF, y-1);
                    nrexpressions++;
                    y = cache->end;
                    continue;
                  }
                  if(type == TELSEIF) {
                    struct vm_cache_t *cache = vm_cache_get(TELSEIF, y-1);
                    nrexpressions++;
                    y = cache->end;
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
                  logprintf_P(F("ERROR: On block without body"));
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
                step = vm_parent(text, obj, TTRUE, start, len, nrexpressions);

                struct vm_ttrue_t *a = (struct vm_ttrue_t *)&obj->ast.buffer[step];
                struct vm_tevent_t *b = (struct vm_tevent_t *)&obj->ast.buffer[step_out];

#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                if(is_mmu == 1) {
                  mmu_set_uint16(&b->go, step);
                  mmu_set_uint16(&a->ret, step_out);
                } else {
#endif
                  b->go = step;
                  a->ret = step_out;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                }
#endif

                go = TEVENT;

                step_out = step;
              } else if(type == TSEMICOLON) {
                struct vm_tevent_t *node = NULL;
                /* LCOV_EXCL_STOP*/

                pos++;

                step = vm_parent(text, obj, TEVENT, start, len, 0);

                node = (struct vm_tevent_t *)&obj->ast.buffer[step];

                pos++;

                int16_t tmp = vm_rewind2(obj, step_out, TTRUE, TFALSE);
                struct vm_ttrue_t *node1 = (struct vm_ttrue_t *)&obj->ast.buffer[tmp];

                uint8_t x = 0, nrgo = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                if(is_mmu == 1) {
                  nrgo = mmu_get_uint8(&node1->nrgo);
                } else {
#endif
                  nrgo = node1->nrgo;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                }
#endif
                for(x=0;x<nrgo;x++) {
                  uint16_t go = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                  if(is_mmu == 1) {
                    go = mmu_get_uint16(&node1->go[x]);
                  } else {
#endif
                    go = node1->go[x];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                  }
#endif
                  if(go == 0) {
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                    if(is_mmu == 1) {
                      mmu_set_uint16(&node1->go[x], step);
                    } else {
#endif
                      node1->go[x] = step;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                    }
#endif
                    break;
                  }
                }

                /* LCOV_EXCL_START*/
                if(x == nrgo) {
                  logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
                  return -1;
                }
                /* LCOV_EXCL_STOP*/
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                if(is_mmu == 1) {
                  mmu_set_uint16(&node->ret, step_out);
                } else {
#endif
                  node->ret = step_out;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                }
#endif

                int16_t tmp1 = vm_rewind3(obj, step_out, TIF, TELSEIF, TEVENT);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                if(is_mmu == 1) {
                  go = mmu_get_uint8(&obj->ast.buffer[tmp1]);
                } else {
#endif
                  go = obj->ast.buffer[tmp1];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                }
#endif

                step_out = tmp;
              } else {
                logprintf_P(F("ERROR: Expected a 'then' token"));
                return -1;
              }
            } else {
              switch(type) {
                case TVAR: {
                  go = TVAR;
                  continue;
                } break;
                case TEVENT: {
                  go = TEVENT;
                  continue;
                } break;
                case TFUNCTION: {
                  struct vm_cache_t *x = vm_cache_get(TFUNCTION, pos);
                  /* LCOV_EXCL_START*/
                  if(x == NULL) {
                    logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
                    return -1;
                  }
                  /* LCOV_EXCL_STOP*/
                  if(lexer_peek(text, x->end, &type, &start, &len) >= 0) {
                    switch(type) {
                      case TSEMICOLON: {
                        int16_t tmp = vm_rewind2(obj, step_out, TTRUE, TFALSE);
                        struct vm_tfunction_t *f = (struct vm_tfunction_t *)&obj->ast.buffer[x->step];
                        struct vm_ttrue_t *t = (struct vm_ttrue_t *)&obj->ast.buffer[step_out];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                        if(is_mmu == 1) {
                          mmu_set_uint16(&f->ret, tmp);
                        } else {
#endif
                          f->ret = tmp;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                        }
#endif

                        int16_t i = 0, nrgo = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                        if(is_mmu == 1) {
                          nrgo = mmu_get_uint8(&t->nrgo);
                        } else {
#endif
                          nrgo = t->nrgo;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                        }
#endif
                        for(i=0;i<nrgo;i++) {
                          uint16_t go = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                          if(is_mmu == 1) {
                            go = mmu_get_uint16(&t->go[i]);
                          } else {
#endif
                            go = t->go[i];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                          }
#endif
                          if(go == 0) {
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                            if(is_mmu == 1) {
                              mmu_set_uint16(&t->go[i], x->step);
                            } else {
#endif
                              t->go[i] = x->step;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                            }
#endif
                            break;
                          }
                        }

                        /* LCOV_EXCL_START*/
                        if(i == nrgo) {
                          logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
                          return -1;
                        }
                        /* LCOV_EXCL_STOP*/

                        go = TEVENT;
                        step_out = tmp;
                        pos = x->end + 1;
                        vm_cache_del(x->start);
                      } break;
                      case TOPERATOR: {
                        go = type;
                      } break;
                      default: {
                        logprintf_P(F("ERROR: Expected an semicolon or operator"));
                        return -1;
                      } break;
                    }
                  }
                  continue;
                } break;
                case TIF: {
                  struct vm_cache_t *cache = vm_cache_get(TIF, pos);
                  if(cache == NULL) {
                    cache = vm_cache_get(TELSEIF, pos);
                  }
                  /* LCOV_EXCL_START*/
                  if(cache == NULL) {
                    logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
                    return -1;
                  }
                  /* LCOV_EXCL_STOP*/

                  step = cache->step;
                  pos = cache->end;

                  /*
                   * After an cached IF block has been linked
                   * it can be removed from cache
                   */
                  vm_cache_del(cache->start);

                  struct vm_tif_t *i = (struct vm_tif_t *)&obj->ast.buffer[step];

                  /*
                   * Attach IF block to TRUE / FALSE node
                   */

                  uint16_t step_out_type = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                  if(is_mmu == 1) {
                    mmu_set_uint16(&i->ret, step_out);
                    step_out_type = mmu_get_uint8(&obj->ast.buffer[step_out]);
                  } else {
#endif
                    i->ret = step_out;
                    step_out_type = obj->ast.buffer[step_out];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                  }
#endif
                  switch(step_out_type) {
                    case TFALSE:
                    case TTRUE: {
                      struct vm_ttrue_t *t = (struct vm_ttrue_t *)&obj->ast.buffer[step_out];

                      /*
                       * Attach the IF block to an empty
                       * TRUE / FALSE go slot.
                       */
                      int16_t x = 0, nrgo = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                      if(is_mmu == 1) {
                        nrgo = mmu_get_uint8(&t->nrgo);
                      } else {
#endif
                        nrgo = t->nrgo;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                      }
#endif
                      for(x=0;x<nrgo;x++) {
                        uint16_t go = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                        if(is_mmu == 1) {
                          go = mmu_get_uint16(&t->go[x]);
                        } else {
#endif
                          go = t->go[x];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                        }
#endif
                        if(go == 0) {
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                          if(is_mmu == 1) {
                            mmu_set_uint16(&t->go[x], step);
                          } else {
#endif
                            t->go[x] = step;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                          }
#endif
                          break;
                        }
                      }

                      /* LCOV_EXCL_START*/
                      if(x == nrgo) {
                        logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
                        return -1;
                      }
                      /* LCOV_EXCL_STOP*/
                      continue;
                    } break;
                    /* LCOV_EXCL_START*/
                    default: {
                      logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
                      return -1;
                    } break;
                    /* LCOV_EXCL_STOP*/
                  }
                  /* LCOV_EXCL_START*/
                  logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
                  return -1;
                  /* LCOV_EXCL_STOP*/
                } break;
                // case TEOF:
                case TEND: {
                  go = -1;

                  int16_t tmp = vm_rewind(obj, step_out, TEVENT);
                  /*
                   * Should never happen
                   */
                  /* LCOV_EXCL_START*/
                  if(has_on > 0) {
                    logprintf_P(F("ERROR: On block inside on block"));
                    return -1;
                  }
                  /* LCOV_EXCL_START*/

                  if(has_on == 0) {
                    struct vm_tstart_t *b = (struct vm_tstart_t *)&obj->ast.buffer[startnode];
                    struct vm_tif_t *a = (struct vm_tif_t *)&obj->ast.buffer[tmp];

#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                    if(is_mmu == 1) {
                      mmu_set_uint16(&b->go, tmp);
                      mmu_set_uint16(&a->ret, startnode);
                    } else {
#endif
                      b->go = tmp;
                      a->ret = startnode;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                    }
#endif

                    step = vm_parent(text, obj, TEOF, 0, 0, 0);
#ifdef DEBUG
                    printf("nr steps: %d\n", step);/*LCOV_EXCL_LINE*/
#endif
                    tmp = vm_rewind(obj, tmp, TSTART);

                    struct vm_tstart_t *node = (struct vm_tstart_t *)&obj->ast.buffer[tmp];

#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                    if(is_mmu == 1) {
                      mmu_set_uint16(&node->ret, step);
                    } else {
#endif
                      node->ret = step;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                    }
#endif

                    if(lexer_peek(text, pos, &type, &start, &len) < 0) {
                      /* LCOV_EXCL_START*/
                      logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
                      return -1;
                      /* LCOV_EXCL_STOP*/
                    }

                    pos++;
                  }

                  /*
                   * If we are the root IF block
                   */
                  if(has_on == 0) {
                    loop = 0;
                  }

                  pos = 0;
                  has_paren = -1;
                  has_function = -1;
                  has_if = -1;
                  has_elseif = -1;
                  has_on = -1;
                  step_out = -1;

                  continue;
                } break;
                default: {
                  logprintf_P(F("ERROR: Unexpected token"));
                  return -1;
                } break;
              }
            }
          } else {
            /* LCOV_EXCL_START*/
            logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
            return -1;
            /* LCOV_EXCL_STOP*/
          }
        } break;
        case TELSEIF:
        case TIF: {
          char current = 0;
          if(lexer_peek(text, pos, &type, &start, &len) < 0) {
            /* LCOV_EXCL_START*/
            logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
            return -1;
            /* LCOV_EXCL_STOP*/
          }
          if(step_out >= 0) {
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            if(is_mmu == 1) {
              current = mmu_get_uint8(&obj->ast.buffer[step_out]);
            } else {
#endif
              current = obj->ast.buffer[step_out];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            }
#endif
          }

          if(
              (
                step_out == -1 ||
                current == TTRUE ||
                current == TFALSE
              ) && (type != TTHEN && type != TELSE)
            ) {
            /*
             * Link cached IF blocks together
             */
            struct vm_cache_t *cache = vm_cache_get(TIF, pos);
            if(cache == NULL) {
              cache = vm_cache_get(TELSEIF, pos);
            }
            if(cache != NULL) {
              step = cache->step;

              if(lexer_peek(text, cache->end, &type, &start, &len) < 0) {
                /* LCOV_EXCL_START*/
                logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
                return -1;
                /* LCOV_EXCL_STOP*/
              }
              struct vm_tif_t *i = (struct vm_tif_t *)&obj->ast.buffer[step];

              /*
               * Relabel ELSEIF to just IF
               */
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              if(is_mmu == 1) {
                mmu_set_uint8(&i->type, TIF);
              } else {
#endif
                i->type = TIF;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              }
#endif

              /*
               * An ELSEIF is always attached to the FALSE node of
               * the previous if block. For proper syntax this FALSE
               * node should not be present, so it's created here.
               */
              if(cache->type == TELSEIF) {
                int16_t tmp = vm_rewind2(obj, step_out, TIF, TELSEIF);
                if(tmp == 0) {
                  /* LCOV_EXCL_START*/
                  logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
                  return -1;
                  /* LCOV_EXCL_STOP*/
                }

                /*
                 * The last parameter is used for
                 * for the number of operations the
                 * TRUE of FALSE will forward to.
                 */
                int16_t step1 = vm_parent(text, obj, TFALSE, 0, 0, 1);
                struct vm_ttrue_t *a = (struct vm_ttrue_t *)&obj->ast.buffer[step1];
                struct vm_tif_t *b = (struct vm_tif_t *)&obj->ast.buffer[tmp];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                if(is_mmu == 1) {
                  if(mmu_get_uint16(&b->false_) > 0) {
                    /* LCOV_EXCL_START*/
                    logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
                    return -1;
                    /* LCOV_EXCL_STOP*/
                  }
                  mmu_set_uint16(&b->false_, step1);
                  mmu_set_uint16(&a->ret, tmp);
                } else {
#endif
                  if(b->false_ > 0) {
                    /* LCOV_EXCL_START*/
                    logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
                    return -1;
                    /* LCOV_EXCL_STOP*/
                  }
                  b->false_ = step1;
                  a->ret = tmp;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                }
#endif

                step_out = step1;
              }

              /*
               * Attach IF block to TRUE / FALSE node
               */
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              if(is_mmu == 1) {
                mmu_set_uint16(&i->ret, step_out);
              } else {
#endif
                i->ret = step_out;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              }
#endif
              pos = cache->end;

              uint8_t step_out_type = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              if(is_mmu == 1) {
                step_out_type = mmu_get_uint8(&obj->ast.buffer[step_out]);
              } else {
#endif
                step_out_type = obj->ast.buffer[step_out];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              }
#endif
              switch(step_out_type) {
                case TFALSE:
                case TTRUE: {
                  uint8_t x = 0, nrgo = 0;
                  struct vm_ttrue_t *t = (struct vm_ttrue_t *)&obj->ast.buffer[step_out];
                  /*
                   * Attach the IF block to an empty
                   * TRUE / FALSE go slot.
                   */

#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                  if(is_mmu == 1) {
                    nrgo = mmu_get_uint8(&t->nrgo);
                  } else {
#endif
                    nrgo = t->nrgo;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                  }
#endif
                  for(x=0;x<nrgo;x++) {
                    uint16_t go = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                    if(is_mmu == 1) {
                      go = mmu_get_uint16(&t->go[x]);
                    } else {
#endif
                      go = t->go[x];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                    }
#endif
                    if(go == 0) {
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                      if(is_mmu == 1) {
                        mmu_set_uint16(&t->go[x], step);
                      } else {
#endif
                        t->go[x] = step;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                      }
#endif
                      break;
                    }
                  }

                  /* LCOV_EXCL_START*/
                  if(x == nrgo) {
                    logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
                    return -1;
                  }
                  /* LCOV_EXCL_STOP*/
                } break;
                /* LCOV_EXCL_START*/
                default: {
                  logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
                  return -1;
                } break;
                /* LCOV_EXCL_STOP*/
              }

              /*
               * An IF block directly followed by another IF block
               */
              if(lexer_peek(text, pos, &type, &start, &len) >= 0 && (type == TIF || type == TELSEIF)) {
                go = TIF;

                /*
                 * After a cached IF block has been linked
                 * it can be removed from cache
                 */
                vm_cache_del(cache->start);
                continue;
              }

              if(cache->type == TELSEIF) {
                if(type == TEOF) {
                  lexer_peek(text, pos-1, &type, &start, &len);
                  pos--;
                }
              }
              /*
               * After a cached IF block has been linked
               * it can be removed from cache
               */
              vm_cache_del(cache->start);
            }

            if(type == TIF || type == TELSEIF) {
              step = vm_parent(text, obj, type, start, len, 0);
              struct vm_tif_t *a = (struct vm_tif_t *)&obj->ast.buffer[step];

              if(pos == 0) {
                struct vm_tstart_t *b = (struct vm_tstart_t *)&obj->ast.buffer[start];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                if(is_mmu == 1) {
                  mmu_set_uint16(&b->go, step);
                  mmu_set_uint16(&a->ret, start);
                } else {
#endif
                  b->go = step;
                  a->ret = start;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                }
#endif
              }

              pos++;

              if(lexer_peek(text, pos+1, &type, &start, &len) >= 0 && type == TOPERATOR) {
                go = TOPERATOR;
                step_out = step;
                continue;
              } else if(lexer_peek(text, pos, &type, &start, &len) >= 0 && type == LPAREN) {
                step_out = step;

                /*
                 * Link cached parenthesis blocks
                 */
                struct vm_cache_t *cache = vm_cache_get(LPAREN, pos);
                /* LCOV_EXCL_START*/
                if(cache == NULL) {
                  logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
                  return -1;
                }
                /* LCOV_EXCL_STOP*/

                /*
                 * If this parenthesis is part of a operator
                 * let the operator handle it.
                 */
                if(lexer_peek(text, cache->end, &type, &start, &len) < 0) {
                  /* LCOV_EXCL_START*/
                  logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
                  return -1;
                  /* LCOV_EXCL_STOP*/
                }

                struct vm_lparen_t *c = (struct vm_lparen_t *)&obj->ast.buffer[cache->step];
                if(type == TOPERATOR) {
                  go = TOPERATOR;
                  step_out = step;
                  continue;
                } else if(type == TTHEN) {
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                  if(is_mmu == 1) {
                    step_out = mmu_get_uint16(&c->go);
                  } else {
#endif
                    step_out = c->go;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                  }
#endif
                  go = TIF;

#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                  if(is_mmu == 1) {
                    mmu_set_uint16(&a->go, cache->step);
                    mmu_set_uint16(&c->ret, step);
                  } else {
#endif
                    a->go = cache->step;
                    c->ret = step;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                  }
#endif
                } else {
                  logprintf_P(F("ERROR: Unexpected token"));
                  return -1;
                }

                pos = cache->end;

                vm_cache_del(cache->start);
                continue;
              } else if(lexer_peek(text, pos, &type, &start, &len) >= 0 && type == TFUNCTION) {
                step_out = step;

                /*
                 * Link cached function blocks
                 */
                struct vm_cache_t *cache = vm_cache_get(TFUNCTION, pos);

                if(lexer_peek(text, cache->end, &type, &start, &len) >= 0 && type == TOPERATOR) {
                  go = TOPERATOR;
                  step_out = step;
                  continue;
                } else {
                  logprintf_P(F("ERROR: Function without operator in if condition"));
                  return -1;
                }
              } else {
                logprintf_P(F("ERROR: Expected a parenthesis block, function or operator"));
                return -1;
              }
            } else {
              switch(type) {
                case TVAR: {
                  go = TVAR;
                  continue;
                } break;
                case TEVENT: {
                  go = TEVENT;
                  continue;
                } break;
                case TELSE: {
                  go = TIF;
                  continue;
                } break;
                case TFUNCTION: {
                  struct vm_cache_t *x = vm_cache_get(TFUNCTION, pos);
                  /* LCOV_EXCL_START*/
                  if(x == NULL) {
                    logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
                    return -1;
                  }
                  /* LCOV_EXCL_STOP*/

                  if(lexer_peek(text, x->end, &type, &start, &len) >= 0) {
                    switch(type) {
                      case TSEMICOLON: {
                        int16_t tmp = vm_rewind2(obj, step_out, TTRUE, TFALSE);
                        struct vm_tfunction_t *f = (struct vm_tfunction_t *)&obj->ast.buffer[x->step];
                        struct vm_ttrue_t *t = (struct vm_ttrue_t *)&obj->ast.buffer[step_out];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                        if(is_mmu == 1) {
                          mmu_set_uint16(&f->ret, tmp);
                        } else {
#endif
                          f->ret = tmp;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                        }
#endif

                        int16_t i = 0, nrgo = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                        if(is_mmu == 1) {
                          nrgo = mmu_get_uint8(&t->nrgo);
                        } else {
#endif
                          nrgo = t->nrgo;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                        }
#endif
                        for(i=0;i<nrgo;i++) {
                          uint16_t go = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                          if(is_mmu == 1) {
                            go = mmu_get_uint16(&t->go[i]);
                          } else {
#endif
                            go = t->go[i];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                          }
#endif
                          if(go == 0) {
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                            if(is_mmu == 1) {
                              mmu_set_uint16(&t->go[i], x->step);
                            } else {
#endif
                              t->go[i] = x->step;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                            }
#endif
                            break;
                          }
                        }

                        if(i == nrgo) {
                          /* LCOV_EXCL_START*/
                          logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
                          return -1;
                          /* LCOV_EXCL_STOP*/
                        }

                        go = TIF;
                        step_out = tmp;
                        pos = x->end + 1;
                        vm_cache_del(x->start);
                      } break;
                      case TOPERATOR: {
                        go = type;
                      } break;
                      default: {
                        logprintf_P(F("ERROR: Expected an semicolon or operator"));
                        return -1;
                      } break;
                    }
                  }
                  continue;
                } break;
                // case TEOF:
                case TEND: {
                  go = -1;
                  int16_t tmp = vm_rewind2(obj, step_out, TIF, TELSEIF);
                  if(MAX(has_if, has_elseif) == has_elseif) {
                    r_rewind = has_elseif;
                  }
                  if(MAX(has_if, has_elseif) == has_if) {
                    r_rewind = has_if;
                  }
                  if(has_elseif > 0 || has_if > 0) {
                    if(lexer_peek(text, pos, &type, &start, &len) < 0 || type != TEND) {
                      /* LCOV_EXCL_START*/
                      logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
                      return -1;
                      /* LCOV_EXCL_STOP*/
                    }

                    pos++;

                    if(MAX(has_if, has_elseif) == has_if) {
                      vm_cache_add(TIF, tmp, has_if, pos);
                    }
                    if(MAX(has_if, has_elseif) == has_elseif) {
                      // ELSEIF doesn't have his own END token,
                      // so compensate for that.
                      vm_cache_add(TELSEIF, tmp, has_elseif, pos-1);
                    }
                  }
                  if(has_if == 0 && has_elseif == -1) {
                    struct vm_tstart_t *b = (struct vm_tstart_t *)&obj->ast.buffer[startnode];
                    struct vm_tif_t *a = (struct vm_tif_t *)&obj->ast.buffer[tmp];

#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                    if(is_mmu == 1) {
                      mmu_set_uint16(&b->go, tmp);
                      mmu_set_uint16(&a->ret, startnode);
                    } else {
#endif
                      b->go = tmp;
                      a->ret = startnode;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                    }
#endif

                    step = vm_parent(text, obj, TEOF, 0, 0, 0);
#ifdef DEBUG
                    printf("nr steps: %d\n", step);/*LCOV_EXCL_LINE*/
#endif
                    tmp = vm_rewind(obj, tmp, TSTART);

                    struct vm_tstart_t *node = (struct vm_tstart_t *)&obj->ast.buffer[tmp];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                    if(is_mmu == 1) {
                      mmu_set_uint16(&node->ret, step);
                    } else {
#endif
                      node->ret = step;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                    }
#endif

                    if(lexer_peek(text, pos, &type, &start, &len) < 0) {
                      /* LCOV_EXCL_START*/
                      logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
                      return -1;
                      /* LCOV_EXCL_STOP*/
                    }

                    pos++;
                  }

                  /*
                   * If we are the root IF block
                   */
                  if(has_if == 0 && has_elseif == -1) {
                    loop = 0;
                  }

                  pos = 0;
                  has_paren = -1;
                  has_function = -1;
                  has_if = -1;
                  has_elseif = -1;
                  has_on = -1;
                  step_out = -1;

                  continue;
                } break;
                default: {
                  logprintf_P(F("ERROR: Unexpected token"));
                  return -1;
                } break;
              }
            }
          } else {
            int16_t t = 0;

            if(lexer_peek(text, pos, &type, &start, &len) < 0) {
              /* LCOV_EXCL_START*/
              logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
              return -1;
              /* LCOV_EXCL_STOP*/
            }

            pos++;

            if(type == TTHEN) {
              t = TTRUE;
            } else if(type == TELSE) {
              t = TFALSE;
            }

            /*
             * Predict how many go slots we need to
             * reserve for the TRUE / FALSE nodes.
             */
            int16_t y = pos, nrexpressions = 0, z = 0;

            while((z = lexer_peek(text, y++, &type, &start, &len)) >= 0) {
              if(type == TEOF) {
                break;
              }
              if(type == TIF) {
                struct vm_cache_t *cache = vm_cache_get(TIF, y-1);
                if(cache == NULL) {
                  /* LCOV_EXCL_START*/
                  logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
                  return -1;
                  /* LCOV_EXCL_STOP*/
                }
                nrexpressions++;
                y = cache->end;
                continue;
              }
              if(type == TELSEIF) {
                struct vm_cache_t *cache = vm_cache_get(TELSEIF, y-1);
                if(cache == NULL) {
                  /* LCOV_EXCL_START*/
                  logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
                  return -1;
                  /* LCOV_EXCL_STOP*/
                }
                /*
                 * The ELSEIF block is attached to a seperate FALSE
                 * node and therefor doesn't count for the current TRUE
                 * node.
                 */
                if(t == TFALSE) {
                  /* LCOV_EXCL_START*/
                  logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
                  return -1;
                  /* LCOV_EXCL_STOP*/
                }
                y = cache->end;
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
              logprintf_P(F("ERROR: If block without body"));
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
            step_out = vm_rewind2(obj, step_out, TIF, TELSEIF);

            if(lexer_peek(text, pos, &type, &start, &len) < 0) {
              /* LCOV_EXCL_START*/
              logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
              return -1;
              /* LCOV_EXCL_STOP*/
            }
            /*
             * The last parameter is used for
             * for the number of operations the
             * TRUE of FALSE will forward to.
             */
            step = vm_parent(text, obj, t, 0, 0, nrexpressions);

            struct vm_ttrue_t *a = (struct vm_ttrue_t *)&obj->ast.buffer[step];
            struct vm_tif_t *b = (struct vm_tif_t *)&obj->ast.buffer[step_out];

#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            if(is_mmu == 1) {
              if(t == TTRUE) {
                mmu_set_uint16(&b->true_, step);
              } else {
                mmu_set_uint16(&b->false_, step);
              }
              mmu_set_uint16(&a->ret, step_out);
              go = mmu_get_uint8(&b->type);
            } else {
#endif
              if(t == TTRUE) {
                b->true_ = step;
              } else {
                b->false_ = step;
              }
              a->ret = step_out;
              go = b->type;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            }
#endif

            step_out = step;
          }
        } break;
        case TOPERATOR: {
          uint8_t a = 0;
          int16_t source = step_out;
          if(lexer_peek(text, pos, &a, &start, &len) >= 0) {
            switch(a) {
              case LPAREN: {
                struct vm_cache_t *x = vm_cache_get(LPAREN, pos);
                /* LCOV_EXCL_START*/
                if(x == NULL) {
                  logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
                  return -1;
                }
                /* LCOV_EXCL_STOP*/
                int16_t oldpos = pos;
                pos = x->end;
                step_out = x->step;
                vm_cache_del(oldpos);
              } break;
              case TFUNCTION: {
                struct vm_cache_t *x = vm_cache_get(TFUNCTION, pos);
                /* LCOV_EXCL_START*/
                if(x == NULL) {
                  logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
                  return -1;
                }
                /* LCOV_EXCL_STOP*/
                int16_t oldpos = pos;
                pos = x->end;
                step_out = x->step;
                vm_cache_del(oldpos);
              } break;
              case TVAR:
              case VNULL:
              case TNUMBER1:
              case TNUMBER2:
              case TNUMBER3:
              case VFLOAT:
              case VINTEGER: {
                step_out = vm_parent(text, obj, a, start, len, 0);
                pos++;

                if(a == TVAR) {
                  uint16_t ptr = vm_parent(text, obj, TVALUE, start, len, 0);
                  struct vm_tvar_t *node = (struct vm_tvar_t *)&obj->ast.buffer[step_out];

#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                  if(is_mmu == 1) {
                    mmu_set_uint16(&node->value, ptr);
                  } else {
#endif
                    node->value = ptr;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                  }
#endif
                }
              } break;
              /* LCOV_EXCL_START*/
              default: {
                logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
                return -1;
              } break;
              /* LCOV_EXCL_STOP*/
            }
          }

          /*
           * The return value is the positition
           * of the root operator
           */
          int16_t step = lexer_parse_math_order(text, obj, TOPERATOR, &pos, &step_out, offset, source);
          if(step == -1) {
            return -1;
          }

          {
            struct vm_tgeneric_t *node = (struct vm_tgeneric_t *)&obj->ast.buffer[step_out];
            uint8_t source_type = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            if(is_mmu == 1) {
              source_type = mmu_get_uint8(&obj->ast.buffer[source]);
              mmu_set_uint16(&node->ret, source);
            } else {
#endif
              source_type = obj->ast.buffer[source];
              node->ret = source;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            }
#endif

            switch(source_type) {
              case TELSEIF:
              case TIF: {
                struct vm_tif_t *node = (struct vm_tif_t *)&obj->ast.buffer[source];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                if(is_mmu == 1) {
                  mmu_set_uint16(&node->go, step);
                } else {
#endif
                  node->go = step;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                }
#endif
              } break;
              case TVAR: {
                struct vm_tvar_t *node = (struct vm_tvar_t *)&obj->ast.buffer[source];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                if(is_mmu == 1) {
                  mmu_set_uint16(&node->go, step);
                } else {
#endif
                  node->go = step;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                }
#endif
              } break;
              /*
               * If this operator block is a function
               * argument, attach it to a empty go slot
               */
              case TFUNCTION: {
                struct vm_tfunction_t *node = (struct vm_tfunction_t *)&obj->ast.buffer[source];
                uint8_t i = 0, nrgo = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                if(is_mmu == 1) {
                  nrgo = mmu_get_uint8(&node->nrgo);
                } else {
#endif
                  nrgo = node->nrgo;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                }
#endif
                for(i=0;i<nrgo;i++) {
                  uint16_t go = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                  if(is_mmu == 1) {
                    go = mmu_get_uint16(&node->go[i]);
                  } else {
#endif
                    go = node->go[i];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                  }
#endif
                  if(go == 0) {
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                    if(is_mmu == 1) {
                      mmu_set_uint16(&node->go[i], step);
                    } else {
#endif
                      node->go[i] = step;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                    }
#endif
                    break;
                  }
                }
                go = TFUNCTION;
                step_out = step;
                continue;
              } break;
              default: {
                logprintf_P(F("ERROR: Unexpected token"));
                return -1;
              } break;
            }
          }

          if(lexer_peek(text, pos, &type, &start, &len) >= 0) {
            switch(type) {
              case TTHEN: {
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                if(is_mmu == 1) {
                  go = mmu_get_uint8(&obj->ast.buffer[source]);
                } else {
#endif
                  go = obj->ast.buffer[source];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                }
#endif
              } break;
              case TSEMICOLON: {
                go = TVAR;
              } break;
              default: {
                logprintf_P(F("ERROR: Unexpected token"));
                return -1;
              } break;
            }
          }
        } break;
        case VNULL: {
          struct vm_vnull_t *node = NULL;
          if(lexer_peek(text, pos, &type, &start, &len) < 0) {
            /* LCOV_EXCL_START*/
            logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
            return -1;
            /* LCOV_EXCL_STOP*/
          }

          step = vm_parent(text, obj, VNULL, start, len, 0);
          pos++;

          {
            uint16_t type = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            if(is_mmu == 1) {
              type = mmu_get_uint8(&obj->ast.buffer[step_out]);
            } else {
#endif
              type = obj->ast.buffer[step_out];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            }
#endif
            switch(type) {
              case TVAR: {
                struct vm_tvar_t *tmp = (struct vm_tvar_t *)&obj->ast.buffer[step_out];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                if(is_mmu == 1) {
                  mmu_set_uint16(&tmp->go, step);
                } else {
#endif
                  tmp->go = step;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                }
#endif
              } break;
              /*
               * FIXME: Think of a rule that can trigger this
               */
              /* LCOV_EXCL_START*/
              default: {
                logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
                return -1;
              } break;
              /* LCOV_EXCL_STOP*/
            }
          }

          node = (struct vm_vnull_t *)&obj->ast.buffer[step];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          if(is_mmu == 1) {
            mmu_set_uint16(&node->ret, step_out);
          } else {
#endif
            node->ret = step_out;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          }
#endif
          int16_t tmp = vm_rewind2(obj, step_out, TVAR, TOPERATOR);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          if(is_mmu == 1) {
            go = mmu_get_uint8(&obj->ast.buffer[tmp]);
          } else {
#endif
            go = obj->ast.buffer[tmp];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          }
#endif
          step_out = step;
        } break;
        /* LCOV_EXCL_START*/
        case TSEMICOLON: {
          logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
          return -1;
        } break;
        /* LCOV_EXCL_STOP*/
        case TVAR: {
          uint8_t step_out_type = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          if(is_mmu == 1) {
            step_out_type = mmu_get_uint8(&obj->ast.buffer[step_out]);
          } else {
#endif
            step_out_type = obj->ast.buffer[step_out];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          }
#endif
          /*
           * We came from the TRUE node from the IF root,
           * which means we start parsing the variable name.
           */
          if(step_out_type == TTRUE || step_out_type == TFALSE) {
            struct vm_tvar_t *node = NULL;
            struct vm_ttrue_t *node1 = NULL;

            if(lexer_peek(text, pos, &type, &start, &len) < 0) {
              /* LCOV_EXCL_START*/
              logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
              return -1;
              /* LCOV_EXCL_STOP*/
            }

            step = vm_parent(text, obj, TVAR, start, len, 0);

            pos++;

            node = (struct vm_tvar_t *)&obj->ast.buffer[step];
            {
              uint16_t ptr = vm_parent(text, obj, TVALUE, start, len, 0);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              if(is_mmu == 1) {
                mmu_set_uint16(&node->value, ptr);
              } else {
#endif
                node->value = ptr;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              }
#endif
            }
            node1 = (struct vm_ttrue_t *)&obj->ast.buffer[step_out];

            {
              uint8_t x = 0, nrgo = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              if(is_mmu == 1) {
                nrgo = mmu_get_uint8(&node1->nrgo);
              } else {
#endif
                nrgo = node1->nrgo;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              }
#endif
              for(x=0;x<nrgo;x++) {
                uint16_t go = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                if(is_mmu == 1) {
                  go = mmu_get_uint16(&node1->go[x]);
                } else {
#endif
                  go = node1->go[x];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                }
#endif
                if(go == 0) {
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                  if(is_mmu == 1) {
                    mmu_set_uint16(&node1->go[x], step);
                  } else {
#endif
                    node1->go[x] = step;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                  }
#endif
                  break;
                }
              }

              /* LCOV_EXCL_START*/
              if(x == nrgo) {
                logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
                return -1;
              }
              /* LCOV_EXCL_STOP*/
            }

#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            if(is_mmu == 1) {
              mmu_set_uint16(&node->ret, step_out);
            } else {
#endif
              node->ret = step_out;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            }
#endif
            step_out = step;

            if(lexer_peek(text, pos, &type, &start, &len) < 0 || type != TASSIGN) {
              /* LCOV_EXCL_START*/
              logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
              return -1;
              /* LCOV_EXCL_STOP*/
            }

            pos++;

            if(lexer_peek(text, pos+1, &type, &start, &len) >= 0 && type == TOPERATOR) {
              switch(type) {
                case TOPERATOR: {
                  go = TOPERATOR;
                  step_out = step;
                  continue;
                } break;
              }
            } else if(lexer_peek(text, pos, &type, &start, &len) >= 0) {
              switch(type) {
                case VINTEGER:
                case VFLOAT:
                case TNUMBER1:
                case TNUMBER2:
                case TNUMBER3: {
                  int16_t foo = vm_parent(text, obj, type, start, len, 0);
                  struct vm_tgeneric_t *a = (struct vm_tgeneric_t *)&obj->ast.buffer[foo];
                  node = (struct vm_tvar_t *)&obj->ast.buffer[step];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                  if(is_mmu == 1) {
                    mmu_set_uint16(&node->go, foo);
                    mmu_set_uint16(&a->ret, step);
                  } else {
#endif
                    node->go = foo;
                    a->ret = step;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                  }
#endif

                  pos++;

                  go = TVAR;
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
                  struct vm_cache_t *x = vm_cache_get(TFUNCTION, pos);
                  /* LCOV_EXCL_START*/
                  if(x == NULL) {
                    logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
                    return -1;
                  }
                  /* LCOV_EXCL_STOP*/
                  if(lexer_peek(text, x->end, &type, &start, &len) >= 0) {
                    switch(type) {
                      case TSEMICOLON: {
                        struct vm_tfunction_t *f = (struct vm_tfunction_t *)&obj->ast.buffer[x->step];
                        struct vm_tvar_t *v = (struct vm_tvar_t *)&obj->ast.buffer[step_out];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                        if(is_mmu == 1) {
                          mmu_set_uint16(&f->ret, step);
                          mmu_set_uint16(&v->go, x->step);
                        } else {
#endif
                          f->ret = step;
                          v->go = x->step;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                        }
#endif

                        int16_t tmp = vm_rewind2(obj, step_out, TTRUE, TFALSE);
                        int16_t tmp1 = vm_rewind3(obj, tmp, TIF, TELSEIF, TEVENT);

#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                        if(is_mmu == 1) {
                          go = mmu_get_uint8(&obj->ast.buffer[tmp1]);
                        } else {
#endif
                          go = obj->ast.buffer[tmp1];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                        }
#endif
                        step_out = tmp;
                        pos = x->end + 1;
                        vm_cache_del(x->start);
                      } break;
                      case TOPERATOR: {
                        go = type;
                      } break;
                      default: {
                        logprintf_P(F("ERROR: Expected an semicolon or operator"));
                        return -1;
                      } break;
                    }
                  }
                } break;
                case LPAREN: {
                  int16_t oldpos = pos;
                  struct vm_cache_t *x = vm_cache_get(LPAREN, pos);
                  /* LCOV_EXCL_START*/
                  if(x == NULL) {
                    logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
                    return -1;
                  }

                  /* LCOV_EXCL_STOP*/
                  if(lexer_peek(text, x->end, &type, &start, &len) >= 0) {
                    if(type == TSEMICOLON) {
                      struct vm_lparen_t *l = (struct vm_lparen_t *)&obj->ast.buffer[x->step];
                      struct vm_tvar_t *v = (struct vm_tvar_t *)&obj->ast.buffer[step_out];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                      if(is_mmu == 1) {
                        mmu_set_uint16(&l->ret, step);
                        mmu_set_uint16(&v->go, x->step);
                      } else {
#endif
                        l->ret = step;
                        v->go = x->step;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                      }
#endif
                      int16_t tmp = vm_rewind2(obj, step_out, TTRUE, TFALSE);

                      go = TIF;
                      step_out = tmp;

                      pos = x->end + 1;

                      vm_cache_del(oldpos);
                    } else {
                      go = type;
                    }
                  }
                } break;
                default: {
                  logprintf_P(F("ERROR: Unexpected token"));
                  return -1;
                } break;
              }
            } else {
              /* LCOV_EXCL_START*/
              logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
              return -1;
              /* LCOV_EXCL_STOP*/
            }
          /*
           * The variable has been called as a value
           */
          } else if(step_out_type == TVAR && lexer_peek(text, pos, &type, &start, &len) >= 0 && type != TSEMICOLON) {
            step = vm_parent(text, obj, TVAR, start, len, 0);
            pos++;

            {
              uint16_t ptr = vm_parent(text, obj, TVALUE, start, len, 0);
              struct vm_tvar_t *node = (struct vm_tvar_t *)&obj->ast.buffer[step];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              if(is_mmu == 1) {
                mmu_set_uint16(&node->value, ptr);
              } else {
#endif
                node->value = ptr;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              }
#endif
            }

            struct vm_tvar_t *in = (struct vm_tvar_t *)&obj->ast.buffer[step];
            struct vm_tvar_t *out = (struct vm_tvar_t *)&obj->ast.buffer[step_out];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            if(is_mmu == 1) {
              mmu_set_uint16(&in->ret, step_out);
              mmu_set_uint16(&out->go, step);
            } else {
#endif
              in->ret = step_out;
              out->go = step;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            }
#endif

            if(lexer_peek(text, pos, &type, &start, &len) >= 0) {
              switch(type) {
                case TSEMICOLON: {
                  go = TVAR;
                } break;
                default: {
                  logprintf_P(F("ERROR: Expected a semicolon"));
                  return -1;
                } break;
              }
            }
          } else {
            if(lexer_peek(text, pos, &type, &start, &len) < 0 || type != TSEMICOLON) {
              /* LCOV_EXCL_START*/
              logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
              return -1;
              /* LCOV_EXCL_STOP*/
            }

            int16_t tmp = step_out;
            uint8_t step_out_type = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            if(is_mmu == 1) {
              step_out_type = mmu_get_uint8(&obj->ast.buffer[tmp]);
            } else {
#endif
              step_out_type = obj->ast.buffer[tmp];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            }
#endif
            pos++;

            while(1) {
              if(step_out_type != TTRUE && step_out_type != TFALSE) {
                struct vm_tgeneric_t *node = (struct vm_tgeneric_t *)&obj->ast.buffer[tmp];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                if(is_mmu == 1) {
                  tmp = mmu_get_uint16(&node->ret);
                  step_out_type = mmu_get_uint8(&obj->ast.buffer[tmp]);
                } else {
#endif
                  tmp = node->ret;
                  step_out_type = obj->ast.buffer[tmp];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                }
#endif
              } else {
                int16_t tmp1 = vm_rewind3(obj, tmp, TIF, TELSEIF, TEVENT);

#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                if(is_mmu == 1) {
                  go = mmu_get_uint8(&obj->ast.buffer[tmp1]);
                } else {
#endif
                  go = obj->ast.buffer[tmp1];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                }
#endif
                step_out = tmp;
                break;
              }
            }
          }
        } break;
        case LPAREN: {
          uint8_t a = 0, b = 0;

          if(lexer_peek(text, has_paren+1, &a, &start, &len) >= 0) {
            switch(a) {
              case LPAREN: {
                struct vm_cache_t *x = vm_cache_get(LPAREN, has_paren+1);
                /* LCOV_EXCL_START*/
                if(x == NULL) {
                  logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
                  return -1;
                }
                /* LCOV_EXCL_STOP*/
                pos = x->end;
                step_out = x->step;
                vm_cache_del(has_paren+1);
              } break;
              case TFUNCTION: {
                struct vm_cache_t *x = vm_cache_get(TFUNCTION, has_paren+1);
                /* LCOV_EXCL_START*/
                if(x == NULL) {
                  logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
                  return -1;
                }
                /* LCOV_EXCL_STOP*/
                step_out = x->step;
                pos = x->end;
                vm_cache_del(has_paren+1);
              } break;
              case TVAR:
              case VNULL:
              case VINTEGER:
              case VFLOAT:
              case TNUMBER1:
              case TNUMBER2:
              case TNUMBER3: {
                pos = has_paren + 1;
                step_out = vm_parent(text, obj, a, start, len, 0);
                pos++;
                if(a == TVAR) {
                  uint16_t ptr = vm_parent(text, obj, TVALUE, start, len, 0);
                  struct vm_tvar_t *node = (struct vm_tvar_t *)&obj->ast.buffer[step_out];

#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                  if(is_mmu == 1) {
                    mmu_set_uint16(&node->value, ptr);
                  } else {
#endif
                    node->value = ptr;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                  }
#endif
                }
              } break;
              case RPAREN: {
                logprintf_P(F("ERROR: Empty parenthesis block"));
                return -1;
              } break;
              case TOPERATOR: {
                logprintf_P(F("ERROR: Unexpected operator"));
                return -1;
              } break;
              default: {
                logprintf_P(F("ERROR: Unexpected token"));
                return -1;
              }
            }
          }

          /*
           * The return value is the positition
           * of the root operator
           */
          int16_t step = lexer_parse_math_order(text, obj, LPAREN, &pos, &step_out, offset, 0);
          if(step == -1) {
            return -1;
          }

          if(lexer_peek(text, pos, &b, &start, &len) < 0) {
            /* LCOV_EXCL_START*/
            logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
            return -1;
            /* LCOV_EXCL_STOP*/
          }

          if(b != RPAREN) {
            logprintf_P(F("ERROR: Expected a right parenthesis"));
            return -1;
          }

          pos++;

          {
            struct vm_tgeneric_t *node = (struct vm_tgeneric_t *)&obj->ast.buffer[step_out];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            if(is_mmu == 1) {
              mmu_set_uint16(&node->ret, 0);
            } else {
#endif
              node->ret = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            }
#endif
          }

          if(lexer_peek(text, has_paren, &type, &start, &len) < 0 || type != LPAREN) {
            /* LCOV_EXCL_START*/
            logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
            return -1;
            /* LCOV_EXCL_STOP*/
          }

          step = vm_parent(text, obj, LPAREN, start, len, 0);

          struct vm_lparen_t *node = (struct vm_lparen_t *)&obj->ast.buffer[step];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          if(is_mmu == 1) {
            mmu_set_uint16(&node->go, step_out);
          } else {
#endif
            node->go = step_out;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          }
#endif

          vm_cache_add(LPAREN, step, has_paren, pos);

          r_rewind = has_paren;

          struct vm_tgeneric_t *node1 = (struct vm_tgeneric_t *)&obj->ast.buffer[step_out];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          if(is_mmu == 1) {
            mmu_set_uint16(&node1->ret, step);
          } else {
#endif
            node1->ret = step;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          }
#endif

          go = -1;

          pos = 0;

          has_paren = -1;
          has_function = -1;
          has_if = -1;
          has_elseif = -1;
          has_on = -1;
          step_out = -1;
        } break;
        case TFUNCTION: {
          struct vm_tfunction_t *node = NULL;
          int16_t arg = 0;

          /*
           * We've entered the function name,
           * so we start by creating a proper
           * root node.
           */
          if(step_out == -1) {
            pos = has_function+1;
            if(lexer_peek(text, has_function, &type, &start, &len) < 0) {
              /* LCOV_EXCL_START*/
              logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
              return -1;
              /* LCOV_EXCL_STOP*/
            }

            /*
             * Determine how many arguments this
             * function has.
             */
            int16_t y = pos + 1, nrargs = 0;
            while(lexer_peek(text, y++, &type, &start, &len) >= 0) {
              if(type == TEOF || type == RPAREN) {
                break;
              }
              if(type == LPAREN) {
                struct vm_cache_t *cache = vm_cache_get(LPAREN, y-1);
                if(cache == NULL) {
                  /* LCOV_EXCL_START*/
                  logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
                  return -1;
                  /* LCOV_EXCL_STOP*/
                }
                y = cache->end;
                continue;
              }
              if(type == TFUNCTION) {
                struct vm_cache_t *cache = vm_cache_get(TFUNCTION, y-1);
                if(cache == NULL) {
                  /* LCOV_EXCL_START*/
                  logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
                  return -1;
                  /* LCOV_EXCL_STOP*/
                }
                y = cache->end;
                continue;
              }
              if(type == TCOMMA) {
                nrargs++;
              }
            }
#ifdef DEBUG
            printf("nrarguments: %d\n", nrargs + 1);/*LCOV_EXCL_LINE*/
#endif
            if(lexer_peek(text, has_function, &type, &start, &len) < 0 || type != TFUNCTION) {
              /* LCOV_EXCL_START*/
              logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
              return -1;
              /* LCOV_EXCL_STOP*/
            }

            step = vm_parent(text, obj, TFUNCTION, start, len, nrargs + 1);

            pos++;

          /*
           * When the root node has been created
           * we parse the different arguments.
           */
          } else {
            int16_t i = 0;
            step = vm_rewind(obj, step_out, TFUNCTION);

            node = (struct vm_tfunction_t *)&obj->ast.buffer[step];

            /*
             * Find the argument we've just
             * looked up.
             */
            {
              uint16_t nrgo = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              if(is_mmu == 1) {
                nrgo = mmu_get_uint8(&node->nrgo);
              } else {
#endif
                nrgo = node->nrgo;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              }
#endif
              for(i=0;i<nrgo;i++) {
                uint16_t go = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                if(is_mmu == 1) {
                  go = mmu_get_uint16(&node->go[i]);
                } else {
#endif
                  go = node->go[i];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                }
#endif
                if(go == step_out) {
                  arg = i+1;
                  break;
                }
              }
            }

            /*
             * Look for the next token in the list
             * of arguments
             */
            while(1) {
              if(lexer_peek(text, pos, &type, &start, &len) < 0) {
                /* LCOV_EXCL_START*/
                logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
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
            }

            lexer_peek(text, pos, &type, &start, &len);
          }

          /*
           * When this token is a semicolon
           * we're done looking up the argument
           * nodes. In the other case, we try to
           * lookup the other arguments in the
           * list.
           */
          if(type != RPAREN) {
            uint8_t stop = 0;
            while(1 && stop == 0) {
              if(lexer_peek(text, pos + 1, &type, &start, &len) < 0) {
                /* LCOV_EXCL_START*/
                logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
                return -1;
                /* LCOV_EXCL_STOP*/
              } else if(type == TOPERATOR) {
                go = TOPERATOR;
                step_out = step;
                break;
              }

              if(lexer_peek(text, pos, &type, &start, &len) < 0) {
                /* LCOV_EXCL_START*/
                logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
                return -1;
                /* LCOV_EXCL_STOP*/
              }

              switch(type) {
                /*
                 * If the arguments are a parenthesis or
                 * another function, link those together.
                 */
                case LPAREN: {
                  struct vm_cache_t *cache = vm_cache_get(LPAREN, pos);
                  /* LCOV_EXCL_START*/
                  if(cache == NULL) {
                    logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
                    return -1;
                  }
                  /* LCOV_EXCL_STOP*/

                  node = (struct vm_tfunction_t *)&obj->ast.buffer[step];
                  struct vm_lparen_t *paren = (struct vm_lparen_t *)&obj->ast.buffer[cache->step];
                  int16_t oldpos = pos;

                  if(lexer_peek(text, cache->end, &type, &start, &len) < 0) {
                    /* LCOV_EXCL_START*/
                    logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
                    return -1;
                    /* LCOV_EXCL_STOP*/
                  }
                  /*
                   * In case the LPAREN / RPAREN combination is part
                   * of another operator, let the operator eat the
                   * LPAREN.
                   */
                  if(type == TOPERATOR) {
                    go = TOPERATOR;
                    pos = oldpos;
                    step_out = step;
                    stop = 1;
                    break;
                  } else {
                    pos = cache->end;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                    if(is_mmu == 1) {
                      mmu_set_uint16(&node->go[arg++], cache->step);
                      mmu_set_uint16(&paren->ret, step);
                    } else {
#endif
                      node->go[arg++] = cache->step;
                      paren->ret = step;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                    }
#endif
                    vm_cache_del(oldpos);
                  }
                } break;
                case TFUNCTION: {
                  struct vm_cache_t *cache = vm_cache_get(TFUNCTION, pos);
                  /* LCOV_EXCL_START*/
                  if(cache == NULL) {
                    logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
                    return -1;
                  }

                  node = (struct vm_tfunction_t *)&obj->ast.buffer[step];
                  struct vm_tfunction_t *func = (struct vm_tfunction_t *)&obj->ast.buffer[cache->step];
                  /* LCOV_EXCL_STOP*/
                  int16_t oldpos = pos;
                  pos = cache->end;

#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                  if(is_mmu == 1) {
                    mmu_set_uint16(&node->go[arg++], cache->step);
                    mmu_set_uint16(&func->ret, step);
                  } else {
#endif
                    node->go[arg++] = cache->step;
                    func->ret = step;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                  }
#endif
                  vm_cache_del(oldpos);
                } break;
                case VNULL:
                case TVAR:
                case VINTEGER:
                case VFLOAT:
                case TNUMBER1:
                case TNUMBER2:
                case TNUMBER3: {
                  int16_t a = vm_parent(text, obj, type, start, len, 0);

                  if(type == TVAR) {
                    uint16_t ptr = vm_parent(text, obj, TVALUE, start, len, 0);
                    struct vm_tvar_t *node = (struct vm_tvar_t *)&obj->ast.buffer[a];

#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                    if(is_mmu == 1) {
                      mmu_set_uint16(&node->value, ptr);
                    } else {
#endif
                      node->value = ptr;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                    }
#endif
                  }
                  pos++;

                  node = (struct vm_tfunction_t *)&obj->ast.buffer[step];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                  if(is_mmu == 1) {
                    mmu_set_uint16(&node->go[arg++], a);
                  } else {
#endif
                    node->go[arg++] = a;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                  }
#endif

                  struct vm_tvar_t *tmp = (struct vm_tvar_t *)&obj->ast.buffer[a];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                  if(is_mmu == 1) {
                    mmu_set_uint16(&tmp->ret, step);
                  } else {
#endif
                    tmp->ret = step;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                  }
#endif
                } break;
                default: {
                  logprintf_P(F("ERROR: Unexpected token"));
                  return -1;
                } break;
              }
              if(stop == 1) {
                break;
              }

              if(lexer_peek(text, pos, &type, &start, &len) < 0) {
                /* LCOV_EXCL_START*/
                logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
                return -1;
                /* LCOV_EXCL_STOP*/
              }

              /*
               * A right parenthesis means we've
               * reached the end of the argument list
               */
              if(type == RPAREN) {
                pos++;
                break;
              } else if(type == TOPERATOR) {
                pos--;
                go = TOPERATOR;
                break;
              } else if(type != TCOMMA) {
                logprintf_P(F("ERROR: Expected a closing parenthesis"));
                return -1;
              }

              pos++;
            }
          } else {
            if(lexer_peek(text, pos, &type, &start, &len) < 0) {
              /* LCOV_EXCL_START*/
              logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
              return -1;
              /* LCOV_EXCL_STOP*/
            }

            pos++;
          }

          /*
           * When we're back to the function
           * root, cache it for further linking.
           */
          if(go == TFUNCTION) {
            vm_cache_add(TFUNCTION, step, has_function, pos);

            r_rewind = has_function;

            go = -1;

            pos = 0;

            has_paren = -1;
            has_function = -1;
            has_if = -1;
            has_elseif = -1;
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
        if(lexer_peek(text, pos++, &type, &start, &len) < 0) {
          /* LCOV_EXCL_START*/
          logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
          return -1;
          /* LCOV_EXCL_STOP*/
        }
      /*
       * If we found the furthest, rewind back step
       * by step until we are at the beginning again.
       */
      } else {
        if(r_rewind > 0 && lexer_peek(text, --r_rewind, &type, &start, &len) < 0) {
          go = -1;
        }
        pos = r_rewind+1;
      }
      if(type == TFUNCTION) {
        has_function = pos-1;
      } else if(type == TIF) {
        has_if = pos-1;
      } else if(type == TELSEIF) {
        has_elseif = pos-1;
      } else if(type == TEVENT && pos == 1) {
        has_on = pos-1;
      } else if(type == LPAREN &&
        lexer_peek(text, pos-2, &type1, &start, &len) >= 0 &&
         type1 != TFUNCTION && type1 != TEVENT) {
        has_paren = pos-1;
      }
      if(type == TEVENT && pos > 1) {
        if(lexer_peek(text, pos, &type1, &start, &len) >= 0 && type1 == TTHEN) {
          logprintf_P(F("ERROR: nested 'on' block"));
          return -1;
        }
      }

      if(has_function != -1 || has_paren != -1 || has_if != -1 || has_elseif != -1 || has_on != -1) {
        if(type == TEOF || r_rewind > -1) {
          if(MAX(has_function, MAX(has_paren, MAX(has_if, MAX(has_on, has_elseif)))) == has_function) {
            offset = (pos = has_function)+1;
            go = TFUNCTION;
            continue;
          } else if(MAX(has_function, MAX(has_paren, MAX(has_if, MAX(has_on, has_elseif)))) == has_if) {
            offset = (pos = has_if);
            if(has_if > 0) {
              offset++;
            }
            go = TIF;
            continue;
          } else if(MAX(has_function, MAX(has_paren, MAX(has_if, MAX(has_on, has_elseif)))) == has_elseif) {
            offset = (pos = has_elseif);
            if(has_elseif > 0) {
              offset++;
            }
            go = TELSEIF;
            continue;
          } else if(MAX(has_function, MAX(has_paren, MAX(has_if, MAX(has_on, has_elseif)))) == has_on) {
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
          has_elseif = -1;
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
        logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
        return -1;
        /* LCOV_EXCL_STOP*/
      }
    }
  }

  {
    /* LCOV_EXCL_START*/
    struct vm_tstart_t *node = (struct vm_tstart_t *)&obj->ast.buffer[0];
    uint16_t go = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
    if(is_mmu == 1) {
      go = mmu_get_uint16(&node->go);
    } else {
#endif
      go = node->go;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
    }
#endif
    if(go == 0) {
      logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
      return -1;
    }
    /* LCOV_EXCL_STOP*/
  }
  return 0;
}

/*LCOV_EXCL_START*/
#ifdef DEBUG
static void print_ast(struct rules_t *obj) {
  uint16_t i = 0;

  for(i=0;i<obj->ast.nrbytes;i++) {
    switch(obj->ast.buffer[i]) {
      case TSTART: {
        struct vm_tstart_t *node = (struct vm_tstart_t *)&obj->ast.buffer[i];
        printf("\"%d\"[label=\"START\"]\n", i);
        printf("\"%d\" -> \"%d\"\n", i, node->go);
        printf("\"%d\" -> \"%d\"\n", i, node->ret);
        i+=sizeof(struct vm_tstart_t)-1;
      } break;
      case TEOF: {
        printf("\"%d\"[label=\"EOF\"]\n", i);
        i+=sizeof(struct vm_teof_t)-1;
      } break;
      case VNULL: {
        printf("\"%d\"[label=\"NULL\"]\n", i);
        i+=sizeof(struct vm_vnull_t)-1;
      } break;
      case TELSEIF:
      case TIF: {
        struct vm_tif_t *node = (struct vm_tif_t *)&obj->ast.buffer[i];
        if(obj->ast.buffer[i] == TELSEIF) {
          printf("\"%d\"[label=\"ELSEIF\"]\n", i);
        } else {
          printf("\"%d\"[label=\"IF\"]\n", i);
        }
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
        struct vm_lparen_t *node = (struct vm_lparen_t *)&obj->ast.buffer[i];
        printf("\"%d\"[label=\"(\"]\n", i);
        printf("\"%d\" -> \"%d\"\n", i, node->go);
        // printf("\"%d\" -> \"%d\"\n", i, node->ret);
        i+=sizeof(struct vm_lparen_t)-1;
      } break;
      case TFALSE:
      case TTRUE: {
        int x = 0;
        struct vm_ttrue_t *node = (struct vm_ttrue_t *)&obj->ast.buffer[i];
        if((obj->ast.buffer[i]) == TFALSE) {
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
        i+=sizeof(struct vm_ttrue_t)+(sizeof(node->go[0])*node->nrgo)-1;
      } break;
      case TFUNCTION: {
        int x = 0;
        struct vm_tfunction_t *node = (struct vm_tfunction_t *)&obj->ast.buffer[i];
        printf("\"%d\"[label=\"%s\"]\n", i, rule_functions[node->token].name);
        for(x=0;x<node->nrgo;x++) {
          printf("\"%d\" -> \"%d\"\n", i, node->go[x]);
        }
        // printf("\"%d\" -> \"%d\"\n", i, node->ret);
        i+=sizeof(struct vm_tfunction_t)+(sizeof(node->go[0])*node->nrgo)-1;
      } break;
      case TVAR: {
        struct vm_tvar_t *node = (struct vm_tvar_t *)&obj->ast.buffer[i];
        printf("\"%d\"[label=\"TVAR\"]\n", i);
        if(node->go > 0) {
          printf("\"%d\" -> \"%d\"\n", i, node->go);
        }
        printf("\"%d\" -> \"%d\" [arrowhead=none,style=dotted]\n", i, node->value);
        i+=sizeof(struct vm_tvar_t)-1;
      } break;
      case TVALUE: {
        struct vm_tvalue_t *node = (struct vm_tvalue_t *)&obj->ast.buffer[i];
        printf("\"%d\"[label=\"%s\",style=dotted]\n", i, node->token);
        if(node->go > 0) {
          printf("\"%d\" -> \"%d\"\n", i, node->go);
        }
        // printf("\"%d\" -> \"%d\"\n", i, node->ret);
        i+=sizeof(struct vm_tvar_t)+strlen((char *)node->token);
      } break;
      case TNUMBER: {
        struct vm_tnumber_t *node = (struct vm_tnumber_t *)&obj->ast.buffer[i];
        printf("\"%d\"[label=\"%s\"]\n", i, node->token);
        i+=sizeof(struct vm_tnumber_t)+strlen((char *)node->token);
      } break;
      case VINTEGER: {
        struct vm_vinteger_t *node = (struct vm_vinteger_t *)&obj->ast.buffer[i];
        printf("\"%d\"[label=\"%d\"]\n", i, node->value);
        i+=sizeof(struct vm_vinteger_t)-1;
      } break;
      case VFLOAT: {
        struct vm_vfloat_t *node = (struct vm_vfloat_t *)&obj->ast.buffer[i];
        float v = 0;
        uint322float(node->value, &v);
        printf("\"%d\"[label=\"%g\"]\n", i, v);
        i+=sizeof(struct vm_vfloat_t)-1;
      } break;
      case TEVENT: {
        struct vm_tevent_t *node = (struct vm_tevent_t *)&obj->ast.buffer[i];
        printf("\"%d\"[label=\"%s\"]\n", i, node->token);
        // printf("\"%i\" -> \"%i\"\n", i, node->ret);
        if(node->go > 0) {
          printf("\"%d\" -> \"%d\"\n", i, node->go);
        }
        i+=sizeof(struct vm_tevent_t)+strlen((char *)node->token);
      } break;
      case TOPERATOR: {
        struct vm_toperator_t *node = (struct vm_toperator_t *)&obj->ast.buffer[i];

        printf("\"%d\"[label=\"%s\"]\n", i, rule_operators[node->token].name);
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
  uint16_t i = 0;

  for(i=0;i<obj->ast.nrbytes;i++) {
    switch(obj->ast.buffer[i]) {
      case TSTART: {
        struct vm_tstart_t *node = (struct vm_tstart_t *)&obj->ast.buffer[i];
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
        printf("\"%d-1\"[label=\"%d\" shape=square]\n", i, i);
        printf("\"%d-2\"[label=\"EOF\"]\n", i);
        printf("\"%d-1\" -> \"%d-2\"\n", i, i);
        i+=sizeof(struct vm_teof_t)-1;
      } break;
      case VNULL: {
        struct vm_vnull_t *node = (struct vm_vnull_t *)&obj->ast.buffer[i];
        printf("\"%d-1\"[label=\"%d\" shape=square]\n", i, i);
        printf("\"%d-2\"[label=\"NULL\"]\n", i);
        printf("\"%d-2\" -> \"%d-3\"\n", i, i);
        printf("\"%d-3\"[label=\"%d\" shape=diamond]\n", i, node->ret);
        printf("\"%d-1\" -> \"%d-2\"\n", i, i);
        i+=sizeof(struct vm_vnull_t)-1;
      } break;
      case TELSEIF:
      case TIF: {
        struct vm_tif_t *node = (struct vm_tif_t *)&obj->ast.buffer[i];
        if(obj->ast.buffer[i] == TELSEIF) {
          printf("\"%d-2\"[label=\"ELSEIF\"]\n", i);
        } else {
          printf("\"%d-2\"[label=\"IF\"]\n", i);
        }
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
        struct vm_lparen_t *node = (struct vm_lparen_t *)&obj->ast.buffer[i];
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
        uint16_t x = 0;
        struct vm_ttrue_t *node = (struct vm_ttrue_t *)&obj->ast.buffer[i];
        printf("\"%d-1\"[label=\"%d\" shape=square]\n", i, i);
        if((obj->ast.buffer[i]) == TFALSE) {
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
        i+=sizeof(struct vm_ttrue_t)+(sizeof(node->go[0])*node->nrgo)-1;
      } break;
      case TFUNCTION: {
        uint16_t x = 0;
        struct vm_tfunction_t *node = (struct vm_tfunction_t *)&obj->ast.buffer[i];
        printf("\"%d-1\"[label=\"%d\" shape=square]\n", i, i);
        printf("\"%d-2\"[label=\"%s\"]\n", i, rule_functions[node->token].name);
        for(x=0;x<node->nrgo;x++) {
          printf("\"%d-2\" -> \"%d-%d\"\n", i, i, x+3);
          printf("\"%d-%d\"[label=\"%d\" shape=square]\n", i, x+3, node->go[x]);
        }
        printf("\"%d-2\" -> \"%d-%d\"\n", i, i, x+4);
        printf("\"%d-%d\"[label=\"%d\" shape=diamond]\n", i, x+4, node->ret);
        printf("\"%d-1\" -> \"%d-2\"\n", i, i);
        i+=sizeof(struct vm_tfunction_t)+(sizeof(node->go[0])*node->nrgo)-1;
      } break;
      case TVAR: {
        struct vm_tvar_t *node = (struct vm_tvar_t *)&obj->ast.buffer[i];
        printf("\"%d-1\"[label=\"%d\" shape=square]\n", i, i);
        printf("\"%d-2\"[label=\"TVAR\"]\n", i);
        if(node->go > 0) {
          printf("\"%d-2\" -> \"%d-4\"\n", i, i);
        }
        printf("\"%d-2\" -> \"%d-3\"\n", i, i);
        printf("\"%d-2\" -> \"%d-5\" [arrowhead=none,style=dotted]\n", i, i);
        printf("\"%d-3\"[label=\"%d\" shape=diamond]\n", i, node->ret);
        if(node->go > 0) {
          printf("\"%d-4\"[label=\"%d\" shape=square]\n", i, node->go);
        }
        printf("\"%d-5\"[label=\"%d\" shape=rectangle,style=dotted]\n", i, node->value);
        printf("\"%d-1\" -> \"%d-2\"\n", i, i);
        i+=sizeof(struct vm_tvar_t)-1;
      } break;
      case TVALUE: {
        struct vm_tvalue_t *node = (struct vm_tvalue_t *)&obj->ast.buffer[i];
        printf("\"%d-1\"[label=\"%d\" shape=rectangle,style=dotted]\n", i, i);
        printf("\"%d-2\"[label=\"%s\", style=dotted]\n", i, node->token);
        if(node->go > 0) {
          printf("\"%d-2\" -> \"%d-4\"\n", i, i);
          printf("\"%d-4\"[label=\"%d\" shape=square,syle=dotted]\n", i, node->go);
        }
        printf("\"%d-1\" -> \"%d-2\" [arrowhead=none,style=dotted]\n", i, i);
        i+=sizeof(struct vm_tvalue_t)+strlen((char *)node->token);
      } break;
      case TEVENT: {
        struct vm_tevent_t *node = (struct vm_tevent_t *)&obj->ast.buffer[i];
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
        struct vm_tnumber_t *node = (struct vm_tnumber_t *)&obj->ast.buffer[i];
        printf("\"%d-1\"[label=\"%d\" shape=square]\n", i, i);
        printf("\"%d-2\"[label=\"%s\"]\n", i, node->token);
        printf("\"%d-1\" -> \"%d-2\"\n", i, i);
        printf("\"%d-3\"[label=\"%d\" shape=diamond]\n", i, node->ret);
        printf("\"%d-2\" -> \"%d-3\"\n", i, i);
        i+=sizeof(struct vm_tnumber_t)+strlen((char *)node->token)-1;
      } break;
      case VINTEGER: {
        struct vm_vinteger_t *node = (struct vm_vinteger_t *)&obj->ast.buffer[i];
        printf("\"%d-1\"[label=\"%d\" shape=square]\n", i, i);
        printf("\"%d-2\"[label=\"%d\"]\n", i, node->value);
        printf("\"%d-1\" -> \"%d-2\"\n", i, i);
        printf("\"%d-3\"[label=\"%d\" shape=diamond]\n", i, node->ret);
        printf("\"%d-2\" -> \"%d-3\"\n", i, i);
        i+=sizeof(struct vm_vinteger_t)-1;
      } break;
      case VFLOAT: {
        struct vm_vfloat_t *node = (struct vm_vfloat_t *)&obj->ast.buffer[i];
        float v = 0;
        uint322float(node->value, &v);
        printf("\"%d-1\"[label=\"%d\" shape=square]\n", i, i);
        printf("\"%d-2\"[label=\"%g\"]\n", i, v);
        printf("\"%d-1\" -> \"%d-2\"\n", i, i);
        printf("\"%d-3\"[label=\"%d\" shape=diamond]\n", i, node->ret);
        printf("\"%d-2\" -> \"%d-3\"\n", i, i);
        i+=sizeof(struct vm_vfloat_t)-1;
      } break;
      case TOPERATOR: {
        struct vm_toperator_t *node = (struct vm_toperator_t *)&obj->ast.buffer[i];
        printf("\"%d-1\"[label=\"%d\" shape=square]\n", i, i);
        printf("\"%d-3\"[label=\"%d\" shape=square]\n", i, node->left);
        printf("\"%d-4\"[label=\"%d\" shape=square]\n", i, node->right);
        printf("\"%d-2\"[label=\"%s\"]\n", i, rule_operators[node->token].name);
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
  printf("\n");
  print_ast(obj);
}
#endif
/*LCOV_EXCL_STOP*/

static uint32_t vm_value_set(struct rules_t *obj, uint16_t step, uint16_t ret) {
  uint16_t out = 0;
  uint8_t step_type = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  uint8_t is_mmu_ast = 0, is_mmu_varstack = 0;
#endif

#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  if(obj->ast.buffer >= (void *)MMU_SEC_HEAP) {
    is_mmu_ast = 1;
  }
  if((void *)obj->varstack >= (void *)MMU_SEC_HEAP) {
    is_mmu_varstack = 1;
  }

  if(is_mmu_varstack == 1) {
    out = mmu_get_uint16(&obj->varstack->nrbytes);
  } else {
#endif
    out = obj->varstack->nrbytes;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  }
  if(is_mmu_ast == 1) {
    step_type = mmu_get_uint8(&obj->ast.buffer[step]);
  } else {
#endif
    step_type = obj->ast.buffer[step];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  }
#endif

#ifdef DEBUG
  printf("%s %d %d\n", __FUNCTION__, __LINE__, out);
#endif

  uint16_t size = out+rule_max_var_bytes();
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  if(is_mmu_varstack == 1) {
    if(out+size >= mmu_get_uint16(&obj->varstack->maxsize)) {
      /*
       * Varstack is too small
       */
      return -2;
    }
  } else {
#endif
    if(out+size >= obj->varstack->maxsize) {
      /*
       * Varstack is too small
       */
      return -2;
    }
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  }
#endif

  switch(step_type) {
    case TNUMBER: {
      uint32_t var = 0;
      struct vm_tnumber_t *node = (struct vm_tnumber_t *)&obj->ast.buffer[step];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      if(is_mmu_ast == 1) {
        uint16_t len = 0, x = 0;
        while(mmu_get_uint8(&node->token[++len]) != 0);
        char cpy[len+1];
        memset(&cpy, 0, len+1);
        for(x=0;x<len;x++) {
          cpy[x] = mmu_get_uint8(&node->token[x]);
        }
        var = atoi(cpy);
      } else {
#endif
        var = atoi((char *)node->token);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
#endif

      struct vm_vinteger_t *value = (struct vm_vinteger_t *)&obj->varstack->buffer[out];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      if(is_mmu_varstack == 1) {
        mmu_set_uint8(&value->type, VINTEGER);
        mmu_set_uint16(&value->ret, ret);
        value->value = var;
        mmu_set_uint16(&obj->varstack->nrbytes, size);
        mmu_set_uint16(&obj->varstack->bufsize, MAX(mmu_get_uint16(&obj->varstack->bufsize), size));
      } else {
#endif
        value->type = VINTEGER;
        value->ret = ret;
        value->value = var;
        obj->varstack->nrbytes = size;
        obj->varstack->bufsize = MAX(obj->varstack->bufsize, size);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
#endif
#ifdef DEBUG
      printf("%s %d %d %d\n", __FUNCTION__, __LINE__, out, (int)var);
#endif
    } break;
    case VFLOAT: {
      struct vm_vfloat_t *value = (struct vm_vfloat_t *)&obj->varstack->buffer[out];
      struct vm_vfloat_t *cpy = (struct vm_vfloat_t *)&obj->ast.buffer[step];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      if(is_mmu_varstack == 1) {
        mmu_set_uint8(&value->type, VFLOAT);
        mmu_set_uint16(&value->ret, ret);
        value->value = cpy->value;
        mmu_set_uint16(&obj->varstack->nrbytes, size);
        mmu_set_uint16(&obj->varstack->bufsize, MAX(mmu_get_uint16(&obj->varstack->bufsize), size));
      } else {
#endif
        value->type = VFLOAT;
        value->ret = ret;
        value->value = cpy->value;
        obj->varstack->nrbytes = size;
        obj->varstack->bufsize = MAX(obj->varstack->bufsize, size);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
#endif
#ifdef DEBUG
      float v = 0;
      uint322float(cpy->value, &v);
      printf("%s %d %d %g\n", __FUNCTION__, __LINE__, out, v);
#endif
    } break;
    case VINTEGER: {
      struct vm_vinteger_t *value = (struct vm_vinteger_t *)&obj->varstack->buffer[out];
      struct vm_vinteger_t *cpy = (struct vm_vinteger_t *)&obj->ast.buffer[step];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      if(is_mmu_varstack == 1) {
        mmu_set_uint8(&value->type, VINTEGER);
        mmu_set_uint16(&value->ret, ret);
        value->value = cpy->value;
        mmu_set_uint16(&obj->varstack->nrbytes, size);
        mmu_set_uint16(&obj->varstack->bufsize, MAX(mmu_get_uint16(&obj->varstack->bufsize), size));
      } else {
#endif
        value->type = VINTEGER;
        value->ret = ret;
        value->value = cpy->value;
        obj->varstack->nrbytes = size;
        obj->varstack->bufsize = MAX(obj->varstack->bufsize, size);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
#endif
#ifdef DEBUG
      printf("%s %d %d %d\n", __FUNCTION__, __LINE__, out, cpy->value);
#endif
    } break;
    case VNULL: {
      struct vm_vnull_t *value = (struct vm_vnull_t *)&obj->varstack->buffer[out];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      if(is_mmu_varstack == 1) {
        mmu_set_uint8(&value->type, VNULL);
        mmu_set_uint16(&value->ret, ret);
        mmu_set_uint16(&obj->varstack->nrbytes, size);
        mmu_set_uint16(&obj->varstack->bufsize, MAX(mmu_get_uint16(&obj->varstack->bufsize), size));
      } else {
#endif
        value->type = VNULL;
        value->ret = ret;
        obj->varstack->nrbytes = size;
        obj->varstack->bufsize = MAX(obj->varstack->bufsize, size);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
#endif
#ifdef DEBUG
      printf("%s %d %d NULL\n", __FUNCTION__, __LINE__, out);
#endif
    } break;
    /* LCOV_EXCL_START*/
    default: {
      logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
      return -1;
    } break;
    /* LCOV_EXCL_STOP*/
  }

  return out;
}

static int16_t vm_value_upd_pos(struct rules_t *obj, uint16_t val, uint16_t step) {
  uint16_t type = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  uint8_t is_mmu = 0;

  if(obj->ast.buffer >= (void *)MMU_SEC_HEAP) {
    is_mmu = 1;
  }

  if(is_mmu == 1) {
    type = mmu_get_uint8(&obj->ast.buffer[step]);
  } else {
#endif
    type = obj->ast.buffer[step];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  }
#endif
  switch(type) {
    case TOPERATOR: {
      struct vm_toperator_t *node = (struct vm_toperator_t *)&obj->ast.buffer[step];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      if(is_mmu == 1) {
        mmu_set_uint16(&node->value, val);
      } else {
#endif
        node->value = val;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
#endif
    } break;
    case TVAR: {
      struct vm_tvar_t *node = (struct vm_tvar_t *)&obj->ast.buffer[step];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      if(is_mmu == 1) {
        mmu_set_uint16(&node->value, val);
      } else {
#endif
        node->value = val;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
#endif
    } break;
    case TFUNCTION: {
      struct vm_tfunction_t *node = (struct vm_tfunction_t *)&obj->ast.buffer[step];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      if(is_mmu == 1) {
        mmu_set_uint16(&node->value, val);
      } else {
#endif
        node->value = val;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
#endif
    } break;
    case VNULL: {
    } break;
    /* LCOV_EXCL_START*/
    default: {
      logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
      return -1;
    } break;
    /* LCOV_EXCL_STOP*/
  }
  return 0;
}

static int32_t vm_value_clone(struct rules_t *obj, unsigned char *val) {
  int16_t ret = 0;
  uint8_t valtype = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  uint8_t is_mmu = 0;

  if((void *)obj->varstack >= (void *)MMU_SEC_HEAP) {
    is_mmu = 1;
  }

  if(is_mmu == 1) {
    ret = mmu_get_uint16(&obj->varstack->nrbytes);
  } else {
#endif
    ret = obj->varstack->nrbytes;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  }
#endif

#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  if((void *)val >= (void *)MMU_SEC_HEAP) {
    valtype = mmu_get_uint8(&val[0]);
  } else {
#endif
    valtype = val[0];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  }
#endif

#ifdef DEBUG
  printf("%s %d %d\n", __FUNCTION__, __LINE__, ret);
#endif

  uint16_t size = ret+rule_max_var_bytes();
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  if(is_mmu == 1) {
    if(ret+size >= mmu_get_uint16(&obj->varstack->maxsize)) {
      /*
       * Varstack is too small
       */
      return -2;
    }
  } else {
#endif
    if(ret+size >= obj->varstack->maxsize) {
      /*
       * Varstack is too small
       */
      return -2;
    }
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  }
#endif

  switch(valtype) {
    case VINTEGER: {
      struct vm_vinteger_t *cpy = (struct vm_vinteger_t *)&val[0];
      struct vm_vinteger_t *value = (struct vm_vinteger_t *)&obj->varstack->buffer[ret];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      if(is_mmu == 1) {
        mmu_set_uint8(&value->type, VINTEGER);
        mmu_set_uint16(&value->ret, 0);
        value->value = cpy->value;
        mmu_set_uint16(&obj->varstack->nrbytes, size);
        mmu_set_uint16(&obj->varstack->bufsize, MAX(mmu_get_uint16(&obj->varstack->bufsize), size));
      } else {
#endif
        value->type = VINTEGER;
        value->ret = 0;
        value->value = cpy->value;
        obj->varstack->nrbytes = size;
        obj->varstack->bufsize = MAX(obj->varstack->bufsize, size);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
#endif
    } break;
    case VFLOAT: {
      struct vm_vfloat_t *cpy = (struct vm_vfloat_t *)&val[0];
      struct vm_vfloat_t *value = (struct vm_vfloat_t *)&obj->varstack->buffer[ret];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      if(is_mmu == 1) {
        mmu_set_uint8(&value->type, VFLOAT);
        mmu_set_uint16(&value->ret, 0);
        value->value = cpy->value;
        mmu_set_uint16(&obj->varstack->nrbytes, size);
        mmu_set_uint16(&obj->varstack->bufsize, MAX(mmu_get_uint16(&obj->varstack->bufsize), size));
      } else {
#endif
        value->type = VFLOAT;
        value->ret = 0;
        value->value = cpy->value;
        obj->varstack->nrbytes = size;
        obj->varstack->bufsize = MAX(obj->varstack->bufsize, size);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
#endif
    } break;
    case VNULL: {
      struct vm_vnull_t *value = (struct vm_vnull_t *)&obj->varstack->buffer[ret];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      if(is_mmu == 1) {
        mmu_set_uint8(&value->type, VNULL);
        mmu_set_uint16(&value->ret, 0);
        mmu_set_uint16(&obj->varstack->nrbytes, size);
        mmu_set_uint16(&obj->varstack->bufsize, MAX(mmu_get_uint16(&obj->varstack->bufsize), size));
      } else {
#endif
        value->type = VNULL;
        value->ret = 0;
        obj->varstack->nrbytes = size;
        obj->varstack->bufsize = MAX(obj->varstack->bufsize, size);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
#endif
    } break;
    /* LCOV_EXCL_START*/
    default: {
      logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
      return -1;
    } break;
    /* LCOV_EXCL_STOP*/
  }
  return ret;
}

static uint16_t vm_value_del(struct rules_t *obj, uint16_t idx) {
  uint16_t x = 0, ret = 0, nrbytes = 0, type = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  uint8_t is_mmu = 0;

  if((void *)obj->varstack >= (void *)MMU_SEC_HEAP) {
    is_mmu = 1;
  }

  if(is_mmu == 1) {
    nrbytes = mmu_get_uint16(&obj->varstack->nrbytes);
    type = mmu_get_uint8(&obj->varstack->buffer[idx]);
  } else {
#endif
    nrbytes = obj->varstack->nrbytes;
    type = obj->varstack->buffer[idx];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  }
#endif
  if(idx == nrbytes) {
    return -1;
  }

#ifdef DEBUG
  printf("%s %d %d\n", __FUNCTION__, __LINE__, idx);
#endif

  ret = rule_max_var_bytes();

#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  if(is_mmu == 1) {
    uint16_t i = 0;
    for(i=0;i<nrbytes-idx-ret;i++) {
      mmu_set_uint8(&obj->varstack->buffer[idx+i], mmu_get_uint8(&obj->varstack->buffer[idx+ret+i]));
    }
  } else {
#endif
    memmove(&obj->varstack->buffer[idx], &obj->varstack->buffer[idx+ret], nrbytes-idx-ret);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  }
#endif

  nrbytes -= ret;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  if(is_mmu == 1) {
    mmu_set_uint16(&obj->varstack->nrbytes, nrbytes);
    mmu_set_uint16(&obj->varstack->bufsize, MAX(mmu_get_uint16(&obj->varstack->bufsize), nrbytes));
  } else {
#endif
    obj->varstack->nrbytes = nrbytes;
    obj->varstack->bufsize = MAX(obj->varstack->bufsize, nrbytes);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  }
#endif
  /*
   * Values are linked back to their root node,
   * by their absolute position in the bytecode.
   * If a value is deleted, these positions changes,
   * so we need to update all nodes.
   */
  for(x=idx;x<nrbytes;x++) {
#ifdef DEBUG
    printf("%s %d %d\n", __FUNCTION__, __LINE__, x);
#endif
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
    if(is_mmu == 1) {
      type = mmu_get_uint8(&obj->varstack->buffer[x]);
    } else {
#endif
      type = obj->varstack->buffer[x];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
    }
#endif
    switch(type) {
      case VINTEGER: {
        struct vm_vinteger_t *node = (struct vm_vinteger_t *)&obj->varstack->buffer[x];
        uint16_t ret = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        if(is_mmu == 1) {
          ret = mmu_get_uint16(&node->ret);
        } else {
#endif
          ret = node->ret;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        }
#endif
        if(ret > 0) {
          vm_value_upd_pos(obj, x, ret);
        }
      } break;
      case VFLOAT: {
        struct vm_vfloat_t *node = (struct vm_vfloat_t *)&obj->varstack->buffer[x];
        uint16_t ret = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        if(is_mmu == 1) {
          ret = mmu_get_uint16(&node->ret);
        } else {
#endif
          ret = node->ret;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        }
#endif
        if(ret > 0) {
          vm_value_upd_pos(obj, x, ret);
        }
      } break;
      case VNULL: {
        struct vm_vnull_t *node = (struct vm_vnull_t *)&obj->varstack->buffer[x];
        uint16_t ret = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        if(is_mmu == 1) {
          ret = mmu_get_uint16(&node->ret);
        } else {
#endif
          ret = node->ret;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        }
#endif
        if(ret > 0) {
          vm_value_upd_pos(obj, x, ret);
        }
      } break;
      /* LCOV_EXCL_START*/
      default: {
        logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
        return -1;
      } break;
      /* LCOV_EXCL_STOP*/
    }
    x += rule_max_var_bytes()-1;
  }

  return ret;
}

/*LCOV_EXCL_START*/
void valprint(struct rules_t *obj, char *out, uint16_t size) {
  uint16_t x = 0, pos = 0, nrbytes = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  uint8_t is_mmu_varstack = 0, is_mmu_ast = 0;

  if((void *)obj->varstack >= (void *)MMU_SEC_HEAP) {
    is_mmu_varstack = 1;
  }
  if((void *)&obj->ast >= (void *)MMU_SEC_HEAP) {
    is_mmu_ast = 1;
  }
#endif

  memset(out, 0, size);
  /*
   * This is only used for debugging purposes
   */
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  if(is_mmu_varstack == 1) {
    nrbytes = mmu_get_uint16(&obj->varstack->nrbytes);
  } else {
#endif
    nrbytes = obj->varstack->nrbytes;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  }
#endif

  for(x=4;x<nrbytes;x++) {
    uint16_t val_ret = 0;
    uint8_t type = 0, type_ret = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
    if(is_mmu_varstack == 1) {
      type = mmu_get_uint8(&obj->varstack->buffer[x]);
    } else {
#endif
      type = obj->varstack->buffer[x];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
    }
#endif
    switch(type) {
      case VINTEGER: {
        struct vm_vinteger_t *val = (struct vm_vinteger_t *)&obj->varstack->buffer[x];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        if(is_mmu_varstack == 1) {
          val_ret = mmu_get_uint16(&val->ret);
        } else {
#endif
          val_ret = val->ret;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        }

        if(is_mmu_ast == 1) {
          type_ret = mmu_get_uint8(&obj->ast.buffer[val_ret]);
        } else {
#endif
          type_ret = obj->ast.buffer[val_ret];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        }
#endif

        switch(type_ret) {
          case TVALUE: {
            struct vm_tvalue_t *node = (struct vm_tvalue_t *)&obj->ast.buffer[val_ret];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            if(is_mmu_ast == 1) {
              uint16_t i = 0;
              while(mmu_get_uint8(&node->token[i]) != 0) {
                pos += snprintf(&out[pos], size - pos, "%c", mmu_get_uint8(&node->token[i++]));
              }
              pos += snprintf(&out[pos], size - pos, " = %d", (int)val->value);
            } else {
#endif
              pos += snprintf(&out[pos], size - pos, "%s = %d", node->token, val->value);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            }
#endif
          } break;
          case TFUNCTION: {
            struct vm_tfunction_t *node = (struct vm_tfunction_t *)&obj->ast.buffer[val_ret];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            if(is_mmu_ast == 1) {
              pos += snprintf(&out[pos], size - pos, "%s = %d", rule_functions[mmu_get_uint8(&node->token)].name, (int)val->value);
            } else {
#endif
              pos += snprintf(&out[pos], size - pos, "%s = %d", rule_functions[node->token].name, val->value);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            }
#endif
          } break;
          case TOPERATOR: {
            struct vm_toperator_t *node = (struct vm_toperator_t *)&obj->ast.buffer[val_ret];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            if(is_mmu_ast == 1) {
              pos += snprintf(&out[pos], size - pos, "%s = %d", rule_operators[mmu_get_uint8(&node->token)].name, (int)val->value);
            } else {
#endif
              pos += snprintf(&out[pos], size - pos, "%s = %d", rule_operators[node->token].name, val->value);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            }
#endif
          } break;
          default: {
            // printf("err: %s %d %d\n", __FUNCTION__, __LINE__, obj->ast.buffer[val->ret]);
            // exit(-1);
          } break;
        }
      } break;
      case VFLOAT: {
        struct vm_vfloat_t *val = (struct vm_vfloat_t *)&obj->varstack->buffer[x];
        float a = 0;
        uint322float(val->value, &a);

#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        if(is_mmu_varstack == 1) {
          val_ret = mmu_get_uint16(&val->ret);
        } else {
#endif
          val_ret = val->ret;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        }

        if(is_mmu_ast == 1) {
          type_ret = mmu_get_uint8(&obj->ast.buffer[val_ret]);
        } else {
#endif
          type_ret = obj->ast.buffer[val_ret];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        }
#endif

        switch(type_ret) {
          case TVALUE: {
            struct vm_tvalue_t *node = (struct vm_tvalue_t *)&obj->ast.buffer[val_ret];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            if(is_mmu_ast == 1) {
              uint16_t i = 0;
              while(mmu_get_uint8(&node->token[i]) != 0) {
                pos += snprintf(&out[pos], size - pos, "%c", mmu_get_uint8(&node->token[i++]));
              }
              pos += snprintf(&out[pos], size - pos, " = %g", a);
            } else {
#endif
              pos += snprintf(&out[pos], size - pos, "%s = %g", node->token, a);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            }
#endif
          } break;
          case TFUNCTION: {
            struct vm_tfunction_t *node = (struct vm_tfunction_t *)&obj->ast.buffer[val_ret];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            if(is_mmu_ast == 1) {
              pos += snprintf(&out[pos], size - pos, "%s = %g", rule_functions[mmu_get_uint8(&node->token)].name, a);
            } else {
#endif
              pos += snprintf(&out[pos], size - pos, "%s = %g", rule_functions[node->token].name, a);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            }
#endif
          } break;
          case TOPERATOR: {
            struct vm_toperator_t *node = (struct vm_toperator_t *)&obj->ast.buffer[val_ret];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            if(is_mmu_ast == 1) {
              pos += snprintf(&out[pos], size - pos, "%s = %g", rule_operators[mmu_get_uint8(&node->token)].name, a);
            } else {
#endif
              pos += snprintf(&out[pos], size - pos, "%s = %g", rule_operators[node->token].name, a);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            }
#endif
          } break;
          default: {
            // printf("err: %s %d %d\n", __FUNCTION__, __LINE__, obj->ast.buffer[val->ret]);
            // exit(-1);
          } break;
        }
      } break;
      case VNULL: {
        struct vm_vnull_t *val = (struct vm_vnull_t *)&obj->varstack->buffer[x];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        if(is_mmu_varstack == 1) {
          val_ret = mmu_get_uint16(&val->ret);
        } else {
#endif
          val_ret = val->ret;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        }

        if(is_mmu_ast == 1) {
          type_ret = mmu_get_uint8(&obj->ast.buffer[val_ret]);
        } else {
#endif
          type_ret = obj->ast.buffer[val_ret];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        }
#endif

        switch(type_ret) {
          case TVALUE: {
            struct vm_tvalue_t *node = (struct vm_tvalue_t *)&obj->ast.buffer[val_ret];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            if(is_mmu_ast == 1) {
              uint16_t i = 0;
              while(mmu_get_uint8(&node->token[i]) != 0) {
                pos += snprintf(&out[pos], size - pos, "%c", mmu_get_uint8(&node->token[i++]));
              }
              pos += snprintf(&out[pos], size - pos, " = NULL");
            } else {
#endif
              pos += snprintf(&out[pos], size - pos, "%s = NULL", node->token);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            }
#endif
          } break;
          case TFUNCTION: {
            struct vm_tfunction_t *node = (struct vm_tfunction_t *)&obj->ast.buffer[val_ret];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            if(is_mmu_ast == 1) {
              pos += snprintf(&out[pos], size - pos, "%s = NULL", rule_functions[mmu_get_uint8(&node->token)].name);
            } else {
#endif
              pos += snprintf(&out[pos], size - pos, "%s = NULL", rule_functions[node->token].name);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            }
#endif
          } break;
          case TOPERATOR: {
            struct vm_toperator_t *node = (struct vm_toperator_t *)&obj->ast.buffer[val_ret];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            if(is_mmu_ast == 1) {
              pos += snprintf(&out[pos], size - pos, "%s = NULL", rule_operators[mmu_get_uint8(&node->token)].name);
            } else {
#endif
              pos += snprintf(&out[pos], size - pos, "%s = NULL", rule_operators[node->token].name);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            }
#endif
          } break;
          default: {
            // printf("err: %s %d %d\n", __FUNCTION__, __LINE__, obj->ast.buffer[val->ret]);
            // exit(-1);
          } break;
        }
      } break;
      /* LCOV_EXCL_START*/
      default: {
        logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
        exit(-1);
      } break;
      /* LCOV_EXCL_STOP*/
    }

    x += rule_max_var_bytes()-1;

    pos += snprintf(&out[pos], size - pos, "\n");
  }

  if(rule_options.prt_token_val_cb != NULL) {
    rule_options.prt_token_val_cb(obj, &out[pos], size - pos);
  }
}
/*LCOV_EXCL_START*/

void vm_clear_values(struct rules_t *obj) {
  uint16_t i = 0, nrbytes = 0;
  uint8_t type = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  uint8_t is_mmu = 0;

  if(obj->ast.buffer >= (void *)MMU_SEC_HEAP) {
    is_mmu = 1;
  }

  if(&obj->ast >= (void *)MMU_SEC_HEAP) {
    nrbytes = mmu_get_uint16(&obj->ast.nrbytes);
  } else {
#endif
    nrbytes = obj->ast.nrbytes;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  }
#endif
  for(i=0;i<nrbytes;i++) {
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
    if(is_mmu == 1) {
      type = mmu_get_uint8(&obj->ast.buffer[i]);
    } else {
#endif
      type = obj->ast.buffer[i];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
    }
#endif

    switch(type) {
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
        struct vm_lparen_t *node = (struct vm_lparen_t *)&obj->ast.buffer[i];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        if(is_mmu == 1) {
          mmu_set_uint16(&node->value, 0);
        } else {
#endif
          node->value = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        }
#endif
        i+=sizeof(struct vm_lparen_t)-1;
      } break;
      case TFALSE:
      case TTRUE: {
        struct vm_ttrue_t *node = (struct vm_ttrue_t *)&obj->ast.buffer[i];
        uint8_t nrgo = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        if(is_mmu == 1) {
          nrgo = mmu_get_uint8(&node->nrgo);
        } else {
#endif
          nrgo = node->nrgo;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        }
#endif
        i+=sizeof(struct vm_ttrue_t)+align((sizeof(node->go[0])*nrgo), 4)-1;
      } break;
      case TFUNCTION: {
        struct vm_tfunction_t *node = (struct vm_tfunction_t *)&obj->ast.buffer[i];
        uint8_t nrgo = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        if(is_mmu == 1) {
          nrgo = mmu_get_uint8(&node->nrgo);
          mmu_set_uint16(&node->value, 0);
        } else {
#endif
          nrgo = node->nrgo;
          node->value = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        }
#endif
        i+=sizeof(struct vm_tfunction_t)+align((sizeof(node->go[0])*nrgo), 4)-1;
      } break;
      case TVAR: {
        i+=sizeof(struct vm_tvar_t)-1;
      } break;
      case TVALUE: {
        struct vm_tvalue_t *node = (struct vm_tvalue_t *)&obj->ast.buffer[i];
        uint16_t x = 0, go = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        if(is_mmu == 1) {
          go = mmu_get_uint16(&node->go);
          mmu_set_uint16(&node->go, 0);
        } else {
#endif
          go = node->go;
          node->go = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        }
#endif
        if(rule_options.clr_token_val_cb != NULL && go > 0) {
          rule_options.clr_token_val_cb(obj, go);
        }
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        if(is_mmu == 1) {
          while(mmu_get_uint8(&node->token[++x]) != 0);
        } else {
#endif
          x = strlen((char *)node->token);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        }
#endif
        i+=sizeof(struct vm_tvalue_t)+align(x+1, 4)-1;
      } break;
      case TEVENT: {
        struct vm_tevent_t *node = (struct vm_tevent_t *)&obj->ast.buffer[i];
        uint16_t x = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        if(is_mmu == 1) {
          while(mmu_get_uint8(&node->token[++x]) != 0);
        } else {
#endif
          x = strlen((char *)node->token);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        }
#endif
        i+=sizeof(struct vm_tevent_t)+align(x+1, 4)-1;
      } break;
      case TNUMBER: {
        struct vm_tnumber_t *node = (struct vm_tnumber_t *)&obj->ast.buffer[i];
        uint16_t x = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        if(is_mmu == 1) {
          while(mmu_get_uint8(&node->token[++x]) != 0);
        } else {
#endif
          x = strlen((char *)node->token);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        }
#endif
        i+=sizeof(struct vm_tnumber_t)+align(x+1, 4)-1;
      } break;
      case VINTEGER: {
        i+=sizeof(struct vm_vinteger_t)-1;
      } break;
      case VFLOAT: {
        i+=sizeof(struct vm_vfloat_t)-1;
      } break;
      case TOPERATOR: {
        struct vm_toperator_t *node = (struct vm_toperator_t *)&obj->ast.buffer[i];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        if(is_mmu == 1) {
          mmu_set_uint16(&node->value, 0);
        } else {
#endif
          node->value = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        }
#endif
        i+=sizeof(struct vm_toperator_t)-1;
      } break;
      /* LCOV_EXCL_START*/
      default: {
        logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
        exit(-1);
        return;
      } break;
      /* LCOV_EXCL_STOP*/
    }
  }
}

static int8_t rule_running(struct rules_t *obj) {
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  if((void *)obj >= (void *)MMU_SEC_HEAP) {
    return mmu_get_uint16(&obj->cont.go) > 0;
  } else {
#endif
    return obj->cont.go > 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  }
#endif
}

int8_t rule_call(uint8_t nr) {
  if(queuenrel + 1 > MAXQUEUESIZE) {
    logprintf_P(F("Too many rules already in buffer. Ignoring this rule.\n"));
    return -1;
  }

  rulequeue[queueend].nr = nr;
  queueend = (queueend + 1) % (MAXQUEUESIZE);
  queuenrel++;
  return 0;
}

static int8_t rule_pop(uint8_t *nr) {
  if(queuenrel > 0) {
    *nr = rulequeue[queuestart].nr;

    queuestart = (queuestart + 1) % (MAXQUEUESIZE);
    queuenrel--;
    return 0;
  }
  return -1;
}

int8_t rule_run(struct rules_t *obj, uint8_t validate) {
  int16_t go = 0, ret = -1, i = -1, start = -1;
  uint16_t goval = 0, retval = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  uint8_t is_mmu_varstack = 0, is_mmu_obj = 0, is_mmu_ast = 0;
#endif
  go = start = 0;

  if(obj->ctx.go != NULL) {
    struct rules_t *newctx = obj->ctx.go;
    while(newctx->ctx.go != NULL) {
      newctx = newctx->ctx.go;
    }
    obj = newctx;
  }

#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  if((void *)obj >= (void *)MMU_SEC_HEAP) {
    is_mmu_obj = 1;
  }
  if((void *)obj->varstack >= (void *)MMU_SEC_HEAP) {
    is_mmu_varstack = 1;
  }
  if(obj->ast.buffer >= (void *)MMU_SEC_HEAP) {
    is_mmu_ast = 1;
  }
#endif

  {
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
    if(is_mmu_obj == 1) {
      goval = mmu_get_uint16(&obj->cont.go);
      retval = mmu_get_uint16(&obj->cont.ret);
    } else {
#endif
      goval = obj->cont.go;
      retval = obj->cont.ret;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
    }
#endif
  }

  if(goval == 0 && retval == 0) {
#ifdef DEBUG
    printf("-----------------------\n");
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
    if(is_mmu_obj == 1) {
      printf("%s %d ", __FUNCTION__, mmu_get_uint8(&obj->nr));
    } else {
#endif
      printf("%s %d ", __FUNCTION__, obj->nr);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
    }
#endif
    if(validate == 1) {
      printf("[validation]\n");
    } else {
      printf("[live]\n");
    }
    printf("-----------------------\n");
#endif
  }

  {
    struct vm_tstart_t *node = (struct vm_tstart_t *)&obj->ast.buffer[go];
    if(goval > 0) {
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      if(is_mmu_obj == 1) {
        go = mmu_get_uint16(&obj->cont.go);
        ret = mmu_get_uint16(&obj->cont.ret);
        mmu_set_uint16(&obj->cont.go, 0);
        mmu_set_uint16(&obj->cont.ret, 0);
      } else {
#endif
        go = obj->cont.go;
        ret = obj->cont.ret;
        obj->cont.go = 0;
        obj->cont.ret = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
#endif
    } else {
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      if(is_mmu_ast == 1) {
        go = mmu_get_uint16(&node->go);
      } else {
#endif
        go = node->go;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
#endif
      vm_clear_values(obj);
    }
  }

  if(go > -1) {
#ifdef ESP8266
    delay(0);
#endif

/*LCOV_EXCL_START*/
#ifdef DEBUG
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
    if(is_mmu_obj == 1) {
      printf("rule: %d, " mmu_get_uint8(&obj->nr));
    } else {
#endif
      printf("rule: %d, " obj->nr);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
    }
#endif
    printf("goto: %3d, ret: %3d, ", go, ret);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
    if((void *)&obj->ast >= (void *)MMU_SEC_HEAP) {
      printf("bytes: %3d\n", mmu_get_uint8(&obj->ast.nrbytes));
    } else {
#endif
      printf("bytes: %3d\n", obj->ast.nrbytes);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
    }
    if((void *)&obj->ast >= (void *)MMU_SEC_HEAP) {
      printf("AST stack is\t%3d bytes", mmu_get_uint16(&obj->ast.nrbytes));
    } else {
#endif
      printf("AST stack is\t%3d bytes", obj->ast.nrbytes);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
    }
    if(is_mmu_varstack == 1) {
      printf(", local stack is\t%3d bytes\n", mmu_get_uint16(&obj->varstack->nrbytes));
    } else {
#endif
      printf(", local stack is\t%3d bytes\n", obj->varstack->nrbytes);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
    }
    if((void *)&obj->ast >= (void *)MMU_SEC_HEAP) {
      printf("AST bufsize is\t%3d bytes", mmu_get_uint16(&obj->ast.bufsize));
    } else {
#endif
      printf("AST bufsize is\t%3d bytes", obj->ast.bufsize);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
    }
    if(is_mmu_varstack == 1) {
      printf(", local bufsize is\t%3d bytes\n", mmu_get_uint16(&obj->varstack->bufsize));
    } else {
#endif
      printf(", local bufsize is\t%3d bytes\n", obj->varstack->bufsize);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
    }
#endif

    {
      char out[1024];
      valprint(obj, (char *)&out, 1024);
      printf("%s\n", out);
    }
#endif
/*LCOV_EXCL_STOP*/

    uint8_t go_type = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
    if(is_mmu_ast == 1) {
      go_type = mmu_get_uint8(&obj->ast.buffer[go]);
    } else {
#endif
      go_type = obj->ast.buffer[go];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
    }
#endif

    switch(go_type) {
      case TEVENT: {
        struct vm_tevent_t *node = (struct vm_tevent_t *)&obj->ast.buffer[go];
        uint16_t tmp = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        if(is_mmu_ast == 1) {
          tmp = mmu_get_uint16(&node->ret);
        } else {
#endif
          tmp = node->ret;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        }
#endif
        if(tmp == 0) {
          uint16_t node_go = 0, node_ret = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          if(is_mmu_ast == 1) {
            node_go = mmu_get_uint16(&node->go);
            node_ret = mmu_get_uint16(&node->ret);
          } else {
#endif
            node_go = node->go;
            node_ret = node->ret;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          }
#endif
          if(node_go == ret) {
            ret = go;
            go = node_ret;
          } else {
            go = node_go;
            ret = go;
          }
        } else {
          if(rule_options.event_cb == NULL) {
            /* LCOV_EXCL_START*/
            logprintf_P(F("FATAL: No 'event_cb' set to handle events"));
            return -1;
            /* LCOV_EXCL_STOP*/
          }

#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          if(is_mmu_obj == 1 && is_mmu_ast == 1) {
            mmu_set_uint16(&obj->cont.ret, go);
            mmu_set_uint16(&obj->cont.go, mmu_get_uint16(&node->ret));
          } else if(is_mmu_obj == 1 && is_mmu_ast == 0) {
            mmu_set_uint16(&obj->cont.ret, go);
            mmu_set_uint16(&obj->cont.go, node->ret);
          } else if(is_mmu_obj == 0 && is_mmu_ast == 1) {
            obj->cont.ret = go;
            obj->cont.go = mmu_get_uint16(&node->ret);
          } else if(is_mmu_obj == 0 && is_mmu_ast == 0) {
#endif
            obj->cont.ret = go;
            obj->cont.go = node->ret;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          }
#endif

          /*
           * Tail recursive
           */
          uint16_t len = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          uint16_t x = 0;
          if(is_mmu_ast == 1) {
            while(mmu_get_uint8(&node->token[++len]) != 0);
            char cpy[len+1];
            memset(&cpy, 0, len+1);
            for(x=0;x<len;x++) {
              cpy[x] = mmu_get_uint8(&node->token[x]);
            }
            return rule_options.event_cb(obj, cpy);
          } else {
#endif
            len = strlen((char *)node->token);
            char cpy[len+1];
            memset(&cpy, 0, len+1);
            strcpy(cpy, (char *)node->token);
            return rule_options.event_cb(obj, cpy);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          }
#endif
        }
      } break;
      case TIF: {
        struct vm_tif_t *node = (struct vm_tif_t *)&obj->ast.buffer[go];

        uint32_t val = 0;
        uint8_t has_val = 0;
        if(ret > -1) {
          uint8_t type = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          if(is_mmu_ast == 1) {
            type = mmu_get_uint8(&obj->ast.buffer[ret]);
          } else {
#endif
            type = obj->ast.buffer[ret];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          }
#endif
          switch(type) {
            case TOPERATOR: {
              uint16_t intpos = 0;
              struct vm_toperator_t *op = (struct vm_toperator_t *)&obj->ast.buffer[ret];
              struct vm_vinteger_t *tmp = NULL;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              if(is_mmu_ast == 1) {
                intpos = mmu_get_uint16(&op->value);
              } else {
#endif
                intpos = op->value;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              }
#endif
              tmp = (struct vm_vinteger_t *)&obj->varstack->buffer[intpos];

              val = tmp->value;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              if(is_mmu_ast == 1) {
                vm_value_del(obj, mmu_get_uint16(&op->value));
              } else {
#endif
                vm_value_del(obj, op->value);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              }
#endif
              has_val = 1;

              /*
               * Reassign node due to various (unsigned char *)REALLOC's
               */
              node = (struct vm_tif_t *)&obj->ast.buffer[go];
            } break;
            case LPAREN: {
              uint16_t intpos = 0;
              struct vm_lparen_t *op = (struct vm_lparen_t *)&obj->ast.buffer[ret];
              struct vm_vinteger_t *tmp = NULL;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              if(is_mmu_ast == 1) {
                intpos = mmu_get_uint16(&op->value);
              } else {
#endif
                intpos = op->value;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              }
#endif
              tmp = (struct vm_vinteger_t *)&obj->varstack->buffer[intpos];

              val = tmp->value;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              if(is_mmu_ast == 1) {
                vm_value_del(obj, mmu_get_uint16(&op->value));
              } else {
#endif
                vm_value_del(obj, op->value);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              }
#endif
              has_val = 1;

              /*
               * Reassign node due to various (unsigned char *)REALLOC's
               */
              node = (struct vm_tif_t *)&obj->ast.buffer[go];
            } break;
            case TTRUE:{
              struct vm_ttrue_t *t = (struct vm_ttrue_t *)&obj->ast.buffer[ret];
              uint16_t node_go = 0;
              uint8_t nrgo = 0, match = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              if(is_mmu_ast == 1) {
                nrgo = mmu_get_uint8(&t->nrgo);
              } else {
#endif
                nrgo = t->nrgo;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              }
#endif
              for(i=0;i<nrgo;i++) {
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                if(is_mmu_ast == 1) {
                  node_go = mmu_get_uint16(&t->go[i]);
                } else {
#endif
                  node_go = t->go[i];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                }
#endif
                if(node_go == go) {
                  match = 1;
                  break;
                }
              }
              if(match == 1) {
                uint8_t sync = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                if(is_mmu_ast == 1) {
                  sync = mmu_get_uint8(&node->sync);
                } else {
#endif
                  sync = node->sync;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                }
#endif
                if(sync == 1) {
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                  if(is_mmu_obj == 1) {
                    mmu_set_uint8(&obj->sync, mmu_get_uint8(&obj->sync)+1);
                  } else {
#endif
                    obj->sync++;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                  }
#endif
                }
              } else {
                uint8_t sync = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                if(is_mmu_ast == 1) {
                  sync = mmu_get_uint8(&node->sync);
                } else {
#endif
                  sync = node->sync;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                }
#endif
                if(sync == 1) {
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                  if(is_mmu_obj == 1) {
                    mmu_set_uint8(&obj->sync, mmu_get_uint8(&obj->sync)-1);
                  } else {
#endif
                    obj->sync--;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                  }
#endif
                }
              }
            } break;
            case TFALSE:
            case TIF:
            case TEVENT: {
            } break;
            /* LCOV_EXCL_START*/
            default: {
              logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
              return -1;
            } break;
            /* LCOV_EXCL_STOP*/
          }
        } else {
          uint8_t sync = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          if(is_mmu_ast == 1) {
            sync = mmu_get_uint8(&node->sync);
          } else {
#endif
            sync = node->sync;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          }
#endif
          if(sync == 1) {
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            if(is_mmu_obj == 1) {
              mmu_set_uint8(&obj->sync, mmu_get_uint8(&obj->sync)+1);
            } else {
#endif
              obj->sync++;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            }
#endif
          }
        }

        uint16_t false_ = 0, true_ = 0, node_go = 0, node_ret = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        if(is_mmu_ast == 1) {
          false_ = mmu_get_uint16(&node->false_);
          true_ = mmu_get_uint16(&node->true_);
          node_go = mmu_get_uint16(&node->go);
          node_ret = mmu_get_uint16(&node->ret);
        } else {
#endif
          false_ = node->false_;
          true_ = node->true_;
          node_go = node->go;
          node_ret = node->ret;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        }
#endif
        if(false_ == ret && false_ > 0) {
          ret = go;
          go = node_ret;
        } else if(true_ == ret) {
          if(false_ != 0 && ((has_val == 1 && val == 0) || validate == 1)) {
            go = false_;
            ret = go;
          } else {
            ret = go;
            go = node_ret;
          }
        } else if(node_go == ret) {
          if(false_ != 0 && has_val == 1 && val == 0 && validate == 0) {
            go = false_;
            ret = go;
          } else if((has_val == 1 && val == 1) || validate == 1) {
            ret = go;
            go = true_;
          } else {
            ret = go;
            go = node_ret;
          }
        } else {
          go = node_go;
        }
      } break;
      case TFUNCTION: {
        struct vm_tfunction_t *node = (struct vm_tfunction_t *)&obj->ast.buffer[go];

        uint16_t match = 0, tmp = go;
        uint8_t nrgo = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        if(is_mmu_ast == 1) {
          nrgo = mmu_get_uint8(&node->nrgo);
        } else {
#endif
          nrgo = node->nrgo;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        }
#endif

        for(i=0;i<nrgo;i++) {
          uint16_t node_go = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          if(is_mmu_ast == 1) {
            node_go = mmu_get_uint16(&node->go[i]);
          } else {
#endif
            node_go = node->go[i];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          }
#endif
          if(node_go == ret) {
            match = 1;
            if(i+1 < nrgo) {
              uint16_t node_go_next = 0;
              uint8_t type = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              if(is_mmu_ast == 1) {
                node_go_next = mmu_get_uint16(&node->go[i+1]);
                type = mmu_get_uint8(&obj->ast.buffer[node_go_next]);
              } else {
#endif
                node_go_next = node->go[i+1];
                type = obj->ast.buffer[node_go_next];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              }
#endif
              switch(type) {
                case TNUMBER:
                case VINTEGER:
                case VFLOAT:
                case VNULL:
                case LPAREN:
                case TOPERATOR: {
                  ret = go;
                  go = node_go_next;
                } break;
                /* LCOV_EXCL_START*/
                default: {
                  logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
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
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          if(is_mmu_ast == 1) {
            go = mmu_get_uint16(&node->go[0]);
          } else {
#endif
            go = node->go[0];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          }
#endif
        }

        if(go == 0) {
          go = tmp;
          int8_t nrgo = 0;

#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          if(is_mmu_ast == 1) {
            nrgo = mmu_get_uint8(&node->nrgo);
          } else {
#endif
            nrgo = node->nrgo;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          }
#endif

          uint16_t idx = 0, i = 0, shift = 0, c = 0;
          uint16_t values[nrgo];
          memset(&values, 0, nrgo);

#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          if(is_mmu_ast == 1) {
            idx = mmu_get_uint8(&node->token);
          } else {
#endif
            idx = node->token;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          }
#endif

          /* LCOV_EXCL_START*/
          if(idx > nr_rule_functions) {
            logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
            return -1;
          }
          /* LCOV_EXCL_STOP*/

          for(i=0;i<nrgo;i++) {
            uint16_t node_go = 0;
            uint8_t node_type = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            if(is_mmu_ast == 1) {
              node_go = mmu_get_uint16(&node->go[i]);
              node_type = mmu_get_uint8(&obj->ast.buffer[node_go]);
            } else {
#endif
              node_go = node->go[i];
              node_type = obj->ast.buffer[node_go];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            }
#endif
            switch(node_type) {
              case TNUMBER:
              case VINTEGER:
              case VFLOAT: {
                int32_t ret = vm_value_set(obj, node_go, 0);
                if(ret < 0) {
                  return ret;
                }
                values[i] = ret;

                /*
                 * Reassign node due to possible reallocs
                 */
                node = (struct vm_tfunction_t *)&obj->ast.buffer[go];
              } break;
              case LPAREN: {
                uint16_t tmp_val = 0;
                struct vm_lparen_t *tmp = (struct vm_lparen_t *)&obj->ast.buffer[node_go];
                struct vm_tgeneric_t *val = NULL;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                if(is_mmu_ast == 1) {
                  tmp_val = mmu_get_uint16(&tmp->value);
                  mmu_set_uint16(&tmp->value, 0);
                } else {
#endif
                  tmp_val = tmp->value;
                  tmp->value = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                }
                if(is_mmu_varstack == 1) {
                  val = (struct vm_tgeneric_t *)&obj->varstack->buffer[tmp_val];
                  mmu_set_uint16(&val->ret, 0);
                } else {
#endif
                  val = (struct vm_tgeneric_t *)&obj->varstack->buffer[tmp_val];
                  val->ret = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                }
#endif

                values[i] = tmp_val;
              } break;
              case TOPERATOR: {
                uint16_t tmp_val = 0;
                struct vm_toperator_t *tmp = (struct vm_toperator_t *)&obj->ast.buffer[node_go];
                struct vm_tgeneric_t *val = NULL;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                if(is_mmu_ast == 1) {
                  tmp_val = mmu_get_uint16(&tmp->value);
                  mmu_set_uint16(&tmp->value, 0);
                } else {
#endif
                  tmp_val = tmp->value;
                  tmp->value = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                }
                if(is_mmu_varstack == 1) {
                  val = (struct vm_tgeneric_t *)&obj->varstack->buffer[tmp_val];
                  mmu_set_uint16(&val->ret, 0);
                } else {
#endif
                  val = (struct vm_tgeneric_t *)&obj->varstack->buffer[tmp_val];
                  val->ret = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                }
#endif

                values[i] = tmp_val;
              } break;
              case TFUNCTION: {
                uint16_t tmp_val = 0;
                struct vm_tfunction_t *tmp = (struct vm_tfunction_t *)&obj->ast.buffer[node_go];
                struct vm_tgeneric_t *val = NULL;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                if(is_mmu_ast == 1) {
                  tmp_val = mmu_get_uint16(&tmp->value);
                  mmu_set_uint16(&tmp->value, 0);
                } else {
#endif
                  tmp_val = tmp->value;
                  tmp->value = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                }
                if(is_mmu_varstack == 1) {
                  val = (struct vm_tgeneric_t *)&obj->varstack->buffer[tmp_val];
                  mmu_set_uint16(&val->ret, 0);
                } else {
#endif
                  val = (struct vm_tgeneric_t *)&obj->varstack->buffer[tmp_val];
                  val->ret = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                }
#endif

                values[i] = tmp_val;
              } break;
              case VNULL: {
                int32_t ret = vm_value_set(obj, node_go, node_go);
                if(ret < 0) {
                  return ret;
                }
                values[i] = ret;

                /*
                 * Reassign node due to possible reallocs
                 */
                node = (struct vm_tfunction_t *)&obj->ast.buffer[go];
              } break;
              case TVAR: {
                if(rule_options.get_token_val_cb != NULL) {
                  struct vm_tvar_t *var = (struct vm_tvar_t *)&obj->ast.buffer[node_go];
                  unsigned char *val = NULL;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                  if(is_mmu_ast == 1) {
                    val = rule_options.get_token_val_cb(obj, mmu_get_uint16(&var->value));
                  } else {
#endif
                    val = rule_options.get_token_val_cb(obj, var->value);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                  }
#endif
                  /* LCOV_EXCL_START*/
                  if(val == NULL) {
                    logprintf_P(F("FATAL: 'get_token_val_cb' did not return a value"));
                    return -1;
                  }
                  /* LCOV_EXCL_STOP*/
                  int32_t ret = vm_value_clone(obj, val);
                  if(ret < 0) {
                    return ret;
                  }
                  values[i] = ret;

                  /*
                   * Reassign node due to possible reallocs
                   */
                  node = (struct vm_tfunction_t *)&obj->ast.buffer[go];
                } else {
                  /* LCOV_EXCL_START*/
                  logprintf_P(F("FATAL: No 'get_token_val_cb' set to handle variables"));
                  return -1;
                  /* LCOV_EXCL_STOP*/
                }
              } break;
              /* LCOV_EXCL_START*/
              default: {
                logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
                return -1;
              } break;
              /* LCOV_EXCL_STOP*/
            }
          }

          if(rule_functions[idx].callback(obj, nrgo, values, &c) != 0) {
            /* LCOV_EXCL_START*/
            logprintf_P(F("FATAL: function call '%s' failed"), rule_functions[idx].name);
            return -1;
            /* LCOV_EXCL_STOP*/
          }

          /*
           * Reassign node due to possible reallocs
           */
          node = (struct vm_tfunction_t *)&obj->ast.buffer[go];
          if(c > 0) {
            uint8_t c_type = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            if(is_mmu_varstack == 1) {
              c_type = mmu_get_uint8(&obj->varstack->buffer[c]);
            } else {
#endif
              c_type = obj->varstack->buffer[c];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            }
#endif
            switch(c_type) {
              case VINTEGER: {
                struct vm_vinteger_t *tmp = (struct vm_vinteger_t *)&obj->varstack->buffer[c];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                if(is_mmu_varstack == 1) {
                  mmu_set_uint16(&tmp->ret, go);
                } else {
#endif
                  tmp->ret = go;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                }
                if(is_mmu_ast == 1) {
                  mmu_set_uint16(&node->value, c);
                } else {
#endif
                  node->value = c;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                }
#endif
              } break;
              case VNULL: {
                struct vm_vnull_t *tmp = (struct vm_vnull_t *)&obj->varstack->buffer[c];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                if(is_mmu_varstack == 1) {
                  mmu_set_uint16(&tmp->ret, go);
                } else {
#endif
                  tmp->ret = go;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                }
                if(is_mmu_ast == 1) {
                  mmu_set_uint16(&node->value, c);
                } else {
#endif
                  node->value = c;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                }
#endif
              } break;
              case VFLOAT: {
                struct vm_vfloat_t *tmp = (struct vm_vfloat_t *)&obj->varstack->buffer[c];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                if(is_mmu_varstack == 1) {
                  mmu_set_uint16(&tmp->ret, go);
                } else {
#endif
                  tmp->ret = go;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                }
                if(is_mmu_ast == 1) {
                  mmu_set_uint16(&node->value, c);
                } else {
#endif
                  node->value = c;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                }
#endif
              } break;
              /* LCOV_EXCL_START*/
              default: {
                logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
                return -1;
              } break;
              /* LCOV_EXCL_STOP*/
            }
          } else {
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            if(is_mmu_ast == 1) {
              mmu_set_uint16(&node->value, 0);
            } else {
#endif
              node->value = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            }
#endif
          }

          for(i=0;i<nrgo;i++) {
            uint16_t type = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            if(is_mmu_varstack == 1) {
              type = mmu_get_uint8(&obj->varstack->buffer[values[i] - shift]);
            } else {
#endif
              type = obj->varstack->buffer[values[i] - shift];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            }
#endif
            switch(type) {
              case VFLOAT:
              case VNULL:
              case VINTEGER: {
                shift += vm_value_del(obj, values[i] - shift);

                /*
                 * Reassign node due to possible reallocs
                 */
                node = (struct vm_tfunction_t *)&obj->ast.buffer[go];
              } break;
              /* LCOV_EXCL_START*/
              default: {
                logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
                return -1;
              } break;
              /* LCOV_EXCL_STOP*/
            }
          }

          ret = go;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          if(is_mmu_ast == 1) {
            go = mmu_get_uint16(&node->ret);
          } else {
#endif
            go = node->ret;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          }
#endif
        }

      } break;
      case TOPERATOR: {
        struct vm_toperator_t *node = (struct vm_toperator_t *)&obj->ast.buffer[go];
        uint16_t right = 0, left = 0;
        uint8_t left_type = 0, right_type = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        if(is_mmu_ast == 1) {
          right = mmu_get_uint16(&node->right);
          left = mmu_get_uint16(&node->left);
          left_type = mmu_get_uint8(&obj->ast.buffer[left]);
          right_type = mmu_get_uint8(&obj->ast.buffer[right]);
        } else {
#endif
          right = node->right;
          left = node->left;
          left_type = obj->ast.buffer[left];
          right_type = obj->ast.buffer[right];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        }
#endif

        if(right == ret ||
            (
              (left_type == VFLOAT || left_type == VINTEGER || left_type == TNUMBER) &&
              (right_type == VFLOAT || right_type == VINTEGER || right_type == TNUMBER)
            )
          ) {
          uint16_t a = 0, b = 0, c = 0, step = 0;

          step = left;

          switch(left_type) {
            case TNUMBER:
            case VFLOAT:
            case VINTEGER: {
              int32_t ret = vm_value_set(obj, step, step);
              if(ret < 0) {
                return ret;
              }
              a = ret;
              /*
               * Reassign node due to possible reallocs
               */
              node = (struct vm_toperator_t *)&obj->ast.buffer[go];
            } break;
            case TOPERATOR: {
              struct vm_toperator_t *tmp = (struct vm_toperator_t *)&obj->ast.buffer[step];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              if(is_mmu_ast == 1) {
                a = mmu_get_uint16(&tmp->value);
                mmu_set_uint16(&tmp->value, 0);
              } else {
#endif
                a = tmp->value;
                tmp->value = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              }
#endif
            } break;
            case LPAREN: {
              struct vm_lparen_t *tmp = (struct vm_lparen_t *)&obj->ast.buffer[step];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              if(is_mmu_ast == 1) {
                a = mmu_get_uint16(&tmp->value);
                mmu_set_uint16(&tmp->value, 0);
              } else {
#endif
                a = tmp->value;
                tmp->value = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              }
#endif
            } break;
            case TFUNCTION: {
              struct vm_tfunction_t *tmp = (struct vm_tfunction_t *)&obj->ast.buffer[step];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              if(is_mmu_ast == 1) {
                a = mmu_get_uint16(&tmp->value);
                mmu_set_uint16(&tmp->value, 0);
              } else {
#endif
                a = tmp->value;
                tmp->value = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              }
#endif
            } break;
            case VNULL: {
              int32_t ret = vm_value_set(obj, step, step);
              if(ret < 0) {
                return ret;
              }
              a = ret;
              /*
               * Reassign node due to possible reallocs
               */
              node = (struct vm_toperator_t *)&obj->ast.buffer[go];
            } break;
            case TVAR: {
              /*
               * If vars are seperate steps
               */
              if(rule_options.get_token_val_cb != NULL) {
                struct vm_tvar_t *var = (struct vm_tvar_t *)&obj->ast.buffer[step];
                unsigned char *val = NULL;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                if(is_mmu_ast == 1) {
                  val = rule_options.get_token_val_cb(obj, mmu_get_uint16(&var->value));
                } else {
#endif
                  val = rule_options.get_token_val_cb(obj, var->value);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                }
#endif

                /* LCOV_EXCL_START*/
                if(val == NULL) {
                  logprintf_P(F("FATAL: 'get_token_val_cb' did not return a value"));
                  return -1;
                }
                /* LCOV_EXCL_STOP*/

                int32_t ret = vm_value_clone(obj, val);
                if(ret < 0) {
                  return ret;
                }
                a = ret;

                /*
                 * Reassign node due to possible reallocs
                 */
                node = (struct vm_toperator_t *)&obj->ast.buffer[go];
              } else {
                /* LCOV_EXCL_START*/
                logprintf_P(F("FATAL: No 'get_token_val_cb' set to handle variables"));
                return -1;
                /* LCOV_EXCL_STOP*/
              }
            } break;
            /* LCOV_EXCL_START*/
            default: {
              logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
              return -1;
            } break;
            /* LCOV_EXCL_STOP*/
          }

          step = right;

          switch(right_type) {
            case TNUMBER:
            case VFLOAT:
            case VINTEGER: {
              int32_t ret = vm_value_set(obj, step, step);
              if(ret < 0) {
                return ret;
              }
              b = ret;
              /*
               * Reassign node due to possible reallocs
               */
              node = (struct vm_toperator_t *)&obj->ast.buffer[go];
            } break;
            case TOPERATOR: {
              struct vm_toperator_t *tmp = (struct vm_toperator_t *)&obj->ast.buffer[step];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              if(is_mmu_ast == 1) {
                b = mmu_get_uint16(&tmp->value);
                mmu_set_uint16(&tmp->value, 0);
              } else {
#endif
                b = tmp->value;
                tmp->value = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              }
#endif
            } break;
            case LPAREN: {
              struct vm_lparen_t *tmp = (struct vm_lparen_t *)&obj->ast.buffer[step];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              if(is_mmu_ast == 1) {
                b = mmu_get_uint16(&tmp->value);
                mmu_set_uint16(&tmp->value, 0);
              } else {
#endif
                b = tmp->value;
                tmp->value = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              }
#endif
            } break;
            case TFUNCTION: {
              struct vm_tfunction_t *tmp = (struct vm_tfunction_t *)&obj->ast.buffer[step];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              if(is_mmu_ast == 1) {
                b = mmu_get_uint16(&tmp->value);
                mmu_set_uint16(&tmp->value, 0);
              } else {
#endif
                b = tmp->value;
                tmp->value = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              }
#endif
            } break;
            case VNULL: {
              int32_t ret = vm_value_set(obj, step, step);
              if(ret < 0) {
                return ret;
              }
              b = ret;
              /*
               * Reassign node due to possible reallocs
               */
              node = (struct vm_toperator_t *)&obj->ast.buffer[go];
            } break;
            case TVAR: {
              /*
               * If vars are seperate steps
               */
              if(rule_options.get_token_val_cb != NULL ) {
                struct vm_tvar_t *var = (struct vm_tvar_t *)&obj->ast.buffer[step];
                unsigned char *val = NULL;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                if(is_mmu_ast == 1) {
                  val = rule_options.get_token_val_cb(obj, mmu_get_uint16(&var->value));
                } else {
#endif
                  val = rule_options.get_token_val_cb(obj, var->value);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                }
#endif

                /* LCOV_EXCL_START*/
                if(val == NULL) {
                  logprintf_P(F("FATAL: 'get_token_val_cb' did not return a value"));
                  return -1;
                }
                /* LCOV_EXCL_STOP*/

                int32_t ret = vm_value_clone(obj, val);
                if(ret < 0) {
                  return ret;
                }
                b = ret;

                /*
                 * Reassign node due to possible reallocs
                 */
                node = (struct vm_toperator_t *)&obj->ast.buffer[go];
              } else {
                /* LCOV_EXCL_START*/
                logprintf_P(F("FATAL: No 'get_token_val_cb' set to handle variables"));
                return -1;
                /* LCOV_EXCL_STOP*/
              }
            } break;
            /* LCOV_EXCL_START*/
            default: {
              logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
              return -1;
            } break;
            /* LCOV_EXCL_STOP*/
          }

          uint8_t idx = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          if(is_mmu_ast == 1) {
            idx = mmu_get_uint8(&node->token);
          } else {
#endif
            idx = node->token;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          }
#endif

          /* LCOV_EXCL_START*/
          if(idx > nr_rule_operators) {
            logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
            return -1;
          }
          /* LCOV_EXCL_STOP*/
          if(rule_operators[idx].callback(obj, a, b, &c) != 0) {
            /* LCOV_EXCL_START*/
            logprintf_P(F("FATAL: operator call '%s' failed"), rule_operators[idx].name);
            return -1;
            /* LCOV_EXCL_STOP*/
          }

          /*
           * Reassign node due to possible (unsigned char *)REALLOC's
           * in the callbacks
           */
          node = (struct vm_toperator_t *)&obj->ast.buffer[go];

          uint8_t c_type = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          if(is_mmu_varstack == 1) {
            c_type = mmu_get_uint8(&obj->varstack->buffer[c]);
          } else {
#endif
            c_type = obj->varstack->buffer[c];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          }
#endif
          switch(c_type) {
            case VINTEGER: {
              struct vm_vinteger_t *tmp = (struct vm_vinteger_t *)&obj->varstack->buffer[c];
              /*
               * Reassign node due to possible reallocs
               */
              node = (struct vm_toperator_t *)&obj->ast.buffer[go];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              if(is_mmu_varstack == 1) {
                mmu_set_uint16(&tmp->ret, go);
              } else {
#endif
                tmp->ret = go;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              }
              if(is_mmu_ast == 1) {
                mmu_set_uint16(&node->value, c);
              } else {
#endif
                node->value = c;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              }
#endif
            } break;
            case VFLOAT: {
              struct vm_vfloat_t *tmp = (struct vm_vfloat_t *)&obj->varstack->buffer[c];
              /*
               * Reassign node due to possible reallocs
               */
              node = (struct vm_toperator_t *)&obj->ast.buffer[go];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              if(is_mmu_varstack == 1) {
                mmu_set_uint16(&tmp->ret, go);
              } else {
#endif
                tmp->ret = go;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              }
              if(is_mmu_ast == 1) {
                mmu_set_uint16(&node->value, c);
              } else {
#endif
                node->value = c;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              }
#endif
            } break;
            case VNULL: {
              struct vm_vnull_t *tmp = (struct vm_vnull_t *)&obj->varstack->buffer[c];
              /*
               * Reassign node due to possible reallocs
               */
              node = (struct vm_toperator_t *)&obj->ast.buffer[go];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              if(is_mmu_varstack == 1) {
                mmu_set_uint16(&tmp->ret, go);
              } else {
#endif
                tmp->ret = go;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              }
              if(is_mmu_ast == 1) {
                mmu_set_uint16(&node->value, c);
              } else {
#endif
                node->value = c;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              }
#endif
            } break;
            /* LCOV_EXCL_START*/
            default: {
              logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
              return -1;
            } break;
            /* LCOV_EXCL_STOP*/
          }

          vm_value_del(obj, MAX(a, b));

          /*
           * Reassign node due to possible reallocs
           */
          node = (struct vm_toperator_t *)&obj->ast.buffer[go];

          vm_value_del(obj, MIN(a, b));

          /*
           * Reassign node due to possible reallocs
           */
          node = (struct vm_toperator_t *)&obj->ast.buffer[go];

          ret = go;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          if(is_mmu_ast == 1) {
            go = mmu_get_uint16(&node->ret);
          } else {
#endif
            go = node->ret;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          }
#endif
        } else if(left == ret) {
          ret = go;
          go = right;
        } else {
          ret = go;
          go = left;
        }
      } break;
      case TNUMBER:
      case VFLOAT:
      case VINTEGER: {
        int tmp = ret;
        ret = go;
        go = tmp;
      } break;
      // case TSTRING: {
        // int tmp = ret;
        // ret = go;
        // go = tmp;
      // } break;
      case VNULL: {
        int tmp = ret;
        ret = go;
        go = tmp;
      } break;
      case LPAREN: {
        struct vm_lparen_t *node = (struct vm_lparen_t *)&obj->ast.buffer[go];
        uint16_t node_go = 0, type = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        if(is_mmu_ast == 1) {
          node_go = mmu_get_uint16(&node->go);
          type = mmu_get_uint8(&obj->ast.buffer[node_go]);
        } else {
#endif
          node_go = node->go;
          type = obj->ast.buffer[node_go];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        }
#endif
        if(node_go == ret) {
          switch(type) {
            case TOPERATOR: {
              struct vm_toperator_t *tmp = (struct vm_toperator_t *)&obj->ast.buffer[ret];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              if(is_mmu_ast == 1) {
                mmu_set_uint16(&node->value, mmu_get_uint16(&tmp->value));
                mmu_set_uint16(&tmp->value, 0);
              } else {
#endif
                node->value = tmp->value;
                tmp->value = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              }
#endif
            } break;
            case LPAREN: {
              struct vm_lparen_t *tmp = (struct vm_lparen_t *)&obj->ast.buffer[ret];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              if(is_mmu_ast == 1) {
                mmu_set_uint16(&node->value, mmu_get_uint16(&tmp->value));
                mmu_set_uint16(&tmp->value, 0);
              } else {
#endif
                node->value = tmp->value;
                tmp->value = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              }
#endif
            } break;
            /* LCOV_EXCL_START*/
            default: {
              logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
              return -1;
            } break;
            /* LCOV_EXCL_STOP*/
          }
          ret = go;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          if(is_mmu_ast == 1) {
            go = mmu_get_uint16(&node->ret);
          } else {
#endif
            go = node->ret;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          }
#endif
        } else {
          ret = go;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          if(is_mmu_ast == 1) {
            go = mmu_get_uint16(&node->go);
          } else {
#endif
            go = node->go;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          }
#endif
        }
      } break;
      case TFALSE:
      case TTRUE: {
        struct vm_ttrue_t *node = (struct vm_ttrue_t *)&obj->ast.buffer[go];
        uint16_t ret_type = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        if(is_mmu_ast == 1) {
          ret_type = mmu_get_uint8(&obj->ast.buffer[ret]);
        } else {
#endif
          ret_type = obj->ast.buffer[ret];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        }
#endif
        switch(ret_type) {
          case TVAR: {
          } break;
          // case TOPERATOR: {
            // struct vm_toperator_t *node = (struct vm_toperator_t *)&obj->ast.buffer[ret];
            // idx = node->value;
            // node->value = 0;
          // } break;
          case TFUNCTION: {
            struct vm_tfunction_t *tmp = (struct vm_tfunction_t *)&obj->ast.buffer[ret];
            uint16_t val = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            if(is_mmu_ast == 1) {
              val = mmu_get_uint16(&tmp->value);
            } else {
#endif
              val = tmp->value;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            }
#endif
            if(val > 0) {
              vm_value_del(obj, val);
            }
            node = (struct vm_ttrue_t *)&obj->ast.buffer[go];
          } break;
          case TIF:
          case TEVENT:
          case TTRUE:
          case TFALSE: {
          } break;
          /* LCOV_EXCL_START*/
          default: {
            logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
            return -1;
          } break;
          /* LCOV_EXCL_STOP*/
        }

        // if((obj->ast.buffer[ret]) == TOPERATOR) {
          // vm_value_del(obj, idx);

          // /*
           // * Reassign node due to various (unsigned char *)REALLOC's
           // */
          // node = (struct vm_ttrue_t *)&obj->ast.buffer[go];
        // }

        uint16_t nrgo = 0, tmp = go, node_go = 0;
        uint8_t match = 0;

#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        if(is_mmu_ast == 1) {
          nrgo = mmu_get_uint8(&node->nrgo);
        } else {
#endif
          nrgo = node->nrgo;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        }
#endif

        for(i=0;i<nrgo;i++) {
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          if(is_mmu_ast == 1) {
            node_go = mmu_get_uint16(&node->go[i]);
          } else {
#endif
            node_go = node->go[i];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          }
#endif
          if(node_go == ret) {
            match = 1;
            if(i+1 < nrgo) {
              ret = go;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              if(is_mmu_ast == 1) {
                go = mmu_get_uint16(&node->go[i+1]);
              } else {
#endif
                go = node->go[i+1];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              }
#endif
            } else {
              go = 0;
            }
            break;
          }
        }

        if(match == 0) {
          ret = go;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          if(is_mmu_ast == 1) {
            go = mmu_get_uint16(&node->go[0]);
          } else {
#endif
            go = node->go[0];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          }
#endif
        }

        if(go == 0) {
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          if(is_mmu_ast == 1) {
            go = mmu_get_uint16(&node->ret);
          } else {
#endif
            go = node->ret;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          }
#endif
          ret = tmp;
        }
      } break;
      case TVAR: {
        struct vm_tvar_t *node = (struct vm_tvar_t *)&obj->ast.buffer[go];
        uint16_t node_go = 0;

#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        if(is_mmu_ast == 1) {
          node_go = mmu_get_uint16(&node->go);
        } else {
#endif
          node_go = node->go;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        }
#endif

        if(node_go == 0) {
          /*
           * Reassign node due to various (unsigned char *)REALLOC's
           */
          node = (struct vm_tvar_t *)&obj->ast.buffer[go];

          ret = go;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          if(is_mmu_ast == 1) {
            go = mmu_get_uint16(&node->ret);
          } else {
#endif
            go = node->ret;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
          }
#endif
        } else {
          /*
           * When we can find the value in the
           * prepared rule and not as a separate
           * node.
           */
          if(node_go == ret) {
            int16_t idx = 0, shift = 0;
            uint16_t node_type = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            if(is_mmu_ast == 1) {
              node_type = mmu_get_uint8(&obj->ast.buffer[node_go]);
            } else {
#endif
              node_type = obj->ast.buffer[node_go];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            }
#endif

            switch(node_type) {
              case TOPERATOR: {
                struct vm_toperator_t *tmp = (struct vm_toperator_t *)&obj->ast.buffer[ret];
                struct vm_tgeneric_t *val = NULL;
                uint16_t validx = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                if(is_mmu_ast == 1) {
                  validx = mmu_get_uint16(&tmp->value);
                  mmu_set_uint16(&tmp->value, 0);
                } else {
#endif
                  validx = tmp->value;
                  tmp->value = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                }
#endif
                val = (struct vm_tgeneric_t *)&obj->varstack->buffer[validx];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                if(is_mmu_varstack == 1) {
                  idx = validx  ;
                  mmu_set_uint16(&val->ret, go);
                } else {
#endif
                  idx = validx;
                  val->ret = go;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                }
#endif
              } break;
              case TFUNCTION: {
                struct vm_tfunction_t *tmp = (struct vm_tfunction_t *)&obj->ast.buffer[ret];
                struct vm_tgeneric_t *val = NULL;
                uint16_t validx = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                if(is_mmu_ast == 1) {
                  validx = mmu_get_uint16(&tmp->value);
                  mmu_set_uint16(&tmp->value, 0);
                } else {
#endif
                  validx = tmp->value;
                  tmp->value = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                }
#endif
                val = (struct vm_tgeneric_t *)&obj->varstack->buffer[validx];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                if(is_mmu_varstack == 1) {
                  idx = validx;
                  mmu_set_uint16(&val->ret, go);
                } else {
#endif
                  idx = validx;
                  val->ret = go;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                }
#endif
              } break;
              case TNUMBER:
              case VFLOAT:
              case VINTEGER:
              case VNULL: {
                int32_t ret = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                if(is_mmu_ast == 1) {
                  ret = vm_value_set(obj, node_go, mmu_get_uint16(&node->value));
                } else {
#endif
                  ret = vm_value_set(obj, node_go, node->value);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                }
#endif
                if(ret < 0) {
                  return ret;
                }
                idx = ret;

                /*
                 * Reassign node due to various (unsigned char *)REALLOC's
                 */
                node = (struct vm_tvar_t *)&obj->ast.buffer[go];
              } break;
              /*
               * Clone variable value to new variable
               */
              case TVAR: {
                if(rule_options.get_token_val_cb != NULL) {

                  struct vm_tvar_t *var = (struct vm_tvar_t *)&obj->ast.buffer[ret];
                  unsigned char *val = NULL;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                  if(is_mmu_ast == 1) {
                    val = rule_options.get_token_val_cb(obj, mmu_get_uint16(&var->value));
                  } else {
#endif
                    val = rule_options.get_token_val_cb(obj, var->value);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                  }
#endif
                  /* LCOV_EXCL_START*/
                  if(val == NULL) {
                    logprintf_P(F("FATAL: 'get_token_val_cb' did not return a value"));
                    return -1;
                  }
                  /* LCOV_EXCL_STOP*/
                  int32_t ret = vm_value_clone(obj, val);
                  if(ret < 0) {
                    return ret;
                  }
                  idx = ret;
                } else {
                  /* LCOV_EXCL_START*/
                  logprintf_P(F("FATAL: No 'get_token_val_cb' set to handle variables"));
                  return -1;
                  /* LCOV_EXCL_STOP*/
                }
                /*
                 * Reassign node due to possible reallocs
                 */
                node = (struct vm_tvar_t *)&obj->ast.buffer[go];
              } break;
              case LPAREN: {
                struct vm_lparen_t *tmp = (struct vm_lparen_t *)&obj->ast.buffer[ret];
                struct vm_tgeneric_t *val = NULL;
                uint16_t validx = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                if(is_mmu_ast == 1) {
                  validx = mmu_get_uint16(&tmp->value);
                  idx = mmu_get_uint16(&tmp->value) - shift;
                  mmu_set_uint16(&tmp->value, 0);
                } else {
#endif
                  validx = tmp->value;
                  idx = tmp->value - shift;
                  tmp->value = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                }
#endif
                val = (struct vm_tgeneric_t *)&obj->varstack->buffer[validx - shift];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                if(is_mmu_varstack == 1) {
                  mmu_set_uint16(&val->ret, go);
                } else {
#endif
                  val->ret = go;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
                }
#endif
              } break;
              /* LCOV_EXCL_START*/
              default: {
                logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
                return -1;
              } break;
              /* LCOV_EXCL_STOP*/
            }

            if(idx > -1) {
              if(rule_options.set_token_val_cb == NULL) {
                /* LCOV_EXCL_START*/
                logprintf_P(F("FATAL: No 'set_token_val_cb' set to handle variables"));
                return -1;
                /* LCOV_EXCL_STOP*/
              }

#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              if(is_mmu_ast == 1) {
                rule_options.set_token_val_cb(obj, mmu_get_uint16(&node->value), idx);
              } else {
#endif
                rule_options.set_token_val_cb(obj, node->value, idx);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              }
#endif
              vm_value_del(obj, idx);

              /*
               * Reassign node due to various (unsigned char *)REALLOC's
               */
              node = (struct vm_tvar_t *)&obj->ast.buffer[go];
            } else {
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              if(is_mmu_ast == 1) {
                mmu_set_uint16(&node->value, 0);
              } else {
#endif
                node->value = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
              }
#endif
            }

            ret = go;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            if(is_mmu_ast == 1) {
              go = mmu_get_uint16(&node->ret);
            } else {
#endif
              go = node->ret;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            }
#endif
          } else {
            ret = go;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            if(is_mmu_ast == 1) {
              go = mmu_get_uint16(&node->go);
            } else {
#endif
              go = node->go;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
            }
#endif
          }
        }
      } break;
      /*LCOV_EXCL_START*/
      default: {
        logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
        return -1;
      } break;
      /*LCOV_EXCL_STOP*/
    }
  }

  if(go == 0 && ret > -1) {
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
    if(is_mmu_obj == 1) {
      mmu_set_uint16(&obj->cont.go, 0);
      mmu_set_uint16(&obj->cont.ret, 0);
    } else {
#endif
      obj->cont.go = 0;
      obj->cont.ret = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
    }
#endif
    if(obj->ctx.ret != NULL) {
#ifdef DEBUG
      printf("-----------------------\n");
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      if(is_mmu_obj == 1) {
        printf("%s %d ", __FUNCTION__, mmu_get_uint8(&obj->ctx.ret->nr));
      } else {
#endif
        printf("%s %d ", __FUNCTION__, obj->ctx.ret->nr);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
#endif
      if(validate == 1) {
        printf("[continuing]\n");
      } else {
        printf("\n");
      }
      printf("-----------------------\n");
#endif

      obj->ctx.ret->ctx.go = NULL;
      obj->ctx.ret = NULL;

#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      if(is_mmu_obj == 1) {
        return mmu_get_uint8(&obj->sync);
      } else {
#endif
        return obj->sync;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
#endif
    }
    return 0;
  } else {
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
    if(is_mmu_obj == 1) {
      mmu_set_uint16(&obj->cont.go, go);
      mmu_set_uint16(&obj->cont.ret, ret);
    } else {
#endif
      obj->cont.go = go;
      obj->cont.ret = ret;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
    }
#endif
    if(go > 0) {
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      if(is_mmu_obj == 1) {
        return mmu_get_uint8(&obj->sync);
      } else {
#endif
        return obj->sync;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
      }
#endif
    } else {
      return 0;
    }
  }

  return -1;
}

int8_t rules_loop(struct rules_t **rules, uint8_t nrrules, uint8_t *nr) {
  uint8_t x = 0;
  int8_t ret = 0, active = 0;

  for(x=0;x<nrrules;x++) {
    if(rule_running(rules[x])) {
      active = 1;
      *nr = x;
      while((ret = rule_run(rules[x], 0)) == 2);

      if(ret == -1) {
        return -1;
      }
      if(ret == -2) {
        logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
        return -1;
      }
      if(ret == 0) {
        return 0;
      }
      return 1;
    }
  }
  if(active == 0) {
    uint8_t x = 0;
    if(rule_pop(&x) == 0) {
      *nr = x;
      while((ret = rule_run(rules[x], 0)) == 2);

      if(ret == -1) {
        return -1;
      }
      if(ret == -2) {
        logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
        return -1;
      }
      if(ret == 0) {
        return 0;
      }
      return 1;
    } else {
      return -2;
    }
  }
  return -2;
}

/*LCOV_EXCL_START*/
#ifdef DEBUG
#ifdef ESP8266
static void print_bytecode_mmu(struct rules_t *obj) {
  uint16_t i = 0;
  uint16_t nrbytes = 0;

  if(&obj->ast >= (void *)MMU_SEC_HEAP) {
    mmu_get_uint16(&obj->ast.nrbytes);
  }

  for(i=0;i<nrbytes;i++) {
    switch(mmu_get_uint8(&obj->ast.buffer[i])) {
      case TIF: {
        struct vm_tif_t *node = (struct vm_tif_t *)&obj->ast.buffer[i];
        Serial.printf("(TIF)[12][");
        Serial.printf("type: %d, ", mmu_get_uint8(&node->type));
        Serial.printf("ret: %d, ", mmu_get_uint16(&node->ret));
        Serial.printf("go: %d, ", mmu_get_uint16(&node->go));
        Serial.printf("sync: %d, ", mmu_get_uint8(&node->sync));
        Serial.printf("true_: %d, ", mmu_get_uint16(&node->true_));
        Serial.printf("false: %d]\n", mmu_get_uint16(&node->false_));
        i += sizeof(struct vm_tif_t)-1;
      } break;
      case LPAREN: {
        struct vm_lparen_t *node = (struct vm_lparen_t *)&obj->ast.buffer[i];
        Serial.printf("(LPAREN)[8][");
        Serial.printf("type: %d, ", mmu_get_uint8(&node->type));
        Serial.printf("ret: %d, ", mmu_get_uint16(&node->ret));
        Serial.printf("go: %d, ", mmu_get_uint16(&node->go));
        Serial.printf("value: %d]\n", mmu_get_uint16(&node->value));
        i += sizeof(struct vm_lparen_t)-1;
      } break;
      case TVAR: {
        struct vm_tvar_t *node = (struct vm_tvar_t *)&obj->ast.buffer[i];

        Serial.printf("(TVAR)[%lu][", 8);
        Serial.printf("type: %d, ", mmu_get_uint8(&node->type));
        Serial.printf("ret: %d, ", mmu_get_uint16(&node->ret));
        Serial.printf("go: %d, ", mmu_get_uint16(&node->go));
        Serial.printf("value: %d]\n", mmu_get_uint16(&node->value));
        i += sizeof(struct vm_tvar_t)-1;
      } break;
      case TVALUE: {
        struct vm_tvalue_t *node = (struct vm_tvalue_t *)&obj->ast.buffer[i];

        uint16_t x = 0, len = 0;
        while(mmu_get_uint8(&node->token[++x]) != 0);
        len = x;

        Serial.printf("(TVALUE)[%lu][", 8+align(len+1, 4));
        Serial.printf("type: %d, ", mmu_get_uint8(&node->type));
        Serial.printf("go: %d, ", mmu_get_uint16(&node->go));
        Serial.printf("token: "); x = 0;
        while(x < len) {
          Serial.printf("%c", mmu_get_uint8(&node->token[x++]));
        }
        Serial.printf("]\n");
        i += sizeof(struct vm_tvalue_t)+align(len+1, 4)-1;
      } break;
      case TEVENT: {
        struct vm_tevent_t *node = (struct vm_tevent_t *)&obj->ast.buffer[i];

        uint16_t x = 0, len = 0;
        while(mmu_get_uint8(&node->token[++x]) != 0);
        len = x;

        Serial.printf("(TEVENT)[%lu][", 5+align(len+1, 4));
        Serial.printf("type: %d, ", mmu_get_uint8(&node->type));
        Serial.printf("ret: %d, ", mmu_get_uint16(&node->ret));
        Serial.printf("go: %d, ", mmu_get_uint16(&node->go));
        Serial.printf("token: "); x = 0;
        while(x < len) {
          Serial.printf("%c", mmu_get_uint8(&node->token[x++]));
        }
        Serial.printf("]\n");
        i += sizeof(struct vm_tevent_t)+align(len+1, 4)-1;
      } break;
      case TSTART: {
        struct vm_tstart_t *node = (struct vm_tstart_t *)&obj->ast.buffer[i];
        Serial.printf("(TSTART)[5][");
        Serial.printf("type: %d, ", mmu_get_uint8(&node->type));
        Serial.printf("ret: %d, ", mmu_get_uint16(&node->ret));
        Serial.printf("go: %d]\n", mmu_get_uint16(&node->go));
        i += sizeof(struct vm_tstart_t)-1;
      } break;
      /* LCOV_EXCL_START*/
      case TNUMBER: {
        struct vm_tnumber_t *node = (struct vm_tnumber_t *)&obj->ast.buffer[i];

        uint16_t x = 0, len = 0;
        while(mmu_get_uint8(&node->token[++x]) != 0);
        len = x;

        Serial.printf("(TNUMBER)[%lu][", 4+align(len+1, 4));
        Serial.printf("type: %d, ", mmu_get_uint8(&node->type));
        Serial.printf("ret: %d, ", mmu_get_uint16(&node->ret));
        Serial.printf("token: "); x = 0;
        while(x < len) {
          Serial.printf("%c", mmu_get_uint8(&node->token[x++]));
        }
        Serial.printf("]\n");
        i += sizeof(struct vm_tnumber_t)+align(len+1, 4)-1;
      } break;
      /* LCOV_EXCL_STOP*/
      case VINTEGER: {
        struct vm_vinteger_t *node = (struct vm_vinteger_t *)&obj->ast.buffer[i];
        Serial.printf("(VINTEGER)[%lu][", sizeof(struct vm_vinteger_t));
        Serial.printf("type: %d, ", mmu_get_uint8(&node->type));
        Serial.printf("ret: %d, ", mmu_get_uint16(&node->ret));
        Serial.printf("value: %d]\n", node->value);
        i += sizeof(struct vm_vinteger_t)-1;
      } break;
      case VFLOAT: {
        struct vm_vfloat_t *node = (struct vm_vfloat_t *)&obj->ast.buffer[i];
        float val = 0;
        uint322float(node->value, &val);

        Serial.printf("(VFLOAT)[%lu][", sizeof(struct vm_vfloat_t));
        Serial.printf("type: %d, ", mmu_get_uint8(&node->type));
        Serial.printf("ret: %d, ", mmu_get_uint16(&node->ret));
        Serial.printf("value: %g]\n", val);
        i += sizeof(struct vm_vfloat_t)-1;
      } break;
      case TFALSE:
      case TTRUE: {
        struct vm_ttrue_t *node = (struct vm_ttrue_t *)&obj->ast.buffer[i];

        uint8_t x = 0, nrgo = 0;
        nrgo = mmu_get_uint8(&node->nrgo);

        Serial.printf("(TTRUE)[%lu][", 4+align((sizeof(node->go[0])*nrgo), 4));
        Serial.printf("type: %d, ", mmu_get_uint8(&node->type));
        Serial.printf("ret: %d, ", mmu_get_uint16(&node->ret));
        Serial.printf("nrgo: %d, ", mmu_get_uint8(&node->nrgo));
        Serial.printf("go[");
        for(x=0;x<nrgo;x++) {
          Serial.printf("%d: %d", x, mmu_get_uint16(&node->go[x]));
          if(nrgo-1 > x) {
            Serial.printf(", ");
          }
        }
        Serial.printf("]]\n");
        i += sizeof(struct vm_ttrue_t)+align((sizeof(node->go[0])*nrgo), 4)-1;
      } break;
      case TFUNCTION: {
        struct vm_tfunction_t *node = (struct vm_tfunction_t *)&obj->ast.buffer[i];
        uint8_t x = 0, nrgo = 0;
        nrgo = mmu_get_uint8(&node->nrgo);

        Serial.printf("(TFUNCTION)[%lu][", 8+(sizeof(node->go[0])*nrgo));
        Serial.printf("type: %d, ", mmu_get_uint8(&node->type));
        Serial.printf("ret: %d, ", mmu_get_uint16(&node->ret));
        Serial.printf("token: %d, ", mmu_get_uint8(&node->token));
        Serial.printf("value: %d, ", mmu_get_uint16(&node->value));
        Serial.printf("nrgo: %d, ", mmu_get_uint8(&node->nrgo));
        Serial.printf("go[");
        for(x=0;x<nrgo;x++) {
          Serial.printf("%d: %d", x, mmu_get_uint16(&node->go[x]));
          if(nrgo-1 > x) {
            Serial.printf(", ");
          }
        }
        Serial.printf("]]\n");
        i += sizeof(struct vm_tfunction_t)+align((sizeof(node->go[0])*nrgo), 4)-1;

      } break;
      case TOPERATOR: {
        struct vm_toperator_t *node = (struct vm_toperator_t *)&obj->ast.buffer[i];
        Serial.printf("(TOPERATOR)[12][");
        Serial.printf("type: %d, ", mmu_get_uint8(&node->type));
        Serial.printf("ret: %d, ", mmu_get_uint16(&node->ret));
        Serial.printf("token: %d, ", mmu_get_uint8(&node->token));
        Serial.printf("left: %d, ", mmu_get_uint16(&node->left));
        Serial.printf("right: %d, ", mmu_get_uint16(&node->right));
        Serial.printf("value: %d]\n", mmu_get_uint16(&node->value));
        i += sizeof(struct vm_toperator_t)-1;
      } break;
      case TEOF: {
        struct vm_teof_t *node = (struct vm_teof_t *)&obj->ast.buffer[i];
        Serial.printf("(TEOF)[4][type: %d]\n", mmu_get_uint8(&node->type));
        i += sizeof(struct vm_teof_t)-1;
      } break;
      case VNULL: {
        struct vm_vnull_t *node = (struct vm_vnull_t *)&obj->ast.buffer[i];
        Serial.printf("(VNULL)[3][");
        Serial.printf("type: %d, ", mmu_get_uint8(&node->type));
        Serial.printf("ret: %d]\n", mmu_get_uint16(&node->ret));
        i += sizeof(struct vm_vnull_t)-1;
      } break;
    }
  }
}
#endif

void print_bytecode(struct rules_t *obj) {
#ifdef ESP8266
  if(obj->ast.buffer >= (void *)MMU_SEC_HEAP) {
    print_bytecode_mmu(obj);
    return;
  }
#endif
  uint16_t i = 0;
  for(i=0;i<obj->ast.nrbytes;i++) {
    printf("%d", i);
    switch(obj->ast.buffer[i]) {
      case TIF: {
        struct vm_tif_t *node = (struct vm_tif_t *)&obj->ast.buffer[i];
        printf("(TIF)[12][");
        printf("type: %d, ", node->type);
        printf("ret: %d, ", node->ret);
        printf("go: %d, ", node->go);
        printf("sync: %d, ", node->sync);
        printf("true_: %d, ", node->true_);
        printf("false: %d]\n", node->false_);
        i += sizeof(struct vm_tif_t)-1;
      } break;
      case LPAREN: {
        struct vm_lparen_t *node = (struct vm_lparen_t *)&obj->ast.buffer[i];
        printf("(LPAREN)[8][");
        printf("type: %d, ", node->type);
        printf("ret: %d, ", node->ret);
        printf("go: %d, ", node->go);
        printf("value: %d]\n", node->value);
        i += sizeof(struct vm_lparen_t)-1;
      } break;
      case TVAR: {
        struct vm_tvar_t *node = (struct vm_tvar_t *)&obj->ast.buffer[i];
        printf("(TVAR)[%u][", 8);
        printf("type: %d, ", node->type);
        printf("ret: %d, ", node->ret);
        printf("go: %d, ", node->go);
        printf("value: %d]\n", node->value);
        i += sizeof(struct vm_tvar_t)-1;
      } break;
      case TVALUE: {
        struct vm_tvalue_t *node = (struct vm_tvalue_t *)&obj->ast.buffer[i];
        printf("(TVALUE)[%u][", 8+align(strlen((char *)node->token)+1, 4));
        printf("type: %d, ", node->type);
        printf("go: %d, ", node->go);
        printf("token: %s]\n", node->token);
        i += sizeof(struct vm_tvalue_t)+align(strlen((char *)node->token)+1, 4)-1;
      } break;
      case TSTRING: {
        struct vm_vchar_t *node = (struct vm_vchar_t *)&obj->ast.buffer[i];
        printf("(TSTRING)[%u][", 8+align(strlen((char *)node->value)+1, 4));
        printf("type: %d, ", node->type);
        printf("ret: %d, ", node->ret);
        printf("value: %s]\n", node->value);
        i += sizeof(struct vm_vchar_t)+align(strlen((char *)node->value)+1, 4)-1;
      } break;
      case TEVENT: {
        struct vm_tevent_t *node = (struct vm_tevent_t *)&obj->ast.buffer[i];
        printf("(TEVENT)[%u][", 8+align(strlen((char *)node->token)+1, 4));
        printf("type: %d, ", node->type);
        printf("ret: %d, ", node->ret);
        printf("go: %d, ", node->go);
        printf("token: %s]\n", node->token);
        i += sizeof(struct vm_tevent_t)+align(strlen((char *)node->token)+1, 4)-1;
      } break;
      case TSTART: {
        struct vm_tstart_t *node = (struct vm_tstart_t *)&obj->ast.buffer[i];
        printf("(TSTART)[8][");
        printf("type: %d, ", node->type);
        printf("ret: %d, ", node->ret);
        printf("go: %d]\n", node->go);
        i += sizeof(struct vm_tstart_t)-1;
      } break;
      /* LCOV_EXCL_START*/
      case TNUMBER: {
        struct vm_tnumber_t *node = (struct vm_tnumber_t *)&obj->ast.buffer[i];
        printf("(TNUMBER)[%u][", 4+align(strlen((char *)node->token)+1, 4));
        printf("type: %d, ", node->type);
        printf("ret: %d, ", node->ret);
        printf("token: %s]\n", node->token);
        i += sizeof(struct vm_tnumber_t)+align(strlen((char *)node->token)+1, 4)-1;
      } break;
      /* LCOV_EXCL_STOP*/
      case VINTEGER: {
        struct vm_vinteger_t *node = (struct vm_vinteger_t *)&obj->ast.buffer[i];
        printf("(VINTEGER)[%lu][", sizeof(struct vm_vinteger_t));
        printf("type: %d, ", node->type);
        printf("ret: %d, ", node->ret);
        printf("value: %d]\n", node->value);
        i += sizeof(struct vm_vinteger_t)-1;
      } break;
      case VFLOAT: {
        struct vm_vfloat_t *node = (struct vm_vfloat_t *)&obj->ast.buffer[i];
        float val = 0;
        uint322float(node->value, &val);

        printf("(VFLOAT)[%lu][", sizeof(struct vm_vfloat_t));
        printf("type: %d, ", node->type);
        printf("ret: %d, ", node->ret);
        printf("value: %g]\n", val);
        i += sizeof(struct vm_vfloat_t)-1;
      } break;
      case TFALSE:
      case TTRUE: {
        struct vm_ttrue_t *node = (struct vm_ttrue_t *)&obj->ast.buffer[i];
        printf("(TTRUE)[%u][", 4+align((node->nrgo*sizeof(node->go[0])), 4));
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
        i += sizeof(struct vm_ttrue_t)+align((sizeof(node->go[0])*node->nrgo), 4)-1;
      } break;
      case TFUNCTION: {
        struct vm_tfunction_t *node = (struct vm_tfunction_t *)&obj->ast.buffer[i];
        printf("(TFUNCTION)[%u][", 8+align((node->nrgo*sizeof(node->go[0])), 4));
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
        i += sizeof(struct vm_tfunction_t)+align((sizeof(node->go[0])*node->nrgo), 4)-1;
      } break;
      case TOPERATOR: {
        struct vm_toperator_t *node = (struct vm_toperator_t *)&obj->ast.buffer[i];
        printf("(TOPERATOR)[12][");
        printf("type: %d, ", node->type);
        printf("ret: %d, ", node->ret);
        printf("token: %d, ", node->token);
        printf("left: %d, ", node->left);
        printf("right: %d, ", node->right);
        printf("value: %d]\n", node->value);
        i += sizeof(struct vm_toperator_t)-1;
      } break;
      case TEOF: {
        struct vm_teof_t *node = (struct vm_teof_t *)&obj->ast.buffer[i];
        printf("(TEOF)[4][type: %d]\n", node->type);
        i += sizeof(struct vm_teof_t)-1;
      } break;
      case VNULL: {
        struct vm_vnull_t *node = (struct vm_vnull_t *)&obj->ast.buffer[i];
        printf("(VNULL)[4][");
        printf("type: %d, ", node->type);
        printf("ret: %d]\n", node->ret);
        i += sizeof(struct vm_vnull_t)-1;
      } break;
    }
  }
}
#endif
/*LCOV_EXCL_STOP*/

int8_t rule_token(struct rule_stack_t *obj, uint16_t pos, unsigned char **out) {
  uint16_t bufsize = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  uint8_t is_mmu = 0;

  if(obj->buffer >= (void *)MMU_SEC_HEAP) {
    is_mmu = 1;
  }
#endif
  if(out == NULL || *out == NULL) {
    uint8_t out_type = 0;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
    if(is_mmu == 1) {
      out_type = mmu_get_uint8(&obj->buffer[pos]);
    } else {
#endif
      out_type = obj->buffer[pos];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
    }
#endif

    switch(out_type) {
      case TVALUE: {
        struct vm_tvalue_t *nodeA = (struct vm_tvalue_t *)&obj->buffer[pos];

#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        if(is_mmu == 1) {
          uint16_t x = 0, len = 0;
          while(mmu_get_uint8(&nodeA->token[++x]) != 0);
          len = x;
          x = 0;

          bufsize = sizeof(struct vm_tvalue_t)+len+1;
          if((*out = (unsigned char *)MALLOC(bufsize)) == NULL) {
            return -1;
          }
          memset(*out, 0, bufsize);

          struct vm_tvalue_t *nodeB = (struct vm_tvalue_t *)*out;

          while(x < len) {
            nodeB->token[x] = mmu_get_uint8(&nodeA->token[x]);
            x++;
          }

          nodeB->type = mmu_get_uint8(&nodeA->type);
          nodeB->go = mmu_get_uint16(&nodeA->go);
        } else {
#endif
          bufsize = sizeof(struct vm_tvalue_t)+strlen((char *)nodeA->token)+1;
          if((*out = (unsigned char *)MALLOC(bufsize)) == NULL) {
            return -1;
          }
          memset(*out, 0, bufsize);

          struct vm_tvalue_t *nodeB = (struct vm_tvalue_t *)*out;

          nodeB->type = nodeA->type;
          nodeB->go = nodeA->go;

          strcpy((char *)nodeB->token, (char *)nodeA->token);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        }
#endif
      } break;
      case VINTEGER: {
        struct vm_vinteger_t *nodeA = (struct vm_vinteger_t *)&obj->buffer[pos];

        bufsize = sizeof(struct vm_vinteger_t);
        if((*out = (unsigned char *)MALLOC(bufsize)) == NULL) {
          return -1;
        }
        memset(*out, 0, bufsize);

        struct vm_vinteger_t *nodeB = (struct vm_vinteger_t *)*out;

#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        if(is_mmu == 1) {
          nodeB->type = mmu_get_uint8(&nodeA->type);
          nodeB->ret = mmu_get_uint16(&nodeA->ret);
          nodeB->value = nodeA->value;
        } else {
#endif
          nodeB->type = nodeA->type;
          nodeB->ret = nodeA->ret;
          nodeB->value = nodeA->value;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        }
#endif
      } break;
      case VFLOAT: {
        struct vm_vfloat_t *nodeA = (struct vm_vfloat_t *)&obj->buffer[pos];

        bufsize = sizeof(struct vm_vfloat_t);
        if((*out = (unsigned char *)MALLOC(bufsize)) == NULL) {
          return -1;
        }
        memset(*out, 0, bufsize);

        struct vm_vfloat_t *nodeB = (struct vm_vfloat_t *)*out;

#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        if(is_mmu == 1) {
          nodeB->type = mmu_get_uint8(&nodeA->type);
          nodeB->ret = mmu_get_uint16(&nodeA->ret);
          nodeB->value = nodeA->value;
        } else {
#endif
          nodeB->type = nodeA->type;
          nodeB->ret = nodeA->ret;
          nodeB->value = nodeA->value;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        }
#endif
      } break;
      case VNULL: {
        struct vm_vnull_t *nodeA = (struct vm_vnull_t *)&obj->buffer[pos];

        bufsize = sizeof(struct vm_vnull_t);
        if((*out = (unsigned char *)MALLOC(bufsize)) == NULL) {
          return -1;
        }
        memset(*out, 0, bufsize);

        struct vm_vnull_t *nodeB = (struct vm_vnull_t *)*out;

#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        if(is_mmu == 1) {
          nodeB->type = mmu_get_uint8(&nodeA->type);
          nodeB->ret = mmu_get_uint16(&nodeA->ret);
        } else {
#endif
          nodeB->type = nodeA->type;
          nodeB->ret = nodeA->ret;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        }
#endif
      } break;
      default: {
        logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
        return -1;
      } break;
    }
  } else {
    switch((*out)[0]) {
      case TVALUE: {
        struct vm_tvalue_t *nodeA = (struct vm_tvalue_t *)*out;
        struct vm_tvalue_t *nodeB = (struct vm_tvalue_t *)&obj->buffer[pos];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        if(is_mmu == 1) {
          uint16_t x = 0, len = strlen((char *)nodeA->token);

          mmu_set_uint8(&nodeB->type, nodeA->type);
          mmu_set_uint16(&nodeB->go, nodeA->go);

          while(x < len) {
            mmu_set_uint8(&nodeB->token[x], nodeA->token[x]);
            x++;
          }
        } else {
#endif
          nodeB->type = nodeA->type;
          nodeB->go = nodeA->go;

          strcpy((char *)nodeB->token, (char *)nodeA->token);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        }
#endif
      } break;
      case VINTEGER: {
        struct vm_vinteger_t *nodeA = (struct vm_vinteger_t *)*out;
        struct vm_vinteger_t *nodeB = (struct vm_vinteger_t *)&obj->buffer[pos];

#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        if(is_mmu == 1) {
          mmu_set_uint8(&nodeB->type, nodeA->type);
          mmu_set_uint16(&nodeB->ret, nodeA->ret);
          nodeB->value = nodeA->value;
        } else {
#endif
          nodeB->type = nodeA->type;
          nodeB->ret = nodeA->ret;
          nodeB->value = nodeA->value;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        }
#endif
      } break;
      case VFLOAT: {
        struct vm_vfloat_t *nodeA = (struct vm_vfloat_t *)*out;
        struct vm_vfloat_t *nodeB = (struct vm_vfloat_t *)&obj->buffer[pos];

#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        if(is_mmu == 1) {
          mmu_set_uint8(&nodeB->type, nodeA->type);
          mmu_set_uint16(&nodeB->ret, nodeA->ret);
          nodeB->value = nodeA->value;
        } else {
#endif
          nodeB->type = nodeA->type;
          nodeB->ret = nodeA->ret;
          nodeB->value = nodeA->value;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        }
#endif
      } break;
      case VNULL: {
        struct vm_vnull_t *nodeA = (struct vm_vnull_t *)*out;
        struct vm_vnull_t *nodeB = (struct vm_vnull_t *)&obj->buffer[pos];

#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        if(is_mmu == 1) {
          mmu_set_uint8(&nodeB->type, nodeA->type);
          mmu_set_uint16(&nodeB->ret, nodeA->ret);
        } else {
#endif
          nodeB->type = nodeA->type;
          nodeB->ret = nodeA->ret;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
        }
#endif
      } break;
      default: {
        logprintf_P(F("FATAL: Internal error in %s #%d"), __FUNCTION__, __LINE__);
        return -1;
      } break;
    }
  }

  return 0;
}

int8_t rule_initialize(struct pbuf *input, struct rules_t ***rules, uint8_t *nrrules, struct pbuf *mempool, void *userdata) {
  assert(nrcache == 0);
  assert(vmcache == NULL);

  struct pbuf *mempool_ori = mempool;
  struct pbuf *mempool_rule = NULL;
  uint16_t nrbytes = 0, newlen = input->tot_len;
  uint16_t suggested_varstack_size = 0, max_varstack_size = 0;

#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  if((void *)input->payload >= (void *)MMU_SEC_HEAP) {
    is_mmu_input = 1;
  } else {
    is_mmu_input = 0;
  }
#endif

  while(mempool) {
    if(mempool->flags == 1) {
      break;
    }
    mempool = mempool->next;
  }
  if(mempool == NULL) {
    mempool = mempool_ori;
  }

  /*
   * Check the size of the varstack of the previous
   * ruleset. If it was bigger than the current one
   * use that value for the current varstack size.
   */
  struct rule_stack_t *stack = (struct rule_stack_t *)&((unsigned char *)mempool->payload)[mempool->len];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  if(stack >= (void *)MMU_SEC_HEAP) {
    if(mmu_get_uint16(&stack->bufsize) > max_varstack_size) {
      max_varstack_size = mmu_get_uint16(&stack->bufsize);
    }
  } else {
#endif
    if(stack->bufsize > max_varstack_size) {
      max_varstack_size = stack->bufsize;
    }
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  }
#endif
  mempool = mempool_ori;

  if(newlen == 0) {
    return 1;
  }

#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  if(is_mmu_input == 1) {
    if(mmu_get_uint8(&((char *)input->payload)[0]) == 0) {
      return 1;
    }
  } else {
#endif
    if(((char *)input->payload)[0] == 0) {
      return 1;
    }
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  }
#endif

  {
    char *a = (char *)input->payload;
    while(mempool) {
      char *b = (char *)mempool->payload;
      /*
       * Check if input is location inside this mempool
       */
      if(&a[0] >= &b[0] && &a[0] <= &b[mempool->tot_len]) {
        if((mempool->len+sizeof(struct rules_t)) >= input->len) {
          mempool = mempool->next;
          continue;
        }
      }
      if((mempool->len+sizeof(struct rules_t)) >= mempool->tot_len) {
        mempool = mempool->next;
        continue;
      } else {
        break;
      }
    }
    if(mempool == NULL) {
      logprintf_P(F("FATAL #%d: ruleset too large, out of memory"), __LINE__);
      return -1;
    }
  }

  mempool_rule = mempool;
  if((*rules = (struct rules_t **)REALLOC(*rules, sizeof(struct rules_t **)*((*nrrules)+1))) == NULL) {
    OUT_OF_MEMORY
  }
  (*rules)[*nrrules] = (struct rules_t *)&((unsigned char *)mempool->payload)[mempool->len];
  memset((*rules)[*nrrules], 0, sizeof(struct rules_t));
  mempool->len += sizeof(struct rules_t);

  if(((*rules)[*nrrules]->timestamp = (struct rule_timer_t *)MALLOC(sizeof(struct rule_timer_t))) == NULL) {
    OUT_OF_MEMORY
  }
  (*rules)[*nrrules]->userdata = userdata;
  struct rules_t *obj = (*rules)[*nrrules];

#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  if((*rules)[*nrrules] >= (void *)MMU_SEC_HEAP) {
    mmu_set_uint8(&obj->nr, (*nrrules)+1);
    mmu_set_uint8(&obj->sync, 1);
  } else {
#endif
    obj->nr = (*nrrules)+1;
    obj->sync = 1;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  }
#endif
  (*nrrules)++;

  obj->ctx.go = NULL;
  obj->ctx.ret = NULL;
  obj->ast.nrbytes = 0;
  obj->ast.bufsize = 0;

/*LCOV_EXCL_START*/
#ifdef ESP8266
  obj->timestamp->first = micros();
#else
  clock_gettime(CLOCK_MONOTONIC, &obj->timestamp->first);
#endif
/*LCOV_EXCL_STOP*/

  if(rule_prepare((char **)&input->payload, &nrbytes, &newlen) == -1) {
    FREE((*rules)[*nrrules-1]->timestamp);
    if((*rules = (struct rules_t **)REALLOC(*rules, sizeof(struct rules_t **)*((*nrrules)))) == NULL) {
      OUT_OF_MEMORY
    }
    mempool_rule->len -= sizeof(struct rules_t);
    (*nrrules)--;
    return -1;
  }

#ifdef ESP8266
  if((nrbytes % 4) != 0) {
    Serial.println("Rules AST not 4 byte aligned!");
    exit(-1);
  }
#endif

/*LCOV_EXCL_START*/
#ifdef ESP8266
  obj->timestamp->second = micros();

  logprintf_P(F("rule #%d was prepared in %d microseconds"), mmu_get_uint8(&obj->nr), obj->timestamp->second - obj->timestamp->first);
#else
  clock_gettime(CLOCK_MONOTONIC, &obj->timestamp->second);

  printf("rule #%d was prepared in %.6f seconds\n", obj->nr,
    ((double)obj->timestamp->second.tv_sec + 1.0e-9*obj->timestamp->second.tv_nsec) -
    ((double)obj->timestamp->first.tv_sec + 1.0e-9*obj->timestamp->first.tv_nsec));
#endif
/*LCOV_EXCL_STOP*/


/*LCOV_EXCL_START*/
#ifdef ESP8266
  obj->timestamp->first = micros();
#else
  clock_gettime(CLOCK_MONOTONIC, &obj->timestamp->first);
#endif
/*LCOV_EXCL_STOP*/

  {
    {
      char *a = (char *)input->payload;
      while(mempool) {
        char *b = (char *)mempool->payload;
        /*
         * Check if input is location inside this mempool
         */
        if(&a[0] >= &b[0] && &a[0] <= &b[mempool->tot_len]) {
          if((mempool->len+nrbytes) >= input->len) {
            mempool = mempool->next;
            continue;
          }
        }
        if((mempool->len+nrbytes) >= mempool->tot_len) {
          mempool = mempool->next;
          continue;
        } else {
          break;
        }
      }
      if(mempool == NULL) {
        logprintf_P(F("FATAL #%d: ruleset too large, out of memory"), __LINE__);
        FREE((*rules)[*nrrules-1]->timestamp);
        if((*rules = (struct rules_t **)REALLOC(*rules, sizeof(struct rules_t **)*((*nrrules)))) == NULL) {
          OUT_OF_MEMORY
        }
        mempool_rule->len -= sizeof(struct rules_t);
        (*nrrules)--;
        return -1;
      }
    }

#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
    if((void *)&obj->ast >= (void *)MMU_SEC_HEAP) {
      mmu_set_uint16(&obj->ast.bufsize, nrbytes);
    } else {
#endif
      obj->ast.bufsize = nrbytes;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
    }
#endif

    obj->ast.buffer = (unsigned char *)&((unsigned char *)mempool->payload)[mempool->len];

    mempool->len += nrbytes;
    memset(obj->ast.buffer, 0, nrbytes);

    if(rule_parse((char **)&input->payload, obj) == -1) {
      vm_cache_gc();
      FREE((*rules)[*nrrules-1]->timestamp);
      if((*rules = (struct rules_t **)REALLOC(*rules, sizeof(struct rules_t **)*((*nrrules)))) == NULL) {
        OUT_OF_MEMORY
      }
      mempool_rule->len -= sizeof(struct rules_t);
      (*nrrules)--;
      return -1;
    }

#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
    if(is_mmu_input == 1) {
      mmu_set_uint16(&input->len, mmu_get_uint16(&input->len) + newlen);
      if(mmu_get_uint8(&((char *)input->payload)[newlen]) == 0) {
        mmu_set_uint16(&input->len, mmu_get_uint16(&input->len) + 1);
      }
      mmu_set_uint16(&input->tot_len, mmu_get_uint16(&input->tot_len) - newlen);
    } else {
#endif
      input->len += newlen;
      if(newlen < input->tot_len) {
        if(((unsigned char *)input->payload)[newlen] == 0) {
          input->len += 1;
        }
      }
      input->tot_len -= newlen;
    }
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  }
#endif

/*LCOV_EXCL_START*/
#ifdef ESP8266
  obj->timestamp->second = micros();

#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  if(obj >= (void *)MMU_SEC_HEAP) {
    logprintf_P(F("rule #%d was parsed in %d microseconds"), mmu_get_uint8(&obj->nr), obj->timestamp->second - obj->timestamp->first);
    logprintf_P(F("bytecode is %d bytes"), mmu_get_uint16(&obj->ast.nrbytes));
  } else {
#endif
    logprintf_P(F("rule #%d was parsed in %d microseconds"), obj->nr, obj->timestamp->second - obj->timestamp->first);
    logprintf_P(F("bytecode is %d bytes"), obj->ast.nrbytes);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  }
#endif
#else
  clock_gettime(CLOCK_MONOTONIC, &obj->timestamp->second);

  printf("rule #%d was parsed in %.6f seconds\n", obj->nr,
    ((double)obj->timestamp->second.tv_sec + 1.0e-9*obj->timestamp->second.tv_nsec) -
    ((double)obj->timestamp->first.tv_sec + 1.0e-9*obj->timestamp->first.tv_nsec));

  printf("bytecode is %d bytes\n", obj->ast.nrbytes);
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
#ifdef ESP8266
  obj->timestamp->first = micros();
#else
  clock_gettime(CLOCK_MONOTONIC, &obj->timestamp->first);
#endif
/*LCOV_EXCL_STOP*/

  /*
   * If the varstack is too small rerun the rule with an
   * increased varstack until we found the safe size
   */
  int8_t ret = -2;
  uint16_t initial_varstack_size = suggested_varstack_size*2;

  while(ret == -2) {
    initial_varstack_size = suggested_varstack_size*2;

    char *a = (char *)input->payload;
    while(mempool) {
      char *b = (char *)mempool->payload;
      /*
       * Check if input is location inside this mempool
       */
      if(&a[0] >= &b[0] && &a[0] <= &b[mempool->tot_len]) {
        suggested_varstack_size = align((input->len-mempool->len-sizeof(struct rule_stack_t))-5, 4);
        if(suggested_varstack_size <= initial_varstack_size || (mempool->len+suggested_varstack_size) >= input->len) {
          mempool = mempool->next;
          suggested_varstack_size = initial_varstack_size;
          continue;
        }
      } else {
        suggested_varstack_size = align((mempool->tot_len-mempool->len)-sizeof(struct rule_stack_t)-5, 4);
      }

      if(suggested_varstack_size <= initial_varstack_size || (mempool->len+suggested_varstack_size) >= mempool->tot_len) {
        mempool = mempool->next;
        suggested_varstack_size = initial_varstack_size;
        continue;
      } else {
        break;
      }
    }
    if(mempool == NULL) {
      logprintf_P(F("FATAL #%d: ruleset too large, out of memory"), __LINE__);
      FREE((*rules)[*nrrules-1]->timestamp);
      if((*rules = (struct rules_t **)REALLOC(*rules, sizeof(struct rules_t **)*((*nrrules)))) == NULL) {
        OUT_OF_MEMORY
      }
      mempool_rule->len -= sizeof(struct rules_t);
      (*nrrules)--;
      return -1;
    }

    /*
     * The memoffset will be increased below
     * as soon as we know how many bytes
     * we maximally need.
     */
    obj->varstack = (struct rule_stack_t *)&((unsigned char *)mempool->payload)[mempool->len];
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
    if(obj->varstack >= (void *)MMU_SEC_HEAP) {
      mmu_set_uint16(&obj->varstack->nrbytes, 4);
      mmu_set_uint16(&obj->varstack->bufsize, 4);
      mmu_set_uint16(&obj->varstack->maxsize, suggested_varstack_size);
    } else {
#endif
      obj->varstack->nrbytes = 4;
      obj->varstack->bufsize = 4;
      obj->varstack->maxsize = suggested_varstack_size;
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
    }
#endif
    obj->varstack->buffer = &((unsigned char *)mempool->payload)[mempool->len+sizeof(struct rule_stack_t)];

    {
      struct pbuf *tmp = mempool_ori;
      while(tmp) {
        tmp->flags = 0;
        tmp = tmp->next;
      }
    }

    /*
     * Flag the current mempool as the one
     * holding the last varstack instance.
     */
    mempool->flags = 1;

    /*
     * Since previous varstacks are overwritten we must
     * remap the pointer to the current varstack. This
     * also makes sure that rule sets that are called from
     * other rulesets share the same varstack. This helps
     * determine how big the final varstack for all rulesets
     * together should be.
     */
    {
      uint8_t x = 0;
      for(x=0;x<*nrrules;x++) {
        (*rules)[x]->varstack = obj->varstack;
      }
    }

    memset(obj->varstack->buffer, 0, suggested_varstack_size);

    /*
     * The rule_run function will return -2
     * if the varstack is too small
     */
    while((ret = rule_run(obj, 1)) > 0);
    if(ret == -1) {
      vm_cache_gc();
      FREE((*rules)[*nrrules-1]->timestamp);
      if((*rules = (struct rules_t **)REALLOC(*rules, sizeof(struct rules_t **)*((*nrrules)))) == NULL) {
        OUT_OF_MEMORY
      }
      mempool_rule->len -= sizeof(struct rules_t);
      (*nrrules)--;
      return -1;
    }
  }

  /*
   * If a previous varstack was bigger than the current
   * varstack, use the previous varstack size. This will
   * make the final varstack shared across all rules have
   * enough space for all variables.
   */
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  if(obj->varstack >= (void *)MMU_SEC_HEAP) {
    if(max_varstack_size > mmu_get_uint16(&obj->varstack->bufsize)) {
      mmu_set_uint16(&obj->varstack->bufsize, max_varstack_size);
    }
  } else {
#endif
    if(max_varstack_size > obj->varstack->bufsize) {
      obj->varstack->bufsize = max_varstack_size;
    }
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  }
#endif

  /*
   * Reserve space for the actual maximum
   * varstack buffer size
   */
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  if(obj->varstack >= (void *)MMU_SEC_HEAP) {
    if((mmu_get_uint16(&obj->varstack->bufsize) % 4) != 0) {
      Serial.println("Rules AST not 4 byte aligned!");
      exit(-1);
    }
  } else {
#endif
    if((obj->varstack->bufsize % 4) != 0) {
      printf("Rules AST not 4 byte aligned!\n");
      exit(-1);
    }
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  }
#endif

/*LCOV_EXCL_START*/
#ifdef ESP8266
  obj->timestamp->second = micros();

#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  if(obj >= (void *)MMU_SEC_HEAP) {
    logprintf_P(F("rule #%d was executed in %d microseconds"), mmu_get_uint8(&obj->nr), obj->timestamp->second - obj->timestamp->first);
  } else {
#endif
    logprintf_P(F("rule #%d was executed in %d microseconds"), obj->nr, obj->timestamp->second - obj->timestamp->first);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  }
  if(&obj->ast >= (void *)MMU_SEC_HEAP) {
    logprintf_P(F("bytecode is %d bytes"), mmu_get_uint16(&obj->ast.nrbytes));
  } else {
#endif
    logprintf_P(F("bytecode is %d bytes"), obj->ast.nrbytes);
#if (!defined(NON32XFER_HANDLER) && defined(MMU_SEC_HEAP)) || defined(COVERALLS)
  }
#endif
#else
  clock_gettime(CLOCK_MONOTONIC, &obj->timestamp->second);

  printf("rule #%d was executed in %.6f seconds\n", obj->nr,
    ((double)obj->timestamp->second.tv_sec + 1.0e-9*obj->timestamp->second.tv_nsec) -
    ((double)obj->timestamp->first.tv_sec + 1.0e-9*obj->timestamp->first.tv_nsec));

  printf("bytecode is %d bytes\n", obj->ast.nrbytes);
#endif
/*LCOV_EXCL_STOP*/

  assert(nrcache == 0);

  return 0;
}
