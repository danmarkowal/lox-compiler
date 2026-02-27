#ifndef CLOX_TABLE_H
#define CLOX_TABLE_H

#include "common.h"
#include "value.h"

typedef struct {
  // storing ObjString pointer iss faster and smaller than wrapping in Value
  ObjString* key;
  Value value;
} Entry;

// storing collisions in a linked list - less values to linearly search, immune to clustering,
//                                       not cache-friendly, memory overhead
// linear probing                      - cache-friendly, weak to clustering
typedef struct {
  int count;
  int capacity;
  Entry* entries;
} Table;

void init_table(Table* table);
void free_table(Table* table);
bool table_get(Table* table, ObjString* key, Value* value);
bool table_set(Table* table, ObjString* key, Value value);
bool table_delete(Table* table, ObjString* key);
void table_add_all(Table* from, Table* to);
ObjString* table_find_string(Table* table, const char* chars, int length, uint32_t hash);
void table_remove_white(Table* table);
void mark_table(Table* table);

#endif

