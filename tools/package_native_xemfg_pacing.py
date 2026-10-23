"""Seal a native candidate and exact local validation; bundle no runtime DLLs."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import xml.etree.ElementTree as ET
import zipfile
from write_build_manifest import compiler_inputs


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--revision', choices=('R1.8', 'R1.9', 'R1.9.1', 'R1.9.2', 'R1.9.3', 'R1.9.4', 'R1.9.5'), default='R1.8')
    for name in ('output', 'evidence', 'previous-addon', 'official-addon', 'intel-sdk'):
        ap.add_argument('--' + name, type=Path, required=True)
    a = ap.parse_args()
    compat = a.revision != 'R1.8'
    legacy = a.revision == 'R1.9.5'
    producer = a.revision in ('R1.9.4','R1.9.5')
    retirement = a.revision in ('R1.9.3','R1.9.4','R1.9.5')
    startup = a.revision in ('R1.9.2','R1.9.3','R1.9.4','R1.9.5')
    handoff = a.revision in ('R1.9.1', 'R1.9.2','R1.9.3','R1.9.4','R1.9.5')
    revision = a.revision
    previous = 'R1.9.4' if legacy else 'R1.9.3' if producer else 'R1.9.2' if retirement else 'R1.9.1' if startup else 'R1.9' if handoff else 'R1.8' if compat else 'R1.7.5'
    total = 101 if legacy else 97 if producer else 95 if retirement else 94 if startup else 89 if compat else 80
    report_name = 'legacy-factory-report.md' if legacy else 'producer-command-report.md' if producer else 'presentation-retirement-report.md' if retirement else 'presentation-ownership-report.md' if startup else 'game-status-report.md' if handoff else 'compatibility-cards-report.md' if compat else 'pacing-reflex-report.md'
    root = Path(__file__).resolve().parent.parent
    def git(*args):
        return subprocess.check_output(['git', *args], cwd=root, text=True).strip()
    if git('status', '--porcelain'):
        raise ValueError('Commit the reviewed source before sealing')
    addon = root / 'build/tests/xemfg/Release/renodx-mfgunlock-native.addon64'
    digest = sha(addon)
    validation = json.loads((a.evidence / 'validation-summary.json').read_text(encoding='utf-8'))
    if validation['addon_sha256'] != digest or validation['ctest'] != dict(total=total, passed=total-3, skipped=3, failed=0):
        raise ValueError('Missing matching complete suite')
    xml = ET.parse(root / f'build/ctest-native-{revision.lower()}.xml').getroot()
    cases = xml.findall('.//testcase')
    if len(cases) != total or any(c.find('failure') is not None for c in cases):
        raise ValueError('CTest XML differs from validation')
    expected = {f'{route}-{m}x-61aeeab7' for route in ('XeLL', 'Reflex') for m in (2, 3, 4, 6)}
    expected |= {'XeLL-4x-d2030dcd', 'Reflex-4x-d2030dcd'}
    if set(validation['sdk_cases']) != expected:
        raise ValueError('Incomplete SDK / latency matrix')
    for key, receipt in validation['sdk_cases'].items():
        result = json.loads((Path(receipt['directory']) / 'result.json').read_text())
        if result['exit'] or result['inputs']['renodx-mfgunlock-native.addon64'] != digest:
            raise ValueError('Stale SDK evidence: ' + key)
        if not result['inputs']['libxell.dll'].startswith(key.rsplit('-', 1)[-1]):
            raise ValueError('XeLL runtime differs from the matrix label: ' + key)
        if not all(result[k] for k in ('control_preferences_persisted', 'sdk_policy_activation_verified', 'hud_filter_verified')):
            raise ValueError('SDK control contract failed: ' + key)
        if handoff and not result.get('passthrough_identity_verified'):
            raise ValueError('Passthrough present identity not witnessed: ' + key)
        route = key.split('-')[0]
        if result['latency_output'] != route or f'Latency route: Reflex -> {route};' not in result['stdout']:
            raise ValueError('Latency route not witnessed: ' + key)
    if compat:
        expected_layouts = {'storage-only', 'preloaded-storage', 'mutated-storage', 'missing', 'preloaded-storage-Reflex'}
        if set(validation['compat_cases']) != expected_layouts:
            raise ValueError('Incomplete runtime layout coverage')
        for layout, receipt in validation['compat_cases'].items():
            result = json.loads((Path(receipt['directory']) / 'result.json').read_text())
            if result['exit'] or result['inputs']['renodx-mfgunlock-native.addon64'] != digest:
                raise ValueError('Stale compatibility evidence: ' + layout)
            fields = ('startup_rejection_verified', 'model_unpublished') if layout in ('mutated-storage', 'missing') else ('runtime_selection_verified', 'control_preferences_persisted', 'hud_filter_verified')
            if not all(result.get(k) for k in fields):
                raise ValueError('Compatibility contract failed: ' + layout)
    if startup:
        if set(validation.get('streamline_cases', {})) != {'XeLL-early', 'XeLL-late', 'Reflex-early', 'Reflex-late'}:
            raise ValueError('Missing real Streamline lifecycle matrix')
        for label, receipt in validation['streamline_cases'].items():
            result = json.loads((Path(receipt['directory']) / 'result.json').read_text())
            if result['exit'] or result['inputs']['renodx-mfgunlock-native.addon64'] != digest or not all(result.get(k) for k in ('presentation_isolated', 'shutdown_completed', 'inference_verified')):
                raise ValueError('Real Streamline lifecycle failed: ' + label)
        capcom = json.loads((Path(validation['capcom_flags']['directory']) / 'result.json').read_text())
        if capcom['exit'] or capcom['inputs']['renodx-mfgunlock-native.addon64'] != digest:
            raise ValueError('Missing matching RE Engine flag coverage')
    if retirement:
        result = json.loads((Path(validation['retirement_case']['directory']) / 'result.json').read_text())
        if result['exit'] or result['inputs']['renodx-mfgunlock-native.addon64'] != digest or not result.get('retirement_during_sleep_verified'):
            raise ValueError('Presentation retirement while source sleep is held was not verified')
    if producer:
        for key, field in (('producer_case','producer_shadow_verified'),('visible_case','visible_presents_verified')):
            result=json.loads((Path(validation[key]['directory'])/'result.json').read_text())
            if result['exit'] or result['inputs']['renodx-mfgunlock-native.addon64']!=digest or not result.get(field):
                raise ValueError('Missing matching producer/visible evidence: '+key)
    if legacy:
        expected_legacy={f'{kind}-{route}' for kind in ('native','real_streamline') for route in ('XeLL','Reflex')}
        if set(validation.get('legacy_cases',{}))!=expected_legacy:
            raise ValueError('Incomplete legacy factory coverage')
        for label,receipt in validation['legacy_cases'].items():
            result=json.loads((Path(receipt['directory'])/'result.json').read_text())
            if result['exit'] or result['inputs']['renodx-mfgunlock-native.addon64']!=digest or not result.get('legacy_factory_verified'):
                raise ValueError('Legacy factory contract failed: '+label)
        if set(validation.get('control_streamline_cases',{}))!={'XeLL','Reflex'}:
            raise ValueError('Missing Control runtime lifecycle matrix')
        for label,receipt in validation['control_streamline_cases'].items():
            result=json.loads((Path(receipt['directory'])/'result.json').read_text())
            if result['exit'] or result['inputs']['renodx-mfgunlock-native.addon64']!=digest or not all(result.get(k) for k in ('legacy_factory_verified','presentation_isolated','shutdown_completed','inference_verified')):
                raise ValueError('Control runtime lifecycle failed: '+label)
    previous_hash = '66eb4e89bb6b695d476a42bc5431a33801fd627d68cee7834e0eca188e02af81' if legacy else '8e4ffb153b40bed7d54d0c51f0a037d41773190f9c02f24b8bc139fc34f694c2' if producer else 'e7e3a6c437d2dc19a3eabdb1dc03bf298539c2a129275acd3c8ca3b38fdad15c' if retirement else '62a85ad1d8c7ca63e5e99406c56bd77888370d1cb41ff2f43ef92f780c47b5e9' if startup else '513d6dde26309a2737c29a23f31e46322a779bc27ee6605488a2ff0490a9fed1' if handoff else '6825a8e2d7a3f5d57899f24db192be9b3bc68d3196e8d402760b7d8da577cf42' if compat else 'e054d4919753de2431a3259048c689982d6e5cc6d9f8d7f0b972179bdef14080'
    if sha(a.previous_addon) != previous_hash:
        raise ValueError(previous + ' rollback differs')
    if sha(a.official_addon) != '9715987f6323a2699dd5b7636412258a6b97a180a4d70c0f79d286714e2972e2':
        raise ValueError('Official rollback differs')
    if a.output.exists() or Path(str(a.output) + '.zip').exists():
        raise ValueError('Refuse to overwrite a sealed package')
    assets = {
        'renodx-mfgunlock.addon64': addon,
        report_name: root / ('docs/research/xemfg-r1.9.5-legacy-factory-2026-10-20.md' if legacy else 'docs/research/xemfg-r1.9.4-producer-commands-2026-10-20.md' if producer else 'docs/research/xemfg-r1.9.3-presentation-retirement-2026-10-20.md' if retirement else 'docs/research/xemfg-r1.9.2-presentation-ownership-2026-10-20.md' if startup else 'docs/research/xemfg-r1.9.1-game-status-2026-10-20.md' if handoff else 'docs/research/xemfg-r1.9-compatibility-cards-2026-10-20.md' if compat else 'docs/research/xemfg-r1.8-pacing-reflex-2026-10-20.md'),
        f'{"archive" if startup else "rollback"}/renodx-mfgunlock-{previous.lower()}.addon64': a.previous_addon,
        'rollback/renodx-mfgunlock-1.4.5.addon64': a.official_addon,
        'Restore-MFGBackup.ps1': root / 'tools/Restore-MFGBackup.ps1',
        'notices/MFGAdaUnlock-MIT.txt': root / 'LICENSE',
        'notices/MFG-Unlocker-MIT.txt': root / 'src/addons/mfgunlock/xemfg/vendor/LICENSE.dashdogy.txt',
        'notices/PROVENANCE.md': root / 'src/addons/mfgunlock/xemfg/vendor/PROVENANCE.md',
        'notices/MinHook-LICENSE.txt': root / 'src/addons/mfgunlock/xemfg/vendor/third_party/minhook/LICENSE.txt',
        'notices/Intel-program-data.txt': root / 'src/addons/mfgunlock/xemfg/vendor/XEFG-PROGRAMS-NOTICE.txt',
        'notices/Intel-headers-MIT.txt': a.intel_sdk / 'inc/MIT.txt',
        'notices/Intel-SDK-LICENSE.txt': a.intel_sdk / 'LICENSE.txt',
        'evidence/validation-summary.json': a.evidence / 'validation-summary.json',
        'evidence/ctest.log': root / f'build/ctest-native-{revision.lower()}.log',
        'evidence/ctest.xml': root / f'build/ctest-native-{revision.lower()}.xml',
        'evidence/last-test.log': root / 'build/tests/Testing/Temporary/LastTest.log',
    }
    if compat:
        assets['Install-NativeXeMfgLocal.ps1'] = root / 'tools/Install-NativeXeMfgLocal.ps1'
    for label, receipt in (validation['sdk_cases'] | validation.get('compat_cases', {})).items():
        directory = Path(receipt['directory'])
        for name in ('result.json', 'MFGAdaUnlock-XeMFG.log', 'ReShade.log'):
            assets[f'evidence/{label}/{name}'] = directory / name
    if startup:
        for label, receipt in validation['streamline_cases'].items():
            for name in ('result.json', 'MFGAdaUnlock-XeMFG.log', 'ReShade.log', 'sl.log'):
                assets[f'evidence/Streamline-{label}/{name}'] = Path(receipt['directory']) / name
        for name in ('result.json', 'MFGAdaUnlock-XeMFG.log', 'ReShade.log'):
            assets[f'evidence/Capcom-flags/{name}'] = Path(validation['capcom_flags']['directory']) / name
    if retirement:
        for name in ('result.json', 'MFGAdaUnlock-XeMFG.log', 'ReShade.log'):
            assets[f'evidence/Retirement/{name}'] = Path(validation['retirement_case']['directory']) / name
    if producer:
        for label,key in (('Producer','producer_case'),('Visible','visible_case')):
            for name in ('result.json','MFGAdaUnlock-XeMFG.log','ReShade.log'):
                assets[f'evidence/{label}/{name}']=Path(validation[key]['directory'])/name
        if not legacy:
            for name in ('pragmata-boot.json','producer-negative.json'):
                assets['evidence/'+name]=a.evidence/name
            boot=json.loads((a.evidence/'pragmata-boot.json').read_text())
            if boot['addon_sha256']!=digest or boot['admission']['resources']!=7 or boot['admission']['failed']:
                raise ValueError('Missing matching game boot/input evidence')
    if legacy:
        for label,receipt in validation['legacy_cases'].items():
            for name in ('result.json','MFGAdaUnlock-XeMFG.log','ReShade.log'):
                assets[f'evidence/Legacy-{label}/{name}']=Path(receipt['directory'])/name
        for label,receipt in validation['control_streamline_cases'].items():
            for name in ('result.json','MFGAdaUnlock-XeMFG.log','ReShade.log'):
                assets[f'evidence/Control-Streamline-{label}/{name}']=Path(receipt['directory'])/name
        for name in ('control-baseline.json','legacy-negative.json'):
            assets['evidence/'+name]=a.evidence/name
        negative=json.loads((a.evidence/'legacy-negative.json').read_text())
        if negative['exit']==0 or negative['inputs']['renodx-mfgunlock-native.addon64']!=previous_hash or 'native output/Streamline/XeLL not ready' not in negative['stderr']:
            raise ValueError('Missing sealed R1.9.4 negative reproduction')
    for page in range(4):
        assets[f'evidence/ui/page-{page}.png'] = root / f'build/tests/ui-preview/page-{page}.png'
    a.output.mkdir(parents=True)
    for name, source in assets.items():
        dest = a.output / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, dest)
    readme = (
        'MFG Unlock 1.4.5 + XeMFG R1.8 - local candidate\n\n'
        'New: XeMFG with Reflex -> Reflex or Reflex -> XeLL; driver-metered queue-ahead; CUDA PSO recorder recognition.\n'
        'Reflex output: enable Reflex in game; Latency > Advanced latency routing > Latency output > Reflex; restart.\n'
        'Auto still uses XeLL. UI Composition remains optional and off by default.\n'
        'General > Advanced pacing comparison > Queue metered frames early compares the new and previous CPU timing.\n'
        'Queue-ahead requires accepted driver metering and sync interval zero. All SDK resource waits remain.\n\n'
        'Close the game and back up the installed addon before replacing only renodx-mfgunlock.addon64.\n'
        'Keep ReShade, RenoDX, Intel runtime DLLs and INIs. No OptiScaler or ASI dependency. No runtime DLLs included.\n'
        '77 tests passed; 3 optional historical checks skipped. Numerical CUDA coverage: 9/17 layer programs.\n'
        'Real SDK tests prove dispatch and ABI routing; no gameplay FPS, displayed pacing or physical latency gain is claimed.\n'
        'The 1.6.2 beta was inspected statically; this is not a complete port of its unpublished changes.\n'
        'Read pacing-reflex-report.md for profile limitations and evidence.\n'
        'Rollback: close the game and restore R1.7.5 or official 1.4.5 from rollback; install only one MFG addon.\n')
    if compat:
        readme = (f'MFG Unlock 1.4.5 + XeMFG {revision} - local candidate\n\n'
            'Compact cards restored. Runtime selection follows the exact admitted loaded pair, including _storage_.\n'
            'Unknown or missing Intel pairs remain refused; the panel now explains startup failures.\n'
            'Reflex -> Reflex and Reflex -> XeLL from R1.8 remain available. Restart after selecting the output.\n'
            'UI Composition remains optional and off by default. No OptiScaler or ASI dependency.\n'
            'Supported XeFG: 1.3.1.78; XeLL: 1.3.0.5 or 1.3.2.10, admitted by exact hashes.\n'
            'No runtime DLLs included. The local installer can refresh the root and existing _storage_ copies with backup.\n'
            f'{total-3} tests passed; 3 optional historical checks skipped. Real SDK layout tests include mutated/missing runtime rejection.\n'
            'Gameplay activation in each game still requires a fresh scene/log; no new pacing or latency gain is claimed.\n'
            f'Read {report_name}. Rollback addon: {"official DLSS-G 1.4.5" if startup else previous+" or official DLSS-G 1.4.5"} in rollback/.\n'
            'For game runtime rollback, use Restore-MFGBackup.ps1 with the installer backup directory.\n')
    if handoff and not startup:
        readme += ('R1.9.1: updates the Intel Present ID even when a batch is bypassed; suppresses only NVIDIA-owned Reflex/backbuffer warnings after NVIDIA FG is disabled.\n'
            'Other status errors and all input validation remain. New bounded diagnostics distinguish depth rejection from game options/state.\n'
            'Pragmata R1.9 failed to interpolate and the user observed flashing. R1.9.1 is a targeted correction requiring a fresh game test, not a confirmed flicker fix.\n')
    if startup:
        readme += ('R1.9.2: disables NVIDIA DLSS-G presentation callbacks at a host startup boundary through the public Streamline API; Intel exclusively owns presentation.\n'
            'Game feature enable/disable requests remain logical controls of the Intel replacement. Discovery is drained before Streamline shutdown.\n'
            'Four real Streamline/ReShade/Intel tests cover early/late startup, XeLL/Reflex, 4x inference, same-window output recreation and shutdown.\n'
            'R1.9.1 failed during Pragmata boot. It is archived for provenance, not recommended as game rollback; use official 1.4.5 and the installation snapshot.\n'
            'Pragmata gameplay activation, missing depth and flicker are not confirmed fixed by these isolated tests.\n')
    if retirement:
        readme += ('R1.9.3: retires the Intel FG presentation context with the application swapchain, while an old source sleep can safely keep its separate XeLL context alive.\n'
            'The regression test fails on R1.9.2 and passes here with a held source call and same-window output recreation.\n'
            'Pragmata R1.9.2 froze in a repeated 0x80070005 creation loop. The new lifetime fix still needs confirmation in that game.\n')
    if producer:
        readme += ('R1.9.4: accepts borrowed producer command lists with readable object-owned vtables and pinned image-backed COM implementations. Unknown/non-code implementations remain rejected.\n'
            'Volatile depth is copied at the original tag through the producer list; no fabricated resource lifetime. Producer and resource must belong to the output device.\n'
            'Pragmata boot: disabling the Steam overlay eliminated the swapchain recreation failure in a controlled boot. Keeping it enabled still freezes; this compatibility problem is NOT fixed.\n'
            'Keep the Steam overlay disabled for this Pragmata candidate. Gameplay interpolation still requires scene validation.\n'
            'The newest installation backup may contain R1.9.3. For a stable rollback use official 1.4.5 in rollback/, or the earlier game snapshot containing official 1.4.5 and the original runtimes.\n')
    if legacy:
        readme = (f'MFG Unlock 1.4.5 + XeMFG {revision} - local candidate\n\n'
            'Adds IDXGIFactory::CreateSwapChain support for D3D12 games, including the API used by CONTROL Resonant.\n'
            'Preserves buffer, format, sample, flags and fullscreen fields when routing to the Intel output. Unsupported formats use the original native call unchanged.\n'
            '98 tests passed; 3 optional historical checks skipped. New real-SDK tests cover legacy creation, FP16 fallback, resize, same-window recreation, inference and shutdown, with XeLL and Reflex output.\n'
            'The same isolated legacy test fails on sealed R1.9.4. This is not yet a CONTROL Resonant scene validation.\n'
            'Existing DLSS-G 1.4.5, CUDA/Tensor, Reflex/XeLL, UI and pacing policies are retained; no performance or displayed-cadence gain is claimed.\n'
            'UI Composition remains optional and off by default. No OptiScaler/ASI dependency. No runtime DLLs bundled.\n'
            'Pragmata R1.9.4 interpolated in a separate gameplay session with Steam overlay disabled. Compatibility with Steam overlay enabled remains unresolved; keep it disabled for the Pragmata workaround.\n'
            'Close the game before installation. Preserve ReShade, RenoDX and settings. Use Install-NativeXeMfgLocal.ps1 with the exact admitted Intel pair and a backup directory.\n'
            'Rollback: close the game and restore the installation snapshot. Previous R1.9.4 is archived; official DLSS-G 1.4.5 is in rollback/.\n'
            f'Read {report_name} for evidence and limits.\n')
    (a.output / 'README.txt').write_text(readme, encoding='utf-8')
    source = {p.relative_to(root).as_posix(): sha(p) for folder in ('src', 'tests', 'tools', 'docs')
              for p in sorted((root / folder).rglob('*')) if p.is_file() and '__pycache__' not in p.parts}
    manifest = dict(identity='Native ReShade XeMFG '+revision, revision=revision, source_commit=git('rev-parse', 'HEAD'),
        branch=git('branch', '--show-current'), base_commit='a003223a16c9336fe8ddf8746c16578a719eb145' if legacy else '60df2d970670cf850a7b661f04f8a69bd5902265' if producer else '96b9038d5c1add9efec9623fd0851feacc832ee3' if retirement else '0cbedd11ebd00a806a790fffee7510490ec72996' if startup else '1d007337be46ef0445b51544efa86b27506a3acb' if handoff else '8f7a45eedb5a54f4c29af1420facc8216fe8ca77' if compat else 'd3588a20bc950c0f13fcba627fd4d2c6d40c8088',
        official_1_4_5_commit='0fe65579d287581107babe267ee3e48c9135bf8a',
        addon_sha256=digest, optiscaler_dependency=False, asi_dependency=False, runtime_dlls_bundled=False,
        gameplay_validated=False, displayed_pacing_gain_measured=False, physical_latency_gain_measured=False,
        cuda_numerical_layer_coverage=dict(available=9, total=17),
        upstream_cuda_commit='29504cdd5d209a25e3b91e809a7e0d6f9db8cbdc',
        beta_asi_sha256='f34abd14975a5328ae3baf613f681b75d3075a4ff2a80da20cfefbaa0e9fab7b',
        beta_code_imported=False, source_inputs_sha256=source,
        compiler_inputs=compiler_inputs(root / 'build/tests/xemfg/mfgunlock_native_xemfg.vcxproj'),
        precompiled_inputs=json.loads((root / 'build/xemfg-inputs/input-provenance.json').read_text(encoding='utf-8')),
        assets=[dict(name=p.relative_to(a.output).as_posix(), sha256=sha(p), bytes=p.stat().st_size)
                for p in sorted(a.output.rglob('*')) if p.is_file()])
    if producer:
        manifest['borrowed_producer_commands']=True
        manifest['pragmata_steam_overlay_compatibility']='unresolved with overlay enabled; disabled overlay required for tested boot'
        manifest['pragmata_boot_inputs_accepted']=None if legacy else True
    if legacy:
        manifest['legacy_dxgi_factory_creation']=True
        manifest['control_scene_validated']=False
    (a.output / 'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf-8')
    (a.output / 'SHA256SUMS.txt').write_text(''.join(f'{sha(p)}  {p.relative_to(a.output).as_posix()}\n'
        for p in sorted(a.output.rglob('*')) if p.is_file()), encoding='utf-8')
    archive = Path(str(a.output) + '.zip')
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for p in sorted(a.output.rglob('*')):
            if p.is_file():
                z.write(p, p.relative_to(a.output).as_posix())
    print(json.dumps(dict(directory=str(a.output), archive=str(archive), zip_sha256=sha(archive), addon_sha256=digest), indent=2))


if __name__ == '__main__':
    main()
