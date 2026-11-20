# TODO

Roughly in order: each step unlocks a larger share of real MEF models.

## 1. Model structure and names

- [x] Scoped names: `define-fault-tree` / `define-component` nesting, `role="private|public"`,
      references by path (`FT.Comp.G`).
- [x] Check reference types.
- [x] Several input files merged into one model.
- [ ] Maybe a lenient mode for converters that make gates private and then reference them
      by bare name from other fault trees (3 generic-PWR files do; strictly an error).
- [ ] Ignore `<attributes>` everywhere, not just as a formula sibling.

## 2. Parameters and expressions

This is what most real basic events use instead of `<float>`.

- [ ] `define-parameter` (in fault trees and `model-data`) and `<parameter name>` references,
      with cycle detection. `unit` can be ignored at first.
- [ ] `<system-mission-time/>`, set by `--mission-time T`.
- [ ] Exact operations on rationals: `int`, `float`, `bool`, `neg`, `add`, `sub`, `mul`, `div`,
      `abs`, `min`, `max`, `mean`, `ceil`, `floor`, `mod`, `pow` with integer exponent,
      comparisons (`eq`, `df`, `lt`, `gt`, `leq`, `geq`), `not`/`and`/`or`, `ite`, `switch`/`case`.
- [ ] Irrational operations: `exp`, `log`, `log10`, `sqrt`, `pow` with a fractional exponent,
      trig, `pi`, and the built-ins `exponential` (1−e^(−λt)), `GLM`, `Weibull`,
      `periodic-test` (4-, 5- and 11-argument forms).
      Decide the policy: evaluate with MPFR at `--precision BITS`, turn the result into the
      exact rational of that binary value, and say on stderr and in a `c` line that the count
      is exact only for the rounded weights. Without the flag, refuse instead of silently
      rounding, so the "exact" claim keeps meaning something.
- [ ] Deviates for point estimates: `uniform-deviate`, `normal-deviate`, `lognormal-deviate`
      (mean / error factor / level), `gamma-deviate`, `beta-deviate`, `histogram` → their mean.
- [ ] Check that every evaluated basic-event probability lies in [0,1], with the location of the
      offending expression.
- [ ] `define-extern-library` / `extern-function`: keep rejecting with a clear message.

## 3. Common-cause failure groups

- [ ] `define-CCF-group` with `members`, `distribution`, `factors`/`factor level`, and the
      `beta-factor`, `MGL`, `alpha-factor` and `phi-factor` models. Expand into one basic event
      per member subset with the model's Q_k, and replace each member by the OR of the subset
      events that contain it. All four models stay rational, so this is exact.
- [ ] Name the generated events predictably (e.g. `[A B C]`) so they can be queried.

## 4. Event trees

- [x] `define-initiating-event` (accepted, ignored), `define-event-tree`, `define-functional-event`,
      `define-sequence`, `define-branch`, `initial-state`, `fork`/`path`, `branch` and
      `sequence` targets, `collect-formula`.
- [ ] `event-tree` transfers, and instructions inside `define-sequence`.
- [ ] Instructions: `collect-expression`, `set-house-event`, `if`/`then`/`else`,
      `block`, `define-rule` / `rule`.
- [x] Each sequence becomes a gate: the OR over the paths that reach it of the AND of the
      collected formulas, success branches as exact negations. Paths are disjoint, so the
      count is exact without any MCUB correction.
- [ ] `set-house-event` changes the gates below it, so encode a gate per (gate, house-event
      assignment); key the memo on the assignment and let structural hashing share the rest.
- [ ] `collect-expression` factors (initiating-event frequency etc.) are not probabilities:
      write the product as a `c` line per sequence (or a `--sequence` option that prints it)
      rather than folding them into weights.
- [ ] `define-consequence` / `define-consequence-group`: one gate per consequence (OR of its
      sequences).
- [x] `--tops` lists sequences (and should list consequences once they exist).

## 5. Less common MEF parts

- [ ] `define-substitution` (delete terms, recovery rules, exchange events). These are defined
      on cut sets, not on Boolean functions; decide whether there is an exact Boolean meaning
      (e.g. delete terms as P(TOP ∧ ¬H)) or keep rejecting them.
- [ ] `define-alignment` / `define-phase`: per-phase house-event settings, result weighted by
      `time-fraction`.

## 6. Encoding and CLI

- [x] `--top G`: write only the cone of influence of `G`, and project onto the basic events
      (ganak went from >300 s to 0.4 s on a 24k-gate chain).
- [ ] `--house NAME=true|false` to override house events.
- [ ] `--house-vars`: keep house events as variables of weight 1/1 so one compilation answers
      every house-event setting by assumption (the unconditioned count is then meaningless).
- [ ] Importance measures from one compilation by assumptions on each basic event:
      Birnbaum, criticality, Fussell–Vesely, RAW, RRW. Probably a script driving
      `ganak --compile` + `bdnnf count --assume`.
- [ ] Uncertainty analysis: sample the deviates (`--sample N --seed S`) and reweight one
      compiled d-DNNF per sample instead of recounting.

## 7. XML reader

- [x] Numeric character references, UTF-8 BOM, UTF-16 with a BOM.
- [ ] CDATA, a DOCTYPE with an internal subset (`<!DOCTYPE x [ ... ]>` currently stops at
      the first `>`).

## 8. Testing

- [ ] Regression corpus: the MEF inputs in SCRAM's repository (Aralia fault trees, CCF and
      event-tree examples). Compare against `scram --bdd --probability`, which is exact up to
      floating point.
- [ ] Validate generated fuzz models against the MEF RELAX NG schema.
- [x] `scripts/fuzz.py`: scopes and paths, event trees, several files, UTF-16, deep chains.
- [ ] Extend it with parameters and CCF groups once they exist.
