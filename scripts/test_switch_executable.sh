#!/bin/zsh
set -euo pipefail
TASK_ROOT="${0:A:h:h}"
TASK_BUILD="$(mktemp -d /tmp/satchel-executable-build.XXXXXX)"
trap 'rm -rf "$TASK_BUILD"' EXIT
cd "$TASK_ROOT"
cat > "$TASK_BUILD/Bridge.h" <<EOF
#include "$TASK_ROOT/Core/SwitchCrypto.h"
#include "$TASK_ROOT/Core/NSOLoader.h"
EOF
xcrun clang -O1 -c Core/NSOLoader.c -o "$TASK_BUILD/nso.o"
xcrun clang -std=c11 -O2 -Wall -Wextra -Werror -I Core \
  -c Core/SwitchCrypto.c -o "$TASK_BUILD/crypto.o"
xcrun swiftc -swift-version 6 -warnings-as-errors -O -import-objc-header "$TASK_BUILD/Bridge.h" \
  App/SwitchControl.swift App/SwitchPackage.swift App/SwitchExecutable.swift App/SwitchRomFS.swift App/SwitchContentMeta.swift App/SwitchPatchStorage.swift Tests/switch_executable.swift \
  "$TASK_BUILD/crypto.o" "$TASK_BUILD/nso.o" -o "$TASK_BUILD/executable-test"
"$TASK_BUILD/executable-test" "$@"
