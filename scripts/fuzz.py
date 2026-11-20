#!/usr/bin/env python3
# Random MEF models, every gate and sequence checked against brute force. Usage: scripts/fuzz.py [--num N] [-j J] [--solver CMD]

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
GANAK = os.environ.get("GANAK", os.path.join(ROOT, "..", "ganak", "build", "ganak"))
SOLVER = shlex.quote(GANAK) + " --mode 1"


class Fail(Exception):
    pass


def run(cmd, inp=None):
    r = subprocess.run(cmd, input=inp, capture_output=True, text=True, timeout=300)
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


class Model:
    def __init__(self):
        self.basics = {}   # name -> probability
        self.houses = {}   # name -> value
        self.gates = []    # (canonical name, formula, scope, local name, role attribute or None)
        self.comps = {}    # scope tuple -> role attribute or None, for components
        self.trees = []    # (name, fes, seqs, branches {name: body}, initial body)


def ref_name(rnd, gate, ctx):
    # Any path suffix that resolves from `ctx`: the full path always does, and so does the
    # part below a container shared with `ctx`; public gates are reachable by bare name too.
    canon, _, scope, local, _ = gate
    opts = [".".join(scope + (local,))]
    for k in range(1, len(scope) + 1):
        if ctx[:k] == scope[:k]:
            opts.append(".".join(scope[k:] + (local,)))
    if canon == local:
        opts.append(local)
    return rnd.choice(opts)


def gen(rnd):
    m = Model()
    m.basics = {f"BE-{i}.x": rand_prob(rnd) for i in range(rnd.randint(1, 6))}
    m.houses = {f"H{i}": rnd.random() < 0.5 for i in range(rnd.randint(0, 1))}
    scopes = [("FT",)]
    if rnd.random() < 0.5:
        scopes.append(("FT2",))
    if rnd.random() < 0.5:
        scopes.append(("FT", "C"))
        m.comps[("FT", "C")] = rnd.choice([None, "private", "public"])

    def formula(lower, ctx, depth):
        refs = [("basic-event", b) for b in m.basics] + [("house-event", h) for h in m.houses]
        refs += [("gate", g[0], ref_name(rnd, g, ctx)) for g in lower]
        if depth <= 0 or rnd.random() < 0.3:
            if rnd.random() < 0.05:
                return ("constant", rnd.random() < 0.5)
            r = rnd.choice(refs)
            if rnd.random() < 0.1:
                r = ("event",) + r[1:]
            return r
        op = rnd.choice(["and", "or", "not", "xor", "atleast", "nand", "nor", "iff", "imply", "cardinality"])
        if op == "not":
            return ("not", [formula(lower, ctx, depth - 1)])
        n = 2 if op in ("iff", "imply") else rnd.randint(1, 4)
        kids = [formula(lower, ctx, depth - 1) for _ in range(n)]
        if op == "atleast":
            return ("atleast", kids, rnd.randint(1, n))
        if op == "cardinality":
            lo = rnd.randint(0, n)
            return ("cardinality", kids, lo, rnd.randint(lo, n))
        return (op, kids)

    for i in range(rnd.randint(1, 5)):
        scope = rnd.choice(scopes)
        role = rnd.choice([None, None, "private", "public"])
        priv = role == "private" if role else m.comps.get(scope) == "private"
        local = f"G{i}"
        canon = ".".join(scope + (local,)) if priv else local
        m.gates.append((canon, formula(list(m.gates), scope, 3), scope, local, role))

    if rnd.random() < 0.5:
        fes = [f"FE{i}" for i in range(rnd.randint(1, 3))]
        seqs = [f"S{i}" for i in range(rnd.randint(1, 3))]
        branches = {}

        def body(depth, allow_branch):
            ins = []
            for _ in range(rnd.choice([0, 1, 1, 2])):
                ins.append(("collect", formula(m.gates, (), 2)))
            r = rnd.random()
            if depth > 0 and r < 0.5:
                ins.append(("fork", rnd.choice(fes), [body(depth - 1, allow_branch) for _ in range(rnd.randint(1, 3))]))
            elif allow_branch and branches and r < 0.65:
                ins.append(("branch", rnd.choice(list(branches))))
            else:
                ins.append(("sequence", rnd.choice(seqs)))
            return ins

        if rnd.random() < 0.4:
            branches["B0"] = body(1, False)
        m.trees.append(("ET", fes, seqs, branches, body(3, True)))
    return m


def f2x(f):
    t = f[0]
    if t in ("basic-event", "house-event", "gate", "event"):
        return f'<{t} name="{f[-1]}"/>'
    if t == "constant":
        return f'<constant value="{"true" if f[1] else "false"}"/>'
    attr = f' min="{f[2]}"' if t == "atleast" else f' min="{f[2]}" max="{f[3]}"' if t == "cardinality" else ""
    return f"<{t}{attr}>" + "".join(f2x(k) for k in f[1]) + f"</{t}>"


