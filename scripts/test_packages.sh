#!/bin/zsh
set -euo pipefail
TASK_ROOT="${0:A:h:h}"
TASK_BUILD="$(mktemp -d /tmp/satchel-package-tests-build.XXXXXX)"
trap 'rm -rf "$TASK_BUILD"' EXIT
cd "$TASK_ROOT"
xcrun swiftc -swift-version 6 -warnings-as-errors -O \
  App/SwitchPackage.swift Tests/package_reader.swift -o "$TASK_BUILD/package-reader"
"$TASK_BUILD/package-reader" "$@"
