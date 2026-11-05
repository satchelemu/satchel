#!/bin/zsh
set -euo pipefail
TASK_ROOT="${0:A:h:h}"
TASK_OUT="${PREPARE_OUT:-$HOME/SatchelBuild/prepare}"
TASK_INPUT_ROOT="${SATCHEL_BUILD_ROOT:-$HOME/SatchelBuild}"
mkdir -p "$TASK_OUT/core"
cd "$TASK_ROOT"
export TMPDIR="$TASK_OUT/core/"
/bin/zsh NativePrepare/Signer/wait-for-build.zsh
nice -n 10 cmake -S Tools/NativeRewriter -B "$TASK_OUT/rewriter-mac" -DCMAKE_BUILD_TYPE=Release -DNR_BUILD_SHARED=OFF -DBUILD_TESTING=OFF -DFETCHCONTENT_SOURCE_DIR_CAPSTONE="$TASK_INPUT_ROOT/rewriter/capstone-src"
nice -n 10 cmake --build "$TASK_OUT/rewriter-mac" -j2
python3 NativePrepare/generate_registry.py "$TASK_OUT/core/Registry.c"
cat > "$TASK_OUT/core/Bridge.h" <<BRIDGE
#include "$TASK_ROOT/Core/SwitchCrypto.h"
#include "$TASK_ROOT/Core/NSOLoader.h"
#include "$TASK_ROOT/Core/SwitchStorage.h"
#include "$TASK_ROOT/NativePrepare/Registry.h"
BRIDGE
signer_sources=()
signer_libraries=()
if [[ "${PREPARE_SIGNER:-0}" == 1 ]]; then
  print '#include "'"$TASK_ROOT"'/NativePrepare/Signer/SigningIdentity.h"' >> "$TASK_OUT/core/Bridge.h"
  signer_sources=(NativePrepare/Signer/PhonePreparedGameSigner.swift NativePrepare/Signer/SignatureVerifier.swift)
  signer_libraries=("$TASK_OUT/libSatchelSigner.a" /opt/homebrew/opt/openssl@3/lib/libcrypto.a -framework Security -framework Foundation)
fi
for source in Core/SwitchCrypto.c Core/NSOLoader.c "$TASK_OUT/core/Registry.c"; do
  nice -n 10 xcrun clang -O1 -I NativePrepare -I Tools/NativeRewriter/include -c "$source" -o "$TASK_OUT/core/${source:t:r}.o"
done
nice -n 10 xcrun swiftc -swift-version 6 -O -g -num-threads 2 -import-objc-header "$TASK_OUT/core/Bridge.h" -I Tools/NativeRewriter/include \
  App/NativeGameProfile.swift App/SwitchPackage.swift App/SwitchControl.swift App/SwitchExecutable.swift App/SwitchRomFS.swift App/SwitchStorage.swift App/SwitchKeys.swift \
  App/SwitchAddOnContent.swift App/PreparedAddOn.swift App/SwitchContentMeta.swift App/SwitchPatchStorage.swift App/SwitchContentPipeline.swift App/SwitchCompressedInput.swift App/SwitchRomFSImage.swift App/SwitchDecodedModule.swift \
  "${signer_sources[@]}" NativePrepare/Progress.swift NativePrepare/Rewrite.swift NativePrepare/MachO.swift NativePrepare/Discovery.swift NativePrepare/NROArchive.swift "${PREPARE_PIPELINE:-NativePrepare/Pipeline.swift}" "${PREPARE_MAIN:-NativePrepare/Harness.swift}" \
  "$TASK_OUT/core/SwitchCrypto.o" "$TASK_OUT/core/NSOLoader.o" "$TASK_OUT/core/Registry.o" \
  "$TASK_OUT/rewriter-mac/libnative_rewriter.a" "$TASK_OUT/rewriter-mac/_deps/capstone-build/libcapstone.a" -lc++ "${SATCHEL_ARCHIVE_LIBRARY:-$TASK_INPUT_ROOT/m5/archive-macosx/libShaderArchive.a}" "${signer_libraries[@]}" -o "${PREPARE_EXECUTABLE:-$TASK_OUT/prepare}"
PREPARE_BINARY="${PREPARE_EXECUTABLE:-$TASK_OUT/prepare}"
cp App/NativeGameProfiles.json "${PREPARE_BINARY:h}/NativeGameProfiles.json"

cp App/NativeRuntimeProfiles.json "${PREPARE_BINARY:h}/NativeRuntimeProfiles.json"
