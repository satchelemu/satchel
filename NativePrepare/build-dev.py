#!/usr/bin/env python3
"""Build Satchel from host archives. NativeApp/build.py owns reproducible orchestration."""
import json, os, plistlib, runpy, subprocess, sys, time
from pathlib import Path
sys.dont_write_bytecode = True
os.environ['ARCHFLAGS']='-arch arm64'
ROOT=Path(__file__).resolve().parents[1]; OUT=Path(os.environ.get('SATCHEL_PREPARE_OUT', str(Path(os.environ.get('SATCHEL_BUILD_ROOT', Path.home().joinpath('SatchelBuild'))) / 'prepare'))); HOST=Path(os.environ.get('SATCHEL_HOST_OUT', str(Path(os.environ.get('SATCHEL_BUILD_ROOT', Path.home().joinpath('SatchelBuild'))) / 'host')))
OUT.mkdir(parents=True, exist_ok=True)
unsigned = '--unsigned' in sys.argv
bridge = '--migration-bridge' in sys.argv
single = not bridge
app_name = 'Satchel' if single else 'SatchelDev'
bundle_id = 'com.dell.satchel' if single else 'com.dell.satchel.dev'
display_name = 'Satchel' if single else 'Satchel Dev'
def run(args): subprocess.run([str(x) for x in args],check=True)
def wait_for_xcode():
    runpy.run_path(str(ROOT/'NativeApp/build_support.py'))['wait']()
wait_for_xcode()
if os.statvfs(OUT).f_bavail*os.statvfs(OUT).f_frsize < 15*1024**3: raise SystemExit('Build blocked: less than 15 GiB free')
run(['nice','-n','10','cmake','-S',ROOT/'Tools/NativeRewriter','-B',OUT/'rewriter-ios','-DCMAKE_SYSTEM_NAME=iOS','-DCMAKE_SYSTEM_PROCESSOR=arm64','-DCMAKE_OSX_SYSROOT=iphoneos','-DCMAKE_OSX_ARCHITECTURES=arm64','-DCMAKE_OSX_DEPLOYMENT_TARGET=26.0','-DCMAKE_BUILD_TYPE=Release','-DNR_BUILD_SHARED=OFF','-DBUILD_TESTING=OFF','-DFETCHCONTENT_SOURCE_DIR_CAPSTONE='+str(Path(os.environ.get('SATCHEL_CAPSTONE_SOURCE', Path(os.environ.get('SATCHEL_BUILD_ROOT', Path.home().joinpath('SatchelBuild'))) / 'rewriter/capstone-src')))])
wait_for_xcode()
run(['nice','-n','10','cmake','--build',OUT/'rewriter-ios','-j2'])
(OUT/'core').mkdir(exist_ok=True)
run(['python3',ROOT/'NativePrepare/generate_registry.py',OUT/'core/Registry.c'])
(OUT/'DevBridge.h').write_text('#include "'+str(ROOT/'NativeHost/Bridge.h')+'"\n#include "'+str(ROOT/'NativePrepare/Registry.h')+'"\n#include "'+str(ROOT/'NativePrepare/Signer/SigningIdentity.h')+'"\n')
sys.path.insert(0, str(ROOT/'NativeHost'))
import shader_dependency
shader_sources = shader_dependency.stage(ROOT, OUT)
with (OUT/'DevBridge.h').open('a') as bridge_header:
    bridge_header.write('#include "'+str(shader_sources/'Bridge.h')+'"\n')
project=json.loads((HOST/'project.json').read_text())
# Only this task's sources and products change. Host source libraries are reused.
source_root=next(Path(row['path']).parent for row in project['targets']['SatchelDev']['sources'] if Path(row['path']).name=='App')
serialized=json.dumps(project).replace(str(source_root),str(ROOT))
project=json.loads(serialized); target=project['targets']['SatchelDev']
for name in list(project['targets']):
    if name!='SatchelDev': del project['targets'][name]
