#!/usr/bin/env python3
"""Original boundary expressions for safe shared constant-buffer allocation."""
import json
from pathlib import Path
import runpy
import subprocess
import sys
DEPENDENCY_PATHS = runpy.run_path(str(Path(__file__).resolve().parents[1] / 'scripts/dependency_paths.py'))
sys.dont_write_bytecode=True
from oracle import TOOLS
sys.path.insert(0,str(TOOLS))
import c1_guard

def main():
    source='uint _1 = as_type<uint>(input.x);\nuint _2 = _1 & 15u;\nint _3 = int(_2) - 2;\nuint _4 = _4 + 1u;\nuint _5 = 3u;\n_5++;\n'
    expressions=['0u','4095','4096','-1','0xffffffffu','_1','_2','_3','_4','_5','_1 + (_2 & 3u)',
        '(_1 & 3u) + 4u','_1 % 4u','int(_1) % 4','min(_2, 7u)','max(0, _3)','clamp(_1, 0u, 31u)',
        '_2 >> 1','_2 << 4','_2 / 2','_3 / 2','_2 * 4','_2 | 31u','_2 ^ 8u','!_1','_1 ? 1u : 7u',
        'uint(-1)','as_type<uint>(_3)','input.x','buffer[0]','*pointer','(_1 & 3u','unknown(_2)','~_2']
    definitions=c1_guard.definitions_of(source)
    expected=[list(c1_guard.Interval(definitions).expression(s)) for s in expressions]
    result=subprocess.run([DEPENDENCY_PATHS['shader_work']()/'analysis-harness'],input=json.dumps({'source':source,'expressions':expressions})+'\n',text=True,capture_output=True,check=True)
    actual=json.loads(result.stdout)
    mismatches=[i for i,(a,b) in enumerate(zip(actual,expected)) if a!=b]
    if len(actual)!=len(expected) or mismatches:raise RuntimeError('Interval parity failed at indices '+str(mismatches))
    print(f'{len(expressions)} interval boundary expressions match, including cyclic and reassigned temporaries and unbounded memory reads.')
if __name__=='__main__':main()
