"""Dump EIP and probable return addresses of all threads of a 32-bit (WOW64) process.

Usage: python -I stackdump.py <pid> [modbase_lo modbase_hi]
"""
import ctypes
import ctypes.wintypes as wt
import sys

k32 = ctypes.WinDLL('kernel32', use_last_error=True)
TH32CS_SNAPTHREAD = 4
THREAD_ALL = 0x1F03FF
PROCESS_ALL = 0x1F0FFF


class THREADENTRY32(ctypes.Structure):
    _fields_ = [('dwSize', wt.DWORD), ('cntUsage', wt.DWORD), ('th32ThreadID', wt.DWORD),
                ('th32OwnerProcessID', wt.DWORD), ('tpBasePri', wt.LONG), ('tpDeltaPri', wt.LONG),
                ('dwFlags', wt.DWORD)]


class FLOATING_SAVE_AREA(ctypes.Structure):
    _fields_ = [('ControlWord', wt.DWORD), ('StatusWord', wt.DWORD), ('TagWord', wt.DWORD),
                ('ErrorOffset', wt.DWORD), ('ErrorSelector', wt.DWORD), ('DataOffset', wt.DWORD),
                ('DataSelector', wt.DWORD), ('RegisterArea', ctypes.c_byte * 80),
                ('Cr0NpxState', wt.DWORD)]


class WOW64_CONTEXT(ctypes.Structure):
    _fields_ = [('ContextFlags', wt.DWORD), ('Dr0', wt.DWORD), ('Dr1', wt.DWORD), ('Dr2', wt.DWORD),
                ('Dr3', wt.DWORD), ('Dr6', wt.DWORD), ('Dr7', wt.DWORD),
                ('FloatSave', FLOATING_SAVE_AREA), ('SegGs', wt.DWORD), ('SegFs', wt.DWORD),
                ('SegEs', wt.DWORD), ('SegDs', wt.DWORD), ('Edi', wt.DWORD), ('Esi', wt.DWORD),
                ('Ebx', wt.DWORD), ('Edx', wt.DWORD), ('Ecx', wt.DWORD), ('Eax', wt.DWORD),
                ('Ebp', wt.DWORD), ('Eip', wt.DWORD), ('SegCs', wt.DWORD), ('EFlags', wt.DWORD),
                ('Esp', wt.DWORD), ('SegSs', wt.DWORD), ('ExtendedRegisters', ctypes.c_byte * 512)]


def main():
    pid = int(sys.argv[1])
    lo = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x401000
    hi = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0x888000
    hp = k32.OpenProcess(PROCESS_ALL, False, pid)
    snap = k32.CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0)
    te = THREADENTRY32()
    te.dwSize = ctypes.sizeof(te)
    ok = k32.Thread32First(snap, ctypes.byref(te))
    while ok:
        if te.th32OwnerProcessID == pid:
            ht = k32.OpenThread(THREAD_ALL, False, te.th32ThreadID)
            k32.Wow64SuspendThread(ht)
            ctx = WOW64_CONTEXT()
            ctx.ContextFlags = 0x10007
            if k32.Wow64GetThreadContext(ht, ctypes.byref(ctx)):
                print('TID %d EIP %08x ESP %08x EBP %08x' % (te.th32ThreadID, ctx.Eip, ctx.Esp, ctx.Ebp))
                buf = (ctypes.c_uint32 * 2048)()
                n = ctypes.c_size_t()
                k32.ReadProcessMemory(hp, ctypes.c_void_p(ctx.Esp), buf, ctypes.sizeof(buf), ctypes.byref(n))
                cnt = 0
                for i in range(n.value // 4):
                    v = buf[i]
                    if lo <= v < hi:
                        print('   [esp+%04x] %08x' % (i * 4, v))
                        cnt += 1
                        if cnt > 25:
                            break
            else:
                print('TID %d ctx error %d' % (te.th32ThreadID, ctypes.get_last_error()))
            k32.ResumeThread(ht)
            k32.CloseHandle(ht)
        ok = k32.Thread32Next(snap, ctypes.byref(te))


if __name__ == '__main__':
    main()
