# Rule library

ESP ready, high performant and low resources rules library written in C.

[![Coverage Status](https://coveralls.io/repos/github/CurlyMoo/rules/badge.svg?branch=main)](https://coveralls.io/github/CurlyMoo/rules?branch=main) [![Build Status](https://travis-ci.com/CurlyMoo/rules.svg?branch=main)](https://travis-ci.com/CurlyMoo/rules) [![Donate](https://www.paypalobjects.com/en_US/i/btn/btn_donate_SM.gif)](https://www.paypal.com/cgi-bin/webscr?cmd=_donations&business=info%40pilight%2eorg&lc=US&item_name=curlymoo&no_note=0&currency_code=USD&bn=PP%2dDonationsBF%3abtn_donate_SM%2egif%3aNonHostedGuest)

# Features

- No classic and no tail recursion
- Minimal memory footprint
- Unlimited number of if / else nesting
- Functions
- Operators (with respect of precedence and associativity)
- Variables
- Unlimited nesting of variables, functions, operators and parenthesis
- Modular functions and operators
- Bytecode parsing
- ESP8266 and ESP32 ready

And example rule:

```
if (1 == 1 && 1 == 0) || 5 >= 4 then
  $a = 1;
  if 6 == 5 then
    $a = 2;
  end
  $a = $a + 3;
  $b = (3 + max($a * 5, 15) + 3 * 1) * 2;
  @c = 5;
else
  if 2 == 2 then
    $a = 6;
  else
    $a = 7;
  end
end
```

# Todo

- String handling in the parser and all operators and functions
- Float handling in some operators and functions
- Storing values platform independent
- Always more optimalizations

# Compiling

## Linux

After cloning this repository from the root folder
```
# mkdir build
# cd build
# cmake ..
# ./start
```

## Arduino (for ESP)

Clone this repository in a folder called `rules`. In this folder:
```
# arduino-cli compile --fqbn esp8266:esp8266:d1 rules.ino
```
Upload the `build\esp8266.esp8266.d1\rules.ino.bin` file to your ESP.

# Background

A rule interpreter can be pretty easily built using a lexer and a parser with techniques like [Shunting Yard](https://en.wikipedia.org/wiki/Shunting-yard_algorithm), a [Recursive Descent Parser](https://en.wikipedia.org/wiki/Recursive_descent_parser), an [Abstract syntax tree](https://en.wikipedia.org/wiki/Abstract_syntax_tree), [Precedence climbing](https://en.wikipedia.org/wiki/Operator-precedence_parser#Precedence_climbing_method) etc.

The downside of all these algoritms are that they - in their common implementation - require techniques not (easily) available on a microcontroller such as the ESP8266 (e.g., recursion, memory alignment), they either require too much memory, too much stack, or are too slow to parse / execute on a microcontroller. This library solves these issues by mixing the core aspects of the algoritmes named above in a custom implementation that does run quickly on microcontollers, but is also very fast on regular enviroments

## Preparing

The first simple step to reduce the memory footprint of a rule is to its core tokens.
```
if 1 == 1 then $a = max(1, 2); end
```
This rule contains some static tokens like `if`, `then`, `end`, `(`, `)`, `;`, `=`, `,` some factors like `1` and `2`, and some modular or dynamic tokens like `==`, `$a`, `max`. Factors are values we literally need to know about, just as the variable names. The `==` operator and `max` functions can be indexed by their operators and functions list position, the rest can simply be numbered.

First we number the static tokens (random numbering)
1. if
2. then
3. =
4. (
5. )
6. ;
7. end
8. comma
9. operator
10. function
11. variable
12. number
13. character

Let's say the `==` operator is the first operator in the operator list and the `max` function is the second function in the functions list.

That leaves the variable tokens. These need to be stored as literally as they are defined.

So, this rule can be rewritten like this:

```
1 12 1 \0 9 0 12 1 \0 2 11 $ a \0 3 10 1 4 12 1 \0 8 12 2 \0 5 6 7 \0 \0
```

The human readable rule took `34 bytes`, the bytecode version is `27 bytes`, which saves `7 bytes`. In the actual bytecode representation the spaces are left out. A token like `if` is reduced from `2 bytes` to `1 bytes`. The `then` token is reduced from `4 bytes` to `1 byte`. A single number increases from `1 byte` to `3 bytes`, but we win processing speed by the way the number is stored in bytecode. The same counts for the variable name which increases from `2 bytes` to `4 bytes` but again gains in processing speed.

A possible improvement is to directly store numbers in a `2 byte` short, a `4 byte` integer, or a `4 byte` float. For now, numbers are stored in characters. Storing these values with a suffix null terminator allows us to directly target them by their absolute position in the bytecode. E.g.:

```c
printf("%s", &rule->bytecode[2]);
```
*The subsequent byte after type 12 is the beginning of the number.*


The lexer parses the bytecode rule with the knowledge of the different token representation:
- `1` means `if`
- `12` means the bytes until the terminating `\0` represents the number `1`
- `9` means an operator with the next byte indicating the position `0` in the operator list
- `12` means the bytes until the terminating `\0` represents the number `1`
- `2` means `then`
- `11` means a variable with the bytes until the terminating `\0` representing the name `$a`
- `3` means `=`
- `10` means a function with the next byte indicating the position `1` in the functions list
- `4` means `(`
- `12` means the bytes until the terminating `\0` represents the number `1`
- `8` means `,`
- `12` means the bytes until the terminating `\0` represents the number `2`
- `5` means `)`
- `6` means `;`
- `7` means `end`

The two null terminators on the end indicatie the end of the prepared rule.

## Parsing

The next step is parsing the rule in a programmatic friendly `abstract syntax tree` (AST).

```
if (1 == 2 || 3 >= 4) then $a = 5; else $b = 6; end
```

![](ast1.png)

So we enter the `if` node. The first branch directs us to the condition. The second branch represents the true body and the last branch represents the false body. To parse the condition we first need to travel downwards to the last nodes and back up again. So we first need to parse `1 == 2`, then `3 >= 4` to be able to parse the `or` condition based on the outcome of the two bottom nodes.

The most easy way to built and execute an AST is by using recursion. The nice thing about an AST built by using recursion is that it allows you to easily process all nested blocks such as nested `if / else` blocks as shown below, nested functions (e.g., `max(random(1, 10), 4)`), parenthesis (e.g., `(1 + 2) * ((4 - 5) ^ (2 * (3 / 4)))`) or a combination of them.

```
if 1 == 2 then
  if 3 >= 4 then
    $a = 5;
  else
    $b = 6;
  end
  $c = 7;
end
```

![](ast2.png)


Building recursion on a non-recursive way requires one of more stacks. Common implementation of recursion using stacks are pretty memory intensive. Using [Tail recursion](https://en.wikipedia.org/wiki/Tail_call) was an option together with [Continuation passing style](https://en.wikipedia.org/wiki/Continuation-passing_style), but you easily wind up lost in the downward and upward traversel of the tree.

### Nesting

So we want to drop recursion and we want to avoid having to rely on stack calls too much to minic recursion, but somehow we need to deal with these nested calls, which recursion is best fit for.

We have determined that there are three types of blocks that can be nested. Parenthesis, If and Function blocks. To deal with these nested blocks, the parser first parses the rule from to end to the beginning.
```
if ((3 * 2) == 2) || 6 > = 5 then
  if 3 >= 4 then
    $a = 5;
  else
    $b = 6;
  end
  $c = 7;
end
```

In case of this example, the inner `if` block is parsed first and stored in the beginning of our parsed bytecode. Secondly the inner `parenthesis` is parsed, then the outer `parenthesis` which links the already parsed inner `parenthesis` to it. And as last it will link root `if` block.

```
| inner if | inner parenthesis | outer parenthesis | root if |
```

The root `if` block will therefor always be parsed last. If the root `if` encounters the outer `parenthesis`, it links that `parenthesis` and continues where the outer `parenthesis` ended. When the outer `parenthesis` encountered the inner `parenthesis`, it links the inner `parenthesis` and continues where the inner `parenthesis` ended. If the root if encounters the inner `if`, it will link that inner `if` and continues where the inner `if` ended.

The same counts for nested function calls:

```
max(1 * 2, (min(5, 6) + 1) * 6)
```

The parsing order will be:

| min function | inner parenthesis | max function

Where we again start parsing from the most deepest nested token to the most outer nested token and finally from root to end, linking these already prepared inner blocks step by step.

To be able to properly parse these nested blocks, some information about them is registered:
- The type (function, parenthesis or if)
- The absolute start position in the prepared rule
- The absolute end position in the prepared rule
- The absolute bytecode position where the block starts

In the final AST, only the type and the absolute bytecode position of the block are irrelevant. So, we cache these blocks in an independent global cache. As soon as a nested block has been linked to another block, it will be removed from the cache. Leaving an empty cache as soon as all rules have been parsed.

### AST structure

An AST consists of various different type of nodes. Each node has its own number of elements. The `if` node consists of three elements: condition branch, true branch, false branch. The `true` and `false` node consists of one of more branches, namely the different expressions that needs te be evaluated inside an `then` or `else` block.

Within an AST one node is linked to another node based on the action associated with the specific type of node. An `if` node will jump to the `true` branch if the condition returns boolean true of the `false` branch if the condition returns boolean false.

To jump back and forth between nodes, they are linked by their absolute position in the bytecode.

If we look at the nested function example again, but now expanded with (random) absolute bytecode positions, and the operators.

```
max(1 * 2, (min(5, 6) + 1) * 6)
```

| 1 min function | 6 inner parenthesis | 9 operator | 11 max function | 15 operator | 18 operator |

1. We start at absolute position 11 where the max function resides. 
2. The first argument of this function is an operator so we jump to position 15.
3. Once the operator has been parsed we go back to position 11.
4. The max function sees we returned from position 11 so the first argument has been evaluated.
5. The next argument is a parenthesis block which resides at position 6.
6. The first token in the parenthesis is the function min that resides at position 1.
7. The min function parses both arguments and returns to position 6.
8. The parenthesis knows we returned from position 6 so we go to position 9 to parse the operator.
9. We return to position 6 which know knows all linked blockes have been evaluated so it can return to the max function as position 11.
10. The last token is an operator which multiplies the result of the parenthesis. So from position 11 we jump to position 18 to run the last operator.
11. Then we go back to positiion 11. The max function now sees all arguments are done evaluating so we can evaluate the max function call.

The downside to this nested parsing approach is that the first parsed node isn't necessarily the root `if` node. However, we do know that the root `if` node always needs to start at position 1 of the rule. The very first node inserted in the parsed bit of the bytecode is a `start` node. Because the `start` node is always the first node of the AST it's easily locatable. As soon as the root `if` node is parsed, it links itself to the `start` node, so the interpreter always knows where to start parsing. As soon as the full rules is parsed an `end` node is created. The `start` will return to the `end` node. The `end` node goes nowhere and returns to nowhere. The `end` node helps the interpreter determine what the last bytes are in the bytecode of the parsed rule.

### Node types in bytecode

The core of the bytecode AST are one or more jumps forwards and a jump backward.

So a generic node consists of a `type` byte. This allows us to define 32 different types of tokens and a `return`  of bytes. This allows us to jump to a token at the absolute maximum position of around byte 16554. If we need to be able to jump further, the `uint16_t` can be increases to `uint32_t`, but it'll increase the size of the bytecode as well.

```c
#define VM_GENERIC_FIELDS \
  uint8_t type; \
  uint16_t ret;
 ```
 
 The `if` node type will look like this:
 ```c
 typedef struct vm_tif_t {
  VM_GENERIC_FIELDS
  uint16_t go;
  uint16_t true_;
  uint16_t false_;
} vm_tif_t;
```

This shares the *generic* fields, but has three additional ones. A `go` of two bytes to jumps to the root operator, a `true` of two bytes to jump to the `true` node, and a `false` of two bytes to jump to the false node. In the actual code the `if` node is type 9 and takes 9 bytes.

In bytecode this simply looks like this:
```
| 1 | 2  3 | 4  5 | 6  7 | 8  9 |
| 9 | . 12 | . 17 | . 20 | . 26 |
```

To easily read this information we look at the first byte. This tells us the upcoming 8 bytes belong to an `if` node, so we can cast the bytecode to `vm_tif_t` and the appropriate fields will be set:

```c
struct vm_tif_t *node = (struct vm_tif_t *)&rule->bytecode[0];
```

To first store the bytes in the bytecode, we simply make space for the `vm_tif_t` node and assign the empty space to the struct:

```c
rule->bytecode = (unsigned char *)realloc(rule->bytecode, rule->nrbytes+sizeof(struct vm_tif_t));
struct vm_tif_t *node = (struct vm_tif_t *)&rule->bytecode[rule->nrbytes];
node->type = 9;
node->ret = 12;
node->go = 17;
node->true_ = 20;
node->false_ = 26;
rule->nrbytes += sizeof(struct vm_tif_t);
```
This simply adds an `if` node in our bytecode in a developer friendly way. Both are neat standard C functions to deal with bytecode.

Each node has these some generic bytes in common and some specific bytes that store information for the specific node type. However, they all interact similarly with the bytecode in the background. They claim bytecode storage to store their specific bytes, or specific bytes are cast to a specific node type struct to easily read the bytecode information.

Working with bytecode casted to specific struct allows us to change the bytecode structure by simply changing the elements within these structs. E.g. increasing the `uint16_t` to `uint32_t` to allow for further jumps.

### Reusing values

In case of specific values or variables the nodes don't duplicate the already known values in the prepared rule, but links to it. 

```c
typedef struct vm_toperator_t {
  VM_GENERIC_FIELDS
  uint16_t token;
  uint16_t left;
  uint16_t right;
  uint16_t value;
} vm_toperator_t;
```

When an `operator` node uses a static value on the left or right hand side, it will not jump to a node in the AST, but it jumps to an absolute position in the prepared rule.
```
1 12 1 \0 9 0 12 1 \0 2 11 $ a \0 3 10 1 4 12 1 \0 8 12 2 \0 5 6 7 \0 \0
```
In this example, the static value `1` was stored in the bytes after byte 2. So, in this case, the left hand or right hand side will link to byte 2 instead of another node in the AST. This make sure we don't have redundant values in our bytecode.

```c
typedef struct vm_tvar_t {
  VM_GENERIC_FIELDS
  uint16_t token;
  uint16_t go;
  uint16_t value;
} vm_tvar_t;
```

With this struct we can create a variable node. Again, this node doesn't store the `$a` value from this example rule again, but it links to byte 11 where the subsequent bytes store the variable name.

### Creating and linking nodes

The creation and linking of the nodes is where the parses comes in. That tells us if the preprared rule as stored at the beginning of the bytecode is actually valid.

The parser starts with parsing the nested blocks. When those are done, it will start parsing the root `if` blocks. As you can expect from a parser, it will just walk through the rule and check if the found token was also an expected token.

This library currently consists of a parser for the `if`, `operator`, `variable`, `parenthesis`, and `function` tokens. Whenever a parser of one type jumps to a parser of another type, the new type knows where it came from. So, when we jump from the `if` parser to the `operator` parser, the `operator` parser knows it was called from the `if` parser. And when the `operator` parser rewinds back to the `if` parser, the `if` parser knows it came from the `operator`. Together with this forward and backward jumps the last `step` is communicated. So when we go from `if` block #1 to an operator block #6, the operator can link node #1 to node #6 and can back again. So node #6 knows it's linked to node #1.

Where a recursive parser only walks forward, this non-recursive parser first parses the nestable blocks in the rule from back to front. Then it walks backwards and forward through the AST continuously linking nodes. The only temporary memory we need to allocate are those for the pointers to the nested blocks in the cache. All nodes and branches are just allocated once and directly fit. Which means they don't have to move around when they're parsed. This make the current parser very memory friendly.

## Interpreting

The interpreter is used to interpret the parsed AST. We now know each node tells us where we need to go next or where to return to. The interpreter start with the `start` node. The start node tells the interpreter where to find the root `if` node. When the `start` node was called from the root `if` node, it knows the interpreter was done parsing, so it can go to the `end` node.

### Jumping back and forth

The `if` node knows it was entered coming from the `start` node. It will go to the root operator first. The root operator is linked to the rest of the operators, factors, and/or parenthesis and/or functions making the full condition. As soon as the root operator is done evaluating, it returns to the calling `if` node. The `if` node now knows it was called from an operator. No need to go back to the operator again. Instead, based on the return value of the operator it calls the `true` node or the `false` node.

The `true` or `false` nodes start going to the first node of the linked expression list. These expressions parse and go back to the calling `true` or `false` node. The `true` and `false` nodes know from which expression the nodes where called. It loops through the expression list again to look for the expression next to the one it was called from.

The interpreter is nothing more than these jumps back and forth.

### Values and variables

Until this point everything is static, while we also have dynamic values like variables and the outcome of operators and functions.
```c
1 == 0 || 5 >= 4
```
To be able to parse the `||` operator it needs to know what the outcome is of the `1 == 0` and `5 >= 4` evaluations. The interpreter stores the intermediate values on a value stack. The value stack resides at the end of the bytecode after the parsed bytecode. These values are stored in the same way as the node types.
```c
typedef struct vm_vinteger_t {
  VM_GENERIC_FIELDS
  int value;
} vm_vinteger_t;
```

Each value uses the generic fields `type` and `ret`, with an additional specific `value` field of the specific type (in this case int). In this libary, `vinteger` is of type 20. So the first byte has the value 20.
```
|  1 |  2  3 |  4  5 |  6  7 |  8  9 | 10 11 |
| 20 |  .  4 |  .  . |  .  . |  .  . |  .  5 |
```
The next two bytes tells us to what token the value was linked. The next four bytes store the integer value. Each value (float, integer and char) are represented in structs which again are stored in the bytecode.

In the previous condition the `1 == 0` outcome is stored in an integer on the values stack. The integer value is associated to the `==` operator and the operator to the integer value. The same counts for the `5 >= 4` operator. When the `||` is called, it pops the outcome values from the left and right operator from the value stack, and places the outcome back on the stack. When the operator was called from an `if` node, the `if` node pops the value from the stack and uses it to determine if we need to continue to the `true` node or the `false` node.

```c
$a = 1;
$b = $a;
```
In this case, the integer value 1 is stored on the stack and associated to the `variable` token. When the value of `$a` is stored in `$b` the association of the value is just changed from `$a` to `$b` instead of popping the value from the stack, realigning all subsequent values on the stack, and inserting it again.