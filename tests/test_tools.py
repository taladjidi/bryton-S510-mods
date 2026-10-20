#!/usr/bin/env python3
"""Offline tests for build.py, deploy.py and fetch_firmware.py (no unit needed; stock R039 must be fetched).

  python3 tests/test_tools.py

Everything is written to a temporary directory: your build/, patches/ and FLASHLOG.md are not touched.
"""
import contextlib, hashlib, io, os, platform, shutil, subprocess, sys, tempfile, unittest, zlib

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
sys.path[:0] = [ROOT, os.path.join(ROOT, 'tools')]
import build, deploy, fetch_firmware  # noqa: E402

# The "recommended" set = P1 P3 P5 P7 (P3 includes the former P4): the image of FLASHLOG #11 (OK on the test unit) without P6,
# whose bytes no other patch touches. If this changes, the default set is no longer what was tested.
RECOMMENDED_RAW_SHA = 'f9eee6e328a2c1bb5757483560d415dfaf4b6217986d45e1853f6151946115e8'
STOCK_IMG = os.path.join(build.VAN, 'img.bin')


def sha(b):
    return hashlib.sha256(b).hexdigest()


class Tools(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.patches = os.path.join(self.tmp, 'patches')
        shutil.copytree(build.PATCHES, self.patches)
        self.saved = build.PATCHES, build.OUT
        build.PATCHES, build.OUT = self.patches, os.path.join(self.tmp, 'build')

    def tearDown(self):
        build.PATCHES, build.OUT = self.saved
        shutil.rmtree(self.tmp)

    def build(self, *argv):
        with contextlib.redirect_stdout(io.StringIO()):
            return build.main(list(argv))

    def refused(self, *argv):
        with self.assertRaises(SystemExit), contextlib.redirect_stdout(io.StringIO()):
            build.main(list(argv))

    def add_patch(self, pid, body):
        open(os.path.join(self.patches, pid + '-test.patch'), 'w').write('title: t\ndesc: d\nsource: s\n' + body)

    def raw_of(self, pkg):
        return zlib.decompressobj(16 + 15).decompress(open(os.path.join(pkg, 'IMG.bin'), 'rb').read())

    def test_vanilla_is_bryton_file(self):
        pkg = self.build('vanilla')
        self.assertEqual(open(os.path.join(pkg, 'IMG.bin'), 'rb').read(), open(STOCK_IMG, 'rb').read())

    def test_recommended_is_the_tested_image(self):
        self.assertEqual(sha(self.raw_of(self.build('recommended'))), RECOMMENDED_RAW_SHA)

    def test_every_patch_builds_and_only_changes_its_bytes(self):
        van = open(os.path.join(build.VAN, 'IMG.raw'), 'rb').read()
        for f in sorted(os.listdir(self.patches)):
            pid = f.split('-')[0]
            raw = self.raw_of(self.build(pid))
            changed = sum(a != b for a, b in zip(raw[:-4], van[:-4]))
            self.assertTrue(0 < changed < 1600, f'{pid}: {changed} bytes changed')   # 1600: the free image tail

    def test_package_passes_the_units_checks(self):
        pkg = self.build('recommended')
        img = open(os.path.join(pkg, 'IMG.bin'), 'rb').read()
        self.assertEqual(len(img) % 512, 0)
        self.assertEqual(sum(img[:-4]) & 0xffffffff, int.from_bytes(img[-4:], 'little'))
        ini = open(os.path.join(pkg, 'update.ini'), 'rb').read()
        self.assertEqual(ini, f'[OS]\r\nName=IMG.bin\r\nVersion=R039\r\nSize={len(img)}\r\n'.encode())

    def test_refusals(self):
        self.refused('-n twice', 'P1', 'P1')                                     # applied twice
        p1 = open(os.path.join(self.patches, 'P1-three-beeps.patch')).read()
        line = next(l for l in p1.splitlines() if l.startswith('0x'))
        self.add_patch('X1', line.replace(line.split()[2], '0000') + '\n')       # same bytes as P1, other content
        self.refused('P1', 'X1')                                                 # overlap
        self.add_patch('X2', '0xd01aaf48  sha256:0000000000000000  0428\n')
        self.refused('X2')                                                       # original bytes don't match
        self.add_patch('X3', '0xc0000000  sha256:0000000000000000  00\n')
        self.refused('X3')                                                       # outside the image
        self.add_patch('X4', '0xd01aaf48  0428\n')
        self.refused('X4')                                                       # malformed line
        self.refused('NOPE')                                                     # unknown patch id

    def test_deploy_to_a_fake_drive(self):
        drive = os.path.join(self.tmp, 'BRYTON')
        os.makedirs(os.path.join(drive, 'System'))
        open(os.path.join(drive, 'System', 'device.txt'), 'w').write('[OS]\nVersion=R039\n')
        saved = deploy.ROOT, platform.system
        deploy.ROOT, platform.system = self.tmp, lambda: 'Linux'               # log in tmp, no diskutil eject
        try:
            with contextlib.redirect_stdout(io.StringIO()):
                deploy.main(['--drive', drive, 'recommended'])
                open(os.path.join(drive, 'IAP.bin'), 'w').write('x')
                with self.assertRaises(SystemExit):                              # never next to a bootloader file
                    deploy.main(['--drive', drive, 'recommended'])
        finally:
            deploy.ROOT, platform.system = saved
        self.assertEqual(sha(self.raw_of(drive)), RECOMMENDED_RAW_SHA)
        self.assertIn('| 0 |', open(os.path.join(self.tmp, 'FLASHLOG.md')).read())

    def test_readback_must_be_stock(self):
        bad = os.path.join(self.tmp, 'readback.bin')
        open(bad, 'wb').write(self.raw_of(self.build('P1')))                      # a patched unit
        with self.assertRaises(SystemExit), contextlib.redirect_stdout(io.StringIO()):
            fetch_firmware.main(['--from-readback', bad, '--dest', self.tmp])
        good = os.path.join(self.tmp, 'good.bin')
        open(good, 'wb').write(open(os.path.join(build.VAN, 'IMG.raw'), 'rb').read() + b'\xff' * 2048)   # padded
        with contextlib.redirect_stdout(io.StringIO()):
            fetch_firmware.main(['--from-readback', good, '--dest', os.path.join(self.tmp, 'fw')])
        self.assertEqual(sha(open(os.path.join(self.tmp, 'fw', 'IMG.raw'), 'rb').read()),
                         open(build.SUMS).read().split()[2])

    def test_code_patches_match_their_source(self):
        if not shutil.which('clang'):
            self.skipTest('clang not installed')
        for f in sorted(os.listdir(build.PATCHES)):
            if not f.endswith(('.s', '.c')):
                continue
            out = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'asm2patch.py'), os.path.join(build.PATCHES, f)],
                                 capture_output=True, text=True, check=True).stdout
            if f.endswith('.c'):
                continue    # ponytail: C code depends on the clang version (CI's differs from the Mac's that built the
                            # bytes tested on the unit): only "builds and fits"; pin clang in CI to compare bytes too
            patch = open(os.path.join(build.PATCHES, os.path.splitext(f)[0] + '.patch')).read()
            for line in out.splitlines():
                self.assertIn(' '.join(line.split()[:3]), ' '.join(patch.split()), f'{f}: {line[:60]}')

    def test_web_builder_agrees(self):
        if not shutil.which('node'):
            self.skipTest('node not installed')
        self.build('recommended')                                                     # test.mjs reads build/recommended
        r = subprocess.run(['node', 'site/test.mjs'], cwd=ROOT, capture_output=True, text=True,
                           env={**os.environ, 'BUILD_DIR': build.OUT})
        self.assertEqual(r.returncode, 0, r.stderr)


if __name__ == '__main__':
    unittest.main()
