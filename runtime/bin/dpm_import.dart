// Copyright (c) 2012, Ladislav Thon. All rights reserved. Use of this source
// code is governed by a BSD-style license that can be found in the LICENSE file.

class ResolvingException extends DpmException {
  ResolvingException(String message) : super(message);
}

class ImportSpecification {
  final PackageCoordinates coordinates;
  final String script;

  ImportSpecification._new(PackageCoordinates this.coordinates, String this.script);

  factory ImportSpecification.parse(String import) {
    String coordinatesStr = import;
    String script;
    final scriptIdx = import.indexOf('/', 0);
    if (scriptIdx != -1) {
      coordinatesStr = import.substring(0, scriptIdx);
      script = import.substring(scriptIdx + 1, import.length);
    }

    PackageCoordinates coordinates = new PackageCoordinates.parse(coordinatesStr);

    return new ImportSpecification._new(coordinates, script);
  }

  String get organization() => coordinates.organization;
  String get name() => coordinates.name;
  VersionSpecification get version() => coordinates.version;

  String toString() => script != null ? '$coordinates/$script' : '$coordinates';
}

class Import implements Hashable {
  final String url;

  Import._new(String this.url);

  factory Import.resolve(ImportSpecification spec, Repository repo) {
    List<PackageId> candidates = repo.find(spec.coordinates);

    if (candidates.length == 0) {
      throw new ResolvingException("Couldn't resolve '$spec'");
    }

    if (spec.organization == null) {
      Set<String> organizations = new Set<String>();
      for (PackageId candidate in candidates) {
        organizations.add(candidate.organization);
      }
      if (organizations.length > 1) {
        String organizationsDesc = Strings.join(new List.from(organizations), ', ');
        throw new ResolvingException("Package '${spec.name}' is available from more organizations [$organizationsDesc], the organization must be specified explicitly");
      }
    }

    if (spec.version != null) {
      candidates = candidates.filter( (c) => spec.version.isSatisfiedBy(c.version) );
    }

    if (candidates.length == 0) {
      throw new ResolvingException("Couldn't resolve '$spec'");
    }

    candidates.sort( (a, b) => -a.version.compareTo(b.version) ); // highest first

    PackageId selected = candidates[0];
    Package package = repo.readPackage(selected);

    String url = repo.toUrl(package, spec.script);
    return new Import._new(url);
  }

  bool operator ==(other) => other is Import && url == other.url;

  int hashCode() => url.hashCode();
}

