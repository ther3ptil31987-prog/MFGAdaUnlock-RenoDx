"""Write a reviewable identity manifest without exporting any provider payloads."""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path
from build_thin_geometry_variants import load_generated_records, fingerprint_elf
from inspect_generated_cubins import arrays

IDENTITIES = {
    'release-1.4.5': b'Release 1.4.5 - Engineering & Reliability',
    'engineering-experimental': b'Experimental 1.4.4 Engineering',
    'release-1.4.4': b'Release 1.4.4 - DLSS-G 310.9.16 Hotfix',
    'release-1.4.3': b'Release 1.4.3 - Continuous Border Stability',
    'control': b'Release 1.4.2 - Local Stability & CPU Overhead',
    'border-axis-candidate': b'1.4.2-base - Border Axis Candidate',
}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def compiler_inputs(project, configuration='Release'):
    """Record MSVC's actual header/library read set, including external inputs."""
    files, logs = {}, {}
    directory = project.parent / (project.stem + '.dir') / configuration
    for pattern in ['CL.read.1.tlog', 'link.read.1.tlog']:
        matches = list(directory.rglob(pattern))
        if len(matches) != 1:
            raise ValueError('Missing or ambiguous actual compiler/linker input log: ' + pattern)
        log = matches[0]
        logs[pattern] = sha(log.read_bytes())
        for line in log.read_text(encoding='utf-16').splitlines():
            path = Path(line)
            if line.startswith('^') or not path.is_file():
                continue
            if path.suffix.lower() in ['.h', '.hpp', '.inl', '.lib', '.cpp'] or not path.suffix:
                files[str(path)] = sha(path.read_bytes())
    if not any(x.lower().endswith('detours.lib') for x in files):
        raise ValueError('Actual external Detours link input was not recorded')
    return dict(read_logs=logs, files=files,
        scope=f'MSVC {configuration} header/library read set; includes external SDK/CRT/dependency hashes, not a reproducible-build guarantee')


def check_validation(report, variant, directory, header_digest):
    if report['metadata']['variant'] != variant:
        raise ValueError('Validation report belongs to another variant')
    if report['metadata']['generated_table_sha256'] != header_digest:
        raise ValueError('Validation report belongs to another generated payload table')
    # The emitter must still be the same source that this binary was built from.
    if report['metadata']['warp_source_sha256'] != sha((directory / 'adaptive_quality_v3.hpp').read_bytes()):
        raise ValueError('Warp source changed since validation')
    if report['metadata']['rewrite_source_sha256'] != sha((directory / 'thin_geometry.hpp').read_bytes()):
        raise ValueError('Rewrite source changed since validation')


