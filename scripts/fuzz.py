#!/usr/bin/env python3
# Random fault trees, every gate's probability checked against brute force. Usage: scripts/fuzz.py [--num N] [-j J] [--solver CMD]

import argparse
import itertools
import os
import random
import re
import shlex
import subprocess
import sys
import tempfile
import traceback
from fractions import Fraction as F
from multiprocessing import Pool

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOL = os.path.join(ROOT, "build", "openpsa-to-wmc")
OUT = os.path.join(ROOT, "fuzz-out")
SOLVER = os.path.join(ROOT, "..", "ganak", "build", "ganak") + " --mode 1"


class Fail(Exception):
    pass


def run(cmd, inp=None):
    r = subprocess.run(cmd, input=inp, capture_output=True, text=True, timeout=60)
    if r.returncode < 0 or r.returncode in (134, 136, 139):
        raise Fail(f"crash {r.returncode}: {' '.join(cmd)}\n{r.stderr[-2000:]}")
    return r


def count(solver, cnf_path):
    r = run(shlex.split(solver) + [cnf_path])
    m = re.search(r"c s exact arb (?:frac|int) (\S+)", r.stdout)
    if not m:
        raise Fail(f"no exact count from the solver:\n{r.stdout[-1500:]}{r.stderr[-500:]}")
    return F(m.group(1))


def rand_prob(rnd):
    r = rnd.random()
    if r < 0.05:
        return F(0)
    if r < 0.1:
        return F(1)
    return rnd.choice([F(1, 2), F(1, 4), F(3, 4), F(1, 10), F(1, 5), F(2, 5), F(1, 8), F(1, 1000), F(1, 3), F(2, 7)])


def gen(rnd):
    basics = {f"BE-{i}.x": rand_prob(rnd) for i in range(rnd.randint(1, 6))}
    houses = {f"H{i}": rnd.random() < 0.5 for i in range(rnd.randint(0, 1))}
    gates = []

    def formula(lower, depth):
        refs = [("basic-event", b) for b in basics] + [("house-event", h) for h in houses] + [("gate", g) for g, _ in lower]
        if depth <= 0 or rnd.random() < 0.3:
            if rnd.random() < 0.05:
                return ("constant", rnd.random() < 0.5)
            return rnd.choice(refs)
        op = rnd.choice(["and", "or", "not", "xor", "atleast", "nand", "nor", "iff", "imply", "cardinality"])
        if op == "not":
            return ("not", [formula(lower, depth - 1)])
        n = 2 if op in ("iff", "imply") else rnd.randint(1, 4)
        kids = [formula(lower, depth - 1) for _ in range(n)]
        if op == "atleast":
            return ("atleast", kids, rnd.randint(1, n))
        if op == "cardinality":
            lo = rnd.randint(0, n)
            return ("cardinality", kids, lo, rnd.randint(lo, n))
        return (op, kids)

    for i in range(rnd.randint(1, 5)):
        gates.append((f"G{i}", formula(list(gates), 3)))
    return basics, houses, gates


def xml(basics, houses, gates):
    def f2x(f):
        t = f[0]
        if t in ("basic-event", "house-event", "gate"):
            return f'<{t} name="{f[1]}"/>'
        if t == "constant":
            return f'<constant value="{"true" if f[1] else "false"}"/>'
        attr = f' min="{f[2]}"' if t == "atleast" else f' min="{f[2]}" max="{f[3]}"' if t == "cardinality" else ""
        return f"<{t}{attr}>" + "".join(f2x(k) for k in f[1]) + f"</{t}>"
    out = ['<?xml version="1.0"?>', "<!-- random -->", "<opsa-mef>", '<define-fault-tree name="FT">']
    for g, f in gates:
        out.append(f'<define-gate name="{g}"><label>gate {g}</label>{f2x(f)}</define-gate>')
    for h, v in houses.items():
        out.append(f'<define-house-event name="{h}"><constant value="{"true" if v else "false"}"/></define-house-event>')
    out.append("</define-fault-tree><model-data>")
    for b, p in basics.items():
        out.append(f'<define-basic-event name="{b}"><float value="{float(p)!r}"/></define-basic-event>')
    out.append("</model-data></opsa-mef>")
    return "\n".join(out) + "\n"


