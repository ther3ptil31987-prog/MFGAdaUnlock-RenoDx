"""Check artifact identity and provenance against the actual experimental build."""
import json
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from write_build_manifest import make_manifest, sha

addon, project = map(Path, sys.argv[1:3])
variant = sys.argv[3] if len(sys.argv) > 3 else 'engineering-experimental'
if not addon.is_file() or not project.is_file():
    print('SKIP: build the experimental addon before artifact provenance tests')
    raise SystemExit(77)
manifest = make_manifest(addon, ROOT, variant, build_project=project)
assert manifest['baseline_commit'] == '3e898157fd59ed9eef65c181619060ddb305c6bd'
assert manifest['addon_sha256'] == sha(addon.read_bytes())
assert manifest['confidence_history'] is False and manifest['temporal_launch_hooks'] is False
assert 'src/addons/mfgunlock/kernel_identities.hpp' in manifest['source_inputs']
assert 'tools/write_build_manifest.py' in manifest['source_inputs']
assert manifest['build_project_sha256'] == sha(project.read_bytes())
assert manifest['instruction_profiles']['kernel_identities']
assert any(x.lower().endswith('detours.lib') for x in manifest['compiler_inputs']['files'])
assert any(x.lower().endswith('sl_dlss_g.h') for x in manifest['compiler_inputs']['files'])
if variant == 'release-1.4.5':
    assert manifest['release_version'] == '1.4.5'
    assert manifest['baseline_version'] == '1.4.4'
    assert manifest['release_channel'] == 'release'
    assert 'MFGUNLOCK_RELEASE_1_4_5' in manifest['build_defines']


def rejected(call):
    try:
        call()
    except ValueError:
        return
    raise AssertionError('Invalid artifact provenance was accepted')


rejected(lambda: make_manifest(addon, ROOT, 'release-1.4.4'))
other = 'release-1.4.5' if variant == 'engineering-experimental' else 'engineering-experimental'
rejected(lambda: make_manifest(addon, ROOT, other, build_project=project))
rejected(lambda: make_manifest(addon, ROOT, variant))
with tempfile.TemporaryDirectory() as temporary:
    wrong = Path(temporary) / 'wrong.vcxproj'
    wrong.write_text('<Project/>', encoding='utf-8')
    rejected(lambda: make_manifest(addon, ROOT, variant, build_project=wrong))
print(f'Actual {variant} binary, baseline, flags, payloads and wrong-label rejection passed')
