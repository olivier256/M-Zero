# M-Zero

Le mode 7 de la Super Nintendo sur la Sega 32X : un plan de
1024 × 1024 pixels vu en perspective, comme le sol de F-Zero ou de
Mario Kart. « M » pour Mars, nom de code de la 32X.

La 32X se conduit comme une carte vidéo pour le 68000 : le 68000 écrit
la caméra (position, orientation, hauteur, horizon) dans les registres
COMM, les deux SH2 calculent la perspective et dessinent le sol. Au-dessus
de l'horizon, la 32X est transparente : la Mega Drive y dessine le ciel.

M-Zero fait suite à [SuperVDP](https://github.com/olivier256/SuperVDP)
(deux plans de tiles), dont il garde le simulateur **pixel** : une 32X
sans 68000, en C (MiniFB, deux threads Master et Slave, VBlank à 60 Hz,
API qui imite les registres : `vdp_dram`, `vdp_palette`, FBCR, `comm[]`).

## Compiler et lancer

```sh
git submodule update --init   # MiniFB, pour la fenêtre (voir ci-dessous)
make run                      # compile et lance la démo
make stats                    # la même, avec VDP_STATS=1
make test                     # tous les tests (environ 15 s)
make bench                    # banc SH2 (Linux, voir tools/sh2cycles)
```

Sans MiniFB, seules les cibles sans fenêtre sont construites
(`mzero_headless`, `test_mode7`). Sous Linux, MiniFB demande les
en-têtes X11 ; `-DMINIFB_USE_OPENGL_API=OFF` évite OpenGL.

Dans VS Code : ouvrir `M-Zero.code-workspace` ; tâches « Compiler »,
« Lancer la démo », « Lancer la démo avec VDP_STATS », « Tests ».

## La démo

Un morceau de 1024 × 1024 pixels du circuit Big Blue (F-Zero, SNES).
Le plan boucle : vus en mosaïque, ses deux morceaux de piste se
raccordent à peu près, et la caméra fait le tour en 6,4 secondes
(382 images, 4 pixels par image), sur une trajectoire calculée d'avance
(`demo/trajectoire.c`, une table en ROM pour le 68000).

| Manette        | Clavier | Effet                                   |
|----------------|---------|-----------------------------------------|
| B maintenu     | K       | pause                                   |
| C maintenu     | L       | deux fois plus vite                     |
| Haut / Bas     | flèches | caméra plus haute / plus basse          |
| Gauche / Droite| flèches | horizon plus haut / plus bas            |

## Registres COMM

| COMM | Sens            | Rôle                                         |
|------|-----------------|----------------------------------------------|
| 1    | Master → Slave  | numéro de l'image à dessiner                 |
| 2    | Slave → Master  | dernière image terminée                      |
| 3    | 68000 → SH2     | horizon : ligne d'écran où le sol est à l'infini |
| 4    | 68000 → SH2     | position X de la caméra, en 1/8 de pixel     |
| 5    | 68000 → SH2     | position Y de la caméra, en 1/8 de pixel     |
| 6    | 68000 → SH2     | orientation, un tour = 65 536                |
| 7    | 68000 → SH2     | hauteur de la caméra, en 1/256 de pixel      |

Le Master relève les registres 3 à 7 une fois par image (deux lectures
identiques de suite), puis publie la scène pour le Slave. Les deux CPU
dessinent donc avec la même caméra.

## Le moteur

**Le plan** (`mzero/m7_types.h`) : comme sur SNES, une carte de
128 × 128 tiles de 8 × 8 pixels, au plus 256 tiles différentes, 256
couleurs. Tel quel, le plan ferait 1 Mo ; sous forme de tiles et de
carte, au plus 32 Ko (16 Ko de carte, un octet par case ; 64 octets par
tile), au prix d'une indirection par pixel. Le plan de la démo : 16 Ko
de carte et 110 tiles (7 Ko).

**Une ligne** (`mzero/mode7.c`) : chaque ligne d'écran est une droite du
plan, parcourue à pas constant. Pixel x : texel en (u + x du, v + x dv),
en virgule fixe 16.16. Par pixel : deux additions, une lecture dans la
carte, une lecture dans la tile ; une écriture de 16 bits pour deux
pixels, chaque mot écrit une fois.

**La perspective** : la ligne y montre le sol à la distance f × h / dy
(dy = y − horizon, f = 192 pixels de focale). Un pixel y couvre
s = h / dy pixels du plan, et
du = s cos a, dv = s sin a, u0 = camX + f dv − 160 du,
v0 = camY − f du − 160 dv. La seule division, h / dy, passe par une
table des inverses de dy (dy est entier : la table est exacte) ; le
reste se fait en deux multiplications 32 × 32 → 64 bits par ligne
(DMULS.L), sans division ni appel à libgcc. Sinus et cosinus : une table
de 1024 entrées, une fois par image. Les tables sont calculées sur l'hôte
à la compilation (`tools/mktables.c`).

**Le ciel ne coûte rien** : la table de lignes du frame buffer fait
pointer toutes les lignes au-dessus de l'horizon sur une même ligne
d'index 0 (transparent), remplie une fois au démarrage. Chaque CPU ne
réécrit les entrées de ses lignes que si l'horizon change.

**Partage** : Master et Slave dessinent une ligne de sol sur deux. Pas
d'interruption (le mode 7 n'a pas besoin du décalage d'écran SFT de
SuperVDP : chaque pixel est calculé, il n'y a pas de source impaire).

## Mesurer : `VDP_STATS=1`

Toutes les 5 secondes dans la fenêtre, et à chaque capture sans fenêtre :
écritures dans le frame buffer (comptées exactement : 160 par ligne de
sol, 25 600 par image avec l'horizon de la démo, 33 % du plafond de
76 800), cycles du bus VDP, cycles CPU **estimés** par le modèle de
`mzero/cost.h`, et le verdict en images de 1/60 s.

Le modèle compte les lignes, et rejoue les lectures de la carte et des
tiles dans un modèle du cache du SH2 (`mzero/cachesim.h`, un par CPU).
Ses coûts unitaires sont **étalonnés** par le banc `tools/sh2cycles` :
le vrai code compilé pour SH2, exécuté sous qemu avec un modèle de temps
du SH2. C'est un modèle, pas une 32X réelle, mais le nombre
d'instructions est exact.

## Vérifier

`tests/run_tests.sh` (ou `make test`) :
1. `test_mode7` : les tables ; les paramètres de ligne contre la
   géométrie calculée en double (pas à 2/65 536 de pixel près,
   extrémités de ligne à 0,01 pixel près) ; le dessin de 483 360 lignes
   au pixel près, contre un rendu naïf qui lit directement le BMP (sans
   tiles ni carte) : toutes les lignes de toute la trajectoire à trois
   hauteurs, et 300 000 lignes au hasard ; 160 écritures 16 bits par
   ligne, chaque mot écrit une fois.
2. Le simulateur complet sans fenêtre : la manette suit un scénario
   (pause, accélération, hauteur, horizon), et l'image capturée doit être
   identique, octet pour octet, à l'image de référence.

## Les images

`demo/assets/bigblue.bmp` : BMP 8 bits indexé, 1024 × 1024, index 0
réservé (transparent), converti en C par `tools/bmp2m7` à chaque
compilation. Il est produit depuis `demo/assets/source/bigblue_1024.png`
par `tools/crop2bmp.py` (Pillow), qui recale le crop sur la grille des
tiles d'origine : 110 tiles au lieu de 276.

## Organisation

| Dossier / fichier          | Rôle                                                    |
|----------------------------|---------------------------------------------------------|
| `pixel/`                   | simulateur 32X : VDP, interface (COMM, V_INT, H_INT), plates-formes fenêtre et sans fenêtre |
| `mzero/fzero.c`            | boucles Master et Slave, relevé de la caméra, table de lignes, échange de frame buffer |
| `mzero/mode7.c`            | paramètres d'une ligne et dessin d'une ligne (le code chaud) |
| `mzero/cost.h`, `cachesim.h`, `mode7_cost.c` | modèle de coûts (cycles estimés), hôte seulement |
| `mzero/m7_types.h`         | le plan : carte et tiles                                |
| `mzero/shared.h`           | registres COMM, scène partagée, accès sans cache        |
| `demo/`                    | contenu de la démo, trajectoire et programme 68000      |
| `tools/bmp2m7.c`           | convertisseur BMP → plan de mode 7 en C                 |
| `tools/mktables.c`         | tables du sinus et des inverses                         |
| `tools/crop2bmp.py`, `tools/trajectoire.py` | préparation du plan et de la trajectoire (Python) |
| `tests/`                   | tests                                                   |
| `tools/sh2cycles/`         | banc SH2 sous qemu : temps du code compilé, étalonnage de `cost.h` |

## Vers une vraie 32X

- Plan, tables et trajectoire en SDRAM : environ 30 Ko sur 256 Ko.
- `vdp_dram_write16()` devient une écriture directe en 2400 0000h +
  offset ; `SHARED()` donne déjà l'adresse cache-through sur SH2.
- Palette : l'index 0 doit laisser voir la Mega Drive (à régler au
  démarrage, à vérifier sur un émulateur 32X).
- `tools/sh2cycles/build-tools.sh` construit la chaîne `sh-elf` (gcc
  13.2) qui servira aussi pour la console.

## Licence

M-Zero est publié sous licence MIT. Copyright © 2026 Olivier BROSSERON.
Si vous utilisez M-Zero, merci de citer le projet et son auteur.
