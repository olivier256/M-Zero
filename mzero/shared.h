#ifndef SHARED_H
#define SHARED_H

#include <stdint.h>

#include "mode7.h"

/* =========================================================================
   Ce que se partagent les trois processeurs.

   1. Registres COMM (pixel/interface.h) : 8 mots visibles du 68000 et des
      deux SH2, sans cache. M-Zero s'en sert comme des registres d'un
      VDP : le 68000 y écrit la caméra, les SH2 dessinent.
   2. La scène de l'image en cours (Scene) : relevée par le Master au
      début de l'image, lue par le Slave, en mémoire partagée (SDRAM).
   ========================================================================= */

/* ---- Registres COMM ----------------------------------------------------- */

#define COMM_FRAME 1      /* Master -> Slave : image à dessiner (1, 2, ...) */
#define COMM_SLAVE_DONE 2 /* Slave -> Master : dernière image terminée */

/* La caméra : écrite par le 68000, relevée par le Master une fois par
   image (voir mode7_view pour les formats). */
#define COMM_HORIZON 3 /* ligne d'écran de l'horizon (signée) */
#define COMM_CAM_X 4   /* position, 1/8 de pixel du plan */
#define COMM_CAM_Y 5
#define COMM_ANGLE 6  /* orientation, un tour = 65536 */
#define COMM_HEIGHT 7 /* hauteur, 1/256 de pixel du plan */
/* COMM 0 : libre */

/* ---- Scène ---------------------------------------------------------------
   Tout ce qu'il faut pour dessiner une image : les deux CPU dessinent avec
   les mêmes valeurs, même si le 68000 les change en cours d'image. */

typedef struct {
    Mode7View view;
    int16_t drawBuffer; /* frame buffer dessiné : 0 ou 1 (valeur de FS
                           quand il sera affiché) */
} Scene;

/* ---- Mémoire partagée entre les SH2 --------------------------------------
   Chaque SH2 a son propre cache : sans précaution, le Slave peut lire une
   copie périmée de ce que le Master vient d'écrire.
   - Sur 32X : on accède à la donnée par son adresse « cache-through »
     (adresse | 2000 0000h), qui contourne le cache.
   - Sur le simulateur : les threads de l'hôte n'ont pas ce problème de
     cache, mais le processeur (Apple Silicon) peut réordonner les
     écritures : il faut une barrière mémoire.
   SHARED(p) donne le pointeur à utiliser ; SHARED_BARRIER() sépare
   l'écriture des données de celle du signal COMM qui les annonce (et,
   côté lecteur, la lecture du signal de celle des données). */

#if defined(__sh__)
#define SHARED(p) ((__typeof__(p))((uintptr_t)(p) | 0x20000000u))
#define SHARED_BARRIER() __asm__ volatile("" ::: "memory")
#else
#define SHARED(p) (p)
#define SHARED_BARRIER() __sync_synchronize()
#endif

#endif
