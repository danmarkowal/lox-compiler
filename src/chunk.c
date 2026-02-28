#include <stdlib.h>
#include <stdio.h>

#include "chunk.h"
#include "memory.h"
#include "vm.h"

void init_chunk(Chunk* chunk) {
  chunk->count = 0;
  chunk->capacity = 0;
  chunk->code = NULL;

  chunk->line_count = 0;
  chunk->line_capacity = 0;
  chunk->lines = NULL;

  init_value_array(&chunk->constants);
}

void free_chunk(Chunk* chunk) {
  FREE_ARRAY(uint8_t, chunk->code, chunk->capacity);
  FREE_ARRAY(LineEncoding, chunk->lines, chunk->line_capacity);
  init_chunk(chunk);

  free_value_array(&chunk->constants);
}

void write_chunk(Chunk* chunk, uint8_t byte, int line) {
  if (chunk->capacity < chunk->count + 1) {
    int old_capacity = chunk->capacity;
    chunk->capacity = GROW_CAPACITY(old_capacity);
    chunk->code = GROW_ARRAY(uint8_t, chunk->code, old_capacity, chunk->capacity);
  }

  chunk->code[chunk->count++] = byte;

  // increment the line count at the end of the array if the line number matches
  if (chunk->line_count > 0 && chunk->lines[chunk->line_count - 1].line == line) {
    chunk->lines[chunk->line_count - 1].count++;
  }
  // otherwise, create a new line encoding at the end of the array
  else {
    // grow the line array
    if (chunk->line_capacity < chunk->line_count + 1) {
      int old_capacity = chunk->line_capacity;
      chunk->line_capacity = GROW_CAPACITY(old_capacity);
      chunk->lines = GROW_ARRAY(LineEncoding, chunk->lines, old_capacity, chunk->line_capacity);
    }

    // create a new line encoding at the end with a count of 1
    chunk->lines[chunk->line_count++] = (LineEncoding) { .line = line, .count = 1 };
  }
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

int get_line(Chunk* chunk, int offset) {
  if (chunk->line_count == 0) return -1;

  LineEncoding* current_line = chunk->lines;
  while ((current_line - chunk->lines) < chunk->line_count) {
    if (offset < current_line->count) {
      return current_line->line;
    }

    offset -= current_line->count;
    current_line++;
  }

  // something has gone wrong
  return chunk->lines[chunk->line_count - 1].line;
}

