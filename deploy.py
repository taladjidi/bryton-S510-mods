#!/usr/bin/env python3
"""Build a package and stage it on the bike computer for its next startup.

  ./deploy.py T1             build.py T1, copy IMG.bin + update.ini to the BRYTON drive, verify, eject
  ./deploy.py vanilla        same with the untouched firmware (normal way back to stock)
  ./deploy.py -n NAME P1 P3  several patches

Plug the unit in normally first (BRYTON drive mounted). After the eject: unplug; the unit installs the update
during startup (battery >= 38 %) and restarts. Each deploy is appended to your local FLASHLOG.md (not in git) with result "pending" — fill in what you saw.
"""
import datetime, hashlib, os, shutil, subprocess, sys

ROOT = os.path.dirname(os.path.abspath(__file__))
VOL = '/Volumes/BRYTON'


def die(msg):
    sys.exit('deploy.py: ' + msg)


def sha(path):
    return hashlib.sha256(open(path, 'rb').read()).hexdigest()


def main(argv):
    if not argv:
        die(__doc__)
    subprocess.run([sys.executable, os.path.join(ROOT, 'build.py'), *argv], check=True)
    name = 'vanilla' if argv == ['vanilla'] else argv[1] if argv[0] == '-n' else '+'.join(argv)
    pkg = os.path.join(ROOT, 'build', name)

    dev = os.path.join(VOL, 'System', 'device.txt')            # the S510 writes this on its drive
    if not os.path.isfile(dev):
        die(f'{VOL} is not mounted or is not the bike computer (no System/device.txt) — plug it in normally')
    leftovers = [f for f in ('IMG.bin', 'update.ini', 'IMGUnzip.bin', 'IAP.bin') if os.path.exists(os.path.join(VOL, f))]
    if leftovers:
        print(f'note: replacing files already on the drive: {leftovers}')
        if os.path.exists(os.path.join(VOL, 'IAP.bin')):
            die('IAP.bin is on the drive — never flash a bootloader; remove it by hand and re-run')

    for f in ('IMG.bin', 'update.ini'):
        dst = os.path.join(VOL, f)
        shutil.copyfile(os.path.join(pkg, f), dst)
        os.sync()
        if sha(dst) != sha(os.path.join(pkg, f)):
            die(f'{f} on the drive does not match the build — not ejecting, check the drive')
    print(f'copied + verified IMG.bin, update.ini -> {VOL}')

    subprocess.run(['diskutil', 'eject', VOL], check=True)

    commit = subprocess.run(['git', '-C', ROOT, 'rev-parse', '--short', 'HEAD'], capture_output=True, text=True).stdout.strip()
    log = os.path.join(ROOT, 'FLASHLOG.md')
    if not os.path.exists(log):
        open(log, 'w').write('# Flash log\n\n| # | date | what | how | result |\n|---|------|------|-----|--------|\n')
    n = sum(1 for line in open(log) if line.startswith('| ') and line[2].isdigit())
    with open(log, 'a') as f:
        f.write(f'| {n} | {datetime.date.today()} | {name} (IMG.bin sha256 {sha(os.path.join(pkg, "IMG.bin"))[:12]}…, '
                f'repo {commit}) | deploy.py: copied to drive, normal update at startup | pending |\n')
    print('ejected. Unplug now: the unit installs the update while starting up, then restarts.\n'
          f'Logged as #{n} in FLASHLOG.md (result: pending).')


if __name__ == '__main__':
    main(sys.argv[1:])
