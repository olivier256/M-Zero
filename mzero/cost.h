#ifndef COST_H
#define COST_H

#include <stdint.h>

#include "mode7.h"

/* =========================================================================
   Modèle de coûts : cycles SH2 estimés.

   Le simulateur exécute le moteur en code natif : il ne mesure pas le
   temps SH2. Le moteur compte donc ce qu'il fait et multiplie par des
   coûts unitaires en cycles. Le résultat est déclaré au simulateur
   (vdp_count_cpu_cycles), qui ajoute lui-même le coût des écritures dans
   le frame buffer (3 cycles, 5 si le FIFO est plein) et l'affiche avec
   VDP_STATS=1.

   En mode 7, une ligne coûte toujours le même calcul (320 pixels, sans
   cas particulier) : le coût d'un pixel ne se distingue pas de celui
   d'une ligne, il y est compté. Ce qui varie, ce sont les défauts de cache : le
   plan (16 Ko de carte, jusqu'à 16 Ko de tiles) ne tient pas dans le cache de
   4 Ko du SH2, et une ligne lointaine saute d'une tile à l'autre à chaque
   pixel. Le simulateur rejoue donc les accès à la carte et aux tiles de
   chaque ligne dans un modèle du cache (voir cachesim.h) : un par CPU,
   comme sur la console.

   Les coûts unitaires sont ÉTALONNÉS par le banc tools/sh2cycles : le
   vrai code de mode7.c, compilé pour SH2 (gcc 13.2 -m2 -O2), exécuté
   sous qemu avec un modèle de temps du SH2 (cycles par instruction,
   branchements, dépendances, cache, SDRAM). Ajustement par moindres
   carrés sur 360 images (180 par CPU) : le modèle reproduit les temps du
   banc à 4 % près. Les refaire après toute modification de mode7.c :
   tools/sh2cycles/run.sh imprime les nouvelles valeurs.
   ========================================================================= */

typedef struct {
    uint32_t lines;  /* lignes de sol dessinées (320 pixels chacune) */
    uint32_t misses; /* défauts de cache (carte et tiles), modèle cachesim */
} Mode7Cost;

/* Coûts unitaires, en cycles SH2, hors écritures dans le frame buffer.
   Cache en mode 4 voies (4 Ko), code et données en SDRAM.
   CYCLES_LINE : 11 800 cycles, soit 36,9 par pixel (35 instructions).
   CYCLES_MISS : 13 par défaut du modèle cachesim ; le banc compte environ
   1,6 fois plus de défauts de données que le modèle, le coût ajusté en
   tient compte.
   CYCLES_FRAME : ESTIMATION (relevé de la caméra, table de lignes,
   échange), le banc ne mesure que les lignes. */
#define CYCLES_FRAME 400
#define CYCLES_LINE 11800
#define CYCLES_MISS 13

static inline uint32_t cost_cycles(const Mode7Cost *c) {
    return CYCLES_FRAME + c->lines * CYCLES_LINE + c->misses * CYCLES_MISS;
}

#endif
