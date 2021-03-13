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
  TEVENT = 12,
  TCEVENT = 13,
  TEND = 14,
  TVAR = 15,
  TASSIGN = 16,
  TSEMICOLON = 17,
  TTRUE = 18,
  TFALSE = 19,
  TSTART = 20,
  VCHAR = 21,
  VINTEGER = 22,
  VFLOAT = 23,
  VNULL = 24
} token_types;

typedef struct rules_t {
  unsigned short nr;

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

  /* Continue here after we processed
   * another rule call.
   */
  struct {
    uint16_t go;
    uint16_t ret;
  } cont;

  /* To which rule do we return after
   * being called from another rule.
   */
  int caller;

  unsigned char *bytecode;
  unsigned int nrbytes;

  void *userdata;
} rules_t;

typedef struct rule_options_t {
  /*
   * Identifying callbacks
   */
  int (*is_token_cb)(struct rules_t *obj, const char *text, int *pos, int size);
  int (*is_event_cb)(struct rules_t *obj, const char *text, int *pos, int size);

  /*
   * Variables
   */
  unsigned char *(*get_token_val_cb)(struct rules_t *obj, uint16_t token);
  void (*cpy_token_val_cb)(struct rules_t *obj, uint16_t token);
  void (*clr_token_val_cb)(struct rules_t *obj, uint16_t token);
  void (*set_token_val_cb)(struct rules_t *obj, uint16_t token, uint16_t val);
  void (*prt_token_val_cb)(struct rules_t *obj, char *out, int size);

  /*
   * Events
   */
  int (*event_cb)(struct rules_t *obj, const char *name);
} rule_options_t;

extern struct rule_options_t rule_options;

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

typedef struct vm_vnull_t {
  VM_GENERIC_FIELDS
} vm_vnull_t;

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

typedef struct vm_tevent_t {
  VM_GENERIC_FIELDS
  uint16_t token;
  uint16_t go;
} vm_tevent_t;

typedef struct vm_tcevent_t {
  VM_GENERIC_FIELDS
  uint16_t token;
} vm_tcevent_t;

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

int rule_initialize(const char *text, int *pos, struct rules_t ***rules, int *nrrules, void *userdata);
void rules_gc(struct rules_t ***obj, int nrrules);
int rule_run(struct rules_t *obj, int validate);
void valprint(struct rules_t *obj, char *out, int size);

#endif
