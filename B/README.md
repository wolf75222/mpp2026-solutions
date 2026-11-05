<div align="center">

# B · 3D wave propagation

**137.50** · 134 s → 0.975 s

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="../docs/problem_B_dark.svg">
  <img alt="Problem B: BOCA speedup over the contest" src="../docs/problem_B.svg" width="760">
</picture>

</div>

### Optimizations

- Barrier-free temporal pipeline: (step, band) tasks with point-to-point acquire/release flags
- Band groups per L3
- AVX2 assembly kernels (`kern.S`), no FMA
- SMT, active wait, `MADV_POPULATE_WRITE`
- Flags: provided Makefile plus `-fopenmp`

### Files

<div align="center">

| File | Content |
|:--|:--|
| `pulse.c` | pipeline, scheduling, I/O |
| `kern.S` | AVX2 kernels |
| `Makefile`, `B` | build and run script |

[← Back](../README.md) · [Details in the log](../docs/LOG.md#b-3d-wave-propagation)

</div>
