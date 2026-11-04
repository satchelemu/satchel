#!/bin/zsh
set -euo pipefail
signer_root=${0:A:h}
signer_out=${SIGNER_OUT:-$HOME/SatchelBuild/signer}
signer_ssl=/opt/homebrew/opt/openssl@3
mkdir -p "$signer_out/mac-objects"
/bin/zsh "$signer_root/wait-for-build.zsh"
for signer_source in "$signer_root/SigningIdentity.mm" "$signer_root"/Vendor/zsign/*.cpp "$signer_root"/Vendor/zsign/common/*.cpp; do
  signer_flags=()
  if [[ $signer_source == *.mm ]]; then signer_flags+=(-fobjc-arc); fi
  nice -n 10 xcrun clang++ -std=c++17 -O2 -DSATCHEL_SIGNER_QUIET=1 "${signer_flags[@]}" \
    -I"$signer_root" -I"$signer_root/Vendor/zsign" -I"$signer_root/Vendor/zsign/common" -I"$signer_ssl/include" \
    -c "$signer_source" -o "$signer_out/mac-objects/${signer_source:t:r}.o"
done
ar rcs "$signer_out/libSatchelSigner.a" "$signer_out"/mac-objects/*.o
