"""Checks a HUD trace (--graw_native_hud_trace) from the edges layout against the game's own
placement. Each line: q <frame> <quad> <x0> <x1> <y0> <y1> <shift> <stretched> [<group x0 x1 y0
y1>], the box as the game placed it in 1280x720 pixels, how far it was moved and what it moved
with. Empty quads (spaces) are left out.

  python tools/check_hud.py <trace file> [--aspect 2.333]

Reports:
- unstable: a quad at the same place in two frames in a row but moved differently (the HUD jumps);
- off the map: a quad moved further from where the layout's map puts its middle than moving with
  a piece narrower than 400 pixels allows (it moved with something far away; lines of text and
  strips, moved by their middles, are left out);
- apart: an icon inside a box in the game's placement whose move differs from the box's.
"""
import argparse
import collections
import sys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('trace')
    ap.add_argument('--aspect', type=float, default=21 / 9)
    args = ap.parse_args()

    frames = collections.defaultdict(list)
    with open(args.trace) as f:
        for line in f:
            p = line.split()
            if len(p) < 9 or p[0] != 'q':
                continue
            frame = int(p[1])
            x0, x1, y0, y1 = map(int, p[3:7])
            if x1 <= x0 or y1 <= y0:
                continue  # empty (a space): nothing drawn
            group = tuple(map(int, p[9:13])) if len(p) >= 13 else (x0, x1, y0, y1)
            frames[frame].append(((x0, x1, y0, y1), float(p[7]), p[8] == '1', group))
    if not frames:
        print('no HUD quads in the trace')
        return 1

    order = sorted(frames)
    unstable = collections.Counter()
    examples = {}
    for a, b in zip(order, order[1:]):
        if b != a + 1:
            continue
        before = {}
        for box, shift, _, _ in frames[a]:
            before.setdefault(box, set()).add(round(shift))
        for box, shift, _, _ in frames[b]:
            if box in before and round(shift) not in before[box]:
                unstable[box] += 1
                examples.setdefault(box, (a, sorted(before[box]), round(shift)))

    k = (16 / 9) / args.aspect
    slack = (1 / k - 1) * 200 + 2  # a piece narrower than 400 pixels moves with its middle
    off = collections.Counter()
    apart = collections.Counter()
    for frame in order:
        quads = frames[frame]
        for box, shift, stretched, group in quads:
            centre = (box[0] + box[1]) / 2
            line = group[3] - group[2] <= 64  # a line of text or a strip, moved by its middle
            if not stretched and not line and abs(shift - (centre - 640) * (1 / k - 1)) > slack:
                off[box] += 1
        # Containment: a small quad inside a bigger, unstretched one moves with it.
        boxes = [(b, s) for b, s, st, _ in quads if not st]
        for inner, si in boxes:
            for outer, so in boxes:
                if outer == inner:
                    continue
                if (outer[0] <= inner[0] and inner[1] <= outer[1] and outer[2] <= inner[2]
                        and inner[3] <= outer[3] and abs(si - so) >= 1):
                    apart[(inner, outer)] += 1
                    break

    print(f'{len(order)} frames, {sum(len(q) for q in frames.values())} quads')
    print(f'unstable: {len(unstable)} quads ({sum(unstable.values())} times)')
    for box, n in unstable.most_common(10):
        a, was, now = examples[box]
        print(f'  x {box[0]}..{box[1]} y {box[2]}..{box[3]}: {n} times, e.g. frame {a} moved {was} then {now}')
    print(f'off the map: {len(off)} quads')
    for box, n in off.most_common(10):
        print(f'  x {box[0]}..{box[1]} y {box[2]}..{box[3]}: {n} frames')
    print(f'icons apart from their box: {len(apart)}')
    for (inner, outer), n in apart.most_common(10):
        print(f'  x {inner[0]}..{inner[1]} y {inner[2]}..{inner[3]} in x {outer[0]}..{outer[1]} y {outer[2]}..{outer[3]}: {n} frames')
    return 0


if __name__ == '__main__':
    sys.exit(main())
