#!/usr/bin/env python3
"""Original synthetic SPIR-V transformation fixtures, no game instructions."""
from pathlib import Path
import runpy
import struct
import subprocess
import sys

sys.dont_write_bytecode=True
from oracle import functions
DEPENDENCY_PATHS = runpy.run_path(str(Path(__file__).resolve().parents[1] / 'scripts/dependency_paths.py'))
# The SPIR-V harness and the fixtures live in the caller's work folder.
ROOT=DEPENDENCY_PATHS['shader_work']()

def instruction(op,*args):return [(len(args)+1)<<16|op,*args]
def main():
    work=ROOT/'spirv-tests';work.mkdir(parents=True,exist_ok=False)
    scope=functions({'spv_single_lane','_ep_drop'})
    scope.update(SG_OPS={333:'true',334:'pred',335:'pred',336:'true',337:'value',338:'value',339:'ballot',345:'value',346:'value',347:'value',348:'value'},
                 SG_BUILTINS={36:'one',41:'zero',4416:'one4',4417:'one4',4418:'zero4',4419:'one4',4420:'zero4'})
    declarations=[instruction(17,1),instruction(17,61),instruction(17,62),instruction(17,64),instruction(17,65),instruction(14,0,1),
        instruction(15,0,10,0x6e69616d,0,7),instruction(71,7,11,41),instruction(19,1),instruction(20,2),instruction(21,3,32,0),
        instruction(23,4,3,4),instruction(32,5,1,3),instruction(33,6,1),instruction(59,5,7,1),instruction(43,3,8,3),instruction(41,2,9)]
    function=[instruction(54,1,10,0,6),instruction(248,11),instruction(61,3,12,7)]
    variants={
        'elect':[instruction(333,2,13,8)],
        'ballot':[instruction(339,4,14,8,9)],
        'shuffle':[instruction(345,3,15,8,12,8)],
        'all':[instruction(334,2,16,8,9)],
        'all_equal':[instruction(336,2,17,8,12)],
        'unsupported':[instruction(340,2,18,8,14)],
    }
    for name,ops in variants.items():
        words=[0x07230203,0x00010300,0,32,0]+[x for ins in declarations+function+ops+[instruction(253),instruction(56)] for x in ins]
        data=struct.pack('<%dI'%len(words),*words)
        source=work/(name+'.spv');oracle=work/(name+'.oracle.spv');actual=work/(name+'.native.spv')
        source.write_bytes(data);oracle.write_bytes(data)
        scope['spv_single_lane'](str(oracle))
        subprocess.run([ROOT/'spirv-harness',source,actual],check=True)
        if actual.read_bytes()!=oracle.read_bytes():raise RuntimeError('SPIR-V fixture differs: '+name)
    print('6 synthetic SPIR-V fixtures match the pinned singleton pass, including OpGroupNonUniformElect and unsupported-operation preservation.')

if __name__=='__main__':main()
