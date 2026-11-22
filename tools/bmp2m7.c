/* =========================================================================
   bmp2m7 : convertit un plan de mode 7 (BMP 8 bits indexé, 1024 x 1024)
   en données C pour M-Zero (voir mzero/m7_types.h).

   Usage : bmp2m7 PLAN.bmp NOM SORTIE
     Produit SORTIE.h et SORTIE.c :
     - asset_NOM (Mode7Plane) : la carte de 128 x 128 tiles et les tiles
       de 8 x 8, les tiles identiques gardées une fois (256 au plus) ;
     - asset_palette : la palette du BMP, au format 32X.

   L'index 0 est réservé (transparent, voir m7_types.h) : un pixel du plan
   à 0 est une erreur.

   Formats acceptés : BMP Windows non compressé, 8 bits par pixel, de haut
   en bas ou de bas en haut (GIMP en mode indexé, Aseprite, Pillow en mode
   "P"...). La lecture BMP vient de tools/bmp2c (SuperVDP).
   ========================================================================= */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "m7_types.h"

typedef struct {
    int width, height;
    uint8_t *pixels;     /* width * height, de haut en bas */
    uint8_t rgb[256][3]; /* palette du fichier */
    int colors;          /* entrées présentes dans le fichier */
} Image;

static void fail(const char *fmt, const char *arg) {
    fprintf(stderr, "bmp2m7 : ");
    fprintf(stderr, fmt, arg);
    fprintf(stderr, "\n");
    exit(1);
}

/* ---- Lecture BMP --------------------------------------------------------- */

