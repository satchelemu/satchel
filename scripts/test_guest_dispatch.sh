#!/bin/zsh
set -euo pipefail
TASK_ROOT="${0:A:h:h}"
TASK_CPU_ARCHIVE="${SATCHEL_CPU_ARCHIVE:-/tmp/satchel-vixl-probe/build/macos/libSatchelCPUProbe.a}"
TASK_OUT="/tmp/satchel-guest-dispatch"
mkdir -p "$TASK_OUT"
# Coordinate with the CPU owner before invoking: its host archive must be frozen.
xcrun clang -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I "$TASK_ROOT/Tools/CPUProbe" -c "$TASK_ROOT/Tools/GuestGraphics/GuestDispatch.c" -o "$TASK_OUT/dispatch.o"
xcrun clang -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I "$TASK_ROOT/Tools/CPUProbe" -c "$TASK_ROOT/Tools/GuestGraphics/DispatchTest.c" -o "$TASK_OUT/test.o"
xcrun clang++ -fsanitize=address,undefined -fno-omit-frame-pointer "$TASK_OUT/dispatch.o" "$TASK_OUT/test.o" \
  "$TASK_CPU_ARCHIVE" -o "$TASK_OUT/test"
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 "$TASK_OUT/test"
if [[ "${1:-}" == "--ios" ]]; then
  xcrun clang -std=c11 -O2 -Wall -Wextra -Werror -target arm64-apple-ios17.0 \
    -isysroot "$(xcrun --sdk iphoneos --show-sdk-path)" -I "$TASK_ROOT/Tools/CPUProbe" \
    -c "$TASK_ROOT/Tools/GuestGraphics/GuestDispatch.c" -o "$TASK_OUT/GuestDispatch-ios.o"
  mkdir -p /tmp/satchel-guest-graphics
  xcrun libtool -static -o /tmp/satchel-guest-graphics/libSatchelGuestDispatch.a "$TASK_OUT/GuestDispatch-ios.o"
fi
