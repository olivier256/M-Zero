#ifndef FZERO_H
#define FZERO_H

#include <stdint.h>

#include "m7_types.h"

/* =========================================================================
   M-Zero : un sol en mode 7 pour la 32X.

   La 32X se conduit comme un VDP pour le 68000 :
   - le 68000 écrit la caméra dans les registres COMM 3 à 7 (voir
     shared.h) : position, orientation, hauteur, horizon ;
   - les deux SH2 dessinent le sol, sous l'horizon, une ligne sur deux
     chacun. Au-dessus, la 32X est transparente (index 0) : la Mega Drive
     y dessine le ciel et le décor.

   fzero.c fournit les points d'entrée des SH2 (master_main, slave_main,
   et leurs routines d'interruption, voir pixel/game.h). Le programme qui
   l'utilise fournit le contenu (ci-dessous) et le programme 68000
   (md_vint).
   ========================================================================= */

typedef struct {
    const Mode7Plane *plane;
    const uint16_t *palette; /* PALETTE_COLORS couleurs ; l'index 0 est
                                transparent sur la console */
} MZeroContent;

/* Fourni par le programme : ce que M-Zero affiche */
extern const MZeroContent mzero_content;

#endif
