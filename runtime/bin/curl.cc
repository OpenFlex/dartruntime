// Copyright (c) 2011, Ladislav Thon. All rights reserved. Use of this source
// code is governed by a BSD-style license that can be found in the LICENSE file.

#include <stdio.h>
#include <math.h>
#include <curl/curl.h>

#include "bin/dartutils.h"

#include "include/dart_api.h"

static int on_download_progress(void *data, double dltotal, double dlnow, double ultotal, double ulnow) {
  int progress = (dlnow / dltotal * 100);
  int done = isnormal(progress) ? round(progress) : 0;
  if (done < 0) done = 0;
  if (done > 100) done = 100;

  Dart_Port *progress_port = (Dart_Port *) data;
  if (progress_port != 0) {
    Dart_Post(*progress_port, Dart_NewInteger(done));
  }
  return 0;
}

void FUNCTION_NAME(Curl_Download)(Dart_NativeArguments args) {
  Dart_EnterScope();

  const char *from = DartUtils::GetStringValue(Dart_GetNativeArgument(args, 0));
  const char *to = DartUtils::GetStringValue(Dart_GetNativeArgument(args, 1));
  bool allow_redirects = DartUtils::GetBooleanValue(Dart_GetNativeArgument(args, 2));
  int max_redirects = DartUtils::GetIntegerValue(Dart_GetNativeArgument(args, 3));
  Dart_Port progress_port = DartUtils::GetPortValue(Dart_GetNativeArgument(args, 4));
  Dart_Port done_port = DartUtils::GetPortValue(Dart_GetNativeArgument(args, 5));
  Dart_Port error_port = DartUtils::GetPortValue(Dart_GetNativeArgument(args, 6));

  FILE *f;

  CURL *curl = curl_easy_init();
  CURLcode res;
  bool ok = false;

  if (curl) {
    f = fopen(to, "wb");

    curl_easy_setopt(curl, CURLOPT_URL, from);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, allow_redirects ? 1 : 0);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, max_redirects < 0 ? -1 : max_redirects);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, f);
    if (progress_port != 0) {
      curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0);
      curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, on_download_progress);
      curl_easy_setopt(curl, CURLOPT_PROGRESSDATA, &progress_port);
    }
    res = curl_easy_perform(curl);

    if (res == CURLE_OK) {
      ok = true;
    }

    if (res != CURLE_OK && error_port != 0) {
      Dart_Handle error = Dart_NewString(curl_easy_strerror(res));
      Dart_Post(error_port, error);
    }

    curl_easy_cleanup(curl);
    fclose(f);
  }

  if (done_port != 0) {
    Dart_Post(done_port, Dart_NewBoolean(ok));
  }

  Dart_ExitScope();
}

