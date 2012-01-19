// Copyright (c) 2011, Ladislav Thon. All rights reserved. Use of this source
// code is governed by a BSD-style license that can be found in the LICENSE file.

#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <archive.h>
#include <archive_entry.h>

#include "bin/dartutils.h"

#include "include/dart_api.h"

static const int buffer_size = 16384;

static const int kArchiveFieldIndex = 0;
static const int kArchiveEntryFieldIndex = 1;

static struct archive* GetArchive(Dart_Handle obj) {
  intptr_t value = 0;
  Dart_Handle result = Dart_GetNativeInstanceField(obj, kArchiveFieldIndex, &value);
  ASSERT(!Dart_IsError(result));
  struct archive *a = reinterpret_cast<struct archive *>(value);
  ASSERT(a != NULL);
  return a;
}

static void SetArchive(Dart_Handle obj, struct archive *a) {
  Dart_SetNativeInstanceField(obj, kArchiveFieldIndex, reinterpret_cast<intptr_t>(a));
}

static struct archive_entry* GetArchiveEntry(Dart_Handle obj) {
  intptr_t value = 0;
  Dart_Handle result = Dart_GetNativeInstanceField(obj, kArchiveEntryFieldIndex, &value);
  ASSERT(!Dart_IsError(result));
  struct archive_entry *ae = reinterpret_cast<struct archive_entry *>(value);
  ASSERT(ae != NULL);
  return ae;
}

static void SetArchiveEntry(Dart_Handle obj, struct archive_entry *ae) {
  Dart_SetNativeInstanceField(obj, kArchiveEntryFieldIndex, reinterpret_cast<intptr_t>(ae));
}

// ---

void FUNCTION_NAME(ArchiveCreate_Init)(Dart_NativeArguments args) {
  Dart_EnterScope();

  Dart_Handle obj = Dart_GetNativeArgument(args, 0);
  const char *path = DartUtils::GetStringValue(Dart_GetNativeArgument(args, 1));

  struct archive *a = archive_write_new();
  archive_write_set_compression_gzip(a);
  archive_write_set_format_pax_restricted(a);
  archive_write_open_filename(a, path);
  struct archive_entry *ae = archive_entry_new();

  SetArchive(obj, a);
  SetArchiveEntry(obj, ae);

  Dart_ExitScope();
}

void FUNCTION_NAME(ArchiveCreate_AddEntry)(Dart_NativeArguments args) {
  Dart_EnterScope();

  Dart_Handle obj = Dart_GetNativeArgument(args, 0);
  const char *file_path = DartUtils::GetStringValue(Dart_GetNativeArgument(args, 1));
  const char *entry_path = DartUtils::GetStringValue(Dart_GetNativeArgument(args, 2));

  struct archive *a = GetArchive(obj);
  struct archive_entry *ae = GetArchiveEntry(obj);

  int fd = open(file_path, O_RDONLY);

  struct stat file_stat;
  fstat(fd, &file_stat);

  archive_entry_set_pathname(ae, entry_path);
  archive_entry_set_size(ae, file_stat.st_size);
  archive_entry_set_filetype(ae, AE_IFREG);
  archive_entry_set_mode(ae, file_stat.st_mode);
  archive_write_header(a, ae);

  char buf[buffer_size];
  int len = read(fd, buf, sizeof(buf));
  while (len > 0) {
    archive_write_data(a, buf, len);
    len = read(fd, buf, sizeof(buf));
  }
  close(fd);
  archive_entry_clear(ae);

  Dart_ExitScope();
}

void FUNCTION_NAME(ArchiveCreate_Finish)(Dart_NativeArguments args) {
  Dart_EnterScope();

  Dart_Handle obj = Dart_GetNativeArgument(args, 0);

  struct archive *a = GetArchive(obj);
  struct archive_entry *ae = GetArchiveEntry(obj);

  archive_entry_free(ae);
  archive_write_close(a);
  archive_write_finish(a);

  SetArchive(obj, NULL);
  SetArchiveEntry(obj, NULL);

  Dart_ExitScope();
}

// ---

