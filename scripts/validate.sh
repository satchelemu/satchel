#!/bin/zsh
set -euo pipefail
TASK_ROOT="${0:A:h:h}"
TASK_BUILD="$(mktemp -d /tmp/satchel-validation.XXXXXX)"
trap 'rm -rf "$TASK_BUILD"' EXIT
cd "$TASK_ROOT"
python3 scripts/build_guest.py
xcrun clang -std=c11 -O2 -Wall -Wextra -Werror -I Core \
  -DNATIVE_REFERENCE -Dguest_main=reference_main -c Guest/brick.c -o "$TASK_BUILD/reference.o"
xcrun clang -std=c11 -O2 -Wall -Wextra -Werror -I Core \
  Core/Interpreter.c Core/NROLoader.c Tests/validate.c "$TASK_BUILD/reference.o" -o "$TASK_BUILD/validate"
"$TASK_BUILD/validate" App/Resources/brick.arm64 App/Resources/expected.json
