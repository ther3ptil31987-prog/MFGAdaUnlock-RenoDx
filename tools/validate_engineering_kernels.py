"""Compile complete C++-emitted 1.4.4 blends and offline ablations; no dispatch."""
import argparse
import ctypes as c
import hashlib
import json
import subprocess
from pathlib import Path
from build_thin_geometry_variants import extract_kernels,CudaDriverCompiler,fingerprint_elf,local_storage_bytes

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--emitter',type=Path,required=True)
    p.add_argument('--provider',type=Path,action='append',required=True)
    p.add_argument('--output',type=Path,required=True)
    args=p.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    results=[];compiler=CudaDriverCompiler();cuda=compiler.cuda
    cuda.cuModuleLoadData.argtypes=[c.POINTER(c.c_void_p),c.c_void_p]
    cuda.cuModuleGetFunction.argtypes=[c.POINTER(c.c_void_p),c.c_void_p,c.c_char_p]
    cuda.cuModuleUnload.argtypes=[c.c_void_p]
    cuda.cuFuncGetAttribute.argtypes=[c.POINTER(c.c_int),c.c_int,c.c_void_p]
    try:
        for i,provider in enumerate(args.provider):
            matches=[sources[120] for name,sources,cubin in extract_kernels(provider)
                     if name=='Kernel_BlendCandidatesFused' and 120 in sources]
            if len(matches)!=1:raise ValueError('Expected unique full blend source')
            original=args.output/f'provider-{i}-original.ptx';original.write_text(matches[0],encoding='ascii',newline='\n')
            for mode in ['v3','v3-no-photo','v3-no-direction','v2','base']:
                ptx=args.output/f'provider-{i}-{mode}.ptx'
                subprocess.run([str(args.emitter),str(original),str(ptx),mode],check=True)
                data=ptx.read_bytes();cubin=compiler.compile(data.decode('ascii'),f'blend-{i}-{mode}')
                (args.output/f'provider-{i}-{mode}.cubin').write_bytes(cubin)
                module=c.c_void_p();function=c.c_void_p();encoded=c.create_string_buffer(cubin)
                compiler._check(cuda.cuModuleLoadData(c.byref(module),encoded),'load complete kernel')
                try:
                    compiler._check(cuda.cuModuleGetFunction(c.byref(function),module,b'Kernel_BlendCandidatesFused'),'get function')
                    attributes={}
                    for key,attr in [('max_threads',0),('shared_bytes',1),('local_bytes',3),('registers',4)]:
                        value=c.c_int();compiler._check(cuda.cuFuncGetAttribute(c.byref(value),attr,function),'attribute');attributes[key]=value.value
                finally:cuda.cuModuleUnload(module)
                results.append(dict(provider_sha256=hashlib.sha256(provider.read_bytes()).hexdigest(),mode=mode,
                    ptx_sha256=hashlib.sha256(data).hexdigest(),cubin_sha256=hashlib.sha256(cubin).hexdigest(),
                    fingerprint=fingerprint_elf(cubin),local_storage=local_storage_bytes(cubin),function_attributes=attributes,dispatched=False))
    finally:compiler.close()
    report=dict(validation='complete-kernel compilation and structural ablations; no timing, occupancy, image-quality or FPS claim',results=results)
    (args.output/'validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report,indent=2))
if __name__=='__main__':main()
