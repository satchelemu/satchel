#!/usr/bin/env python3
"""Pinned library build; only original synthetic fixtures enter test logs/files."""
import argparse, concurrent.futures, hashlib, os, pathlib, subprocess
ROOT=pathlib.Path(__file__).resolve().parents[2]
HERE=ROOT/'Tools/ShaderBridge'
WORK=pathlib.Path('/tmp/satchel-shader-bridge')
PIN='f11ba9f0b21ba8fc15153d50a2a1ae31ab1cf8f7'
SOURCES=['spirv_cross.cpp','spirv_parser.cpp','spirv_cross_parsed_ir.cpp','spirv_cfg.cpp','spirv_glsl.cpp','spirv_msl.cpp']
def run(args,**kwargs):
    subprocess.run([str(x) for x in args],check=True,**kwargs)
def capture(args):
    return subprocess.check_output([str(x) for x in args],text=True).strip()
def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--ios',action='store_true')
    parser.add_argument('--exit-spv',type=pathlib.Path)
    args=parser.parse_args()
    source=WORK/'spirv-cross'
    if not (source/'.git').exists():
        raise SystemExit('Pinned SPIRV-Cross source absent; see Tools/ShaderBridge/README.md.')
    if capture(['git','-C',source,'rev-parse','HEAD'])!=PIN or capture(['git','-C',source,'status','--porcelain']):
        raise SystemExit('SPIRV-Cross source pin/clean-tree verification failed.')
    def build(sdk,sanitize):
        out=WORK/('macos' if sdk=='macosx' else 'ios');out.mkdir(parents=True,exist_ok=True)
        sysroot=capture(['xcrun','--sdk',sdk,'--show-sdk-path'])
        target='arm64-apple-macos14.0' if sdk=='macosx' else 'arm64-apple-ios17.0'
        flags=['-std=c++17','-O1','-g','-fexceptions','-fvisibility=hidden','-target',target,'-isysroot',sysroot,'-I',source,'-I',HERE]
        if sanitize: flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
        inputs=[source/name for name in SOURCES]+[HERE/'ShaderBridge.cpp']
        digest=hashlib.sha256(repr(flags).encode())
        for header in sorted([*source.glob('*.hpp'),*source.glob('*.h'),*HERE.glob('*.h')]):
            digest.update(header.read_bytes())
        fingerprint=out/'build-flags-and-headers.sha256'
        config_changed=not fingerprint.exists() or fingerprint.read_text()!=digest.hexdigest()
        objects=[out/(path.stem+'.o') for path in inputs]
        def compile(pair):
            src,obj=pair
            if config_changed or not obj.exists() or obj.stat().st_mtime<src.stat().st_mtime:
                run(['xcrun','clang++',*flags,'-c',src,'-o',obj])
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
            list(pool.map(compile,zip(inputs,objects)))
        fingerprint.write_text(digest.hexdigest())
        archive=out/'libSatchelShaderBridge.a'
        run(['xcrun','libtool','-static','-o',archive,*objects])
        if sanitize:
            test=out/'test';run(['xcrun','clang++',*flags,HERE/'Test.cpp',archive,'-o',test])
            env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1')
            run([test,*([args.exit_spv] if args.exit_spv else [])],env=env)
            metal=out/'metal-test'
            run(['xcrun','clang++',*flags,'-fobjc-arc',HERE/'MetalTest.mm',archive,'-framework','Foundation','-framework','Metal','-o',metal])
            run([metal,*([args.exit_spv] if args.exit_spv else [])],env=env)
        print(f'Built static archive: {archive}')
    build('macosx',True)
    if args.ios: build('iphoneos',False)
if __name__=='__main__': main()
