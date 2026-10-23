"""Package the independent ReShade transport, without OptiScaler/ASI/runtime DLLs."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
from write_build_manifest import compiler_inputs

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--output',type=Path,required=True)
    ap.add_argument('--official-addon',type=Path,required=True)
    ap.add_argument('--intel-sdk',type=Path,required=True)
    ap.add_argument('--game-validation',type=Path)
    ap.add_argument('--previous-addon',type=Path)
    ap.add_argument('--audit-evidence',type=Path,required=True)
    ap.add_argument('--pacing-review',type=Path,required=True)
    ap.add_argument('--revision',choices=['R1.6','R1.7','R1.7.1','R1.7.2','R1.7.3','R1.7.4'],default='R1.7.4')
    args=ap.parse_args()
    shortcut_revision=args.revision in ('R1.7.1','R1.7.2','R1.7.3','R1.7.4')
    root=Path(__file__).resolve().parent.parent
    output=args.output.resolve(); output.mkdir(parents=True,exist_ok=True)
    build=root/'build/tests/xemfg'
    addon=build/'Release/renodx-mfgunlock-native.addon64'
    if b'Native ReShade XeMFG R1' not in addon.read_bytes():
        raise ValueError('Not the native ReShade binary')
    if sha(args.official_addon)!='9715987f6323a2699dd5b7636412258a6b97a180a4d70c0f79d286714e2972e2':
        raise ValueError('Rollback must be sealed official 1.4.5')
    assets={
        'renodx-mfgunlock.addon64':addon,
        'Install-NativeXeMFG.ps1':root/'tools/Install-NativeXeMFG.ps1',
        'Restore-MFGBackup.ps1':root/'tools/Restore-MFGBackup.ps1',
        'README.txt':root/'docs/native-xemfg-readme.txt',
        'engineering-report.md':root/'docs/research/native-reshade-xemfg-2026-10-19.md',
        'rollback/renodx-mfgunlock-1.4.5.addon64':args.official_addon,
        'notices/MFGAdaUnlock-MIT.txt':root/'LICENSE',
        'notices/MFG-Unlocker-MIT.txt':root/'src/addons/mfgunlock/xemfg/vendor/LICENSE.dashdogy.txt',
        'notices/MinHook-LICENSE.txt':root/'src/addons/mfgunlock/xemfg/vendor/third_party/minhook/LICENSE.txt',
        'notices/Intel-program-data.txt':root/'src/addons/mfgunlock/xemfg/vendor/XEFG-PROGRAMS-NOTICE.txt',
        'notices/AMD-FidelityFX-API-MIT.txt':root/'src/addons/mfgunlock/xemfg/vendor/ffx_api/LICENSE.txt',
        'notices/Intel-headers-MIT.txt':args.intel_sdk/'inc/MIT.txt',
        'notices/Intel-SDK-LICENSE.txt':args.intel_sdk/'LICENSE.txt',
        'notices/PROVENANCE.md':root/'src/addons/mfgunlock/xemfg/vendor/PROVENANCE.md',
        'evidence/ctest.log':root/'build/ctest-native.log',
        'evidence/ctest-r1.1.log':root/'build/ctest-native-r1.1.log',
        'evidence/ctest-r1.2.log':root/'build/ctest-native-r1.2.log',
        'evidence/ctest-r1.3.log':root/'build/ctest-native-r1.3.log',
        'evidence/ctest-r1.3-before-hook-fix.log':root/'build/ctest-native-r1.3-before-hook-fix.log',
        'evidence/ctest-r1.4.log':root/'build/ctest-native-r1.4.log',
        'evidence/ctest-r1.5.log':root/'build/ctest-native-r1.5.log',
        'evidence/ctest-r1.6.log':root/'build/ctest-native-r1.6.log',
        'evidence/ctest-r1.6-before-startup-fix.log':root/'build/ctest-native-r1.6-before-startup-fix.log',
        'evidence/intermediate/ctest-r1.6-targeted.log':root/'build/ctest-r1.6-targeted.log',
        'overlay-controls.md':root/'docs/research/native-xemfg-overlay-r1.2-2026-10-19.md',
        'interface-and-latency.md':root/'docs/research/xemfg-r1.3-interface-and-latency-2026-10-19.md',
        'compact-interface.md':root/'docs/research/xemfg-r1.4-compact-interface-2026-10-19.md',
        'ui-composition-and-pacing.md':root/'docs/research/xemfg-r1.5-ui-composition-and-pacing-2026-10-19.md',
    }
    for name in ('ENGINEERING_AUDIT.md','PACING_ANALYSIS.md','BENCHMARK_PLAN.md','CHANGELOG.md'):
        assets[('history/r1.6/' if args.revision!='R1.6' else '')+name]=root/'docs/research/xemfg-r1.6'/name
    for name in ('baseline.json','validation-summary.json'):
        assets['evidence/'+args.revision.lower()+'/'+name]=args.audit_evidence/name
    if args.revision=='R1.6':
        assets['evidence/r1.6/frame-store-benchmark-final.json']=args.audit_evidence/'frame-store-benchmark-final.json'
    else:
        assets['pacing-overlay-hud.md']=root/'docs/research/xemfg-r1.7-pacing-overlay-hud-2026-10-19.md'
        assets['evidence/ctest-r1.7.log']=root/'build/ctest-native-r1.7.log'
        for name in ('ctest-r1.7-overlay-first.log','ctest-r1.7-pacing-first.log','ctest-r1.7-hud-pacing.log'):
            assets['evidence/intermediate/'+name]=root/'build'/name
        for name in ('FRAMEPACING_REPORT.md','framepacing-analysis.json','framepacing.png'):
            assets['evidence/r1.6-game-reference/'+name]=args.audit_evidence/'game-reference'/name
        if shortcut_revision:
            assets['overlay-input-fix.md']=root/'docs/research/xemfg-r1.7.1-overlay-input-2026-10-19.md'
            assets['evidence/ctest-r1.7.1.log']=root/'build/ctest-native-r1.7.1.log'
            for name in ('short-tap-r17','initial-dedup','key-test-pass'):
                for file in ('result.json','MFGAdaUnlock-XeMFG.log'):
                    assets[f'evidence/intermediate/{name}/{file}']=args.audit_evidence/name/file
        if args.revision in ('R1.7.2','R1.7.3','R1.7.4'):
            assets['passive-card-focus.md']=root/'docs/research/xemfg-r1.7.2-passive-card-focus-2026-10-19.md'
            assets['evidence/ctest-r1.7.2.log']=root/'build/ctest-native-r1.7.2.log'
            for name in ('navigation','navigation-compact'):
                assets[f'evidence/ui/{name}-synthetic-status.png']=root/f'build/tests/ui-preview/{name}.png'
        if args.revision in ('R1.7.3','R1.7.4'):
            assets['overlay-routing.md']=root/'docs/research/xemfg-r1.7.3-overlay-routing-2026-10-19.md'
            assets['evidence/ctest-r1.7.3.log']=root/'build/ctest-native-r1.7.3.log'
            for name in ('result.json','MFGAdaUnlock-XeMFG.log','ReShade.log'):
                assets[f'evidence/intermediate/child-r172/{name}']=args.audit_evidence/'child-r172'/name
            for name in ('observation.json','MFGAdaUnlock-XeMFG.log','ReShade.log','ReShade.ini','MFGAdaUnlock.ini'):
                assets[f'evidence/r1.7.2-game-reference/{name}']=args.audit_evidence/'game-reference-r172'/name
        if args.revision=='R1.7.4':
            assets['window-hotkey.md']=root/'docs/research/xemfg-r1.7.4-window-hotkey-2026-10-19.md'
            assets['evidence/ctest-r1.7.4.log']=root/'build/ctest-native-r1.7.4.log'
            for name in ('result.json','MFGAdaUnlock-XeMFG.log','ReShade.log'):
                assets[f'evidence/intermediate/lost-legacy-r173/{name}']=args.audit_evidence/'lost-legacy-r173'/name
                assets[f'evidence/foreground-hotkey/{name}']=args.audit_evidence/'foreground-hotkey'/name
            for name in ('result.json','stdout.txt'):
                assets[f'evidence/window-hotkey/{name}']=args.audit_evidence/'window-hotkey'/name
            for name in ('observation.json','MFGAdaUnlock-XeMFG.log','ReShade.log','ReShade.ini','MFGAdaUnlock.ini'):
                assets[f'evidence/r1.7.3-game-reference/{name}']=args.audit_evidence/'game-reference-r173'/name
    for name in ('review.json','MFGAdaUnlock-XeMFG.log'):
        assets['evidence/r1.5-pacing-review/'+name]=args.pacing_review/name
    game_validation=None
    validation=json.loads((args.audit_evidence/'validation-summary.json').read_text(encoding='utf-8'))
    if validation['addon_sha256']!=sha(addon) or validation['ctest']['failed']:
        raise ValueError('Audit validation does not match the addon or has failed tests')
    if validation['ctest']['passed']!={'R1.6':62,'R1.7':63,'R1.7.1':64,'R1.7.2':66,'R1.7.3':66,'R1.7.4':66}[args.revision] or validation['ctest']['skipped']!=3:
        raise ValueError('Update the documented test scope before packaging a different suite')
    if args.revision=='R1.7.4':
        foreground=json.loads((args.audit_evidence/'foreground-hotkey/result.json').read_text(encoding='utf-8'))
        if foreground['exit'] or not foreground.get('foreground_hotkey_verified') or foreground['inputs']['renodx-mfgunlock-native.addon64']!=sha(addon):
            raise ValueError('Missing final-addon foreground hotkey/input release evidence')
        window=json.loads((args.audit_evidence/'window-hotkey/result.json').read_text(encoding='utf-8'))
        if window['exit'] or window['executable_sha256']!=sha(build/'Release/xemfg_window_tests.exe'):
            raise ValueError('Missing final window hotkey lifecycle evidence')
    if args.previous_addon:
        previous={'66384ac1f948964fc5fd1488267d021a22697f7c63b4e2b5abc8fd07222d99ac':'r1.1',
            '18bfb21b9d9b1da4006aac06adb77c9a1dc20b5313f5525fe263e1470da6eaf7':'r1.2',
            'f20a92b2ced415fbe8462b4cfb82f1f80b526e4a9b839bfbf6417e704bd7dcff':'r1.3',
            'd6204480a38682c04a7e4c5fcafec6c0e087517dffc30c39d0a5738efba2e324':'r1.4',
            'cd902f48500b8ee110c2cd7095a981694ebfbb371db3e5e7ad1aa78237d18647':'r1.5',
            '025c6996412f638a18ab53f601b1106aed16ebd7af3f907082f1a93646fd9c3c':'r1.6',
            'cce8fd873fa803bea7740379a86c3d472b58c0496bf48e8d27fc6db68b43f5fc':'r1.7',
            'a4fca91b5971f2bfb516ed284129fa623bc3bed2d86f336adf307c17a8628818':'r1.7.1',
            '8c9bbbf03e14bd4e33b19b1da8010991e71ebc78b17ac6d60782408928eb1a32':'r1.7.2',
            'b1001ca8a48c8f0cfa46ca40f2b18b6d2789df4a5d89d342c93f2e44b8d23021':'r1.7.3'}
        revision=previous.get(sha(args.previous_addon))
        if revision is None: raise ValueError('Previous native rollback must be a sealed known build')
        assets[f'rollback/renodx-mfgunlock-native-{revision}.addon64']=args.previous_addon
        if shortcut_revision:
            rollback=args.previous_addon.parent/'rollback/renodx-mfgunlock-native-r1.6.addon64'
            if sha(rollback)!='025c6996412f638a18ab53f601b1106aed16ebd7af3f907082f1a93646fd9c3c': raise ValueError('R1.6 rollback differs')
            assets['rollback/renodx-mfgunlock-native-r1.6.addon64']=rollback
            rollback=args.previous_addon.parent/'rollback/renodx-mfgunlock-native-r1.7.addon64'
            if rollback.is_file():
                if sha(rollback)!='cce8fd873fa803bea7740379a86c3d472b58c0496bf48e8d27fc6db68b43f5fc': raise ValueError('R1.7 rollback differs')
                assets['rollback/renodx-mfgunlock-native-r1.7.addon64']=rollback
            if args.revision in ('R1.7.3','R1.7.4'):
                rollback=args.previous_addon.parent/'rollback/renodx-mfgunlock-native-r1.7.1.addon64'
                if sha(rollback)!='a4fca91b5971f2bfb516ed284129fa623bc3bed2d86f336adf307c17a8628818': raise ValueError('R1.7.1 rollback differs')
                assets['rollback/renodx-mfgunlock-native-r1.7.1.addon64']=rollback
            if args.revision=='R1.7.4':
                rollback=args.previous_addon.parent/'rollback/renodx-mfgunlock-native-r1.7.2.addon64'
                if sha(rollback)!='8c9bbbf03e14bd4e33b19b1da8010991e71ebc78b17ac6d60782408928eb1a32': raise ValueError('R1.7.2 rollback differs')
                assets['rollback/renodx-mfgunlock-native-r1.7.2.addon64']=rollback
    for page in range(4):
        assets[f'evidence/ui/page-{page}-synthetic-status.png']=root/f'build/tests/ui-preview/page-{page}.png'
    assets['evidence/ui/large-font-synthetic-status.png']=root/'build/tests/ui-preview/large-font.png'
    for name in ('routing-expanded','routing-large-font','tooltip','multiplier-open','performance','performance-large-font'):
        assets[f'evidence/ui/{name}-synthetic-status.png']=root/f'build/tests/ui-preview/{name}.png'
    if args.revision!='R1.6':
        for name in ('pacing','pacing-large-font'):
            assets[f'evidence/ui/{name}-synthetic-status.png']=root/f'build/tests/ui-preview/{name}.png'
    if args.game_validation:
        game_validation=json.loads(args.game_validation.read_text(encoding='utf-8'))
        if game_validation['input_identity']['renodx-mfgunlock.addon64']!=sha(addon):
            raise ValueError('Gameplay evidence belongs to a different addon')
        if not game_validation.get('functional_runtime_activation_confirmed'):
            raise ValueError('Gameplay evidence did not confirm runtime activation')
        assets['evidence/game/validation.json']=args.game_validation
        for name in game_validation['artifacts']:
            src=args.game_validation.parent/name
            if src.resolve().parent!=args.game_validation.parent.resolve() or sha(src)!=game_validation['artifacts'][name]:
                raise ValueError('Gameplay evidence file changed or escapes its directory: '+name)
            assets['evidence/game/'+name]=src
    for count in (2,3,4,6):
        for case in sorted((root/'build/tests/native-checks').glob('native-*'),reverse=True):
            result=case/'result.json'; candidate=case/'renodx-mfgunlock-native.addon64'
            if not result.is_file() or not candidate.is_file() or sha(candidate)!=sha(addon): continue
            data=json.loads(result.read_text(encoding='utf-8'))
            if data['exit']!=0 or f'{count}x requested' not in data['stdout']: continue
            if shortcut_revision and not data.get('overlay_key_recovery_verified'): continue
            if args.revision in ('R1.7.3','R1.7.4') and not (data.get('overlay_child_recovery_verified') and data.get('overlay_input_receipts_verified')): continue
            if data['inputs']['libxell.dll']!='61aeeab7fdcfb6d1742f86c14c68c663d759607d01a6132645c857291eaebb86': continue
            for name in ('result.json','MFGAdaUnlock-XeMFG.log','ReShade.log','MFGAdaUnlock.ini'):
                assets[f'evidence/{count}x/{name}']=case/name
            break
        else:
            raise ValueError(f'Missing final-addon {count}x evidence')
    for case in sorted((root/'build/tests/native-checks').glob('native-*'),reverse=True):
        result=case/'result.json'
        if not result.is_file(): continue
        data=json.loads(result.read_text(encoding='utf-8'))
        if (data['exit']!=0 or '4x requested' not in data['stdout']
            or data['inputs']['renodx-mfgunlock-native.addon64']!=sha(addon)
            or data['inputs']['libxell.dll']!='d2030dcd694fda8f2ec7e044b13e6db8f0b56d4ba9113a5efad334e3f3ded8c7'): continue
        if shortcut_revision and not data.get('overlay_key_recovery_verified'): continue
        if args.revision in ('R1.7.3','R1.7.4') and not (data.get('overlay_child_recovery_verified') and data.get('overlay_input_receipts_verified')): continue
        for name in ('result.json','MFGAdaUnlock-XeMFG.log','ReShade.log','MFGAdaUnlock.ini'):
            assets[f'evidence/4x-xell-1.3.2.10/{name}']=case/name
        break
    else: raise ValueError('Missing final-addon alternative XeLL evidence')
    for name,src in assets.items():
        dest=output/name; dest.parent.mkdir(parents=True,exist_ok=True); shutil.copy2(src,dest)
    def git(*args):
        return subprocess.check_output(['git',*args],cwd=root,text=True).strip()
    source={}
    for folder in ('src','tests','tools','docs'):
        for path in sorted((root/folder).rglob('*')):
            if path.is_file() and '__pycache__' not in path.parts:
                source[path.relative_to(root).as_posix()]=sha(path)
    manifest={
        'identity':'Native ReShade XeMFG R1',
        'revision':args.revision,
        'source_commit':git('rev-parse','HEAD'),'branch':git('branch','--show-current'),
        'working_tree':git('status','--porcelain'),
        'official_1_4_5_commit':'0fe65579d287581107babe267ee3e48c9135bf8a',
        'previous_working_x1_3_commit':'0d8dc0b6d9aff9aad9afaf57f6939e94fada93c5',
        'license':'MIT bridge and reused MIT components; incorporated Intel data retains vendor terms',
        'optiscaler_dependency':False,'asi_dependency':False,
        'runtime_dlls_bundled':False,
        'intel_headers_commit':'de0fb9c1c510661c571164e1418ceca8101dab69',
        'upstream_mfg_commit':'a4cf2c4ab632e84c72254932793bb9e64b33d76e',
        'source_inputs_sha256':source,
        'compiler_inputs':compiler_inputs(build/'mfgunlock_native_xemfg.vcxproj'),
        'precompiled_inputs':json.loads((root/'build/xemfg-inputs/input-provenance.json').read_text(encoding='utf-8')),
        'test_scope':('R1.7.4: 66 passed, 3 optional skipped; real-SDK 2x/3x/4x/6x and alternate XeLL 4x retained. Extra foreground system-key test with real ReShade, discarded legacy messages and command capture/release; window hotkey text/rebind/disable/conflict/retirement checks. No complete Cyberpunk Home or performance gain claim.' if args.revision=='R1.7.4' else 'R1.7.3: 66 passed, 3 optional skipped; child-window held/short taps, exact native deduplication and bounded receipt verification in real-SDK 2x/3x/4x/6x and alternate XeLL 4x. Window tests reject unrelated targets and retain capture reasons. R1.7.2 passive-card tests retained. No complete gameplay Home fix or pacing gain claimed.' if args.revision=='R1.7.3' else 'R1.7.2: 66 passed, 3 optional skipped; wide/compact passive-card mouse and keyboard navigation, with real controls remaining navigable. R1.7.1 runtime/shortcut tests and real-SDK 2x/3x/4x/6x plus alternate XeLL retained. No actual gameplay Home/focus or pacing-gain claim.' if args.revision=='R1.7.2' else 'R1.7.1: 64 passed, 3 optional skipped; real-SDK 2x/3x/4x/6x and additional XeLL 1.3.2.10 at 4x; native Home and short-tap recovery with exact deduplication, including resize/FG-off. Numerical shortcut capture/rebind/overflow tests. Retains R1.7 runtime/HUD/pacing coverage. No actual game hotkey/focus or pacing-gain claim.' if args.revision=='R1.7.1' else 'R1.7: 63 passed, 3 optional skipped; real-SDK 2x/3x/4x/6x and additional XeLL 1.3.2.10 at 4x; manual ReShade runtime create/update/resize/retirement, Intel-specific optional HUD filtering, SDK policy activation/restoration. Occluded deadline bodies may be unobserved: null is not a passthrough proof. No R1.7 gameplay, image, hotkey or displayed-cadence claim.' if args.revision=='R1.7' else 'R1.6: 62 passed, 3 optional skipped; real-SDK 2x/3x/4x/6x and additional XeLL 1.3.2.10 at 4x; details in validation-summary.json. No gameplay/display cadence/physical latency claim.'),
        'gameplay_validated':bool(game_validation and game_validation.get('functional_and_initial_user_validation_complete')),
        'gameplay_validation_scope':None if not game_validation else {
            'game':'Cyberpunk 2077','sdk_outputs_observed':[2,3,4,6],
            'functional_runtime_activation':game_validation['functional_runtime_activation_confirmed'],
            'user_reports_image_and_response_good':game_validation.get('user_reports_image_and_response_good',False),
            'limits':game_validation['limits'],
            'evidence_source_commit':game_validation['source_commit'],
        },
        'assets':[{'name':name,'sha256':sha(output/name),'bytes':(output/name).stat().st_size} for name in assets],
    }
    (output/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
    (output/'SHA256SUMS.txt').write_text(''.join(f'{sha(output/name)}  {name}\n' for name in [*assets,'manifest.json']),encoding='utf-8')
    print(output)

if __name__=='__main__': main()
