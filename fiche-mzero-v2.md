# Fiche de reprise : projet « M-Zero » (mode 7 sur Sega 32X)

## Le projet

**M-Zero** (« M » pour Mars, nom de code de la 32X) : l'équivalent du
mode 7 de la Super Nintendo sur la 32X. Un plan de 1024 × 1024 pixels vu
en perspective, comme le sol de F-Zero ou de Mario Kart. La 32X se
conduit comme une carte vidéo : le 68000 donne la caméra, les deux SH2
dessinent le sol, la Mega Drive dessine le ciel au-dessus de l'horizon.

- Dépôt : https://github.com/olivier256/M-Zero.git (parti de SuperVDP,
  dont il garde l'historique).
- Patch 0001 livré et appliqué : « M-Zero : mode 7 en C, démo sur un
  morceau de Big Blue ».

## Comment je travaille

- **Vouvoyez-moi.** Commentaires du code en français, C lisible.
- Livraison par **patchs** que j'applique avec `git am`. Chaque patch
  ajoute une section à la fin de `VERSION.md` (ce que fait la version) et
  vient avec un **message de commit proposé**.
- **Mesurer avant d'optimiser.** Vérifier chaque changement : référence
  au pixel près, tests exhaustifs, compteurs d'écritures.
- **Assembleur** seulement si le gain est notable. Il garde la même
  interface que la version C, s'active par une option de compilation, et
  il est vérifié contre le C, qui reste la référence et la version de
  l'hôte. **Soumettez-moi l'assembleur avant ou pendant le
  développement** : j'aime proposer des améliorations.
- Pas de Docker chez moi : les outils (banc SH2) tournent dans votre
  session. `build-tools.sh` y prend environ 15 minutes.
- Je commence par `make test`, puis `VDP_STATS=1 make run`, et je vous
  colle les sorties.

## État du dépôt (patch 0001)

| Fichier | Rôle |
|---|---|
| `pixel/` | simulateur 32X de SuperVDP, inchangé (sauf nom et titre) |
| `mzero/fzero.c` | boucles Master/Slave, relevé de la caméra, table de lignes, échange |
| `mzero/mode7.c` | `mode7_view`, `mode7_line_params`, `mode7_draw_line` : le code chaud |
| `mzero/m7_types.h` | le plan : carte 128 × 128 (un octet par case), tiles 8 × 8, 256 au plus |
| `mzero/cost.h`, `cachesim.h`, `mode7_cost.c` | modèle de coûts (hôte), cache SH2 simulé, un par CPU |
| `tools/mktables.c` | tables du sinus (1024, Q2.30) et des inverses de dy (2^32 / dy) |
| `tools/bmp2m7.c` | BMP 8 bits 1024 × 1024 → carte, tiles, palette (index 0 réservé) |
| `tools/crop2bmp.py` | recale le crop sur la grille d'origine : décalage (1, 3), 110 tiles au lieu de 276 |
| `tools/trajectoire.py` | boucle de 382 images, 4 pixels par image, table en ROM pour le 68000 |
| `tests/test_mode7.c` | 483 360 lignes au pixel près contre un rendu naïf qui lit le BMP |
| `tests/run_tests.sh` | + 5 scénarios du simulateur complet, identiques octet pour octet |
| `tools/sh2cycles/` | banc SH2 adapté au mode 7 (`make bench`) |

Registres COMM : 1 et 2 synchronisation Master/Slave ; 3 horizon ; 4 et
5 position (1/8 de pixel, 13.3) ; 6 angle (65 536 par tour) ; 7 hauteur
(1/256 de pixel, 8.8). COMM 0 libre.

Démo : B pause, C deux fois plus vite, Haut/Bas hauteur, Gauche/Droite
horizon. Hauteur 56 pixels, horizon ligne 63 (160 lignes de sol), focale
192.

## Choix de conception retenus

- **Formats** : u, v, du, dv en **16.16** (partie entière par un seul
  `SHLR16`, retenue automatique), sinus et cosinus en Q2.30 (on garde les
  32 bits de poids fort de DMULS.L), hauteur en 8.8, position en 13.3
  dans les COMM 16 bits. Arithmétique modulo 2^32 : le plan boucle.
- **Perspective** : s = h / dy par la **table des inverses** (dy entier,
  table exacte) ; du = s cos a, dv = s sin a ; deux DMULS.L par ligne,
  ni division, ni DIVU, ni 68000, ni libgcc.
