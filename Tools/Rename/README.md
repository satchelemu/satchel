# Satchel clean-break rename preparation

Run the default read-only plan on the final merged main after stopping all workers.
This is a transform of the current tracked tree, not a diff tied to today's line numbers.
It never commits, stages, visits other worktrees or changes git history.

```zsh
python3 -B Tools/Rename/rename_to_satchel.py
python3 -B Tools/Rename/rename_to_satchel.py --apply
python3 -B Tools/Rename/rename_to_satchel.py --check
python3 -B Tools/Rename/rename_to_satchel.py --apply
```

The final command must report zero changes, including unchanged modification times.
`--root /absolute/worktree` selects a throwaway tree. Every affected file is printed.
The remaining-name check is case insensitive. Unknown mixed casing, binary content
matches, collisions, changed layout/policy anchors and symlinks needing a target
change fail before writing. A binary with an old filename but no old-name content
can move without changing its bytes. Work only with stopped writers: preflight
catches observed concurrent edits but does not lock other editors or roll back a
hardware/write failure. The git index is untouched; review `git diff` and untracked
new paths together before the orchestrator stages anything.

The only old-name allowances are `Validation/history`, third-party directories
named `upstream`, `vendor`, `vendored`, `third_party` or `third-party`, license and
notice files/directories, and these five controller files: `rename_to_satchel.py`,
`test_rename.py`, `pl-symbols.json`, `README.md` and `PHONE.md` under `Tools/Rename`.
The controller needs the before-name literals to remain reusable. Exclusions are
byte-preserved. New unlisted files under `Tools/Rename` are scanned normally.
Untracked files are outside the transform. There are no preserved app identities,
settings keys, launch aliases, marker exports or old framework ABI names.

`pl-symbols.json` enumerates project-owned short prefixes found in Core, App, the
native host overlays and the owned Tools headers/implementations. Their paired
Swift/C call sites, `_pl_` linker spellings, header guards and constants are renamed
together to Satchel, satchel_ or SATCHEL_. The unrelated words `PLAN`, `PLIST`,
`PLT`, `PLAYERS`, `PLUS`, `PLATFORM_IOS` and `PLATFORM_MACOS` remain. A new unlisted PL identifier is
kept and printed for review rather than inferred from its first two letters.
Vendored identifiers are always excluded. The current policy has no ambiguous
project-owned symbol left unclassified; review any future branch's new entries.

The tool also rewrites the naming policy in AGENTS.md, GOALS.md and PLAN.md.
The Mach-O writer needs two structural updates in addition to its new strings:
its marker constant is now 20 bytes and the export trie terminal moves one byte.
The 40-byte trie assertion and independent Apple-linker parity check remain intact.
The local NativeRuntimeProfiles-to-NativeGameProfiles SHA-256 binding is updated
only after verifying the existing digest, because renamed profile comments change
the hashed bytes. Unexplained input drift refuses the entire plan. The 30 explicit repository-local bindings in `reviewed-bindings.json` are also
updated after checking their reviewed before/after source digests. The scalar and
captured compiler builders consume these files directly. A changed source or
manifest refuses the rename and requires re-review. Third-party pins, generated
fixture hashes, historical archive digests and validation assertions stay intact.
The VIXL patch also has two recomputed Git blob IDs; its full-diff check stays intact.

The native build drivers gain explicit `SATCHEL_BUILD_ROOT` and
`SATCHEL_DEPS_ROOT` inputs for isolated validation. Defaults use only the new paths,
with no fallback to old paths. The Mac Prepare driver also accepts
`SATCHEL_ARCHIVE_LIBRARY` for a freshly rebuilt archive bridge. `SATCHEL_OUT`,
`SATCHEL_HOST_OUT` and `SATCHEL_PREPARE_OUT` keep products outside the repository.

The tracked-tree transform does not itself rebuild binaries. The separate
`rebuild_dependencies.py` stages reference/tool inputs into a new root and
rebuilds project-owned code. Run it from a renamed checkout under the existing
resource guard. Do not point the destination at the original dependency root.
It clones only named tool/reference directories from the mixed snapshot, so
private game extractions are not carried into the new dependency tree.

```zsh
ANYGAME_OUT="$HOME/SatchelBuild/rename/final-guard" nice -n 10 \
  python3 -B NativePrepare/guard.py python3 -B Tools/Rename/rebuild_dependencies.py \
  --source "$PWD" --inputs "$HOME/SatchelBuild/rename/deps-original" \
  --build-inputs "$HOME/SatchelBuild" --destination "$HOME/SatchelDeps" \
  --nativeaot-packages "$HOME/SatchelBuild/rename/inputs/packages" \
  --moltenvk-inputs "$HOME/SatchelBuild/rename/inputs/moltenvk" --apply
```

The command above is an apply-day template, not authorization to move either
real root. The mover currently moves the original dependency root directly to
SatchelDeps. The orchestrator must preserve that root as a separate read-only
input before using the new destination. The dependency script refuses an
in-place rebuild. See the task report for the exact pending apply order.

