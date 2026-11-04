#!/bin/zsh
set -euo pipefail
TASK_ROOT="${0:A:h:h}"
TASK_BUILD="$(mktemp -d /tmp/satchel-format-tests.XXXXXX)"
trap 'rm -rf "$TASK_BUILD"' EXIT
cd "$TASK_ROOT"
xcrun clang -std=c11 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Wall -Wextra -Werror -I Core Core/HomebrewFormat.c Core/NROLoader.c \
  Tests/formats.c -o "$TASK_BUILD/formats"
# Filename extensions never decide whether content enters the NRO validator.
cp App/Resources/hello-horizon.nro "$TASK_BUILD/renamed.nsp"
cp App/Resources/hello-horizon.nro "$TASK_BUILD/renamed.xci"
cp App/Resources/hello-horizon.nro "$TASK_BUILD/without-extension"
"$TASK_BUILD/formats" App/Resources/hello-horizon.nro \
  "$TASK_BUILD/renamed.nsp" "$TASK_BUILD/renamed.xci" "$TASK_BUILD/without-extension"
