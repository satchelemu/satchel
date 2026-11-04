#!/bin/zsh
set -euo pipefail
TASK_ROOT="${0:A:h:h}"
TASK_BUILD="$(mktemp -d /tmp/satchel-process-tests.XXXXXX)"
trap 'rm -rf "$TASK_BUILD"' EXIT
TASK_CPU_ARCHIVE="${SATCHEL_CPU_PROBE_ARCHIVE:-/tmp/satchel-vixl-probe/build/macos/libSatchelCPUProbe.a}"
[[ -f "$TASK_CPU_ARCHIVE" ]] || { print -u2 "Build the current CPU probe archive before running process tests."; exit 1; }
TASK_IPC_ARCHIVE="${SATCHEL_HORIZON_IPC_ARCHIVE:-/tmp/satchel-horizon-ipc/macos/libSatchelHorizonIPC.a}"
[[ -f "$TASK_IPC_ARCHIVE" ]] || { print -u2 "Build the validated Horizon IPC archive before running process tests."; exit 1; }
cd "$TASK_ROOT"
TASK_TEST_FLAGS=(-DSATCHEL_SWITCH_RESOURCES_TESTING)
# The optional Mac-only CPU allocation seam is absent from iOS/release archives.
if nm -g "$TASK_CPU_ARCHIVE" | rg ' T _satchel_cpu_probe_test_mapping_allocation_failure$' > /dev/null; then
  TASK_TEST_FLAGS+=(-DSATCHEL_CPU_TESTING)
fi
xcrun clang -std=c11 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Wall -Wextra -Werror "${TASK_TEST_FLAGS[@]}" -I Core -I Tools/CPUProbe -I Tools/HorizonIPC Core/SwitchProcess.c Core/ProcessMetadata.c Core/SwitchStorageIPC.c Core/SwitchResources.c Core/SwitchEvents.c Tests/switch_process.c \
  "$TASK_CPU_ARCHIVE" "$TASK_IPC_ARCHIVE" -lc++ -o "$TASK_BUILD/process"
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 "$TASK_BUILD/process"
