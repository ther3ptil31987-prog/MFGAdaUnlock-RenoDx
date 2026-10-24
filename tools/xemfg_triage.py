# SPDX-License-Identifier: MIT
"""Classify one MFGAdaUnlock-XeMFG.log session by its first persistent cause.

Usage: python tools/xemfg_triage.py <log> [--pid N]
Without --pid the last session in the file is analyzed. Output is JSON.
"""
import json, sys

JITTER_KEY = '35112441167526893749578'


def sessions(lines):
    by_pid = {}
    for line in lines:
        line = line.strip()
        if not line:
            continue
        try:
            event = json.loads(line)
        except json.JSONDecodeError:
            continue
        by_pid.setdefault(event.get('pid'), []).append(event)
    return by_pid


def analyze(lines, pid=None):
    by_pid = sessions(lines)
    if not by_pid:
        return {'cause': 'empty_log'}
    events = by_pid[pid] if pid is not None else list(by_pid.values())[-1]
    kinds = [e.get('event') for e in events]
    details = [e.get('detail', '') for e in events if e.get('event') == 'native_bridge']
    admissions = [e for e in events if e.get('event') == 'native_frame_admission']
    requested = [e for e in admissions if e.get('requested')]
    owners = {e.get('owner') for e in events if e.get('event') == 'context_create'}
    interpolated = any(e.get('event') == 'output_status' and e.get('tensor_launches', 0) > 0 and e.get('enabled')
                       for e in events)
    boundary = [e for e in events if e.get('event') == 'native_simulation_boundary_sleep']
    rejections = [e for e in events if e.get('event') == 'native_sleep_rejected']
    result = {
        'pid': events[0].get('pid'),
        'route': 'streamline' if any(k == 'native_game_options' for k in kinds) and 'host' in owners
                 else 'native_xefg' if 'game' in owners else 'streamline' if requested else 'none',
        'fg_requested': bool(requested),
        'interpolated': interpolated,
        'blocker': requested[-1].get('blocker') if requested else None,
        'explicit_sleep_rejections': len(rejections),
    }
    if boundary:
        result['boundary_sleep_result'] = boundary[-1].get('result')
    xell = [e for e in events if e.get('event') == 'native_xell_failure']
    if xell:
        result['xell_failure'] = {'operation': xell[0].get('operation'), 'xell_result': xell[0].get('xell_result')}
    messages = [e.get('message') for e in events if e.get('event') == 'native_xell_log' and e.get('level', 0) >= 3]
    if messages:
        result['xell_errors'] = sorted(set(messages))[:4]
    # 1.5.5 diagnostics: which Intel file was refused, the game's VSync, and
    # whether the session ended or the log was taken while it still ran.
    files = [e for e in events if e.get('event') == 'runtime_files']
    if files:
        result['runtime_files'] = {name: files[-1].get(name, {}).get('state') for name in ('libxess_fg.dll', 'libxell.dll')}
    vsync = [e.get('sync_interval') for e in events if e.get('event') == 'native_game_vsync']
    summaries = [e for e in events if e.get('event') == 'native_summary']
    if vsync:
        result['game_vsync'] = vsync[-1]
    elif summaries and summaries[-1].get('game_sync_interval', -1) >= 0:
        result['game_vsync'] = summaries[-1]['game_sync_interval']
    result['device_lost'] = any(e.get('event') == 'native_device_failure' for e in events)
    shutdown = any(e.get('event') == 'native_streamline_shutdown' and e.get('phase') == 'complete' for e in events) or any(
        e.get('event') == 'native_output_retired' for e in events)
    result['ended'] = 'clean' if shutdown else 'device_lost' if result['device_lost'] else 'open'
    if summaries:
        result['last_summary_gap_ms'] = events[-1].get('tick', 0) - summaries[-1].get('tick', 0)
    startup = [e for e in events if e.get('event') == 'startup_rejected']
    jitter = any('jittered-motion' in d for d in details) or any(
        e.get('event') == 'shader_decode_failure' and e.get('key') == JITTER_KEY for e in events) or any(
        e.get('event') == 'native_constants_status' and e.get('jittered') == 1 and e.get('issues', 0) & 16 for e in events)
    if interpolated:
        cause = 'interpolating'
    elif startup:
        cause = 'startup_rejected:' + str(startup[-1].get('reason'))
    elif jitter:
        cause = 'jittered_motion_unsupported'
    elif any(e.get('event') == 'native_device_failure' for e in events):
        cause = 'device_lost'
    elif any('but expected frame' in m for m in messages):
        # XeLL bound to XeFG requires consecutive sleep frame ids.
        cause = 'xell_frame_sequence_gap'
    elif boundary and any(e.get('result') for e in boundary):
        cause = 'boundary_sleep_failed'
    elif result['blocker'] == 'source_wait_not_completed':
        cause = 'source_wait_missing'
    elif result['blocker']:
        cause = result['blocker']
    elif not requested and admissions:
        cause = 'game_fg_disabled'
    else:
        cause = 'no_output'
    result['cause'] = cause
    return result


if __name__ == '__main__':
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    pid = int(sys.argv[sys.argv.index('--pid') + 1]) if '--pid' in sys.argv else None
    with open(sys.argv[1], encoding='utf-8', errors='replace') as f:
        print(json.dumps(analyze(f.read().splitlines(), pid), indent=2))
