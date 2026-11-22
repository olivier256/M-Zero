#ifndef M7_TYPES_H
#define M7_TYPES_H

#include <stdint.h>

/* =========================================================================
   Le plan de mode 7, tel que le produit tools/bmp2m7.

   Comme sur la Super Nintendo : 1024 x 1024 pixels, faits d'une carte de
   128 x 128 tiles de 8 x 8 pixels, au plus 256 tiles différentes. Pixels
   en index de palette sur 8 bits (mode Packed Pixel du VDP 32X).

   Tel quel, le plan ferait 1 Mo : il ne tient pas dans les 256 Ko de
   SDRAM. Sous forme de tiles et de carte, il fait au plus 16 Ko de carte
   (un octet par tile) et 16 Ko de tiles (64 octets chacune), au prix
   d'une indirection par pixel.

   Le plan boucle dans les deux directions (comme le mode 7 SNES avec
   « screen over » = 0) : coordonnées prises modulo 1024.

   L'index 0 n'apparaît pas dans le plan : il est transparent, et sert aux
   lignes au-dessus de l'horizon, que dessine la Mega Drive.
   ========================================================================= */

#define M7_TILE_SHIFT 3 /* tiles de 8 x 8 */
#define M7_TILE_SIZE (1 << M7_TILE_SHIFT)
#define M7_TILE_PIXELS (M7_TILE_SIZE * M7_TILE_SIZE) /* 64 octets */

#define M7_MAP_SHIFT 7 /* carte de 128 x 128 tiles */
#define M7_MAP_SIZE (1 << M7_MAP_SHIFT)
#define M7_MAP_CELLS (M7_MAP_SIZE * M7_MAP_SIZE) /* 16 Ko */

#define M7_PLANE_SHIFT (M7_MAP_SHIFT + M7_TILE_SHIFT) /* 1024 pixels */
#define M7_PLANE_SIZE (1 << M7_PLANE_SHIFT)

#define M7_MAX_TILES 256 /* numéro de tile sur un octet */

/* Plan : la carte (M7_MAP_CELLS numéros de tile, ligne par ligne) et les
   tiles (tileCount x 64 octets, ligne par ligne). Les deux commencent à
   une adresse multiple de 16 (une ligne du cache SH2). */
typedef struct {
    const uint8_t *map;
    const uint8_t *tiles;
    uint16_t tileCount;
} Mode7Plane;

/* Palette : 256 couleurs au format du VDP 32X, 0bbbbbgggggrrrrr */
#define PALETTE_COLORS 256

#endif
