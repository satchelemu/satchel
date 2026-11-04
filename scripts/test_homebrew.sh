#!/bin/zsh
set -euo pipefail
TASK_ROOT="${0:A:h:h}"
TASK_BUILD="$(mktemp -d /tmp/satchel-nro-tests.XXXXXX)"
trap 'rm -rf "$TASK_BUILD"' EXIT
cd "$TASK_ROOT"
python3 scripts/build_homebrew.py
xcrun clang -target aarch64-none-elf -DGUEST_PROBE -c Tests/arm64_probe.S -o "$TASK_BUILD/probe.o"
python3 - "$TASK_BUILD/probe.o" "$TASK_BUILD/probe.bin" <<'PY'
import pathlib,struct,sys
b=pathlib.Path(sys.argv[1]).read_bytes()
off=struct.unpack_from('<Q',b,40)[0]
ent,n,si=struct.unpack_from('<HHH',b,58)
sections=[struct.unpack_from('<IIQQQQIIQQ',b,off+i*ent) for i in range(n)]
s=sections[si]; names=b[s[4]:s[4]+s[5]]
for s in sections:
    assert s[1] not in (4,9) or s[5]==0
    if names[s[0]:].split(b'\0',1)[0]==b'.text': pathlib.Path(sys.argv[2]).write_bytes(b[s[4]:s[4]+s[5]])
PY
xcrun clang -c Tests/arm64_probe.S -o "$TASK_BUILD/native.o"
xcrun clang -std=c11 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Wall -Wextra -Werror -I Core Core/Interpreter.c Core/NROLoader.c \
  Tests/architecture.c "$TASK_BUILD/native.o" -o "$TASK_BUILD/architecture"
"$TASK_BUILD/architecture" "$TASK_BUILD/probe.bin"
xcrun clang -std=c11 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Wall -Wextra -Werror -I Core Core/Interpreter.c Core/NROLoader.c \
  Tests/homebrew.c -o "$TASK_BUILD/homebrew"
"$TASK_BUILD/homebrew" App/Resources/hello-horizon.nro "$@"
