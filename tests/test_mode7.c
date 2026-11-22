/* =========================================================================
   Tests du mode 7, hors simulateur (pas de threads, pas de VBlank).

   1. Tables : le sinus contre sin(), les inverses contre la division.
   2. Paramètres de ligne (mode7_line_params) contre la géométrie calculée
      en double : pas (du, dv), point de départ et point d'arrivée de la
      ligne, pour toutes les orientations, plusieurs hauteurs, positions
      et horizons. L'arithmétique entière arrondit : on borne l'erreur.
   3. Dessin d'une ligne (mode7_draw_line), AU PIXEL PRÈS, contre un rendu
      de référence naïf qui ne partage rien avec le moteur : le pixel x
      est lu directement dans l'image du plan (le BMP, relu ici, sans
      tiles ni carte) au point (u + x du, v + x dv), calculé par une
      multiplication et non par des additions successives. On vérifie
      aussi les écritures : 160 par ligne, toutes en 16 bits, à des
      adresses paires, chaque mot de la ligne écrit une fois, rien hors de
      la ligne. Jeux d'essai : toutes les lignes de sol de toutes les
      positions de la trajectoire de la démo, à trois hauteurs ; et
      300 000 lignes tirées au hasard (u, v, du, dv quelconques sur
      32 bits).

   Autre usage : écrire l'image de référence d'un écran entier, pour la
   comparer à une capture du simulateur (tests/run_tests.sh) :
     test_mode7 --ppm POSITION HAUTEUR HORIZON fichier.ppm
   POSITION : image de la trajectoire de la démo ; HAUTEUR en 8.8.
   ========================================================================= */

#include "framebuffer.h"
#include "mode7.h"
#include "vdp.h"

#include "demo_assets.h"
#include "trajectoire.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PI 3.14159265358979323846
#define SCREEN_HEIGHT 224

static const Mode7Plane *const plane = &asset_bigblue;

/* ---- L'image du plan, relue depuis le BMP --------------------------------
   Lecture minimale : BMP 8 bits non compressé, 1024 x 1024. */

static uint8_t image[M7_PLANE_SIZE * M7_PLANE_SIZE];

