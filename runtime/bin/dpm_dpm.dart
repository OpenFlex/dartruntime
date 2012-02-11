// Copyright (c) 2012, Ladislav Thon. All rights reserved. Use of this source
// code is governed by a BSD-style license that can be found in the LICENSE file.










class DpmException implements Exception {
  final String _message;

  DpmException(String this._message);

  String toString() => _message;
}

String resolveLocally(String importSpecStr) {
  ImportSpecification spec = new ImportSpecification.parse(importSpecStr);
  Repository repo = new _LocalUserRepository();

  Import import = new Import.resolve(spec, repo);
  return import.url;
}