Without `--apply`, the rebuild driver prints its destination and makes no writes.
`--step` selects stage, cpu, archives, scalar, cross, archive, shader-bridge or
frameworks or parity for resuming. CPU, scalar and shader bridge use their existing test
builders. The 31 C archives use reviewed source/member recipes and exact renamed
export sets. Their historical host/GPU suites are not represented as rerun.
Every compilation is recorded under `rebuild-logs`. The production Vulkan recipe
retains the existing official package, consumed-byte, source-pin and exact-export
checks. It does not claim that historical GPU test gates were rerun.

CapturedFlat keeps the checked-in vendor tree byte-identical. After validating
its original source lock, its builder stages those sources outside the repository
and renames only 406 project budget namespace references in 32 C# files. The
staged hashes are recorded and checked again after the full existing test gates.
No alias or old exported name is introduced. `--framework captured` or another
named recipe can resume a framework-specific fix without rerunning passing ones.

Framework builds require an existing offline NativeAOT package cache and the
pinned MoltenVK source, official tar files and extracted releases. Use
`--nativeaot-packages` and `--moltenvk-inputs` to supply approved local inputs.
`fetch_inputs.py` handles the explicitly approved one-time download. Its default
is offline verification, and only `--fetch` permits downloads. It checks the
existing runtime/source pins, NuGet author and repository signatures, the exact
MoltenVK Git revision, both official tar hashes and extracted payload bytes.
`inputs/manifest.json` records source URLs, sizes, hashes and verification results.
The seven NuGet packages are version 10.0.14: Microsoft.DotNet.ILCompiler,
Microsoft.NET.ILLink.Tasks, runtime.osx-arm64.Microsoft.DotNet.ILCompiler,
Microsoft.NETCore.App.Runtime.NativeAOT.ios-arm64,
Microsoft.NETCore.App.Runtime.NativeAOT.osx-arm64,
Microsoft.NETCore.App.Runtime.osx-arm64 and Microsoft.AspNetCore.App.Runtime.osx-arm64.
These are the existing SDK/publish restore closure, not new project references.
The framework preflight refuses missing inputs. The build uses a cleared package
source list, so it cannot fetch missing packages during compilation.

Before moving roots, run the fetch command once with the explicitly approved
`--fetch`, using the current task input path and staged .NET executable. On apply
day, reuse the relocated inputs and run this offline command before rebuilding:

```zsh
python3 -B Tools/Rename/fetch_inputs.py --source "$PWD" \
  --inputs "$HOME/SatchelBuild/rename/inputs" \
  --dotnet "$HOME/SatchelBuild/rename/deps-original/tmp-snapshot-2026-11-01/tmp/pocket-switch-integration/dotnet/dotnet"
```

The old snapshot component in that command belongs to the preserved original
dependency root. The builder stages it under the new name before use. No root
move or fetch is performed by the offline command.
The final all-step inventory requires all 37 artifacts. The game translator and
both shader bridges are rebuilt separately under the destination's `m5` folder.
`SATCHEL_SHADER_DEPS` selects that folder for the app build. The prior M5 parity
receipt is not copied as fresh evidence. The all-step route builds the translation
harness and runs the existing full parity gate into a fresh pipeline-full output.
Use `--step parity --resume-parity` only for an unchanged interrupted checkpoint.
Its existing implementation/input fingerprints still reject drift. See the task
report for the actual framework, parity and app validation results.

Never relabel an old binary or add ABI aliases. External pinned code and its
license notices remain unchanged. Compiler parallelism is limited to two jobs.
The CPU patch is first checked against the original reviewed diff, then the
rename is applied in the cloned VIXL tree. Neither original dependency files nor
registered worktrees are modified by the dependency rebuild.

`audit_app.py` checks the built product, including wide strings. Its report lists
every old-name string even when it is an accepted exception. Temporary trial
paths require an explicit `--build-path`. The two preserved provenance documents
in `preserved-notices.json` are accepted only at their exact original SHA-256
values. Their three historical name lines remain visible in the report. A new
notice, changed bytes, filename, plist or exported-name match still fails.
The existing linked OpenSSL archive also contains seven historical install-prefix
defaults. `--openssl-archive /path/to/libcrypto.a` records its hash and accepts
only those exact configured-path strings present in that archive. They remain
listed as preserved third-party binary provenance, not temporary trial paths.
Moving roots does not rewrite them. Rebuilding that vendor library is separate
from the project-owned rename dependency recipes.

```zsh
python3 -B Tools/Rename/test_rename.py
```

The external `move-build-folders.sh` has dry-run, apply and undo modes, process
refusal, checksummed backups, changed-file undo refusal and worktree repair. Its
real apply must run from an ordinary terminal after Codex and build tools exit.
There is no symlink at either old root. The task tested apply and undo only with
synthetic home/repository fixtures, never the user's real roots.

Read `PHONE.md` before removing old apps. The new IDs start with
`com.dell.satchel`, with no access to the old app's credentials or prepared library.
Save ZIPs are backed up and restored separately. Keys, certificate import with the
user-entered passphrase, game import and Prepare must be repeated by the user.
The external `~/PocketLabBuild/rename/report.md` (then `~/SatchelBuild/rename/report.md`
after relocation) records the current validation,
remaining blocker and exact orchestrator apply order.
