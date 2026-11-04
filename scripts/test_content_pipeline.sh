#!/bin/zsh
set -euo pipefail
TASK_ROOT="${0:A:h:h}"
TASK_BUILD="${CONTENT_BUILD:-$HOME/SatchelBuild/content/tests}"
mkdir -p "$TASK_BUILD"
cd "$TASK_ROOT"
export TMPDIR="$TASK_BUILD/"
C_FLAGS=()
SWIFT_FLAGS=()
if [[ "${CONTENT_SANITIZE:-0}" == 1 ]]; then
  C_FLAGS=(-fsanitize=address -fno-omit-frame-pointer)
  SWIFT_FLAGS=(-sanitize=address)
fi
# Reuse existing synthetic constructors; remove only their executable entry point.
sed 's/@main//' Tests/switch_executable.swift > "$TASK_BUILD/executable_fixtures.swift"
sed 's/@main//' Tests/switch_romfs.swift > "$TASK_BUILD/romfs_fixtures.swift"
cat > "$TASK_BUILD/Bridge.h" <<BRIDGE
#include "$TASK_ROOT/Core/SwitchCrypto.h"
#include "$TASK_ROOT/Core/SwitchStorage.h"
#include "$TASK_ROOT/Core/NSOLoader.h"
BRIDGE
nice -n 15 xcrun clang -std=c11 -O1 -g -Wall -Wextra -Werror "${C_FLAGS[@]}" -c Core/SwitchCrypto.c -o "$TASK_BUILD/crypto.o"
nice -n 15 xcrun clang -std=c11 -O1 -g -Wall -Wextra -Werror "${C_FLAGS[@]}" -c Core/NSOLoader.c -o "$TASK_BUILD/nso.o"
nice -n 15 xcrun swiftc -swift-version 6 -warnings-as-errors -O -num-threads 2 "${SWIFT_FLAGS[@]}" \
  -import-objc-header "$TASK_BUILD/Bridge.h" \
  App/SwitchPackage.swift App/SwitchControl.swift App/SwitchExecutable.swift App/SwitchRomFS.swift \
  App/SwitchStorage.swift App/SwitchKeys.swift App/SwitchContentMeta.swift App/SwitchPatchStorage.swift \
  App/SwitchContentPipeline.swift App/SwitchCompressedInput.swift App/SwitchRomFSImage.swift App/SwitchDecodedModule.swift \
  "$TASK_BUILD/executable_fixtures.swift" "$TASK_BUILD/romfs_fixtures.swift" Tests/switch_content.swift \
  "$TASK_BUILD/crypto.o" "$TASK_BUILD/nso.o" "$HOME/SatchelBuild/m5/archive-macosx/libShaderArchive.a" -o "$TASK_BUILD/content-tests"
nice -n 15 "$TASK_BUILD/content-tests" "$TASK_BUILD/synthetic"
