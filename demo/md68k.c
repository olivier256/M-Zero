/* =========================================================================
   Programme 68000 (Mega Drive) de la démo.

   À chaque VBlank, il lit la manette, avance d'une position sur la
   trajectoire de la caméra (une table en ROM, demo/trajectoire.c) et
   écrit la caméra dans les registres COMM 3 à 7 : pour lui, la 32X est
   un VDP. Les SH2 font tout le reste, perspective comprise.

   Le 68000 n'est pas émulé : ce programme est en C, appelé par la
   plate-forme au début de chaque VBlank (voir pixel/game.h). Sur la
   console, ce serait du code 68000 : lecture du port $A10003 (TH haut
   puis TH bas), lecture de la table, puis écritures en $A15120 + 2 x n.

   Ordre : on écrit la position courante, puis on avance. Si la pause
   commence à l'image P (B maintenu dès l'image P), la caméra reste donc
   sur la position P de la trajectoire (tests/run_tests.sh s'en sert).

   Commandes :
   - B maintenu : pause (la caméra s'arrête) ;
   - C maintenu : deux fois plus vite ;
   - Haut / Bas : caméra plus haute / plus basse ;
   - Gauche / Droite : horizon plus haut / plus bas.
   ========================================================================= */

#include "game.h"
#include "interface.h"
#include "platform.h"
#include "shared.h"

#include "trajectoire.h"

#define HEIGHT_DEFAULT (56 * 256) /* 56 pixels du plan, en 8.8 */
#define HEIGHT_MIN (16 * 256)
#define HEIGHT_MAX (160 * 256)
#define HEIGHT_STEP 128    /* un demi-pixel par image */
#define HORIZON_DEFAULT 63 /* le sol occupe les lignes 64 à 223 */
#define HORIZON_MIN 8
#define HORIZON_MAX 160

static int position = 0; /* image de la trajectoire */
static int height = HEIGHT_DEFAULT;
static int horizon = HORIZON_DEFAULT;

static int clamp(int v, int min, int max) {
    return v < min ? min : (v > max ? max : v);
}

void md_vint(void) {
    const unsigned pad = platform_read_pad();

    if (pad & PAD_UP)
        height = clamp(height + HEIGHT_STEP, HEIGHT_MIN, HEIGHT_MAX);
    if (pad & PAD_DOWN)
        height = clamp(height - HEIGHT_STEP, HEIGHT_MIN, HEIGHT_MAX);
    if (pad & PAD_LEFT)
        horizon = clamp(horizon - 1, HORIZON_MIN, HORIZON_MAX);
    if (pad & PAD_RIGHT)
        horizon = clamp(horizon + 1, HORIZON_MIN, HORIZON_MAX);

    const TrajectoryPoint *p = &trajectory[position];
    comm[COMM_CAM_X] = p->x;
    comm[COMM_CAM_Y] = p->y;
    comm[COMM_ANGLE] = p->angle;
    comm[COMM_HEIGHT] = (uint16_t)height;
    comm[COMM_HORIZON] = (uint16_t)horizon;

    if (!(pad & PAD_B)) {
        position += (pad & PAD_C) ? 2 : 1;
        if (position >= trajectory_length)
            position -= trajectory_length;
    }
}
