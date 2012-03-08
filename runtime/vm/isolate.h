// Copyright (c) 2012, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef VM_ISOLATE_H_
#define VM_ISOLATE_H_

#include "include/dart_api.h"
#include "platform/assert.h"
#include "platform/thread.h"
#include "vm/gc_callbacks.h"
#include "vm/store_buffer.h"
#include "vm/timer.h"

namespace dart {

// Forward declarations.
class ApiState;
class CodeIndexTable;
class Debugger;
class HandleScope;
class HandleVisitor;
class Heap;
class LongJump;
class MessageHandler;
class Mutex;
class ObjectPointerVisitor;
class ObjectStore;
class RawContext;
class RawError;
class StackResource;
class StubCode;
class Zone;


class Isolate {
 public:
  ~Isolate();

  static inline Isolate* Current() {
    return reinterpret_cast<Isolate*>(Thread::GetThreadLocal(isolate_key));
  }

  static void SetCurrent(Isolate* isolate);

  static void InitOnce();
  static Isolate* Init(const char* name_prefix);
  void Shutdown();

  // Visit all object pointers.
  void VisitObjectPointers(ObjectPointerVisitor* visitor, bool validate_frames);

  void VisitWeakPersistentHandles(HandleVisitor* visitor);

  StoreBufferBlock* store_buffer() { return &store_buffer_; }

  Dart_MessageNotifyCallback message_notify_callback() const {
    return message_notify_callback_;
  }
  void set_message_notify_callback(Dart_MessageNotifyCallback value) {
    message_notify_callback_ = value;
  }

  const char* name() const { return name_; }

  Dart_Port main_port() { return main_port_; }
  void set_main_port(Dart_Port port) {
    ASSERT(main_port_ == 0);  // Only set main port once.
    main_port_ = port;
  }

  Heap* heap() const { return heap_; }
  void set_heap(Heap* value) { heap_ = value; }
  static intptr_t heap_offset() { return OFFSET_OF(Isolate, heap_); }

  ObjectStore* object_store() const { return object_store_; }
  void set_object_store(ObjectStore* value) { object_store_ = value; }
  static intptr_t object_store_offset() {
    return OFFSET_OF(Isolate, object_store_);
  }

  StackResource* top_resource() const { return top_resource_; }
  void set_top_resource(StackResource* value) { top_resource_ = value; }

  RawContext* top_context() const { return top_context_; }
  void set_top_context(RawContext* value) { top_context_ = value; }
  static intptr_t top_context_offset() {
    return OFFSET_OF(Isolate, top_context_);
  }

  int32_t random_seed() const { return random_seed_; }
  void set_random_seed(int32_t value) { random_seed_ = value; }

  uword top_exit_frame_info() const { return top_exit_frame_info_; }
  void set_top_exit_frame_info(uword value) { top_exit_frame_info_ = value; }
  static intptr_t top_exit_frame_info_offset() {
    return OFFSET_OF(Isolate, top_exit_frame_info_);
  }

  ApiState* api_state() const { return api_state_; }
  void set_api_state(ApiState* value) { api_state_ = value; }

  StubCode* stub_code() const { return stub_code_; }
  void set_stub_code(StubCode* value) { stub_code_ = value; }

  CodeIndexTable* code_index_table() const { return code_index_table_; }
  void set_code_index_table(CodeIndexTable* value) {
    code_index_table_ = value;
  }

  LongJump* long_jump_base() const { return long_jump_base_; }
  void set_long_jump_base(LongJump* value) { long_jump_base_ = value; }

  TimerList& timer_list() { return timer_list_; }

  Zone* current_zone() const { return current_zone_; }
  void set_current_zone(Zone* zone) { current_zone_ = zone; }
  static intptr_t current_zone_offset() {
    return OFFSET_OF(Isolate, current_zone_);
  }

  int32_t no_gc_scope_depth() const {
#if defined(DEBUG)
    return no_gc_scope_depth_;
#else
    return 0;
#endif
  }

  void IncrementNoGCScopeDepth() {
#if defined(DEBUG)
    ASSERT(no_gc_scope_depth_ < INT_MAX);
    no_gc_scope_depth_ += 1;
#endif
  }

  void DecrementNoGCScopeDepth() {
#if defined(DEBUG)
    ASSERT(no_gc_scope_depth_ > 0);
    no_gc_scope_depth_ -= 1;
#endif
  }

  int32_t no_handle_scope_depth() const {
#if defined(DEBUG)
    return no_handle_scope_depth_;
#else
    return 0;
#endif
  }

