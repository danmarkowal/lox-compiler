#include <stdlib.h>

#include "compiler.h"
#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include "vm.h"

#ifdef DEBUG_LOG_GC
#include <stdio.h>
#include "debug.h"
#endif

#define GC_HEAP_GROW_FACTOR 2

void *reallocate(void* ptr, size_t old_size, size_t new_size) {
  vm.bytes_allocated += new_size - old_size;
  // only reallocate when growing an allocation
  if (new_size > old_size) {
#ifdef DEBUG_STRESS_GC
    collect_garbage();
#endif
    
    if (vm.bytes_allocated > vm.next_gc) {
      collect_garbage();
    }
  }

  if (new_size == 0) {
    free(ptr);
    return NULL;
  }

  void* res = realloc(ptr, new_size);
  if (res == NULL) exit(1);
  return res;
}

void mark_object(Obj* object) {
  if (object == NULL) return;
  if (object->is_marked) return;

#ifdef DEBUG_LOG_GC
  printf("%p mark ", (void*)object);
  print_value(OBJ_VAL(object));
  printf("\n");
#endif

  object->is_marked = true;

  if (vm.gray_capacity < vm.gray_count + 1) {
    vm.gray_capacity = GROW_CAPACITY(vm.gray_capacity);
    vm.gray_stack = (Obj**)realloc(vm.gray_stack,
                                  sizeof(Obj*) * vm.gray_capacity);

    if (vm.gray_stack == NULL) exit(1);
  }

  vm.gray_stack[vm.gray_count++] = object;
}

void mark_value(Value value) {
  // numbers, bools, nil stored inline
  if (IS_OBJ(value)) mark_object(AS_OBJ(value));
}

static void mark_array(ValueArray* array) {
  for (int i = 0; i < array->count; i++) {
    mark_value(array->values[i]);
  }
}

static void blacken_object(Obj* object) {
#ifdef DEBUG_LOG_GC
  printf("%p blacken ", (void*)object);
  print_value(OBJ_VAL(object));
  printf("\n");
#endif

  switch (object->type) {
    case OBJ_BOUND_METHOD: {
      ObjBoundMethod* bound = (ObjBoundMethod*)object;
      mark_value(bound->receiver);
      mark_object((Obj*)bound->method);
      break;
    }
    case OBJ_CLASS: {
      ObjClass* klass = (ObjClass*)object;
      mark_object((Obj*)klass->name);
      mark_table(&klass->methods);
      break;
    }
    case OBJ_CLOSURE: {
      ObjClosure* closure = (ObjClosure*)object;
      mark_object((Obj*)closure->function);
      for (int i = 0; i < closure->upvalue_count; i++) {
        mark_object((Obj*)closure->upvalues[i]);
      }

      break;
    }
    case OBJ_FUNCTION: {
      ObjFunction* function = (ObjFunction*)object;
      mark_object((Obj*)function->name);
      mark_array(&function->chunk.constants);
      break;
    }
    case OBJ_INSTANCE: {
      ObjInstance* instance = (ObjInstance*)object;
      mark_object((Obj*)instance->klass);
      mark_table(&instance->fields);
      break;
    }
    case OBJ_UPVALUE:
      mark_value(((ObjUpvalue*)object)->closed);
      break;
    case OBJ_NATIVE:
    case OBJ_STRING:
      break;
  }
}

static void mark_roots() {
  // mark stack roots
  for (Value* slot = vm.stack; slot < vm.stack_top; slot++) {
    mark_value(*slot);
  }

  // mark call frames
  for (int i = 0; i < vm.frame_count; i++) {
    mark_object((Obj*)vm.frames[i].closure);
  }

  // mark open upvalues
  // closed upvalues are marked by the closure itself
  for (ObjUpvalue* upvalue = vm.open_upvalues;
       upvalue != NULL;
       upvalue = upvalue->next) {
    mark_object((Obj*)upvalue);
  }

  // mark globals
  mark_table(&vm.globals);
  mark_compiler_roots();
  mark_object((Obj*)vm.init_string);
} 

static void trace_references() {
  while (vm.gray_count > 0) {
    Obj* object = vm.gray_stack[--vm.gray_count];
    blacken_object(object);
  }
}

static void free_object(Obj* object) {
#ifdef DEBUG_LOG_GC
  printf("%p free type %d\n", (void*)object, object->type);
#endif

  switch (object->type) {
    case OBJ_BOUND_METHOD:
      FREE(ObjBoundMethod, object);
      break;
    case OBJ_CLASS: {
      ObjClass* klass = (ObjClass*)object;
      free_table(&klass->methods);
      FREE(ObjClass, object);
      break;
    } 
    case OBJ_CLOSURE: {
      ObjClosure* closure = (ObjClosure*)object;
      FREE_ARRAY(ObjUpvalue*, closure->upvalues,
                 closure->upvalue_count);
      FREE(ObjClosure, object);
      break;
    }
    case OBJ_FUNCTION: {
      ObjFunction* function = (ObjFunction*)object;
      free_chunk(&function->chunk);
      FREE(ObjFunction, object);
      break;
    }
    case OBJ_INSTANCE: {
      ObjInstance* instance = (ObjInstance*)object;
      free_table(&instance->fields);
      FREE(ObjInstance, object);
      break;
    }
    case OBJ_NATIVE:
      FREE(ObjNative, object);
      break;
    case OBJ_STRING: {
      ObjString* string = (ObjString*)object;
      // not needed for flexible array member
      // FREE_ARRAY(char, string->chars, string->length + 1);
      // FREE(ObjString, object);
      reallocate(object, sizeof(ObjString) + string->length * sizeof(char), 0);
      break;
    case OBJ_UPVALUE:
      FREE(ObjUpvalue, object);
      break;
    }
  }
}

static void sweep() {
  Obj* previous = NULL;
  Obj* object = vm.objects;
  while (object != NULL) {
    if (object->is_marked) {
      previous = object;
      object = object->next;
    } else {
      Obj* unreached = object;
      object = object->next;
      if (previous != NULL) {
        previous->next = object;
      } else {
        vm.objects = object;
      }

      free_object(unreached);
    }
  }
}

// mark sweep approach
void collect_garbage() {
#ifdef DEBUG_LOG_GC
  printf("-- gc begin\n");
  size_t before = vm.bytes_allocated;
#endif

  mark_roots(); 
  trace_references();
  table_remove_white(&vm.strings);
  sweep();

  vm.next_gc = vm.bytes_allocated * GC_HEAP_GROW_FACTOR;

#ifdef DEBUG_LOG_GC
  printf("-- gc end\n");
  printf("   collected %zu bytes (from %zu to %zu) next at %zu\n",
         before - vm.bytes_allocated, before, vm.bytes_allocated,
         vm.next_gc);
#endif
}

void free_objects() {
  Obj* object =  vm.objects;
  while (object != NULL) {
    Obj* next =  object->next;
    free_object(object);
    object = next;
  }

  free(vm.gray_stack);
}

