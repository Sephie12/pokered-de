#!/usr/bin/env python3
"""
recomp.py - Static Recompiler fuer pokered (SM83 -> C).

Liest die gebaute ROM und die Symboldatei (pokered.sym) und erzeugt pro
ROM-Bank eine C-Datei. Code wird ab allen Labels, Vektoren, Ruecksprung-
adressen und optional zusaetzlichen Seeds (Laufzeit-Misses) rekursiv
entdeckt. Zusammenhaengende Befehle (Fallthrough + direkte Spruenge
innerhalb derselben Bank) bilden eine Komponente = eine C-Funktion.

Jede Funktion laedt die CPU-Register in lokale Variablen, springt per
switch zum Einstiegspunkt (g_gb.cpu.pc) und verlaesst die Funktion bei
Calls, Returns, Spruengen nach ausserhalb und - je nach Modus - wenn die
naechste Hardware-Deadline erreicht ist.

Modi:
  --precise  Deadline-Pruefung nach jedem Befehl, jeder Befehl ist ein
             Einstiegspunkt. Zyklengenau identisch zum Interpreter;
             fuer den Differenztest.
  (Standard) Pruefung nur an Rueckwaertsspruengen. Schneller, Interrupts
             koennen einige Befehle spaeter kommen (wie bei Emulatoren
             mit Block-JIT ueblich).

Aufruf:
  recomp.py pokered.gbc pokered.sym pokered.map outdir [--precise]
            [--seeds extra_seeds.txt]
"""
import argparse
import os
import re
import sys

R8 = ['b', 'c', 'd', 'e', 'h', 'l', '(hl)', 'a']
ALU = ['alu_add', 'alu_adc', 'alu_sub', 'alu_sbc', 'alu_and', 'alu_xor', 'alu_or', 'alu_cp']
COND = ['!(f & FLAG_Z)', '(f & FLAG_Z)', '!(f & FLAG_C)', '(f & FLAG_C)']
INVALID = {0xD3, 0xDB, 0xDD, 0xE3, 0xE4, 0xEB, 0xEC, 0xED, 0xF4, 0xFC, 0xFD}

# Sektionen, die reine Daten enthalten (keine Seeds von dort)
DATA_SECTIONS = re.compile(
    r'^(Pics|Tilesets|NPC Sprites|Font Graphics|Trainer Pics|Pok.dex Text|Move Names|'
    r'Pok.mon Names|Music \d|Sound Effects|Sound Effect Headers|Music Headers|Text \d|'
    r'Main Data|Tilemap|Saved Boxes|Header)')

# Labelnamen, die praktisch immer Daten bezeichnen (nur ROMX)
DATA_NAMES = re.compile(
    r'(_Blocks|_Object|_h|Names|FrameBlock\w*|Pointers|Coords|Movements?\d*|Tiles\d*|GFX\w*|'
    r'Pic(Front|Back)?|Sprites?\d*|Graphics|Text\d*|Entries|Table\d*|Moves\d*|Palettes?|'
    r'Packet|SubanimationPointers|BaseStats|Cries|Prices|Layouts?|Attributes|Coll|Bytes|Mons\d*|'
    r'Dex(Entry|Entries)?|Header|Events)$')

LEN = [1] * 256
for op in (0x06, 0x0E, 0x16, 0x1E, 0x26, 0x2E, 0x36, 0x3E, 0x18, 0x20, 0x28, 0x30, 0x38,
           0xC6, 0xCE, 0xD6, 0xDE, 0xE6, 0xEE, 0xF6, 0xFE, 0xE0, 0xF0, 0xE8, 0xF8, 0xCB, 0x10):
    LEN[op] = 2
for op in (0x01, 0x11, 0x21, 0x31, 0x08, 0xC2, 0xC3, 0xCA, 0xD2, 0xDA, 0xC4, 0xCC, 0xCD,
           0xD4, 0xDC, 0xEA, 0xFA):
    LEN[op] = 3


class Instr:
    __slots__ = ('addr', 'op', 'length', 'b1', 'b2', 'kind', 'target', 'nxt')


def rom_byte(rom, bank, addr):
    if addr < 0x4000:
        return rom[addr]
    return rom[bank * 0x4000 + (addr - 0x4000)]


