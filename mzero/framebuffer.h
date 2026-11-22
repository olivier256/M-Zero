#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include <stdint.h>

#include "vdp.h"

/* =========================================================================
   Frame buffer du VDP 32X en mode Packed Pixel (un octet par pixel).

   La DRAM commence par la table de lignes : un mot par ligne affichée,
   qui donne l'adresse (en mots) des pixels de la ligne. On réserve
   256 mots (512 octets), comme le 32X Hardware Manual, puis les lignes se
   suivent, de FB_PITCH octets chacune (pas de décalage d'écran SFT en
   mode 7 : chaque pixel est calculé, il n'y a pas de source impaire).

   Les lignes au-dessus de l'horizon sont dessinées par la Mega Drive :
   la 32X y montre l'index 0, transparent. Plutôt que de les remplir à
   chaque image, la table de lignes les fait toutes pointer sur une même
   ligne d'index 0 (FB_BLANK_LINE), remplie une fois au démarrage : le
   ciel ne coûte aucune écriture.
   ========================================================================= */

#define FB_WIDTH VDP_SCREEN_WIDTH
#define FB_PITCH VDP_SCREEN_WIDTH
#define FB_LINE_TABLE_BYTES 512
#define FB_MAX_LINES 240  /* 224 en NTSC, 240 en PAL */
#define FB_BLANK_LINE 240 /* la ligne transparente, après l'écran */

/* Position du pixel (x, y) en octets depuis le début du frame buffer de
   dessin : c'est l'offset attendu par vdp_dram_write*(). */
static inline uint32_t fb_offset(int x, int y) {
    return (uint32_t)(FB_LINE_TABLE_BYTES + y * FB_PITCH + x);
}

/* Entrée de la table de lignes : adresse en mots de la ligne line */
static inline uint16_t fb_line_address(int line) {
    return (uint16_t)(fb_offset(0, line) / 2);
}

/* Écrit l'entrée 'y' de la table de lignes du frame buffer de dessin :
   l'écran affichera, à la ligne y, la ligne 'line' du frame buffer.
   Une écriture de 16 bits, comptée comme les autres. */
void fb_set_line(int y, int line);

/* Remplit la table de lignes (chaque ligne montre sa propre ligne) et la
   ligne transparente, dans le frame buffer de dessin. Au démarrage. */
void fb_init(int screenHeight);

#endif
