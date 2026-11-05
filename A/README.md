<div align="center">

# A · Terrain flow and watersheds

**4,115.23** · 62 s → 15.1 ms

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="../docs/problem_A_dark.svg">
  <img alt="Problem A: BOCA speedup over the contest" src="../docs/problem_A.svg" width="760">
</picture>

</div>

### Optimizations

- Fast parser (Clinger fast path) and `%g` formatter
- `mmap` input, block writes
- O(V) flow accumulation
- Parsing and terrain formatting fused in one pass
- Up to 4M cells (`dtm`): pinned pthreads, futex barriers, no OpenMP runtime, static libstdc++
- Above 4M cells (`dtm_par`): separate pthread program, started with `execv`

### Files

<div align="center">

| File | Content |
|:--|:--|
| `all.h` | both programs |
| `dtm.cpp` | defines `A_PART_DTM`, includes `all.h` |
| `dtm_par.cpp` | includes `all.h` |
| `fastio.h` | parser and formatters |
| `Makefile`, `A` | build and run script |

[← Back](../README.md) · [Details in the log](../docs/LOG.md#a-terrain-flow-and-watersheds)

</div>
