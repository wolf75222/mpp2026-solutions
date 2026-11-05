# C: quadtree range query

The program generates n points with glibc's `rand()` (`srand(42)`) and stores them in a quadtree (at most 4 points per leaf). It then prints the points of a rectangular query as `(%.1lf,%.1lf)`. The reference `intercept()` only tests corners; its false prunings must be reproduced.

**Speedup 4411**: ≈ 5.4 ms on the judge vs ≈ 24 s for the reference.

## Optimizations

- **Query-limited construction**:
  - only nodes whose `intercept()` succeeds all the way from the root are built;
  - the reference's false prunings are therefore reproduced by construction;
  - the work reduces to generation plus O(m · depth).
- **Exact `rand()` emulation**:
  - glibc TYPE_3 generator;
  - exact jump-ahead (x^j mod x³¹ − x²⁸ − 1), giving 16 interleaved substreams and parallel generation;
  - integer filtering on `rand() % 10⁶`.
- **Hand-written AVX2 kernel** for the generation and filtering phase (`p1asm.S`).
- **Exact `%.1f` formatter**: round-half-even, `-0.0` included.
- **Thread count** sized per phase from the work, since creating a thread costs about 37 µs.
- **libc-free fast path**:
  - static binary (`-nostdlib`), own `_start`, raw system calls;
  - `clone` threads, futex barriers;
  - 15–21% faster than the same code linked against glibc.
  - Any input outside the proven-exact path is handed by `execve` to `qtd`, the same code linked against glibc.
- **Script**: `unset OMP_NUM_THREADS`, because the judge exports `OMP_NUM_THREADS=1`.

## Validation

Acceptance: 33,441 runs on 8,610 inputs, no mismatch.

## Files

`qc.h` holds both programs: `qf.c` defines `QF` (libc-free path, binary `qt`), and `qt.c` builds the `qtd` fallback. This layout comes from BOCA's upload limit (≈ 32 KB).
