# Homebrew files and containers: October 31, 2026

| Content | Role | Satchel handling |
| --- | --- | --- |
| NRO0 (`.nro`) | Relocatable executable used by Switch homebrew; may include assets | Candidate for full NRO validation and interpreter execution; compatibility depends on its code and service needs |
| PFS0 (`.nsp`, also other uses) | Container directory; NSP is a package, not a flat NRO executable | Recognized and rejected with an explicit unsupported-container message |
| XCI (`.xci`) | Gamecard image containing partitions and program/content archives | Recognized and rejected with an explicit unsupported-image message |
| NSO0 (`.nso`) | Another Switch executable/module format, with potentially compressed segments | Recognized; NSO loading is not implemented |
| ZIP (`.zip`) | Download/archive wrapper | Recognized; extract in Files and choose the NRO inside |

An extension is not evidence of homebrew provenance or compatibility. An NSP
can package homebrew, and renaming a container to `.nro` does not convert it.
Conversely, NRO bytes with an incorrect filename extension can still be validated
as NRO. PFS0 is used beyond NSP, so the diagnostic deliberately says PFS0 rather
than claiming every PFS0 file is an installable package.

Container detection is only a signature check. It does not validate the entries,
extract content, verify signatures, decrypt archives, or claim that the contents
can run. Supporting NSP/XCI execution would require the relevant bounded
container/content loaders and executable/runtime support; adding extensions to
the picker would not provide those capabilities. No keys are needed for the
current signature detection, and no key acquisition is implemented.

## Integration

`Core/HomebrewFormat.h/.c` expose:

```c
SatchelHomebrewFormat satchel_homebrew_format(const uint8_t *bytes, size_t size);
const char *satchel_homebrew_format_error(SatchelHomebrewFormat format);
```

Read at most `SATCHEL_HOMEBREW_PROBE_SIZE` bytes under the existing security-scoped
coordinated file read. Reject any result other than `SATCHEL_FORMAT_NRO` with the
returned diagnostic. Then apply the existing whole-file size limit, rewind,
read the complete NRO, and call `satchel_nro_inspect`. Probe before the NRO size check
so a large XCI gets a useful format message without reading the full image.
Keep the whole-file NRO validation; a recognized signature alone is not enough.

Add `#include "HomebrewFormat.h"` to `App/Bridge.h`, replace the importer's
extension-only guard with this content check, and regenerate with XcodeGen.
The existing `Core` source directory entry includes the new C source. Keep
the Files picker broad enough to select containers and report the actual reason
they are unsupported. Do not advertise NSP/XCI as executable formats.

## Validation

`zsh scripts/test_formats.sh` builds with AddressSanitizer and
UndefinedBehaviorSanitizer. It tests every short-buffer boundary up to each
magic offset (including XCI with a 4 KiB key-area prefix), misplaced signatures,
outer-container precedence, malformed NRO handoff to the structural validator,
null input, diagnostic routing, and the same valid NRO with `.nsp`, `.xci`, and
no extension. Fixtures are synthetic headers plus the project's original NRO;
these are not tests of real NSP/XCI game execution.

## Sources

- [libnx NRO definitions](https://github.com/switchbrew/libnx/blob/master/nx/include/switch/nro.h): 16-byte start structure before NRO0 magic.
- [hactool PFS0 definitions](https://github.com/SciresM/hactool/blob/master/pfs0.h): PFS0 header and directory entries.
- [Switchbrew PFS0 layout](https://switchbrew.org/wiki/NCA#PFS0): container header and file-data layout.
- [hactool XCI header](https://github.com/SciresM/hactool/blob/master/xci.h): normal image header has HEAD at offset 0x100.
- [Switchbrew XCI structure](https://switchbrew.org/wiki/XCI): gamecard layout, including the raw key-area prefix.
- [Switchbrew NSO layout](https://switchbrew.org/wiki/NSO): NSO0 magic and module segment format.

Sources reviewed October 31, 2026. This is a new, small detector written for this
project; no implementation from the referenced tools was copied or linked.
