# B: 3D wave propagation

Speedup 137.5 (0.975 s vs 134 s).

- Barrier-free temporal pipeline: (step, band) tasks with point-to-point acquire/release flags
- Band groups per L3
- AVX2 assembly kernels (`kern.S`), no FMA
- SMT, active wait, `MADV_POPULATE_WRITE`
- Flags: provided Makefile plus `-fopenmp`
