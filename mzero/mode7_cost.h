#ifndef MODE7_COST_H
#define MODE7_COST_H

#include "cachesim.h"
#include "cost.h"
#include "mode7.h"

/* Compteurs du modèle de coûts pour une ligne dessinée par
   mode7_draw_line : une ligne (320 pixels), et les défauts de cache de
   ses lectures (carte puis tile, pour chaque pixel), rejouées dans le
   cache 'cache' du CPU qui la dessine. Hôte seulement. */
void mode7_count_line(const Mode7Plane *plane, const Mode7Line *line,
                      CacheSim *cache, Mode7Cost *cost);

#endif
