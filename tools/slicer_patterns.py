#!/usr/bin/env python3
"""Slicer+FLG TIMING PTN P01-P16, transcribed from the owner's manual (p. 44, note *2).

Each pattern is one 4/4 bar written in note values. Tokens:
  n<dur> = sounding note, r<dur> = rest (sound cut)
  durations in 32nd-note units: 8=4, 8.=6, 16=2, 16.=3
P09 contains a tie (16th tied to 8th) which is written as one sounding
note of 6 units.

Running this script checks that every bar sums to 32 units and prints the
32-step gate strings used in docs/SPEC.md and the effect implementation.
  X = note onset, = = note continues, . = silent
"""
U = {"8": 4, "8.": 6, "16": 2, "16.": 3, "tie16_8": 6}

P = {
 1: "n8 r8 n8 r8 n8 r8 n8 r8",
 2: "n8. r16 n8. r16 n8. r16 n8. r16",
 3: "n16 r8. n16 r8. n16 r8. n16 r8.",
 4: "n8. r16 n16 r16 n16 r16 n8. r16 n16 r16 n16 r16",
 5: "n16 r16 n16 r16 n16 r8. n16 r16 n16 r16 n16 r8.",
 6: "n16 r16 n16 r16 n16 r16 n16 r16 n16 r16 n16 r16 n16 r8.",
 7: "n8 r8 n16 r16 n16 r16 n8 r8 n16 r16 n16 r16",
 8: "n8 r8 r16 n8 r16 n8 r8 r16 n8 r16",
 9: "n8 r8 n16 r8 ntie16_8 r8 n16 r8.",
10: "n8 r16 n16 r16 n16 r8 n8 r8 r16 n16 r8",
11: "r16 n8 r16 r8 n16 r16 r16 n8 r16 r8 n16 r16",
12: "r8 n8 r16 n16 r16 n16 r8 n8 r8. n16",
13: "n8. r16 n16 r16 n16 r16 n16 r16 n8. r16 n16 r16",
14: "n16 r16 n16 r16 n16 r16 n16 r16 n8. r16 n16 r16 n16 r16",
15: "n8 r8 n16 r16 n16 r16 n16 r16 n16 r16 n16 r8.",
16: "n8. r16 n8. r16 n16 r16 n8. r16 n16 r16",
}

def expand(s):
    out = ""
    for t in s.split():
        kind, d = t[0], U[t[1:]]
        out += ("X" + "=" * (d - 1)) if kind == "n" else "." * d
    return out

if __name__ == "__main__":
    ok = True
    for k, s in P.items():
        g = expand(s)
        flag = "" if len(g) == 32 else f"  <-- {len(g)} units, expected 32"
        ok &= len(g) == 32
        print(f"P{k:02d} {g}{flag}")
    raise SystemExit(0 if ok else 1)
