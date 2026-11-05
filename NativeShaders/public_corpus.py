"""Original public Maxwell fixtures, never extracted from a game.

Graphics bitfields follow Tools/CapturedFlatCompiler/Test.c original().
Compute EXIT follows Tools/MaxwellShaderCompiler/fixture.c.
"""
import hashlib
import struct


def graphics(stage, location, rotate):
    code = bytearray(144)
    def word(at, value): struct.pack_into('<I', code, at, value)
    def instruction(at, value): struct.pack_into('<Q', code, at, value)
    word(0, 0x60461 if stage == 'vs' else 0x61462)
    word(16, 0xff000)
    if stage == 'vs':
        word(24, 255); word(52, 0xf0000000); word(56, 15 << (location * 4))
        ops = [0, 0xefd9ff800807ff00, 0xeff1ff800707ff00, 0xefd9ff800907ff04,
               0, 0xeff1ff800807ff04 + (location * 16 << 20), 0xe30000000007000f]
        for i, op in enumerate(ops): instruction(80 + i * 8, op)
    else:
        word(24 + location // 4 * 4, 0x55 << (location % 4 * 8)); word(72, 15)
        for i in range(4):
            slot = i + 1 if i < 3 else 5
            offset = 0x80 + location * 16 + (i + rotate) % 4 * 4
            instruction(80 + slot * 8, 0xe003ff800ff7ff00 | (offset << 28) | i)
        instruction(128, 0xe30000000007000f)
    return bytes(code)


def corpus():
    rows = []
    def add(name, code, stage, options=None, **facts):
        job = dict(key=hashlib.sha256(name.encode()).hexdigest()[:16], stage=stage,
                   c1='', tex={}, img={}, attr={}, fout={}, debug=True, reduced=True,
                   source='public-synthetic', pc_paired='')
        job.update(facts)
        rows.append(dict(name=name, code=code.hex(), job=job, **(options or {})))
    for location in (0, 1, 3, 7):
        add('vertex-' + str(location), graphics('vs', location, 0), 'vs',
            options={'wanted': [location, 8]})
        for rotate in (0, 1, 3):
            add(f'fragment-{location}-{rotate}', graphics('ps', location, rotate), 'ps',
                options={'dual': rotate == 1, 'mappingOnly': rotate == 3},
                early_fragment_tests=rotate == 3)
    for kind in ('sint', 'uint'):
        add('vertex-' + kind, graphics('vs', 0, 0), 'vs', attr={'0': kind, '1': kind})
        add('fragment-' + kind, graphics('ps', 0, 0), 'ps', fout={'0': kind})
    for local, shared in [([1, 1, 1], 0), ([8, 4, 1], 256), ([32, 1, 2], 4096)]:
        add('compute-' + str(local[0]), struct.pack('<4Q', 0, 0xe30000000007000f, 0, 0),
            'cs', local=local, shared=shared)
    return rows
