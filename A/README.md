# A: terrain flow and watersheds

D8 flow directions, flow accumulation and watersheds on a DEM read from stdin. The text output matches the C++ reference byte for byte.

**Speedup 4115**: ≈ 15.1 ms on the judge vs ≈ 62 s for the reference.

## Optimizations

- **Exact I/O**: iostream accounts for 77% of the reference time.
  - Fast parser: Clinger's fast path, exact for a mantissa < 2²⁴ and |e10| ≤ 10, falling back to `strtof` otherwise.
  - glibc's `%g` reproduced, checked against all 2³² float bit patterns.
  - stdin read through `mmap`, output written in large blocks.
- **O(V) accumulation** instead of repeated sweeps: in-degrees, then walks where the last arriving contributor carries on.
- **Watersheds** numbered by the rank of their smallest index, as in the reference.
- **Fused pass**: parsing and terrain formatting in one pass, with no intermediate copy.
- **`dtm`, up to 4M cells**:
  - pinned pthreads created as a tree, futex barriers;
  - no OpenMP runtime, static libstdc++;
  - a single thread for small grids.
- **`dtm_par`, above 4M cells**: a separate pthread program started with `execv`. About 0.40× the time of the previous OpenMP version on ROMEO.

## Validation

Independent acceptance: 19,567 runs for `dtm`, 377/377 cases above 4M cells for `dtm_par`, degenerate cases included.

## Files

`all.h` holds both programs: `dtm.cpp` defines `A_PART_DTM` before including it, and `dtm_par.cpp` includes it as is. This layout comes from BOCA's upload limit (≈ 32 KB). Here `all.h` is re-indented; under GCC 9.4 it builds the same binaries as the submitted file (`../submissions/A_ys_nc.zip`).
