#!/bin/zsh
set -euo pipefail
probe_root=${0:A:h:h}
probe_out="$HOME/SatchelBuild/spike2/format"
python3 - "$probe_out" <<'PY'
from pathlib import Path
import struct
import sys
root = Path(sys.argv[1])
code = bytearray((root / 'selfsigned-S1d-A.dylib').read_bytes())
code[0x4000] ^= 1
(root / 'tampered-code.dylib').write_bytes(code)
der = bytearray((root / 'selfsigned-S1d-C.dylib').read_bytes())
cursor = 32
for _ in range(struct.unpack_from('<I', der, 16)[0]):
    cmd, size = struct.unpack_from('<II', der, cursor)
    if cmd == 0x1d:
        sigoff = struct.unpack_from('<I', der, cursor + 8)[0]
    cursor += size
for index in range(struct.unpack_from('>I', der, sigoff + 8)[0]):
    slot, offset = struct.unpack_from('>II', der, sigoff + 12 + 8 * index)
    if slot == 7:
        der[sigoff + offset + 8] ^= 1
(root / 'tampered-der.dylib').write_bytes(der)
PY
for probe_kind in code der; do
  probe_file="$probe_out/tampered-$probe_kind.dylib"
  if python3 "$probe_root/Scripts/check-signatures.py" "$probe_file" > "$probe_file.structure.log" 2>&1; then
    print -u2 "FAIL: structure checker accepted tampered $probe_kind"
    exit 1
  fi
  if codesign --verify --strict --verbose=4 "$probe_file" > "$probe_file.verify.log" 2>&1; then
    print -u2 "FAIL: codesign accepted tampered $probe_kind"
    exit 1
  fi
  print "PASS: both checkers rejected tampered $probe_kind"
done
