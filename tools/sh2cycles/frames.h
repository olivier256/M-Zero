#ifndef BENCH_FRAMES_H
#define BENCH_FRAMES_H

/* =========================================================================
   Images mesurées par le banc sh2cycles : la même liste pour le programme
   SH2 (bench.c, temps mesuré) et pour le programme hôte (costs.c,
   compteurs du modèle). Reproductible : pas de hasard, un générateur fixe.
   - 0 à 95   : la trajectoire de la démo (une position sur quatre), à la
                hauteur et à l'horizon de la démo (56 pixels, ligne 63) ;
   - 96 à 179 : caméras quelconques : position, orientation, hauteur de
                16 à 160 pixels, horizon de la ligne 8 à la ligne 160.
                Elles font varier le nombre de lignes et les défauts de
                cache (une caméra haute voit loin).
   ========================================================================= */

#include "trajectoire.h"

#define BENCH_FRAMES 180
#define BENCH_TRAJECTORY_FRAMES 96
#define BENCH_SCREEN_HEIGHT 224

typedef struct {
    uint16_t x, y, angle, height;
    int16_t horizon;
} BenchFrame;

static inline BenchFrame bench_frame(int i) {
    BenchFrame f;
    if (i < BENCH_TRAJECTORY_FRAMES) {
        const TrajectoryPoint *p = &trajectory[(i * 4) % trajectory_length];
        f.x = p->x;
        f.y = p->y;
        f.angle = p->angle;
        f.height = 56 * 256;
        f.horizon = 63;
    } else {
        uint32_t s = 2026u + (uint32_t)i * 2654435761u;
        s ^= s >> 13;
        s *= 0x5bd1e995u;
        s ^= s >> 15;
        f.x = (uint16_t)(s * 7u);
        f.y = (uint16_t)(s >> 7);
        f.angle = (uint16_t)(s >> 16);
        f.height = (uint16_t)((16 + (s >> 3) % 145u) * 256u);
        f.horizon = (int16_t)(8 + (s >> 11) % 153u);
    }
    return f;
}

#endif