for dep in target['dependencies']:
    if 'target' in dep:
        name=dep.pop('target'); archive=HOST/'DerivedData/Build/Products/Release-iphoneos'/('lib'+name+'.a')
        assert archive.is_file(); dep['framework']=str(archive); dep['embed']=False
# The cached host project can predate the current menu integration.
existing_sources = {Path(row['path']).name for row in target['sources']}
for name in ['PlayerSettingsView.swift', 'NativeMenuBridge.swift', 'SatchelMenuSession.swift', 'PlayerProfiles.plist']:
    if name not in existing_sources:
        row = {'path':str(ROOT/'NativeHost'/name)}
        if name.endswith('.plist'): row['buildPhase'] = 'resources'
        target['sources'].append(row)
settings=target['settings']['base']
# The signing team never lives in the repository: NativeApp/build_support.py resolves it.
settings.pop('DEVELOPMENT_TEAM', None)
if not unsigned: settings['DEVELOPMENT_TEAM'] = runpy.run_path(str(ROOT/'NativeApp/build_support.py'))['signing_team'](ROOT)
# The app owns migration, Prepare and native play in every full runtime build.
target['sources'].append({'path': str(ROOT/'NativeMigration'), 'includes': ['MigrationStore.swift', 'MigrationApp.swift']})
settings.update(PRODUCT_BUNDLE_IDENTIFIER=bundle_id, PRODUCT_NAME=app_name,
                ASSETCATALOG_COMPILER_APPICON_NAME='AppIcon' if single else 'AppIconDev',
                SWIFT_ACTIVE_COMPILATION_CONDITIONS='$(inherited)')
# Pin the shipped version instead of XcodeGen's implicit 1.0 (1) Info.plist defaults.
release = json.loads((ROOT/'NativeApp/Distribution/release.json').read_text())
settings.update(MARKETING_VERSION=release['version'], CURRENT_PROJECT_VERSION=release['build'],
                IPHONEOS_DEPLOYMENT_TARGET=release['minimumOS'])
target['info']['properties'].update(CFBundleDisplayName=display_name,
    CFBundleShortVersionString='$(MARKETING_VERSION)', CFBundleVersion='$(CURRENT_PROJECT_VERSION)')
if single or bridge:
    entitlements = plistlib.loads((ROOT/'NativeHost/Dev.entitlements').read_bytes())
    entitlements['com.apple.security.application-groups'] = ['group.com.dell.satchel.migration']
    entitlements['keychain-access-groups'] = ['$(AppIdentifierPrefix)' + bundle_id,
                                           '$(AppIdentifierPrefix)com.dell.satchel.migration']
    (OUT/'Satchel.entitlements').write_bytes(plistlib.dumps(entitlements))
    settings['CODE_SIGN_ENTITLEMENTS'] = str(OUT/'Satchel.entitlements')
    target['info']['properties'].update(SatchelMigrationBridge=bridge,
        SatchelPrivateKeychainGroup='$(AppIdentifierPrefix)' + bundle_id)
settings['SWIFT_OBJC_BRIDGING_HEADER']=str(OUT/'DevBridge.h')
settings['HEADER_SEARCH_PATHS'] += ['"'+str(ROOT/'NativePrepare')+'"','"'+str(ROOT/'Tools/NativeRewriter/include')+'"']  # quoted: the checkout path has spaces
settings['OTHER_LDFLAGS']=[x.replace('$(BUILT_PRODUCTS_DIR)',str(HOST/'DerivedData/Build/Products/Release-iphoneos')) for x in settings['OTHER_LDFLAGS']]
settings['OTHER_LDFLAGS'] += ['-lc++']
settings['OTHER_LDFLAGS'] += ['-Wl,-u,_satchel_phone_shader_' + name for name in ['prepare', 'inputs', 'reflection', 'library']]
target['sources'].append({'path': str(ROOT/'NativeHost/PhoneShaderRuntime.swift')})
target['sources'].append({'path': str(ROOT/'NativeHost/ShaderWarmupResources.swift')})
target['sources'].append({'path': str(ROOT/'NativeHost/graphics/WarmupPipelines.m')})
target['sources'] += [{'path': str(shader_sources/(name+'.swift'))} for name in shader_dependency.SOURCES]
target['sources'].append({'path': str(shader_sources/'Notices'), 'type': 'folder', 'buildPhase': 'resources'})
# Keep the earlier fixture probe independent from the general game translator.
target['dependencies'] += [{'framework': str(shader_sources/'libGameShaderBridge.a'), 'embed': False},
                           {'framework': str(shader_sources/'libShaderArchive.a'), 'embed': False},
                           {'framework': str(shader_sources/'SatchelShaderTranslator.framework'), 'embed': True, 'codeSign': True}]
