#!/bin/zsh
set -euo pipefail
probe_out="$HOME/SatchelBuild/spike"
mkdir -p "$probe_out"
if [[ ! -f "$probe_out/openssl-3.6.3/Configure" ]]; then
  curl -fL https://github.com/openssl/openssl/releases/download/openssl-3.6.3/openssl-3.6.3.tar.gz -o "$probe_out/openssl-3.6.3.tar.gz"
  tar -xzf "$probe_out/openssl-3.6.3.tar.gz" -C "$probe_out"
fi
cd "$probe_out/openssl-3.6.3"
./Configure ios64-xcrun no-shared no-tests no-apps no-module -miphoneos-version-min=17.0 --prefix="$probe_out/openssl-ios"
make -j8 build_libs
make install_dev