def ins2x(ins):
    out = []
    for i in ins:
        if i[0] == "collect":
            out.append(f"<collect-formula>{f2x(i[1])}</collect-formula>")
        elif i[0] == "fork":
            out.append(f'<fork functional-event="{i[1]}">')
            for k, p in enumerate(i[2]):
                out.append(f'<path state="{["success", "failure", "bypass"][k]}">{ins2x(p)}</path>')
            out.append("</fork>")
        else:
            out.append(f'<{i[0]} name="{i[1]}"/>')
    return "".join(out)


def xml(rnd, m):
    """The model as one or two files' text: fault trees and event trees, and maybe model data apart."""
    def gates_in(scope):
        out = []
        for canon, f, sc, local, role in m.gates:
            if sc == scope:
                r = f' role="{role}"' if role else ""
                out.append(f'<define-gate name="{local}"{r}><label>gate {canon}</label>{f2x(f)}</define-gate>')
        for sub, role in m.comps.items():
            if sub[:-1] == scope:
                r = f' role="{role}"' if role else ""
                out.append(f'<define-component name="{sub[-1]}"{r}>' + "".join(gates_in(sub)) + "</define-component>")
        return out

    trees = ['<?xml version="1.0"?>', "<!-- random -->", "<opsa-mef>"]
    for ft in sorted({g[2][0] for g in m.gates} | {"FT"}):
        trees.append(f'<define-fault-tree name="{ft}">')
        trees += gates_in((ft,))
        if ft == "FT":
            for h, v in m.houses.items():
                trees.append(f'<define-house-event name="{h}"><constant value="{"true" if v else "false"}"/></define-house-event>')
        trees.append("</define-fault-tree>")
    for name, fes, seqs, branches, init in m.trees:
        trees.append(f'<define-initiating-event name="IE" event-tree="{name}"/><define-event-tree name="{name}">')
        trees += [f'<define-functional-event name="{x}"/>' for x in fes]
        trees += [f'<define-sequence name="{x}"><label>sequence {x}</label></define-sequence>' for x in seqs]
        trees += [f'<define-branch name="{b}">{ins2x(body)}</define-branch>' for b, body in branches.items()]
        trees.append(f"<initial-state>{ins2x(init)}</initial-state></define-event-tree>")
    data = ["<model-data>"]
    for b, p in m.basics.items():
        pad = rnd.choice(["", "", " ", "\n  "])
        data.append(f'<define-basic-event name="{b}"><float value="{pad}{float(p)!r}{pad}"/></define-basic-event>')
    data.append("</model-data>")
    if rnd.random() < 0.3:
        return ["\n".join(trees + ["</opsa-mef>"]) + "\n", "\n".join(["<opsa-mef>"] + data + ["</opsa-mef>"]) + "\n"]
    return ["\n".join(trees + data + ["</opsa-mef>"]) + "\n"]


def write(rnd, path, text):
    enc = rnd.choice(["utf-8", "utf-8", "utf-8", "utf-8-sig", "utf-16", "utf-16-be"])
    raw = ("﻿" + text).encode(enc) if enc == "utf-16-be" else text.encode(enc)
    with open(path, "wb") as fh:
        fh.write(raw)


def evf(f, val):
    t = f[0]
    if t in ("basic-event", "house-event", "gate", "event"):
        return val[f[1]]
    if t == "constant":
        return f[1]
    ks = [evf(k, val) for k in f[1]]
    return {"and": lambda: all(ks), "or": lambda: any(ks), "not": lambda: not ks[0],
            "xor": lambda: sum(ks) % 2 == 1, "nand": lambda: not all(ks), "nor": lambda: not any(ks),
            "iff": lambda: ks[0] == ks[1], "imply": lambda: (not ks[0]) or ks[1],
            "atleast": lambda: sum(ks) >= f[2], "cardinality": lambda: f[2] <= sum(ks) <= f[3]}[t]()


def evt(ins, ok, val, branches, out):
    for i in ins:
        if i[0] == "collect":
            ok = ok and evf(i[1], val)
        elif i[0] == "fork":
            for p in i[2]:
                evt(p, ok, val, branches, out)
        elif i[0] == "branch":
            evt(branches[i[1]], ok, val, branches, out)
        else:
            out[i[1]] = out[i[1]] or ok


