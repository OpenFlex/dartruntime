// Copyright (c) 2011, Ladislav Thon. All rights reserved. Use of this source
// code is governed by a BSD-style license that can be found in the LICENSE file.

interface CreateArchive default _CreateArchive {
  /**
   * Creates an object that can be used for creating a .tar.gz archive.
   * Call [addFile] as many times as needed, than call [build] once.
   */
  CreateArchive();

  /**
   * Records a [file] to be added to the archive. Within the archive, it will
   * be located at [path] (relative to the root of the archive).
   */
  void addFile(File file, String path);

  /**
   * Creates the final archive located at the path denoted by [archive]
   * and returns whether it was successful.
   *
   * As a sanity check, the [archive] path is checked and if it denotes
   * an existing file, the operation isn't even started. Also, all the
   * [File]s passed to [addFile] are checked and if one of them doesn't
   * exist, the operation isn't started.
   */
  bool build(File archive);
}

interface ExtractArchive default _ExtractArchive {
  /**
   * Creates an object for extracting an archive. All formats and compressions
   * supported by libarchive are supported.
   *
   * The [archive] file must exist. If it doesn't, the [extractTo] method
   * will not succeed.
   */
  ExtractArchive(File archive);

  /**
   * Extract this archive to specified [directory] and returns whether it was
   * successful. If the [directory] doesn't exist, it will be created.
   */
  bool extractTo(Directory directory);

  /**
   * Extract a single entry of this archive denoted by [pathWithinArchive]
   * to specified [directory] and returns whether it was successful.
   * If no such entry exists within the archive, returns [:false:].
   * If the [directory] doesn't exist, it will be created.
   */
  bool extractEntry(String pathWithinArchive, Directory directory);

  /**
   * Returns whether this archive contains an entry with specified path.
   * If the archive doesn't exist, returns [:false:].
   */
  bool findEntry(String pathWithinArchive);

  /**
   * Returns a [List] of paths of all entries in this archive
   * in no particular order. If the archive doesn't exist, returns [:null:].
   */
  List<String> listEntries();
}

