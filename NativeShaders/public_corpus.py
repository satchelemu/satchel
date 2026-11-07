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


RZ = 255
PT = 7 << 16
# Opcode patterns from the pinned translator's Decoders/InstTable.cs (ALD, AST, MOV32I, S2R,
# FADD and FFMA register forms, EXIT); fields from Decoders/InstDecoders.cs.
ALD, AST, MOV32I, S2R = 0x1DFB << 51, 0x1DFE << 51, 0x010 << 52, 0x1E19 << 51
FADD, FFMA, EXIT = 0xB8B << 51, 0xB3 << 55, 0xE30 << 52


def ald(dest, attribute, vertex=RZ, size=0, patch=False):
    return ALD | PT | dest | RZ << 8 | attribute << 20 | patch << 31 | vertex << 39 | size << 47


def ast(source, attribute, size=0, patch=False):
    return AST | PT | source | RZ << 8 | attribute << 20 | patch << 31 | RZ << 39 | size << 47


def mov32i(dest, value):
    return MOV32I | PT | dest | 15 << 12 | value << 20


def f32(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def program(stage_type, threads, ops):
    """A VTG shader: SPH (SphType 1, version 3, the stage, SASS 3), then groups of a zero
    scheduling word and three instructions."""
    header = bytearray(0x50)
    struct.pack_into('<5I', header, 0, 1 | 3 << 5 | stage_type << 10 | 3 << 17, 0, threads << 24, 0, 0xff000)
    words = []
    for i in range(0, len(ops), 3):
        words += [0] + ops[i:i + 3]
    words += [0] * (-len(words) % 4)
    return bytes(header) + struct.pack('<%dQ' % len(words), *words)


def hull(points, outer, inner):
    """Each invocation passes its control point's position and location 0 through, then
    writes the levels and per-patch location 0 (0.25, 0.5, 0.75, 1)."""
    levels = [mov32i(16 + i, f32(v)) for i, v in enumerate(outer)] + [mov32i(20 + i, f32(v)) for i, v in enumerate(inner)]
    return program(2, points, [S2R | PT | 4 | 17 << 20, ald(0, 0x70, 4, 3), ast(0, 0x70, 3), ald(8, 0x80, 4, 3), ast(8, 0x80, 3)]
                   + levels + [mov32i(24 + i, f32(v)) for i, v in enumerate((0.25, 0.5, 0.75, 1.0))]
                   + [ast(16, 0x000, 3, True), ast(20, 0x010, 1, True), ast(24, 0x018, 3, True), EXIT | PT | 15])


def domain(base, along_u, along_v):
    """Position = P[base] + u (P[along_u] - P[base]) + v (P[along_v] - P[base]); outputs
    control point 1's location 0, per-patch location 0, the outer levels, then the inner
    levels and primitive ID."""
    ops = [ald(0, 0x2f0, size=1), mov32i(20, along_u), mov32i(21, along_v), mov32i(22, base), mov32i(23, 1),
           ald(4, 0x70, 20, 3), ald(8, 0x70, 21, 3), ald(12, 0x70, 22, 3)]
    for c in range(4):
        ops += [FADD | PT | (24 + c) | (4 + c) << 8 | (12 + c) << 20 | 1 << 45,
                FADD | PT | (28 + c) | (8 + c) << 8 | (12 + c) << 20 | 1 << 45,
                FFMA | PT | (16 + c) | 0 << 8 | (24 + c) << 20 | (12 + c) << 39,
                FFMA | PT | (16 + c) | 1 << 8 | (28 + c) << 20 | (16 + c) << 39]
    return program(3, 0, ops + [ast(16, 0x70, 3), ald(4, 0x80, 23, 3), ast(4, 0x80, 3), ald(8, 0x018, size=3, patch=True), ast(8, 0x90, 3),
                                ald(8, 0x000, size=3, patch=True), ast(8, 0xa0, 3), ald(8, 0x010, size=1, patch=True), ald(10, 0x060),
                                mov32i(11, 0), ast(8, 0xb0, 3), EXIT | PT | 15])


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
    # Tessellation control and evaluation (2026-11-07). The mode facts are the 3D engine's
    # tessellation state the request supplies; check-tessellation.py draws these on the GPU.
    add('hull-tri', hull(3, (3, 5, 2, 1), (4, 1)), 'hs')
    add('hull-quad', hull(4, (3, 5, 2, 7), (3, 5)), 'hs')
    add('domain-tri', domain(2, 0, 1), 'ds')
    add('domain-tri-even', domain(2, 0, 1), 'ds', tess_spacing='fractional_even')
    add('domain-quad-odd-cw', domain(0, 1, 3), 'ds', tess_domain='quads', tess_spacing='fractional_odd', tess_cw=True)
    add('domain-isolines', domain(0, 1, 3), 'ds', tess_domain='isolines')
    return rows
