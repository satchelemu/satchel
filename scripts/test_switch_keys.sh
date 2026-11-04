#!/bin/zsh
set -euo pipefail
TASK_ROOT="${0:A:h:h}"
TASK_BUILD="$(mktemp -d /tmp/satchel-key-tests.XXXXXX)"
trap 'rm -rf "$TASK_BUILD"' EXIT
cd "$TASK_ROOT"
xcrun swiftc -O -warnings-as-errors App/SwitchKeys.swift Tests/switch_keys.swift -o "$TASK_BUILD/keys"
"$TASK_BUILD/keys"
