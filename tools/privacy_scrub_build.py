"""Scrubs the Windows build number out of CMake's own files in the project's build folders (privacy, 2026-11-16).

CMake writes the PC's exact Windows version ("10.0.<build>") into every build folder it configures:
  CMakeFiles/<cmake version>/CMakeSystem.cmake   set(CMAKE_HOST_SYSTEM "Windows-10.0.<build>") and three more lines
  CMakeFiles/CMakeConfigureLog.yaml              "The system is: Windows - 10.0.<build> - AMD64"
  CMakeFiles/CMakeOutput.log                     the same line (older CMake)
This keeps "10.0" (what every Windows 10 / 11 PC says) and drops the build. Nothing else in the files changes, and each
file keeps its date: ninja and CMake decide what to redo from dates, so a scrubbed build folder neither re-runs CMake nor
rebuilds anything. (CMake reads CMakeSystem.cmake back on a later re-run instead of asking Windows again, so the value
stays scrubbed until the folder is configured from scratch: run this again then.)

Run it once at the end, not per build: at each landmark install (deploy.ps1 calls it) and before a shareable or
portable build is made.
    python tools\\privacy_scrub_build.py            scrub every build folder under Development (not scratch/backups/...)
    python tools\\privacy_scrub_build.py <folder>... only these folders (searched below them)
    python tools\\privacy_scrub_build.py --check    change nothing; list what still holds a build number (exit 1 if any)
It holds no value of this PC: it matches the version's form, not the number.
"""
import os
import re
import sys

DEV = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))     # Development
# never searched: working copies, backups, tests' game copies, and big trees that hold no build folder of ours
SKIP = {'scratch', 'backups', 'preservation', 'qa', 'logs', '.git', '__pycache__', 'node_modules', 'RecompiledFuncs',
        'python', 'roms', 'third_party', 'src', 'include', 'assets'}

# the lines CMake writes the version on; group 1 keeps "<major>.<minor>", the build after it goes
LINE = [
    re.compile(rb'(?m)^(\s*set\(CMAKE_(?:HOST_)?SYSTEM(?:_VERSION)?\s+"(?:[A-Za-z]+-)?\d+\.\d+)\.\d+(?:\.\d+)?(?="\))'),
    re.compile(rb'(?m)^(.*The system is: Windows - \d+\.\d+)\.\d+(?:\.\d+)?(?= - )'),
]


DEPTH = 4     # build folders sit near the top (exe\build, six-engine\NoMercySix\build, libs\N64Recomp\build-win ...)


def subdirs(d):
    """d's folders, not junctions or links (scratch folders link to the live ones)."""
    try:
        entries = list(os.scandir(d))
    except OSError:
        return
    for e in entries:
        try:
            if e.is_dir(follow_symlinks=False) and not (
                    getattr(e.stat(follow_symlinks=False), 'st_file_attributes', 0) & 0x400):
                yield e
        except OSError:
            continue


def build_dirs(root):
    """CMake build folders (a CMakeCache.txt in them) at most DEPTH folders below root; not searched below one."""
    stack = [(root, 0)]
    while stack:
        d, depth = stack.pop()
        if os.path.isfile(os.path.join(d, 'CMakeCache.txt')):
            yield d
            continue
        if depth >= DEPTH:
            continue
        for e in subdirs(d):
            if e.name in SKIP or (d == DEV and e.name == 'toolchain') or e.name == 'CMakeFiles':
                continue
            stack.append((e.path, depth + 1))


def build_files(roots):
    """The files that hold the version: <build>/CMakeFiles/... and the same in its _deps/*-subbuild folders."""
    for root in roots:
        for b in build_dirs(root):
            cmf = [os.path.join(b, 'CMakeFiles')]
            cmf += [os.path.join(e.path, 'CMakeFiles') for e in subdirs(os.path.join(b, '_deps'))]
            for c in cmf:
                if not os.path.isdir(c):
                    continue
                for n in ('CMakeConfigureLog.yaml', 'CMakeOutput.log'):
                    p = os.path.join(c, n)
                    if os.path.isfile(p):
                        yield p
                for v in subdirs(c):
                    p = os.path.join(v.path, 'CMakeSystem.cmake')
                    if os.path.isfile(p):
                        yield p


def scrub(data):
    n = 0
    for rx in LINE:
        data, k = rx.subn(rb'\1', data)
        n += k
    return data, n


def main():
    args = sys.argv[1:]
    check = '--check' in args
    roots = [a for a in args if a != '--check'] or [DEV]
    found = changed = 0
    for p in build_files(roots):
        data = open(p, 'rb').read()
        new, n = scrub(data)
        if not n or new == data:
            continue
        found += 1
        try:
            rel = os.path.relpath(p, DEV)
        except ValueError:          # (another drive)
            rel = p
        if check:
            print('holds a build number:', rel)
            continue
        st = os.stat(p)
        with open(p, 'wb') as f:
            f.write(new)
        os.utime(p, ns=(st.st_atime_ns, st.st_mtime_ns))     # the same date: no re-run of CMake, no rebuild
        changed += 1
        print('scrubbed (%d line%s): %s' % (n, '' if n == 1 else 's', rel))
    if check:
        print('%d file(s) still hold a Windows build number' % found)
        sys.exit(1 if found else 0)
    print('%d file(s) scrubbed' % changed)


if __name__ == '__main__':
    main()
