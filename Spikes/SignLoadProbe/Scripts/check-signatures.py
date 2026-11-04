#!/usr/bin/env python3
"""Check this probe's thin ARM64 signatures independently of the signer."""
import hashlib
import json
from pathlib import Path
import plistlib
import struct
import sys


def inspect(path, variant=None):
    data = path.read_bytes()
    assert struct.unpack_from('<I', data)[0] == 0xFEEDFACF, 'expected thin Mach-O 64'
    assert struct.unpack_from('<I', data, 12)[0] == 6, 'expected dylib'
    cursor = 32
    text = None
    signature = None
    for _ in range(struct.unpack_from('<I', data, 16)[0]):
        cmd, size = struct.unpack_from('<II', data, cursor)
        assert size >= 8 and cursor + size <= len(data), 'load command bounds'
        if cmd == 0x19 and data[cursor + 8:cursor + 24].rstrip(b'\0') == b'__TEXT':
            text = struct.unpack_from('<QQ', data, cursor + 40)
        if cmd == 0x1D:
            signature = struct.unpack_from('<II', data, cursor + 8)
        cursor += size
    assert text is not None and signature is not None, 'missing TEXT/signature'
    sigoff, sigsize = signature
    assert sigoff + sigsize == len(data), 'signature allocation bounds'
    sb = data[sigoff:sigoff + sigsize]
    magic, length, count = struct.unpack_from('>III', sb)
    assert magic == 0xFADE0CC0 and 12 + count * 8 <= length <= sigsize, 'SuperBlob header'
    blobs = {}
    ranges = []
    for index in range(count):
        slot, offset = struct.unpack_from('>II', sb, 12 + 8 * index)
        assert slot not in blobs and offset >= 12 + count * 8, 'duplicate/overlapping slot'
        magic, size = struct.unpack_from('>II', sb, offset)
        assert size >= 8 and offset + size <= length, 'blob bounds'
        blobs[slot] = sb[offset:offset + size]
        ranges.append((offset, offset + size))
    ranges.sort()
    assert all(a[1] <= b[0] for a, b in zip(ranges, ranges[1:])), 'overlapping blobs'
    assert set(blobs).issuperset({0, 2, 0x10000}), 'missing CD, requirements, or CMS'
    assert struct.unpack_from('>I', blobs[2])[0] == 0xFADE0C01, 'requirements magic'
    assert struct.unpack_from('>I', blobs[0x10000])[0] == 0xFADE0B01, 'CMS wrapper magic'
    cd = blobs[0]
    names = ['magic', 'length', 'version', 'flags', 'hashOffset', 'identOffset',
             'nSpecialSlots', 'nCodeSlots', 'codeLimit', 'hashSize', 'hashType',
             'platform', 'pageSize', 'spare2', 'scatterOffset', 'teamOffset',
             'spare3', 'codeLimit64', 'execSegBase', 'execSegLimit', 'execSegFlags']
    fields = dict(zip(names, struct.unpack_from('>9I4B4I4Q', cd)))
    assert fields['magic'] == 0xFADE0C02 and fields['version'] == 0x20400, 'CodeDirectory header'
    assert fields['length'] == len(cd), 'CodeDirectory length'
    assert fields['hashType'] == 2 and fields['hashSize'] == 32, 'expected SHA-256 only'
    assert fields['flags'] == 0 and fields['platform'] == 0, 'unexpected flags/platform'
    assert all(fields[n] == 0 for n in ['spare2', 'scatterOffset', 'spare3', 'codeLimit64']), 'unexpected extension'
    # Version 0x20400: scatterOffset starts at 44, execSegBase at 64, not 72.
    assert (fields['execSegBase'], fields['execSegLimit'], fields['execSegFlags']) == (*text, 0), 'exec segment mismatch'
    assert fields['codeLimit'] == sigoff, 'codeLimit differs from signature offset'
    for name, offset_name in [('identifier', 'identOffset'), ('team', 'teamOffset')]:
        start = fields[offset_name]
        assert 88 <= start < fields['hashOffset'] - fields['nSpecialSlots'] * 32, 'string offset'
        fields[name] = cd[start:cd.index(b'\0', start)].decode()
    page_bytes = 1 << fields['pageSize']
    assert fields['nCodeSlots'] == (sigoff + page_bytes - 1) // page_bytes, 'page count'
    assert fields['hashOffset'] + fields['nCodeSlots'] * 32 == len(cd), 'code hash extent'
    for index in range(fields['nCodeSlots']):
        actual = hashlib.sha256(data[index * page_bytes:min((index + 1) * page_bytes, sigoff)]).digest()
        offset = fields['hashOffset'] + index * 32
        assert cd[offset:offset + 32] == actual, f'code hash mismatch at page {index}'
    for slot in range(1, fields['nSpecialSlots'] + 1):
        actual = hashlib.sha256(blobs[slot]).digest() if slot in blobs else bytes(32)
        offset = fields['hashOffset'] - slot * 32
        assert cd[offset:offset + 32] == actual, f'special hash mismatch at slot {slot}'
    if 5 in blobs:
        assert struct.unpack_from('>I', blobs[5])[0] == 0xFADE7171, 'XML magic'
        assert plistlib.loads(blobs[5][8:]) == {}, 'expected empty XML dictionary'
    if 7 in blobs:
        assert struct.unpack_from('>I', blobs[7])[0] == 0xFADE7172, 'DER magic'
        assert blobs[7][8:] == bytes.fromhex('7005020101b000'), 'expected version 1 empty DER dictionary'
    if variant:
        slots, page, special = {
            'A': ({0, 2, 0x10000}, 14, 2), 'B': ({0, 2, 0x10000}, 12, 2),
            'C': ({0, 2, 5, 7, 0x10000}, 14, 7), 'D': ({0, 2, 5, 0x10000}, 12, 5),
        }[variant]
        assert set(blobs) == slots, f'wrong slots for {variant}'
        assert fields['pageSize'] == page and fields['nSpecialSlots'] == special, f'wrong page/special count for {variant}'
    fields.update(fileSize=len(data), signatureOffset=sigoff, signatureAllocation=sigsize,
                  superBlobLength=length, slots=sorted(blobs), blobLengths={str(k): len(v) for k, v in blobs.items()},
                  cdhash=hashlib.sha256(cd).hexdigest()[:40], codeHashes='pass', specialHashes='pass')
    return fields, blobs


