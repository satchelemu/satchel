#!/usr/bin/env python3
"""Select an installed Metal toolchain for the native-game development build.

Xcode can select a newer Metal asset even when an older compiler remains mounted.
This shim executes the requested compiler, never changes its version output or
the upstream fingerprint lock. Other tools still use Apple's xcrun unchanged.
Install as an isolated tools directory's bin/xcrun, not in a system directory.
"""
import os
from pathlib import Path
import subprocess
import sys


def main():
    args = sys.argv[1:]
    root = os.environ.get("SATCHEL_METAL_TOOLCHAIN")
    if not root:
        os.execv("/usr/bin/xcrun", ["xcrun", *args])
    sdk = None
    find = False
    index = 0
    while index < len(args):
        arg = args[index]
        if arg in ("--sdk", "-sdk") and index + 1 < len(args):
            sdk = args[index + 1]
            index += 2
        elif arg in ("--find", "-f"):
            find = True
            index += 1
        elif arg in ("--run", "-r", "--no-cache", "-n"):
            index += 1
        else:
            break
    if index >= len(args) or args[index] not in ("metal", "metallib", "metal-tt"):
        os.execv("/usr/bin/xcrun", ["xcrun", *args])
    tool = Path(root) / "usr/bin" / args[index]
    if not tool.is_file() or not os.access(tool, os.X_OK):
        sys.exit("Configured Metal toolchain is unavailable; reselect the installed asset.")
    if find:
        print(tool)
        return
    env = os.environ.copy()
    if sdk:
        result = subprocess.run(["/usr/bin/xcrun", "--sdk", sdk, "--show-sdk-path"],
                                capture_output=True, text=True, check=True)
        env["SDKROOT"] = result.stdout.strip()
    os.execve(str(tool), [str(tool), *args[index + 1:]], env)


if __name__ == "__main__":
    main()
