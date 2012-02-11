// Copyright (c) 2012, Ladislav Thon. All rights reserved. Use of this source
// code is governed by a BSD-style license that can be found in the LICENSE file.

class RepositoryException extends DpmException {
  RepositoryException(String message) : super(message);
}

interface Repository {
  List<PackageId> find(PackageCoordinates coordinates);
  Package readPackage(PackageId id);
  String toUrl(Package package, [String script]);
}

class FilesystemRepository implements Repository {
  final Directory root;

  FilesystemRepository(Directory dpmRoot) : root = dpmRoot.subdirectory(['packages']);

  List<PackageId> find(PackageCoordinates coordinates) {
    if (coordinates.organization != null && coordinates.version != null) {
      return findByOrganizationAndNameAndVersion(coordinates.organization, coordinates.name, coordinates.version);
    } else if (coordinates.organization != null && coordinates.version == null) {
      return findByOrganizationAndName(coordinates.organization, coordinates.name);
    } else if (coordinates.organization == null && coordinates.version != null) {
      return findByNameAndVersion(coordinates.name, coordinates.version);
    } else if (coordinates.organization == null && coordinates.version == null) {
      return findByName(coordinates.name);
    } else {
      throw new RepositoryException("Invalid coordinates '$coordinates'");
    }
  }

  List<PackageId> findByName(String name) {
    List<PackageId> result = <PackageId>[];

    Directory packagesDir = new Directory(root.path);
    packagesDir.dirHandler = (dir) => result.addAll(findByOrganizationAndName(dir, name));
    packagesDir.listSync(fullPaths: false);
    return result;
  }

  List<PackageId> findByOrganizationAndName(String organization, String name) {
    List<PackageId> result = <PackageId>[];

    Directory packageDir = root.subdirectory([organization, name]);
    if (!packageDir.existsSync()) {
      return result;
    }

    packageDir.dirHandler = (dir) => result.add(new PackageId(organization, name, new Version(dir)));
    packageDir.listSync(fullPaths: false);
    return result;
  }

  List<PackageId> findByOrganizationAndNameAndVersion(String organization, String name, VersionSpecification version) {
    List<PackageId> allVersions = findByOrganizationAndName(organization, name);
    return allVersions.filter( (p) => version.isSatisfiedBy(p.version) );
  }

  List<PackageId> findByNameAndVersion(String name, VersionSpecification version) {
    List<PackageId> allVersions = findByName(name);
    return allVersions.filter( (p) => version.isSatisfiedBy(p.version) );
  }

  Package readPackage(PackageId id) {
    Directory packageDir = root.subdirectory([id.organization, id.name, "${id.version}"]);
    if (!packageDir.existsSync()) {
      throw new RepositoryException("Package '$id' doesn't exist in '${root.path}'");
    }

    File packageDescriptorFile = packageDir.file("info.dpm");
    if (!packageDescriptorFile.existsSync()) {
      throw new RepositoryException("Package '$id' in '${root.path}' doesn't contain a descriptor (info.dpm)");
    }

    return new Package.fromDescriptorFile(packageDescriptorFile);
  }

  String toUrl(Package package, [String script]) {
    Directory packageDir = root.subdirectory([package.organization, package.name, "${package.version}"]);
    if (script != null) {
      return packageDir.file(script).name;
    } else if (package.mainScript != null) {
      return packageDir.file(package.mainScript).name;
    } else {
      throw new RepositoryException("$package doesn't specify Main-Script and no script was defined");
    }
  }
}

class _LocalUserRepository extends FilesystemRepository {
  _LocalUserRepository() : super(new Directory.home().subdirectory([".dpm"]));
}

