"""Build the release archive from a fresh build.

Usage: python -I tools/make_release.py <version>
Needs out\\dinput.dll (run build.cmd first). Writes dist\\bcmpfix-<version>.zip and
dist\\SHA256SUMS.txt. The archive unpacks directly into the game folder.
"""
import hashlib
import os
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
    dist = os.path.join(ROOT, 'dist')
    os.makedirs(dist, exist_ok=True)
    zpath = os.path.join(dist, 'bcmpfix-%s.zip' % ver)
    with zipfile.ZipFile(zpath, 'w', zipfile.ZIP_DEFLATED) as z:
        for src, dst in FILES:
            p = os.path.join(ROOT, src)
            if not os.path.exists(p):
                sys.exit('missing %s' % src)
            z.write(p, dst)
    dll = os.path.join(ROOT, 'out', 'dinput.dll')
    sums = ['%s  %s' % (sha256(zpath), os.path.basename(zpath)), '%s  dinput.dll' % sha256(dll)]
    open(os.path.join(dist, 'SHA256SUMS.txt'), 'w', newline='\n').write('\n'.join(sums) + '\n')
    print('\n'.join(sums))


if __name__ == '__main__':
    main()
