#!/usr/bin/env python3
"""Run the code of every patch on an emulated Cortex-M7, inside the patched R039 image, and fail on anything that
would go wrong on the unit. The bytes run are the ones in the .patch files, as build.py installs them.

  python3 tools/patchsim.py P8 P9              those patches (on top of each other, like a build)
  python3 tools/patchsim.py -n 2000 P9         more trials per entry point (default 300)

Entry points come from the patch lines themselves: every bl / b.w written into the firmware that lands in new code,
and every function pointer (odd word) pointing there. Function pointers the code hands out while running (a field
callback, for instance) become entry points too. Each one is called many times with random registers and random
memory: RAM is filled with a mix of small numbers and pointers into a scratch pool, so the code follows plausible
pointers and takes its branches. Firmware functions it calls are not run: they return a random value.

Fails on (each would fault, hang or corrupt something on the unit):
  - a call or jump into firmware code in ARM state (a function pointer without its Thumb bit: the 2026-10-22 bug);
  - a fetch, read or write of unmapped memory, an undefined instruction, a breakpoint or SVC;
  - a write into the image (read-only under the MPU) or ITCM, into a peripheral other than the RTC backup
    registers, or into firmware RAM that is not a declared variable ("@sym", 0x200 bytes from its address);
  - more than LIMIT instructions in one call (a loop that does not end), or more than STACK bytes of stack.
Not covered: what the firmware functions it calls would do; timing; another task running in the middle.
The core is Unicorn's Cortex-A7 in Thumb state: its Cortex-M7 model applies the M-profile default memory map, where
0xd0000000 cannot execute (the bootloader's MPU region allows it on the unit), and Unicorn has no MPU to set. The
Thumb-2 and VFP instructions the patches use are the same; the M-only MRS / MSR of PRIMASK etc. are emulated here,
and every instruction is checked to run in Thumb state (an A-profile core would switch to ARM state silently where
the M7 takes a usage fault).
Needs: pip install unicorn==2.1.4, and firmware/R039 (tools/fetch_firmware.py).
"""
import glob, os, random, re, struct, sys

from unicorn import (Uc, UcError, UC_ARCH_ARM, UC_MODE_THUMB, UC_HOOK_CODE, UC_HOOK_MEM_WRITE, UC_HOOK_MEM_INVALID,
                     UC_HOOK_INTR)
from unicorn.arm_const import (UC_ARM_REG_R0, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_S0,
                               UC_ARM_REG_CPSR, UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC, UC_CPU_ARM_CORTEX_A7)

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
sys.path.insert(0, ROOT)
import build  # noqa: E402

BASE = build.BASE
POOL, POOL_LEN = 0x60000000, 0x200000         # scratch memory the random pointers point into (not on the chip)
STACK_TOP, STACK = 0x6f010000, 512            # stack budget per call, bytes (firmware tasks have little spare)
STACK_MAP = 0x6f000000
END = 0x6ff00000                              # return address of the call: reaching it ends the trial
LIMIT = 3_000_000                             # instructions per call
# (base, size, writable): the STM32H747 memory the firmware uses
RAM = [(0x20000000, 0x20000), (0x24000000, 0x80000), (0x30000000, 0x48000), (0x38000000, 0x10000),
       (0x38800000, 0x1000)]
OPEN_RAM = [(0x38000000, 0x10000), (0x38800000, 0x1000),   # SRAM4 and backup SRAM: unused by the firmware
            (0x58004000, 0x400)]                             # RTC, backup registers at +0x50
REGS = [UC_ARM_REG_R0 + i for i in range(13)]


class Fail(Exception):
    pass


SMALL = [0, 1, 2, 3, 4, 5, 7, 8, 10, 11, 12, 15, 16, 20, 31, 32, 63, 64, 100, 127, 128, 200, 255]