def check_model(rnd, tmp, idx, solver):
    m = gen(rnd)
    paths = []
    for k, text in enumerate(xml(rnd, m)):
        paths.append(os.path.join(tmp, f"m{idx}-{k}.xml"))
        write(rnd, paths[-1], text)
    pb = {b: F(repr(float(p))) for b, p in m.basics.items()}
    names = [g[0] for g in m.gates] + [s for t in m.trees for s in t[2]]
    want = {n: F(0) for n in names}
    for bits in itertools.product([False, True], repeat=len(pb)):
        w = F(1)
        val = dict(m.houses)
        for b, x in zip(pb, bits):
            w *= pb[b] if x else 1 - pb[b]
            val[b] = x
        for g in m.gates:
            val[g[0]] = evf(g[1], val)
        for _, _, seqs, branches, init in m.trees:
            seq = {s: False for s in seqs}
            evt(init, True, val, branches, seq)
            val.update(seq)
        for n in names:
            if val[n]:
                want[n] += w
    queries = names + ([rnd.choice(list(pb))] if rnd.random() < 0.2 else [])
    for q in queries:
        cnf = os.path.join(tmp, f"m{idx}-{q}.cnf")
        r = run([TOOL, "--top", q, "-o", cnf] + paths)
        if r.returncode != 0:
            raise Fail(f"openpsa-to-wmc failed:\n{r.stderr}")
        got = count(solver, cnf)
        exp = want[q] if q in want else pb[q]
        if got != exp:
            raise Fail(f"P({q}): expected {exp}, got {got}")


def check_chain(rnd, tmp, idx, solver):
    """A gate chain far deeper than any recursion limit."""
    n = rnd.randint(5000, 40000)
    nb = rnd.randint(1, 3)
    pb = [rand_prob(rnd) for _ in range(nb)]
    ops = [rnd.choice(["and", "or", "xor", "nand"]) for _ in range(n)]
    out = ['<opsa-mef><define-fault-tree name="FT">']
    for i in range(n):
        nxt = f'<gate name="g{i + 1}"/>' if i + 1 < n else '<basic-event name="b0"/>'
        out.append(f'<define-gate name="g{i}"><{ops[i]}><basic-event name="b{i % nb}"/>{nxt}</{ops[i]}></define-gate>')
    out.append("</define-fault-tree><model-data>")
    out += [f'<define-basic-event name="b{k}"><float value="{float(p)!r}"/></define-basic-event>' for k, p in enumerate(pb)]
    out.append("</model-data></opsa-mef>")
    path = os.path.join(tmp, f"c{idx}.xml")
    with open(path, "w") as fh:
        fh.write("\n".join(out))
    want = F(0)
    for bits in itertools.product([False, True], repeat=nb):
        v = bits[0]
        for i in reversed(range(n)):
            a = bits[i % nb]
            v = {"and": a and v, "or": a or v, "xor": a != v, "nand": not (a and v)}[ops[i]]
        if v:
            w = F(1)
            for p, x in zip(pb, bits):
                w *= F(repr(float(p))) if x else 1 - F(repr(float(p)))
            want += w
    cnf = os.path.join(tmp, f"c{idx}.cnf")
    r = run([TOOL, "--top", "g0", "-o", cnf, path])
    if r.returncode != 0:
        raise Fail(f"openpsa-to-wmc failed on a chain of {n}:\n{r.stderr}")
    got = count(solver, cnf)
    if got != want:
        raise Fail(f"chain of {n}: expected {want}, got {got}")


def check_mutation(rnd, tmp, idx, solver):
    text = list(xml(rnd, gen(rnd))[0])
    for _ in range(rnd.randint(1, 5)):
        pos = rnd.randrange(len(text) + 1)
        if rnd.random() < 0.5 and text:
            del text[min(pos, len(text) - 1)]
        else:
            text.insert(pos, rnd.choice(['<', '>', '/', '"', '=', ' ', 'a', '1', '<gate name="G9"/>', '</or>', '<and>',
                                         '.', '&#', ';', '<sequence name="S0"/>', '<fork functional-event="FE0">']))
    path = os.path.join(tmp, f"x{idx}.xml")
    write(rnd, path, "".join(text))
    r = run([TOOL, "-o", os.path.join(tmp, "x.cnf"), path])
    if r.returncode not in (0, 1):
        raise Fail(f"mutated input: exit {r.returncode}\n{r.stderr}")


def one(args):
    seed, solver = args
    rnd = random.Random(seed)
    kind = rnd.choices([check_model, check_mutation, check_chain], [15, 5, 1])[0]
    with tempfile.TemporaryDirectory() as tmp:
        try:
            kind(rnd, tmp, seed, solver)
            return None
        except Exception as e:
            os.makedirs(OUT, exist_ok=True)
            for fn in os.listdir(tmp):
                if fn.endswith(".xml"):
                    with open(os.path.join(tmp, fn), "rb") as src, open(os.path.join(OUT, f"{seed}-{fn}"), "wb") as dst:
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
