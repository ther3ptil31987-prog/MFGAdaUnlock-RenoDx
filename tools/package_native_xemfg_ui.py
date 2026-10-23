"""Seal the R1.7.5 UI/input candidate and its exact offline evidence."""
import argparse,hashlib,json,shutil,subprocess
from pathlib import Path
from write_build_manifest import compiler_inputs

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    ap=argparse.ArgumentParser()
    for name in ('output','evidence','previous-addon','official-addon','intel-sdk'):
        ap.add_argument('--'+name,type=Path,required=True)
    a=ap.parse_args();root=Path(__file__).resolve().parent.parent
    def git(*args):return subprocess.check_output(['git',*args],cwd=root,text=True).strip()
    if git('status','--porcelain'):raise ValueError('Commit the reviewed source before sealing')
    addon=root/'build/tests/xemfg/Release/renodx-mfgunlock-native.addon64';digest=sha(addon)
    validation=json.loads((a.evidence/'validation-summary.json').read_text(encoding='utf-8'))
    if validation['addon_sha256']!=digest or validation['ctest']['failed'] or validation['ctest']['passed']!=66 or validation['ctest']['skipped']!=3:
        raise ValueError('Missing matching complete suite')
    if set(validation['sdk_cases'])!={'2x-61aeeab7','3x-61aeeab7','4x-61aeeab7','6x-61aeeab7','4x-d2030dcd'}:raise ValueError('Incomplete SDK matrix')
    ordinary=Path(validation['ordinary_input_case']);foreground=Path(validation['foreground_native_case'])
    data=json.loads((ordinary/'result.json').read_text())
    if not data['passed'] or data['inputs']['renodx-mfgunlock-native.addon64']!=digest:raise ValueError('Ordinary runtime input evidence mismatch')
    data=json.loads((foreground/'result.json').read_text())
    if data['exit'] or not data['passed'] or not data['manual_runtime_verified'] or data['inputs']['renodx-mfgunlock-native.addon64']!=digest:raise ValueError('Native output input evidence mismatch')
    if sha(a.previous_addon)!='1a9d67b86f76c7a4c97290dc55a3e64daf57667cf71c5ce7469a2793ae12ec3c':raise ValueError('Previous addon differs')
    if sha(a.official_addon)!='9715987f6323a2699dd5b7636412258a6b97a180a4d70c0f79d286714e2972e2':raise ValueError('Official rollback differs')
    assets={'renodx-mfgunlock.addon64':addon,'ui-input-report.md':root/'docs/research/xemfg-r1.7.5-ui-input-2026-10-19.md',
        'rollback/renodx-mfgunlock-native-r1.7.4.addon64':a.previous_addon,'rollback/renodx-mfgunlock-1.4.5.addon64':a.official_addon,
        'Restore-MFGBackup.ps1':root/'tools/Restore-MFGBackup.ps1',
        'notices/MFGAdaUnlock-MIT.txt':root/'LICENSE',
        'notices/MFG-Unlocker-MIT.txt':root/'src/addons/mfgunlock/xemfg/vendor/LICENSE.dashdogy.txt',
        'notices/MinHook-LICENSE.txt':root/'src/addons/mfgunlock/xemfg/vendor/third_party/minhook/LICENSE.txt',
        'notices/Intel-program-data.txt':root/'src/addons/mfgunlock/xemfg/vendor/XEFG-PROGRAMS-NOTICE.txt',
        'notices/Intel-headers-MIT.txt':a.intel_sdk/'inc/MIT.txt','notices/Intel-SDK-LICENSE.txt':a.intel_sdk/'LICENSE.txt',
        'evidence/validation-summary.json':a.evidence/'validation-summary.json','evidence/ctest.log':root/'build/ctest-native-r1.7.5.log'}
    cases={'ordinary-input':ordinary,'native-input':foreground,**{k:Path(v['directory']) for k,v in validation['sdk_cases'].items()}}
    for label,d in cases.items():
        data=json.loads((d/'result.json').read_text())
        if data['exit'] or data['inputs']['renodx-mfgunlock-native.addon64']!=digest:raise ValueError('Stale evidence: '+label)
        for name in ('result.json','MFGAdaUnlock-XeMFG.log','ReShade.log'):assets['evidence/'+label+'/'+name]=d/name
    for page in range(4):assets[f'evidence/ui/page-{page}.png']=root/f'build/tests/ui-preview/page-{page}.png'
    assets['evidence/ui/large-font.png']=root/'build/tests/ui-preview/large-font.png'
    if a.output.exists():raise ValueError('Refuse to overwrite a sealed package directory')
    a.output.mkdir(parents=True)
    for name,src in assets.items():
        dst=a.output/name;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,dst)
    readme=a.output/'README.txt'
    readme.write_text('MFG Unlock 1.4.5 + XeMFG R1.7.5 (local candidate)\n\nSingle-window compact UI; ReShade owns the overlay shortcut and input capture.\nGeneration, XeLL and pacing remain as in the previous candidate.\nUI Composition is optional and off by default.\n\nClose the game, back up the previous addon, and replace only renodx-mfgunlock.addon64.\nKeep the existing ReShade, RenoDX, Intel runtime DLLs and configuration. Restart the game.\nThis package contains no OptiScaler, ASI or Intel runtime DLLs.\n\nOffline tests passed. The reported Cyberpunk/Pragmata input failure still needs gameplay confirmation.\nThe Intel runtime pair currently installed in Pragmata remains unsupported for XeMFG.\nFor rollback, close the game and restore the previous addon.\n',encoding='utf-8')
    source={p.relative_to(root).as_posix():sha(p) for folder in ('src','tests','tools','docs') for p in sorted((root/folder).rglob('*')) if p.is_file() and '__pycache__' not in p.parts}
    manifest={'identity':'Native ReShade XeMFG R1','revision':'R1.7.5','source_commit':git('rev-parse','HEAD'),
        'branch':git('branch','--show-current'),'working_tree':git('status','--porcelain'),
        'official_1_4_5_commit':'0fe65579d287581107babe267ee3e48c9135bf8a','base_commit':'5ef1af84f15454a013715b3e77856834b396266a',
        'license':'MIT bridge and MIT components; embedded Intel program data retains vendor terms',
        'optiscaler_dependency':False,'asi_dependency':False,'runtime_dlls_bundled':False,
        'source_inputs_sha256':source,'compiler_inputs':compiler_inputs(root/'build/tests/xemfg/mfgunlock_native_xemfg.vcxproj'),
        'precompiled_inputs':json.loads((root/'build/xemfg-inputs/input-provenance.json').read_text(encoding='utf-8')),
        'gameplay_validated':False,'test_scope':'66 passed, 3 optional skipped; real SDK matrix; real production panel and ReShade command capture/release. R1.7.4 isolated UI also passed: no causal gameplay cure claim.',
        'assets':[{'name':p.relative_to(a.output).as_posix(),'sha256':sha(p),'bytes':p.stat().st_size} for p in sorted(a.output.rglob('*')) if p.is_file()]}
    (a.output/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
    (a.output/'SHA256SUMS.txt').write_text(''.join(f'{sha(p)}  {p.relative_to(a.output).as_posix()}\n' for p in sorted(a.output.rglob('*')) if p.is_file()),encoding='utf-8')
    print(a.output)
if __name__=='__main__':main()