settings['LIBRARY_SEARCH_PATHS'] += [str(HOST/'DerivedData/Build/Products/Release-iphoneos'),str(OUT/'rewriter-ios'),str(OUT/'rewriter-ios/_deps/capstone-build'),str(shader_sources)]
for path in [OUT/'rewriter-ios/libnative_rewriter.a',OUT/'rewriter-ios/_deps/capstone-build/libcapstone.a']:
    target['dependencies'].append({'framework':str(path),'embed':False})
for name in ['Progress.swift','Rewrite.swift','MachO.swift','Discovery.swift','NROArchive.swift','Pipeline.swift','PrepareJob.swift','AddOnUpdate.swift','AddOnJob.swift','PrepareGameView.swift','WarmupJob.swift','WarmupView.swift','WarmupInventory.swift','WarmupRunner.swift']:
    target['sources'].append({'path':str(ROOT/'NativePrepare'/name)})
target['sources'].append({'path':str(OUT/'core/Registry.c')})
# Reuse the phone-proven OpenSSL 3.6.3 archive, with no identity build inputs.
ssl = Path(os.environ.get('SATCHEL_OPENSSL_IOS', str(Path(os.environ.get('SATCHEL_BUILD_ROOT', Path.home().joinpath('SatchelBuild'))) / 'spike/openssl-ios')))
assert (ssl/'lib/libcrypto.a').is_file(), 'Run NativePrepare/Signer/build-openssl.zsh first'
signer = ROOT/'NativePrepare/Signer'
settings['HEADER_SEARCH_PATHS'] += ['"'+str(x)+'"' for x in [signer, signer/'Vendor/zsign', signer/'Vendor/zsign/common', ssl/'include']]  # quoted like the paths above
settings['GCC_PREPROCESSOR_DEFINITIONS'] = ['$(inherited)', 'SATCHEL_SIGNER_QUIET=1']
settings['CLANG_CXX_LANGUAGE_STANDARD'] = 'c++17'
settings['LIBRARY_SEARCH_PATHS'] += [str(ssl/'lib')]
target['dependencies'] += [{'framework':str(ssl/'lib/libcrypto.a'),'embed':False}, {'sdk':'Security.framework'}]
for name in ['SigningIdentity.mm','SignatureMetadata.mm','SigningIdentityView.swift','PhonePreparedGameSigner.swift','SignatureVerifier.swift']:
    target['sources'].append({'path':str(signer/name)})
target['sources'].append({'path':str(signer/'Vendor/zsign'),'excludes':['LICENSE']})

# Ship reviewed provenance and required attribution, with no private game assets.
resources=OUT/'PrepareAssets'; resources.mkdir(exist_ok=True)
import shutil
for name in ['registry.json','source-lock.json']:
    shutil.copyfile(ROOT/'Tools/NativeRewriter/profiles'/name,resources/name)
