// Copyright (c) 2012, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "bin/directory.h"

#include <dirent.h>
#include <errno.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>

#include "bin/file.h"
#include "bin/platform.h"


static char* SafeStrNCpy(char* dest, const char* src, size_t n) {
  strncpy(dest, src, n);
  dest[n - 1] = '\0';
  return dest;
}


static void SetOsErrorMessage(char* os_error_message,
                              int os_error_message_len) {
  SafeStrNCpy(os_error_message, strerror(errno), os_error_message_len);
}


// Forward declarations.
static bool ListRecursively(const char* dir_name,
                            bool recursive,
                            DirectoryListing* listing);
static bool DeleteRecursively(const char* dir_name);


static bool ComputeFullPath(const char* dir_name,
                            char* path,
                            int* path_length) {
  char* abs_path;
  do {
    abs_path = realpath(dir_name, path);
  } while (abs_path == NULL && errno == EINTR);
  if (abs_path == NULL) {
    return false;
  }
  *path_length = strlen(path);
  size_t written = snprintf(path + *path_length,
                            PATH_MAX - *path_length,
                            "%s",
                            File::PathSeparator());
  if (written != strlen(File::PathSeparator())) {
    return false;
  }
  *path_length += written;
  return true;
}


static bool HandleDir(char* dir_name,
                      char* path,
                      int path_length,
                      bool recursive,
                      DirectoryListing *listing) {
  if (strcmp(dir_name, ".") != 0 &&
      strcmp(dir_name, "..") != 0) {
    size_t written = snprintf(path + path_length,
                              PATH_MAX - path_length,
                              "%s",
                              dir_name);
    if (written != strlen(dir_name)) {
      return false;
    }
    bool ok = listing->HandleDirectory(dir_name);
    if (!ok) return ok;
    if (recursive) {
      return ListRecursively(path, recursive, listing);
    }
  }
  return true;
}


static bool HandleFile(char* file_name,
                       char* path,
                       int path_length,
                       DirectoryListing *listing) {
  // TODO(sgjesse): Pass flags to indicate whether file responses are
  // needed.
  size_t written = snprintf(path + path_length,
                            PATH_MAX - path_length,
                            "%s",
                            file_name);
  if (written != strlen(file_name)) {
    return false;
  }
  return listing->HandleFile(path);
}


static void PostError(DirectoryListing *listing,
                      const char* prefix,
                      const char* suffix,
                      int error_code) {
  // TODO(sgjesse): Pass flags to indicate whether error response is
  // needed.
  char* error_str = Platform::StrError(error_code);
  int error_message_size =
      strlen(prefix) + strlen(suffix) + strlen(error_str) + 3;
  char* message = static_cast<char*>(malloc(error_message_size + 1));
  int written = snprintf(message,
                         error_message_size + 1,
                         "%s%s (%s)",
                         prefix,
                         suffix,
                         error_str);
  ASSERT(written == error_message_size);
  free(error_str);
  listing->HandleError(message);
  free(message);
}


static bool ListRecursively(const char* dir_name,
                            bool recursive,
                            DirectoryListing *listing) {
  DIR* dir_pointer;
  do {
    dir_pointer = opendir(dir_name);
  } while (dir_pointer == NULL && errno == EINTR);
  if (dir_pointer == NULL) {
    PostError(listing, "Directory listing failed for: ", dir_name, errno);
    return false;
  }

  // Compute full path for the directory currently being listed.  The
  // path buffer will be used to construct the current path in the
  // recursive traversal. path_length does not always equal
  // strlen(path) but indicates the current prefix of path that is the
  // path of the current directory in the traversal.
  char *path = static_cast<char*>(malloc(PATH_MAX));
  ASSERT(path != NULL);
  int path_length = 0;
  bool valid = ComputeFullPath(dir_name, path, &path_length);
  if (!valid) {
    free(path);
    PostError(listing, "Directory listing failed for: ", dir_name, errno);
    return false;
  }

  // Iterated the directory and post the directories and files to the
  // ports.
  int read = 0;
  bool success = true;
  dirent entry;
  dirent* result;
  while ((read = TEMP_FAILURE_RETRY(readdir_r(dir_pointer,
                                              &entry,
                                              &result))) == 0 &&
         result != NULL &&
         success) {
    switch (entry.d_type) {
      case DT_DIR:
        success = success && HandleDir(entry.d_name,
                                       path,
                                       path_length,
                                       recursive,
                                       listing);
        break;
      case DT_REG:
        success = success && HandleFile(entry.d_name,
                                        path,
                                        path_length,
                                        listing);
        break;
      case DT_UNKNOWN: {
        // On some file systems the entry type is not determined by
        // readdir_r. For those we use lstat to determine the entry
        // type.
        struct stat entry_info;
        size_t written = snprintf(path + path_length,
                                  PATH_MAX - path_length,
                                  "%s",
                                  entry.d_name);
        if (written != strlen(entry.d_name)) {
          success = false;
          break;
        }
        int lstat_success = TEMP_FAILURE_RETRY(lstat(path, &entry_info));
        if (lstat_success == -1) {
          success = false;
          PostError(listing, "Directory listing failed for: ", path, errno);
          break;
        }
        if ((entry_info.st_mode & S_IFMT) == S_IFDIR) {
          success = success && HandleDir(entry.d_name,
                                         path,
                                         path_length,
                                         recursive,
                                         listing);
        } else if ((entry_info.st_mode & S_IFMT) == S_IFREG) {
          success = success && HandleFile(entry.d_name,
                                          path,
                                          path_length,
                                          listing);
        }
        break;
      }
      default:
        break;
    }
  }

  if (read != 0) {
    success = false;
    PostError(listing, "Directory listing failed", "", read);
  }

  if (closedir(dir_pointer) == -1) {
    PostError(listing, "Failed to close directory", "", errno);
  }
  free(path);

  return success;
}


