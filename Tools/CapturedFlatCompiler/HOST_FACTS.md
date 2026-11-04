# Live device and source-proved host facts (scratch ABI2)

This prototype adapter resolves the24 CapturedFlat host-query facts for the exact
bounded flat VS/FS profile. It is not a general physical-device capability layer.
`host-facts-map.json` records every query, classification, actual consumer and
source hash. The host gate performs no App or phone action.

The live same-parent device/target query returns three disjoint masks:

- device_known=8: actual physical BGRA8 color-attachment support and an actual
  live native RGBA8 target justify the upstream no-remap contract;
- policy_known=16777201: precision reduction is disabled; unsupported renderer
  mechanisms and disabled optional features are false for this implemented
  profile, not assertions that hardware lacks them;
- unknown=6: FrontFacingBug and VectorIndexingBug remain unknown device facts.

The current pinned factory passes no feature structs/pEnabledFeatures to
vkCreateDevice and enables only portability_subset. Features advertised by a
physical device are not thereby enabled. Ballot, barrier-divergence, textures,
scaled formats and separate samplers also remain unsupported in the admitted
renderer profile; those policy decisions are distinct from feature-enable bits.
A changed factory/schema/profile requires this snapshot's policy version/lock to
be reviewed. Storage alignment is actual; outside16..65536 stops explicitly.
Physical BGRA absence, non-graphics queue, wrong/stale device/target or unsupported
format stops. No guessed current-target format is accepted.

`SatchelCFInputV2` carries explicit device/policy/requested-unused provenance. Exactly
the current two unknown bug bits may be requested as source-unused. The compiler
copies and fully admits both actual source programs under the same gate/budget
as v1 before normalization. StrictSource only permits user attribute/varying
ALD/AST/IPA, MOV and EXIT; no FrontFacing input, constant-buffer access or dynamic
index can pass. The two pinned consumers are therefore provably irrelevant.
Only then are their internal accessor values normalized false; the output reports
`proven_source_unused=6` separately. This is not an observed bug-free device claim.
Any other unknown/overlapping provenance or unsupported source refuses without
changing output. v1's public entry/header/requirements remain unchanged.

`satchel_captured_compile_for_target` constructs ABI2 from the actual live backend
snapshot; callers cannot supply host masks or host values. Actual recorded shader
source/state remain caller inputs. Caller must own/serialize device, target and
source spans for the synchronous call; cancellation is pure. Nothing retains CPU
or resource pointers after return. New ABI2 input/output and adapter/facts outputs
require their natural8-byte alignment, checked before typed access. Compilation
is cooperative and bounded, not hard realtime. GPU pipeline/admission/submission
still occurs through the existing independent typed renderer and real fences.

The original host journey supplies32 independently encoded source pairs and
explicit original raster state, then creates pipelines from actual production
compiler output. No precompiled SPIR-V substitution, imported game shader,
window/presentation or phone execution is claimed. Counts include pixel/guard
assertions rather than independent test scenarios.

Reproduce the compiler with its existing `build.py --renderer-root <repo>` and
the backend with `scripts/test_vulkan_flat.sh --ios`. Then run:

```
zsh scripts/test_captured_host_facts.sh --ios \
  --compiler-build /tmp/<validated-compiler-output> \
  --renderer-build /tmp/<validated-flat-output> \
  --output /tmp/<new-host-facts-output>
```

The optional `--renderer-source-root` supports a relocated reviewed renderer tree.
The recipe verifies both validated production artifacts and exact consumed source
locks before use, preserves all source/canonical outputs, and writes only to a
marked owned `/tmp` directory. It produces `libSatchelCapturedHostAdapter.a` for
iOS17 with one public export. The canonical compiler recipe retains all v1 cold
and hostile-source tests and packages both public headers/entries. The independent
v1 live GPU recipe remains a separate compatibility gate with unchanged tests.

Full translator/MoltenVK licenses, source pins and private upstream snapshot
remain unchanged. ABI2 adds no dependency and no general shader compatibility.

Primary Vulkan feature-selection contract:
https://docs.vulkan.org/refpages/latest/refpages/source/VkDeviceCreateInfo.html
Pinned query semantics and proofs are linked by exact file hashes in the map.
