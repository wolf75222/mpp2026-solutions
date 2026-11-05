# 21st Parallel Programming Marathon (SSCAD 2026)

Solutions finales de l'équipe `root@romeo:~#_rm_-rf_/bin` (Remote). Énoncés : [problemset.pdf](problemset.pdf).

| Problème | Speedup BOCA | t_ref | t juge |
|---|---|---|---|
| [A](A/) | 4115,23 | ≈ 62 s | ≈ 15,1 ms |
| [B](B/) | 137,50 | ≈ 134 s | ≈ 0,975 s |
| [C](C/) | 4410,95 | ≈ 24 s | ≈ 5,4 ms |
| **Total** | **8663,68** | | |

Total le plus élevé du classement provisoire à la fin du concours (deuxième : 4285,75).

Machine du juge : AMD EPYC 7713 (128 cœurs / 256 threads), GCC 9.4. Score = t_ref / t, sortie identique octet pour octet à la référence.

## Contenu

- `A/`, `B/`, `C/` : sources des versions soumises en dernier, avec un README par problème.
- `submissions/` : les zips exacts envoyés à BOCA (`A_ys_nc` 1872dbf9, `B_fix` 524328ab, `C_free_s` afea1efc).

## Utilisation

```sh
cd A && make all && ./A < input > output
```
