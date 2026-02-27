#include <stdlib.h>
#include <stdio.h>

#include "chunk.h"
#include "memory.h"
#include "vm.h"

void init_chunk(Chunk* chunk) {
  chunk->count = 0;
  chunk->capacity = 0;
  chunk->code = NULL;
  chunk->lines = NULL;

  init_value_array(&chunk->constants);
}

void free_chunk(Chunk* chunk) {
  FREE_ARRAY(uint8_t, chunk->code, chunk->capacity);
  FREE_ARRAY(int, chunk->lines, chunk->capacity);
  init_chunk(chunk);

  free_value_array(&chunk->constants);
}

void write_chunk(Chunk* chunk, uint8_t byte, int line) {
  if (chunk->capacity < chunk->count + 1) {
    int old_capacity = chunk->capacity;
    chunk->capacity = GROW_CAPACITY(old_capacity);
    chunk->lines  = GROW_ARRAY(int, chunk->lines, old_capacity, chunk->capacity);
    chunk->code = GROW_ARRAY(uint8_t, chunk->code, old_capacity, chunk->capacity);
  }

  chunk->code[chunk->count] = byte;
  chunk->lines[chunk->count] = line;
  chunk->count++;
}

int add_constant(Chunk* chunk, Value value) {
  push(value);
  write_value_array(&chunk->constants, value);
  pop();
  return chunk->constants.count - 1;
}

int write_constant(Chunk* chunk, Value value, int line) {
  int constant = add_constant(chunk, value);
  // check if the value of contant is greater than 255 (cannot fit in one byte)
  // if so, use OP_CONSTANT_LONG to define the constant
  if (constant > 255) {
    write_chunk(chunk, OP_CONSTANT_LONG, line);
    // store the value in little endian format
    // 1st byte
    write_chunk(chunk, (uint8_t) ((constant >> 0) & 0xFF), line);
    // 2nd byte
    write_chunk(chunk, (uint8_t) ((constant >> 8) & 0xFF), line);
    // 3rd byte
    write_chunk(chunk, (uint8_t) ((constant >> 16) & 0xFF), line);
  } else {
    write_chunk(chunk, OP_CONSTANT, line);
    write_chunk(chunk, constant, line);
  }

  return constant;
}

