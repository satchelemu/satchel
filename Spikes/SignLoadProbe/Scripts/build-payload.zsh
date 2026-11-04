#!/bin/zsh
set -euo pipefail
probe_root=${SRCROOT:-${0:A:h:h}}
probe_out="$HOME/SatchelBuild/spike2/payload"
mkdir -p "$probe_out"
probe_sdk=$(xcrun --sdk iphoneos --show-sdk-path)
nice -n 10 xcrun clang -target arm64-apple-ios17.0 -isysroot "$probe_sdk" -dynamiclib -O2 \
  -Wl,-headerpad,0x4000 -Wl,-no_adhoc_codesign -Wl,-install_name,@rpath/probe.dylib \
  "$probe_root/Payload/probe.c" "$probe_root/Payload/page.S" -o "$probe_out/unsigned-probe.bin"
cp "$probe_out/unsigned-probe.bin" "$probe_out/probe.dylib"
codesign --force --sign "${EXPANDED_CODE_SIGN_IDENTITY:-${SATCHEL_SIGNING_IDENTITY:?set SATCHEL_SIGNING_IDENTITY to your codesign identity}}" \
  --identifier com.dell.satchel.signprobe.payload --timestamp=none "$probe_out/probe.dylib"
codesign --verify --strict --verbose=2 "$probe_out/probe.dylib"
