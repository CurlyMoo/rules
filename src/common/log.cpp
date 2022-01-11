/*
  Copyright (C) CurlyMo

  This Source Code Form is subject to the terms of the Mozilla Public
  License, v. 2.0. If a copy of the MPL was not distributed with this
  file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#ifdef ESP8266
  #include <Arduino.h>
#else
  #define strcpy_P strcpy
  #define strlen_P strlen
  #define F
  #define PSTR
  #define PGM_P char *
  #define __FlashStringHelper char
#endif

#include "mem.h"

void _logprintln(const char *file, unsigned int line, char *msg) {
#ifdef ESP8266
  Serial.println(msg);
#else
  printf("%s\n", msg);
#endif
}

void _logprintf(const char *file, unsigned int line, char *fmt, ...) {
  char *str = NULL;

  va_list ap, apcpy;
  va_copy(apcpy, ap);
  va_start(apcpy, fmt);

  int bytes = vsnprintf(NULL, 0, fmt, apcpy);

  va_end(apcpy);
  if((str = (char *)MALLOC(bytes+1)) == NULL) {
    OUT_OF_MEMORY
  }
  va_start(ap, fmt);
  vsprintf(str, fmt, ap);
  va_end(ap);

  _logprintln(file, line, str);

  FREE(str);
}

void _logprintln_P(const char *file, unsigned int line, const __FlashStringHelper *msg) {
  PGM_P p = (PGM_P)msg;
  int len = strlen_P((const char *)p);
  char *str = (char *)MALLOC(len+1);
  if(str == NULL) {
    OUT_OF_MEMORY
  }
  strcpy_P(str, p);

  _logprintln(file, line, str);

  FREE(str);
}

void _logprintf_P(const char *file, unsigned int line, const __FlashStringHelper *fmt, ...) {
  PGM_P p = (PGM_P)fmt;
  int len = strlen_P((const char *)p);
  char *foo = (char *)MALLOC(len+1);
  if(foo == NULL) {
    OUT_OF_MEMORY
  }
  strcpy_P(foo, p);

  char *str = NULL;

  va_list ap, apcpy;
  va_copy(apcpy, ap);
  va_start(apcpy, fmt);

  int bytes = vsnprintf(NULL, 0, foo, apcpy);

  va_end(apcpy);
  if((str = (char *)MALLOC(bytes+1)) == NULL) {
    OUT_OF_MEMORY
  }
  va_start(ap, fmt);
  vsprintf(str, foo, ap);
  va_end(ap);

  _logprintln(file, line, str);

  FREE(foo);
  FREE(str);
}