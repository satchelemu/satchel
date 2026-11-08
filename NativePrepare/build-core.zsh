#!/bin/zsh
set -euo pipefail
TASK_ROOT="${0:A:h:h}"
TASK_OUT="${PREPARE_OUT:-$HOME/SatchelBuild/prepare}"
TASK_INPUT_ROOT="${SATCHEL_BUILD_ROOT:-$HOME/SatchelBuild}"
mkdir -p "$TASK_OUT/core"
cd "$TASK_ROOT"
# The shader archive of the current dependency root, or a clear stop naming the variable to set.
ARCHIVE_LIBRARY="$(python3 -B scripts/dependency_paths.py archive-library macosx)"
# Capstone 5.0.7 source: SATCHEL_CAPSTONE_SOURCE (as the app build reads it), else the build root's copy.
CAPSTONE_SOURCE="${SATCHEL_CAPSTONE_SOURCE:-$TASK_INPUT_ROOT/rewriter/capstone-src}"
[[ -f "$CAPSTONE_SOURCE/CMakeLists.txt" ]] || { print -u2 "No Capstone source at $CAPSTONE_SOURCE: set SATCHEL_CAPSTONE_SOURCE to the Capstone 5.0.7 source folder (BUILDING.md)"; exit 1; }
export TMPDIR="$TASK_OUT/core/"
/bin/zsh NativePrepare/Signer/wait-for-build.zsh
nice -n 10 cmake -S Tools/NativeRewriter -B "$TASK_OUT/rewriter-mac" -DCMAKE_BUILD_TYPE=Release -DNR_BUILD_SHARED=OFF -DBUILD_TESTING=OFF -DFETCHCONTENT_SOURCE_DIR_CAPSTONE="$CAPSTONE_SOURCE"
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
  # This is a Mac executable, so it needs a macOS libcrypto. SATCHEL_OPENSSL_IOS (the app's iOS
  # OpenSSL) cannot link here: ld refuses objects built for iOS in a macOS link.
  # SATCHEL_OPENSSL_MAC names a macOS OpenSSL 3 prefix; the default is Homebrew's openssl@3.
  OPENSSL_MAC="${SATCHEL_OPENSSL_MAC:-/opt/homebrew/opt/openssl@3}"
  [[ -f "$OPENSSL_MAC/lib/libcrypto.a" ]] || { print -u2 "No macOS libcrypto.a under $OPENSSL_MAC: set SATCHEL_OPENSSL_MAC to a macOS OpenSSL 3 prefix (not SATCHEL_OPENSSL_IOS, which is built for iOS)"; exit 1; }
  print '#include "'"$TASK_ROOT"'/NativePrepare/Signer/SigningIdentity.h"' >> "$TASK_OUT/core/Bridge.h"
  signer_sources=(NativePrepare/Signer/PhonePreparedGameSigner.swift NativePrepare/Signer/SignatureVerifier.swift)
  signer_libraries=("$TASK_OUT/libSatchelSigner.a" "$OPENSSL_MAC/lib/libcrypto.a" -framework Security -framework Foundation)
fi
for source in Core/SwitchCrypto.c Core/NSOLoader.c "$TASK_OUT/core/Registry.c"; do
  nice -n 10 xcrun clang -O1 -I NativePrepare -I Tools/NativeRewriter/include -c "$source" -o "$TASK_OUT/core/${source:t:r}.o"
done
nice -n 10 xcrun swiftc -swift-version 6 -O -g -num-threads 2 -import-objc-header "$TASK_OUT/core/Bridge.h" -I Tools/NativeRewriter/include \
  App/NativeGameProfile.swift App/SwitchPackage.swift App/SwitchControl.swift App/SwitchExecutable.swift App/SwitchRomFS.swift App/SwitchStorage.swift App/SwitchKeys.swift \
  App/SwitchAddOnContent.swift App/PreparedAddOn.swift App/SwitchContentMeta.swift App/SwitchPatchStorage.swift App/SwitchContentPipeline.swift App/SwitchCompressedInput.swift App/SwitchRomFSImage.swift App/SwitchDecodedModule.swift \
  "${signer_sources[@]}" NativePrepare/Progress.swift NativePrepare/Rewrite.swift NativePrepare/MachO.swift NativePrepare/Discovery.swift NativePrepare/NROArchive.swift "${PREPARE_PIPELINE:-NativePrepare/Pipeline.swift}" "${PREPARE_MAIN:-NativePrepare/Harness.swift}" \
  "$TASK_OUT/core/SwitchCrypto.o" "$TASK_OUT/core/NSOLoader.o" "$TASK_OUT/core/Registry.o" \
  "$TASK_OUT/rewriter-mac/libnative_rewriter.a" "$TASK_OUT/rewriter-mac/_deps/capstone-build/libcapstone.a" -lc++ "$ARCHIVE_LIBRARY" "${signer_libraries[@]}" -o "${PREPARE_EXECUTABLE:-$TASK_OUT/prepare}"
PREPARE_BINARY="${PREPARE_EXECUTABLE:-$TASK_OUT/prepare}"
cp App/NativeGameProfiles.json "${PREPARE_BINARY:h}/NativeGameProfiles.json"

cp App/NativeRuntimeProfiles.json "${PREPARE_BINARY:h}/NativeRuntimeProfiles.json"
