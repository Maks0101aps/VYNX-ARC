"""Windows publication-boundary handle/identity/Restart Manager snapshot.

Called synchronously by an explicit publication-diagnostics build. Never closes
another process's handles, terminates a process, or changes filesystem policy.
"""
import argparse
import ctypes as c
from ctypes import wintypes as w
import datetime
import json
import os
import pathlib


def snapshot(pid, source, temp, include_restart=True):
    k = c.WinDLL('kernel32', use_last_error=True)
    nt = c.WinDLL('ntdll')
    k.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
    k.OpenProcess.restype = w.HANDLE
    k.CloseHandle.argtypes = [w.HANDLE]
    k.GetCurrentProcess.restype = w.HANDLE
    k.DuplicateHandle.argtypes = [w.HANDLE, w.HANDLE, w.HANDLE, c.POINTER(w.HANDLE), w.DWORD, w.BOOL, w.DWORD]
    k.GetFinalPathNameByHandleW.argtypes = [w.HANDLE, w.LPWSTR, w.DWORD, w.DWORD]
    k.CreateFileW.argtypes = [w.LPCWSTR, w.DWORD, w.DWORD, c.c_void_p, w.DWORD, w.DWORD, w.HANDLE]
    k.CreateFileW.restype = w.HANDLE
    nt.NtQuerySystemInformation.argtypes = [w.ULONG, c.c_void_p, w.ULONG, c.POINTER(w.ULONG)]
    nt.NtQuerySystemInformation.restype = c.c_long

    class Handle(c.Structure):
        _fields_ = [('object', c.c_void_p), ('pid', c.c_size_t), ('value', c.c_size_t),
                    ('access', w.ULONG), ('trace', w.USHORT), ('type', w.USHORT),
                    ('attributes', w.ULONG), ('reserved', w.ULONG)]

    class Info(c.Structure):
        _fields_ = [('attributes', w.DWORD), ('created', w.FILETIME),
                    ('accessed', w.FILETIME), ('written', w.FILETIME),
                    ('volume', w.DWORD), ('size_hi', w.DWORD), ('size_lo', w.DWORD),
                    ('links', w.DWORD), ('id_hi', w.DWORD), ('id_lo', w.DWORD)]
    k.GetFileInformationByHandle.argtypes = [w.HANDLE, c.POINTER(Info)]
    wanted = [str(pathlib.Path(p).resolve()).removeprefix('\\\\?\\').lower()
              for p in (source, temp, pathlib.Path(source).parent)]
    result = {'timestamp_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
              'pid': pid, 'source': str(source), 'temp': str(temp), 'handles': [], 'probes': []}
    process = k.OpenProcess(0x40, False, pid)  # PROCESS_DUP_HANDLE
    if not process:
        result['handle_error'] = c.get_last_error()
    else:
        try:
            size = 1024 * 1024
            while True:
                buffer = c.create_string_buffer(size)
                needed = w.ULONG()
                status = nt.NtQuerySystemInformation(64, buffer, size, c.byref(needed))
                if status == 0:
                    break
                if status != -1073741820 or size >= 128 * 1024 * 1024:
                    raise OSError(f'NtQuerySystemInformation status {status:#x}')
                size = max(size * 2, needed.value + 65536)
            count = c.c_size_t.from_buffer(buffer).value
            offset = c.sizeof(c.c_size_t) * 2
            if offset + count * c.sizeof(Handle) > size:
                raise OSError('Invalid system handle snapshot length')
            failed = []
            for index in range(count):
                h = Handle.from_buffer(buffer, offset + index * c.sizeof(Handle))
                if h.pid != pid:
                    continue
                duplicate = w.HANDLE()
                if not k.DuplicateHandle(process, h.value, k.GetCurrentProcess(), c.byref(duplicate), 0, False, 2):
                    failed.append({'handle': h.value, 'object_type': h.type, 'access': hex(h.access), 'error': c.get_last_error()})
                    continue
                try:
                    name = c.create_unicode_buffer(32768)
                    n = k.GetFinalPathNameByHandleW(duplicate, name, len(name), 0)
                    if not 0 < n < len(name):
                        continue
                    normalized = name.value.removeprefix('\\\\?\\').lower()
                    if not any(normalized == p or normalized.startswith(p + ':') for p in wanted):
                        continue
                    info = Info()
                    identity = None
                    if k.GetFileInformationByHandle(duplicate, c.byref(info)):
                        identity = {'volume_serial': info.volume, 'file_id': (info.id_hi << 32) | info.id_lo,
                                    'attributes': info.attributes}
                    result['handles'].append({'handle': h.value, 'object_type': h.type, 'granted_access': hex(h.access),
                                              'path': name.value, 'identity': identity})
                finally:
                    k.CloseHandle(duplicate)
            result['duplicate_failures'] = failed
        except OSError as error:
            result['handle_error'] = str(error)
        finally:
            k.CloseHandle(process)
    # Success proves no live non-delete-sharing handle at this instant. Kernel
    # handle enumeration exposes access, not share flags; do not confuse them.
    for p in [source, temp, str(source) + ':Zone.Identifier', str(temp) + ':Zone.Identifier']:
        handle = k.CreateFileW(str(p), 0x10000, 7, None, 3, 0x02000000, None)
        error = c.get_last_error() if handle == c.c_void_p(-1).value else 0
        if not error:
            info = Info()
            identity = None
            if k.GetFileInformationByHandle(handle, c.byref(info)):
                identity = {'volume_serial': info.volume, 'file_id': (info.id_hi << 32) | info.id_lo,
                            'attributes': info.attributes}
            k.CloseHandle(handle)
        else:
            identity = None
        result['probes'].append({'path': str(p), 'delete_access_error': error, 'identity': identity})
    result['mappings'] = mapped_files(pid, source, temp)
    if include_restart:
        result['restart_manager'] = restart_manager(source, temp)
        result['external_process_snapshots'] = [snapshot(row['pid'], source, temp, False)
            for row in result['restart_manager'].get('processes', []) if row['pid'] != pid]
        print('KERNEL_SNAPSHOT ' + json.dumps(result, ensure_ascii=False), flush=True)
    return result


