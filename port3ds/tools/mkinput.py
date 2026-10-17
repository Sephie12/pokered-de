#!/usr/bin/env python3
"""mkinput.py - erzeugt Eingabeskripte fuer den Host-Test aus einer kompakten Sprache.
Befehle (Leerzeichen-getrennt):  A, B, S (Start), s (Select): kurz druecken
  U3 / D2 / L1 / R4: so viele Felder laufen;  W60: 60 Frames warten;  Ax10: 10x A
"""
import re, sys
def build(prog, start=1300):
    f = start; out = []
    def press(b, hold=4, gap=40):
        nonlocal f
        out.append(f"{f} {b}"); out.append(f"{f+hold} -"); f += gap
    for tok in prog.split():
        m = re.match(r'([ABSsUDLR])(?:x(\d+))?(\d+)?$', tok)
        if tok.startswith('W'):
            f += int(tok[1:]); continue
        b, rep, n = m.group(1), int(m.group(2) or 1), m.group(3)
        if b in 'UDLR':
            steps = int(n or 1)
            out.append(f"{f} {b}"); f += steps * 16 + 2
            out.append(f"{f} -"); f += 8
        else:
            for _ in range(rep): press(b, gap=int(n) if n else 45)
    return out, f
if __name__ == '__main__':
    prog = open(sys.argv[1]).read() if len(sys.argv) > 2 else sys.argv[1]
    out, f = build(prog)
    open(sys.argv[-1] if len(sys.argv) > 2 else 'build/input.txt', 'w').write("\n".join(out) + "\n")
    print(f)
