#!/usr/bin/env python3
"""Exercise the capture format with synthetic bytes and the pinned decoder."""
from pathlib import Path
import argparse,ast,hashlib,json,re,shutil,struct,subprocess,sys,tempfile

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--engine',type=Path,required=True)
    p.add_argument('--profile',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
    title='0000000000000001';passed=0
    with tempfile.TemporaryDirectory(dir=a.output,prefix='synthetic-capture-') as tmp:
        tmp=Path(tmp);profiles=tmp/'profiles';(profiles/title).mkdir(parents=True)
        base=a.profile.read_text()
        def profile(skip=False):
            text=re.sub(r'(?m)^(title_id|program_id)\t.*$', lambda m:m[1]+'\t'+title,base)
            text=re.sub(r'(?m)^shader_key_skip\t.*\n?', '',text)
            if skip:text+='\nshader_key_skip\t0x770+0x10\n'
            (profiles/title/'game.tsv').write_text(text)
        profile()
        def shader(stage,short=False,skip=False,nonce=0):
            ctl=bytearray(0x850 if short else 0x880);code=bytearray(0x80)
            struct.pack_into('<I',ctl,0,0x98761234)
            struct.pack_into('<I',ctl,0x20,len(ctl));struct.pack_into('<I',ctl,0x704,len(code))
            ctl[0x100]=nonce
            if stage=='cs':
                struct.pack_into('<I',code,0,0x12345679);struct.pack_into('<4I',ctl,0x748,8,4,1,17)
            else:
                struct.pack_into('<I',code,0,0x12345678)
                struct.pack_into('<I',code,0x30,{'vs':1,'hs':2,'ds':3,'gs':4,'ps':5}[stage]<<10)
            ctl[0x770:0x780]=bytes(range(16))
            hashed=bytearray(ctl)
            if skip:hashed[0x770:0x780]=bytes(16)
            return hashlib.sha256(hashed+code).hexdigest()[:16],bytes(ctl),bytes(code)
        counter=0
        def case(name,items,expect=None,pairs=None,require=False,mutate=None):
            nonlocal counter,passed
            counter+=1;capture=tmp/str(counter);capture.mkdir();output=tmp/(str(counter)+'-out')
            for key,ctl,code in items:
                (capture/(key+'.ctl')).write_bytes(ctl);(capture/(key+'.code')).write_bytes(code)
            if pairs is not None:(capture/'draw-pairs.tsv').write_text(pairs)
            if mutate:mutate(capture)
            cmd=[sys.executable,str(Path(__file__).with_name('capture_shaders.py')),str(capture),str(output),'--engine',str(a.engine.resolve()),'--profiles',str(profiles),'--title',title]
            if require:cmd.append('--require-pairs')
            r=subprocess.run(cmd,capture_output=True,text=True)
            if expect:assert r.returncode and expect in r.stderr,(name,r.stderr)
            else:assert r.returncode==0,(name,r.stderr)
            passed+=1;print('PASS '+name)
            return output
        vertex,pixel,compute=shader('vs'),shader('ps'),shader('cs')
        entries=[shader(s,short=s=='vs') for s in ['vs','ps','cs','gs','hs','ds']]
        output=case('six stages and actual draw pair',entries,pairs=entries[0][0]+'\t'+entries[1][0]+'\n',require=True)
        # Execute only the pinned decoder's record-splitting function. Importing
        # dmsh_build.py itself would start a Metal build.
        tree=ast.parse((a.engine/'dm/gfx/tools/dmsh_build.py').read_text())
        function=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='records')
        namespace={'re':re};exec(compile(ast.Module(body=[function],type_ignores=[]),'<pinned record decoder>','exec'),namespace)
        found={}
        for path in (output/'pkg').glob('*.bin'):
            for record in namespace['records'](path.read_bytes()):
                nctl,ncode=struct.unpack_from('<II',record,0x28)
                code=record[0x38:0x38+ncode];ctl=record[0x38+ncode:]
                assert len(ctl)==nctl
                found[hashlib.sha256(ctl+code).hexdigest()[:16]]=(ctl,code)
        assert found=={key:(ctl,code) for key,ctl,code in entries}
        assert json.loads((output/'capture-index.json').read_text())['drawPairs']==1
        assert json.loads((output/'tables/shader_facts.tsv').read_text().splitlines()[1].split('\t')[1])=={'local':[8,4,1],'shared':32}
        passed+=1;print('PASS pinned record decoder round trip and compute facts')
        case('legacy missing-only capture', [vertex,pixel])
        case('pair evidence required',[vertex],expect='Draw-pair evidence is missing',require=True)
        case('dangling pair',[vertex],pairs=vertex[0]+'\t'+pixel[0]+'\n',expect='uncaptured shader')
        case('reversed pair',[vertex,pixel],pairs=pixel[0]+'\t'+vertex[0]+'\n',expect='stages do not match')
        case('truncated control',[(vertex[0],vertex[1][:-1],vertex[2])],expect='truncated control')
        case('bad code size',[(vertex[0],vertex[1],vertex[2][:-1])],expect='code area length')
        case('wrong digest',[('0'*16,vertex[1],vertex[2])],expect='digest mismatch')
        case('orphan code',[vertex],expect='without a matching',mutate=lambda c:(c/'1111111111111111.code').write_bytes(b'synthetic'))
        case('missing code',[vertex],expect='missing or oversized',mutate=lambda c:(c/(vertex[0]+'.code')).unlink())
        case('capture symlink',[vertex],expect='symlinks',mutate=lambda c:((c/(vertex[0]+'.code')).unlink(),(c/(vertex[0]+'.code')).symlink_to(c/(vertex[0]+'.ctl'))))
        profile(True);case('profile control skip range',[shader('vs',skip=True)])
        profile();key,ctl,code=shader('vs',short=True)
        case('legacy padded short control',[(key,ctl+bytes(0x30),code)])
    print(f'{passed} capture checks passed')

if __name__=='__main__':main()
