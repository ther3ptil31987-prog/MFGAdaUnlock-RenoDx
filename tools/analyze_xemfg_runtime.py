"""Summarize observed runtime failures without inferring GPU idle or scanout."""
import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path

CONSTANT_ISSUES={1:'metadata/ABI',2:'camera motion absent or unknown',4:'3D motion or unknown',
    8:'depth convention differs from initialization',16:'jitter convention differs from initialization',
    32:'reset undefined',64:'camera basis/position non-finite',128:'projection non-finite',
    256:'jitter offset non-finite',512:'motion scale non-finite'}

def analyze(events,pid=None):
    pids=sorted({e['pid'] for e in events if 'pid' in e})
    if pid is None:
        if len(pids)!=1: raise ValueError('Specify --pid: appended logs may interleave launchers and rendering processes')
        pid=pids[0]
    selected=[e for e in events if e.get('pid')==pid]
    if not selected: raise ValueError('PID absent from log')
    by=defaultdict(list)
    for e in selected: by[e['event']].append(e)
    calls=defaultdict(list)
    for e in by['runtime_call']: calls[e['call']].append(e)
    pending=[]
    for e in by['runtime_call_pending']:
        e=dict(e); completed=e.get('gpu_completed');begin=e.get('gpu_begin_fence')
        e['source_queue_progress']=('unavailable' if completed is None or not begin else
            'device_removed' if completed==(1<<64)-1 else 'begin_fence_completed' if completed>=begin else 'begin_fence_pending')
        final=e.get('presentation_gpu_completed');beginFinal=e.get('presentation_gpu_begin_fence')
        e['presentation_queue_progress']=('unavailable' if final is None or not beginFinal else
            'device_removed' if final==(1<<64)-1 else 'begin_fence_completed' if final>=beginFinal else 'begin_fence_pending')
        pending.append(e)
    statuses=by['output_status']; active=[e for e in statuses if e.get('enabled')]
    issues=[]
    for e in by['native_constants_status']:
        e=dict(e);e['reasons']=[label for bit,label in CONSTANT_ISSUES.items() if e['issues']&bit];issues.append(e)
    success=Counter(e['frames_presented'] for e in active if e['interpolation_result']==0 and e['frames_presented']>1)
    return dict(pid=pid,events=dict(Counter(e['event'] for e in selected)),
        successful_sdk_output_samples={str(k):v for k,v in success.items()},
        negative_sdk_results=[e for e in active if e['interpolation_result']<0],
        sdk_warnings=[e for e in active if e['interpolation_result']>0],
        max_tensor_launches=max((e.get('tensor_launches',0) for e in statuses),default=0),
        constant_statuses=issues,marker_entrypoints=by['native_marker_hook'],
        slow_calls={name:dict(samples=len(es),max_duration_us=max(e['duration_us'] for e in es),
            failed=[e for e in es if e.get('result_known') and (e.get('failed') or e['result']<0)]) for name,es in calls.items()},
        pending_calls=pending,gpu_source_queue_samples=sum(e.get('queue','source')=='source' for e in by['gpu_queue_span']),
        gpu_presentation_queue_samples=sum(e.get('queue')=='presentation' for e in by['gpu_queue_span']),
        queue_clocks=by['gpu_queue_clock'],sampled_input_calls=by['native_input_call'],viewport_options=by['native_viewport_options'],
        native_output_continuity=by['native_output_retained']+by['native_output_reused'],
        initialization_failures=by['startup_rejected']+by['native_swapchain_init_failed'],
        lifetime_events=by['native_output_retired']+by['native_retirement_pending']+by['native_final_output_retired'],
        limits=['SDK outputs are not displayed cadence.', 'Source-queue spans do not isolate XeFG kernels or private queues.',
            'An uncompleted fence may be queue backlog; it is not automatically a deadlock.',
            'A completed source fence does not prove the GPU is globally idle.', 'Bounded and optional logs do not audit every frame.'])

def main():
    p=argparse.ArgumentParser();p.add_argument('log',type=Path);p.add_argument('--pid',type=int);p.add_argument('--output',type=Path)
    a=p.parse_args();events=[]
    for line in a.log.read_text(encoding='utf-8-sig').splitlines():
        try: events.append(json.loads(line))
        except json.JSONDecodeError: continue
    result=json.dumps(analyze(events,a.pid),ensure_ascii=False,indent=2)+'\n'
    if a.output: a.output.write_text(result,encoding='utf-8')
    else: print(result,end='')
if __name__=='__main__': main()
