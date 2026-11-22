# sh2cycles : temps SH2 du moteur, et étalonnage du modèle de coûts

Le simulateur pixel exécute M-Zero en code natif : il compte exactement
les écritures dans le frame buffer, mais pas le temps des SH2. Il affiche
donc des cycles CPU **estimés**, d'après le modèle de coûts de
`mzero/cost.h`. Ce banc donne à ce modèle des valeurs mesurées.

Repris de SuperVDP ; seuls le programme mesuré et l'ajustement changent.

## Ce que fait le banc

1. `mzero/mode7.c` est compilé **pour SH2** (`sh-elf-gcc -m2 -O2`) :
   c'est le code qui tournera sur la 32X. Il est lié aux adresses de la
   32X (`bench.ld`) : code, données, plan et pile en SDRAM
   (0600 0000h), frame buffer en 2400 0000h. `run.sh` vérifie qu'il
   n'appelle pas libgcc (division, décalage variable...).
2. Le programme (`bench.c`) dessine les lignes de sol d'un CPU pour
   180 images (`frames.h` : la trajectoire de la démo, et des caméras
   quelconques). Il tourne sous `qemu-sh4eb`, avec le greffon
   `sh2cycles_plugin.c` qui mesure chaque image, d'après un modèle du
   SH2 :
   - temps de chaque instruction (SH7604 Hardware Manual) : 1 cycle, sauf
     branchements (BT/BF pris 3, BT/S/BF/S pris 2, BRA, JSR, RTS... 2),
     multiplications ;
   - +1 cycle quand une instruction lit le registre que la précédente
     vient de charger ;
   - cache (lignes de 16 octets, LRU, commun aux instructions et aux
     données, écriture simultanée) : +12 cycles par défaut (une ligne
     depuis la SDRAM), +1 par écriture en SDRAM ;
   - par défaut, cache de 4 Ko en 4 voies : le mode 7 lit beaucoup de
     données (carte et tiles, 30 Ko), il a besoin de tout le cache.
     `CACHE=ram` : mode « 2 voies + RAM » (registre CCR), 2 Ko de cache
     et le code de `mode7.c` dans les 2 Ko de RAM interne
     (`bench_ram.ld`), pour comparer. qemu refusant les adresses au-delà
     de 2 Go, la RAM interne y est placée en 5000 0000h ; le temps simulé
     est le même ;
   - les écritures dans le frame buffer sont comptées à part (le
     simulateur leur compte 3 à 5 cycles).
   Un programme par CPU : chacun a son propre cache.
3. Sur l'hôte, `costs.c` calcule les compteurs du modèle pour les mêmes
   images (lignes, et défauts de cache du modèle `mzero/cachesim.h`).
   `fit.py` vérifie que le code SH2 dessine exactement les mêmes pixels
   (empreintes) et écrit 160 mots par ligne, puis ajuste les coûts
   unitaires par moindres carrés.

Non modélisé : le conflit entre lecture d'instruction et accès aux
données dans le même cycle, le partage de la SDRAM avec l'autre SH2,
l'attente quand le FIFO du frame buffer est plein. Les temps donnés sont
ceux d'un modèle, plus fin que `cost.h`, mais pas d'une 32X réelle.

## Lancer

**Sous Linux**, directement (ou dans la session de Claude) :

```sh
sudo apt install build-essential curl xz-utils libgmp-dev libmpfr-dev \
     libmpc-dev ninja-build libglib2.0-dev pkg-config python3-venv \
     python3-distlib python3-numpy flex bison texinfo
tools/sh2cycles/build-tools.sh     # une fois : ~/opt/sh-elf et ~/opt/qemu
tools/sh2cycles/run.sh             # ou : make bench
```

**Sur Mac** : qemu en mode utilisateur n'existe que sous Linux. Le banc
tourne alors dans un conteneur Linux (Docker Desktop, OrbStack ou
colima) : `tools/sh2cycles/docker.sh`. La première fois, l'image est
construite (environ 40 minutes) ; ensuite, le banc prend quelques
secondes. Les résultats sont dans `build/sh2cycles`.

Variables : `PREFIX` (dossier des outils, `~/opt` par défaut), ou, une
par une, `SH_ELF` (préfixe de la chaîne), `QEMU` et `QEMU_INCLUDE`
(dossier de `qemu-plugin.h`) ; `CACHE=ram` (voir plus haut).

La même chaîne `sh-elf` servira à compiler M-Zero pour la vraie 32X.

## Profil : où passe le temps

```sh
~/opt/qemu/bin/qemu-sh4eb \
    -plugin build/sh2cycles/libsh2cycles.so,profile=on \
    build/sh2cycles/bench_master.elf >/dev/null 2>profil.txt
```

- `profil;adresse;opcode;exécutions` : exécutions de chaque instruction
  pendant les mesures ; avec `sh-elf-objdump -d
  build/sh2cycles/bench_master.elf`, on retrouve les boucles chères ;
- `defaut;ligne;nombre` : défauts de cache par ligne de 16 octets ; avec
  `sh-elf-nm -n`, on voit quelles données (ou quel code) se chassent.

Chaque ligne `sh2cycles;...` donne aussi, en dernier champ, les défauts
de cache dus aux instructions.
