"""Normalize the config-only AOT version alias used by Aster's WAMR 2.4.3.

The vendor core changes AOT_CURRENT_VERSION from 5 to 6 without changing
the serialized layout (see build_wamrc_windows.ps1). Only the pinned
upstream Windows RV32 compiler may use this alias; other formats fail closed.
"""
import hashlib,struct
def normalize(path,compiler,host,expected):
    data=path.read_bytes()
    if len(data)<8 or data[:4]!=b'\0aot':raise ValueError('Invalid AOT compiler output')
    actual=struct.unpack_from('<I',data,4)[0]
    if actual==expected:return
    if (actual,expected)!=(5,6) or host.get('emitted_aot_format')!=5:raise ValueError('AOT format differs from compiler profile')
    if hashlib.sha256(compiler.read_bytes()).hexdigest()!=host['wamrc_sha256']:raise ValueError('Unpinned compiler cannot use the AOT version alias')
    path.write_bytes(data[:4]+struct.pack('<I',expected)+data[8:])
