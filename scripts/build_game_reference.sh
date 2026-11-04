#!/bin/zsh
set -euo pipefail
TASK_ROOT="${0:A:h:h}"
# Arguments: pinned original source file, external output directory.
TASK_SOURCE="${1:A}"
TASK_OUTPUT="${2:A}"
mkdir -p "$TASK_OUTPUT"
xcrun clang -O2 -I "$TASK_ROOT/Tests/NativeSwitch" \
  -D"GAME_SOURCE=\"$TASK_SOURCE\"" "$TASK_ROOT/Tests/tictactoe_reference.c" -o "$TASK_OUTPUT/reference"
"$TASK_OUTPUT/reference" "$TASK_OUTPUT"
