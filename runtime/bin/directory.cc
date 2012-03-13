// Copyright (c) 2012, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "bin/directory.h"

#include "bin/dartutils.h"
#include "bin/thread.h"
#include "include/dart_api.h"
#include "platform/assert.h"

dart::Mutex Directory::mutex_;
int Directory::service_ports_size_ = 0;
Dart_Port* Directory::service_ports_ = NULL;
int Directory::service_ports_index_ = 0;

void FUNCTION_NAME(Directory_Current)(Dart_NativeArguments args) {
  Dart_EnterScope();
  char* current = Directory::Current();
  if (current != NULL) {
    Dart_SetReturnValue(args, Dart_NewString(current));
    free(current);
  }
  Dart_ExitScope();
}


void FUNCTION_NAME(Directory_Exists)(Dart_NativeArguments args) {
  static const int kError = -1;
  static const int kExists = 1;
  static const int kDoesNotExist = 0;
  Dart_EnterScope();
  Dart_Handle path = Dart_GetNativeArgument(args, 0);
  if (Dart_IsString(path)) {
    Directory::ExistsResult result =
        Directory::Exists(DartUtils::GetStringValue(path));
    int return_value = kError;
    if (result == Directory::EXISTS) {
      return_value = kExists;
    }
    if (result == Directory::DOES_NOT_EXIST) {
      return_value = kDoesNotExist;
    }
    Dart_SetReturnValue(args, Dart_NewInteger(return_value));
  } else {
    Dart_SetReturnValue(args, Dart_NewInteger(kDoesNotExist));
  }
  Dart_ExitScope();
}


void FUNCTION_NAME(Directory_Create)(Dart_NativeArguments args) {
  Dart_EnterScope();
  Dart_Handle path = Dart_GetNativeArgument(args, 0);
  if (Dart_IsString(path)) {
    bool created = Directory::Create(DartUtils::GetStringValue(path));
    Dart_SetReturnValue(args, Dart_NewBoolean(created));
  } else {
    Dart_SetReturnValue(args, Dart_NewBoolean(false));
  }
  Dart_ExitScope();
}


void FUNCTION_NAME(Directory_CreateTemp)(Dart_NativeArguments args) {
  Dart_EnterScope();
  Dart_Handle path = Dart_GetNativeArgument(args, 0);
  char* result = Directory::CreateTemp(DartUtils::GetStringValue(path));
  if (result != NULL) {
    Dart_SetReturnValue(args, Dart_NewString(result));
    free(result);
  } else {
    Dart_Handle err = DartUtils::NewDartOSError();
    if (Dart_IsError(err)) {
      Dart_PropagateError(err);
    }
    Dart_SetReturnValue(args, err);
  }
  Dart_ExitScope();
}


void FUNCTION_NAME(Directory_Delete)(Dart_NativeArguments args) {
  Dart_EnterScope();
  Dart_Handle path = Dart_GetNativeArgument(args, 0);
  Dart_Handle recursive = Dart_GetNativeArgument(args, 1);
  if (Dart_IsString(path) && Dart_IsBoolean(recursive)) {
    bool deleted = Directory::Delete(DartUtils::GetStringValue(path),
                                     DartUtils::GetBooleanValue(recursive));
    Dart_SetReturnValue(args, Dart_NewBoolean(deleted));
  } else {
    Dart_SetReturnValue(args, Dart_NewBoolean(false));
  }
  Dart_ExitScope();
}


static CObject* DirectoryCreateRequest(const CObjectArray& request) {
  if (request.Length() == 2 && request[1]->IsString()) {
    CObjectString path(request[1]);
    bool created = Directory::Create(path.CString());
    return CObject::Bool(created);
  }
  return CObject::False();
}


static CObject* DirectoryDeleteRequest(const CObjectArray& request) {
  if (request.Length() == 3 && request[1]->IsString() && request[2]->IsBool()) {
    CObjectString path(request[1]);
    CObjectBool recursive(request[2]);
    bool deleted = Directory::Delete(path.CString(), recursive.Value());
    return CObject::Bool(deleted);
  }
  return CObject::False();
}


static CObject* DirectoryExistsRequest(const CObjectArray& request) {
  static const int kError = -1;
  static const int kExists = 1;
  static const int kDoesNotExist = 0;
  if (request.Length() == 2 && request[1]->IsString()) {
    CObjectString path(request[1]);
    Directory::ExistsResult result = Directory::Exists(path.CString());
    int return_value = kError;
    if (result == Directory::EXISTS) {
      return_value = kExists;
    }
    if (result == Directory::DOES_NOT_EXIST) {
      return_value = kDoesNotExist;
    }
    return new CObjectInt32(CObject::NewInt32(return_value));
  }
  return new CObjectInt32(CObject::NewInt32(kDoesNotExist));
}