static uint32_t le32(const uint8_t *p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint16_t le16(const uint8_t *p) { return p[0] | (p[1] << 8); }

static void loadBMP(const char *path, Image *img) {
    FILE *f = fopen(path, "rb");
    if (f == NULL)
        fail("impossible d'ouvrir %s", path);

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc((size_t)size);
    if (data == NULL || fread(data, 1, (size_t)size, f) != (size_t)size)
        fail("lecture impossible : %s", path);
    fclose(f);

    if (size < 54 || data[0] != 'B' || data[1] != 'M')
        fail("%s n'est pas un fichier BMP", path);

    uint32_t pixelOffset = le32(data + 10);
    uint32_t infoSize = le32(data + 14);
    int32_t width = (int32_t)le32(data + 18);
    int32_t height = (int32_t)le32(data + 22);
    uint16_t bpp = le16(data + 28);
    uint32_t compression = le32(data + 30);
    uint32_t colorsUsed = le32(data + 46);

    if (infoSize < 40)
        fail("%s : en-tête BMP trop ancien (OS/2)", path);
    if (bpp != 8)
        fail("%s : il faut une image 8 bits indexée (256 couleurs)", path);
    if (compression != 0)
        fail("%s : la compression RLE n'est pas prise en charge", path);

    int topDown = height < 0;
    if (topDown)
        height = -height;
    if (width <= 0 || height <= 0)
        fail("%s : dimensions invalides", path);

    img->width = width;
    img->height = height;
    img->colors = colorsUsed ? (int)colorsUsed : 256;
    if (img->colors > 256)
        fail("%s : palette de plus de 256 couleurs", path);

    memset(img->rgb, 0, sizeof img->rgb);
    const uint8_t *pal = data + 14 + infoSize;
    if (pal + img->colors * 4 > data + size)
        fail("%s : palette tronquée", path);
    for (int i = 0; i < img->colors; i++) {
        img->rgb[i][0] = pal[i * 4 + 2]; /* BMP : B, G, R, réservé */
        img->rgb[i][1] = pal[i * 4 + 1];
        img->rgb[i][2] = pal[i * 4 + 0];
    }

    /* Chaque ligne est complétée à un multiple de 4 octets */
    size_t stride = ((size_t)width + 3) & ~(size_t)3;
    if (pixelOffset + stride * (size_t)height > (size_t)size)
        fail("%s : pixels tronqués", path);

    img->pixels = malloc((size_t)width * height);
    if (img->pixels == NULL)
        fail("mémoire insuffisante pour %s", path);
    for (int y = 0; y < height; y++) {
        int srcY = topDown ? y : height - 1 - y;
        memcpy(img->pixels + (size_t)y * width,
               data + pixelOffset + (size_t)srcY * stride, (size_t)width);
    }
    free(data);
}

/* ---- Tiles et carte ------------------------------------------------------ */

static uint8_t tiles[M7_MAX_TILES][M7_TILE_PIXELS];
static int tileCount = 0;
static uint8_t map[M7_MAP_CELLS];

/* Numéro de la tile (une recherche linéaire suffit : 256 tiles au plus) */
static int findOrAddTile(const uint8_t *pixels) {
    for (int t = 0; t < tileCount; t++)
        if (memcmp(tiles[t], pixels, M7_TILE_PIXELS) == 0)
            return t;
    if (tileCount == M7_MAX_TILES)
        return -1;
    memcpy(tiles[tileCount], pixels, M7_TILE_PIXELS);
    return tileCount++;
}

static void cutTiles(const Image *img, const char *path) {
    for (int ty = 0; ty < M7_MAP_SIZE; ty++)
        for (int tx = 0; tx < M7_MAP_SIZE; tx++) {
            uint8_t pixels[M7_TILE_PIXELS];
            for (int y = 0; y < M7_TILE_SIZE; y++)
                for (int x = 0; x < M7_TILE_SIZE; x++) {
                    const uint8_t p =
                        img->pixels[(size_t)(ty * M7_TILE_SIZE + y) *
                                        M7_PLANE_SIZE +
                                    tx * M7_TILE_SIZE + x];
                    if (p == 0)
                        fail("%s : index 0 dans le plan (réservé, "
                             "transparent)",
                             path);
                    pixels[y * M7_TILE_SIZE + x] = p;
                }
            const int t = findOrAddTile(pixels);
            if (t < 0)
                fail("%s : plus de 256 tiles différentes", path);
            map[ty * M7_MAP_SIZE + tx] = (uint8_t)t;
        }
}

/* ---- Écriture ------------------------------------------------------------ */

static void writeBytes(FILE *c, const uint8_t *bytes, size_t n) {
    for (size_t i = 0; i < n; i++)
        fprintf(c, "%s%u,", i % 16 ? " " : "\n    ", bytes[i]);
    fprintf(c, "\n};\n\n");
}

static void writeOutput(const Image *img, const char *name, const char *out) {
    char path[1024];

    snprintf(path, sizeof path, "%s.h", out);
    FILE *h = fopen(path, "w");
    if (h == NULL)
        fail("impossible d'écrire %s", path);
    fprintf(h, "/* Produit par tools/bmp2m7 : ne pas modifier. */\n"
               "#ifndef DEMO_ASSETS_H\n#define DEMO_ASSETS_H\n\n"
               "#include \"m7_types.h\"\n\n");
    fprintf(h, "extern const Mode7Plane asset_%s;\n", name);
    fprintf(h, "extern const uint16_t asset_palette[PALETTE_COLORS];\n");
    fprintf(h, "\n#endif\n");
    fclose(h);

    snprintf(path, sizeof path, "%s.c", out);
    FILE *c = fopen(path, "w");
    if (c == NULL)
        fail("impossible d'écrire %s", path);
    fprintf(c,
            "/* Produit par tools/bmp2m7 : ne pas modifier. */\n"
            "#include \"%s.h\"\n\n",
            strrchr(out, '/') ? strrchr(out, '/') + 1 : out);

    /* Alignées sur 16 octets : une ligne du cache SH2 */
    fprintf(c,
            "static const uint8_t map_%s[M7_MAP_CELLS] "
            "__attribute__((aligned(16))) = {",
            name);
    writeBytes(c, map, M7_MAP_CELLS);
    fprintf(c,
            "static const uint8_t tiles_%s[%d * M7_TILE_PIXELS] "
            "__attribute__((aligned(16))) = {",
            name, tileCount);
    writeBytes(c, &tiles[0][0], (size_t)tileCount * M7_TILE_PIXELS);
    fprintf(c, "const Mode7Plane asset_%s = {map_%s, tiles_%s, %d};\n\n", name,
            name, name, tileCount);

    fprintf(c, "const uint16_t asset_palette[PALETTE_COLORS] = {");
    for (int i = 0; i < PALETTE_COLORS; i++) {
        unsigned r = img->rgb[i][0] >> 3, g = img->rgb[i][1] >> 3,
                 b = img->rgb[i][2] >> 3;
        fprintf(c, "%s0x%04X,", i % 8 ? " " : "\n    ",
                r | (g << 5) | (b << 10));
    }
    fprintf(c, "\n};\n");
    fclose(c);
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "usage : bmp2m7 PLAN.bmp NOM SORTIE\n");
        return 2;
    }
    Image img;
    loadBMP(argv[1], &img);
    if (img.width != M7_PLANE_SIZE || img.height != M7_PLANE_SIZE)
        fail("%s : il faut une image de 1024 x 1024", argv[1]);
    cutTiles(&img, argv[1]);
    writeOutput(&img, argv[2], argv[3]);
    fprintf(stderr, "bmp2m7 : %s, %d tiles (%d octets) + carte (%d octets)\n",
            argv[2], tileCount, tileCount * M7_TILE_PIXELS, M7_MAP_CELLS);
    return 0;
}
