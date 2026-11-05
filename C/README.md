# C : requête dans un quadtree

Le programme génère n points avec le `rand()` de la glibc (`srand(42)`) et les range dans un quadtree (au plus 4 points par feuille). Il imprime ensuite les points d'une requête rectangulaire au format `(%.1lf,%.1lf)`. Le `intercept()` de la référence ne teste que les coins ; ses faux élagages doivent être reproduits.

**Speedup 4411** : ≈ 5,4 ms au juge contre ≈ 24 s pour la référence.

## Optimisations

- **Construction limitée à la requête** :
  - seuls les nœuds dont `intercept()` réussit depuis la racine sont construits ;
  - les faux élagages de la référence sont donc reproduits par construction ;
  - le travail se réduit à la génération plus O(m · profondeur).
- **Émulation exacte de `rand()`** :
  - générateur glibc TYPE_3 ;
  - saut en avant exact (x^j mod x³¹ − x²⁸ − 1), d'où 16 sous-flux entrelacés et une génération parallèle ;
  - filtrage en entiers sur `rand() % 10⁶`.
- **Noyau AVX2 en assembleur** pour la phase de génération et de filtrage (`p1asm.S`).
- **Formateur `%.1f` exact** : arrondi au pair, `-0.0` compris.
- **Nombre de threads** dimensionné par phase selon le travail, la création d'un thread coûtant environ 37 µs.
- **Chemin rapide sans libc** :
  - binaire statique (`-nostdlib`), `_start` propre, appels système directs ;
  - threads `clone`, barrières futex ;
  - gain de −15 à −21 % par rapport à la même version liée à la glibc.
  - Toute entrée hors du chemin prouvé exact repart par `execve` vers `qtd`, le même code lié à la glibc.
- **Script** : `unset OMP_NUM_THREADS`, car le juge exporte `OMP_NUM_THREADS=1`.

## Validation

Acceptance : 33 441 exécutions sur 8 610 entrées, sans écart.

## Fichiers

`qc.h` contient les deux programmes : `qf.c` définit `QF` (chemin sans libc, binaire `qt`), `qt.c` donne le repli `qtd`. Ce découpage vient de la limite d'upload de BOCA (≈ 32 Ko).