static CObject* DirectoryCreateTempRequest(const CObjectArray& request) {
  if (request.Length() == 2 && request[1]->IsString()) {
    CObjectString path(request[1]);
    char* result = Directory::CreateTemp(path.CString());
    if (result != NULL) {
      CObject* temp_dir = new CObjectString(CObject::NewString(result));
      free(result);
      return temp_dir;
    } else {
      return CObject::NewOSError();
    }
  }
  return CObject::False();
}


static CObject* DirectoryListRequest(const CObjectArray& request,
                                     Dart_Port response_port) {
  if (request.Length() == 3 && request[1]->IsString() && request[2]->IsBool()) {
    DirectoryListing* dir_listing = new DirectoryListing(response_port);
    CObjectString path(request[1]);
    CObjectBool recursive(request[2]);
    bool completed = Directory::List(
        path.CString(), recursive.Value(), dir_listing);
    delete dir_listing;
    CObjectArray* response = new CObjectArray(CObject::NewArray(2));
    response->SetAt(
        0, new CObjectInt32(CObject::NewInt32(DirectoryListing::kListDone)));
    response->SetAt(1, CObject::Bool(completed));
    return response;
  }
  return CObject::False();
}


void DirectoryService(Dart_Port dest_port_id,
                      Dart_Port reply_port_id,
                      Dart_CObject* message) {
  CObject* response = CObject::False();
  CObjectArray request(message);
  if (message->type == Dart_CObject::kArray) {
    if (request.Length() > 1 && request[0]->IsInt32()) {
      CObjectInt32 requestType(request[0]);
      switch (requestType.Value()) {
        case Directory::kCreateRequest:
          response = DirectoryCreateRequest(request);
          break;
        case Directory::kDeleteRequest:
          response = DirectoryDeleteRequest(request);
          break;
        case Directory::kExistsRequest:
          response = DirectoryExistsRequest(request);
          break;
        case Directory::kCreateTempRequest:
          response = DirectoryCreateTempRequest(request);
          break;
        case Directory::kListRequest:
          response = DirectoryListRequest(request, reply_port_id);
          break;
        default:
          UNREACHABLE();
      }
    }
  }

  Dart_PostCObject(reply_port_id, response->AsApiCObject());
}


Dart_Port Directory::GetServicePort() {
  MutexLocker lock(&mutex_);
  if (service_ports_size_ == 0) {
    ASSERT(service_ports_ == NULL);
    service_ports_size_ = 16;
    service_ports_ = new Dart_Port[service_ports_size_];
    service_ports_index_ = 0;
    for (int i = 0; i < service_ports_size_; i++) {
      service_ports_[i] = kIllegalPort;
    }
  }

  Dart_Port result = service_ports_[service_ports_index_];
  if (result == kIllegalPort) {
    result = Dart_NewNativePort("DirectoryService",
                                DirectoryService,
                                true);
    ASSERT(result != kIllegalPort);
    service_ports_[service_ports_index_] = result;
  }
  service_ports_index_ = (service_ports_index_ + 1) % service_ports_size_;
  return result;
}


void FUNCTION_NAME(Directory_NewServicePort)(Dart_NativeArguments args) {
  Dart_EnterScope();
  Dart_SetReturnValue(args, Dart_Null());
  Dart_Port service_port = Directory::GetServicePort();
  if (service_port != kIllegalPort) {
    // Return a send port for the service port.
    Dart_Handle send_port = Dart_NewSendPort(service_port);
    Dart_SetReturnValue(args, send_port);
  }
  Dart_ExitScope();
}


CObjectArray* DirectoryListing::NewResponse(Response type, char* arg) {
  CObjectArray* response = new CObjectArray(CObject::NewArray(2));
  response->SetAt(0, new CObjectInt32(CObject::NewInt32(type)));
  response->SetAt(1, new CObjectString(CObject::NewString(arg)));
  return response;
}


bool DirectoryListing::HandleDirectory(char* dir_name) {
  // TODO(sgjesse): Pass flags to indicate whether directory
  // responses are needed.
  CObjectArray* response = NewResponse(kListDirectory, dir_name);
  return Dart_PostCObject(response_port_, response->AsApiCObject());
}


bool DirectoryListing::HandleFile(char* file_name) {
  // TODO(sgjesse): Pass flags to indicate whether file
  // responses are needed.
  CObjectArray* response = NewResponse(kListFile, file_name);
  return Dart_PostCObject(response_port_, response->AsApiCObject());
}


bool DirectoryListing::HandleError(char* message) {
  // TODO(sgjesse): Pass flags to indicate whether error
  // responses are needed.
  CObjectArray* response = NewResponse(kListError, message);
  return Dart_PostCObject(response_port_, response->AsApiCObject());
}