def main():
    directory = Path(sys.argv[1])
    if directory.is_file():
        fields, _ = inspect(directory)
        print(json.dumps(fields, indent=2))
        return
    results = {}
    for payload in ['S1c', 'S1d']:
        for variant in ['A', 'B', 'C', 'D']:
            name = f'selfsigned-{payload}-{variant}.dylib'
            fields, blobs = inspect(directory / name, variant)
            assert fields['team'] == 'PROBETEST1', 'fixture team'
            assert fields['identifier'] == f'com.dell.satchel.signprobe.{payload}', 'fixture identifier'
            results[name] = fields
            if variant != 'D':
                reference = f'mac-{payload}-{variant}.dylib'
                mac, mac_blobs = inspect(directory / reference, variant)
                assert mac['team'] == os.environ.get('SATCHEL_TEAM', mac['team']), 'reference team'
                for key in ['identifier', 'version', 'flags', 'hashSize', 'hashType', 'platform',
                            'pageSize', 'nSpecialSlots', 'execSegBase', 'execSegLimit', 'execSegFlags', 'slots']:
                    assert fields[key] == mac[key], f'{payload}/{variant} Mac mismatch: {key}'
                if variant == 'C':
                    assert blobs[7] == mac_blobs[7], 'DER differs from Mac reference'
                results[reference] = mac
    print(json.dumps(results, indent=2, sort_keys=True))


if __name__ == '__main__':
    main()