def same_space(bank, addr, target):
    """Liegt target im selben Adressraum wie addr (ROM0->ROM0, ROMX(n)->ROMX(n))?"""
    if addr < 0x4000:
        return target < 0x4000
    return 0x4000 <= target < 0x8000


def decode(rom, bank, addr):
    limit = 0x4000 if addr < 0x4000 else 0x8000
    ins = Instr()
    ins.addr = addr
    op = rom_byte(rom, bank, addr)
    ins.op = op
    ins.length = LEN[op]
    if addr + ins.length > limit or op in INVALID:
        return None
    ins.b1 = rom_byte(rom, bank, addr + 1) if ins.length > 1 else 0
    ins.b2 = rom_byte(rom, bank, addr + 2) if ins.length > 2 else 0
    ins.nxt = addr + ins.length
    ins.target = None
    k = 'normal'
    if op == 0x18:
        k = 'jr'
        ins.target = (ins.nxt + (ins.b1 - 256 if ins.b1 > 127 else ins.b1)) & 0xFFFF
    elif op in (0x20, 0x28, 0x30, 0x38):
        k = 'jrc'
        ins.target = (ins.nxt + (ins.b1 - 256 if ins.b1 > 127 else ins.b1)) & 0xFFFF
    elif op == 0xC3:
        k = 'jp'
        ins.target = ins.b1 | (ins.b2 << 8)
    elif op in (0xC2, 0xCA, 0xD2, 0xDA):
        k = 'jpc'
        ins.target = ins.b1 | (ins.b2 << 8)
    elif op == 0xCD:
        k = 'call'
        ins.target = ins.b1 | (ins.b2 << 8)
    elif op in (0xC4, 0xCC, 0xD4, 0xDC):
        k = 'callc'
        ins.target = ins.b1 | (ins.b2 << 8)
    elif op == 0xC9:
        k = 'ret'
    elif op in (0xC0, 0xC8, 0xD0, 0xD8):
        k = 'retc'
    elif op == 0xD9:
        k = 'reti'
    elif op == 0xE9:
        k = 'jphl'
    elif op in (0xC7, 0xCF, 0xD7, 0xDF, 0xE7, 0xEF, 0xF7, 0xFF):
        k = 'rst'
        ins.target = op & 0x38
    elif op == 0x76:
        k = 'halt'
    elif op == 0xFB:
        k = 'ei'
    ins.kind = k
    return ins


TERMINATORS = {'jr', 'jp', 'call', 'ret', 'reti', 'jphl', 'rst', 'halt', 'ei'}


def load_symbols(path):
    syms = []
    with open(path, encoding='utf-8') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith(';'):
                continue
            m = re.match(r'([0-9a-fA-F]+):([0-9a-fA-F]+)\s+(\S+)', line)
            if not m:
                continue
            bank, addr = int(m.group(1), 16), int(m.group(2), 16)
            syms.append((bank, addr, m.group(3)))
    return syms


def load_data_ranges(path):
    """(Datensektionen, belegte Sektionen) als Listen von (bank, start, end)."""
    ranges = []
    used = []
    if not path or not os.path.exists(path):
        return ranges, used
    bank = None
    with open(path, encoding='utf-8') as f:
        for line in f:
            m = re.match(r'(ROM0|ROMX) bank #(\d+):', line)
            if m:
                bank = int(m.group(2))
                continue
            if line.startswith(('WRAM', 'SRAM', 'HRAM', 'VRAM', 'SUMMARY', 'OAM')):
                bank = None
                continue
            if bank is None:
                continue
            m = re.match(r'\s+SECTION: \$([0-9a-f]+)(?:-\$([0-9a-f]+))? \(.*\) \["(.*)"\]', line)
            if m:
                start = int(m.group(1), 16)
                end = int(m.group(2), 16) if m.group(2) else start
                if m.group(2):
                    used.append((bank, start, end))
                if DATA_SECTIONS.match(m.group(3)):
                    ranges.append((bank, start, end))
    return ranges, used


