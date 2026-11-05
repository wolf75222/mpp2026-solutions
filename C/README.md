# C: quadtree range query

Speedup 4411 (5.4 ms vs 24 s).

- Only the nodes reached by the query are built
- glibc `rand()` emulation with exact jump-ahead: 16 parallel substreams
- AVX2 assembly kernel for point generation (`p1asm.S`)
- `%.1f` formatter
- Thread count chosen per phase
- Static libc-free binary (raw syscalls, `clone`, futex), with `qtd` (glibc build) as fallback via `execve`
- `unset OMP_NUM_THREADS` in the script

`qc.h` holds both programs; `qf.c` defines `QF`.