def mapped_files(pid, source, temp):
    k = c.WinDLL('kernel32', use_last_error=True)
    psapi = c.WinDLL('psapi', use_last_error=True)
    k.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
    k.OpenProcess.restype = w.HANDLE
    k.CloseHandle.argtypes = [w.HANDLE]

    class Memory(c.Structure):
        _fields_ = [('base', c.c_void_p), ('allocation', c.c_void_p), ('allocation_protect', w.DWORD),
                    ('partition', w.WORD), ('size', c.c_size_t), ('state', w.DWORD),
                    ('protect', w.DWORD), ('type', w.DWORD)]
    k.VirtualQueryEx.argtypes = [w.HANDLE, c.c_void_p, c.POINTER(Memory), c.c_size_t]
    k.VirtualQueryEx.restype = c.c_size_t
    psapi.GetMappedFileNameW.argtypes = [w.HANDLE, c.c_void_p, w.LPWSTR, w.DWORD]
    process = k.OpenProcess(0x410, False, pid)
    if not process:
        return {'error': c.get_last_error()}
    wanted = [str(pathlib.Path(p).resolve()).removeprefix('\\\\?\\')[2:].lower() for p in [source, temp]]
    rows = []
    address = 0
    try:
        while address < 0x7fffffffffff:
            memory = Memory()
            if not k.VirtualQueryEx(process, address, c.byref(memory), c.sizeof(memory)):
                break
            if memory.state == 0x1000 and memory.type in [0x40000, 0x1000000]:
                name = c.create_unicode_buffer(32768)
                if psapi.GetMappedFileNameW(process, address, name, len(name)):
                    if any(name.value.lower().endswith(p) for p in wanted):
                        rows.append({'address': address, 'size': memory.size, 'type': memory.type, 'path': name.value})
            next_address = (memory.base or 0) + memory.size
            if next_address <= address:
                return {'error': 'Invalid VirtualQueryEx region', 'regions': rows}
            address = next_address
        return {'regions': rows}
    finally:
        k.CloseHandle(process)


def restart_manager(source, temp):
    rm = c.WinDLL('rstrtmgr')

    class Unique(c.Structure):
        _fields_ = [('pid', w.DWORD), ('start', w.FILETIME)]

    class Process(c.Structure):
        _fields_ = [('process', Unique), ('name', w.WCHAR * 256), ('service', w.WCHAR * 64),
                    ('type', c.c_int), ('status', w.ULONG), ('session', w.DWORD), ('restartable', w.BOOL)]
    rm.RmStartSession.argtypes = [c.POINTER(w.DWORD), w.DWORD, w.LPWSTR]
    rm.RmRegisterResources.argtypes = [w.DWORD, w.UINT, c.POINTER(w.LPCWSTR), w.UINT, c.c_void_p, w.UINT, c.c_void_p]
    rm.RmGetList.argtypes = [w.DWORD, c.POINTER(w.UINT), c.POINTER(w.UINT), c.POINTER(Process), c.POINTER(w.DWORD)]
    session = w.DWORD()
    key = c.create_unicode_buffer(33)
    code = rm.RmStartSession(c.byref(session), 0, key)
    if code:
        return {'error': code, 'stage': 'start'}
    try:
        files = (w.LPCWSTR * 2)(str(source), str(temp))
        code = rm.RmRegisterResources(session, 2, files, 0, None, 0, None)
        if code:
            return {'error': code, 'stage': 'register'}
        needed, count, reasons = w.UINT(), w.UINT(), w.DWORD()
        code = rm.RmGetList(session, c.byref(needed), c.byref(count), None, c.byref(reasons))
        if code == 234:
            count.value = needed.value
            rows = (Process * count.value)()
            code = rm.RmGetList(session, c.byref(needed), c.byref(count), rows, c.byref(reasons))
        else:
            rows = []
        if code:
            return {'error': code, 'stage': 'list'}
        return {'processes': [{'pid': row.process.pid, 'name': row.name, 'application_type': row.type}
                              for row in list(rows)[:count.value]], 'reboot_reasons': reasons.value}
    finally:
        rm.RmEndSession(session)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', type=int, required=True)
    parser.add_argument('--source', type=pathlib.Path, required=True)
    parser.add_argument('--temp', type=pathlib.Path, required=True)
    args = parser.parse_args()
    if os.name != 'nt':
        parser.error('Windows only')
    snapshot(args.pid, args.source.resolve(), args.temp.resolve())
