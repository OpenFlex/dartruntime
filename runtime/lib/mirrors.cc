// Copyright (c) 2012, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/bootstrap_natives.h"

#include "vm/dart_entry.h"
#include "vm/exceptions.h"
#include "vm/json.h"
#include "vm/message.h"
#include "vm/port.h"
#include "vm/stack_frame.h"
#include "vm/resolver.h"

namespace dart {

static uint8_t* allocator(uint8_t* ptr, intptr_t old_size, intptr_t new_size) {
  void* new_ptr = realloc(reinterpret_cast<void*>(ptr), new_size);
  return reinterpret_cast<uint8_t*>(new_ptr);
}


DEFINE_NATIVE_ENTRY(Mirrors_send, 3) {
  GET_NATIVE_ARGUMENT(Instance, port, arguments->At(0));
  GET_NATIVE_ARGUMENT(Instance, message, arguments->At(1));
  GET_NATIVE_ARGUMENT(Instance, replyTo, arguments->At(2));

  // Get the send port id.
  Object& result = Object::Handle();
  result = DartLibraryCalls::PortGetId(port);
  if (result.IsError()) {
    Exceptions::PropagateError(result);
  }

  Integer& value = Integer::Handle();
  value ^= result.raw();
  int64_t send_port_id = value.AsInt64Value();

  // Get the reply port id.
  result = DartLibraryCalls::PortGetId(replyTo);
  if (result.IsError()) {
    Exceptions::PropagateError(result);
  }
  value ^= result.raw();
  int64_t reply_port_id = value.AsInt64Value();

  // Construct the message.
  uint8_t* data = NULL;
  SnapshotWriter writer(Snapshot::kMessage, &data, &allocator);
  writer.WriteObject(message.raw());
  writer.FinalizeBuffer();

  // Post the message.
  bool retval = PortMap::PostMessage(new Message(
      send_port_id, reply_port_id, data, Message::kOOBPriority));
  const Bool& retval_obj = Bool::Handle(Bool::Get(retval));
  arguments->SetReturn(retval_obj);
}


DEFINE_NATIVE_ENTRY(Mirrors_caller, 2) {
  GET_NATIVE_ARGUMENT(Integer, level, arguments->At(0));
  GET_NATIVE_ARGUMENT(Instance, map, arguments->At(1));

  StackFrame* frame;
  int64_t i = level.AsInt64Value();
  DartFrameIterator iterator;
  while (i > 0) {
    frame = iterator.NextFrame();
    if (frame == NULL) {
      return;
    }
    i--;
  }

  ASSERT(frame != NULL);

#define PUT_TO_MAP_AND_CHECK(xkey, xvalue)                                     \
  {                                                                            \
    String& key = String::Handle(String::New(#xkey));                          \
    String& value = String::Handle(xvalue);                                    \
    Object& result = Object::Handle(DartLibraryCalls::MapSetAt(map, key,       \
        value));                                                               \
    if (result.IsError()) {                                                    \
      Exceptions::PropagateError(result);                                      \
    }                                                                          \
  }

  const Function& caller = Function::Handle(frame->LookupDartFunction());
  ASSERT(!caller.IsNull());
  PUT_TO_MAP_AND_CHECK(function, caller.name())

  // if it is an inner function, then find out the outermost function name
  if (caller.IsLocalFunction()) {
    Function& outer_function = Function::Handle(caller.parent_function());
    while (outer_function.IsLocalFunction()) {
      outer_function = outer_function.parent_function();
    }
    PUT_TO_MAP_AND_CHECK(outer_function, outer_function.name())
  }

  const Class& caller_class = Class::Handle(caller.owner());
  ASSERT(!caller_class.IsNull());
  PUT_TO_MAP_AND_CHECK(class, caller_class.Name())

  const Library& caller_library = Library::Handle(caller_class.library());
  ASSERT(!caller_library.IsNull());
  PUT_TO_MAP_AND_CHECK(library, caller_library.name())

  const Script& caller_script = Script::Handle(caller_class.script());
  ASSERT(!caller_script.IsNull());
  PUT_TO_MAP_AND_CHECK(script, caller_script.url())

#undef PUT_TO_MAP_AND_CHECK
}


static bool JSONGetString(JSONReader* reader,
                          const char** value_chars,
                          int* value_len) {
  if (reader->Type() != JSONReader::kString) {
    return false;
  }
  *value_chars = reader->ValueChars();
  *value_len = reader->ValueLen();
  return true;
}


DEFINE_NATIVE_ENTRY(Mirrors_processResponse, 3) {
  GET_NATIVE_ARGUMENT(Instance, port, arguments->At(0));
  GET_NATIVE_ARGUMENT(String, command, arguments->At(1));
  GET_NATIVE_ARGUMENT(String, response, arguments->At(2));

  const char* json_text = response.ToCString();
  if (command.Equals("isolateMirrorOf")) {
    JSONReader reader(json_text);
    const char* debug_name = "";
    int debug_name_len = 0;
    if (!reader.Seek("ok") || !reader.IsTrue() ||
        !reader.Seek("debugName") ||
        !JSONGetString(&reader, &debug_name, &debug_name_len)) {
      // TODO(turnidge): Use an exception class instead of a String.
      Exceptions::Throw(Instance::Handle(String::NewFormatted(
          "Error while processing mirror request.")));
      UNREACHABLE();
    }

    // Create and return a new instance of _IsolateMirrorImpl.
    Library& lib = Library::Handle(Library::MirrorsLibrary());
    const String& public_class_name =
        String::Handle(String::NewSymbol("_IsolateMirrorImpl"));
    const String& class_name =
        String::Handle(lib.PrivateName(public_class_name));
    const String& function_name =
        String::Handle(String::NewSymbol("_make"));
    const int kNumArgs = 2;
    const Array& kNoArgNames = Array::Handle();
    const Function& function = Function::Handle(
        Resolver::ResolveStatic(lib,
                                class_name,
                                function_name,
                                kNumArgs,
                                kNoArgNames,
                                Resolver::kIsQualified));
    ASSERT(!function.IsNull());
    GrowableArray<const Object*> args(kNumArgs);
    args.Add(&port);
    const String& debug_name_str = String::Handle(
        String::NewFormatted("%.*s", debug_name_len, debug_name));
    args.Add(&debug_name_str);
    const Object& result = Object::Handle(
        DartEntry::InvokeStatic(function, args, kNoArgNames));
    arguments->SetReturn(result);
  }
}


void HandleMirrorsMessage(Isolate* isolate,
                          Dart_Port reply_port,
                          const Instance& message) {
  TextBuffer buffer(64);
  if (!message.IsString()) {
    buffer.Printf(
        "{ \"ok\": false, \"error\": \"Malformed mirrors request\" }");
  } else {
    String& json_string = String::Handle();
    json_string ^= message.raw();
    const char* json_text = json_string.ToCString();
    JSONReader reader(json_text);

    if (reader.Seek("command")) {
      if (reader.IsStringLiteral("isolateMirrorOf")) {
        buffer.Printf("{ \"ok\": true, \"debugName\": \"%s\" }",
                      isolate->name());
      } else {
        const char* command = "";
        int command_len = 0;
        JSONGetString(&reader, &command, &command_len);
        buffer.Printf(
            "{ \"ok\": false, \"error\": \"Command '%.*s' not recognized\" }",
            command_len, command);
      }
    } else {
      buffer.Printf(
          "{ \"ok\": false, \"error\": \"Field 'command' not found\" }");
    }
  }

  Dart_CObject reply;
  reply.type = Dart_CObject::kString;
  reply.value.as_string = buffer.buf();
  if (!Dart_PostCObject(reply_port, &reply)) {
    OS::PrintErr("Unable to post mirrors reply");
    return;
  }
}

}  // namespace dart