- **Ciel gratuit** : les lignes au-dessus de l'horizon pointent toutes
  sur une ligne transparente unique de la table de lignes ; chaque CPU ne
  réécrit ses entrées que si l'horizon change.
- **Partage** : Master et Slave dessinent une ligne de sol sur deux. Pas
  d'interruption (pas de SFT en mode 7).
- **Cache** : 4 voies, 4 Ko, tout en SDRAM (le mode 2 voies + RAM est un
  peu moins bon : 38,5 contre 38,2 cycles par pixel).

## Mesures (banc SH2, par CPU et par image)

| Version | Cycles par pixel | Images de 1/60 s |
|---|---|---|
| C du patch 0001 | 38,2 | 2,74 (≈ 22 images/s) |
| Essai : carte en mots (tile × 64) | 36,3 | 2,57 |
| Essai : demi-résolution horizontale, en C | 21,0 | 1,54 |

- 35 instructions par pixel ; le cache ne pèse qu'environ 4 %. Mes stats
  sur Mac confirment : 2,60 à 2,69 images, 25 600 écritures par image
  (33 % du plafond du bus).
- `cost.h` étalonné : CYCLES_LINE 11 800 (dont 320 pixels), CYCLES_MISS
  13, CYCLES_FRAME 400 (estimé). Modèle à ±4 % du banc.

## Ce que nous avons appris

- **Le goulot est le calcul d'adresse, pas l'arithmétique.** Sur la
  trentaine d'instructions par pixel, seules 2 additions font le calcul
  (u += du, v += dv). Le reste : parties entières (4), case de la carte
  (9), numéro de tile × 64 (4), position dans la tile (6), lecture,
  assemblage des deux pixels. Sans barrel shifter, chaque décalage et
  chaque masque coûte une instruction.
- **MAC.L / MAC.W n'aident pas** : la multiplication matricielle de
  Wikipédia se réduit déjà, sur une ligne, à deux additions par pixel ;
  les multiplications ne servent qu'une fois par ligne (quelques cycles
  sur 11 800). Dans la boucle, MAC serait plus lent que deux ADD.
- **F-Zero SNES tournait à 60 images/s**, mais le mode 7 y est câblé dans
  le PPU ; sur 32X, le SH2 fait ce travail en logiciel.
- **Même un très bon assembleur** (20 à 22 cycles par pixel) laisserait
  environ 1,5 image en pleine résolution : 25 600 pixels par CPU, c'est
  trop. Il faut calculer moins de texels.
- **Le format est le levier** : placer les bits pour que les décalages
  disparaissent (par exemple v gardé multiplié par 32, pour que `SHLR16`
  donne directement la rangée de la carte). 16 bits de fraction donnent
  0,005 pixel d'erreur sur une ligne ; 12 bits resteraient sous 0,1.
- La grande image du circuit reçue a été réduite (8192 → 8000 pixels, en
  JPEG) : grille des tiles perdue. Il faudra l'original en PNG (zip, ou
  dossier ajouté par « Add folder »).

## Prochaines étapes proposées (à valider)

1. **Formats en C** : revoir les formats de u, v et de la carte pour
   supprimer des décalages ; mesurer chaque variante au banc.
2. **Demi-résolution adaptative** : un texel pour deux pixels là où un
   texel couvre déjà deux pixels ou plus (s = h / dy ≤ 0,5 : environ 50
   lignes sur 160 avec la caméra de la démo, sans perte visible), voire
   dès s ≤ 1 (environ 100 lignes, petite perte). Mesurer.
3. **Assembleur de la boucle** : esquisse soumise avant écriture, même
   interface que le C, option de compilation, vérifié contre le C.
4. Ensuite : le circuit entier (plan plus grand que 1024 × 1024) et le
   mode « hors du plan = tile fixe » de la SNES ; l'index 0 transparent
   sur la console (palette), à vérifier sur un émulateur 32X.

Autres leviers possibles, à discuter : nombre de lignes de sol (horizon),
30 images/s en pleine résolution.

## Questions ouvertes

- Quelle direction pour tenir les 60 images/s : demi-résolution
  adaptative + assembleur, 30 images/s, ou moins de lignes de sol ?
- Seuil de la demi-résolution : sans perte (s ≤ 0,5) ou avec une petite
  perte (s ≤ 1) ?