def ptr(rnd):
    return POOL + 4 * rnd.randrange((POOL_LEN // 2) // 4)    # 1 MB after it: room for fields of large objects


def soup(rnd, n):
    """n words as the firmware's memory holds them: 50 % valid pointers (into the pool), 35 % small numbers, 15 %
    zero. No wild values: a word the patch uses as a pointer is a pointer the firmware set."""
    out = []
    for _ in range(n):
        k = rnd.random()
        out.append(ptr(rnd) if k < .5 else rnd.choice(SMALL) if k < .85 else 0)
    return struct.pack(f'<{n}I', *out)


def branch_target(at, ins):
    """Target of a Thumb-2 BL / B.W at `at`, or None."""
    h1, h2 = struct.unpack('<HH', ins)
    if h1 >> 11 != 0b11110 or h2 & 0xd000 not in (0xd000, 0x9000):
        return None
    return at + 4 + tools_asm2patch.branch_addend(ins)


sys.path.insert(0, os.path.join(ROOT, 'tools'))
import asm2patch as tools_asm2patch  # noqa: E402


def sources(fname):
    pid = fname.split('-')[0]
    return [s for s in glob.glob(os.path.join(build.PATCHES, pid + '-*')) if s.endswith(('.c', '.s'))]


def arg_kinds(fname):
    """{function name: [argument kinds]} from the patch's C source: the prototype gives 'p' (pointer), 'i' (integer),
    'f' (float); a line "@args NAME p{0x18:p} i" overrides it where the firmware promises more (here: the word at
    +0x18 of the first argument is a valid pointer). "@args" lists registers from r0 on, so a hook in the middle
    of a firmware function can say which of the caller's registers hold pointers ("x": any value, as for an
    assembly entry without "@args")."""
    out = {}
    for src in sources(fname):
        text = open(src).read()
        for m in re.finditer(r'^[\w ]*?[\s*](\w+)\s*\(([^)]*)\)\s*\{', text, re.M):
            params = [a.strip() for a in m[2].split(',') if a.strip() not in ('', 'void')]
            out[m[1]] = ['f' if re.match(r'(const )?(float|double)\b', a) and '*' not in a
                         else 'p' if '*' in a else 'i' for a in params][:4]      # the rest go on the stack
        for m in re.finditer(r'@args\s+(\w+)\s+(.*)$', text, re.M):
            out[m[1]] = m[2].split()
    return out


def fill(rnd, uc, kind):
    """A value for one argument kind: i, f, p, or p{OFF:KIND,...} (a pointer whose fields are set)."""
    if kind == 'i':
        return rnd.choice(SMALL)
    if not kind.startswith('p'):
        return None
    a = ptr(rnd) & ~0xff
    for f in re.findall(r'(0x[0-9a-fA-F]+|\d+):(\w)', kind):
        uc.mem_write(a + int(f[0], 0), struct.pack('<I', fill(rnd, uc, f[1])))
    return a


def names(fname):
    """{address (odd): function} from the "# sym" lines tools/asm2patch.py writes into the .patch file."""
    out = {}
    for m in re.finditer(r'^# sym (0x[0-9a-f]+) (\w+)', open(os.path.join(build.PATCHES, fname)).read(), re.M):
        out[int(m[1], 16)] = m[2]
    return out


def declared_vars(fname):
    """Firmware RAM the patch's source names (an @sym, or an address in a #define): the patch may write there."""
    out = set()
    for s in sources(fname):
        for m in re.finditer(r'0x[0-9a-fA-F]{8}', open(s).read()):
            a = int(m[0], 16)
            if any(b <= a < b + n for b, n in RAM):
                out.add(a)
    return sorted(out)


class Sim:
    def __init__(self, pids, seed=1):
        raw, _, self.changes = build.apply(pids)
        self.raw = bytes(raw)
        self.stock_end = BASE + len(open(os.path.join(build.VAN, 'IMG.raw'), 'rb').read())
        self.rnd = random.Random(seed)
        self.code = []                                       # (start, end) of new code blocks
        self.entries = {}                                    # address (odd) -> (patch, how it is reached)
        for fname, ch in self.changes.items():
            for a, new in ch:
                if len(new) > 8:
                    self.code.append((a, a + len(new)))
        self.names, self.kinds = {}, {}
        for fname in self.changes:
            self.names.update(names(fname))
            self.kinds[fname] = arg_kinds(fname)
        for fname, ch in self.changes.items():
            for a, new in ch:
                if len(new) != 4:
                    continue
                t = branch_target(a, new)
                w = struct.unpack('<I', new)[0]
                if t is not None and self.in_code(t):
                    self.entries.setdefault(t | 1, (fname, f'{self.names.get(t | 1, "?")}, '
                                                           f'{"bl" if new[3] & 0x40 else "b.w"} at 0x{a:08x}'))
                elif w & 1 and self.in_code(w & ~1):
                    self.entries.setdefault(w, (fname, f'{self.names.get(w, "?")}, pointer at 0x{a:08x}'))
        self.vars = {f: declared_vars(f) for f in self.changes}
        self.soup = [soup(self.rnd, POOL_LEN // 4) for _ in range(4)]
        self.ram_soup = soup(self.rnd, 0x80000 // 4)

    def in_code(self, a):
        return any(s <= a < e for s, e in self.code)

    def setup(self):
        uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
        uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A7)
        uc.reg_write(UC_ARM_REG_C1_C0_2, 0xf << 20)                         # FPU on, as the firmware's startup
        uc.reg_write(UC_ARM_REG_FPEXC, 1 << 30)
        uc.mem_map(0, 0x10000)                                               # ITCM
        uc.mem_map(BASE, 0x800000)
        uc.mem_write(BASE, self.raw)
        for b, n in RAM:
            uc.mem_map(b, n)
        uc.mem_map(0x40000000, 0x20000000)                                   # peripherals
        uc.mem_map(0xe0000000, 0x100000)                                     # system control space
        uc.mem_map(POOL, POOL_LEN)
        uc.mem_map(STACK_MAP, STACK_TOP - STACK_MAP + 0x1000)
        uc.mem_map(END, 0x1000)
        uc.hook_add(UC_HOOK_CODE, self.on_firmware, begin=0, end=self.stock_end - 1)
        uc.hook_add(UC_HOOK_CODE, self.on_firmware, begin=POOL, end=POOL + POOL_LEN - 1)
        uc.hook_add(UC_HOOK_CODE, self.on_end, begin=END, end=END + 0xfff)
        uc.hook_add(UC_HOOK_CODE, self.on_step, begin=BASE, end=BASE + 0x800000 - 1)
        uc.hook_add(UC_HOOK_MEM_WRITE, self.on_write)
        uc.hook_add(UC_HOOK_MEM_INVALID, self.on_invalid)
        uc.hook_add(UC_HOOK_INTR, self.on_intr)
        self.uc = uc

    def trial(self, entry, fname):
        uc, rnd = self.uc, self.rnd
        for b, n in RAM:
            uc.mem_write(b, self.ram_soup[:n])
        uc.mem_write(POOL, rnd.choice(self.soup))
        uc.mem_write(STACK_MAP, bytes(STACK_TOP - STACK_MAP) + soup(rnd, 64))    # + the caller's frame
        words = list(struct.unpack('<13I', soup(rnd, 13)))
        kinds = [k for k in self.kinds.get(self.owner(entry), {}).get(self.names.get(entry), []) if k != 'f']
        for i, k in enumerate(kinds[:13]):
            v = fill(rnd, uc, k)
            words[i] = words[i] if v is None else v
        for r, v in zip(REGS, words):
            uc.reg_write(r, v)
        for i in range(16):
            uc.reg_write(UC_ARM_REG_S0 + i, struct.unpack('<I', struct.pack('<f', rnd.uniform(-1e3, 1e3)))[0])
        uc.reg_write(UC_ARM_REG_SP, STACK_TOP)
        uc.reg_write(UC_ARM_REG_LR, END | 1)
        self.fname, self.steps, self.low_sp, self.err, self.calls, self.prev = fname, 0, STACK_TOP, None, [], entry
        self.allowed = [(a, a + 0x200) for a in self.vars.get(fname, [])]
        try:
            uc.emu_start(entry, END, count=LIMIT + 10)
        except UcError as e:
            if not self.err:
                pc = uc.reg_read(UC_ARM_REG_PC)
                self.err = f'{e} at pc 0x{pc:08x}'
        if self.err:
            raise Fail(self.err)
        if self.steps >= LIMIT:
            raise Fail(f'did not return after {LIMIT} instructions (endless loop?)')
        return STACK_TOP - self.low_sp

    def stop(self, why):
        if not self.err:
            self.err = why
        self.uc.emu_stop()

    def on_step(self, uc, address, size, _):
        self.steps += 1
        self.prev = address
        if not uc.reg_read(UC_ARM_REG_CPSR) & 0x20:
            return self.stop(f'running in ARM state at 0x{address:08x}: reached through a pointer without the '
                             f'Thumb bit (usage fault on the unit)')
        h1, h2 = struct.unpack('<HH', bytes(uc.mem_read(address, 4)))
        if h1 == 0xf3ef and h2 & 0xf000 == 0x8000:                         # MRS Rd, <special>: PRIMASK etc. = 0
            uc.reg_write(UC_ARM_REG_R0 + (h2 >> 8 & 15), 0)
            return uc.reg_write(UC_ARM_REG_PC, (address + 4) | 1)
        if h1 & 0xfff0 == 0xf380 and h2 & 0xff00 == 0x8800:                 # MSR <special>, Rn
            return uc.reg_write(UC_ARM_REG_PC, (address + 4) | 1)
        sp = uc.reg_read(UC_ARM_REG_SP)
        self.low_sp = min(self.low_sp, sp)
        if STACK_TOP - sp > STACK:
            self.stop(f'stack over {STACK} bytes at pc 0x{address:08x}')
        elif self.steps >= LIMIT:
            uc.emu_stop()
        elif address >= self.stock_end and not self.in_code(address):
            self.stop(f'executing outside the new code at 0x{address:08x}')

    def on_firmware(self, uc, address, size, _):
        """Firmware code (or a function pointer from random memory) reached: a call returns, a jump ends."""
        if not uc.reg_read(UC_ARM_REG_CPSR) & 0x20 and not POOL <= address < POOL + POOL_LEN:
            return self.stop(f'jump to 0x{address:08x} in ARM state: a function pointer without the Thumb bit '
                             f'(usage fault on the unit)')
        lr = uc.reg_read(UC_ARM_REG_LR)
        self.calls.append(address)
        h1, h2 = struct.unpack('<HH', bytes(uc.mem_read(self.prev, 4)))
        called = (h1 >> 11 == 0b11110 and h2 & 0xd000 == 0xd000) or h1 & 0xff87 == 0x4780   # BL, BLX Rm
        if called and self.in_code(lr & ~1):                                 # a call from the patch: return
            uc.reg_write(UC_ARM_REG_R0, self.rnd.choice([0, 1, 2, POOL + 0x100, self.rnd.getrandbits(8)]))
            uc.reg_write(UC_ARM_REG_PC, lr)
            if not uc.reg_read(UC_ARM_REG_CPSR) & 0x20:                      # a method pointer from memory
                uc.reg_write(UC_ARM_REG_CPSR, uc.reg_read(UC_ARM_REG_CPSR) | 0x20)
        else:                                                                # tail jump back into the firmware
            uc.emu_stop()

    def on_end(self, uc, address, size, _):
        uc.emu_stop()

    def on_write(self, uc, access, address, size, value, _):
        a = address
        if STACK_MAP <= a < STACK_TOP + 0x100 or POOL <= a < POOL + POOL_LEN:      # incl. the caller's frame
            return
        if any(b <= a < b + n for b, n in OPEN_RAM) or any(b <= a < e for b, e in self.allowed):
            return
        what = ('the image (read-only)' if BASE <= a < BASE + 0x800000 else 'ITCM' if a < 0x10000
                else 'a peripheral' if 0x40000000 <= a < 0x60000000 or a >= 0xe0000000 else 'firmware RAM')
        pc = uc.reg_read(UC_ARM_REG_PC)
        self.stop(f'write of {size} B to 0x{a:08x} ({what}) at pc 0x{pc:08x}')

    def on_invalid(self, uc, access, address, size, value, _):
        pc = uc.reg_read(UC_ARM_REG_PC)
        self.err = self.err or f'access to unmapped 0x{address:08x} at pc 0x{pc:08x}'
        return False

    def on_intr(self, uc, intno, _):
        pc = uc.reg_read(UC_ARM_REG_PC)
        names = {6: 'usage fault (undefined instruction, or ARM state: missing Thumb bit)', 3: 'hard fault',
                 4: 'memory fault', 5: 'bus fault', 11: 'SVC', 7: 'breakpoint'}
        self.stop(f'exception {intno} {names.get(intno, "")} at pc 0x{pc:08x}')

    def run(self, n):
        self.setup()
        todo, done, worst, bad = dict(self.entries), set(), {}, []
        while todo:
            entry, (fname, how) = todo.popitem()
            done.add(entry)
            for t in range(n):
                self.handed_out = []
                try:
                    used = self.trial(entry, fname)
                    worst[entry] = max(worst.get(entry, 0), used)
                except Fail as e:
                    bad.append(f'{fname}: entry 0x{entry & ~1:08x} ({how}), trial {t}: {e}')
                    break
            for a in self.pointers_written():
                if a not in done and a not in todo:
                    todo[a] = (self.owner(a), f'{self.names.get(a, "?")}, a function pointer it hands out')
                    self.entries[a] = todo[a]
        return worst, bad

    def owner(self, a):
        for fname, ch in self.changes.items():
            if any(s <= (a & ~1) < s + len(new) for s, new in ch if len(new) > 8):
                return fname
        return '?'

    def pointers_written(self):
        """Odd words pointing into new code found in the pool or open RAM after the last trial: handed-out
        function pointers (callbacks)."""
        out = set()
        for b, n in [(POOL, POOL_LEN)] + OPEN_RAM[:2] + RAM[:3]:
            data = self.uc.mem_read(b, n)
            for (w,) in struct.iter_unpack('<I', bytes(data)):
                if w & 1 and self.in_code(w & ~1):
                    out.add(w)
        return out


def check(pids, n=300):
    """Failures (list of lines) for these patches; [] when every entry point passed. Pure-data patches: []."""
    sim = Sim(pids)
    return sim.run(n)[1] if sim.entries else []


def main(argv):
    n = 300
    if argv[:1] == ['-n']:
        n, argv = int(argv[1]), argv[2:]
    if not argv:
        sys.exit(__doc__)
    sim = Sim(argv)
    worst, bad = sim.run(n)
    for e in sorted(worst):
        fname, how = sim.entries[e]
        print(f'ok    0x{e & ~1:08x}  {fname.split("-")[0]:4s} {how:60s} {n} calls, stack <= {worst[e]} B')
    for b in bad:
        print('FAIL ', b)
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main(sys.argv[1:])
