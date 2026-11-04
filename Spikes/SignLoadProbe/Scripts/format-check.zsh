#!/bin/zsh
set -euo pipefail
probe_root=${0:A:h:h}
probe_out="$HOME/SatchelBuild/spike2/format"
probe_ssl=/opt/homebrew/opt/openssl@3
mkdir -p "$probe_out"
# This key is a synthetic test fixture, never an Apple or user identity.
umask 077
"$probe_ssl/bin/openssl" req -x509 -newkey rsa:2048 -nodes -keyout "$probe_out/fixture-key.pem" \
  -out "$probe_out/fixture-cert.pem" -days 2 -subj '/CN=Satchel Format Fixture/OU=PROBETEST1' \
  -addext 'keyUsage=critical,digitalSignature,keyCertSign' -addext 'extendedKeyUsage=codeSigning' > "$probe_out/identity-generation.log" 2>&1
"$probe_ssl/bin/openssl" pkcs12 -export -inkey "$probe_out/fixture-key.pem" -in "$probe_out/fixture-cert.pem" \
  -certfile "$probe_out/fixture-cert.pem" -out "$probe_out/fixture.p12" -passout pass:
nice -n 10 xcrun clang++ -std=c++17 -fobjc-arc -O2 -framework Foundation \
  -I"$probe_root/Native" -I"$probe_root/../../NativePrepare/Signer/Vendor/zsign" -I"$probe_root/../../NativePrepare/Signer/Vendor/zsign/common" -I"$probe_ssl/include" \
  "$probe_root/Scripts/FormatCheck.mm" "$probe_root/Native/SignerBridge.mm" "$probe_root/Native/GeneratedMachO.mm" \
  "$probe_root"/../../NativePrepare/Signer/Vendor/zsign/*.cpp "$probe_root"/../../NativePrepare/Signer/Vendor/zsign/common/*.cpp \
  "$probe_ssl/lib/libcrypto.a" -o "$probe_out/format-check" > "$probe_out/compile.log" 2>&1
"$probe_out/format-check" "$probe_out/fixture.p12" "$HOME/SatchelBuild/spike2/payload/unsigned-probe.bin" "$probe_out"
for probe_file in "$probe_out"/selfsigned-*.dylib; do
  codesign -dvvv "$probe_file" > "$probe_file.display.log" 2>&1
  codesign --verify --strict --verbose=4 "$probe_file" > "$probe_file.verify.log" 2>&1
  print "PASS: ${probe_file:t}"
done
# Reference signing stays inside Apple's codesign. No user identity is exported.
print '<?xml version="1.0" encoding="UTF-8"?><plist version="1.0"><dict/></plist>' > "$probe_out/empty.plist"
for probe_payload in S1c S1d; do
  for probe_variant in A B C; do
    probe_file="$probe_out/mac-$probe_payload-$probe_variant.dylib"
    cp "$probe_out/$probe_payload-unsigned.dylib" "$probe_file"
    probe_options=()
    if [[ $probe_variant == B ]]; then probe_options+=(--pagesize 4096); fi
    if [[ $probe_variant == C ]]; then
      probe_options+=(--force-library-entitlements --entitlements "$probe_out/empty.plist" --generate-entitlement-der)
    fi
    codesign --force --sign "${SATCHEL_SIGNING_IDENTITY:?set SATCHEL_SIGNING_IDENTITY to your codesign identity}" \
      --identifier "com.dell.satchel.signprobe.$probe_payload" --timestamp=none \
      "${probe_options[@]}" "$probe_file"
    codesign -dvvv "$probe_file" > "$probe_file.display.log" 2>&1
    codesign --verify --strict --verbose=4 "$probe_file" > "$probe_file.verify.log" 2>&1
    print "PASS: ${probe_file:t}"
  done
done
python3 "$probe_root/Scripts/check-signatures.py" "$probe_out" > "$probe_out/structure.json"
print "PASS: structure, code hashes, special hashes, XML/DER, exec segments, and Mac comparisons"
cp "$probe_out/generated-macos-unsigned.dylib" "$probe_out/generated-macos-adhoc.dylib"
codesign --force --sign - "$probe_out/generated-macos-adhoc.dylib"
nice -n 10 xcrun clang "$probe_root/Scripts/LoadControl.c" -o "$probe_out/load-control"
"$probe_out/load-control" "$probe_out/generated-macos-adhoc.dylib"
