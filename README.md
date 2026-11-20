# openpsa-to-wmc

Exact probabilities for probabilistic safety assessment (PSA) models, by
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
./openpsa-to-wmc --tops model.xml               # the top gates
./openpsa-to-wmc --top TOP model.xml > top.cnf  # weighted count = P(TOP)
ganak --mode 1 top.cnf                          # or any other weighted model counter
```

Without `--top` the CNF has no unit clause, and the weighted count is 1. Every
basic event and gate is a variable named in a `c <var> <name>` line, all of
them in the `c p show` set, so a counter or d-DNNF compiler that takes
assumptions (for example `ganak --compile` and then `bdnnf count --assume`)
can answer many queries from one compilation.

The encoding: each basic event with probability p is a variable with weights
p and 1−p; each gate is defined by its formula in both directions (Tseitin),
so every gate is a function of the basic events and the count needs no
projection. Probabilities are kept exact (`1e-3` is 1/1000).

## What is supported

Fault trees: `define-gate` with `and`, `or`, `not`, `xor`, `iff`, `imply`,
`nand`, `nor`, `atleast` and `cardinality`; references to gates, basic events
and house events; `define-basic-event` with a `float` probability;
`define-house-event` with a `constant`. Anything else (parameters and
expressions, common-cause groups, event trees, substitutions) is reported as
not supported yet.

## Testing

```shell
tests/run.sh                       # small models with known answers
scripts/fuzz.py --num 500          # random fault trees against brute force
```

Both use `../ganak/build/ganak --mode 1` by default; pass another counter as
`tests/run.sh <command>` or `scripts/fuzz.py --solver "<command>"`. The counter
must print its exact result as `c s exact arb frac N` (or `int N`).
