# A: terrain flow and watersheds

Speedup 4115 (15.1 ms vs 62 s).

- Fast parser (Clinger fast path) and `%g` formatter
- `mmap` input, block writes
- O(V) flow accumulation
- Parsing and terrain formatting fused in one pass
- Up to 4M cells (`dtm`): pinned pthreads, futex barriers, no OpenMP runtime, static libstdc++
- Above 4M cells (`dtm_par`): separate pthread program, started with `execv`

`all.h` holds both programs; `dtm.cpp` defines `A_PART_DTM`.
