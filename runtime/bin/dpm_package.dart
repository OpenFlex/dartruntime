// Copyright (c) 2012, Ladislav Thon. All rights reserved. Use of this source
// code is governed by a BSD-style license that can be found in the LICENSE file.

final RegExp _id = const RegExp(@'^[a-zA-Z0-9_][a-zA-Z0-9\-\.]*$');

class PackageException extends DpmException {
  PackageException(String message) : super(message);
}

class PackageCoordinates implements Hashable {
  final String organization;
  final String name;
  final VersionSpecification version;

  PackageCoordinates._new(String this.organization, String this.name, VersionSpecification this.version);

  factory PackageCoordinates.parse(String pkg) {
    final parts = pkg.split(':');

    String name;
    String organization;
    VersionSpecification version;

    if (parts.length == 1) {
      name = parts[0];
    } else if (parts.length == 2) {
      if (_VersionSpecificationParser.matches(parts[1])) {
        name = parts[0];
        version = new VersionSpecification(parts[1]);
      } else {
        organization = parts[0];
        name = parts[1];
      }
    } else if (parts.length == 3) {
      organization = parts[0];
      name = parts[1];
      version = new VersionSpecification(parts[2]);
    } else {
      throw new PackageException("Package coordinates '$pkg' has bad format (it must have 1, 2 or 3 parts)");
    }

    if (organization != null && !_id.hasMatch(organization)) {
      throw new PackageException("Package organization is malformed in '$pkg'");
    }
    if (name == null) {
      throw new PackageException("Package name is missing in '$pkg'");
    }
    if (!_id.hasMatch(name)) {
      throw new PackageException("Package name is malformed in '$pkg'");
    }

    return new PackageCoordinates._new(organization, name, version);
  }

  bool operator ==(other) => other is PackageCoordinates
      && organization == other.organization
      && name == other.name
      && version == other.version;

  int hashCode() {
    int result = 17;
    result = 31 * result + (organization != null ? organization.hashCode() : 0);
    result = 31 * result + (name != null ? name.hashCode() : 0);
    result = 31 * result + (version != null ? version.hashCode() : 0);
    return result;
  }

  String toString() {
    String organizationStr = organization != null ? organization : '*';
    String versionStr = version != null ? '$version' : '*';
    return '$organizationStr:$name:$versionStr';
  }
}

class PackageId implements Hashable {
  final String organization;
  final String name;
  final Version version;

  PackageId(String this.organization, String this.name, Version this.version) {
    if (organization == null) {
      throw new PackageException("Organization must not be null");
    }
    if (name == null) {
      throw new PackageException("Name must not be null");
    }
    if (version == null) {
      throw new PackageException("Version must not be null");
    }
  }

  bool operator ==(other) => other is PackageId
      && organization == other.organization
      && name == other.name
      && version == other.version;

  int hashCode() {
    int result = 17;
    result = 31 * result + (organization != null ? organization.hashCode() : 0);
    result = 31 * result + (name != null ? name.hashCode() : 0);
    result = 31 * result + (version != null ? version.hashCode() : 0);
    return result;
  }

  String toString() => '$organization:$name:$version';
}

class _PackageDescriptor {
  final Map<String, String> content;

  _PackageDescriptor(Map<String, String> this.content);

  factory _PackageDescriptor.parse(String descriptor) {
    Map<String, String> content = new Map<String, String>();
    List<String> lines = descriptor.split("\n");
    for (String line in lines) {
      int commentIdx = line.indexOf("#");
      if (commentIdx >= 0) {
        line = line.substring(0, commentIdx);
      }

      if (line.trim().length == 0) {
        continue;
      }

      int delimiterIdx = line.indexOf(":");
      if (delimiterIdx == -1) {
        throw new PackageException("Line in descriptor '$line' has bad format");
      }

      String key = line.substring(0, delimiterIdx).trim();
      String value = line.substring(delimiterIdx + 1).trim();
      content[key.toLowerCase()] = value;
    }
    return new _PackageDescriptor(content);
  }

  String mandatory(String variable) {
    String value = content[variable.toLowerCase()];
    if (value == null) {
      throw new PackageException("Variable '$variable' is mandatory in package descriptor");
    }
    return value;
  }

  String optional(String variable, [String defaultValue = null]) {
    String value = content[variable.toLowerCase()];
    return value != null ? value : defaultValue;
  }
}

class Package implements Hashable {
  final PackageId id;
  final String mainScript;
  final List<PackageCoordinates> dependencies;
  final String author;
  final String www;
  final String license;
  final String description;
  final List<String> binaries;

  Package._new(PackageId this.id, String this.mainScript,
               List<PackageCoordinates> this.dependencies,
               String this.author, String this.www, String this.license,
               String this.description, List<String> this.binaries);

  factory Package.fromDescriptor(String descriptorStr) {
    _PackageDescriptor descriptor = new _PackageDescriptor.parse(descriptorStr);

    String organization = descriptor.mandatory("Organization");
    String name = descriptor.mandatory("Name");;
    String versionStr = descriptor.mandatory("Version");
    PackageId id = new PackageId(organization, name, new Version(versionStr));

    String mainScript = descriptor.optional("Main-Script");
    String dependenciesStr = descriptor.optional("Dependencies", "");

    String author = descriptor.optional("Author");
    String www = descriptor.optional("WWW");
    String license = descriptor.optional("License");
    String description = descriptor.optional("Description");
    String binariesStr = descriptor.optional("Binaries", "");

    List<PackageCoordinates> dependencies = new List<PackageCoordinates>();
    List<String> dependenciesParts = dependenciesStr.split(",");
    for (String dependencyStr in dependenciesParts) {
      if (dependencyStr.trim().length == 0) {
        continue;
      }

      dependencies.add(new PackageCoordinates.parse(dependencyStr.trim()));
    }

    List<String> binaries = new List<String>();
    List<String> binariesParts = binariesStr.split(",");
    for (String binaryStr in binariesParts) {
      binaryStr = binaryStr.trim();
      if (binaryStr.length == 0) {
        continue;
      }

      binaries.add(binaryStr.trim());
    }

    return new Package._new(id, mainScript, dependencies, author, www,
        license, description, binaries);
  }

  factory Package.fromDescriptorFile(File descriptorFile) {
    FileInputStream input = descriptorFile.openInputStream();
    StringInputStream strInput = new StringInputStream(input, "UTF-8");
    String descriptor = strInput.readSync();
    input.close();
    return new Package.fromDescriptor(descriptor);
  }

  int hashCode() {
    int result = 17;
    result = 31 * result + id.hashCode();
    result = 31 * result + (mainScript != null ? mainScript.hashCode() : 0);
    return result;
  }

  String get organization() => id.organization;
  String get name() => id.name;
  Version get version() => id.version;

  String toString() => 'package $organization:$name:$version';
}

