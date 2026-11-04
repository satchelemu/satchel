#!/usr/bin/env python3
"""Build the original Vulkan compute diagnostic against a checksum-pinned runtime."""
from pathlib import Path
import argparse,hashlib,json,os,plistlib,shutil,subprocess,tarfile
ROOT=Path(__file__).resolve().parents[2];HERE=ROOT/'Tools/VulkanBackend';WORK=Path('/tmp/satchel-vulkan-backend')
PIN='db66022459ffb663aa2b50f6b018bc2e124f5edf'
HASHES={'ios':'b5d947b1660e6e9fed40b9cd2387e160aaab9e80b775c0cef7e14059405178c1','macos':'f95765a6229cb7b915990a2890ce12ebe36a730b021545d3d52ae69ce4c4024e'}
def run(args,**kwargs):subprocess.run([str(x) for x in args],check=True,**kwargs)
def capture(args):return subprocess.check_output([str(x) for x in args],text=True).strip()
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--ios',action='store_true');parser.add_argument('--staging',action='store_true');parser.add_argument('--output-root',type=Path);args=parser.parse_args()
    if args.output_root and not args.staging:raise SystemExit('--output-root requires --staging.')
    source=WORK/'MoltenVK-source'
    if not source.exists() or capture(['git','-C',source,'rev-parse','HEAD'])!=PIN:
        raise SystemExit('Pinned MoltenVK source absent/mismatched; see Tools/VulkanBackend/README.md.')
    for platform in ['macos']+(['ios'] if args.ios else []):
        archive=WORK/f'MoltenVK-{platform}.tar'
        if not archive.exists() or hashlib.file_digest(archive.open('rb'),'sha256').hexdigest()!=HASHES[platform]:
            raise SystemExit('MoltenVK official release archive checksum mismatch.')
        package=WORK/platform/'MoltenVK/MoltenVK'
        slice='macos-arm64_x86_64' if platform=='macos' else 'ios-arm64'
        library=package/'static/MoltenVK.xcframework'/slice/'libMoltenVK.a'
        # Verify consumed extracted binary/headers against the checked official tar.
        with tarfile.open(archive) as release:
            for member in release.getmembers():
                relative=member.name.removeprefix('./')
                if member.isfile() and (relative==str(library.relative_to(WORK/platform)) or relative.startswith('MoltenVK/MoltenVK/include/')):
                    local=WORK/platform/relative
                    if not local.is_file() or hashlib.sha256(release.extractfile(member).read()).digest()!=hashlib.file_digest(local.open('rb'),'sha256').digest():
                        raise SystemExit('Extracted MoltenVK library/header differs from pinned release.')
        out=(args.output_root or WORK/('staging' if args.staging else 'build'))/platform;out.mkdir(parents=True,exist_ok=True)
        sdk='macosx' if platform=='macos' else 'iphoneos'
        target='arm64-apple-macos14.0' if platform=='macos' else 'arm64-apple-ios17.0'
        flags=['-std=c++17','-O1','-g','-fvisibility=hidden','-Wall','-Wextra','-Werror','-Wno-missing-field-initializers','-target',target,'-isysroot',capture(['xcrun','--sdk',sdk,'--show-sdk-path']),'-I',package/'include','-I',ROOT/'Tools/GuestGraphics']
        frameworks=['Foundation','Metal','QuartzCore','CoreGraphics','IOSurface']+(['IOKit','AppKit'] if platform=='macos' else ['UIKit'])
        links=[x for name in frameworks for x in ['-framework',name]]
        if platform=='macos':
            sanitize=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
            test=out/'test'
            run(['xcrun','clang++',*flags,*sanitize,HERE/'VulkanBackend.cpp',HERE/'Test.cpp',library,*links,'-o',test])
            run([test],env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
            lifecycle=out/'device-test'
            run(['xcrun','clang++',*flags,*sanitize,HERE/'VulkanBackend.cpp',HERE/'DeviceTest.cpp',library,*links,'-o',lifecycle])
            run([lifecycle],env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
            buffers=out/'buffer-test'
            run(['xcrun','clang++',*flags,*sanitize,HERE/'VulkanBackend.cpp',HERE/'BufferTest.cpp',library,*links,'-o',buffers])
            run([buffers],env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
            submissions=out/'submission-test'
            run(['xcrun','clang++',*flags,*sanitize,HERE/'VulkanBackend.cpp',HERE/'SubmissionTest.cpp',library,*links,'-o',submissions])
            run([submissions],env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
            batches=out/'batch-test'
            run(['xcrun','clang++',*flags,*sanitize,HERE/'VulkanBackend.cpp',HERE/'BatchTest.cpp',library,*links,'-o',batches])
            run([batches],env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
        else:
            framework=out/'SatchelVulkan.framework';framework.mkdir(exist_ok=True)
            headers=framework/'Headers';headers.mkdir(exist_ok=True)
            for header in ['VulkanBackend.h','VulkanDevice.h','VulkanBuffer.h','VulkanSubmission.h','VulkanTransferBatch.h','VulkanCompute.h','VulkanScalar.h','VulkanRenderTarget.h','VulkanRaster.h','VulkanRasterBatch.h','RasterCompiler.h']:(headers/header).write_bytes((HERE/header).read_bytes())
            (headers/'ScalarCompiler.h').write_bytes((ROOT/'Tools/ScalarCompiler/ScalarCompiler.h').read_bytes())
            (headers/'GuestDeviceBackend.h').write_bytes((ROOT/'Tools/GuestGraphics/GuestDeviceBackend.h').read_bytes())
            module=framework/'Modules';module.mkdir(exist_ok=True)
            (module/'module.modulemap').write_text('framework module SatchelVulkan { umbrella header "VulkanBackend.h" export * }\n')
            exports=out/'exports.txt';api=['satchel_vulkan_compute_diagnostic','satchel_vulkan_device_create','satchel_vulkan_device_capabilities','satchel_vulkan_device_destroy','satchel_vulkan_device_live_count','satchel_vulkan_guest_device_backend','satchel_vulkan_buffer_create','satchel_vulkan_buffer_info','satchel_vulkan_buffer_write','satchel_vulkan_buffer_read','satchel_vulkan_buffer_destroy','satchel_vulkan_buffer_stats','satchel_vulkan_buffer_diagnostic','satchel_vulkan_buffer_fill','satchel_vulkan_buffer_copy','satchel_vulkan_submission_wait','satchel_vulkan_submission_live_count','satchel_vulkan_transfer_diagnostic','satchel_vulkan_device_drain','satchel_vulkan_guest_device_backend_v2','satchel_vulkan_transfer_batch','satchel_vulkan_compute_limits','satchel_vulkan_compute_pipeline_create_original','satchel_vulkan_compute_pipeline_destroy','satchel_vulkan_compute_pipeline_live_count','satchel_vulkan_compute_submit_original','satchel_vulkan_scalar_validate','satchel_vulkan_scalar_pipeline_create','satchel_vulkan_scalar_submit','satchel_vulkan_target_capabilities','satchel_vulkan_target_create','satchel_vulkan_target_info','satchel_vulkan_target_destroy','satchel_vulkan_target_stats','satchel_vulkan_target_clear_readback','satchel_vulkan_raster_validate','satchel_vulkan_raster_create','satchel_vulkan_raster_info','satchel_vulkan_raster_destroy','satchel_vulkan_raster_stats','satchel_vulkan_raster_draw_readback','satchel_vulkan_raster_capabilities','satchel_vulkan_target_upload','satchel_vulkan_raster_batch_readback'];exports.write_text(''.join('_'+name+'\n' for name in api))
            run(['xcrun','clang++',*flags,'-dynamiclib',HERE/'VulkanBackend.cpp',library,*links,'-Wl,-install_name,@rpath/SatchelVulkan.framework/SatchelVulkan','-Wl,-exported_symbols_list,'+str(exports),'-o',framework/'SatchelVulkan'])
            nested_symbols=framework/'SatchelVulkan.dSYM'
            if nested_symbols.exists():
                external_symbols=out/'SatchelVulkan.framework.dSYM'
                if external_symbols.exists(): shutil.rmtree(external_symbols)
                shutil.move(nested_symbols,external_symbols)
            if list(framework.rglob('*.dSYM')):
                raise SystemExit('Debug symbols must remain outside the framework.')
            exports_actual=capture(['xcrun','nm','-gU',framework/'SatchelVulkan']).splitlines()
            if {line.split()[-1] for line in exports_actual} != {'_'+name for name in api}:
                raise SystemExit('Framework must export exactly its reviewed C ABI.')
            if 'IOKit' in capture(['otool','-L',framework/'SatchelVulkan']):
                raise SystemExit('Unexpected IOKit dependency in isolated iOS framework.')
            shutil.copytree(HERE/'Notices',framework/'Notices',dirs_exist_ok=True)
            shutil.copy2(HERE/'ComputeOriginal.json',framework/'Notices/compute-original.json')
            scalar_notices=framework/'Notices/scalar-compiler';scalar_notices.mkdir(exist_ok=True)
            for name in ['NOTICES.md','Reference-LICENSE.txt','source-lock.json']:shutil.copy2(ROOT/'Tools/ScalarCompiler'/name,scalar_notices/name)
            info={'CFBundleIdentifier':'com.dell.satchel.vulkan','CFBundleName':'SatchelVulkan','CFBundleExecutable':'SatchelVulkan','CFBundlePackageType':'FMWK','CFBundleVersion':'1','CFBundleShortVersionString':'0.1','MinimumOSVersion':'17.0','CFBundleSupportedPlatforms':['iPhoneOS']}
            (framework/'Info.plist').write_bytes(plistlib.dumps(info))
            print('Built unsigned isolated iOS framework:',framework)
if __name__=='__main__':main()
