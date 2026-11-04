#!/bin/zsh
set -euo pipefail
signer_root=${0:A:h}
signer_out=${SIGNER_OUT:-$HOME/SatchelBuild/signer}
signer_fixture="${SIGNER_FIXTURE_OUT:-$signer_out/format}"
signer_ssl=/opt/homebrew/opt/openssl@3
mkdir -p "$signer_fixture"
umask 077
for signer_name in fixture other expired; do
  signer_dates=(-days 2)
  if [[ $signer_name == expired ]]; then signer_dates=(-not_before 20200101000000Z -not_after 20200102000000Z); fi
  "$signer_ssl/bin/openssl" req -x509 -newkey rsa:2048 -noenc \
    -keyout "$signer_fixture/$signer_name-key.pem" -out "$signer_fixture/$signer_name-cert.pem" \
    "${signer_dates[@]}" -subj '/CN=Satchel Disposable Fixture/OU=PROBETEST1' \
    -addext 'keyUsage=critical,digitalSignature,keyCertSign' -addext 'extendedKeyUsage=codeSigning' \
    > "$signer_fixture/$signer_name-generation.log" 2>&1
  "$signer_ssl/bin/openssl" pkcs12 -export -inkey "$signer_fixture/$signer_name-key.pem" \
    -in "$signer_fixture/$signer_name-cert.pem" -certfile "$signer_fixture/$signer_name-cert.pem" \
    -out "$signer_fixture/$signer_name.p12" -passout pass:fixture-only
done
"$signer_out/signer-tests" "$signer_fixture" "${SIGNER_UNSIGNED_INPUT:-$HOME/SatchelBuild/prepare/game-unsigned.dylib}"
codesign --verify --strict --verbose=4 "$signer_fixture/game.dylib"
python3 "$signer_root/../../Spikes/SignLoadProbe/Scripts/check-signatures.py" "$signer_fixture/game.dylib" > "$signer_out/structure.json"
print 'PASS: codesign strict and independent signature structure checker'
