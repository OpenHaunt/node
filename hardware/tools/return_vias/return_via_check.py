#!/usr/bin/env python3
"""Return-via check: for each signal via, count the GND vias within RADIUS whose straight line to it is not
crossed by a track of another net on the reference planes of the layers the signal uses at that via
(F.Cu -> In1.Cu, B.Cu -> In2.Cu, both GND here). The return current changes planes through the GND via, so
only a cut in those planes lengthens its path; a track on the signal layer itself does not.

Usage: return_via_check.py <board.kicad_pcb> [out.csv]   (reads the saved board; save in KiCad first)
"""
import csv, math, re, sys

RADIUS = 2.0
POWER = {'GND', '+5V', '+3V3', '+3.3VA', '+12V'}
REF = {'F.Cu': 'In1.Cu', 'B.Cu': 'In2.Cu', 'In1.Cu': 'In1.Cu', 'In2.Cu': 'In2.Cu'}


def blocks(s, start_re):
    for m in re.finditer(start_re, s):
        a = m.start() + m.group(0).index('(')
        d = 0
        for b in range(a, len(s)):
            if s[b] == '(':
                d += 1
            elif s[b] == ')':
                d -= 1
                if d == 0:
                    yield m.group(1), s[a:b + 1]
                    break


def segdist(p, a, b):
    (ax, ay), (bx, by), (px, py) = a, b, p
    dx, dy = bx - ax, by - ay
    L = dx * dx + dy * dy
    t = 0 if L == 0 else max(0, min(1, ((px - ax) * dx + (py - ay) * dy) / L))
    return math.hypot(px - ax - t * dx, py - ay - t * dy)


def ccw(A, B, C):
    return (C[1] - A[1]) * (B[0] - A[0]) > (B[1] - A[1]) * (C[0] - A[0])


def seg_seg(a, b, c, d):
    if ccw(a, c, d) != ccw(b, c, d) and ccw(a, b, c) != ccw(a, b, d):
        return 0.0
    return min(segdist(a, c, d), segdist(b, c, d), segdist(c, a, b), segdist(d, a, b))


def main():
    s = open(sys.argv[1]).read()
    net = lambda b: (re.search(r'\(net "([^"]*)"\)', b) or [None, ''])[1]
    pt = lambda b, k: tuple(map(float, re.search(r'\(%s ([\d.\-]+) ([\d.\-]+)' % k, b).groups()))
    segs = [(pt(b, 'start'), pt(b, 'end'), float(re.search(r'\(width ([\d.]+)\)', b)[1]), net(b),
             re.search(r'\(layer "([^"]+)"\)', b)[1]) for _, b in blocks(s, r'\n\t\((segment|arc)\b')]
    vias = [(pt(b, 'at'), net(b)) for _, b in blocks(s, r'\n\t\((via)\b')]

    rows = []
    for (vx, vy), vn in vias:
        if vn in POWER or not vn:
            continue
        lay = {REF[L] for a, b, w, n, L in segs if n == vn and min(math.dist((vx, vy), a), math.dist((vx, vy), b)) < 0.05}
        good, blocked = [], []
        for (gx, gy), gn in vias:
            d = math.dist((vx, vy), (gx, gy))
            if gn != 'GND' or d > RADIUS:
                continue
            hit = sorted({n for a, b, w, n, L in segs
                          if n not in (vn, 'GND') and L in lay and seg_seg((vx, vy), (gx, gy), a, b) < w / 2})
            (blocked if hit else good).append((round(d, 2), hit))
        good.sort(); blocked.sort()
        rows.append({'net': vn, 'x': vx, 'y': vy, 'clear': len(good), 'blocked': len(blocked),
                     'nearest_clear_mm': good[0][0] if good else '',
                     'blocked_by': ' '.join(sorted({n for _, h in blocked for n in h}))})
    rows.sort(key=lambda r: (r['clear'], r['net']))
    hist = {}
    for r in rows:
        hist[min(r['clear'], 3)] = hist.get(min(r['clear'], 3), 0) + 1
    print(f'{len(rows)} signal vias; clear GND vias within {RADIUS} mm: '
          + ', '.join(f'{k}{"+" if k == 3 else ""}: {hist[k]}' for k in sorted(hist)))
    for r in rows:
        if r['clear'] < 2:
            print(f"  {r['clear']}  {r['net']:<22} ({r['x']:.2f}, {r['y']:.2f})  blocked by {r['blocked_by'] or '-'}")
    if len(sys.argv) > 2:
        with open(sys.argv[2], 'w', newline='') as f:
            w = csv.DictWriter(f, fieldnames=list(rows[0]))
            w.writeheader(); w.writerows(rows)


if __name__ == '__main__':
    main()
