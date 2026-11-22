/* =========================================================================
   Modèle de coûts du mode 7 (hôte seulement) : rejoue les lectures de
   mode7_draw_line dans le modèle du cache.

   Adresses : seules leurs positions dans le cache comptent (bits 4 à 9
   pour l'ensemble). La carte et les tiles font chacune 16 Ko au plus ;
   on les place à deux adresses multiples de 16 Ko, comme si elles se
   suivaient en SDRAM.
   ========================================================================= */

#include "mode7_cost.h"

#define MAP_BASE 0x06010000u
#define TILES_BASE 0x06014000u

void mode7_count_line(const Mode7Plane *plane, const Mode7Line *line,
                      CacheSim *cache, Mode7Cost *cost) {
    uint32_t u = line->u, v = line->v;
    uint32_t misses = 0;
    for (int x = 0; x < M7_SCREEN_WIDTH; x++) {
        const uint32_t px = u >> 16, py = v >> 16;
        const uint32_t cell = ((py << 4) & 0x3F80) | ((px >> 3) & 0x7F);
        const uint32_t tile = plane->map[cell];
        misses += (uint32_t)cachesim_read(cache, MAP_BASE + cell);
        misses += (uint32_t)cachesim_read(
            cache, TILES_BASE + ((tile << 6) | ((py << 3) & 0x38) | (px & 7)));
        u += line->du;
        v += line->dv;
    }
    cost->lines++;
    cost->misses += misses;
}
