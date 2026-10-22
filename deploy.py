#!/usr/bin/env python3
"""Build a package and stage it on the bike computer for its next startup.

  ./deploy.py recommended    the recommended set of patches (see build.py PRESETS)
  ./deploy.py vanilla        the untouched firmware (normal way back to stock)
  ./deploy.py -n NAME P1 P3  your own selection
  ./deploy.py --drive PATH … if the BRYTON drive is not found automatically

Plug the unit in normally first (BRYTON drive mounted). After the copy: eject, unplug; the unit installs the
update during startup (battery >= 38 %) and restarts. Each deploy is appended to your local FLASHLOG.md (not in
git) with result "pending"; fill in what you saw.
"""
import datetime, getpass, glob, hashlib, os, platform, string, subprocess, sys

import build

ROOT = os.path.dirname(os.path.abspath(__file__))


def die(msg):
    sys.exit('deploy.py: ' + msg)


def sha(path):
    return hashlib.sha256(open(path, 'rb').read()).hexdigest()


def find_drive():
    """Mounted drives that have the S510's System/device.txt."""
    user = getpass.getuser()
    cands = ['/Volumes/BRYTON', f'/media/{user}/BRYTON', f'/run/media/{user}/BRYTON', '/media/BRYTON']
    cands += glob.glob('/Volumes/*') + glob.glob(f'/media/{user}/*') + glob.glob(f'/run/media/{user}/*')
    if platform.system() == 'Windows':
        cands += [f'{d}:\\' for d in string.ascii_uppercase]
    hits = []
    for c in dict.fromkeys(cands):
        dev = os.path.join(c, 'System', 'device.txt')
        try:
            if os.path.isfile(dev) and '[OS]' in open(dev, errors='replace').read():
                hits.append(c)
        except OSError:
            pass
    return hits


def main(argv):
    if not argv:
        die(__doc__)
    vol = None
    if argv[0] == '--drive':
        vol, argv = argv[1], argv[2:]
    name, _ = build.resolve(argv)
    if vol is None:
        hits = find_drive()
        if len(hits) != 1:
            die(f'found {len(hits)} bike computer drives {hits}; plug it in normally, or pass --drive PATH')
        vol = hits[0]
    if not os.path.isfile(os.path.join(vol, 'System', 'device.txt')):
        die(f'{vol} is not the bike computer (no System/device.txt)')
    pkg = build.main(argv)
    pids = build.resolve(argv)[1]
    if pids != ['vanilla']:
        try:
            sys.path.insert(0, os.path.join(ROOT, 'tools'))
            import patchsim
        except ImportError:
            die('the patch code must pass tools/patchsim.py before it goes on the unit: pip install unicorn==2.1.4')
        bad = patchsim.check(pids)
        if bad:
            die('tools/patchsim.py found problems, not copying:\n  ' + '\n  '.join(bad))
        print('patchsim: every entry point of the new code ran clean (tools/patchsim.py)')

    if os.path.exists(os.path.join(vol, 'IAP.bin')):
        die('IAP.bin is on the drive. Never flash a bootloader: remove it by hand and re-run')
    for f in ('IMG.bin', 'update.ini'):
        dst = os.path.join(vol, f)
        with open(os.path.join(pkg, f), 'rb') as src, open(dst, 'wb') as out:
            out.write(src.read())
            out.flush()
            os.fsync(out.fileno())
        if sha(dst) != sha(os.path.join(pkg, f)):
            die(f'{f} on the drive does not match the build; not ejecting, check the drive')
    print(f'copied + verified IMG.bin, update.ini -> {vol}')

    if platform.system() == 'Darwin':
        subprocess.run(['diskutil', 'eject', vol], check=True)
        print('ejected. Unplug now: the unit installs the update while starting up, then restarts.')
    else:
        print('Now eject the BRYTON drive (file manager / "Safely remove"), then unplug: the unit installs the '
              'update while starting up, then restarts.')

    commit = subprocess.run(['git', '-C', ROOT, 'rev-parse', '--short', 'HEAD'], capture_output=True, text=True).stdout.strip()
    log = os.path.join(ROOT, 'FLASHLOG.md')
    if not os.path.exists(log):
        open(log, 'w').write('# Flash log\n\n| # | date | what | how | result |\n|---|------|------|-----|--------|\n')
    n = sum(1 for line in open(log) if line.startswith('| ') and line[2].isdigit())
    with open(log, 'a') as f:
        f.write(f'| {n} | {datetime.date.today()} | {name} (IMG.bin sha256 {sha(os.path.join(pkg, "IMG.bin"))[:12]}…, '
                f'repo {commit or "?"}) | deploy.py: copied to drive, normal update at startup | pending |\n')
    print(f'Logged as #{n} in FLASHLOG.md (result: pending).')


if __name__ == '__main__':
    main(sys.argv[1:])