class Recompiler:
    def __init__(self, rom, precise):
        self.rom = rom
        self.nbanks = len(rom) // 0x4000
        self.precise = precise
        self.instrs = {}        # (bank, addr) -> Instr
        self.parent = {}        # union-find
        self.entries = set()    # (bank, addr)
        self.work = []
        self.used = set()       # (bank, addr) belegter ROM-Bytes laut .map

    # --- Union-Find ---
    def find(self, k):
        p = self.parent
        root = k
        while p[root] != root:
            root = p[root]
        while p[k] != root:
            p[k], k = root, p[k]
        return root

    def union(self, a, b):
        ra, rb = self.find(a), self.find(b)
        if ra != rb:
            if ra < rb:
                self.parent[rb] = ra
            else:
                self.parent[ra] = rb

    def key(self, bank, addr):
        return (0 if addr < 0x4000 else bank, addr)

    def seed(self, bank, addr, entry=True):
        if addr >= 0x8000:
            return
        if addr >= 0x4000 and (bank <= 0 or bank >= self.nbanks):
            return
        k = self.key(bank, addr)
        if entry:
            self.entries.add(k)
        if k not in self.instrs:
            self.work.append(k)

    def discover(self):
        while self.work:
            k = self.work.pop()
            bank, addr = k
            while True:
                if k in self.instrs:
                    break
                if self.used and (k not in self.used or self.key(bank, addr + LEN[rom_byte(self.rom, bank, addr)] - 1) not in self.used):
                    break
                ins = decode(self.rom, bank, addr)
                if ins is None:
                    break
                self.instrs[k] = ins
                self.parent[k] = k
                kind = ins.kind
                t = ins.target
                if t is not None:
                    if kind == 'rst':
                        self.seed(0, t)
                    elif same_space(bank, addr, t):
                        self.seed(bank, t, entry=(kind in ('call', 'callc')))
                    elif t < 0x4000:
                        self.seed(0, t)
                if kind in ('call', 'callc', 'rst', 'halt', 'ei'):
                    self.seed(bank, ins.nxt)
                if kind in TERMINATORS:
                    break
                limit = 0x4000 if addr < 0x4000 else 0x8000
                if ins.nxt >= limit:
                    break
                addr = ins.nxt
                k = self.key(bank, addr)

    def build_components(self):
        for k, ins in self.instrs.items():
            bank, addr = k
            if ins.kind not in TERMINATORS:
                nk = self.key(bank, ins.nxt)
                if nk in self.instrs and ins.nxt < (0x4000 if addr < 0x4000 else 0x8000):
                    self.union(k, nk)
            if ins.kind in ('jr', 'jrc', 'jp', 'jpc') and same_space(bank, addr, ins.target):
                tk = self.key(bank, ins.target)
                if tk in self.instrs:
                    self.union(k, tk)
        comps = {}
        for k in self.instrs:
            comps.setdefault(self.find(k), []).append(k)
        for v in comps.values():
            v.sort()
        return comps

    # --- C-Erzeugung ---
    def emit_component(self, root, keys, out):
        bank = root[0]
        comp = set(keys)
        fname = 'rc_%02x_%04x' % (bank, root[1])
        romx = root[1] >= 0x4000
        precise = self.precise

        entries = sorted(k for k in keys if k in self.entries or precise)
        # goto-Ziele, die Labels brauchen
        labels = set(entries)
        exits = set()
        for k in keys:
            ins = self.instrs[k]
            if ins.kind in ('jr', 'jrc', 'jp', 'jpc') and same_space(bank, k[1], ins.target):
                tk = self.key(bank, ins.target)
                if tk in comp:
                    labels.add(tk)
                    if ins.target <= k[1] or precise:
                        # Rueckwaertssprung: Deadline-Exit muss wieder einsteigen koennen
                        exits.add(tk)
        entries = sorted(set(entries) | exits)
        for e in entries:
            self.entries.add(e)

        w = out.append
        w('static void %s(void)\n{\n' % fname)
        w('    REGS_DECL\n')
        w('    REGS_LOAD\n')
        w('    switch (g_gb.cpu.pc) {\n')
        for e in entries:
            w('    case 0x%04x: goto L_%04x;\n' % (e[1], e[1]))
        w('    default: EXIT(g_gb.cpu.pc);\n    }\n')

        # Fallthrough-Ziele, die nicht direkt folgen (ueberlappende Dekodierung)
        for i, k in enumerate(keys):
            ins = self.instrs[k]
            if ins.kind not in TERMINATORS:
                nk = self.key(bank, ins.nxt)
                if nk in comp and (i + 1 >= len(keys) or keys[i + 1] != nk):
                    labels.add(nk)

        for i, k in enumerate(keys):
            ins = self.instrs[k]
            a = k[1]
            if k in labels:
                w('L_%04x:\n' % a)
            body = self.emit_instr(bank, ins, comp, romx)
            for line in body:
                w('    ' + line + '\n')
            if ins.kind not in TERMINATORS:
                nk = self.key(bank, ins.nxt)
                if nk not in comp:
                    w('    EXIT(0x%04x);\n' % ins.nxt)
                elif i + 1 >= len(keys) or keys[i + 1] != nk:
                    w('    goto L_%04x;\n' % ins.nxt)
        w('}\n\n')
        return fname, entries

    def jump(self, bank, ins, comp, target, cyc):
        """Code fuer einen genommenen Sprung."""
        tk = self.key(bank, target)
        s = ['CYC(%d);' % cyc]
        if same_space(bank, ins.addr, target) and tk in comp:
            if self.precise or target <= ins.addr:
                s.append('CHK(0x%04x);' % target)
            s.append('goto L_%04x;' % target)
        else:
            s.append('EXIT(0x%04x);' % target)
        return s

    def emit_instr(self, bank, ins, comp, romx):
        op = ins.op
        n = ins.b1
        nn = ins.b1 | (ins.b2 << 8)
        nxt = ins.nxt
        precise = self.precise
        L = ['/* %04x: %02x */' % (ins.addr, op)]

        def done(cyc, wrote_unknown=False):
            L.append('CYC(%d);' % cyc)
            if wrote_unknown and romx:
                L.append('BANKCHK(0x%02x, 0x%04x);' % (bank, nxt))
            if precise:
                L.append('CHK(0x%04x);' % nxt)
            return L

        def rd(i):
            return 'RD(HL)' if i == 6 else R8[i]

        def wr(i, v):
            if i == 6:
                return 'WR(HL, %s);' % v
            return '%s = %s;' % (R8[i], v)

        k = ins.kind
        if k == 'jr':
            return L + self.jump(bank, ins, comp, ins.target, 12)
        if k in ('jrc', 'jpc'):
            c = COND[(op >> 3) & 3]
            taken, nt = (12, 8) if k == 'jrc' else (16, 12)
            L.append('if (%s) {' % c)
            L.extend('    ' + x for x in self.jump(bank, ins, comp, ins.target, taken))
            L.append('}')
            return done(nt)
        if k == 'jp':
            return L + self.jump(bank, ins, comp, ins.target, 16)
        if k == 'call':
            L.append('PUSH16(0x%04x); CYC(24); EXIT(0x%04x);' % (nxt, ins.target))
            return L
        if k == 'callc':
            c = COND[(op >> 3) & 3]
            L.append('if (%s) { PUSH16(0x%04x); CYC(24); EXIT(0x%04x); }' % (c, nxt, ins.target))
            return done(12)
        if k == 'rst':
            L.append('PUSH16(0x%04x); CYC(16); EXIT(0x%04x);' % (nxt, ins.target))
            return L
        if k == 'ret':
            L.append('POP16(t16); CYC(16); EXIT(t16);')
            return L
        if k == 'retc':
            c = COND[(op >> 3) & 3]
            L.append('if (%s) { POP16(t16); CYC(20); EXIT(t16); }' % c)
            return done(8)
        if k == 'reti':
            L.append('POP16(t16); g_gb.cpu.ime = 1; CYC(16); FLUSH(); SAVE_REGS(); gb_update_deadline(); g_gb.cpu.pc = t16; return;')
            return L
        if k == 'jphl':
            L.append('CYC(4); EXIT(HL);')
            return L
        if k == 'halt':
            L.append('g_gb.cpu.halted = 1; CYC(4); EXIT(0x%04x);' % nxt)
            return L
        if k == 'ei':
            L.append('g_gb.cpu.ime_delay = 1; CYC(4); EXIT(0x%04x);' % nxt)
            return L

        # --- normale Befehle ---
        if 0x40 <= op < 0x80:
            d, s = (op >> 3) & 7, op & 7
            if d == 6:
                L.append('WR(HL, %s);' % R8[s])
                return done(8, True)
            L.append('%s = %s;' % (R8[d], rd(s)))
            return done(8 if s == 6 else 4)
        if 0x80 <= op < 0xC0:
            fn = ALU[(op >> 3) & 7]
            v = rd(op & 7)
            if fn == 'alu_cp':
                L.append('alu_cp(a, %s, &f);' % v)
            else:
                L.append('a = %s(a, %s, &f);' % (fn, v))
            return done(8 if (op & 7) == 6 else 4)
        if op == 0x00 or op == 0x10:
            return done(4)
        if op in (0x01, 0x11, 0x21):
            rp = ['b', 'd', 'h'][op >> 4]
            lo = ['c', 'e', 'l'][op >> 4]
            L.append('%s = 0x%02x; %s = 0x%02x;' % (rp, ins.b2, lo, ins.b1))
            return done(12)
        if op == 0x31:
            L.append('sp = 0x%04x;' % nn)
            return done(12)
        if op == 0x02:
            L.append('WR(BC, a);')
            return done(8, True)
        if op == 0x12:
            L.append('WR(DE, a);')
            return done(8, True)
        if op == 0x22:
            L.append('WR(HL, a); SET_HL(HL + 1);')
            return done(8, True)
        if op == 0x32:
            L.append('WR(HL, a); SET_HL(HL - 1);')
            return done(8, True)
        if op == 0x0A:
            L.append('a = RD(BC);')
            return done(8)
        if op == 0x1A:
            L.append('a = RD(DE);')
            return done(8)
        if op == 0x2A:
            L.append('a = RD(HL); SET_HL(HL + 1);')
            return done(8)
        if op == 0x3A:
            L.append('a = RD(HL); SET_HL(HL - 1);')
            return done(8)
        if op in (0x03, 0x13, 0x23):
            p = ['BC', 'DE', 'HL'][op >> 4]
            L.append('SET_%s(%s + 1);' % (p, p))
            return done(8)
        if op in (0x0B, 0x1B, 0x2B):
            p = ['BC', 'DE', 'HL'][op >> 4]
            L.append('SET_%s(%s - 1);' % (p, p))
            return done(8)
        if op == 0x33:
            L.append('sp++;')
            return done(8)
        if op == 0x3B:
            L.append('sp--;')
            return done(8)
        if op in (0x09, 0x19, 0x29, 0x39):
            p = ['BC', 'DE', 'HL', 'sp'][op >> 4]
            L.append('SET_HL(alu_add16(HL, %s, &f));' % p)
            return done(8)
        if (op & 0xC7) == 0x04:
            r = (op >> 3) & 7
            if r == 6:
                L.append('t8 = alu_inc(RD(HL), &f); WR(HL, t8);')
                return done(12, True)
            L.append('%s = alu_inc(%s, &f);' % (R8[r], R8[r]))
            return done(4)
        if (op & 0xC7) == 0x05:
            r = (op >> 3) & 7
            if r == 6:
                L.append('t8 = alu_dec(RD(HL), &f); WR(HL, t8);')
                return done(12, True)
            L.append('%s = alu_dec(%s, &f);' % (R8[r], R8[r]))
            return done(4)
        if (op & 0xC7) == 0x06:
            r = (op >> 3) & 7
            if r == 6:
                L.append('WR(HL, 0x%02x);' % n)
                return done(12, True)
            L.append('%s = 0x%02x;' % (R8[r], n))
            return done(8)
        if op == 0x07:
            L.append('a = alu_rlca(a, &f);')
            return done(4)
        if op == 0x0F:
            L.append('a = alu_rrca(a, &f);')
            return done(4)
        if op == 0x17:
            L.append('a = alu_rla(a, &f);')
            return done(4)
        if op == 0x1F:
            L.append('a = alu_rra(a, &f);')
            return done(4)
        if op == 0x08:
            L.append(static_wr(nn, '(U8)sp') + ' ' + static_wr((nn + 1) & 0xFFFF, '(U8)(sp >> 8)'))
            return done(20, nn < 0x8000)
        if op == 0x27:
            L.append('a = alu_daa(a, &f);')
            return done(4)
        if op == 0x2F:
            L.append('a = (U8)~a; f |= FLAG_N | FLAG_H;')
            return done(4)
        if op == 0x37:
            L.append('f = (U8)((f & FLAG_Z) | FLAG_C);')
            return done(4)
        if op == 0x3F:
            L.append('f = (U8)((f & FLAG_Z) | ((f & FLAG_C) ^ FLAG_C));')
            return done(4)
        if op in (0xC1, 0xD1, 0xE1):
            p = ['BC', 'DE', 'HL'][(op >> 4) - 0xC]
            L.append('POP16(t16); SET_%s(t16);' % p)
            return done(12)
        if op == 0xF1:
            L.append('POP16(t16); a = (U8)(t16 >> 8); f = (U8)(t16 & 0xF0);')
            return done(12)
        if op in (0xC5, 0xD5, 0xE5):
            p = ['BC', 'DE', 'HL'][(op >> 4) - 0xC]
            L.append('PUSH16(%s);' % p)
            return done(16)
        if op == 0xF5:
            L.append('PUSH16((U16)((a << 8) | f));')
            return done(16)
        if op in (0xC6, 0xCE, 0xD6, 0xDE, 0xE6, 0xEE, 0xF6, 0xFE):
            fn = ALU[(op >> 3) & 7]
            if fn == 'alu_cp':
                L.append('alu_cp(a, 0x%02x, &f);' % n)
            else:
                L.append('a = %s(a, 0x%02x, &f);' % (fn, n))
            return done(8)
        if op == 0xCB:
            r = n & 7
            x = n >> 6
            y = (n >> 3) & 7
            v = rd(r)
            if x == 0:
                if r == 6:
                    L.append('t8 = alu_cb_rot(%d, RD(HL), &f); WR(HL, t8);' % y)
                    return done(16, True)
                L.append('%s = alu_cb_rot(%d, %s, &f);' % (R8[r], y, R8[r]))
                return done(8)
            if x == 1:
                L.append('alu_bit(%d, %s, &f);' % (y, v))
                return done(12 if r == 6 else 8)
            if x == 2:
                if r == 6:
                    L.append('t8 = RD(HL); WR(HL, (U8)(t8 & 0x%02x));' % (~(1 << y) & 0xFF))
                    return done(16, True)
                L.append('%s &= 0x%02x;' % (R8[r], ~(1 << y) & 0xFF))
                return done(8)
            if r == 6:
                L.append('t8 = RD(HL); WR(HL, (U8)(t8 | 0x%02x));' % (1 << y))
                return done(16, True)
            L.append('%s |= 0x%02x;' % (R8[r], 1 << y))
            return done(8)
        if op == 0xE0:
            L.append(static_wr(0xFF00 | n, 'a'))
            return done(12)
        if op == 0xF0:
            L.append('a = %s;' % static_rd(0xFF00 | n))
            return done(12)
        if op == 0xE2:
            L.append('WRH(c, a);')
            return done(8)
        if op == 0xF2:
            L.append('a = RDH(c);')
            return done(8)
        if op == 0xEA:
            L.append(static_wr(nn, 'a'))
            return done(16, nn < 0x8000)
        if op == 0xFA:
            L.append('a = %s;' % static_rd(nn))
            return done(16)
        if op == 0xE8:
            L.append('sp = alu_addsp(sp, (S8)0x%02x, &f);' % n)
            return done(16)
        if op == 0xF8:
            L.append('SET_HL(alu_addsp(sp, (S8)0x%02x, &f));' % n)
            return done(12)
        if op == 0xF9:
            L.append('sp = HL;')
            return done(8)
        if op == 0xF3:
            L.append('g_gb.cpu.ime = 0; g_gb.cpu.ime_delay = 0;')
            return done(4)
        raise RuntimeError('Opcode %02x nicht behandelt' % op)


