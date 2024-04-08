# Rule library

ESP ready, high performant and low resources rules library written in C.

[![Coverage Status](https://coveralls.io/repos/github/CurlyMoo/rules/badge.svg?branch=main)](https://coveralls.io/github/CurlyMoo/rules?branch=main)
[![Build Status](https://github.com/curlymoo/rules/actions/workflows/esp8266.yml/badge.svg)]()
[![Build Status](https://github.com/curlymoo/rules/actions/workflows/esp8266-safe.yml/badge.svg)]()
[![Build Status](https://github.com/curlymoo/rules/actions/workflows/esp8266-fast.yml/badge.svg)]()
[![Build Status](https://github.com/curlymoo/rules/actions/workflows/coveralls.yml/badge.svg)]()
[![License: MPL 2.0](https://img.shields.io/badge/License-MPL%202.0-brightgreen.svg)](https://opensource.org/licenses/MPL-2.0) ![GitHub issues](https://img.shields.io/github/issues-raw/CurlyMoo/rules) [![Donate](https://www.paypalobjects.com/en_US/i/btn/btn_donate_SM.gif)](https://www.paypal.com/cgi-bin/webscr?cmd=_donations&business=info%40pilight%2eorg&lc=US&item_name=curlymoo&no_note=0&currency_code=USD&bn=PP%2dDonationsBF%3abtn_donate_SM%2egif%3aNonHostedGuest)

---
---

## Table of Contents

* [Background](#background)
* [Features](#features)
* [Currently supported platforms](#currently-supported-platforms)
* [Changelog](#changelog)
	 * [Release v1.0](#release-v10)
	 * [Todo](#todo)
* [Prerequisites](#prerequisites)
* [Installation](#installation)
	 * [Linux](#linux)
	 * [Arduino (for ESP)](#arduino-for-esp)
	 * [In your (Arduino) project](#in-your-arduino-project)
* [Syntax](#syntax)
	 * [If blocks](#if-blocks)
	 * [Nested if blocks](#nested-if-blocks)
	 * [Conditions and math](#conditions-and-math)
	 * [Event blocks](#event-blocks)
	 * [Functions](#functions)
	 * [Body](#body)
	 * [Variables](#variables)
	 * [Parenthesis](#parenthesis)
* [API](#api)
	 * [Providing rules](#providing-rules)
	 * [Events](#events)
	 * [Variables](#variables-1)
* [Technical reference](#technical-reference)
	 * [Preparing](#preparing)
	 * [Parsing](#parsing)
	 * [2nd heap](#2nd-heap)

---
---

## Background

A rule interpreter can be pretty easily built using a lexer and a parser with techniques like [Shunting Yard](https://en.wikipedia.org/wiki/Shunting-yard_algorithm), a [Recursive Descent Parser](https://en.wikipedia.org/wiki/Recursive_descent_parser), an [Abstract syntax tree](https://en.wikipedia.org/wiki/Abstract_syntax_tree), [Precedence climbing](https://en.wikipedia.org/wiki/Operator-precedence_parser#Precedence_climbing_method) etc.

The downside of all these algoritms are that they - in their common implementation - require techniques not (easily) available on a microcontroller such as the ESP8266 (e.g., recursion, memory alignment), they either require too much memory, too much stack, or are too slow to parse / execute on a microcontroller. This library solves these issues by mixing the core aspects of the theories named above in a custom implementation that does run quickly on microcontollers, but is also very fast on regular enviroments.

This library uses similar techniques as Lua and similar interfaces but it less feature rich. However, it matches Lua speeds and most importantly uses a lot less memory.

## Features

- Mempool usage; minimal memory footprint and minimal fragmentation
- Unlimited number of if / else nesting
- Functions
- Operators (with respect of precedence and associativity)
- Variables
- Unlimited nesting of variables, functions, operators and parenthesis
- Unlimited calls to other code blocks
- Modular functions
- Modular callbacks for e.g. implementing global variables
- Bytecode parsing
- ESP8266 and ESP32 ready
- ESP8266 runs in the 2nd heap (fast and safe mode)
- Allows running rules fully async
- Lua like API interface

When properly configured the 2nd heap can give you 16KB mempool which can be almost fully used as dedicated memory for the rule parser. The rules library does store string in regular memory. This leaves the normal memory for the core program. Because the 2nd heap is used as a mempool it also prevents memory fragmentation.

---

## Currently supported platforms

1. ESP8266
2. ESP32
3. i386
4. amd64

---
---

## Changelog

### Release v1.0

1. Initial release

### Todo


## Prerequisites

- Arduino IDE
- ESP8266 Core

---
---

## Installation

### Linux

After cloning this repository from the root folder
```
# mkdir build
# cd build
# cmake ..
# ./start
```

### Arduino (for ESP)

Clone this repository in a folder called `rules`. In this folder:
```
# arduino-cli compile --fqbn esp8266:esp8266:d1 rules.ino
```
Upload the `build\esp8266.esp8266.d1\rules.ino.bin` file to your ESP.

Or open the `rules.ino` in your Arduino GUI.

### In your (Arduino) project

1. Move the full `src` folder into your project root.
2. Include the `#include "src/rules/rules.h"` header.
3. Configure the rules API as described below.
4. Parse your rules as described below.

---
---

## Syntax

This library is looze typed.

### If blocks

If blocks start with a condition defining when either the `if` block should execute or the `else` block should execute. The `else` block is optional. An if / else block always ends with a `end` token.

The body's are not optional. So you can't define an empty `if` / `else` blocks.
```
if [condition] then
  [body]
[else]
  [body]
end
```

The syntax also supports `elseif` statements.
```
if [condition] then
  [body]
[elseif] [condition] [then]
  [body]
[else]
  [body]
end
```

### Nested if blocks

The `if` body can contain (multiple) if body's. You can nest an unlimited number of `if` blocks. Each if block should have a accompanied `end` token.

```
if [condition] then
  if [condition] then
     [body]
  [else]
     [body]
  end
[else]
  [body]
end
```

### Conditions and math

Conditions and math are written equally. A number or function are compared or mathematically processed with another number or function.

```
[number | [function] | $variable] [operator] [number | [function] | $variable] [operator] [number | [function] | $variable] ...
```

So
```ruby
1 + 1 > 5 || 1 + 2 < 6
```

Or
```ruby
max(1, 2, 3) <= 6 && round(1, 2) > 5
```

Or
```ruby
$a + 5 * $c + max(1, $c)
```

### Event blocks

On blocks are user defined functions or events. A `on` block should be callable from another `on` block or `if` block. The way an `on` block is labeled is customizable. This means the developer implementing this library should define themselves how this label is formatted. The most simple implementation is just using strings as labels. Other ideas are `timer=5` or `@GPIO1`.

`On` blocks cannot be nested inside other `on` blocks or `if` blocks.
```
on [customizable] then
   [body]
end
```

```
on [customizable]([arguments], [arguments], ...) then
   [body]
end
```

An event block can take arguments. Passing events to an event is the same as passing arguments to a function. In case certain arguments are not set, the value will be `NULL`, e.g.:

```
on foo($a, $b, $c) then
   [body]
end
```

```
on bar($a, $b, $c) then
   foo(1, 2, 3);
end
```

### Functions

A function is written by using the function name followed by at least one opening and closing parenthesis.

```
[string]([arguments], [arguments], ...)
```

Functions and events can have one or more arguments. Arguments are given inside the parenthesis and are delimited by a `comma`. E.g.
```ruby
max(1, 2, 3);
```
Functions can be nested unlimitedly. E.g.,
```ruby
max(round(0, 12), $hours);
```

### Body

An `if` and `on` body can contain `variables`, `event calls`, `if blocks`. Each statement should end with a semicolon. E.g.:
```ruby
if 1 == 1 then
  if 5 < 6 then
     $a = 1;
     foo();
  else
    $a = 2;
    $b = max(1, 2) * 3;
  end
  $c = 6;
  bar();
  $d = $c + 6 ^ 5;
end
```

### Variables

Variables definitions are customizable. That means just as with `on` labels that what is defined a variable should be implemented by the developer implementing this library. Usefull ideas are `$foo`, `%hour%`, or `@global`.

Variables can be part of a condition or math or used to store a certain value. When storing a value the variable should be followed by an equal sign. After the equal sign the condition or math is placed.

```ruby
$a = $a + max(1, $c);
```

### Parenthesis

Variables can be used in math to prioritize condition above their regular precedence. Variables can allows unlimited nesting.

```ruby
(1 * (1 + 1) / 2) ^ 3
```

## API

### Providing rules

```c
int8_t rule_initialize(struct pbuf *input, struct rules_t ***rules, uint8_t *nrrules, struct pbuf *mempool, void *userdata);
```

The first step is to offer rulesets to the `rule_initialize` function. The `input.payload` value should link to an individual rule block. The `input.len` will be updated to point to the beginning of the next rule block. You can use the `input.len` value to point to the new rule block which again should be assigned to the `input.payload` value. This should be repeated while the `rule_initialize` returns `0`.

The mempool parameter should link to a dedicated block of memory assigned to a `pbuf` struct. On the ESP8266, the mempool can reside in either the 1st or 2nd heap. Because (almost) everything takes place inside the mempool, hardly no additional memory needs to be allocated preventing memory fragmentation.

You can use the userdata parameter to implement local and or global variables. In case of implementing a global rulestack, it is adviced to use the `struct rule_stack_t` just like the library uses itself. This allows you to use the helper functions provides with this library.

The `typedef struct rules_t` array should be declared outside the rule library and it's where all processed rules are stored. The same counts for the `nrrules` integer. That number reflects the number rules already processed.

**Example**

Let's say the full rule set is saved in the `text` variable and a 2nd heap is used. An example on how this could be implemented. Check the examples and unittest on how to implement variables and events.

```c
void main() {
  /*
   * All individually parsed rules reside in
   * the rules_t struct array. This struct
   * fully lives inside the mempool.
   */
  struct rules_t **rules = NULL;
  /*
   * The number of rules parsed.
   */
  int nrrules = 0;

  /*
   * The pbuf struct that contains all info
   * for the rules library to interface with
   * the mempool and rulesets.
   */
  struct pbuf mem;
  struct pbuf input;

  /*
   * MMU_SEC_HEAP_SIZE are MMU_SEC_HEAP are
   * special defines for the ESP8266 when
   * using the 2nd heap. In this example the
   * full 2nd heap is used.
   */
  mem.payload = (unsigned char *)MMU_SEC_HEAP;
  mem.len = 0;
  mem.tot_len = MMU_SEC_HEAP_SIZE;

  /*
   * The text variable is just a string
   * containing the ruleset.
   */
  input.payload = &text[0];
  input.len = 0;
  input.tot_len = strlen(text);

  int ret = 0;
  while((ret = rule_initialize(&input, &rules, &nrrules, &mem, NULL)) == 0) {
    input.payload = &text[input.len];
  }
}
```

A tip is to place the raw ruleset string at the end of the mempool. As soon as a rule block has been parsed from within the ruleset it's no longer needed so it can be overwritten by the rule parser. Hardly no overhead is needed for this kind of parsing.

**Example**

```c
void main() {
  struct rules_t **rules = NULL;
  int nrrules = 0;

  const char *rule = "on foo then if 1 == 1 then $a = 1; $b = 1.25; $c = 10; $d = 100; else $a = 1; end end on bar then $e = NULL; $f = max(1, 2); $g = 1 + 1.25; foo(); end";

  int len = strlen(rule);
  struct pbuf input;
  memset(&input, 0, sizeof(struct pbuf));

  struct pbuf mem;
  memset(&mem, 0, sizeof(struct pbuf));
  mem.payload = (unsigned char *)malloc(1024);
  mem.len = 0;
  mem.tot_len = 1024;
  memset(mem.payload, 0, 1024);

  uint16_t txtoffset = mem->tot_len-len;
  for(y=0;y<len;y++) {
    ((unsigned char *)mem->payload)[txtoffset+y] = (uint8_t)rule[y];
  }

  input.payload = &((unsigned char *)mem->payload)[txtoffset];
  input.len = txtoffset;
  input.tot_len = len;

  while((ret = rule_initialize(&input, &rules, &nrrules, mem, NULL)) == 0) {
    input.payload = &((unsigned char *)mem->payload)[input.len];
  }
```

Although one single mempool is provided to the `rule_initialize` function, the rules library can handle multiple mempools; both in the 1st and/or 2nd heap. `pbuf` structs can be linked together as a linked list. This comes in handy when for example the 2nd heap cannot provide enough storage for your need.

**Example**

```c
void main() {
  /*
   * Rule read from the filesystem
   */
  const char *rule = ...;

  int len = strlen(rule);
  struct pbuf input;
  memset(&input, 0, sizeof(struct pbuf));

  struct pbuf mem[2];
  memset(&mem[0], 0, sizeof(struct pbuf));
  memset(&mem[1], 0, sizeof(struct pbuf));

  mem[0].payload = (unsigned char *)malloc(1024);
  mem[0].len = 0;
  mem[0].tot_len = 1024;
  memset(mem[0].payload, 0, 1024);

  mem[1].payload = (unsigned char *)MMU_SEC_HEAP;
  mem[1].len = 0;
  mem[1].tot_len = MMU_SEC_HEAP_SIZE;
  memset(mem[1].payload, 0, 1024);

  mem[0]->next = mem[1];

  uint16_t txtoffset = mem[0]->tot_len-len;
  for(y=0;y<len;y++) {
    ((unsigned char *)mem[0]->payload)[txtoffset+y] = (uint8_t)rule[y];
  }

  input.payload = &((unsigned char *)mem[0]->payload)[txtoffset];
  input.len = txtoffset;
  input.tot_len = len;

  while((ret = rule_initialize(&input, &rules, &nrrules, mem[0], NULL)) == 0) {
    input.payload = &((unsigned char *)mem[0]->payload)[input.len];
  }
```

### Modular functions

As can be read in the syntax description, to fully use this library, a developers should implement their own logic for variables and events. Without this logic, variables and events are not supported.

This also allows the library to interact with the outside world.

*Check the unittests in main.cpp for implementation examples or ask for help in the issues*.

1. Storing the parsed rules is done outside the library. So to call one rule block from another rule block (`events`), the called event needs to looked for in that external rule array.
2. The library only supports variables stored into the local scope for the lifetime of the function call. Developers can define their own variables like globals, system variables, hardware parameters.

```c
typedef struct rule_options_t {
  /*
   * Identifying callbacks
   */
  int8_t (*is_variable_cb)(char *text, uint16_t size);
  int8_t (*is_event_cb)(char *text, uint16_t size);

  int8_t (*vm_value_set)(struct rules_t *obj);
  int8_t (*vm_value_get)(struct rules_t *obj);
  /*
   * Events
   */
  int8_t (*event_cb)(struct rules_t *obj, char *name);
} rule_options_t;
```

The first two functions are identification functions that help the rule parser to identify the different tokens inside the syntax.
- `is_variable_cb` should identify a token as a variable
- `is_event_cb` should identify a token as an event

The `is_variable_cb` function is called with two parameters:
1. `char *text` contains the name of the token encountered, possibly a variable name.
2. `uint16_t size` the size (number of characters) of the token encountered.

The `is_event_cb` function is called with two parameters:
1. `char *text` contains the name of the token encountered, possibly a event name.
3. `uint16_t size` the size (number of characters) of the token encountered.

Both function should return a `-1` when the token isn't a variable neither an event. The `is_variable_cb` function should return the length of the token found. The `is_event_cb` should return `0` when a token was indeed an event.

### Events

The rules library allows the user to define their own functions, greatly reducing redundant code.
```ruby
on foo then
 $a = 1;
 $b = 2;
end

on bar then
  foo();
end
```

The library implements these calls using tail-recursion as follows.

A function call like `foo()` and an `on` block is tokenized as `TEVENT`. So you have event callee and event caller functions. Since this ruleset only contains two rule blocks the `on foo then` block is the first rule block in the `rules_t` array and `on bar then` the second.

```
static int8_t event_cb(struct rules_t *obj, char *name);
```

The `event_cb` function is used to find the rule being called by another rule. A special helper function `rule_by_name` can be used to locate the rule to be called. The rule library will do the rest. The `go` field of the `ctx` struct is a pointer to the to be called rule. The `ret` field is a pointer to the rule where the called rule was called from.

```c
static int8_t event_cb(struct rules_t *obj, char *name) {
  int8_t nr = rule_by_name(rules, nrrules, name);
  if(nr == -1) {
    return -1;
  }

  obj->ctx.go = rules[nr];
  rules[nr]->ctx.ret = obj;

  return 1;
}
```

Return 1 if you want to continue executing the rule block, return 0 when it should end here and return -1 when an error occured.

### Variables

Interaction with variables is done similarly as in Lua, making it relatively easy to interface with this library.

*Setting*

This function is used to store a variable. The variable name is placed on the stack at position `-2` and the variable value at position `-1`. The rest of the stack should be remain untouched.

So to retrieve the variable name:
```c
  const char *key = rules_tostring([rule], -2);
```

To detect the variable type of the variable value you can use the `rules_type` function, e.g.:
```c
  switch(rules_type([rule], -1)) {
    case VCHAR:
    case VINTEGER:
    case VFLOAT:
    case VNULL: {
    } break;
    default: {
      return -1;
    } break;
  }
```

Then you can retrieve the variable value, e.g.:
```c
int i = rules_tointeger([rule], -1);
float f = rules_tofloat([rule], -1);
```

The stack is automatically cleared when the library is done interacting with the outside world.

*Getting*

This function is used to retrieve a variable. The variable to be retrieved is placed on top of the stack at position `-1`, e.g.:
```c
const char *key = rules_tostring([rule], -1);
```

The returned value should be placed on top of the stack. This is done with the different `rules_push*` functions. E.g.,
```c
rules_pushinteger([rule], 1);
rules_pushfloat([rule], 5.5);
rules_pushstring([rule], "foo");
rules_pushnil([rule]);
```

Again, the stack is automatically cleared when the library is done interacting with the outside world.

*Additionally*

The `rules_gettop([rule])` function can be used to number of element on the stack.

The `rules_ref([string])` and `rules_unref([string])` functions are used to increase the reference for this string. As long as the reference for a given string is above zero, the garbage collector will ignore it. Without properly using the referencing of strings, the system memory will eventually will be exhausted. The library will automatically ignore referencing for constants.

### Functions

Functions are modular. Both are programmed in C just as the libary itself.

When creating a new function it should be added to the `rule_functions` arrays. The structure of the list items are self-explanatory. Each function or operator reside in their own seperate source files.

A function is formatted with just a single parameters:

```c
int8_t (*callback)(struct rules_t *rule);
```

An function module should return `0` if it ran correctly, if it failed, return `-1`. A failing function will trigger an exception on the ESP so should be used carefully. It should be used only in case of fatal programmatic errors.

The heap will only contain values to be used by the function. Make sure to parse all variables or to return an error. You can remove variables from the heap when you're done parsing them by using the `rules_remove(rule, -1)` helper function. The second argument is the relative position on the heap.

## Technical reference

### Preparing

The first simple step to reduce the processing speed of a rule is to bring it back to its core tokens. When processing syntax, we are constantly processing if a part is a function, a variable, an operator, a number etc. That identification process is relatively slow so it's better to do it only once.

```ruby
if 1 == 1 then $a = max(1, 2); end
```

This rule contains some static tokens like `if`, `then`, `end`, `(`, `)`, `;`, `=`, `,` some factors like `1` and `2`, and some modular or dynamic tokens like `==`, `$a`, `max`. Factors are values we literally need to know about, just as the variable names. The `==` operator and `max` functions can be indexed by their operators and functions list position, the rest can simply be numbered.

That leaves the variable tokens. These need to be stored exactly as they are defined.

First we number the static tokens
1.  TOPERATOR
2.  TFUNCTION
3.  TSTRING
4.  TNUMBER
5.  TNUMBER1
6.  TNUMBER2
7.  TNUMBER3
8.  TEOF
9.  LPAREN
10. RPAREN
11. TCOMMA
12. TIF
13. TELSE
14. TELSEIF
15. TTHEN
16. TEVENT
17. TEND
18. TVAR
19. TASSIGN
20. TSEMICOLON
21. TTRUE
22. TFALSE
23. TSTART
24. TVALUE
25. VCHAR
26. VINTEGER
27. VFLOAT
28. VNULL

Let's say the `==` operator is the first operator (counted from zero) in the operator list and the `max` function is the second function in the functions list. Then this rule can be rewritten like this:

```
12 5 1 1 0 5 1 15 18 $ a 19 2 1 9 5 1 11 5 2 10 20 17
```

The prepared rule overwrites the original rule so no new memory needs to be allocated. There are some tricks involved to enable this.

The most easy way to tokenize a syntax is like this, e.g. a number:
```
4 1 5 \0
```

So a token identifier (TNUMBER), the actual number, and a null terminator. The downside to this is that the original number `15` increases from 2 bytes to 4 bytes. Depending on the rule structure this increase of bytes makes it too big to fit in the original rule. The first step is to drop the null terminator. Which saves one byte. But dropping the null terminator removes the cue how many ASCII bytes to read. Therefor three token types are introduced: `TNUMBER1`, `TNUMBER2`, `TNUMBER3`. A `TNUMBER1` token is followed by a number stored in one ASCII byte. A `TNUMBER2` by two ASCII bytes, a `TNUMBER3` by three ASCII bytes. There isn't a `TNUMBER4` because an integer or float is already stored in four bytes.

A similar logic is applied to the variables. No null terminator is used. However, we only use 28 tokens. The allowed variables characters reside in the ASCII 33 to 126 range. This means that any ASCII character before 33 must be a token. Therefor, the token identifier is used as a null termator.

The last issue occurs is certain rule syntaxes. E.g.:
```ruby
(1 + 1); end
```
When replacing the syntax with tokens the `1);` sequence is problematic. Sometimes the last number `1` is being overwritten starting from the same byte. This would overwrite the closing parenthesis, since the smallest number replacement is 2 bytes. But, the space between between the semicolon and the `end` rescues us. We can move the closing parenthesis and semicolon one place fixing this issue. Both tokens are replaced taking just one byte, just as the `end` token.

Parsing the rule into an `abstract syntax tree` (AST) replaces each token with a tree node. These tree nodes are just like the lexer tokens of a certain size that can be calculated while reading the syntax. So, when we finish the preperation step, the necessary memory size for the AST is known.

### Parsing

The next step is parsing the rule in a programmatic friendly bytecode representation. This logic is lend from Lua, but adapted to work better with a simple implementation. This library uses a registry based approach to store values and to be more efficient with memory usage.

```ruby
if (1 == 2 || 3 >= 4) then $a = 5; else $b = 6; end
```

```cmd
Bytecode
 0      OP_EQ           -7      -1      -2
 1      OP_GE           -8      -3      -4
 2      OP_OR           -7      -7      -8
 3      OP_JMP          6
 4      OP_SETVAL       0       -5
 5      OP_JMP          7
 6      OP_SETVAL       1       -6
 7      OP_RET

Heap
 1      VINTEGER        1
 2      VINTEGER        2
 3      VINTEGER        3
 4      VINTEGER        4
 5      VINTEGER        5
 6      VINTEGER        6
 7      VNULL
 8      VNULL

Varstack
 0      $a
 1      $b
```

#### Bytecode structure

*Opcodes*

The bytecode consists of opcodes. Each opcode is stored in 4 bytes:
1. Opcode type
2. Value A
3. Value B
4. Value C

There are 22 different opcodes. The main group of opcodes consist of operators: `OP_EQ`, `OP_NE`, `OP_LT`, `OP_LE`, `OP_GT`, `OP_GE`, `OP_AND`, `OP_OR`, `OP_SUB`, `OP_ADD`, `OP_DIV`, `OP_MUL`, `OP_POW`, `OP_MOD`. For now summarized as: `OP_OP`. If the operators are used as input for specific `if`, `else`, `ifelse` flow blocks, the last opcode is always followed by an `OP_JMP`. In case the opcode is `false` if will hit the `OP_JMP`. If the outcome of the operator is `true` it will skip the `OP_JMP`, just as in Lua.

The `OP_JMP` is the only opcode that allows us to jump to other (forward) locations in the bytecode. Otherwise, each opcode is just executed one by one.

- `OP_OP`: Executes an operator
  - 1 Heap location to store the result / 2 Left value location on the heap / 3 Right value location on the heap.
- `OP_TEST`: Perform a boolean test in these cases: `if 1 then`, `if max(0) then` or `if $a then`.
- `OP_JMP`: Jumps to a specific forward location in the bytecode
  - 1 Location to jump to.
- `OP_SETVAL`: Requests the storage of a heap value to a variable. After executing this opcode, the stack is cleared.
  - 1 Location on the stack if (value < 0) | Location on the varstack (value > 0)[^1] / 2 Location on the heap
- `OP_GETVAL`: Requests the retrieval of an variable value to a heap
  - 1 Location on the heap / 2 Location on the stack
- `OP_PUSH`: Pushes heap values on the stack to be used as function parameters
  - 1 Location on the heap which is pushed on top of the stack (value < 0) | Location on the varstack which is pushed on top of the stack (value > 0)[^1]
- `OP_CALL`: Calls an internal function or external event
  - 1 Heap location to store the result / 2 Function index / 3 A 0 if this is an internal function, 1 if this is an external event
- `OP_CLEAR`: Fully clears the stack. When a function or event call doesn't result in setting a variable, this additional opcode is called to still clear the stack.
- `OP_RET`: Defines the end of the bytecode

[^1]: The actual location on the varstack is: `(value - 1) * sizeof(struct vm_vchar_t)`

If we look at a nested function and operators example.

```ruby
if 1 == 1 then $a = max(1 * 2, (min(5, 6) + 1) * 6); end
```

This translates to the following bytecode:

```cmd
Bytecode
 0      OP_EQ           -5      -1      -1
 1      OP_JMP          12
 2      OP_PUSH         -2
 3      OP_PUSH         -3
 4      OP_CALL         -6      1       0
 5      OP_MUL          -7      -1      -4
 6      OP_PUSH         -7
 7      OP_ADD          -6      -6      -1
 8      OP_MUL          -6      -6      -3
 9      OP_PUSH         -6
10      OP_CALL         -6      0       0
11      OP_SETVAL       0       -6
12      OP_RET

Heap
 1      VINTEGER        1
 2      VINTEGER        5
 3      VINTEGER        6
 4      VINTEGER        2
 5      VNULL
 6      VNULL
 7      VNULL

Varstack
 0      $a
```

#### Values and variables

Until this point everything is static, while we also have dynamic values like variables and the outcome of operators and functions.

```ruby
1 == 0 || 5 >= 4
```

To be able to parse the `||` operator it needs to know what the outcome is of the `1 == 0` and `5 >= 4` evaluations. The interpreter stores the intermediate values on heap. When preparing the rulesets the exact amount of memory needed is precalculated. This amount of memory is therefor reserved for this ruleset on the mempool.

It is important to realize integers are stored in 24 bits and floats in 27 bits. This still allows for big integers and high float precision, but less than in a regular 32bit enviroment. The reason for this is to allow for storing integers and floats in a 4 byte struct, where the first 5 bits of the first byte holds the variable type. The last three bytes store the actual number. For floats, the last 3 bits of the first byte are used to also store float to allow for higher precision.

```c
typedef struct vm_vinteger_t {
  uint8_t type;
  uint8_t value[3];
} __attribute__((aligned(4))) vm_vinteger_t;

typedef struct vm_vfloat_t {
  uint8_t type;
  uint8_t value[3];
} __attribute__((aligned(4))) vm_vfloat_t;
```

See the `rules_pushfloat`, the `rules_tofloat`, the `rules_pushinteger`, and `rules_tointeger` on how the conversion is done internally.

Variables are not stored on the heap and also not on the stack. Instead a special `VPTR` struct is used also stored in a 4 byte struct:

```c
typedef struct vm_vptr_t {
  uint8_t type;
  uint16_t value;
} __attribute__((aligned(4))) vm_vptr_t;
```

The second byte is now used to location a variable on a special stack. This stack is an internal but global stack stored on in regular memory. Variables are only stored once across all rules to save memory. So the `VPTR` is placed on the rule specific stack which points to a specific place on the global special variable stack. Hence, the name `VPTR` which stand for `value pointer`.

Because of this special global stack a `rules_gc` function needs to be called in case of a restart of the library, or else the allocated memory for this stack will remain in use.

### 2nd heap

The rule library will automatically detect if the memory is located in the 1st or 2nd heap and if the library runs in 2nd heap fast mode or safe mode.

### Efficiency

#### Rules, stacks and heaps

The rules, heap, and stack are all placed on the mempool. The heaps are rule specific whereas the regular stack is shared across all rules. The rule struct and the special global variable stack is placed in regular memory. Just as the timestamp used for benchmarking the rule parsing and execution.

The slots in global variable stack on it's own point to regularly allocated string. So, if a string changes in size, the stack doesn't need reallocation, but only memory wherein the string resides. This is done to mimimize the memory allocations and therefor fragmentation.

### Free registry slots

Another way to minimize memory usage is to try to minimize the amount of free registry slots need to store temporary values on the heap. E.g. `if 1 / 2 + 3 * 4 == 5 then $a = 6; end`:

```cmd
Bytecode
 0      OP_DIV          -7      -1      -2
 1      OP_MUL          -8      -3      -4
 2      OP_ADD          -7      -7      -8
 3      OP_EQ           -7      -7      -5
 4      OP_JMP          6       0       0
 5      OP_SETVAL       0       -6      0
 6      OP_RET          0       0       0

Heap
 1      VINTEGER        1
 2      VINTEGER        2
 3      VINTEGER        3
 4      VINTEGER        4
 5      VINTEGER        5
 6      VINTEGER        6
 7      VNULL
 8      VNULL
```

First we need to store the result of the `OP_DIV` somewhere. In this case in slot 7. Then we need to store the result of `OP_MUL`. In this case in slot 8. Both the outcome of slot 7 and 8 are used in the `OP_ADD`. After that, slot 7 and 8 are available again. The outcome of `OP_ADD` can therefor be placed in slot 7. Also, `OP_EQ` used the outcome of slot 7 and the constant number 5. Therefor, slot 7 can be used to store the outcome op `OP_EQ`.

This can also be seen in the debug output of the rule execution:
```cmd
rule #1, pos: 0, op_id: 11, op: OP_DIV
        1 / 2 = 0.5 -> -7
rule #1, pos: 1, op_id: 12, op: OP_MUL
        3 * 4 = 12 -> -8
rule #1, pos: 2, op_id: 10, op: OP_ADD
        0.5 + 12 = 12.5 -> -7
rule #1, pos: 3, op_id: 1, op: OP_EQ
        12.5 == 5 = 0 -> -7
rule #1, pos: 4, op_id: 16, op: OP_JMP
rule #1, pos: 6, op_id: 22, op: OP_RET
```

To conclude. The minimal amount of temporary slots needed to store temporary values is 2. The rules library tries to calculate this minimal amount or at least the amount as close as it can get to minimize the heap size.