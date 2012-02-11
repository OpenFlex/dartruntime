// Copyright (c) 2012, Ladislav Thon. All rights reserved. Use of this source
// code is governed by a BSD-style license that can be found in the LICENSE file.

class VersionException extends DpmException {
  VersionException(String message) : super(message);
}

class Version implements Comparable, Hashable {
  static final RegExp _versionFormat = const RegExp(@'^([0-9]+\.)*[0-9]+$');

  final String _value;

  Version(String value) : _value = value {
    if (_value == null) {
      throw new VersionException("Version must not be null");
    }
    if (!_versionFormat.hasMatch(_value)) {
      throw new VersionException("Version '$_value' has bad format");
    }
  }

  bool operator==(Object other) => other is Version && _value == other._value;

  int compareTo(Version other) => _value.compareTo(other._value);

  int hashCode() => _value.hashCode();

  String toString() => _value;
}

interface VersionSpecification extends Hashable default _VersionSpecificationParser {
  VersionSpecification(String version);

  bool isSatisfiedBy(Version version);

  bool operator ==(VersionSpecification other);

  String toString();
}

class _FixedVersionSpecification implements VersionSpecification {
  final String _value;

  _FixedVersionSpecification(String value) : _value = value;

  bool isSatisfiedBy(Version version) => _value == version._value;

  bool operator ==(other) => other is _FixedVersionSpecification && _value == other._value;

  int hashCode() => _value.hashCode();

  String toString() => _value;
}

class _WildcardVersionSpecification implements VersionSpecification {
  final String _value;

  _WildcardVersionSpecification(String value) : _value = value;

  bool isSatisfiedBy(Version version) => version._value.startsWith(_value);

  bool operator ==(other) => other is _WildcardVersionSpecification && _value == other._value;

  int hashCode() => _value.hashCode();

  String toString() => _value.length > 0 ? '$_value.*' : '*';
}

class _VersionSpecificationParser {
  static final RegExp fixedVersion = Version._versionFormat;
  static final RegExp wildcardVersion = const RegExp(@'^([0-9]+\.)*\*$');

  static bool matches(String str) => fixedVersion.hasMatch(str)
      || wildcardVersion.hasMatch(str);

  factory VersionSpecification(String version) {
    if (version == null) {
      throw new VersionException("Version specification must not be null");
    }

    if (fixedVersion.hasMatch(version)) {
      return new _FixedVersionSpecification(version);
    } else if (wildcardVersion.hasMatch(version)) {
      if (version.endsWith('*')) { version = version.substring(0, version.length - 1); }
      if (version.endsWith('.')) { version = version.substring(0, version.length - 1); }
      return new _WildcardVersionSpecification(version);
    } else {
      throw new VersionException("Version specification '$version' has bad format");
    }
  }
}

