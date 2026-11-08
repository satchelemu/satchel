#!/usr/bin/env python3
"""Default dependency locations for build tools and checks.

Every tool that reads the engine snapshot, the native build inputs or the shader
products asks this module, so all of them follow one rule. An explicit variable always
wins. Otherwise the dependency root is SATCHEL_DEPS_ROOT, or ~/SatchelDeps only when that
folder is a bootstrap root materialized at the current engine pin. Anything else stops
with the variable to set; nothing falls back to an older materialization.

  SATCHEL_DEPS_ROOT            dependency root (scripts/bootstrap_deps.py --root)
  SATCHEL_NATIVE_BUILD_INPUTS  default <root>/build-inputs
  SATCHEL_SHADER_DEPS          default <root>/m5
  SATCHEL_ARCHIVE_LIBRARY      default <shader deps>/archive-<platform>/libShaderArchive.a
  SATCHEL_MAC_HOST_GEN         default <build inputs>/host-gen (Mac session probes only)
  SATCHEL_OPENSSL_IOS          default <root>/openssl-ios (the iOS libcrypto the app links)

Outputs are never written to a shared default. The NativeShaders harness builders and
checks write to the caller's own work folder:

  SATCHEL_SHADER_WORK          default <SATCHEL_OUT>/native-shaders-work; no default without SATCHEL_OUT

From a shell: `python3 -B scripts/dependency_paths.py <name> [platform]` prints one path
(names: deps-root, engine, build-inputs, shader-deps, integration-tools, mac-host-gen,
archive-library, openssl-ios, shader-work) or exits 1 with the instruction.
"""
import json
import os
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
PIN_FILE = ROOT / 'scripts/deps/upstream/nxnative-queue/.upstream-ref'
DEFAULT_ROOT = Path.home() / 'SatchelDeps'
SETUP = ('set SATCHEL_DEPS_ROOT to a dependency root made by scripts/bootstrap_deps.py at that pin '
         '(and SATCHEL_SHADER_DEPS when the shader products live elsewhere), for example by sourcing '
         'the environment script that sets them; see BUILDING.md, "Native dependencies the build expects"')


class DependencyPathError(SystemExit):
    """A default that cannot be used. Exits with the message when uncaught."""


def pin():
    return PIN_FILE.read_text().strip()


def materialized_pin(root):
    """The engine commit a bootstrap root records, or None when it records none."""
    for name in ('NX2Apple/engine-materialization.json', 'build-inputs/engine-materialization.json'):
        path = Path(root) / name
        if path.is_file():
            try:
                return json.loads(path.read_text()).get('pin')
            except (OSError, ValueError):
                return None
    return None


def deps_root():
    value = os.environ.get('SATCHEL_DEPS_ROOT')
    if value:
        return Path(value)
    found = materialized_pin(DEFAULT_ROOT)
    if found == pin():
        return DEFAULT_ROOT
    state = ('records engine ' + found[:8]) if found else 'records no engine materialization'
    raise DependencyPathError(f'SATCHEL_DEPS_ROOT is not set and the default {DEFAULT_ROOT} {state}, '
                              f'not the pinned engine {pin()[:8]}: {SETUP}.')


def engine():
    return deps_root() / 'NX2Apple/engine'


def integration_tools():
    """The interpreter-era snapshot (tool venv, .NET) inside the dependency root."""
    return deps_root() / 'tmp-snapshot-2026-11-01/tmp/satchel-switch-integration'


def build_inputs():
    value = os.environ.get('SATCHEL_NATIVE_BUILD_INPUTS')
    return Path(value) if value else deps_root() / 'build-inputs'


def shader_deps():
    value = os.environ.get('SATCHEL_SHADER_DEPS')
    return Path(value) if value else deps_root() / 'm5'


def mac_host_gen():
    """Generated inputs of the Mac session probes: a game profile header, msl-macosx and vpx/libvpx.a."""
    value = os.environ.get('SATCHEL_MAC_HOST_GEN')
    path = Path(value) if value else build_inputs() / 'host-gen'
    if not path.is_dir():
        raise DependencyPathError(f'No Mac host generation folder at {path}: set SATCHEL_MAC_HOST_GEN to a host-gen '
                                  f'folder generated from the pinned engine {pin()[:8]} (one made at an older pin '
                                  'only builds a probe of that older engine).')
    return path


def archive_library(platform='macosx'):
    value = os.environ.get('SATCHEL_ARCHIVE_LIBRARY')
    path = Path(value) if value else shader_deps() / ('archive-' + platform) / 'libShaderArchive.a'
    if path.is_file():
        return path
    if value:
        raise DependencyPathError(f'No shader archive library at {path}, the path SATCHEL_ARCHIVE_LIBRARY names: '
                                  'point it at an existing libShaderArchive.a, or unset it to use SATCHEL_SHADER_DEPS.')
    raise DependencyPathError(f'No shader archive library at {path}: set SATCHEL_SHADER_DEPS to the shader products '
                              f'of the current dependency root (they hold archive-{platform}/libShaderArchive.a), '
                              'or SATCHEL_ARCHIVE_LIBRARY to the library itself; see BUILDING.md.')


def openssl_ios():
    """The OpenSSL 3.6.3 iOS build (lib/libcrypto.a, include/) that the app links."""
    value = os.environ.get('SATCHEL_OPENSSL_IOS')
    path = Path(value) if value else deps_root() / 'openssl-ios'
    if (path / 'lib/libcrypto.a').is_file():
        return path
    named = 'the path SATCHEL_OPENSSL_IOS names' if value else 'the dependency root\'s default'
    raise DependencyPathError(f'No iOS OpenSSL at {path} ({named}): set SATCHEL_OPENSSL_IOS to an OpenSSL 3.6.3 iOS '
                              'build holding lib/libcrypto.a (the bootstrap makes <root>/openssl-ios; '
                              'NativePrepare/Signer/build-openssl.zsh makes one too); see BUILDING.md.')


def shader_work():
    """Where the NativeShaders harness builders and checks write: harnesses, results, guard samples.

    Never the shared shader products folder by default: an explicit SATCHEL_SHADER_WORK, else a
    folder inside the caller's own SATCHEL_OUT, else stop. The products stay read-only inputs."""
    value = os.environ.get('SATCHEL_SHADER_WORK')
    if value:
        return Path(value)
    out = os.environ.get('SATCHEL_OUT')
    if out:
        return Path(out) / 'native-shaders-work'
    raise DependencyPathError('The NativeShaders builders and checks write their harnesses and results to a work '
                              'folder of your own: set SATCHEL_OUT to your build folder (they then use '
                              '<SATCHEL_OUT>/native-shaders-work) or SATCHEL_SHADER_WORK to the folder itself. '
                              'Nothing writes to the shader products (SATCHEL_SHADER_DEPS) by default.')


COMMANDS = {'deps-root': deps_root, 'engine': engine, 'build-inputs': build_inputs,
            'shader-deps': shader_deps, 'integration-tools': integration_tools,
            'mac-host-gen': mac_host_gen, 'archive-library': archive_library,
            'openssl-ios': openssl_ios, 'shader-work': shader_work}

if __name__ == '__main__':
    if len(sys.argv) < 2 or sys.argv[1] not in COMMANDS:
        raise SystemExit('usage: dependency_paths.py ' + '|'.join(COMMANDS) + ' [platform]')
    print(COMMANDS[sys.argv[1]](*sys.argv[2:]))