def static_rd(addr):
    """C-Ausdruck fuer einen Lesezugriff auf eine zur Compilezeit bekannte Adresse."""
    if addr < 0x4000:
        return 'g_gb.rom[0x%04x]' % addr
    if addr < 0x8000:
        return 'g_gb.rom[((U32)g_gb.rom_bank << 14) | 0x%04x]' % (addr & 0x3FFF)
    if addr < 0xA000:
        return 'g_gb.vram[0x%04x]' % (addr & 0x1FFF)
    if 0xC000 <= addr < 0xFE00:
        return 'g_gb.wram[0x%04x]' % (addr & 0x1FFF)
    if 0xFE00 <= addr < 0xFEA0:
        return 'g_gb.oam[0x%02x]' % (addr - 0xFE00)
    if 0xFF80 <= addr < 0xFFFF:
        return 'g_gb.hram[0x%02x]' % (addr & 0x7F)
    if addr == 0xFFFF:
        return 'g_gb.ie'
    return 'RD(0x%04x)' % addr


def static_wr(addr, v):
    """C-Anweisung fuer einen Schreibzugriff auf eine bekannte Adresse."""
    if 0x8000 <= addr < 0xA000:
        return 'g_gb.vram[0x%04x] = %s;' % (addr & 0x1FFF, v)
    if 0xC000 <= addr < 0xFE00:
        return 'g_gb.wram[0x%04x] = %s;' % (addr & 0x1FFF, v)
    if 0xFE00 <= addr < 0xFEA0:
        return 'g_gb.oam[0x%02x] = %s;' % (addr - 0xFE00, v)
    if 0xFF80 <= addr < 0xFFFF:
        return 'g_gb.hram[0x%02x] = %s;' % (addr & 0x7F, v)
    return 'WR(0x%04x, %s);' % (addr, v)


