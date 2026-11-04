#!/bin/zsh
set -euo pipefail
TASK_ROOT="${0:A:h:h}"
TASK_BUILD="$(mktemp -d /tmp/satchel-time-build.XXXXXX)"
trap 'rm -rf "$TASK_BUILD"' EXIT
cd "$TASK_ROOT"
xcrun swiftc -swift-version 6 -warnings-as-errors -O -g -sanitize=address \
 -import-objc-header Core/SwitchProcess.h -Xcc -ICore -Xcc -ITools/CPUProbe \
 App/SwitchTime.swift Tests/switch_time.swift -o "$TASK_BUILD/time-test"
ASAN_OPTIONS=halt_on_error=1 "$TASK_BUILD/time-test"
