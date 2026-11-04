"""Mac-only oracle checks. No content or keys are printed. Outputs stay private."""
import argparse
import hashlib
import subprocess
import sys
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument("--tools", type=Path, required=True, help="NX2Apple engine/dm/tools")
p.add_argument("--synthetic", type=Path)
p.add_argument("--reference", type=Path, help="Private reference extract/v720896")
p.add_argument("--native", type=Path, help="Private native parity outputs")
a = p.parse_args()
sys.path.insert(0, str(a.tools.resolve()))

if a.synthetic:
    import romfs_image
    root = a.synthetic / "oracle-tree"
    subprocess.run(["xattr", "-w", "org.d2rmobile.guest-name", "a", str(root / ".dm-case-0")], check=True)
    # APFS normalizes host names. Preserve the fixture's original UTF-8 through
    # the same guest-name xattr mechanism used by upstream extraction.
    subprocess.run(["xattr", "-w", "org.d2rmobile.guest-name", "é", str(root / "z/.dm-case-1")], check=True)
    expected = a.synthetic / "oracle.img"
    romfs_image.pack(root, expected)
    assert (a.synthetic / "tree.img").read_bytes() == expected.read_bytes()
    print("PASS synthetic DMROMFS1 byte parity: nested/empty directories, case collision, UTF-8, empty and multi-chunk files")

if a.reference or a.native:
    assert a.reference and a.native
    import nso
    for name in ("rtld", "main", "subsdk0", "sdk"):
        module = nso.read(a.reference / "ExeFS" / name)
        for i, (offset, size) in enumerate(module.segments):
            actual = (a.native / f"{name}.segment{i}").read_bytes()
            assert len(actual) == size
            assert hashlib.sha256(actual).digest() == hashlib.sha256(module.image[offset:offset + size]).digest()
        print(f"PASS independent NX2Apple decoded segment parity {name}: segments=3")
