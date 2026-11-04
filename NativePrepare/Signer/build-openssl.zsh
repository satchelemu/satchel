#!/bin/zsh
set -euo pipefail
signer_root=${0:A:h}
signer_out=${SIGNER_OUT:-$HOME/SatchelBuild/signer}
mkdir -p "$signer_out"
if [[ ! -f "$signer_out/openssl-3.6.3/Configure" ]]; then
  curl -fL https://github.com/openssl/openssl/releases/download/openssl-3.6.3/openssl-3.6.3.tar.gz -o "$signer_out/openssl-3.6.3.tar.gz"
  tar -xzf "$signer_out/openssl-3.6.3.tar.gz" -C "$signer_out"
fi
/bin/zsh "$signer_root/wait-for-build.zsh"
cd "$signer_out/openssl-3.6.3"
./Configure ios64-xcrun no-shared no-tests no-apps no-module -miphoneos-version-min=17.0 --prefix="$signer_out/openssl-ios"
nice -n 10 make -j2 build_libs
nice -n 10 make -j2 install_dev
