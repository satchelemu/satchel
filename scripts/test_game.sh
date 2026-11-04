#!/bin/zsh
set -euo pipefail
TASK_ROOT="${0:A:h:h}"
# Use the pinned author's original NRO and its corresponding original main.c.
TASK_NRO="${1:A}"
TASK_SOURCE="${2:A}"
TASK_BUILD="$(mktemp -d /tmp/satchel-game-tests.XXXXXX)"
trap 'rm -rf "$TASK_BUILD"' EXIT
cd "$TASK_ROOT"
zsh scripts/build_game_reference.sh "$TASK_SOURCE" "$TASK_BUILD/reference"
mkdir -p "$TASK_BUILD/executed"
xcrun clang -O3 -Wall -Wextra -Werror -I Core Core/Interpreter.c Core/NROLoader.c \
  Tests/game_execution.c -o "$TASK_BUILD/game"
"$TASK_BUILD/game" "$TASK_NRO" "$TASK_BUILD/reference" "$TASK_BUILD/executed"
