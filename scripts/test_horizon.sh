#!/bin/zsh
set -euo pipefail
TASK_ROOT="${0:A:h:h}"
TASK_BUILD="$(mktemp -d /tmp/satchel-horizon-tests.XXXXXX)"
trap 'rm -rf "$TASK_BUILD"' EXIT
cd "$TASK_ROOT"
xcrun clang -std=c11 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Wall -Wextra -Werror -I Core Tests/horizon_ipc.c Core/NROLoader.c -o "$TASK_BUILD/horizon"
"$TASK_BUILD/horizon" App/Resources/hello-horizon.nro
