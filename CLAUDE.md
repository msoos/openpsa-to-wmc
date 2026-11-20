# Benchmarks

Open-PSA MEF benchmark models are in /home/soos/media/psa:

- `aralia-fault-tree-dataset/data/openpsa/`: the 43 Aralia fault trees
- `generic-pwr-model/data/openpsa/`: generic PWR model (event trees, needs parameters/CCF)
- `scram/input/`, `scram/tests/input/`: SCRAM's models; `scram --bdd` is the reference
- `synthetic-models/models/`, `model-benchmarks/models/openpsa/`: synthetic fault trees for scaling
  (skip the benchexec/xfta config `.xml` files there)
