/* =========================================================================
   Banc sh2cycles, côté SH2 : dessine les lignes de sol d'un CPU pour
   chaque image de frames.h, avec le vrai code de mzero/mode7.c compilé
   pour SH2, et marque chaque image pour le greffon qemu qui mesure le
   temps (sh2cycles_plugin.c).

   BENCH_FIRST_LINE : 0 pour le Master (lignes paires), 1 pour le Slave.
   Chaque CPU ayant son propre cache, on lance un programme par CPU.

   Après chaque image, hors mesure, une empreinte des lignes dessinées est
   imprimée : elle doit être identique à celle du programme hôte
   (costs.c), preuve que le code SH2 dessine juste.
   ========================================================================= */

#include "framebuffer.h"
#include "mode7.h"
#include "vdp.h"

#include "demo_assets.h"
#include "frames.h"

#ifndef BENCH_FIRST_LINE
#define BENCH_FIRST_LINE 0
#endif

/* Placés par bench.ld : 2000 0000h (marqueurs), 2400 0000h (frame buffer) */
volatile uint32_t bench_marker[4] __attribute__((section(".marker")));
volatile uint8_t bench_fb[VDP_DRAM_SIZE] __attribute__((section(".fb")));

void sys_write(const char *text, int length);

/* Les lignes de ce CPU sous l'horizon, comme drawScene (fzero.c) */
static int firstLine(const Mode7View *view) {
    int first = view->horizon + 1;
    if ((first & 1) != BENCH_FIRST_LINE)
        first++;
    return first;
}

static void drawFrame(const BenchFrame *f) {
    Mode7View view;
    mode7_view(&view, f->x, f->y, f->angle, f->height, f->horizon);
    for (int y = firstLine(&view); y < BENCH_SCREEN_HEIGHT; y += 2) {
        Mode7Line line;
        mode7_line_params(&view, y, &line);
        mode7_draw_line(fb_offset(0, y), &asset_bigblue, &line);
    }
}

/* Empreinte des lignes de sol dessinées par ce CPU */
static uint32_t fingerprint(const BenchFrame *f) {
    Mode7View view;
    mode7_view(&view, f->x, f->y, f->angle, f->height, f->horizon);
    uint32_t h = 2166136261u;
    for (int y = firstLine(&view); y < BENCH_SCREEN_HEIGHT; y += 2)
        for (int x = 0; x < FB_WIDTH; x++)
            h = (h ^ bench_fb[fb_offset(x, y)]) * 16777619u;
    return h;
}

static void printHex(uint32_t v) {
    static const char digits[] = "0123456789abcdef";
    char text[9];
    for (int i = 0; i < 8; i++)
        text[i] = digits[(v >> (28 - 4 * i)) & 15];
    text[8] = '\n';
    sys_write(text, 9);
}

int main(void) {
    /* Une image hors mesure, pour remplir le cache comme en régime établi */
    const BenchFrame warm = bench_frame(0);
    drawFrame(&warm);

    for (int i = 0; i < BENCH_FRAMES; i++) {
        const BenchFrame f = bench_frame(i);
        bench_marker[0] = 1; /* début de la mesure */
        drawFrame(&f);
        bench_marker[1] = 1; /* fin */
        printHex(fingerprint(&f));
    }
    return 0;
}
