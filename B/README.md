# B : propagation d'onde 3D (Gauss-Seidel)

Équation d'onde sur une grille N³ (N ≤ 480, T ≤ 160) : laplacien d'ordre 12, un balayage de Gauss-Seidel en place par pas, source de Ricker. La sortie doit être bit-exacte, et `-ffp-contract=off` est imposé.

**Speedup 137,5** : ≈ 0,975 s au juge contre ≈ 134 s pour la référence.

## Optimisations

- **Pipeline temporel sans barrière** :
  - les tâches (pas, bande) s'enchaînent sur tous les pas ;
  - chaque tâche attend trois drapeaux point à point (acquire/release) ;
  - les schémas synchronisés à chaque pas plafonnent vers 13× ; ce pipeline lève ce plafond ;
  - chaque point est calculé avec la même expression flottante que la référence, d'où les mêmes bits.
- **Groupes de bandes par L3** : chaque groupe a son propre compteur et son premier contact mémoire.
- **Noyaux AVX2 en assembleur** (`kern.S`) : environ −13 % au juge. Aucune FMA : multiplications et additions restent séparées, comme sous `-ffp-contract=off`.
- **Réglages d'exécution** :
  - SMT utilisé (`OMP_PLACES=threads`), attente active ;
  - mémoire préparée à l'avance (`MADV_POPULATE_WRITE`) et coûts fixes réduits.
- **Flags** : ceux du Makefile fourni, plus `-fopenmp`.

Écartés après mesure : le GPU (création du contexte CUDA trop coûteuse au juge) et MPI.

## Validation

Acceptance : 782/782 champs exacts.
