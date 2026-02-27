#ifndef CLOX_CHUNK_H
#define CLOX_CHUNK_H

#include "common.h"
#include "value.h"

// challenge: which instructions could you eliminate?
// - remove OP_SUBTRACT, replace with OP_NEGATE, OP_ADD
// - cannot remove OP_NEGATE with OP_CONSTANT <zero>, OP_ADD
//   this is because if 5 was on the stack, zero would be pushed on
//   then a = 5, b = 0, a + b = 5 != -5
//   or zero would have to be added before the 5
// challenge: what instructions could be defined to speed up execution?
// - OP_LEQ, OP_GEQ, OP_NEQ, (later down the line: OP_MOD, OP_POW)
// going to have to add OP_GET_CONSTANT_LONG, OP_SET_CONSTANT_LONG etc.
typedef enum {
  OP_CONSTANT,
  OP_NIL,
  OP_TRUE,
  OP_FALSE,
  OP_POP,
  OP_GET_LOCAL,
  OP_SET_LOCAL,
  OP_DEFINE_GLOBAL,
  OP_GET_GLOBAL,
  OP_SET_GLOBAL,
  OP_GET_UPVALUE,
  OP_SET_UPVALUE,
  OP_GET_PROPERTY,
  OP_SET_PROPERTY,
  OP_GET_SUPER,
  OP_EQUAL,
  OP_GREATER,
  OP_LESS,
  OP_CONSTANT_LONG,
  OP_ADD,
  OP_SUBTRACT,
  OP_MULTIPLY,
  OP_DIVIDE,
  OP_NOT,
  OP_NEGATE,
  OP_PRINT,
  OP_JUMP,
  OP_JUMP_IF_FALSE,
  OP_LOOP,
  OP_CALL,
  OP_INVOKE,
  OP_SUPER_INVOKE,
  OP_CLOSURE,
  OP_CLOSE_UPVALUE,
  OP_RETURN,
  OP_CLASS,
  OP_INHERIT,
  OP_METHOD
} OpCode;

typedef struct {
  int count;
  int capacity;
  // contains the bytecode instruction for the program
  uint8_t* code;
  // stores the line number of the previous instruction
  // int prev_line;
  // stores the number of instructions on the current line
  // int line_count;
  // contains the lines number corresponding to each instruction 
  int* lines;
  // contains immediate values referenced in the code
  ValueArray constants;
} Chunk;

void init_chunk(Chunk* chunk);
void free_chunk(Chunk* chunk);
void write_chunk(Chunk* chunk, uint8_t byte, int line);
// returns the index in the value array where the constant was defined
int add_constant(Chunk* chunk, Value value);
int write_constant(Chunk* chunk, Value value, int line);

#endif
