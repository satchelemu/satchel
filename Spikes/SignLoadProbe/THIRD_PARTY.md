# Probe dependencies

The signer reuses zhlynn/zsign, MIT license, commit
`9299685bab7ef99827e06e5dfda93af747386590` from
https://github.com/zhlynn/zsign. The copied subset is in `../../NativePrepare/Signer/Vendor/zsign`; its
license is included. It supplies Mach-O signature allocation, requirements,
SHA-256 CodeDirectory page hashes, XML/DER entitlement serialization and CMS
with Apple's hash agility attributes. Dylibs select no entitlements, empty XML plus DER, or XML only for the signing matrix.

Local adaptations add an in-memory PKCS#12 importer with certificate OU/team
and validity checks, private key cleanup, captured diagnostic text, CMS cleanup,
Mach-O mapping cleanup, a certificate-pinned designated requirement for
self-issued format fixtures, and namespaced requirement constants to coexist with
Apple SDK headers. The matrix adaptation adds per-signature 4 KiB/16 KiB page
selection, optional dylib XML/DER slots, DER special-slot hashing for dylibs,
and per-image exec-segment file offset and size fields. No zsign CLI, bundle rewriter or archive code is included.

OpenSSL 3.6.3, Apache License 2.0, supplies CMS, PKCS#12 and crypto primitives.
Source: https://github.com/openssl/openssl/releases/tag/openssl-3.6.3.
Its license is `../../NativePrepare/Signer/Vendor/OpenSSL-LICENSE.txt`. The static iOS build and downloaded
source stay under `~/SatchelBuild/spike/`, outside this repository.
The Mac format check links the installed Homebrew OpenSSL 3.6.3 static library.
No user certificate, private key, PKCS#12 file, or passphrase is a build input.

The remap experiment implements the documented Mach VM API call shape after
reading NX2Apple `dm/host/loader.c` `remap_at`. No NX2Apple code is ported or linked.
