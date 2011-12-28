// Copyright (c) 2011, Ladislav Thon. All rights reserved. Use of this source
// code is governed by a BSD-style license that can be found in the LICENSE file.

class _CurlDownloadIsolate extends Isolate {
  _CurlDownloadIsolate() : super.heavy();

  void main() {
    port.receive( (msg, replyTo) {
      _download(msg["from"],
                msg["to"],
                msg["allowRedirects"],
                msg["maxRedirects"],
                msg["progressPort"],
                msg["donePort"],
                msg["errorPort"]);
      replyTo.send(null);
      port.close();
    });
  }

  static void _download(String from,
                        String to,
                        bool allowRedirects,
                        int maxRedirects,
                        SendPort progressPort,
                        SendPort donePort,
                        SendPort errorPort) native "Curl_Download";
}

class _Curl implements Curl {
  var _progressHandler;
  var _doneHandler;
  var _errorHandler;

  _Curl();

  void download(String url, String path, [bool allowRedirects = true, int maxRedirects = -1]) {
    if (new File(path).existsSync()) {
      // there is an inherent race in this, so it is a sanity check only
      return false;
    }

    new _CurlDownloadIsolate().spawn().then( (port) {
      Map params = new Map();
      params["from"] = url;
      params["to"] = path;
      params["allowRedirects"] = allowRedirects;
      params["maxRedirects"] = maxRedirects;

      ReceivePort progressPort;
      ReceivePort donePort;
      ReceivePort errorPort;

      if (_progressHandler !== null) {
        progressPort = new ReceivePort();
        progressPort.receive( (int percent, ignored) {
          _progressHandler(percent);
        });
        params["progressPort"] = progressPort.toSendPort();
      }
      if (_doneHandler !== null) {
        donePort = new ReceivePort.singleShot();
        donePort.receive( (bool completed, ignored) {
          _doneHandler(completed);
        });
        params["donePort"] = donePort.toSendPort();
      }
      if (_errorHandler !== null) {
        errorPort = new ReceivePort.singleShot();
        errorPort.receive( (String error, ignored) {
          _errorHandler(error);
        });
        params["errorPort"] = errorPort.toSendPort();
      }

      // Close ports when listing is done.
      ReceivePort finishedPort = new ReceivePort.singleShot();
      finishedPort.receive( (msg, ignored) {
        _closePort(progressPort);
        _closePort(donePort);
        _closePort(errorPort);
      });

      port.send(params, finishedPort.toSendPort());
    });
    return true;
  }

  void set progressHandler(void progressHandler(int percent)) {
    _progressHandler = progressHandler;
  }

  void set doneHandler(void doneHandler(bool completed)) {
    _doneHandler = doneHandler;
  }

  void set errorHandler(void errorHandler(String error)) {
    _errorHandler = errorHandler;
  }

  void _closePort(ReceivePort port) {
    if (port !== null) {
      port.close();
    }
  }
}

