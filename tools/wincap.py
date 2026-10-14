"""Capture a window by title (PrintWindow) into a PNG.

Usage: python -I wincap.py "<title substring>" out.png [index]
       python -I wincap.py --pid <pid> <out prefix>   (all windows of a process: texts + PNGs)
"""
import ctypes
import ctypes.wintypes as wt
import struct
import sys
import zlib

user32 = ctypes.WinDLL('user32', use_last_error=True)
gdi32 = ctypes.WinDLL('gdi32', use_last_error=True)
user32.SetProcessDPIAware()


def find_windows(sub):
    res = []

    @ctypes.WINFUNCTYPE(ctypes.c_bool, wt.HWND, wt.LPARAM)
    def cb(h, l):
        n = user32.GetWindowTextLengthW(h)
        if n and user32.IsWindowVisible(h):
            b = ctypes.create_unicode_buffer(n + 1)
            user32.GetWindowTextW(h, b, n + 1)
            if sub.lower() in b.value.lower():
                res.append((h, b.value))
        return True
    user32.EnumWindows(cb, 0)
    return res


def write_png(path, w, h, bgra):
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        row = bgra[y * w * 4:(y + 1) * w * 4]
        for x in range(w):
            b, g, r = row[x * 4], row[x * 4 + 1], row[x * 4 + 2]
            raw += bytes((r, g, b))

    def chunk(t, d):
        c = struct.pack('>I', len(d)) + t + d
        return c + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(bytes(raw), 6)) + chunk(b'IEND', b'')
    open(path, 'wb').write(png)


def capture(h, path):
    r = wt.RECT()
    user32.GetClientRect(h, ctypes.byref(r))
    w, hh = r.right, r.bottom
    if w <= 0 or hh <= 0:
        return 3
    hdc = user32.GetDC(h)
    mdc = gdi32.CreateCompatibleDC(hdc)
    bmp = gdi32.CreateCompatibleBitmap(hdc, w, hh)
    gdi32.SelectObject(mdc, bmp)
    ok = user32.PrintWindow(h, mdc, 3)  # PW_CLIENTONLY | PW_RENDERFULLCONTENT
    if not ok:
        gdi32.BitBlt(mdc, 0, 0, w, hh, hdc, 0, 0, 0x00CC0020)

    class BITMAPINFOHEADER(ctypes.Structure):
        _fields_ = [('biSize', wt.DWORD), ('biWidth', wt.LONG), ('biHeight', wt.LONG),
                    ('biPlanes', wt.WORD), ('biBitCount', wt.WORD), ('biCompression', wt.DWORD),
                    ('biSizeImage', wt.DWORD), ('biXPelsPerMeter', wt.LONG),
                    ('biYPelsPerMeter', wt.LONG), ('biClrUsed', wt.DWORD), ('biClrImportant', wt.DWORD)]
    bi = BITMAPINFOHEADER()
    bi.biSize = ctypes.sizeof(bi)
    bi.biWidth = w
    bi.biHeight = -hh
    bi.biPlanes = 1
    bi.biBitCount = 32
    buf = ctypes.create_string_buffer(w * hh * 4)
    gdi32.GetDIBits(mdc, bmp, 0, hh, buf, ctypes.byref(bi), 0)
    write_png(path, w, hh, buf.raw)
    gdi32.DeleteObject(bmp)
    gdi32.DeleteDC(mdc)
    user32.ReleaseDC(h, hdc)
    print('saved', path, w, hh)
    return 0


def window_text(h):
    """Text of a window via WM_GETTEXT with a timeout (works across processes)."""
    n = ctypes.c_size_t(0)
    if not user32.SendMessageTimeoutW(h, 0x000E, 0, 0, 2, 1000, ctypes.byref(n)):  # WM_GETTEXTLENGTH
        return ''
    b = ctypes.create_unicode_buffer(n.value + 1)
    r = ctypes.c_size_t(0)
    user32.SendMessageTimeoutW(h, 0x000D, n.value + 1, b, 2, 1000, ctypes.byref(r))  # WM_GETTEXT
    return b.value


def class_name(h):
    b = ctypes.create_unicode_buffer(256)
    user32.GetClassNameW(h, b, 256)
    return b.value


def dump_pid(pid, prefix):
    """All top-level windows of a process: title, class and child window texts into
    <prefix>.txt, and a PNG of each visible window."""
    tops = []

    @ctypes.WINFUNCTYPE(ctypes.c_bool, wt.HWND, wt.LPARAM)
    def cb(h, l):
        p = wt.DWORD(0)
        user32.GetWindowThreadProcessId(h, ctypes.byref(p))
        if p.value == pid:
            tops.append(h)
        return True
    user32.EnumWindows(cb, 0)
    out = []
    for i, h in enumerate(tops):
        out.append('=== window %d hwnd=%s class=%s visible=%d title=%r' % (
            i, hex(h), class_name(h), user32.IsWindowVisible(h), window_text(h)))
        kids = []

        @ctypes.WINFUNCTYPE(ctypes.c_bool, wt.HWND, wt.LPARAM)
        def ccb(c, l):
            kids.append(c)
            return True
        user32.EnumChildWindows(h, ccb, 0)
        for c in kids:
            t = window_text(c)
            if t:
                out.append('--- child %s class=%s' % (hex(c), class_name(c)))
                out.append(t)
        if user32.IsWindowVisible(h) and not user32.IsHungAppWindow(h):
            try:
                capture(h, '%s_%d.png' % (prefix, i))
            except Exception as e:
                out.append('capture failed: %r' % e)
    with open(prefix + '.txt', 'w', encoding='utf-8') as f:
        f.write('\n'.join(out) + '\n')
    return 0


def main():
    if sys.argv[1] == '--pid':
        return dump_pid(int(sys.argv[2]), sys.argv[3])
    wins = find_windows(sys.argv[1])
    idx = int(sys.argv[3]) if len(sys.argv) > 3 else 0
    if not wins:
        print('no window')
        return 1
    for i, (h, t) in enumerate(wins):
        print(i, hex(h), t)
    h = wins[idx][0]
    if user32.IsHungAppWindow(h):
        print('window hung')
        return 2
    return capture(h, sys.argv[2])


if __name__ == '__main__':
    sys.exit(main())
