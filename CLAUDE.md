# Benchmarks

Open-PSA MEF benchmark models are in /home/soos/media/psa:

- `aralia-fault-tree-dataset/data/openpsa/`: the 43 Aralia fault trees
- `generic-pwr-model/data/openpsa/`: generic PWR model (simple event trees over private gates)
- `scram/input/`, `scram/tests/input/`: SCRAM's models; `scram --bdd` is the reference.
  Some need a second file: `X-basic-events.xml`, `TransTest/trans_model_data.xml`
- `synthetic-models/models/`, `model-benchmarks/models/openpsa/`: synthetic fault trees for scaling
  (skip the benchexec/xfta config `.xml` files; `model-benchmarks` is Git LFS, needs `git lfs pull`)

# ganak

Always run ganak with `--coverdrop 0` on these CNFs. With the default, Arjun's cover-drop
preprocessing burns its whole budget (~60 s, 582k conflicts on Aralia isp9604), gives up
and removes nothing; counting itself takes ~2 s. Same count either way. Measured
2026-11-20: 11 instances went from 56-106 s to 0.4-17 s.
