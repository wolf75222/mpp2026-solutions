# 21st Parallel Programming Marathon (SSCAD 2026)

Team `root@romeo:~#_rm_-rf_/bin` (Remote): **#1**. Problem set: [problemset.pdf](problemset.pdf).

| Problem | Speedup | t_ref | t |
|---|---|---|---|
| [A](A/) | 4115.23 | 62 s | 15.1 ms |
| [B](B/) | 137.50 | 134 s | 0.975 s |
| [C](C/) | 4410.95 | 24 s | 5.4 ms |
| **Total** | **8663.68** | | |

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/boca_scores_dark.svg">
  <img alt="BOCA speedup per problem over the contest" src="docs/boca_scores.svg">
</picture>

Judge: AMD EPYC 7713 (128 cores / 256 threads), GCC 9.4.

Steps, measurements and BOCA history: [LOG.md](LOG.md). `submissions/` holds the zips submitted to BOCA.

```sh
cd A && make all && ./A < input > output
```