def evf(f, val):
    t = f[0]
    if t in ("basic-event", "house-event", "gate"):
        return val[f[1]]
    if t == "constant":
        return f[1]
    ks = [evf(k, val) for k in f[1]]
    return {"and": lambda: all(ks), "or": lambda: any(ks), "not": lambda: not ks[0],
            "xor": lambda: sum(ks) % 2 == 1, "nand": lambda: not all(ks), "nor": lambda: not any(ks),
            "iff": lambda: ks[0] == ks[1], "imply": lambda: (not ks[0]) or ks[1],
            "atleast": lambda: sum(ks) >= f[2], "cardinality": lambda: f[2] <= sum(ks) <= f[3]}[t]()


def check_model(rnd, tmp, idx, solver):
    basics, houses, gates = gen(rnd)
    path = os.path.join(tmp, f"m{idx}.xml")
    with open(path, "w") as fh:
        fh.write(xml(basics, houses, gates))
    pb = {b: F(repr(float(p))) for b, p in basics.items()}
    want = {g: F(0) for g, _ in gates}
    for bits in itertools.product([False, True], repeat=len(pb)):
        w = F(1)
        val = dict(houses)
        for b, x in zip(pb, bits):
            w *= pb[b] if x else 1 - pb[b]
            val[b] = x
        for g, f in gates:
            val[g] = evf(f, val)
            if val[g]:
                want[g] += w
    for g, _ in gates:
        cnf = os.path.join(tmp, f"m{idx}-{g}.cnf")
        r = run([TOOL, "--top", g, "-o", cnf, path])
        if r.returncode != 0:
            raise Fail(f"openpsa-to-wmc failed:\n{r.stderr}")
        got = count(solver, cnf)
        if got != want[g]:
            raise Fail(f"P({g}): expected {want[g]}, got {got}")


def check_mutation(rnd, tmp, idx, solver):
    text = list(xml(*gen(rnd)))
    for _ in range(rnd.randint(1, 5)):
        pos = rnd.randrange(len(text) + 1)
        if rnd.random() < 0.5 and text:
            del text[min(pos, len(text) - 1)]
        else:
            text.insert(pos, rnd.choice(['<', '>', '/', '"', '=', ' ', 'a', '1', '<gate name="G9"/>', '</or>', '<and>']))
    path = os.path.join(tmp, f"x{idx}.xml")
    with open(path, "w") as fh:
        fh.write("".join(text))
    r = run([TOOL, "-o", os.path.join(tmp, "x.cnf"), path])
    if r.returncode not in (0, 1):
        raise Fail(f"mutated input: exit {r.returncode}\n{r.stderr}")


def one(args):
    seed, solver = args
    rnd = random.Random(seed)
    kind = rnd.choice([check_model, check_model, check_model, check_mutation])
    with tempfile.TemporaryDirectory() as tmp:
        try:
            kind(rnd, tmp, seed, solver)
            return None
        except Exception as e:
            os.makedirs(OUT, exist_ok=True)
            for fn in os.listdir(tmp):
                if fn.endswith(".xml"):
                    with open(os.path.join(tmp, fn)) as src, open(os.path.join(OUT, f"{seed}-{fn}"), "w") as dst:
                        dst.write(src.read())
            return f"seed {seed}: " + (str(e) if isinstance(e, Fail) else traceback.format_exc())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--num", type=int, default=500)
    ap.add_argument("-j", type=int, default=16)
    ap.add_argument("--seed", type=int, default=None)
    ap.add_argument("--solver", default=SOLVER, help="weighted model counter printing 'c s exact arb frac|int N'")
    a = ap.parse_args()
    base = a.seed if a.seed is not None else random.randrange(10 ** 9)
    bad = 0
    with Pool(a.j) as pool:
        for res in pool.imap_unordered(one, [(base + i, a.solver) for i in range(a.num)]):
            if res:
                bad += 1
                print(res, flush=True)
    print(f"{a.num} tests from seed {base}, {bad} failures" + (f" (files in {OUT})" if bad else ""))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
