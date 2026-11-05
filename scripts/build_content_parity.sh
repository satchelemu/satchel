#!/bin/zsh
set -euo pipefail
TASK_ROOT="${0:A:h:h}"
TASK_BUILD="${CONTENT_BUILD:-$HOME/SatchelBuild/content/tests}"
mkdir -p "$TASK_BUILD"
cd "$TASK_ROOT"
export TMPDIR="$TASK_BUILD/"
cat > "$TASK_BUILD/ParityBridge.h" <<BRIDGE
#include "$TASK_ROOT/Core/SwitchCrypto.h"
#include "$TASK_ROOT/Core/NSOLoader.h"
BRIDGE
nice -n 15 xcrun clang -std=c11 -O1 -g -Wall -Wextra -Werror -c Core/SwitchCrypto.c -o "$TASK_BUILD/crypto.o"
nice -n 15 xcrun clang -std=c11 -O1 -g -Wall -Wextra -Werror -c Core/NSOLoader.c -o "$TASK_BUILD/nso.o"
nice -n 15 xcrun swiftc -swift-version 6 -warnings-as-errors -g -O -num-threads 2 \
  -import-objc-header "$TASK_BUILD/ParityBridge.h" \
  App/SwitchPackage.swift App/SwitchControl.swift App/SwitchExecutable.swift App/SwitchRomFS.swift \
  App/SwitchKeys.swift App/SwitchContentMeta.swift App/SwitchPatchStorage.swift App/SwitchContentPipeline.swift App/SwitchCompressedInput.swift \
  App/SwitchRomFSImage.swift App/SwitchDecodedModule.swift "${CONTENT_MAIN:-Tools/ContentPipeline/Parity.swift}" \
  "$TASK_BUILD/crypto.o" "$TASK_BUILD/nso.o" "${SATCHEL_ARCHIVE_LIBRARY:-$HOME/SatchelBuild/m5/archive-macosx/libShaderArchive.a}" -o "${CONTENT_EXECUTABLE:-$TASK_BUILD/content-parity}"
