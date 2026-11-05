# 21st Parallel Programming Marathon (SSCAD 2026)

Final solutions of team `root@romeo:~#_rm_-rf_/bin` (Remote). Problem statements: [problemset.pdf](problemset.pdf).

| Problem | BOCA speedup | t_ref | Judge time |
|---|---|---|---|
| [A](A/) | 4115.23 | ≈ 62 s | ≈ 15.1 ms |
| [B](B/) | 137.50 | ≈ 134 s | ≈ 0.975 s |
| [C](C/) | 4410.95 | ≈ 24 s | ≈ 5.4 ms |
| **Total** | **8663.68** | | |

Highest total on the provisional scoreboard at the end of the contest (runner-up: 4285.75).

Judge machine: AMD EPYC 7713 (128 cores / 256 threads), GCC 9.4. Score = t_ref / t. Output must match the reference byte for byte.

## Contents

- `A/`, `B/`, `C/`: sources of the last submitted versions, each with a README.
- `submissions/`: the exact zips sent to BOCA (`A_ys_nc` 1872dbf9, `B_fix` 524328ab, `C_free_s` afea1efc).

## Usage

```sh
cd A && make all && ./A < input > output
```
