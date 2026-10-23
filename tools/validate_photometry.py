"""Execute the exact V3 photometry fragment against its FP32 oracle.

Synthetic GPU correctness only. Does not validate a game's pixels, pacing,
full-kernel runtime cost, or physical input latency.
"""
import argparse
import csv
import ctypes as c
import hashlib
import json
import math
import subprocess
from pathlib import Path
from build_thin_geometry_variants import CudaDriverCompiler, fingerprint_elf

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--emitter',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    args=p.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    subprocess.run([str(args.emitter),'--emit',str(args.output)],check=True)
    body=(args.output/'photometry-program.ptx').read_text(encoding='ascii')
    rows=list(csv.DictReader((args.output/'photometry-cases.csv').open(newline='')))
    fields=['a0','a1','a2','b0','b1','b2','c0','c1','c2','d0','d1','d2']
    values=[float(row[k]) for row in rows for k in fields]
    expected=[float(row[k]) for row in rows for k in ['expected_base','expected_candidate']]
    source='''.version 8.0
.target sm_89
.address_size 64
.visible .entry Photometry(.param .u64 input, .param .u64 output, .param .u32 count){
.reg .b64 %ptr<4>;
.reg .b32 %index<4>;
.reg .pred %done;
.reg .f32 %qf<8>;
.reg .f32 %f<182>;
ld.param.u64 %ptr0,[input];
ld.param.u64 %ptr1,[output];
ld.param.u32 %index3,[count];
mov.u32 %index0,%ctaid.x;
mov.u32 %index1,%ntid.x;
mov.u32 %index2,%tid.x;
mad.lo.u32 %index0,%index0,%index1,%index2;
setp.ge.u32 %done,%index0,%index3;
@%done bra FINISH;
mul.wide.u32 %ptr2,%index0,48;
add.u64 %ptr2,%ptr0,%ptr2;
'''
    for i,reg in enumerate([115,116,117,119,120,121,125,126,127,131,132,133]):
        source+=f'ld.global.f32 %f{reg},[%ptr2+{4*i}];\n'
    source+=body+'''
mul.wide.u32 %ptr3,%index0,8;
add.u64 %ptr3,%ptr1,%ptr3;
st.global.f32 [%ptr3],%qf6;
st.global.f32 [%ptr3+4],%f176;
FINISH:
ret;
}
'''
    (args.output/'photometry-harness.ptx').write_text(source,encoding='ascii')
    compiler=CudaDriverCompiler();cuda=compiler.cuda
    module=c.c_void_p();allocations=[];ptr=c.c_uint64
    cuda.cuMemAlloc_v2.argtypes=[c.POINTER(ptr),c.c_size_t]
    cuda.cuMemFree_v2.argtypes=[ptr]
    cuda.cuMemcpyHtoD_v2.argtypes=[ptr,c.c_void_p,c.c_size_t]
    cuda.cuMemcpyDtoH_v2.argtypes=[c.c_void_p,ptr,c.c_size_t]
    cuda.cuModuleLoadData.argtypes=[c.POINTER(c.c_void_p),c.c_void_p]
    cuda.cuModuleGetFunction.argtypes=[c.POINTER(c.c_void_p),c.c_void_p,c.c_char_p]
    cuda.cuModuleUnload.argtypes=[c.c_void_p]
    cuda.cuLaunchKernel.argtypes=[c.c_void_p]+[c.c_uint]*7+[c.c_void_p,c.POINTER(c.c_void_p),c.c_void_p]
    cuda.cuCtxSynchronize.argtypes=[]
    try:
        cubin=compiler.compile(source,'photometry-oracle');encoded=c.create_string_buffer(cubin)
        compiler._check(cuda.cuModuleLoadData(c.byref(module),encoded),'load harness')
        function=c.c_void_p();compiler._check(cuda.cuModuleGetFunction(c.byref(function),module,b'Photometry'),'get function')
        host_input=(c.c_float*len(values))(*values);host_output=(c.c_float*len(expected))()
        inputs,outputs=ptr(),ptr()
        for target,size in [(inputs,c.sizeof(host_input)),(outputs,c.sizeof(host_output))]:
            compiler._check(cuda.cuMemAlloc_v2(c.byref(target),size),'allocate');allocations.append(target)
        compiler._check(cuda.cuMemcpyHtoD_v2(inputs,host_input,c.sizeof(host_input)),'upload')
        count=c.c_uint(len(rows));parameters=(c.c_void_p*3)(*[c.cast(c.byref(x),c.c_void_p) for x in (inputs,outputs,count)])
        compiler._check(cuda.cuLaunchKernel(function,(len(rows)+127)//128,1,1,128,1,1,0,None,parameters,None),'launch')
        compiler._check(cuda.cuCtxSynchronize(),'sync')
        compiler._check(cuda.cuMemcpyDtoH_v2(host_output,outputs,c.sizeof(host_output)),'download')
        errors=[abs(actual-ref) for actual,ref in zip(host_output,expected)]
        tolerance=0.0001
        failures=[i for i,(actual,ref,error) in enumerate(zip(host_output,expected,errors))
                  if not math.isfinite(actual) or error>tolerance*max(1,abs(ref))]
        report=dict(cases=len(rows),outputs=len(expected),max_absolute_error=max(errors),relative_or_absolute_tolerance=tolerance,
            failures=len(failures),fragment_sha256=hashlib.sha256(body.encode('ascii')).hexdigest(),
            cubin_sha256=hashlib.sha256(cubin).hexdigest(),harness_fingerprint=fingerprint_elf(cubin),
            validation='synthetic GPU numerical correctness; not gameplay, quality preference or kernel performance')
        (args.output/'gpu-validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
        print(json.dumps(report,indent=2))
        if failures:raise RuntimeError(f'{len(failures)} GPU/oracle mismatches')
    finally:
        for allocation in allocations:cuda.cuMemFree_v2(allocation)
        if module:cuda.cuModuleUnload(module)
        compiler.close()
if __name__=='__main__':main()
