# A : écoulement et bassins versants

Directions D8, accumulation de flux et bassins versants sur un MNT lu sur stdin. Sortie texte identique octet pour octet à la référence C++.

**Speedup 4115** : ≈ 15,1 ms au juge contre ≈ 62 s pour la référence.

## Optimisations

- **E/S exactes** : 77 % du temps de la référence est de l'iostream.
  - Analyseur rapide : chemin de Clinger, exact pour une mantisse < 2²⁴ et |e10| ≤ 10, sinon repli `strtof`.
  - `%g` de la glibc reproduit, vérifié sur les 2³² motifs de float.
  - stdin lu par `mmap`, sortie écrite en gros blocs.
- **Accumulation en O(V)** au lieu des balayages répétés : degré entrant, puis marches où le dernier contributeur arrivé poursuit.
- **Bassins** numérotés par le rang du plus petit indice, comme la référence.
- **Passe fusionnée** : lecture et formatage du terrain en une seule passe, sans copie intermédiaire.
- **`dtm`, jusqu'à 4 M de cellules** :
  - pthreads épinglés, créés en arbre, barrières futex ;
  - pas de runtime OpenMP, libstdc++ statique ;
  - un seul thread pour les petites grilles.
- **`dtm_par`, au-delà de 4 M de cellules** : programme pthreads séparé, lancé par `execv`. Environ 0,40× le temps de la version OpenMP précédente sur ROMEO.

## Validation

Acceptance indépendante : 19 567 exécutions pour `dtm`, 377/377 cas au-delà de 4 M pour `dtm_par`, cas dégénérés compris.

## Fichiers

`all.h` contient les deux programmes : `dtm.cpp` définit `A_PART_DTM` avant de l'inclure, `dtm_par.cpp` l'inclut tel quel. Ce découpage vient de la limite d'upload de BOCA (≈ 32 Ko). Ici, `all.h` est réindenté ; sous GCC 9.4, il donne les mêmes binaires que le fichier soumis (`../submissions/A_ys_nc.zip`).