static bool DeleteFile(char* file_name,
                       char* path,
                       int path_length) {
  size_t written = snprintf(path + path_length,
                            PATH_MAX - path_length,
                            "%s",
                            file_name);
  if (written != strlen(file_name)) {
    return false;
  }
  return (remove(path) == 0);
}


static bool DeleteDir(char* dir_name,
                      char* path,
                      int path_length) {
  if (strcmp(dir_name, ".") != 0 &&
      strcmp(dir_name, "..") != 0) {
    size_t written = snprintf(path + path_length,
                              PATH_MAX - path_length,
                              "%s",
                              dir_name);
    if (written != strlen(dir_name)) {
      return false;
    }
    return DeleteRecursively(path);
  }
  return true;
}


static bool DeleteRecursively(const char* dir_name) {
  DIR* dir_pointer;
  do {
    dir_pointer = opendir(dir_name);
  } while (dir_pointer == NULL && errno == EINTR);

  if (dir_pointer == NULL) {
    return false;
  }

  // Compute full path for the directory currently being deleted.  The
  // path buffer will be used to construct the current path in the
  // recursive traversal. path_length does not always equal
  // strlen(path) but indicates the current prefix of path that is the
  // path of the current directory in the traversal.
  char *path = static_cast<char*>(malloc(PATH_MAX));
  ASSERT(path != NULL);
  int path_length = 0;
  bool valid = ComputeFullPath(dir_name, path, &path_length);
  if (!valid) {
    free(path);
    return false;
  }

  // Iterate the directory and delete all files and directories.
  int read = 0;
  bool success = true;
  dirent entry;
  dirent* result;
  while ((read = TEMP_FAILURE_RETRY(readdir_r(dir_pointer,
                                              &entry,
                                              &result))) == 0 &&
         result != NULL &&
         success) {
    switch (entry.d_type) {
      case DT_DIR:
        success = success && DeleteDir(entry.d_name, path, path_length);
        break;
      case DT_REG:
        success = success && DeleteFile(entry.d_name, path, path_length);
        break;
      case DT_UNKNOWN: {
        // On some file systems the entry type is not determined by
        // readdir_r. For those we use lstat to determine the entry
        // type.
        struct stat entry_info;
        size_t written = snprintf(path + path_length,
                                  PATH_MAX - path_length,
                                  "%s",
                                  entry.d_name);
        if (written != strlen(entry.d_name)) {
          success = false;
          break;
        }
        int lstat_success = TEMP_FAILURE_RETRY(lstat(path, &entry_info));
        if (lstat_success == -1) {
          success = false;
          break;
        }
        if ((entry_info.st_mode & S_IFMT) == S_IFDIR) {
          success = success && DeleteDir(entry.d_name, path, path_length);
        } else if ((entry_info.st_mode & S_IFMT) == S_IFREG) {
          success = success && DeleteFile(entry.d_name, path, path_length);
        }
        break;
      }
      default:
        break;
    }
  }

  free(path);

  if ((read != 0) ||
      (closedir(dir_pointer) == -1) ||
      (remove(dir_name) == -1)) {
    return false;
  }

  return success;
}