def make_manifest(addon, root, variant, validation=None, build_project=None):
    binary = addon.read_bytes()
    if not binary.startswith(b'MZ') or IDENTITIES[variant] not in binary:
        raise ValueError('Addon binary identity does not match requested variant')
    for identity in ['engineering-experimental', 'release-1.4.5']:
        if IDENTITIES[identity] in binary and variant != identity:
            raise ValueError('Engineering binary cannot be labeled as another release')
    if variant in ['engineering-experimental', 'release-1.4.5'] and subprocess.run(
        ['git','merge-base','--is-ancestor','3e898157fd59ed9eef65c181619060ddb305c6bd','HEAD'],cwd=root).returncode:
        raise ValueError('Experimental source is not descended from the authoritative 1.4.4 baseline')
    directory = root / 'src/addons/mfgunlock'
    header = directory / 'thin_geometry_stability.generated.hpp'
    blackwell = directory / 'blackwell_cubins.generated.hpp'
    availability_header = directory / 'thin_geometry_cubins.generated.hpp'
    payloads = []
    for record in load_generated_records(header):
        payloads.append(dict(mechanism=record['mechanism'],
            source_fingerprint=record['source_fingerprint'],
            source_fnv1a64=f"{record['source_hash']:016x}",
            slot_size=record['slot_size'],
            replacement_sha256=sha(record['replacement']),
            replacement_fingerprint=fingerprint_elf(record['replacement'])))
    blackwell_payloads = [dict(symbol=name, sha256=sha(data), fingerprint=fingerprint_elf(data))
        for name, data in arrays(blackwell).items()]
    commit = subprocess.check_output(['git','rev-parse','HEAD'], cwd=root, text=True).strip()
    status = subprocess.check_output(['git','status','--porcelain','--untracked-files=normal'], cwd=root, text=True)
    source_inputs = {}
    for subdir in ['src', 'tools', 'tests', 'docs']:
        for file in sorted((root / subdir).rglob('*')):
            if file.is_file() and file.suffix in ['.hpp','.cpp','.py','.ps1','.txt','.json','.md','.wprp'] and 'generated.hpp' not in file.name:
                source_inputs[file.relative_to(root).as_posix()] = sha(file.read_bytes())
    manifest = dict(baseline_version='1.4.3' if variant == 'release-1.4.4' else '1.4.2',
        baseline_commit='e09ed477e81825cf1ec788e86b8289a0994cb525' if variant == 'release-1.4.4' else '3a2c021d5882ffd62e2872d84ed752398be58f58',
        release_version='1.4.4' if variant == 'release-1.4.4' else '1.4.3' if variant == 'release-1.4.3' else ('1.4.2' if variant == 'control' else '1.4.2-base'),
        variant=variant, addon_sha256=sha(binary), source_commit=commit, source_modified=bool(status.strip()),
        source_inputs=source_inputs, geometry='Local Stable', inpaint='V2 Compatibility',
        confidence_history=False, temporal_launch_hooks=False,
        provider_validation='exact 310.9.0/310.9.1/310.9.16 profiles; 310.9.16 offline checked; in-game validation pending',
        generated_headers={header.name:sha(header.read_bytes()),blackwell.name:sha(blackwell.read_bytes()),
            availability_header.name:sha(availability_header.read_bytes())},
        thin_geometry_payload_inputs=payloads, blackwell_payload_inputs=blackwell_payloads,
        in_game_validation='initial maintainer feedback positive; comparative benchmarks pending'
            if variant == 'release-1.4.3' else 'pending')
    if variant in ['engineering-experimental', 'release-1.4.5']:
        if not build_project or not build_project.is_file():
            raise ValueError('Engineering manifest requires the actual generated build project')
        import xml.etree.ElementTree as ET
        defines=[]
        for group in ET.parse(build_project).iter():
            if group.tag.endswith('ItemDefinitionGroup') and 'Release|x64' in group.attrib.get('Condition',''):
                for item in group.iter():
                    if item.tag.endswith('PreprocessorDefinitions') and item.text:
                        defines += [x for x in item.text.split(';') if x and not x.startswith('%')]
        required={'MFGUNLOCK_ENGINEERING_EXPERIMENTAL','MFGUNLOCK_LOCAL_LOW_OVERHEAD',
            'MFGUNLOCK_LOCAL_STABILITY','MFGUNLOCK_CONTINUOUS_BORDER_AXES','MFGUNLOCK_RELEASE_1_4_3','MFGUNLOCK_RELEASE_1_4_4'}
        if variant == 'release-1.4.5':
            required.add('MFGUNLOCK_RELEASE_1_4_5')
        elif 'MFGUNLOCK_RELEASE_1_4_5' in defines:
            raise ValueError('Release 1.4.5 flags cannot identify the historical experiment')
        if not required.issubset(defines):raise ValueError('Build flags do not match the engineering release policy')
        manifest.update(baseline_version='1.4.4',baseline_commit='3e898157fd59ed9eef65c181619060ddb305c6bd',
            release_version='1.4.5' if variant == 'release-1.4.5' else '1.4.4-engineering-experimental',
            release_channel='release' if variant == 'release-1.4.5' else 'experimental',build_defines=defines,
            build_project_sha256=sha(build_project.read_bytes()),
            compiler_inputs=compiler_inputs(build_project),
            source_status=status.splitlines(),
            source_diff_sha256=sha(subprocess.check_output(['git','diff','--binary','HEAD'],cwd=root)),
            instruction_profiles=json.loads((directory/'binary_profiles.json').read_text(encoding='utf-8')),
            provider_validation='offline content mutation and transactional apply/restore; runtime active provider remains capture-dependent',
            hot_unload='unsupported; addon pinned after first observed Present; module leases retained until process exit',
            gameplay_validation='pending; no FPS, displayed pacing, image-quality or physical latency gain established')
    if validation:
        report = json.loads(validation.read_text())
        check_validation(report, variant, directory, sha(header.read_bytes()))
        manifest['assembly_validation'] = report
        manifest['assembly_report_sha256'] = sha(validation.read_bytes())
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--addon', type=Path, required=True)
    parser.add_argument('--source-root', type=Path, required=True)
    parser.add_argument('--variant', choices=IDENTITIES, required=True)
    parser.add_argument('--validation', type=Path)
    parser.add_argument('--build-project', type=Path)
    args = parser.parse_args()
    manifest = make_manifest(args.addon, args.source_root, args.variant, args.validation, args.build_project)
    output = args.addon.with_suffix(args.addon.suffix+'.manifest.json')
    output.write_text(json.dumps(manifest, indent=2)+'\n', encoding='utf-8')
    print(f'Build manifest: {output.name}; {manifest["addon_sha256"]}')

if __name__ == '__main__':
    main()
