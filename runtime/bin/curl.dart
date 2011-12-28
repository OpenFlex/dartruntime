// Copyright (c) 2011, Ladislav Thon. All rights reserved. Use of this source
// code is governed by a BSD-style license that can be found in the LICENSE file.

interface Curl default _Curl {
  /**
   * Creates a Curl object. It can be reused for as many operations as needed.
   * However, construction is cheap, so you can also create new Curl object
   * for each operation.
   */
  Curl();

  /**
   * Downloads a file from [url] and stores it in a file specified by [path].
   * If [allowRedirects] is set, then [maxRedirects] controls the maximum
   * redirections performed until an error is signaled. A value less than 0
   * means infinite number of redirections. Default values are
   * [:allowRedirects = true:] and  [:maxRedirects = -1:].
   *
   * Returns whether download was really started. As a sanity check,
   * the [path] is checked and if it denotes an existing file, download
   * isn't started at all.
   */
  bool download(String url, String path, [bool allowRedirects, int maxRedirects]);

  /**
   * Sets the handler that is called periodically while the operation
   * is in progress. The [percent] parameter is an estimate of the progress;
   * no precision is guaranteed. It is always >= 0 and <= 100.
   */
  void set progressHandler(void progressHandler(int percent));

  /**
   * Set the handler that is called when the operation is finished.
   * The handler is called with an indication of whether or not it completed.
   */
  void set doneHandler(void doneHandler(bool completed));

  /**
   * Sets the handler that is called if there is an error.
   */
  void set errorHandler(void errorHandler(String error));
}

