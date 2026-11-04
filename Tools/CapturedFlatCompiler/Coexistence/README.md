# NativeAOT facade coexistence checkpoint

The two **unchanged production** macOS libraries coexist in the isolated host
fixture tested here. Both direct linking and explicit-handle `dlopen` calls pass
fresh cold, failure/retry, nested cross-runtime callback, ownership and concurrent
translation checks. An iOS17 executable links both existing framework images.
It was not executed, packaged, signed for a phone, or installed. These results do
not establish iOS runtime coexistence or game rendering.

## Application linkage at the original checkpoint

At the initial checkpoint, `project.yml`/the generated Xcode project linked and embedded only the existing
`SatchelShaderProbe.framework` with CodeSignOnCopy. Its production API is the
resource-free probe, runtime-feature query and original fixed compute fixture.
`Core/GraphicsDiagnostic.m` uses the probe; other original diagnostic code uses
the compute fixture. `SatchelCapturedFlat.framework` is a separately built,
standalone artifact and is not added to the App by this task.

Both products use installed .NET10.0.403 tooling/NativeAOT10.0.14. The installed
ILCompiler package repository pin is
`dotnet/dotnet@0427b9abbead3fa64c6655eb4290c537397131fa`. Its Unix target selects
`libRuntime.WorkstationGC.a` for each ordinary NativeAOT product and restricts
Apple exports to the generated list. Both actual Mach-O images use TWOLEVEL
namespaces. Their sole overlapping exported symbol is `DotNetRuntimeDebugHeader`;
all application exports are distinct. Handle-scoped symbol lookup and `dladdr`
prove the calls reach different intended images, and each image exposes a
separate debug-header address. This is consistent with separate self-contained
runtime instances; the test does not measure GC heap sizes or collection counts.
No managed object, native runtime allocation, GC handle or exception crosses
between libraries, only C scalars and caller-owned copied buffers.

## Checks

`Coexist.c` independently constructs original compute and VertexB/fragment
programs from public decoder bitfields. Host facts for the flat fixture are
explicit original-test assumptions, not a complete live GPU capability binding.
No source or precompiled SPIR-V from a game is used.

There are twelve fresh processes: dynamic loading and direct linkage, each with
old-first, new-first, nested-first, cold-cancel, cold-limit and cold-invalid order.
No managed test export, runtime-feature query or warm-up precedes the first real
production operation. In nested-first, the new facade calls a borrowed native
callback which enters the old production facade before its first managed call.
Both valid and invalid calls retain the other library's output/reference state.
The nested compile is deliberate adversarial lifetime stress beyond the pure
cancellation-callback contract; it does not authorize production callbacks to
start other compiler work. Application cancellation callbacks should only read
their bounded cancellation state.

Every process runs512 parallel compiles across two libraries on separate native
threads, then a cross-runtime nested callback on a512KiB-stack worker. Inputs and
callback context placed on three caller-owned pages become PROT_NONE after the
return; subsequent compiles still succeed without revisiting those pages. The
caller retains output bytes independently. This tests borrowed-pointer lifetime
and failure cleanup without exchanging managed references. Libraries are kept
loaded for process lifetime; there is deliberately no unload test.

The native C caller uses ASan/UBSan; managed NativeAOT code retains runtime checks
but is not ASan-instrumented. Leak detection is disabled because the retained
runtime/library lifetime is intentional. No whole-runtime leak/GC-stress proof
or CPU/GPU scheduling guarantee is claimed.

## Recommendation

For the next **device validation**, keep the two existing shared frameworks
separate and preserve their unique bundle names/install names and current C ABI
ownership. This minimizes changes to the already validated probe and the new
bounded compiler. Load/link them for the whole process; never `dlclose` either.
The iPhone test still needs actual embed/sign/launch, both cold orders, original
compute/flat GPU diagnostics, worker/cancellation behavior and background cleanup.
No App edit or phone test is included here.

A single newly published NativeAOT assembly could avoid duplicate compiler/runtime
images later, but it is a separate engineering change: the two current facades
use differently instrumented translator copies. Simply compiling both copies into
one project creates duplicate type identities; using only the new copy would
require scoped WorkBudget integration and full legacy API regression. Do not
merge the two precompiled NativeAOT static archives into one image. Neither code
size nor RAM/performance improvement from consolidation has been measured.

## Primary references

Microsoft's [NativeAOT library documentation](https://learn.microsoft.com/en-us/dotnet/core/deploying/native-aot/libraries)
explains self-contained shared libraries and unsupported unloading. Its official
[native library sample](https://github.com/dotnet/samples/blob/main/core/nativeaot/NativeLibrary/README.md)
specifically excludes combining multiple NativeAOT static libraries in one
loadable module while distinguishing shared libraries. These are guidance, not
an iOS compatibility promise. Installed package `.nuspec` and Unix/native targets
are hashed in the evidence manifest to identify the actual10.0.14 implementation.

## Reproduce

```
python3 build.py --source-root '/path/to/satchel' \
  --old-checkpoint /tmp/satchel-shader-cold-staging \
  --new-build /tmp/satchel-captured-flat-build-oim8hg99 \
  --output /tmp/my-new-coexistence-gate
```

This compiles only original C tests and the iOS link-only probe using installed
artifacts; it does not rebuild either framework, download packages, edit App
sources, or access a device. `validation-manifest.json` binds producer manifests,
headers, actual binary hashes, test logs, export/install-name inspection and
source inputs before/after the gate. Existing libraries remain unmodified.

The output directory must be new and outside the source tree; omission creates a
fresh `/tmp/satchel-nativeaot-coexist-build-*` directory. `--old-framework` and
`--ilcompiler-package` can identify relocated installed artifacts. Their bytes
still must match the recorded producer evidence. `--compiler-source-root` can
identify the exact frozen V1 source checkpoint if the repository has since moved
to a newer compiler revision.

An explicit `--new-layout host-facts-v2 --new-build /path/to/host-facts-stage`
validates the newer producer's complete source map, source lock, source delta and
actual artifacts before testing. It still calls the original V1 entry point in
that binary; the separate ABI2 validation gate owns provenance/normalization
coverage. There is no automatic fallback between incompatible manifest layouts.
All host facts in this coexistence test remain original fixture assumptions.
