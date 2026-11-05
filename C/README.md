<div align="center">

# C · Quadtree range query

**×4,410.95** · 24 s → 5.4 ms

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="../docs/problem_C_dark.svg">
  <img alt="Problem C: speedup over the contest" src="../docs/problem_C.svg" width="760">
</picture>

</div>

## Problem

The program generates n points with glibc's `rand()` (`srand(42)`) and stores them in a quadtree with at most 4 points per leaf. It then prints the points of a rectangular query as `(%.1lf,%.1lf)`.

Three traps:

- the reference `intercept()` only tests corners, so it prunes nodes that do overlap the query, and those points must be lost too;
- FMA contraction of `x*W + x_min` changes 10,262,024 of 2·10⁷ coordinates;
- `%.1lf` rounds the exact binary value half to even and prints `-0.0`.

The reference builds the full tree (0.89 µs per point at 10⁷, 121 bytes per point) and keeps its results in a Θ(m²) list.

## Optimizations

<div align="center">

| Step (ROMEO, example query, 10⁷ points) | Time | Speedup |
|:--|--:|--:|
| Reference | 8.861 s | ×1 |
| Tail pointer, arena, parallel generation | 5.791 s | ×1.53 |
| Query-limited construction, integer generation, thread caps | 6.3 ms | ×1,406 |

</div>

- **Query-limited construction**: a node is built only if `intercept()` succeeds on its whole path from the root, so the false prunings are reproduced by construction.
- **Exact `rand()` emulation**
  - glibc TYPE_3 generator, checked against glibc on 2·10⁸ values;
  - exact jump-ahead (x^j mod x³¹ − x²⁸ − 1, ≈ 15 µs per jump): 16 interleaved substreams, generated in parallel;
  - integer thresholds on `rand() % 10⁶`: 12.5 → 1.0–1.2 ns per point per thread.
- **AVX2 assembly kernel** for generation and filtering (`p1asm.S`).
- **Exact `%.1f` formatter**, checked against `snprintf` on 29.8M values.
- **Thread count per phase**: a thread costs ≈ 37 µs to start, so each phase sizes its team from its work.
- **libc-free fast path**
  - static binary (`-nostdlib`), own `_start`, raw system calls, `clone` threads, futex barriers;
  - removes the dynamic loader and glibc start-up;
  - inputs outside the proven-exact path go through `execve` to `qtd`, the same code linked against glibc.
- **`unset OMP_NUM_THREADS`** in the script: the judge exports `OMP_NUM_THREADS=1`.

<div align="center">

| n (ROMEO) | glibc build | libc-free |
|--:|--:|--:|
| 10⁶ | 2.16 ms | 1.76 ms |
| 10⁷ | 2.89 ms | 2.34 ms |
| 10⁸ | 5.50 ms | 4.35 ms |
| 10⁹ | 15.33 ms | 12.35 ms |

</div>

## On BOCA

<div align="center">

| Change | Zip | Speedup |
|:--|:--|--:|
| First submission | `C` | ×1,023 |
| `unset OMP_NUM_THREADS` | `C_unset` | ×3,703 |
| AVX2 assembly kernel | `C_asm` | ×3,859 |
| libc-free static binary | `C_free_s` | ×4,411 |

</div>

<details>
<summary><b>Tried and dropped</b></summary>
<br>

| Idea | Result |
|:--|:--|
| Futex waits instead of spinning | ×384 |
| One thread | ×984 |
| Fortran core | ×1,956 |
| A parallel region over the whole machine | ≈ 6 ms slower on ROMEO |

</details>

## Validation

Acceptance: 33,441 runs on 8,610 inputs, no mismatch.

## Files

<div align="center">

| File | Content |
|:--|:--|
| `qc.h` | both programs |
| `qf.c` | defines `QF`: libc-free `qt` |
| `qt.c` | glibc fallback `qtd` |
| `p1asm.S` | AVX2 generation kernel |
| `Makefile`, `C` | build and run script |

`make all && ./C < input > output`

[← Back](../README.md) · [Full log](../docs/LOG.md#c-quadtree-range-query)

</div>
