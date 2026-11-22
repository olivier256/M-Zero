#ifndef MODE7_H
#define MODE7_H

#include <stdint.h>

#include "m7_types.h"

/* =========================================================================
   Mode 7 : un sol vu en perspective.

   Comme sur la Super Nintendo, chaque ligne d'écran est une droite du
   plan, parcourue à pas constant : on part du point (u, v) du plan, et
   chaque pixel avance de (du, dv). Seuls ces quatre nombres changent d'une
   ligne à l'autre ; c'est ce qui donne la perspective. Sur SNES, le
   programme les calcule (matrice et centre, ligne par ligne, par HDMA) ;
   ici, le 68000 ne donne que la caméra, et les SH2 font le reste.

   Caméra : position (camX, camY) dans le plan, hauteur h au-dessus du
   sol, orientation a. Angle 0 : la caméra regarde vers le haut du plan
   (v décroissant) ; l'angle croît dans le sens des aiguilles d'une montre.
       devant = (sin a, -cos a)      droite = (cos a, sin a)
   La ligne d'écran y montre le sol à la distance z = f x h / dy, où
   dy = y - horizon et f est la focale (M7_FOCAL, en pixels d'écran).
   Un pixel d'écran y couvre s = z / f = h / dy pixels du plan, d'où :
       du = s x cos a          dv = s x sin a
       u0 = camX + f x dv - 160 x du
       v0 = camY - f x du - 160 x dv
   (le point vu au pixel x = 0, à 160 pixels à gauche du centre).

   La seule division, h / dy, se fait par une table des inverses de dy
   (mode7_reciprocal), exacte puisque dy est entier : une multiplication
   par ligne (DMULU.L sur SH2), pas de division. Le sinus et le cosinus
   viennent d'une table (mode7_sine), une fois par image.

   Virgule fixe, en arithmétique modulo 2^32 (non signée) : le plan
   boucle, seuls les bits bas de la partie entière comptent.
   - u, v, du, dv, camX, camY : 16.16 (pixels du plan) ;
   - sinus, cosinus : Q2.30 (1,0 = 2^30) ;
   - hauteur : 8.8 (pixels du plan).
   ========================================================================= */

#define M7_SCREEN_WIDTH 320
#define M7_LINE_WORDS (M7_SCREEN_WIDTH / 2)
#define M7_FOCAL 192 /* pixels d'écran : champ de 80° environ */

/* dy va de 1 (la ligne sous l'horizon) à M7_MAX_DY */
#define M7_MAX_DY 224

/* Angle : un tour = 65536 ; la table du sinus a 1024 entrées */
#define M7_SINE_SHIFT 10
#define M7_SINE_SIZE (1 << M7_SINE_SHIFT)

/* Tables (mode7_tables.c, produit par tools/mktables) :
   - mode7_sine[i] = sin(2 pi i / 1024), Q2.30 ;
   - mode7_reciprocal[dy] = 2^32 / dy, arrondi par défaut (dy >= 1 ; pour
     dy = 1, 2^32 - 1). L'entrée 0 n'est pas utilisée. */
extern const int32_t mode7_sine[M7_SINE_SIZE];
extern const uint32_t mode7_reciprocal[M7_MAX_DY + 1];

/* Ce que les SH2 tirent des registres COMM, une fois par image */
typedef struct {
    uint32_t camX, camY; /* 16.16 */
    int32_t sinA, cosA;  /* Q2.30 */
    uint32_t height;     /* 8.8 */
    int horizon;         /* ligne d'écran où dy = 0 : de -1 à 223 */
} Mode7View;

/* Les valeurs des registres COMM (voir shared.h) :
   - x, y : position de la caméra en 1/8 de pixel (13.3) ;
   - angle : un tour = 65536 ;
   - height : hauteur en 1/256 de pixel (8.8) ;
   - horizon : ligne d'écran de l'horizon, ramenée entre -1 et 223. */
void mode7_view(Mode7View *view, uint16_t x, uint16_t y, uint16_t angle,
                uint16_t height, int16_t horizon);

/* Une ligne d'écran : point du plan au pixel 0, et pas par pixel */
typedef struct {
    uint32_t u, v;   /* 16.16 */
    uint32_t du, dv; /* 16.16, signés en complément à 2 */
} Mode7Line;

/* La ligne d'écran y, qui doit être sous l'horizon (y > horizon) */
void mode7_line_params(const Mode7View *view, int y, Mode7Line *line);

/* Dessine les 320 pixels de la ligne : 160 écritures de 16 bits à partir
   de l'offset 'offset' du frame buffer de dessin (voir fb_offset), chaque
   mot écrit une fois. Pixel x : texel du plan en
   ((u + x du) >> 16, (v + x dv) >> 16), modulo 1024. */
void mode7_draw_line(uint32_t offset, const Mode7Plane *plane,
                     const Mode7Line *line);

#endif