static uint32_t le32(const uint8_t *p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void loadImage(void) {
    FILE *f = fopen(DEMO_BMP_PATH, "rb");
    if (f == NULL) {
        perror(DEMO_BMP_PATH);
        exit(1);
    }
    static uint8_t data[64 + 1024 + M7_PLANE_SIZE * M7_PLANE_SIZE];
    const size_t size = fread(data, 1, sizeof data, f);
    fclose(f);
    const uint32_t offset = le32(data + 10);
    const int32_t width = (int32_t)le32(data + 18);
    int32_t height = (int32_t)le32(data + 22);
    const int topDown = height < 0;
    if (topDown)
        height = -height;
    if (width != M7_PLANE_SIZE || height != M7_PLANE_SIZE || data[28] != 8 ||
        offset + (size_t)M7_PLANE_SIZE * M7_PLANE_SIZE > size) {
        fprintf(stderr, "%s : BMP 8 bits de 1024 x 1024 attendu\n",
                DEMO_BMP_PATH);
        exit(1);
    }
    for (int y = 0; y < M7_PLANE_SIZE; y++)
        memcpy(image + y * M7_PLANE_SIZE,
               data + offset +
                   (size_t)(topDown ? y : M7_PLANE_SIZE - 1 - y) *
                       M7_PLANE_SIZE,
               M7_PLANE_SIZE);
}

/* Pixel x de la ligne, selon la référence */
static uint8_t refPixel(const Mode7Line *l, int x) {
    const uint32_t u = l->u + (uint32_t)x * l->du;
    const uint32_t v = l->v + (uint32_t)x * l->dv;
    return image[((v >> 16) & (M7_PLANE_SIZE - 1)) * M7_PLANE_SIZE +
                 ((u >> 16) & (M7_PLANE_SIZE - 1))];
}

/* ---- Frame buffer de test -------------------------------------------------
   mode7_draw_line écrit par vdp_dram_write16 : on la fournit ici, à la
   place du simulateur, pour tout vérifier. Une seule ligne est dessinée
   à la fois, à l'offset LINE_OFFSET. */

#define LINE_OFFSET 0x1000u

static uint8_t fb[VDP_DRAM_SIZE];
static int wordWrites[M7_LINE_WORDS];
static long badWrites = 0; /* hors ligne ou mal alignées */
static long writeCount = 0;

void vdp_dram_write16(uint32_t offset, uint16_t pixels) {
    writeCount++;
    if ((offset & 1) || offset < LINE_OFFSET ||
        offset >= LINE_OFFSET + M7_SCREEN_WIDTH) {
        badWrites++;
        return;
    }
    wordWrites[(offset - LINE_OFFSET) / 2]++;
    fb[offset] = (uint8_t)(pixels >> 8);
    fb[offset + 1] = (uint8_t)pixels;
}

/* Non utilisées par mode7.c ; déclarées par vdp.h */
void vdp_dram_write8(uint32_t offset, uint8_t pixel) {
    (void)offset;
    (void)pixel;
    badWrites++;
}

static long linesChecked = 0, lineFailures = 0;

static void checkLine(const Mode7Line *l, const char *what) {
    memset(wordWrites, 0, sizeof wordWrites);
    writeCount = 0;
    badWrites = 0;
    mode7_draw_line(LINE_OFFSET, plane, l);
    linesChecked++;

    int ok = writeCount == M7_LINE_WORDS && badWrites == 0;
    for (int w = 0; w < M7_LINE_WORDS; w++)
        if (wordWrites[w] != 1)
            ok = 0;
    int x = 0;
    for (; ok && x < M7_SCREEN_WIDTH; x++)
        if (fb[LINE_OFFSET + x] != refPixel(l, x))
            ok = 0;
    if (!ok && lineFailures++ < 10)
        fprintf(stderr,
                "ECHEC %s : u=%08x v=%08x du=%08x dv=%08x, %ld ecritures, "
                "%ld hors ligne, pixel %d\n",
                what, l->u, l->v, l->du, l->dv, writeCount, badWrites, x - 1);
}

/* ---- 1. Tables ----------------------------------------------------------- */

static int testTables(void) {
    int failures = 0;
    double worst = 0;
    for (int i = 0; i < M7_SINE_SIZE; i++) {
        const double e = fabs(mode7_sine[i] / (double)(1L << 30) -
                              sin(2 * PI * i / M7_SINE_SIZE));
        if (e > worst)
            worst = e;
    }
    if (worst > 1.0 / (1L << 30))
        failures++;
    for (unsigned dy = 2; dy <= M7_MAX_DY; dy++)
        if (mode7_reciprocal[dy] != (uint32_t)((1ULL << 32) / dy))
            failures++;
    if (mode7_reciprocal[1] != 0xFFFFFFFFu)
        failures++;
    printf("tables : sinus a %.1e pres, %d inverses ; %s\n", worst, M7_MAX_DY,
           failures ? "ECHEC" : "ok");
    return failures;
}

/* ---- 2. Paramètres de ligne ---------------------------------------------- */

/* Écart signé a - b, en unités de 2^-16 pixel, modulo 2^32 */
static double gap(uint32_t a, double b) {
    double d = (double)a - fmod(b, 4294967296.0);
    d = fmod(d, 4294967296.0);
    if (d > 2147483648.0)
        d -= 4294967296.0;
    if (d < -2147483648.0)
        d += 4294967296.0;
    return fabs(d);
}

static double worstStep = 0, worstEnd = 0;

static void checkParams(uint16_t x, uint16_t y, uint16_t angle, uint16_t height,
                        int16_t horizon) {
    Mode7View view;
    mode7_view(&view, x, y, angle, height, horizon);
    /* L'angle effectif : les 10 bits de poids fort */
    const double a = 2 * PI * (angle >> 6) / M7_SINE_SIZE;
    for (int sy = view.horizon + 1; sy < SCREEN_HEIGHT; sy++) {
        Mode7Line l;
        mode7_line_params(&view, sy, &l);
        const double s = height / 256.0 / (sy - view.horizon) * 65536.0;
        const double du = s * cos(a), dv = s * sin(a);
        const double u0 = x * 8192.0 + M7_FOCAL * dv - 160 * du;
        const double v0 = y * 8192.0 - M7_FOCAL * du - 160 * dv;
        const double steps[] = {gap(l.du, du), gap(l.dv, dv)};
        const double ends[] = {gap(l.u, u0), gap(l.v, v0),
                               gap(l.u + 319 * l.du, u0 + 319 * du),
                               gap(l.v + 319 * l.dv, v0 + 319 * dv)};
        for (int i = 0; i < 2; i++)
            if (steps[i] > worstStep)
                worstStep = steps[i];
        for (int i = 0; i < 4; i++)
            if (ends[i] > worstEnd)
                worstEnd = ends[i];
    }
}

/* Tolérances, en 2^-16 pixel : le pas à 4 unités près (arrondis de s et
   du produit), les extrémités de la ligne à 1/16 de pixel près (le pas
   multiplié par 160 à 320, et la focale). */
#define STEP_TOLERANCE 4.0
#define END_TOLERANCE 4096.0

static int testParams(void) {
    static const uint16_t heights[] = {16 * 256, 56 * 256, 160 * 256,
                                       255 * 256 + 255};
    static const int16_t horizons[] = {-1, 63, 150};
    for (int a = 0; a < 65536; a += 64)
        for (int h = 0; h < 4; h++)
            for (int z = 0; z < 3; z++)
                checkParams((uint16_t)(a * 7), (uint16_t)(a * 13), (uint16_t)a,
                            heights[h], horizons[z]);
    const int failures = worstStep > STEP_TOLERANCE || worstEnd > END_TOLERANCE;
    printf("parametres de ligne : pas a %.0f / 65536 pixel pres, extremites "
           "a %.3f pixel pres ; %s\n",
           worstStep, worstEnd / 65536.0, failures ? "ECHEC" : "ok");
    return failures;
}

/* ---- 3. Dessin ----------------------------------------------------------- */

static uint32_t rngState = 2026u;
static uint32_t rng(void) {
    rngState ^= rngState << 13;
    rngState ^= rngState >> 17;
    rngState ^= rngState << 5;
    return rngState;
}

static int testDraw(void) {
    /* La trajectoire de la démo, toutes ses lignes de sol */
    static const uint16_t heights[] = {16 * 256, 56 * 256, 160 * 256};
    for (int h = 0; h < 3; h++)
        for (int p = 0; p < trajectory_length; p++) {
            Mode7View view;
            mode7_view(&view, trajectory[p].x, trajectory[p].y,
                       trajectory[p].angle, heights[h], 63);
            for (int y = view.horizon + 1; y < SCREEN_HEIGHT; y++) {
                Mode7Line l;
                mode7_line_params(&view, y, &l);
                checkLine(&l, "trajectoire");
            }
        }
    const long trajectoryLines = linesChecked;

    /* Lignes quelconques */
    for (int i = 0; i < 300000; i++) {
        Mode7Line l = {rng(), rng(), rng(), rng()};
        if (i & 1) { /* des pas plus vraisemblables, une fois sur deux */
            l.du = (uint32_t)((int32_t)l.du >> 12);
            l.dv = (uint32_t)((int32_t)l.dv >> 12);
        }
        checkLine(&l, "hasard");
    }

    printf("dessin : %ld lignes verifiees au pixel pres (%ld de la "
           "trajectoire, %ld au hasard), %ld echecs\n",
           linesChecked, trajectoryLines, linesChecked - trajectoryLines,
           lineFailures);
    return lineFailures != 0;
}

/* ---- Image de référence d'un écran entier -------------------------------- */

static uint32_t paletteRGB(uint8_t index) {
    const uint16_t c = asset_palette[index];
    uint32_t r = c & 0x1F, g = (c >> 5) & 0x1F, b = (c >> 10) & 0x1F;
    r = (r << 3) | (r >> 2);
    g = (g << 3) | (g >> 2);
    b = (b << 3) | (b >> 2);
    return (r << 16) | (g << 8) | b;
}

static int writeReference(int position, int height, int horizon,
                          const char *path) {
    const TrajectoryPoint *p = &trajectory[position % trajectory_length];
    Mode7View view;
    mode7_view(&view, p->x, p->y, p->angle, (uint16_t)height, (int16_t)horizon);
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        perror(path);
        return 1;
    }
    fprintf(f, "P6\n%d %d\n255\n", M7_SCREEN_WIDTH, SCREEN_HEIGHT);
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        Mode7Line l;
        if (y > view.horizon)
            mode7_line_params(&view, y, &l);
        for (int x = 0; x < M7_SCREEN_WIDTH; x++) {
            const uint32_t rgb =
                paletteRGB(y > view.horizon ? refPixel(&l, x) : 0);
            const uint8_t bytes[3] = {(uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8),
                                      (uint8_t)rgb};
            fwrite(bytes, 1, 3, f);
        }
    }
    return fclose(f) != 0;
}

int main(int argc, char *argv[]) {
    loadImage();

    if (argc == 6 && strcmp(argv[1], "--ppm") == 0)
        return writeReference(atoi(argv[2]), atoi(argv[3]), atoi(argv[4]),
                              argv[5]);
    if (argc != 1) {
        fprintf(stderr, "usage : test_mode7 [--ppm POSITION HAUTEUR "
                        "HORIZON fichier.ppm]\n");
        return 2;
    }

    int failures = testTables();
    failures += testParams();
    failures += testDraw();
    printf(failures ? "ECHEC\n" : "Tous les tests du mode 7 sont passes.\n");
    return failures != 0;
}
