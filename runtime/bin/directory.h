// Copyright (c) 2012, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef BIN_DIRECTORY_H_
#define BIN_DIRECTORY_H_

#include "bin/builtin.h"
#include "bin/dartutils.h"
#include "platform/globals.h"

class DirectoryListing {
 public:
  enum Response {
    kListDirectory = 0,
    kListFile = 1,
    kListError = 2,
    kListDone = 3
  };

  explicit DirectoryListing(Dart_Port response_port)
      : response_port_(response_port) {}
  bool HandleDirectory(char* dir_name);
  bool HandleFile(char* file_name);
  bool HandleError(char* message);

 private:
  CObjectArray* NewResponse(Response response, char* arg);
  Dart_Port response_port_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(DirectoryListing);
};


class Directory {
 public:
  enum ExistsResult {
    UNKNOWN,
    EXISTS,
    DOES_NOT_EXIST
  };

  enum DirectoryRequest {
    kCreateRequest = 0,
    kDeleteRequest = 1,
    kExistsRequest = 2,
    kCreateTempRequest = 3,
    kListRequest = 4
  };

  static bool List(const char* path,
                   bool recursive,
                   DirectoryListing* listing);

  static void ListSync(const char* path,
                       bool recursive,
                       bool full_paths,
                       Dart_Handle dir_callback,
                       Dart_Handle file_callback,
                       Dart_Handle done_callback,
                       Dart_Handle error_callback);

  static ExistsResult Exists(const char* path);

  static bool Create(const char* path);

  static int CreateTemp(const char* const_template,
                        char** path,
                        char* os_error_message,
                        int os_error_message_len);

  static bool Delete(const char* path, bool recursive);

  static const char* CurrentUserHome();

  DISALLOW_ALLOCATION();
  DISALLOW_IMPLICIT_CONSTRUCTORS(Directory);
};


#endif  // BIN_DIRECTORY_H_
