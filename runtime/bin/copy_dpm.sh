#!/bin/bash

for F in dpm.dart version.dart package.dart repository.dart import.dart ; do
  cp ../../../dart-package-manager/$F dpm_$F
  sed -i -e 's/^#.*$//' dpm_$F
done


