#!/bin/zsh
set -euo pipefail
TASK_ROOT="${0:A:h:h}"
TASK_BUILD="$(mktemp -d /tmp/satchel-control-build.XXXXXX)"
trap 'rm -rf "$TASK_BUILD"' EXIT
cd "$TASK_ROOT"
cat > "$TASK_BUILD/Bridge.h" <<EOF
#include "$TASK_ROOT/Core/SwitchCrypto.h"
#include "$TASK_ROOT/Core/NSOLoader.h"
EOF
xcrun clang -O1 -c Core/NSOLoader.c -o "$TASK_BUILD/nso.o"
xcrun clang -std=c11 -O1 -g -fsanitize=address -fno-omit-frame-pointer -Wall -Wextra -Werror -I Core -c Core/SwitchCrypto.c -o "$TASK_BUILD/crypto.o"
xcrun swiftc -swift-version 6 -warnings-as-errors -O -g -sanitize=address -import-objc-header "$TASK_BUILD/Bridge.h" \
 App/SwitchPackage.swift App/SwitchExecutable.swift App/SwitchControl.swift App/SwitchRomFS.swift App/SwitchContentMeta.swift App/SwitchPatchStorage.swift App/SwitchKeys.swift Tests/switch_control.swift \
 "$TASK_BUILD/crypto.o" "$TASK_BUILD/nso.o" -o "$TASK_BUILD/control-test"
ASAN_OPTIONS=halt_on_error=1 "$TASK_BUILD/control-test"
