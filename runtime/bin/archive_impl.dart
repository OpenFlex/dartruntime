// Copyright (c) 2011, Ladislav Thon. All rights reserved. Use of this source
// code is governed by a BSD-style license that can be found in the LICENSE file.

class _ArchiveElement {
  File file;
  String archivePath;

  _ArchiveElement(File this.file, String this.archivePath);
}

class _CreateArchive extends NativeFieldWrapperClass2 implements CreateArchive {
  List<_ArchiveElement> elements;

  _CreateArchive() : elements = new List<_ArchiveElement>();

  void addFile(File file, String path) {
    elements.add(new _ArchiveElement(file, path));
  }

  bool build(File archive) {
    // there is an inherent race in next two checks, it is for sanity only
    if (archive.existsSync()) {
      return false;
    }

    for (final e in elements) {
      if (!e.file.existsSync()) {
        return false;
      }
    }

    _initArchive(archive.name);
    for (final e in elements) {
      _addArchiveEntry(e.file.fullPathSync(), e.archivePath);
    }
    _finishArchive();
    return true;
  }

  void _initArchive(String path) native "ArchiveCreate_Init";
  void _addArchiveEntry(String filePath, String entryPath) native "ArchiveCreate_AddEntry";
  void _finishArchive() native "ArchiveCreate_Finish";
}

class _ExtractArchive implements ExtractArchive {
  File archive;

  _ExtractArchive(File this.archive);

  bool extractTo(Directory directory) {
    // there is an inherent race in this check, it is for sanity only
    if (!archive.existsSync()) {
      return false;
    }

    _extractArchive(archive.fullPathSync(), directory.path);
    return true;
  }

  bool extractEntry(String pathWithinArchive, Directory directory) {
    if (!archive.existsSync()) {
      return false;
    }

    return _extractEntryOfArchive(archive.fullPathSync(), pathWithinArchive, directory.path);
  }

  bool findEntry(String pathWithinArchive) {
    if (!archive.existsSync()) {
      return false;
    }

    return _findEntryInArchive(archive.fullPathSync(), pathWithinArchive);
  }

  List<String> listEntries() {
    if (!archive.existsSync()) {
      return null;
    }

    List<String> result = [];
    _listEntriesInArchive(archive.fullPathSync(), result);
    return result;
  }

  static void _extractArchive(String archivePath, String outDirectoryPath)
      native "ArchiveExtract_ExtractAll";
  static bool _extractEntryOfArchive(String archivePath, String entryPath,
      String outDirectoryPath) native "ArchiveExtract_ExtractOne";
  static bool _findEntryInArchive(String archivePath, String entryPath)
      native "ArchiveExtract_FindEntry";
  static void _listEntriesInArchive(String archivePath, List<String> result)
      native "ArchiveExtract_ListEntries";
}