  void IncrementNoHandleScopeDepth() {
#if defined(DEBUG)
    ASSERT(no_handle_scope_depth_ < INT_MAX);
    no_handle_scope_depth_ += 1;
#endif
  }

  void DecrementNoHandleScopeDepth() {
#if defined(DEBUG)
    ASSERT(no_handle_scope_depth_ > 0);
    no_handle_scope_depth_ -= 1;
#endif
  }

  HandleScope* top_handle_scope() const {
#if defined(DEBUG)
    return top_handle_scope_;
#else
    return 0;
#endif
  }

  void set_top_handle_scope(HandleScope* handle_scope) {
#if defined(DEBUG)
    top_handle_scope_ = handle_scope;
#endif
  }

  void set_init_callback_data(void* value) {
    init_callback_data_ = value;
  }
  void* init_callback_data() const {
    return init_callback_data_;
  }

  Dart_LibraryTagHandler library_tag_handler() const {
    return library_tag_handler_;
  }
  void set_library_tag_handler(Dart_LibraryTagHandler value) {
    library_tag_handler_ = value;
  }

  void SetStackLimit(uword value);
  void SetStackLimitFromCurrentTOS(uword isolate_stack_top);

  uword stack_limit_address() const {
    return reinterpret_cast<uword>(&stack_limit_);
  }

  // The current stack limit.  This may be overwritten with a special
  // value to trigger interrupts.
  uword stack_limit() const { return stack_limit_; }

  // The true stack limit for this isolate.  This does not change
  // after isolate initialization.
  uword saved_stack_limit() const { return saved_stack_limit_; }

  enum {
    kApiInterrupt = 0x1,      // An interrupt from Dart_InterruptIsolate.
    kMessageInterrupt = 0x2,  // An interrupt to process an out of band message.

    kInterruptsMask = kApiInterrupt | kMessageInterrupt,
  };

  void ScheduleInterrupts(uword interrupt_bits);
  uword GetAndClearInterrupts();

  MessageHandler* message_handler() const { return message_handler_; }
  void set_message_handler(MessageHandler* value) { message_handler_ = value; }

  // Returns null on success, a RawError on failure.
  RawError* StandardRunLoop();

  intptr_t ast_node_id() const { return ast_node_id_; }
  void set_ast_node_id(int value) { ast_node_id_ = value; }

  Debugger* debugger() const { return debugger_; }

  static void SetCreateCallback(Dart_IsolateCreateCallback cback);
  static Dart_IsolateCreateCallback CreateCallback();

  static void SetInterruptCallback(Dart_IsolateInterruptCallback cback);
  static Dart_IsolateInterruptCallback InterruptCallback();

  GcPrologueCallbacks& gc_prologue_callbacks() {
    return gc_prologue_callbacks_;
  }

  GcEpilogueCallbacks& gc_epilogue_callbacks() {
    return gc_epilogue_callbacks_;
  }

 private:
  Isolate();

  void BuildName(const char* name_prefix);
  void PrintInvokedFunctions();

  static uword GetSpecifiedStackSize();

  static const uword kStackSizeBuffer = (128 * KB);
  static const uword kDefaultStackSize = (1 * MB);

  static ThreadLocalKey isolate_key;
  StoreBufferBlock store_buffer_;
  Dart_MessageNotifyCallback message_notify_callback_;
  char* name_;
  Dart_Port main_port_;
  Heap* heap_;
  ObjectStore* object_store_;
  StackResource* top_resource_;
  RawContext* top_context_;
  Zone* current_zone_;
#if defined(DEBUG)
  int32_t no_gc_scope_depth_;
  int32_t no_handle_scope_depth_;
  HandleScope* top_handle_scope_;
#endif
  int32_t random_seed_;
  uword top_exit_frame_info_;
  void* init_callback_data_;
  Dart_LibraryTagHandler library_tag_handler_;
  ApiState* api_state_;
  StubCode* stub_code_;
  CodeIndexTable* code_index_table_;
  Debugger* debugger_;
  LongJump* long_jump_base_;
  TimerList timer_list_;
  intptr_t ast_node_id_;
  Mutex* mutex_;  // protects stack_limit_ and saved_stack_limit_.
  uword stack_limit_;
  uword saved_stack_limit_;
  MessageHandler* message_handler_;
  GcPrologueCallbacks gc_prologue_callbacks_;
  GcEpilogueCallbacks gc_epilogue_callbacks_;

  static Dart_IsolateCreateCallback create_callback_;
  static Dart_IsolateInterruptCallback interrupt_callback_;

  DISALLOW_COPY_AND_ASSIGN(Isolate);
};

}  // namespace dart

#endif  // VM_ISOLATE_H_
