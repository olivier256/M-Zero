#ifndef CACHESIM_H
#define CACHESIM_H

#include <stdint.h>

/* =========================================================================
   Modèle du cache du SH2, pour le modèle de coûts (cost.h). Hôte
   seulement : sur la console, c'est le vrai cache qui travaille.

   SH7604 : 4 Ko, 64 ensembles de 4 voies, lignes de 16 octets,
   remplacement LRU. En mode « 2 voies + RAM » (bit TW du CCR), 2 Ko de
   cache en 64 ensembles de 2 voies. Écriture immédiate (write-through)
   sans allocation : seules les lectures chargent des lignes.

   On n'y fait passer que les lectures de la carte et des tiles : le code
   et la pile, petits et relus sans cesse, restent en cache.
   ========================================================================= */

#define CACHESIM_SETS 64
#define CACHESIM_MAX_WAYS 4

typedef struct {
    int ways;
    /* étiquette + 1 de chaque voie (0 = vide), de la plus récente à la
       plus ancienne */
    uint32_t tag[CACHESIM_SETS][CACHESIM_MAX_WAYS];
} CacheSim;

static inline void cachesim_init(CacheSim *c, int ways) {
    c->ways = ways;
    for (int s = 0; s < CACHESIM_SETS; s++)
        for (int w = 0; w < CACHESIM_MAX_WAYS; w++)
            c->tag[s][w] = 0;
}

/* Lecture à l'adresse addr : renvoie 1 si c'est un défaut de cache */
static inline int cachesim_read(CacheSim *c, uint32_t addr) {
    uint32_t *const set = c->tag[(addr >> 4) & (CACHESIM_SETS - 1)];
    const uint32_t tag = (addr >> 10) + 1;
    int w = 0;
    while (w < c->ways && set[w] != tag)
        w++;
    const int miss = w == c->ways;
    if (miss)
        w = c->ways - 1; /* la plus ancienne est remplacée */
    for (; w > 0; w--)   /* la ligne devient la plus récente */
        set[w] = set[w - 1];
    set[0] = tag;
    return miss;
}

#endif
