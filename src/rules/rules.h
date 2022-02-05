/*
  Copyright (C) CurlyMo

  This Source Code Form is subject to the terms of the Mozilla Public
  License, v. 2.0. If a copy of the MPL was not distributed with this
  file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/


#ifndef _RULES_H_
#define _RULES_H_

#include <stdint.h>
#include <unistd.h>
#include "stack.h"

#ifndef ESP8266
  #define F
  #define MEMPOOL_SIZE 16000
  typedef struct pbuf {
    struct pbuf *next;
    void *payload;
    uint16_t tot_len;
    uint16_t len;
    uint8_t type;
    uint8_t flags;
    uint16_t ref;
  } pbuf;
  static uint8_t mmu_set_uint8(void *ptr, uint8_t src) { *(uint8_t *)ptr = src; return src; }
  static uint8_t mmu_get_uint8(void *ptr) { return *(uint8_t *)ptr; }
  static uint16_t mmu_set_uint16(void *ptr, uint16_t src) { *(uint16_t *)ptr = src; return src; }
  static uint16_t mmu_get_uint16(void *ptr) { return (*(uint16_t *)ptr); }

  typedef struct serial_t {
    void (*printf)(const char *fmt, ...);
    void (*println)(const char *val);
    void (*flush)(void);
  } serial_t;
  extern struct serial_t Serial;
  #define MMU_SEC_HEAP 0
#else
  #include <Arduino.h>
  #include "lwip/pbuf.h"
  #define MEMPOOL_SIZE MMU_SEC_HEAP_SIZE
#endif

#define EPSILON  0.000001

/*
 * max(sizeof(vm_vfloat_t), sizeof(vm_vinteger_t), sizeof(vm_vnull_t))
 */
#define MAX_VARSTACK_NODE_SIZE 7

#define MAX(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })

#define MIN(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a < _b ? _a : _b; })

typedef enum {
  TOPERATOR = 1,
  TFUNCTION = 2,
  TSTRING = 3,
  TNUMBER = 4,
  TNUMBER1 = 5,
  TNUMBER2 = 6,
  TNUMBER3 = 7,
  TEOF = 8,
  LPAREN = 9,
  RPAREN = 10,
  TCOMMA = 11,
  TIF = 12,
  TELSE = 13,
  TTHEN = 14,
  TEVENT = 15,
  TCEVENT = 16,
  TEND = 17,
  TVAR = 18,
  TASSIGN = 19,
  TSEMICOLON = 20,
  TTRUE = 21,
  TFALSE = 22,
  TSTART = 23,
  VCHAR = 24,
  VINTEGER = 25,
  VFLOAT = 26,
  VNULL = 27,
} token_types;

typedef struct rules_t {
  struct {
#ifdef ESP8266
    uint32_t first;
    uint32_t second;
#else
    struct timespec first;
    struct timespec second;
#endif
  } __attribute__((aligned(4))) timestamp;

  struct {
    uint16_t parsed;
    uint16_t vars;
  } __attribute__((aligned(4))) pos;

  /* Continue here after we processed
   * another rule call.
   */
  struct {
    uint16_t go;
    uint16_t ret;
  } __attribute__((aligned(4))) cont;

  /* To which rule do we return after
   * being called from another rule.
   */
  uint8_t caller;

  uint8_t nr;
  void *userdata;

  struct rule_stack_t ast;
  struct rule_stack_t varstack;

} __attribute__((aligned(4))) rules_t;

typedef struct rule_options_t {
  /*
   * Identifying callbacks
   */
  int8_t (*is_token_cb)(char *text, uint16_t size);
  int8_t (*is_event_cb)(char *text, uint16_t *pos, uint16_t size);

  /*
   * Variables
   */
  unsigned char *(*get_token_val_cb)(struct rules_t *obj, uint16_t token);
  void (*cpy_token_val_cb)(struct rules_t *obj, uint16_t token);
  int8_t (*clr_token_val_cb)(struct rules_t *obj, uint16_t token);
  void (*set_token_val_cb)(struct rules_t *obj, uint16_t token, uint16_t val);
  void (*prt_token_val_cb)(struct rules_t *obj, char *out, uint16_t size);

  /*
   * Events
   */
  int (*event_cb)(struct rules_t *obj, char *name);
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
} __attribute__((aligned(4))) vm_vchar_t;

typedef struct vm_vnull_t {
  VM_GENERIC_FIELDS
} __attribute__((aligned(4))) vm_vnull_t;

typedef struct vm_vinteger_t {
  VM_GENERIC_FIELDS
  uint32_t value;
} __attribute__((aligned(4))) vm_vinteger_t;

typedef struct vm_vfloat_t {
  VM_GENERIC_FIELDS
  uint32_t value;
} __attribute__((aligned(4))) vm_vfloat_t;

typedef struct vm_tgeneric_t {
  VM_GENERIC_FIELDS
} __attribute__((aligned(4))) vm_tgeneric_t;

typedef struct vm_tstart_t {
  VM_GENERIC_FIELDS
  uint16_t go;
} __attribute__((aligned(4))) vm_tstart_t;

typedef struct vm_tif_t {
  VM_GENERIC_FIELDS
  uint16_t go;
  uint16_t true_;
  uint16_t false_;
} __attribute__((aligned(4))) vm_tif_t;

typedef struct vm_lparen_t {
  VM_GENERIC_FIELDS
  uint16_t go;
  uint16_t value;
} __attribute__((aligned(4))) vm_lparen_t;

typedef struct vm_tnumber_t {
  VM_GENERIC_FIELDS
  uint8_t token[];
} __attribute__((aligned(4))) __vm_tnumber_t;

typedef struct vm_ttrue_t {
  VM_GENERIC_FIELDS
  uint8_t nrgo;
  uint16_t go[];
} __attribute__((aligned(4))) vm_ttrue_t;

typedef struct vm_tfunction_t {
  VM_GENERIC_FIELDS
  uint16_t value;
  uint8_t token;
  uint8_t nrgo;
  uint16_t go[];
} __attribute__((aligned(4))) vm_tfunction_t;

typedef struct vm_tvar_t {
  VM_GENERIC_FIELDS
  uint16_t go;
  uint16_t value;
  uint8_t token[];
} __attribute__((aligned(4))) vm_tvar_t;

typedef struct vm_tevent_t {
  VM_GENERIC_FIELDS
  uint16_t go;
  uint8_t token[];
} __attribute__((aligned(4))) vm_tevent_t;

typedef struct vm_tcevent_t {
  VM_GENERIC_FIELDS
  uint8_t token[];
} __attribute__((aligned(4))) vm_tcevent_t;

typedef struct vm_toperator_t {
  VM_GENERIC_FIELDS
  uint8_t token;
  uint16_t left;
  uint16_t right;
  uint16_t value;
} __attribute__((aligned(4))) vm_toperator_t;

typedef struct vm_teof_t {
  uint8_t type;
} __attribute__((aligned(4))) vm_teof_t;

unsigned int alignedvarstack(int v);
int rule_initialize(struct pbuf *input, struct rules_t ***rules, uint8_t *nrrules, struct pbuf *mempool, void *userdata);
int rule_run(struct rules_t *obj, int validate);
void valprint(struct rules_t *obj, char *out, uint16_t size);

#endif
