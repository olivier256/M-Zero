/* =========================================================================
   Mode 7 : paramètres d'une ligne, et dessin d'une ligne.

   Voir mode7.h pour la géométrie. Ce fichier est le code chaud du moteur,
   écrit pour le SH2 :
   - aucune division, aucun % : les inverses viennent d'une table ;
   - aucun décalage de distance variable (le SH2 n'a que des décalages
     de 1, 2, 8 et 16 bits) : tous les décalages sont constants ;
   - les produits 32 x 32 -> 64 bits ne gardent que les 32 bits de poids
     fort (DMULS.L / DMULU.L, puis STS MACH) : jamais de décalage d'un
     nombre de 64 bits, qui appellerait libgcc.
   ========================================================================= */

#include "mode7.h"
#include "vdp.h"

/* 32 bits de poids fort du produit, signé et non signé */
static inline int32_t mulhi(int32_t a, int32_t b) {
    return (int32_t)(((int64_t)a * b) >> 32);
}
static inline uint32_t mulhu(uint32_t a, uint32_t b) {
    return (uint32_t)(((uint64_t)a * b) >> 32);
}

void mode7_view(Mode7View *view, uint16_t x, uint16_t y, uint16_t angle,
                uint16_t height, int16_t horizon) {
    /* 13.3 -> 16.16 */
    view->camX = (uint32_t)x << 13;
    view->camY = (uint32_t)y << 13;

    /* Les 10 bits de poids fort de l'angle ; cos a = sin (a + 1/4 tour) */
    const unsigned i = angle >> (16 - M7_SINE_SHIFT);
    view->sinA = mode7_sine[i];
    view->cosA = mode7_sine[(i + M7_SINE_SIZE / 4) & (M7_SINE_SIZE - 1)];

    view->height = height;
    view->horizon = horizon < -1 ? -1 : (horizon > 223 ? 223 : horizon);
}

void mode7_line_params(const Mode7View *view, int y, Mode7Line *line) {
    const unsigned dy = (unsigned)(y - view->horizon); /* 1 à 224 */

    /* s = h / dy en 16.16 : (h en 8.8) x 2^8 x (2^32 / dy), >> 32.
       h < 2^16, donc h x 2^8 < 2^24 : pas de débordement. */
    const uint32_t s = mulhu(view->height << 8, mode7_reciprocal[dy]);

    /* s < 2^24 : s x 4 tient sur 31 bits. (s x 4) x (sin en Q2.30) >> 32
       = s x sin, en 16.16. */
    const int32_t du = mulhi((int32_t)(s << 2), view->cosA);
    const int32_t dv = mulhi((int32_t)(s << 2), view->sinA);

    /* Modulo 2^32 : un point lointain peut sortir des 16 bits de partie
       entière, sans conséquence puisque le plan boucle. */
    line->du = (uint32_t)du;
    line->dv = (uint32_t)dv;
    line->u = view->camX + (uint32_t)dv * M7_FOCAL -
              (uint32_t)du * (M7_SCREEN_WIDTH / 2);
    line->v = view->camY - (uint32_t)du * M7_FOCAL -
              (uint32_t)dv * (M7_SCREEN_WIDTH / 2);
}

/* Le pixel du plan au point (u, v), 16.16. Sur SH2, u >> 16 est un seul
   SHLR16 ; les autres décalages se composent de SHLL2, SHLL et SHLR2. */
static inline uint32_t texel(const uint8_t *map, const uint8_t *tiles,
                             uint32_t u, uint32_t v) {
    const uint32_t x = u >> 16; /* pixel du plan, modulo 65536 */
    const uint32_t y = v >> 16;
    /* Case de la carte : (y / 8) x 128 + x / 8, modulo 128 x 128 */
    const uint32_t cell = ((y << 4) & 0x3F80) | ((x >> 3) & 0x7F);
    const uint32_t tile = map[cell];
    /* Pixel de la tile : (y mod 8) x 8 + x mod 8 */
    return tiles[(tile << 6) | ((y << 3) & 0x38) | (x & 7)];
}

void mode7_draw_line(uint32_t offset, const Mode7Plane *plane,
                     const Mode7Line *line) {
    const uint8_t *const map = plane->map;
    const uint8_t *const tiles = plane->tiles;
    const uint32_t du = line->du, dv = line->dv;
    uint32_t u = line->u, v = line->v;

    for (int i = 0; i < M7_LINE_WORDS; i++) {
        const uint32_t left = texel(map, tiles, u, v);
        u += du;
        v += dv;
        const uint32_t right = texel(map, tiles, u, v);
        u += du;
        v += dv;
        /* Poids fort = pixel de gauche, comme MOV.W sur SH2 */
        vdp_dram_write16(offset, (uint16_t)(left << 8 | right));
        offset += 2;
    }
}