HEADER = r'''/* Automatisch erzeugt von recomp/recomp.py - nicht von Hand bearbeiten. */
#include "gb_recomp.h"

'''


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('rom')
    ap.add_argument('sym')
    ap.add_argument('map')
    ap.add_argument('outdir')
    ap.add_argument('--precise', action='store_true')
    ap.add_argument('--seeds', action='append', default=[])
    args = ap.parse_args()

    rom = open(args.rom, 'rb').read()
    rc = Recompiler(rom, args.precise)
    data_ranges, used = load_data_ranges(args.map)
    for b, st, en in used:
        for x in range(st, en + 1):
            rc.used.add((b, x))

    def is_data(bank, addr):
        for b, s, e in data_ranges:
            if b == bank and s <= addr <= e:
                return True
        return False

    for v in list(range(0, 0x40, 8)) + [0x40, 0x48, 0x50, 0x58, 0x60, 0x100]:
        rc.seed(0, v)
    nsyms = 0
    for bank, addr, name in load_symbols(args.sym):
        if addr >= 0x8000 or is_data(bank, addr):
            continue
        if bank != 0 and DATA_NAMES.search(name.split('.')[-1]):
            continue
        rc.seed(bank, addr)
        nsyms += 1
    for path in args.seeds:
        if not os.path.exists(path):
            continue
        with open(path) as f:
            for line in f:
                m = re.match(r'([0-9a-fA-F]+):([0-9a-fA-F]+)', line.strip())
                if m:
                    rc.seed(int(m.group(1), 16), int(m.group(2), 16))
    rc.discover()
    comps = rc.build_components()

    os.makedirs(args.outdir, exist_ok=True)
    by_bank = {}
    for root, keys in comps.items():
        by_bank.setdefault(root[0], []).append((root, keys))

    reg_lines = []
    total_entries = 0
    bank_files = []
    for bank in sorted(by_bank):
        out = [HEADER]
        regs = []
        for root, keys in sorted(by_bank[bank]):
            fname, entries = rc.emit_component(root, keys, out)
            for e in entries:
                regs.append((e[1], fname))
        out.append('static const RecompEntry k_entries_%02x[] = {\n' % bank)
        for addr, fname in regs:
            out.append('    { 0x%04x, %s },\n' % (addr, fname))
        out.append('    { 0, 0 }\n};\n\n')
        out.append('void recomp_reg_%02x(void)\n{\n    recomp_register_list(0x%02x, k_entries_%02x);\n}\n'
                   % (bank, bank, bank))
        path = os.path.join(args.outdir, 'recomp_b%02x.c' % bank)
        with open(path, 'w') as f:
            f.write(''.join(out))
        bank_files.append(path)
        reg_lines.append(bank)
        total_entries += len(regs)

    with open(os.path.join(args.outdir, 'recomp_all.c'), 'w') as f:
        f.write(HEADER)
        f.write('int recomp_available = 1;\n\n')
        for b in reg_lines:
            f.write('void recomp_reg_%02x(void);\n' % b)
        f.write('\nvoid recomp_register_all(void)\n{\n')
        for b in reg_lines:
            f.write('    recomp_reg_%02x();\n' % b)
        f.write('}\n')

    print('Symbole: %d  Befehle: %d  Komponenten: %d  Einstiege: %d  Baenke: %d  Modus: %s'
          % (nsyms, len(rc.instrs), len(comps), total_entries, len(reg_lines),
             'praezise' if args.precise else 'schnell'))


if __name__ == '__main__':
    main()
