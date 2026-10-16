"""List the DXGI adapters (name, vendor, video memory) in enumeration order, as dgVoodoo numbers them.

Usage: python -I dxgi_adapters.py
"""
import ctypes
from ctypes import wintypes


class GUID(ctypes.Structure):
    _fields_ = [('Data1', ctypes.c_ulong), ('Data2', ctypes.c_ushort), ('Data3', ctypes.c_ushort),
                ('Data4', ctypes.c_ubyte * 8)]


class DXGI_ADAPTER_DESC1(ctypes.Structure):
    _fields_ = [('Description', ctypes.c_wchar * 128), ('VendorId', ctypes.c_uint), ('DeviceId', ctypes.c_uint),
                ('SubSysId', ctypes.c_uint), ('Revision', ctypes.c_uint), ('DedicatedVideoMemory', ctypes.c_size_t),
                ('DedicatedSystemMemory', ctypes.c_size_t), ('SharedSystemMemory', ctypes.c_size_t),
                ('AdapterLuid', ctypes.c_ulonglong), ('Flags', ctypes.c_uint)]


def method(obj, index, restype, *argtypes):
    vtbl = ctypes.cast(obj, ctypes.POINTER(ctypes.POINTER(ctypes.c_void_p))).contents
    return ctypes.WINFUNCTYPE(restype, ctypes.c_void_p, *argtypes)(vtbl[index])


def main():
    iid = GUID(0x770aae78, 0xf26f, 0x4dba, (ctypes.c_ubyte * 8)(0xa8, 0x29, 0x25, 0x3c, 0x83, 0xd1, 0xb3, 0x87))
    factory = ctypes.c_void_p()
    hr = ctypes.windll.dxgi.CreateDXGIFactory1(ctypes.byref(iid), ctypes.byref(factory))
    if hr != 0:
        print('CreateDXGIFactory1 failed 0x%08x' % (hr & 0xffffffff))
        return
    i = 0
    while True:
        adapter = ctypes.c_void_p()
        hr = method(factory, 12, ctypes.c_long, ctypes.c_uint, ctypes.POINTER(ctypes.c_void_p))(
            factory, i, ctypes.byref(adapter))  # IDXGIFactory1::EnumAdapters1
        if hr != 0:
            break
        d = DXGI_ADAPTER_DESC1()
        method(adapter, 10, ctypes.c_long, ctypes.POINTER(DXGI_ADAPTER_DESC1))(adapter, ctypes.byref(d))  # GetDesc1
        print('adapter %d: %s | vendor %04x device %04x | video memory %d MB | flags %d' % (
            i + 1, d.Description, d.VendorId, d.DeviceId, d.DedicatedVideoMemory // 2 ** 20, d.Flags))
        method(adapter, 2, ctypes.c_ulong)(adapter)  # Release
        i += 1
    method(factory, 2, ctypes.c_ulong)(factory)


if __name__ == '__main__':
    main()
