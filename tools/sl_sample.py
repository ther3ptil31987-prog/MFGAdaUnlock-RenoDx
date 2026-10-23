# SPDX-License-Identifier: MIT
"""Build NVIDIA's Streamline_Sample against a given Streamline SDK release.

python tools/sl_sample.py build --sample <checkout> --sdk-zip <streamline-sdk-vX.zip>

The sample checkout must match the SDK version (NVIDIA-RTX/Streamline_Sample
tags follow Streamline tags). DirectX 12 only: Vulkan is disabled and the
Windows SDK dxc.exe compiles DXIL. A local, uncommitted patch lets the
environment variable MFG_SAMPLE_DLSSG=1 start with DLSS-G and Reflex on,
so a test can run without UI interaction. Nothing here is redistributed.
"""
import argparse, glob, os, pathlib, re, shutil, subprocess, sys, zipfile

PATCH_MARK = '// MFGAdaUnlock test patch'
HELPER = PATCH_MARK + ': MFG_SAMPLE_DLSSG=1 starts with DLSS-G and Reflex enabled.\n' \
    'inline bool MfgSampleAutoFG() { char* v=nullptr; size_t n=0; _dupenv_s(&v,&n,"MFG_SAMPLE_DLSSG");\n' \
    '  const bool on=v && v[0]==\'1\'; free(v); return on; }\n'


def dxc():
    found = sorted(glob.glob(r'C:\Program Files (x86)\Windows Kits\10\bin\10.*\x64\dxc.exe'))
    if not found:
        raise SystemExit('dxc.exe not found in the Windows SDK')
    return found[-1]


def extract(sample, archive):
    target = sample / 'streamline'
    if (target / 'include' / 'sl.h').is_file():
        return
    with zipfile.ZipFile(archive) as z:
        names = z.namelist()
        # Releases either place include/ at the root or under one folder.
        prefix = min((n[:n.index('include/')] for n in names if n.endswith('include/sl.h')), key=len)
        for name in names:
            if not name.startswith(prefix) or name.endswith('/'):
                continue
            out = target / name[len(prefix):]
            out.parent.mkdir(parents=True, exist_ok=True)
            with z.open(name) as src, open(out, 'wb') as dst:
                shutil.copyfileobj(src, dst)


def patch(sample):
    ui = sample / 'src' / 'UIData.h'
    text = ui.read_text(encoding='utf-8')
    if PATCH_MARK in text:
        return
    text, fg = re.subn(r'DLSSG_mode\s*=\s*sl::DLSSGMode::eOff', 'DLSSG_mode = MfgSampleAutoFG()?sl::DLSSGMode::eOn:sl::DLSSGMode::eOff', text)
    text, rx = re.subn(r'REFLEX_Mode\s*=\s*static_cast<int>\(sl::ReflexMode::eOff\)',
                       'REFLEX_Mode = static_cast<int>(MfgSampleAutoFG()?sl::ReflexMode::eLowLatency:sl::ReflexMode::eOff)', text)
    if fg != 1 or rx != 1:
        raise SystemExit(f'UIData.h layout not recognized (DLSS-G {fg}, Reflex {rx})')
    include = text.index('#include')
    text = text[:include] + '#include <cstdlib>\n' + HELPER + text[include:]
    ui.write_text(text, encoding='utf-8')


def build(sample, archive):
    extract(sample, archive)
    patch(sample)
    out = sample / '_build'
    subprocess.run(['cmake', '-S', str(sample), '-B', str(out), '-G', 'Visual Studio 17 2022', '-A', 'x64',
                    '-DUSE_SL=ON', '-DDONUT_WITH_VULKAN=OFF', '-DNVRHI_WITH_VULKAN=OFF', '-DDONUT_WITH_DX11=OFF',
                    # Newer donut reads SHADERMAKE_DXC_PATH; older ShaderMake searches the
                    # registry SDK root, which can differ from the installed bin folder.
                    '-DSHADERMAKE_FIND_DXC_VK=OFF', '-DSHADERMAKE_FIND_DXC_SPIRV=OFF', f'-DSHADERMAKE_DXC_PATH={dxc()}',
                    f'-DDXC_PATH={dxc()}', f'-DFXC_PATH={pathlib.Path(dxc()).with_name("fxc.exe")}'], check=True)
    subprocess.run(['cmake', '--build', str(out), '--config', 'Release', '--parallel', '8'], check=True)
    exes = [p for p in sample.rglob('StreamlineSample.exe') if 'Release' in str(p) or 'bin' in str(p)]
    if not exes:
        raise SystemExit('StreamlineSample.exe not produced')
    print(max(exes, key=lambda p: p.stat().st_mtime))


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('command', choices=['build'])
    ap.add_argument('--sample', type=pathlib.Path, required=True)
    ap.add_argument('--sdk-zip', type=pathlib.Path, required=True)
    a = ap.parse_args()
    build(a.sample.resolve(), a.sdk_zip.resolve())
