import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from analyze_xemfg_runtime import analyze
events=[dict(pid=3,event='output_status',enabled=1,interpolation_result=3,frames_presented=1,tensor_launches=0),
    dict(pid=3,event='output_status',enabled=1,interpolation_result=0,frames_presented=4,tensor_launches=52),
    dict(pid=3,event='native_constants_status',issues=66),
    dict(pid=3,event='runtime_call_pending',call='xefg_present',gpu_begin_fence=9,gpu_completed=9),
    dict(pid=3,event='runtime_call_pending',call='xell_sleep',gpu_begin_fence=10,gpu_completed=9),
    dict(pid=3,event='runtime_call_pending',call='xefg_destroy',gpu_begin_fence=10,gpu_completed=(1<<64)-1),
    dict(pid=3,event='runtime_call',call='prepare_present',duration_us=123000,result=0,result_known=False),
    dict(pid=3,event='gpu_queue_span',queue='source'),dict(pid=3,event='gpu_queue_span',queue='presentation')]
r=analyze(events)
assert r['successful_sdk_output_samples']=={'4':1} and not r['negative_sdk_results'] and len(r['sdk_warnings'])==1
assert len(r['constant_statuses'][0]['reasons'])==2
assert [e['source_queue_progress'] for e in r['pending_calls']]==['begin_fence_completed','begin_fence_pending','device_removed']
assert not r['slow_calls']['prepare_present']['failed']
assert r['gpu_source_queue_samples']==1 and r['gpu_presentation_queue_samples']==1
try: analyze(events+[dict(pid=8,event='ready')])
except ValueError: pass
else: raise AssertionError('interleaved PID selection guessed')
assert analyze(events+[dict(pid=8,event='ready')],3)['max_tensor_launches']==52
print('PASS: explicit PID, warnings vs errors, constant reasons, CPU calls and bounded GPU-fence interpretation')