// Forward declaration.
static bool ListRecursivelySync(const char* dir_name,
                                bool recursive,
                                bool full_paths,
                                Dart_Handle dir_callback,
                                Dart_Handle file_callback,
                                Dart_Handle done_callback,
                                Dart_Handle error_callback);


static bool HandleDirSync(char* dir_name,
                          char* path,
                          int path_length,
                          bool recursive,
                          bool full_paths,
                          Dart_Handle dir_callback,
                          Dart_Handle file_callback,
                          Dart_Handle done_callback,
                          Dart_Handle error_callback) {
  if (strcmp(dir_name, ".") != 0 &&
      strcmp(dir_name, "..") != 0) {
    size_t written = snprintf(path + path_length,
                              PATH_MAX - path_length,
                              "%s",
                              dir_name);
    ASSERT(written == strlen(dir_name));
    if (Dart_IsClosure(dir_callback)) {
      Dart_Handle arguments[1];
      arguments[0] = Dart_NewString(full_paths ? path : dir_name);
      Dart_InvokeClosure(dir_callback, 1, arguments);
    }
    if (recursive) {
      return ListRecursivelySync(path,
                                 recursive,
                                 full_paths,
                                 dir_callback,
                                 file_callback,
                                 done_callback,
                                 error_callback);
    }
  }
  return true;
}


static void HandleFileSync(char* file_name,
                           char* path,
                           int path_length,
                           bool full_paths,
                           Dart_Handle file_callback) {
  if (Dart_IsClosure(file_callback)) {
    size_t written = snprintf(path + path_length,
                              PATH_MAX - path_length,
                              "%s",
                              file_name);
    ASSERT(written == strlen(file_name));
    Dart_Handle arguments[1];
    arguments[0] = Dart_NewString(full_paths ? path : file_name);
    Dart_InvokeClosure(file_callback, 1, arguments);
  }
}


static void PostErrorSync(Dart_Handle error_callback,
                          const char* prefix,
                          const char* suffix,
                          int error_code) {
  if (Dart_IsClosure(error_callback)) {
    char* error_str = Platform::StrError(error_code);
    int error_message_size =
        strlen(prefix) + strlen(suffix) + strlen(error_str) + 3;
    char* message = static_cast<char*>(malloc(error_message_size + 1));
    int written = snprintf(message,
                           error_message_size + 1,
                           "%s%s (%s)",
                           prefix,
                           suffix,
                           error_str);
    ASSERT(written == error_message_size);
    free(error_str);
    Dart_Handle arguments[1];
    arguments[0] = Dart_NewString(message);
    Dart_InvokeClosure(error_callback, 1, arguments);
    free(message);
  }
}


static bool ListRecursivelySync(const char* dir_name,
                                bool recursive,
                                bool full_paths,
                                Dart_Handle dir_callback,
                                Dart_Handle file_callback,
                                Dart_Handle done_callback,
                                Dart_Handle error_callback) {
  DIR* dir_pointer = opendir(dir_name);
  if (dir_pointer == NULL) {
    PostErrorSync(error_callback, "Directory listing failed for: ", dir_name, errno);
    return false;
  }

  // Compute full path for the directory currently being listed.
  char *path = static_cast<char*>(malloc(PATH_MAX));
  ASSERT(path != NULL);
  int path_length = 0;
  ComputeFullPath(dir_name, path, &path_length);

  // Iterated the directory and post the directories and files to the
  // callbacks.
  int success = 0;
  bool listing_error = false;
  dirent entry;
  dirent* result;
  while ((success = readdir_r(dir_pointer, &entry, &result)) == 0 &&
         result != NULL &&
         !listing_error) {
    switch (entry.d_type) {
      case DT_DIR:
        listing_error = listing_error || !HandleDirSync(entry.d_name,
                                                    path,
                                                    path_length,
                                                    recursive,
                                                    full_paths,
                                                    dir_callback,
                                                    file_callback,
                                                    done_callback,
                                                    error_callback);
        break;
      case DT_REG:
        HandleFileSync(entry.d_name, path, path_length, full_paths, file_callback);
        break;
      case DT_UNKNOWN: {
        // On some file systems the entry type is not determined by
        // readdir_r. For those we use lstat to determine the entry
        // type.
        struct stat entry_info;
        size_t written = snprintf(path + path_length,
                                  PATH_MAX - path_length,
                                  "%s",
                                  entry.d_name);
        ASSERT(written == strlen(entry.d_name));
        int lstat_success = lstat(path, &entry_info);
        if (lstat_success == -1) {
          listing_error = true;
          PostErrorSync(error_callback, "Directory listing failed for: ", path, errno);
          break;
        }
        if ((entry_info.st_mode & S_IFMT) == S_IFDIR) {
          listing_error = listing_error || !HandleDirSync(entry.d_name,
                                                          path,
                                                          path_length,
                                                          recursive,
                                                          full_paths,
                                                          dir_callback,
                                                          file_callback,
                                                          done_callback,
                                                          error_callback);
        } else if ((entry_info.st_mode & S_IFMT) == S_IFREG) {
          HandleFileSync(entry.d_name, path, path_length, full_paths, file_callback);
        }
        break;
      }
      default:
        break;
    }
  }

  if (success != 0) {
    listing_error = true;
    PostErrorSync(error_callback, "Directory listing failed", "", success);
  }

  if (closedir(dir_pointer) == -1) {
    PostErrorSync(error_callback, "Failed to close directory", "", errno);
  }
  free(path);

  return !listing_error;
}


