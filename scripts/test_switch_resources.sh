#!/bin/zsh
set -euo pipefail
TASK_ROOT="${0:A:h:h}"
TASK_BUILD="$(mktemp -d /tmp/satchel-switch-resources.XXXXXX)"
trap 'rm -rf "$TASK_BUILD"' EXIT
cd "$TASK_ROOT"
xcrun clang -std=c11 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -DSATCHEL_SWITCH_RESOURCES_TESTING -Wall -Wextra -Werror -I Core \
  Core/SwitchResources.c Tests/switch_resources.c -o "$TASK_BUILD/resources"
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 "$TASK_BUILD/resources"
