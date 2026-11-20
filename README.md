# openpsa-to-wmc

Exact probabilities for probabilistic safety assessment (PSA) models, via
weighted model counting.

[Open-PSA](https://open-psa.github.io/) is an open initiative of the PSA
community; its [Model Exchange Format](https://open-psa.github.io/mef/) (MEF)
is the XML format in which fault trees, event trees and their data are passed
between PSA tools. `openpsa-to-wmc` reads an MEF model and writes a weighted
CNF in the [model counting competition](https://mccompetition.org/) format.
Any weighted model counter then computes the probability of a gate, such as
the top event of a fault tree, exactly: no cut-set truncation, no rare-event
approximation, and negations (success branches) handled exactly.

## Building

Needs a C++20 compiler, CMake and GMP.

```shell
mkdir build && cd build
cmake ..
make -j$(nproc)
```

## Usage

```shell
./openpsa-to-wmc --tops model.xml               # the top gates and event-tree sequences
./openpsa-to-wmc --top TOP model.xml > top.cnf  # weighted count = P(TOP)
ganak --mode 1 --coverdrop 0 top.cnf             # or any other weighted model counter
./openpsa-to-wmc --top S1 tree.xml data.xml     # several files are read as one model
```

The tests and the fuzzer use [ganak](https://github.com/meelgroup/ganak) as
the counter. They look for it at `../ganak/build/ganak` (next to this
repository); elsewhere, set `GANAK`:

```shell
GANAK=/path/to/ganak tests/run.sh
GANAK=/path/to/ganak scripts/fuzz.py
```

Run ganak with `--coverdrop 0`. Its cover-drop preprocessing does not help on
these CNFs and costs about a minute even on small ones; without it, ganak
solves most real-world models in seconds:

| Model | Variables / clauses | Time |
|---|---|---|
| Aralia `jbd9601` | 1088 / 1881 | 17.4 s |
| Aralia `edfpa14o` | 599 / 1225 | 13.2 s |
| Aralia `isp9604` | 441 / 713 | 2.6 s |
| Aralia `edfpa15p` (SCRAM's copy) | 220 / 531 | 2.3 s |
| Aralia `elf9601` | 532 / 1152 | 2.2 s |
| Generic PWR `EQK-BIN3` Grp-1, gate `FT44.G31` | 637 / 1241 | 2.1 s |
| Aralia `das9206` | 323 / 626 | 1.9 s |
| Generic PWR `HCN-BIN1` Grp-1, gate `FT56.G251` | 643 / 1248 | 1.6 s |
| Aralia `edf9205` | 413 / 736 | 0.6 s |
| Generic PWR `FLI-4160VACB` Grp-1, gate `FT26.G138` | 556 / 1063 | 0.4 s |
| Generic PWR `FLI-4160VACA` Grp-1, sequence `S1336` | 637 / 1235 | 0.4 s |
| Aralia `das9203` | 110 / 175 | 0.1 s |

Exact rational results, one core each, 5-minute timeout, from a random sample
of 32 benchmark models. Of the sample, Aralia `edf9203` and 11 of the 12 large
synthetic fault trees (3k–147k variables) timed out.

With `--top`, only what the top depends on is written, plus its unit clause,
and `c p show` lists just the basic events: every gate is a function of them,
so the count is the same, and counters are much faster with the smaller
projection. Without `--top` there is no unit clause, the weighted count is 1,
and every basic event and gate is a variable named in a `c <var> <name>` line
and listed in `c p show`, so a counter or d-DNNF compiler that takes
assumptions (for example `ganak --compile` and then `bdnnf count --assume`)
can answer many queries from one compilation.

The encoding: each basic event with probability p is a variable with weights
p and 1−p; each gate is defined by its formula in both directions (Tseitin).
Probabilities are kept exact (`1e-3` is 1/1000).

## What is supported

- Fault trees: `define-gate` with `and`, `or`, `not`, `xor`, `iff`, `imply`,
  `nand`, `nor`, `atleast` and `cardinality`; `define-basic-event` with a
  `float` probability; `define-house-event` with a `constant`.
- Names: `define-fault-tree` and `define-component` scopes with
  `role="private|public"`; references by bare name or path (`FT.Comp.G`).
  Private events are called by their full path, e.g. in `--top`.
- Event trees: `define-event-tree` with functional events, sequences,
  branches, `fork`/`path` and `collect-formula`. Each sequence is a gate, the
  OR over the paths that reach it of what they collect.
- Several input files, UTF-8 or UTF-16 with a BOM.

Parameters and expressions, common-cause groups, substitutions and the other
event-tree instructions are reported as not supported yet; see `TODO.md`.

## Testing

```shell
tests/run.sh                       # small models with known answers
scripts/fuzz.py --num 500          # random models against brute force
```

To use a counter other than ganak, pass it as `tests/run.sh <command>` or
`scripts/fuzz.py --solver "<command>"`. It must print its exact result as
`c s exact arb frac N` (or `int N`).
