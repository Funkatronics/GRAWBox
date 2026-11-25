"""Find functions of one generated program in another by their instruction shape.

    python -I tools/match_functions.py <generated A> <generated B> <address in A> [...]

Each function is reduced to its instruction sequence with branch targets and address halves removed.
Exact matches are listed first, then the closest ones by shared instruction trigrams. Reads only
locally generated code, which stays untracked.
"""

import glob
import re
import sys

INSN = re.compile(r"^\s*// ([a-z][\w.]*) ?(.*)$")
FUNC = re.compile(r"^DEFINE_REX_FUNC\((\w+)\)")


def load(folder):
    functions, name, body = {}, None, []
    for path in sorted(glob.glob(folder + "/*recomp*.cpp")):
        for line in open(path, encoding="utf-8", errors="replace"):
            m = FUNC.match(line)
            if m:
                name, body = m.group(1), []
                functions[name] = body
            elif name:
                m = INSN.match(line)
                if m:
                    body.append((m.group(1), m.group(2).strip()))
    return functions


def shape(body, offsets):
    out = []
    for op, args in body:
        if re.match(r"b[a-z]*[.+-]?$", op) and "0x" in args:
            args = re.sub(r"0x[0-9a-f]+", "T", args)
        elif op == "lis" or not offsets:
            args = re.sub(r"-?\d+(?=\(|$)", "N", args)
        out.append(op + " " + args)
    return tuple(out)


def main(argv):
    if len(argv) < 3:
        sys.exit(__doc__)
    a, b = load(argv[0]), load(argv[1])
    exact, loose = {}, {}
    for name, body in b.items():
        exact.setdefault(shape(body, True), []).append(name)
        loose.setdefault(shape(body, False), []).append(name)
    grams = {}
    for name, body in b.items():
        s = [op for op, _ in body]
        grams[name] = set(zip(s, s[1:], s[2:], s[3:]))
    for address in argv[2:]:
        name = "sub_" + address.upper().replace("0X", "")
        if name not in a:
            print(address, "not in A")
            continue
        body = a[name]
        e, l = exact.get(shape(body, True), []), loose.get(shape(body, False), [])
        text = f"{name} ({len(body)} insns): exact={','.join(e[:4]) or '-'} shape={','.join(l[:4]) or '-'}"
        if not e and not l:
            s = [op for op, _ in body]
            mine = set(zip(s, s[1:], s[2:], s[3:]))
            scored = []
            for other, g in grams.items():
                if 0.5 < (len(b[other]) + 1) / (len(body) + 1) < 2 and mine and g:
                    scored.append((len(mine & g) / len(mine | g), other, len(b[other])))
            scored.sort(reverse=True)
            text += " close=" + ", ".join(f"{o} {r:.2f} ({n})" for r, o, n in scored[:3])
        print(text)


if __name__ == "__main__":
    main(sys.argv[1:])
