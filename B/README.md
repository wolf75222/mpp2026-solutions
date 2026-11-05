# B: 3D wave propagation (Gauss-Seidel)

Wave equation on an N³ grid (N ≤ 480, T ≤ 160): 12th-order Laplacian, one in-place Gauss-Seidel sweep per step, Ricker source. The output must be bit-exact, and `-ffp-contract=off` is mandatory.

**Speedup 137.5**: ≈ 0.975 s on the judge vs ≈ 134 s for the reference.

## Optimizations

- **Barrier-free temporal pipeline**:
  - (step, band) tasks chain across all steps;
  - each task waits on three point-to-point flags (acquire/release);
  - schemes that synchronize at every step cap at about 13×; this pipeline removes that cap;
  - every point is computed with the reference's floating-point expression, so the bits are identical.
- **Band groups per L3**: each group has its own counter and first-touch memory.
- **Hand-written AVX2 kernels** (`kern.S`): about −13% on the judge. No FMA: multiplies and adds stay separate, as under `-ffp-contract=off`.
- **Runtime settings**:
  - SMT used (`OMP_PLACES=threads`), active waiting;
  - memory prepared up front (`MADV_POPULATE_WRITE`), reduced fixed costs.
- **Flags**: those of the provided Makefile, plus `-fopenmp`.

Rejected after measurement: GPU (CUDA context creation too costly on the judge) and MPI.

## Validation

Acceptance: 782/782 fields exact.
