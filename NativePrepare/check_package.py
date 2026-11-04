#!/usr/bin/env python3
"""Use the common app-verifier harness, including its unsigned rejection check."""
from pathlib import Path
import runpy
runpy.run_path(str(Path(__file__).resolve().parents[1] / 'NativeHost/check_package.py'), run_name='__main__')
