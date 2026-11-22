/* =========================================================================
   Banc sh2cycles, côté hôte : pour chaque image de frames.h et les lignes
   d'un CPU, les compteurs du modèle de coûts (mzero/cost.h), dont les
   défauts de cache du modèle cachesim.h, et l'empreinte des lignes
   dessinées.

   Usage : costs PREMIERE_LIGNE VOIES
     PREMIERE_LIGNE : 0 = Master, 1 = Slave ;
     VOIES : 4 (cache de 4 Ko) ou 2 (mode « 2 voies + RAM »).
   Sortie (stdout), une ligne par image :
     image;lines;misses;modele;empreinte
   ========================================================================= */

#include "framebuffer.h"
#include "mode7.h"
#include "mode7_cost.h"
#include "vdp.h"

#include "demo_assets.h"
#include "frames.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t fb[VDP_DRAM_SIZE];

void vdp_dram_write16(uint32_t offset, uint16_t pixels) {
    fb[offset] = (uint8_t)(pixels >> 8);
    fb[offset + 1] = (uint8_t)pixels;
}

void vdp_dram_write8(uint32_t offset, uint8_t pixel) { fb[offset] = pixel; }

static void frameCost(const BenchFrame *f, int first, CacheSim *cache,
                      Mode7Cost *c, uint32_t *print) {
    Mode7View view;
    mode7_view(&view, f->x, f->y, f->angle, f->height, f->horizon);
    int y0 = view.horizon + 1;
    if ((y0 & 1) != first)
        y0++;
    for (int y = y0; y < BENCH_SCREEN_HEIGHT; y += 2) {
        Mode7Line line;
        mode7_line_params(&view, y, &line);
        mode7_draw_line(fb_offset(0, y), &asset_bigblue, &line);
        if (c != NULL)
            mode7_count_line(&asset_bigblue, &line, cache, c);
    }
    /* Empreinte des lignes dessinées (voir bench.c) */
    uint32_t h = 2166136261u;
    for (int y = y0; y < BENCH_SCREEN_HEIGHT; y += 2)
        for (int x = 0; x < FB_WIDTH; x++)
            h = (h ^ fb[fb_offset(x, y)]) * 16777619u;
    *print = h;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "usage : costs PREMIERE_LIGNE VOIES\n");
        return 2;
    }
    const int first = atoi(argv[1]);
    CacheSim cache;
    cachesim_init(&cache, atoi(argv[2]));

    /* Image de mise en route, comme bench.c : le cache se remplit */
    uint32_t print;
    Mode7Cost warm = {0};
    const BenchFrame w = bench_frame(0);
    frameCost(&w, first, &cache, &warm, &print);

    for (int i = 0; i < BENCH_FRAMES; i++) {
        const BenchFrame f = bench_frame(i);
        Mode7Cost c;
        memset(&c, 0, sizeof c);
        frameCost(&f, first, &cache, &c, &print);
        printf("%d;%u;%u;%u;%08x\n", i, c.lines, c.misses, cost_cycles(&c),
               print);
    }
    return 0;
}
