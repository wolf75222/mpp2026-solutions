# 21st Parallel Programming Marathon (SSCAD 2026)

Team `root@romeo:~#_rm_-rf_/bin` (Remote). Problem set: [problemset.pdf](problemset.pdf).

| Problem | Speedup | t_ref | t |
|---|---|---|---|
| [A](A/) | 4115.23 | 62 s | 15.1 ms |
| [B](B/) | 137.50 | 134 s | 0.975 s |
| [C](C/) | 4410.95 | 24 s | 5.4 ms |
| **Total** | **8663.68** | | |

Judge: AMD EPYC 7713 (128 cores / 256 threads), GCC 9.4.

`submissions/` holds the zips submitted to BOCA.

```sh
cd A && make all && ./A < input > output
```