void FUNCTION_NAME(ArchiveExtract_ExtractAll)(Dart_NativeArguments args) {
  Dart_EnterScope();

  const char *archive_path = DartUtils::GetStringValue(Dart_GetNativeArgument(args, 0));
  const char *out_directory_path = DartUtils::GetStringValue(Dart_GetNativeArgument(args, 1));

  struct archive *r = archive_read_new();
  archive_read_support_compression_all(r);
  archive_read_support_format_all(r);
  archive_read_open_filename(r, archive_path, buffer_size);

  int extract_flags = ARCHIVE_EXTRACT_SECURE_SYMLINKS | ARCHIVE_EXTRACT_SECURE_NODOTDOT;
  struct archive *w = archive_write_disk_new();
  archive_write_disk_set_options(w, extract_flags);

  struct archive_entry *e;

  while (archive_read_next_header(r, &e) == ARCHIVE_OK) {
    const char *entry_path = archive_entry_pathname(e);
    char *full_path = (char *) malloc((strlen(out_directory_path) + 1 + strlen(entry_path) + 1) * sizeof(char));
    strcpy(full_path, out_directory_path);
    strcat(full_path, "/");
    strcat(full_path, entry_path);
    archive_entry_set_pathname(e, full_path);

    archive_write_header(w, e);

    if (archive_entry_size(e) > 0) {
      char buf[buffer_size];
      size_t size;
      while ((size = archive_read_data(r, &buf, buffer_size)) > 0) {
        archive_write_data(w, &buf, size);
      }
    }

    archive_write_finish_entry(w);

    free(full_path);
  }

  archive_read_close(r);
  archive_read_finish(r);
  archive_write_close(w);
  archive_write_finish(w);

  Dart_ExitScope();
}

void FUNCTION_NAME(ArchiveExtract_ExtractOne)(Dart_NativeArguments args) {
  Dart_EnterScope();

  const char *archive_path = DartUtils::GetStringValue(Dart_GetNativeArgument(args, 0));
  const char *desired_entry_path = DartUtils::GetStringValue(Dart_GetNativeArgument(args, 1));
  const char *out_directory_path = DartUtils::GetStringValue(Dart_GetNativeArgument(args, 2));

  bool found = false;

  struct archive *r = archive_read_new();
  archive_read_support_compression_all(r);
  archive_read_support_format_all(r);
  archive_read_open_filename(r, archive_path, buffer_size);

  int extract_flags = ARCHIVE_EXTRACT_SECURE_SYMLINKS | ARCHIVE_EXTRACT_SECURE_NODOTDOT;
  struct archive *w = archive_write_disk_new();
  archive_write_disk_set_options(w, extract_flags);

  struct archive_entry *e;

  while (archive_read_next_header(r, &e) == ARCHIVE_OK) {
    const char *entry_path = archive_entry_pathname(e);
    if (strcmp(entry_path, desired_entry_path) != 0) {
      archive_read_data_skip(r);
      continue;
    }

    found = true;

    char *full_path = (char *) malloc((strlen(out_directory_path) + 1 + strlen(entry_path) + 1) * sizeof(char));
    strcpy(full_path, out_directory_path);
    strcat(full_path, "/");
    strcat(full_path, entry_path);
    archive_entry_set_pathname(e, full_path);

    archive_write_header(w, e);

    if (archive_entry_size(e) > 0) {
      char buf[buffer_size];
      size_t size;
      while ((size = archive_read_data(r, &buf, buffer_size)) > 0) {
        archive_write_data(w, &buf, size);
      }
    }

    archive_write_finish_entry(w);

    free(full_path);

    break;
  }

  archive_read_close(r);
  archive_read_finish(r);
  archive_write_close(w);
  archive_write_finish(w);

  Dart_SetReturnValue(args, Dart_NewBoolean(found));

  Dart_ExitScope();
}

void FUNCTION_NAME(ArchiveExtract_FindEntry)(Dart_NativeArguments args) {
  Dart_EnterScope();

  const char *archive_path = DartUtils::GetStringValue(Dart_GetNativeArgument(args, 0));
  const char *desired_entry_path = DartUtils::GetStringValue(Dart_GetNativeArgument(args, 1));

  bool found = false;

  struct archive *r = archive_read_new();
  archive_read_support_compression_all(r);
  archive_read_support_format_all(r);
  archive_read_open_filename(r, archive_path, buffer_size);

  struct archive_entry *e;

  while (archive_read_next_header(r, &e) == ARCHIVE_OK) {
    const char *entry_path = archive_entry_pathname(e);
    if (strcmp(entry_path, desired_entry_path) != 0) {
      archive_read_data_skip(r);
      continue;
    }

    found = true;
    break;
  }

  archive_read_close(r);
  archive_read_finish(r);

  Dart_SetReturnValue(args, Dart_NewBoolean(found));

  Dart_ExitScope();
}

