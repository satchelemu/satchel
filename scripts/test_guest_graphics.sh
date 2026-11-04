#!/bin/zsh
set -euo pipefail
TASK_ROOT="${0:A:h:h}"
TASK_OUT="/tmp/satchel-guest-graphics"
mkdir -p "$TASK_OUT"
xcrun clang -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  "$TASK_ROOT/Tools/GuestGraphics/GuestSymbols.c" "$TASK_ROOT/Tools/GuestGraphics/Test.c" -o "$TASK_OUT/test"
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 "$TASK_OUT/test"
if [[ "${1:-}" == "--ios" ]]; then
  xcrun clang -std=c11 -O2 -Wall -Wextra -Werror -target arm64-apple-ios17.0 \
    -isysroot "$(xcrun --sdk iphoneos --show-sdk-path)" -c "$TASK_ROOT/Tools/GuestGraphics/GuestSymbols.c" -o "$TASK_OUT/GuestSymbols-ios.o"
  xcrun libtool -static -o "$TASK_OUT/libSatchelGuestSymbols.a" "$TASK_OUT/GuestSymbols-ios.o"
fi
