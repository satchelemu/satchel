#!/bin/zsh
set -euo pipefail
TASK_ROOT="${0:A:h:h}"
TASK_BUILD="$(mktemp -d /tmp/satchel-package-import.XXXXXX)"
trap 'rm -rf "$TASK_BUILD"' EXIT
cd "$TASK_ROOT"
xcrun swiftc -O -warnings-as-errors App/SwitchPackage.swift App/SwitchPackageImport.swift \
  Tests/package_import.swift -o "$TASK_BUILD/package-import"
"$TASK_BUILD/package-import" "$@"
