"""Build the release files from a fresh build.

Usage: python -I tools/make_release.py <version>
Needs out\\dinput.dll and out\\BC_MP_fix.exe (run build.cmd, then installer\\build.cmd).
Writes dist\\BC_MP_fix.exe (installer), dist\\bcmpfix-<version>.zip (unpacks directly into the
game folder) and dist\\SHA256SUMS.txt.
"""
import hashlib
import os
import re
import shutil
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FILES = [  # (source in the repository, path inside the archive)
    ('out/dinput.dll', 'dinput.dll'),
    ('release/bcmpfix.ini', 'bcmpfix.ini'),
    ('release/README.md', 'bcmpfix_README.md'),
    ('scripts/bcmpfix_mp.py', 'scripts/bcmpfix_mp.py'),
    ('LICENSE', 'bcmpfix_LICENSE.txt'),
]


def sha256(path):
    return hashlib.sha256(open(path, 'rb').read()).hexdigest()


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    ver = sys.argv[1]
    hv = re.search(r'#define BCMPFIX_VERSION "([^"]+)"', open(os.path.join(ROOT, 'installer', 'version.h')).read())
    if not hv or hv.group(1) != ver:
        sys.exit('installer/version.h says %s, not %s' % (hv.group(1) if hv else '?', ver))
    exe = os.path.join(ROOT, 'out', 'BC_MP_fix.exe')
    if not os.path.exists(exe) or os.path.getmtime(exe) < os.path.getmtime(os.path.join(ROOT, 'out', 'dinput.dll')):
        sys.exit('out\\BC_MP_fix.exe missing or older than out\\dinput.dll - run installer\\build.cmd')
    dist = os.path.join(ROOT, 'dist')
    os.makedirs(dist, exist_ok=True)
    zpath = os.path.join(dist, 'bcmpfix-%s.zip' % ver)
    with zipfile.ZipFile(zpath, 'w', zipfile.ZIP_DEFLATED) as z:
        for src, dst in FILES:
            p = os.path.join(ROOT, src)
            if not os.path.exists(p):
                sys.exit('missing %s' % src)
            z.write(p, dst)
    shutil.copyfile(exe, os.path.join(dist, 'BC_MP_fix.exe'))
    dll = os.path.join(ROOT, 'out', 'dinput.dll')
    sums = ['%s  BC_MP_fix.exe' % sha256(exe), '%s  %s' % (sha256(zpath), os.path.basename(zpath)),
            '%s  dinput.dll' % sha256(dll)]
    open(os.path.join(dist, 'SHA256SUMS.txt'), 'w', newline='\n').write('\n'.join(sums) + '\n')
    print('\n'.join(sums))


if __name__ == '__main__':
    main()
