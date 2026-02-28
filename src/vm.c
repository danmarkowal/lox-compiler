#include <stdarg.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "chunk.h"
#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include "object.h"
#include "memory.h"
#include "vm.h"

VM vm;

static Value clock_native(int arg_count, Value* args) {
  return NUMBER_VAL((double) clock() / CLOCKS_PER_SEC);
}

static void reset_stack() {
  vm.stack_top = vm.stack;
  vm.frame_count = 0;
  vm.open_upvalues = NULL;
}

static Value peek(int distance) {
  return vm.stack_top[-1 - distance];
}

static bool is_falsey(Value value) {
  return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static void concatenate() {
  ObjString* b = AS_STRING(peek(0));
  ObjString* a = AS_STRING(peek(1));

  int length = a->length + b->length;
  char* chars = ALLOCATE(char, length + 1);
  memcpy(chars, a->chars, a->length);
  memcpy(chars + a->length, b->chars, b->length);
  chars[length] = '\0';

  ObjString* result = take_string(chars, length);
  pop();
  pop();
  push(OBJ_VAL(result));
}

static void runtime_error(const char* format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  fputs("\n", stderr);

  // print stacktrace from most recently called function
  for (int i = vm.frame_count - 1; i >= 0; i--) {
    CallFrame* frame = &vm.frames[i];
    ObjFunction* function = frame->closure->function;
    // this has to be a size_t because we are doing pointer math
    size_t instruction = frame->ip - function->chunk.code - 1;
    fprintf(stderr, "[line %d] in ", get_line(&function->chunk, (int) instruction));
    if (function->name == NULL) {
      // main
      fprintf(stderr, "script\n");
    } else {
      fprintf(stderr, "%s()\n", function->name->chars);
    }
  }

  reset_stack();
}

static void define_native(const char* name, NativeFn function) {
  push(OBJ_VAL(copy_string(name, (int) strlen(name))));
  push(OBJ_VAL(new_native(function)));
  table_set(&vm.globals, AS_STRING(vm.stack[0]), vm.stack[1]);
  pop();
  pop();
}

static bool call(ObjClosure* closure, int arg_count) {
  if (arg_count != closure->function->arity) {
    runtime_error("Expected %d arguments but got %d.", closure->function->arity, arg_count);
    return false;
  }

  if (vm.frame_count == FRAMES_MAX) {
    runtime_error("Stack overflow");
    return false;
  }

  CallFrame* frame = &vm.frames[vm.frame_count++];
  frame->closure = closure;
  frame->ip = closure->function->chunk.code;
  // parameters start at slot 1 as slot 0 is reserved
  // for the function/method being called
  frame->slots = vm.stack_top - arg_count - 1;
  return true;
}

static bool call_value(Value callee, int arg_count) {
  if (IS_OBJ(callee)) {
    switch (OBJ_TYPE(callee)) {
      case OBJ_BOUND_METHOD: {
        ObjBoundMethod* bound = AS_BOUND_METHOD(callee);
        vm.stack_top[-arg_count - 1] = bound->receiver;
        return call(bound->method, arg_count);
      }
      case OBJ_CLASS: {
        ObjClass* klass = AS_CLASS(callee);
        vm.stack_top[-arg_count - 1] = OBJ_VAL(new_instance(klass));
        Value initializer;
        if (table_get(&klass->methods, vm.init_string,
                     &initializer)) {
          return call(AS_CLOSURE(initializer), arg_count);
        } else if (arg_count != 0) {
          runtime_error("Expected 0 arguments but got %d", arg_count);
          return false;

        }
        return true;
      }
      case OBJ_CLOSURE:
        return call(AS_CLOSURE(callee), arg_count);
      case OBJ_NATIVE: {
        NativeFn native = AS_NATIVE(callee);
        Value result = native(arg_count, vm.stack_top - arg_count);
        vm.stack_top -= arg_count + 1;
        push(result);
        return true;
      }
      default:
        break; 
    }
  }
 
  runtime_error("Can only call functions and classes");
  return false;
}

static bool invoke_from_class(ObjClass* klass, ObjString* name,
                            int arg_count) {
  Value method;
  if (!table_get(&klass->methods, name, &method)) {
    runtime_error("Undefined property '%s'", name->chars);
    return false;
  }

  return call(AS_CLOSURE(method), arg_count);
}

static bool invoke(ObjString* name, int arg_count) {
  Value receiver = peek(arg_count);
  if (!IS_INSTANCE(receiver)) {
    runtime_error("Only instances have methods");
    return false;
  }

  ObjInstance* instance = AS_INSTANCE(receiver);

  Value value;
  if (table_get(&instance->fields, name, &value)) {
    vm.stack_top[-arg_count - 1] = value;
    return call_value(value, arg_count);
  }

  return invoke_from_class(instance->klass, name, arg_count);
}

static ObjUpvalue* capture_upvalue(Value* local) {
  // look for existing upvalue in list
  ObjUpvalue* prev_upvalue = NULL;
  ObjUpvalue* upvalue = vm.open_upvalues;
  while (upvalue != NULL && upvalue->location > local) {
    prev_upvalue = upvalue;
    upvalue = upvalue->next;
  }

  if (upvalue != NULL && upvalue->location == local) {
    return upvalue;
  }

  // create a new one otherwise...
  ObjUpvalue* created_upvalue = new_upvalue(local);

  // ...and insert it into the list
  created_upvalue->next = upvalue;

  if (prev_upvalue == NULL) {
    vm.open_upvalues = created_upvalue;
  } else {
    prev_upvalue->next = created_upvalue;
  }

  return created_upvalue;
}

static void close_upvalues(Value* last) {
  while (vm.open_upvalues != NULL &&
         vm.open_upvalues->location >= last) {
    ObjUpvalue* upvalue = vm.open_upvalues;
    upvalue->closed = *upvalue->location;
    upvalue->location = &upvalue->closed;
    vm.open_upvalues = upvalue->next;
  }
}

static void define_method(ObjString* name) {
  Value method = peek(0);
  ObjClass* klass = AS_CLASS(peek(1));
  table_set(&klass->methods, name, method);
  pop();
}

static bool bind_method(ObjClass* klass, ObjString* name) {
  Value method;
  if (!table_get(&klass->methods, name, &method)) {
    runtime_error("Undefined property '%s'", name->chars);
    return false;
  }

  ObjBoundMethod* bound = new_bound_method(peek(0),
                                         AS_CLOSURE(method));
  pop();
  push(OBJ_VAL(bound));
  return true;
}

static InterpretResult run() {
  CallFrame* frame = &vm.frames[vm.frame_count - 1];

#define READ_BYTE() (*frame->ip++)
#define READ_SHORT() \
    (frame->ip += 2, \
    (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_CONSTANT() \
    (frame->closure->function->chunk.constants.values[READ_BYTE()])
#define READ_CONSTANT_LONG() \
    (frame->closure->function->chunk.constants.values[(READ_BYTE() << 0) | (READ_BYTE() << 8) | (READ_BYTE() << 16)])
#define READ_STRING() AS_STRING(READ_CONSTANT())
#define READ_STRING_LONG() AS_STRING(READ_CONSTANT_LONG())
#define BINARY_OP(value_type, op) \
    do { \
      if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) { \
        runtime_error("Operands must be numbers"); \
        return INTERPRET_RUNTIME_ERROR; \
      } \
      double b = AS_NUMBER(pop()); \
      double a = AS_NUMBER(pop()); \
      push(value_type(a op b)); \
    } while (false)

  for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
    // print the stack and disassembled instruction
    printf("          ");
    for (Value* slot = vm.stack; slot < vm.stack_top; slot++) {
      printf("[ ");
      print_value(*slot);
      printf(" ]");
    }
    printf("\n");
    
    disassemble_instruction(&frame->closure->function->chunk,
        (int)(frame->ip - frame->closure->function->chunk.code));
#endif

    uint8_t instruction;
    switch (instruction = READ_BYTE()) {
      case OP_CONSTANT:
      case OP_CONSTANT_LONG: {
        Value constant = instruction == OP_CONSTANT ? READ_CONSTANT() : READ_CONSTANT_LONG();
        push(constant);
        break;
      }
      case OP_NIL: {
        push(NIL_VAL);
        break;
      }
      case OP_TRUE: {
        push(BOOL_VAL(true));
        break;
      }
      case OP_FALSE: {
        push(BOOL_VAL(false));
        break;
      }
      case OP_POP: {
        pop();
        break;
      }
      case OP_GET_LOCAL: {
        uint8_t slot = READ_BYTE();
        push(frame->slots[slot]); 
        break;
      }
      case OP_SET_LOCAL: {
        uint8_t slot = READ_BYTE();
        frame->slots[slot] = peek(0);
        break;
      }
      case OP_GET_GLOBAL:
      case OP_GET_GLOBAL_LONG: {
        ObjString* name = instruction == OP_GET_GLOBAL ? READ_STRING() : READ_STRING_LONG();
        Value value;
        if (!table_get(&vm.globals, name, &value)) {
          runtime_error("Undefined variable '%s'", name->chars);
          return INTERPRET_RUNTIME_ERROR;
        }

        push(value);
        break;
      }
      case OP_DEFINE_GLOBAL:
      case OP_DEFINE_GLOBAL_LONG: {
        ObjString* name = instruction == OP_DEFINE_GLOBAL ? READ_STRING() : READ_STRING_LONG();
        table_set(&vm.globals, name, peek(0));
        pop();
        break;
      }
      case OP_SET_GLOBAL:
      case OP_SET_GLOBAL_LONG: {
        ObjString* name = instruction == OP_SET_GLOBAL ? READ_STRING() : READ_STRING_LONG();
        // not !table_set(...) as if table_set returns true, that means a new entry was added
        // that means that the variable was previously undefined
        if (table_set(&vm.globals, name, peek(0))) {
          table_delete(&vm.globals, name); 
          runtime_error("Undefined variable '%s'", name->chars);
          return INTERPRET_RUNTIME_ERROR;
        }

        break;
      }
      case OP_GET_UPVALUE: {
        uint8_t slot = READ_BYTE();
        push(*frame->closure->upvalues[slot]->location);
        break;
      }
      case OP_SET_UPVALUE: {
        uint8_t slot = READ_BYTE();
        *frame->closure->upvalues[slot]->location = peek(0);
        break;
      }
      case OP_GET_PROPERTY:
      case OP_GET_PROPERTY_LONG: {
        if (!IS_INSTANCE(peek(0))) {
          runtime_error("Only instances have properties");
          return INTERPRET_RUNTIME_ERROR;
        }

        ObjInstance* instance = AS_INSTANCE(peek(0));
        ObjString* name = instruction == OP_GET_PROPERTY ? READ_STRING() : READ_STRING_LONG();

        Value value;
        if (table_get(&instance->fields, name, &value)) {
          pop();
          push(value);
          break;
        }

        if (!bind_method(instance->klass, name)) {
          return INTERPRET_RUNTIME_ERROR;
        }

        break;
      }
      case OP_SET_PROPERTY:
      case OP_SET_PROPERTY_LONG: {
        if (!IS_INSTANCE(peek(1))) {
          runtime_error("Only instances have fields");
          return INTERPRET_RUNTIME_ERROR;
        }
        
        ObjInstance* instance = AS_INSTANCE(peek(1));
        ObjString* name = instruction == OP_SET_PROPERTY ? READ_STRING() : READ_STRING_LONG();
        table_set(&instance->fields, name, peek(0));
        Value value = pop();
        pop();
        push(value);
        break;
      }
      case OP_GET_SUPER: {
        ObjString* name = READ_STRING();
        ObjClass* superclass = AS_CLASS(pop());

        if (!bind_method(superclass, name)) {
          return INTERPRET_RUNTIME_ERROR;
        }

        break;
      }
      case OP_EQUAL: {
        Value b = pop();
        Value a = pop();
        push(BOOL_VAL(values_equal(a, b)));
        break;
      }
      case OP_GREATER: {
        BINARY_OP(BOOL_VAL, >);
        break;
      }
      case OP_LESS: {
        BINARY_OP(BOOL_VAL, <);
        break;
      }
      case OP_ADD: {
        // challenge: what would I have this do if one was a string and another wasn't?
        // if the first operand is a string and the second a number, JS concatenates them together
        // that seems reasonable, especially if doing something like print("Hello " + 18 + " world!")
        if (IS_STRING(peek(0)) && IS_STRING(peek(1))) {
          concatenate();
        } else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
          // do this instead of binary op to reduce redundant checks
          double b = AS_NUMBER(pop());
          double a = AS_NUMBER(pop());
          push(NUMBER_VAL(a + b));
        } else {
          runtime_error("Operands must be two numbers or two strings");
          return INTERPRET_RUNTIME_ERROR;
        }
        
        break;
      }
      case OP_SUBTRACT: {
        BINARY_OP(NUMBER_VAL, -);
        break;
      }
      case OP_MULTIPLY: {
        BINARY_OP(NUMBER_VAL, *);
        break;
      }
      case OP_DIVIDE: {
        BINARY_OP(NUMBER_VAL, /);
        break;
      }
      case OP_NOT: {
        // prevents pushing and popping the same value off the stack
        *(vm.stack_top - 1) = BOOL_VAL(is_falsey(*(vm.stack_top - 1)));
        break;
      }
      case OP_NEGATE: {
        if (!IS_NUMBER(peek(0))) {
          runtime_error("Operand must be a number");
          return INTERPRET_RUNTIME_ERROR;
        }

        // prevents pushing and popping the same value off the stack
        *(vm.stack_top - 1) = NUMBER_VAL(-AS_NUMBER(*(vm.stack_top - 1)));
        break;
      }
      case OP_JUMP: {
        uint16_t offset = READ_SHORT();
        frame->ip += offset;
        break;
      }
      case OP_JUMP_IF_FALSE: {
        uint16_t offset = READ_SHORT();
        if (is_falsey(peek(0))) frame->ip += offset;
        break;
      }
      // expressions modify the stack
      // statements have a total stack effect of zero
      case OP_PRINT: {
        print_value(pop());
        printf("\n");
        break;
      }
      case OP_LOOP: {
        uint16_t offset = READ_SHORT();
        frame->ip -= offset;
        break;
      }
      case OP_CALL: {
        int arg_count = READ_BYTE();
        if (!call_value(peek(arg_count), arg_count)) {
          return INTERPRET_RUNTIME_ERROR;
        }
        
        frame = &vm.frames[vm.frame_count - 1];
        break;
      }
      case OP_INVOKE:
      case OP_INVOKE_LONG: {
        ObjString* method = instruction == OP_INVOKE ? READ_STRING() : READ_STRING_LONG();
        int arg_count = READ_BYTE();
        if (!invoke(method, arg_count)) {
          return INTERPRET_RUNTIME_ERROR;
        }

        frame = &vm.frames[vm.frame_count - 1];
        break;
      }
      case OP_SUPER_INVOKE: {
        ObjString* method = READ_STRING();
        int arg_count = READ_BYTE();
        ObjClass* superclass = AS_CLASS(pop());
        if (!invoke_from_class(superclass, method, arg_count)) {
          return INTERPRET_RUNTIME_ERROR;
        }

        frame = &vm.frames[vm.frame_count - 1];
        break;
      }
      case OP_CLOSURE:
      case OP_CLOSURE_LONG: {
        ObjFunction* function = AS_FUNCTION(instruction == OP_CLOSURE ? READ_CONSTANT() : READ_CONSTANT_LONG());
        ObjClosure* closure = new_closure(function);
        push(OBJ_VAL(closure));

        for (int i = 0; i < closure->upvalue_count; i++) {
          uint8_t is_local = READ_BYTE();
          uint8_t index = READ_BYTE();
          if (is_local) {
            closure->upvalues[i] =
                capture_upvalue(frame->slots + index);
          } else {
            closure->upvalues[i] = frame->closure->upvalues[index];
          }
        }

        break;
      }
      case OP_CLOSE_UPVALUE:
        close_upvalues(vm.stack_top - 1);
        pop();
        break;
      case OP_RETURN: {
        // store the result as a temporary variable
        Value result = pop();
        close_upvalues(frame->slots);
        // exit out of current  frame
        vm.frame_count--;

        // return from "main" function
        if (vm.frame_count == 0) {
          pop();
          return INTERPRET_OK;
        }
        
        // reset stack pointer to where it was before
        vm.stack_top = frame->slots;
        // push result back onto the stack
        push(result);
        // set current frame to the one before
        frame = &vm.frames[vm.frame_count - 1];
        break;
      }    
      case OP_CLASS:
      case OP_CLASS_LONG:
        push(OBJ_VAL(new_class(instruction == OP_CLASS ? READ_STRING() : READ_STRING_LONG())));
        break;
      case OP_INHERIT: {
        Value superclass = peek(1);
        if (!IS_CLASS(superclass)) {
          runtime_error("Superclass must be a class");
          return INTERPRET_RUNTIME_ERROR;
        }

        ObjClass* subclass = AS_CLASS(peek(0));
        // this optimisation only works because classes are closed
        table_add_all(&AS_CLASS(superclass)->methods, &subclass->methods);
        pop();
        break;
      }
      case OP_METHOD:
      case OP_METHOD_LONG:
        define_method(instruction == OP_METHOD ? READ_STRING() : READ_STRING_LONG());
        break;
    }
  }

#undef READ_BYTE
#undef READ_CONSTANT
#undef READ_CONSTANT_LONG
#undef READ_SHORT
#undef READ_STRING
#undef READ_STRING_LONG
#undef BINARY_OP
}

void init_vm() {
  reset_stack();
  vm.objects = NULL;
  vm.bytes_allocated = 0;
  vm.next_gc = 1024 * 1024;
  vm.gray_count = 0;
  vm.gray_capacity = 0;
  vm.gray_stack = NULL;

  init_table(&vm.globals);
  init_table(&vm.strings);

  // prevent GC from reading uninitialised memory
  vm.init_string = NULL;
  vm.init_string = copy_string("init", 4);

  define_native("clock", clock_native);
}

void free_vm() {
  free_table(&vm.strings);
  free_table(&vm.strings);
  vm.init_string = NULL;
  free_objects();
}

InterpretResult interpret(const char* source) {
  ObjFunction* function = compile(source);
  if (function == NULL) return INTERPRET_COMPILE_ERROR;

  // push "main" function onto the stack at position zero
  push(OBJ_VAL(function));
  // call "main" function
  ObjClosure* closure = new_closure(function);
  pop();
  push(OBJ_VAL(closure));
  call(closure, 0); 

  return run();
}

void push(Value value) {
  *vm.stack_top = value;
  vm.stack_top++;
}

Value pop() {
  vm.stack_top--;
  return *vm.stack_top;
}

