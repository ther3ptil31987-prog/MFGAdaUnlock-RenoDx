# SPDX-License-Identifier: MIT
# Replays field sessions through the triage analyzer. The addon is not
# re-executed: this checks that diagnostics still name each session's cause.
import json, sys, pathlib
root = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(root.parent / 'tools'))
import xemfg_triage

corpus = root / 'fixtures' / 'replay'
expected = json.loads((corpus / 'expected.json').read_text(encoding='utf-8'))
failures = []
for name, want in expected['sessions'].items():
    got = xemfg_triage.analyze((corpus / name).read_text(encoding='utf-8').splitlines())
    for key, value in want.items():
        if key in ('game', 'addon_sha256'):
            continue
        if got.get(key) != value:
            failures.append(f'{name}: {key} expected {value!r}, got {got.get(key)!r}')
    print(f'{name}: {got["cause"]} ({got.get("blocker")})')
if failures:
    print('\n'.join(failures))
    raise SystemExit(1)
print(f'PASS: {len(expected["sessions"])} field sessions classified by their known cause')
