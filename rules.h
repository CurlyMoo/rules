/*
  Copyright (C) CurlyMo

  This Source Code Form is subject to the terms of the Mozilla Public
  License, v. 2.0. If a copy of the MPL was not distributed with this
  file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/


#ifndef _RULES_H_
#define _RULES_H_

#include <stdint.h>

#define EPSILON  0.000001

typedef enum {
  TOPERATOR = 1,
  TFUNCTION = 2,
  TSTRING = 3,
  TNUMBER = 4,
  TEOF = 5,
  LPAREN = 6,
  RPAREN = 7,
  TCOMMA = 8,
  TIF = 9,
  TELSE = 10,
  TTHEN = 11,
  TEND = 12,
  TVAR = 13,
  TASSIGN = 14,
  TSEMICOLON = 15,
  TTRUE = 16,
  TFALSE = 17,
  TSTART = 18,
  VCHAR = 19,
  VINTEGER = 20,
  VFLOAT = 21
} token_types;

typedef struct rules_t {
  unsigned short nr;
  char *text;

  struct {
#if defined(DEBUG) or defined(ESP8266)
  #ifdef ESP8266
      unsigned long first;
      unsigned long second;
  #else
      struct timespec first;
      struct timespec second;
  #endif
#endif
  }  timestamp;

  struct {
    int parsed;
    int vars;
  } pos;

  unsigned char *bytecode;
  unsigned int nrbytes;
} rules_t;

/*
 * Each position field is the closest
 * aligned width of 11 bits.
 */
#define VM_GENERIC_FIELDS \
  uint8_t type; \
  uint16_t ret;

typedef struct vm_vchar_t {
  VM_GENERIC_FIELDS
  char value[];
} vm_char_t;

typedef struct vm_vinteger_t {
  VM_GENERIC_FIELDS
  int value;
} vm_vinteger_t;

typedef struct vm_vfloat_t {
  VM_GENERIC_FIELDS
  float value;
} vm_vfloat_t;

typedef struct vm_tgeneric_t {
  VM_GENERIC_FIELDS
} vm_tgeneric_t;

typedef struct vm_tstart_t {
  VM_GENERIC_FIELDS
  uint16_t go;
} vm_tstart_t;

typedef struct vm_tif_t {
  VM_GENERIC_FIELDS
  uint16_t go;
  uint16_t true_;
  uint16_t false_;
} vm_tif_t;

typedef struct vm_lparen_t {
  VM_GENERIC_FIELDS
  uint16_t go;
  uint16_t value;
} vm_lparen_t;

typedef struct vm_tnumber_t {
  VM_GENERIC_FIELDS
  uint16_t token;
} vm_tnumber_t;

typedef struct vm_ttrue_t {
  VM_GENERIC_FIELDS
  uint8_t nrgo;
  uint16_t go[];
} vm_ttrue_t;

typedef struct vm_tfunction_t {
  VM_GENERIC_FIELDS
  uint16_t token;
  uint16_t value;
  uint8_t nrgo;
  uint16_t go[];
} vm_tfunction_t;

typedef struct vm_tvar_t {
  VM_GENERIC_FIELDS
  uint16_t token;
  uint16_t go;
  uint16_t value;
} vm_tvar_t;

typedef struct vm_toperator_t {
  VM_GENERIC_FIELDS
  uint16_t token;
  uint16_t left;
  uint16_t right;
  uint16_t value;
} vm_toperator_t;

typedef struct vm_teof_t {
  uint8_t type;
} vm_teof_t;

int rule_initialize(char *rule, struct rules_t *obj, int depth, unsigned short validate);
void rule_gc(void);
int rule_run(struct rules_t *obj, int validate);
void valprint(struct rules_t *obj, char *out, int size);

#endif
