// Copyright (c) 2012, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

// VM-specific implementation of the dart:mirrors library.

class _IsolateMirrorImpl implements IsolateMirror {
  _IsolateMirrorImpl(this.port, this.debugName) {}

  final SendPort port;
  final String debugName;

  static _make(SendPort port, String debugName) {
    return new _IsolateMirrorImpl(port, debugName);
  }
}

class _CallerMirrorImpl implements CallerMirror {
  final String functionName;
  final String outerFunctionName;
  final String className;
  final String libraryName;
  final String scriptUrl;

  _CallerMirrorImpl(this.functionName, this.outerFunctionName, this.className,
      this.libraryName, this.scriptUrl);
}

class _Mirrors {
  static Future<IsolateMirror> isolateMirrorOf(SendPort port) {
    Completer<IsolateMirror> completer = new Completer<IsolateMirror>();
    String request = '{ "command": "isolateMirrorOf" }';
    ReceivePort rp = new ReceivePort();
    if (!send(port, request, rp.toSendPort())) {
      throw new Exception("Unable to send mirror request to port $port");
    }
    rp.receive((message, _) {
        rp.close();
        completer.complete(_Mirrors.processResponse(
            port, "isolateMirrorOf", message));
      });
    return completer.future;
  }

  static CallerMirror caller(int level) {
    if (level < 0) {
      throw new IllegalArgumentException("Level must be >= 0");
    }
    // when walking, the Dart stack looks like:
    // - (dart:mirrors) _Mirrors.nativeCaller
    // - (dart:mirrors) _Mirrors.caller
    // - (dart:mirrors) caller
    // - the calling method itself
    // that's why +4
    level += 4;
    var result = new Map<String, String>();
    nativeCaller(level, result);
    if (result["function"] == null) {
      return null;
    }
    return new _CallerMirrorImpl(result["function"], result["outer_function"],
        result["class"], result["library"], result["script"]);
  }

  static void nativeCaller(int level, Map<String, String> result)
      native "Mirrors_caller";

  static void processCommand(var message, SendPort replyTo) {
    Map response = new Map();
    if (message[0] == 'isolateMirrorOf') {
      _IsolateMirrorImpl.buildResponse(response);
    } else {
      response['ok'] = false;
    }
    replyTo.send(response);
  }

  static bool send(SendPort port, String request, SendPort replyTo)
      native "Mirrors_send";

  static processResponse(SendPort port, String command, String response)
      native "Mirrors_processResponse";
}