bool Directory::List(const char* dir_name,
                     bool recursive,
                     DirectoryListing *listing) {
  bool completed = ListRecursively(dir_name, recursive, listing);
  return completed;
}


void Directory::ListSync(const char* dir_name,
                         bool recursive,
                         bool full_paths,
                         Dart_Handle dir_callback,
                         Dart_Handle file_callback,
                         Dart_Handle done_callback,
                         Dart_Handle error_callback) {
  bool completed = ListRecursivelySync(dir_name,
                                       recursive,
                                       full_paths,
                                       dir_callback,
                                       file_callback,
                                       done_callback,
                                       error_callback);
  if (Dart_IsClosure(done_callback)) {
    Dart_Handle arguments[1];
    arguments[0] = Dart_NewBoolean(completed);
    Dart_InvokeClosure(done_callback, 1, arguments);
  }
}


Directory::ExistsResult Directory::Exists(const char* dir_name) {
  struct stat entry_info;
  int lstat_success = TEMP_FAILURE_RETRY(lstat(dir_name, &entry_info));
  if (lstat_success == 0) {
    if ((entry_info.st_mode & S_IFMT) == S_IFDIR) {
      return EXISTS;
    } else {
      return DOES_NOT_EXIST;
    }
  } else {
    if (errno == EACCES ||
        errno == EBADF ||
        errno == EFAULT ||
        errno == ENOMEM ||
        errno == EOVERFLOW) {
      // Search permissions denied for one of the directories in the
      // path or a low level error occured. We do not know if the
      // directory exists.
      return UNKNOWN;
    }
    ASSERT(errno == ELOOP ||
           errno == ENAMETOOLONG ||
           errno == ENOENT ||
           errno == ENOTDIR);
    return DOES_NOT_EXIST;
  }
}


bool Directory::Create(const char* dir_name) {
  // Create the directory with the permissions specified by the
  // process umask.
  return (TEMP_FAILURE_RETRY(mkdir(dir_name, 0777)) == 0);
}


int Directory::CreateTemp(const char* const_template,
                          char** path,
                          char* os_error_message,
                          int os_error_message_len) {
  // Returns a new, unused directory name, modifying the contents of
  // dir_template.  Creates the directory with the permissions specified
  // by the process umask.
  // The return value must be freed by the caller.
  *path = static_cast<char*>(malloc(PATH_MAX + 1));
  SafeStrNCpy(*path, const_template, PATH_MAX + 1);
  int path_length = strlen(*path);
  if (path_length > 0) {
    if ((*path)[path_length - 1] == '/') {
      snprintf(*path + path_length, PATH_MAX - path_length, "temp_dir_XXXXXX");
    } else {
      snprintf(*path + path_length, PATH_MAX - path_length, "XXXXXX");
    }
  } else {
    snprintf(*path, PATH_MAX, "/tmp/temp_dir1_XXXXXX");
  }
  char* result;
  do {
    result = mkdtemp(*path);
  } while (result == NULL && errno == EINTR);
  if (result == NULL) {
    SetOsErrorMessage(os_error_message, os_error_message_len);
    free(*path);
    *path = NULL;
    return errno;
  }
  return 0;
}


bool Directory::Delete(const char* dir_name, bool recursive) {
  if (!recursive) {
    return (TEMP_FAILURE_RETRY(remove(dir_name)) == 0);
  } else {
    return DeleteRecursively(dir_name);
  }
}

const char* Directory::CurrentUserHome() {
  char* home = getenv("HOME");
  if (home == NULL) {
    home = getpwuid(getuid())->pw_dir;
  }
  return home;
}
