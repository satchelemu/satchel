#!/bin/zsh
set -euo pipefail
TASK_ROOT="${0:A:h:h}"
TASK_BUILD="$(mktemp -d /tmp/satchel-storage-ipc.XXXXXX)"
trap 'rm -rf "$TASK_BUILD"' EXIT
TASK_IPC="/tmp/satchel-horizon-ipc"
[[ -f "$TASK_IPC/reference/libnx_reference.h" && -f "$TASK_IPC/macos/libSatchelHorizonIPC.a" ]] || {
  print -u2 'Build scripts/test_horizon_ipc_parser.sh before the storage IPC checks.'; exit 1
}
cd "$TASK_ROOT"
xcrun clang -std=gnu11 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Wall -Wextra -Werror -I Core -I Tools/HorizonIPC -I "$TASK_IPC/reference" \
  Core/SwitchStorageIPC.c Tests/switch_storage_ipc.c "$TASK_IPC/macos/libSatchelHorizonIPC.a" \
  -o "$TASK_BUILD/storage"
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 "$TASK_BUILD/storage"