for p in (ROOT/'Tools/NativeRewriter/licenses').iterdir(): shutil.copyfile(p,resources/p.name)
shutil.copyfile(signer/'Vendor/zsign/LICENSE', resources/'zsign-LICENSE.txt')
shutil.copyfile(signer/'Vendor/OpenSSL-LICENSE.txt', resources/'OpenSSL-LICENSE.txt')
target['sources'].append({'path':str(resources),'type':'folder','buildPhase':'resources'})
target['info']['path']=str(OUT/'Info.plist')
# Prepare runs as an iOS continued processing task (BGContinuedProcessingTaskRequest), whose
# identifiers must be declared with a wildcard.
target['info'].setdefault('properties',{})['BGTaskSchedulerPermittedIdentifiers']=[bundle_id + '.prepare.*']
# Navigation tests use only the read-only suite, never the legacy import/runtime journeys.
sys.path.insert(0, str(ROOT/'Tests/UI/Navigation'))
from configure import add_suite, quote_build_paths
project['name'] = app_name
project['targets'] = {app_name: target}
project['schemes'] = {app_name: {'build': {'targets': {app_name: 'all'}}, 'run': {'config': 'Release'}}}
add_suite(project, app_name, app_name + 'NavigationUITests', dev=True)
quote_build_paths(project)
(OUT/'project.json').write_text(json.dumps(project,indent=2)+'\n')
run(['xcodegen','generate','--spec',OUT/'project.json','--project',OUT])
wait_for_xcode()
run(['nice','-n','10','xcodebuild','-quiet','-project',OUT/(app_name + '.xcodeproj'),'-scheme',app_name,'-configuration','Release','-destination','generic/platform=iOS','-derivedDataPath',OUT/'DerivedData','-jobs','2'] + (['CODE_SIGNING_ALLOWED=NO'] if unsigned else ['-allowProvisioningUpdates']) + ['build'])
# Execute the host's original build checks against this task's output root.
sys.path.insert(0,str(ROOT/'NativeHost')); import build
build.OUT=OUT
os.environ.update(SATCHEL_VERIFY_APP=app_name, SATCHEL_VERIFY_BUNDLE=bundle_id, SATCHEL_VERIFY_DISPLAY=display_name, SATCHEL_VERIFY_MIGRATION='1' if single or bridge else '0')
if unsigned:
    print('SKIPPED: app signature and provisioning checks for the explicitly unsigned build. The app cannot be installed until the orchestrator signs it.')
else:
    exec(compile((ROOT/'NativeHost/verify_build.py').read_text(),str(ROOT/'NativeHost/verify_build.py'),'exec'),{'__name__':'__main__'})
app=OUT/'DerivedData/Build/Products/Release-iphoneos'/(app_name + '.app')
symbols=subprocess.check_output(['nm',str(app/app_name)],text=True)
for name in ['_nr_rewrite','_cs_open','_cs_disasm','_satchel_registry_profile','_OBJC_CLASS_$_SatchelSigningIdentity'] + ['_satchel_phone_shader_' + name for name in ['prepare', 'inputs', 'reflection', 'library']]:
    assert any(line.endswith(' '+name) for line in symbols.splitlines()), 'Missing preparation symbol '+name
framework = app/'Frameworks/SatchelShaderTranslator.framework'
assert (framework/'SatchelShaderTranslator').is_file(), 'Native shader translator framework was not embedded'
if not unsigned:
    run(['codesign', '--verify', '--strict', framework])
imports = subprocess.check_output(['xcrun', 'dyld_info', '-imports', app/app_name], text=True)
for symbol, owner in [('_satchel_shader_fixture_compile', 'SatchelShaderProbe'),
                      ('_satchel_shader_probe', 'SatchelShaderProbe'),
                      ('_satchel_shader_translate_v1', 'SatchelShaderTranslator'),
                      ('_satchel_shader_translate_free', 'SatchelShaderTranslator')]:
    assert any(symbol + ' ' in line and '(from ' + owner + ')' in line for line in imports.splitlines()), 'Incorrect shader facade import: ' + symbol
print('PASS: original diagnostic and game shader imports retain separate framework ownership')
for name in ['registry.json','source-lock.json']:
    assert (app/'PrepareAssets'/name).read_bytes()==(ROOT/'Tools/NativeRewriter/profiles'/name).read_bytes()
print('PASS: Release build, preparation symbols and registry checks complete. No device actions.')
