/* =========================================================================
   M-Zero : boucles des deux SH2.

   Déroulement d'une image (comme dans SuperVDP) :

     68000 (VBlank)  Master                       Slave
     --------------  ------                       -----
     COMM 3-7 =      relève COMM 3-7 (caméra)
     caméra          publie la scène,
                     COMM_FRAME = n          -->  lit la scène
                     dessine les lignes           dessine les lignes
                     paires du sol                impaires du sol
                     attend                  <--  COMM_SLAVE_DONE = n
                     COMM_SLAVE_DONE = n
                     demande l'échange de
                     frame buffer, attend
                     qu'il soit fait (VBlank)
                     image suivante               attend COMM_FRAME = n + 1

   Le Slave ne commence jamais avant l'échange : il attend le COMM_FRAME
   suivant, que le Master n'écrit qu'une fois l'échange fait.

   Une ligne sur deux plutôt qu'une moitié d'écran chacun : une ligne
   lointaine (près de l'horizon) coûte plus cher qu'une ligne proche, car
   ses pixels sautent d'une tile à l'autre (défauts de cache). En
   s'entrelaçant, les deux CPU ont presque le même travail.

   Au-dessus de l'horizon, rien à dessiner : la table de lignes y montre
   la ligne transparente (voir framebuffer.h). Chaque CPU tient à jour les
   entrées de ses propres lignes, quand l'horizon change.

   Pas d'interruption : le mode 7 n'utilise pas le décalage d'écran (SFT)
   de SuperVDP.
   ========================================================================= */

#include "fzero.h"
#include "framebuffer.h"
#include "game.h"
#include "interface.h"
#include "mode7.h"
#include "platform.h"
#include "shared.h"
#include "vdp.h"

#if !defined(__sh__)
#include "mode7_cost.h"
#endif

/* Scène de l'image en cours, publiée par le Master (voir shared.h) */
static Scene sharedScene;

/* Horizon pour lequel chaque CPU a écrit ses entrées de la table de
   lignes, dans chaque frame buffer ([cpu][frame buffer]). Chaque CPU ne
   touche que sa ligne du tableau. -2 : table pas encore écrite. */
static int tableHorizon[PLATFORM_CPU_COUNT][2] = {{-2, -2}, {-2, -2}};

static void initVDP(int screenHeight);
static Scene readCamera(void);
static void drawScene(const Scene *scene, int cpu, int screenHeight);
static int flipFrameBuffer(void);

/* ---- Interruptions : aucune (masquées) ---------------------------------- */

void master_vint(void) { interface_clear_vint(); }
void master_hint(void) { interface_clear_hint(); }
void slave_vint(void) { interface_clear_vint(); }
void slave_hint(void) { interface_clear_hint(); }

/* ---- Master ------------------------------------------------------------- */

void master_main(void) {
    const int screenHeight = vdp_screen_height();

    initVDP(screenHeight);

    uint16_t frame = 0;

    for (;;) {
        if (++frame == 0) /* 0 veut dire « pas encore d'image » */
            frame = 1;

        Scene scene = readCamera();
        scene.drawBuffer = (vdp_read_fbcr() & FBCR_FS) ? 0 : 1;

        /* Publier la scène, puis lancer le Slave */
        *SHARED(&sharedScene) = scene;
        SHARED_BARRIER();
        comm[COMM_FRAME] = frame;

        drawScene(&scene, PLATFORM_CPU_MASTER, screenHeight);

        /* Attendre que le Slave ait fini ses lignes */
        while (comm[COMM_SLAVE_DONE] != frame)
            if (!platform_running())
                return;

        if (!flipFrameBuffer())
            return;
    }
}

/* ---- Slave -------------------------------------------------------------- */

void slave_main(void) {
    const int screenHeight = vdp_screen_height();
    uint16_t lastFrame = 0;

    for (;;) {
        uint16_t frame;
        while ((frame = comm[COMM_FRAME]) == lastFrame)
            if (!platform_running())
                return;

        SHARED_BARRIER();
        const Scene scene = *SHARED(&sharedScene);

        drawScene(&scene, PLATFORM_CPU_SLAVE, screenHeight);

        SHARED_BARRIER();
        comm[COMM_SLAVE_DONE] = frame;
        lastFrame = frame;
    }
}

/* ---- Initialisation ----------------------------------------------------- */

static void initVDP(int screenHeight) {
    /* Mode Blank : l'échange de frame buffer est immédiat */
    *bitmapModeRegister = (*bitmapModeRegister & 0xFFFC);

    /* Table de lignes et ligne transparente, dans les deux frame buffers */
    fb_init(screenHeight);
    vdp_write_fbcr(vdp_read_fbcr() ^ FBCR_FS);
    fb_init(screenHeight);

    for (int i = 0; i < PALETTE_COLORS; i++)
        vdp_palette[i] = mzero_content.palette[i];

    /* Mode Packed Pixel */
    *bitmapModeRegister = (*bitmapModeRegister & 0xFFFC) | 0x0001;
}

/* ---- Registres de la caméra ----------------------------------------------
   Le 68000 peut les écrire à tout moment. D'après le manuel, un registre
   COMM lu pendant que l'autre côté l'écrit donne une valeur indéfinie, et
   le 68000 peut n'avoir écrit qu'une partie des cinq. On relit donc les
   registres jusqu'à obtenir deux lectures identiques de suite. */

#define CAMERA_REGISTERS 5 /* COMM 3 à 7 */

static void readOnce(uint16_t r[CAMERA_REGISTERS]) {
    for (int i = 0; i < CAMERA_REGISTERS; i++)
        r[i] = comm[COMM_HORIZON + i];
}

static Scene readCamera(void) {
    uint16_t previous[CAMERA_REGISTERS], current[CAMERA_REGISTERS];
    readOnce(previous);
    for (;;) {
        readOnce(current);
        int same = 1;
        for (int i = 0; i < CAMERA_REGISTERS; i++)
            if (current[i] != previous[i]) {
                same = 0;
                previous[i] = current[i];
            }
        if (same)
            break;
    }

    Scene s;
    mode7_view(&s.view, current[COMM_CAM_X - COMM_HORIZON],
               current[COMM_CAM_Y - COMM_HORIZON],
               current[COMM_ANGLE - COMM_HORIZON],
               current[COMM_HEIGHT - COMM_HORIZON],
               (int16_t)current[COMM_HORIZON - COMM_HORIZON]);
    return s;
}

/* ---- Dessin ------------------------------------------------------------- */

/* Les lignes y > horizon montrent le sol ; Master : lignes paires,
   Slave : impaires. */
static void drawScene(const Scene *scene, int cpu, int screenHeight) {
    const Mode7Plane *const plane = mzero_content.plane;
    const int horizon = scene->view.horizon;
    if (screenHeight > FB_MAX_LINES)
        screenHeight = FB_MAX_LINES;

    /* Table de lignes : seulement si l'horizon a changé depuis la
       dernière image dessinée dans ce frame buffer */
    int *const tableDone = &tableHorizon[cpu][scene->drawBuffer];
    if (*tableDone != horizon) {
        for (int y = cpu; y < screenHeight; y += PLATFORM_CPU_COUNT)
            fb_set_line(y, y > horizon ? y : FB_BLANK_LINE);
        *tableDone = horizon;
    }

    /* Première ligne de ce CPU sous l'horizon */
    int first = horizon + 1;
    if ((first & 1) != cpu)
        first++;

#if !defined(__sh__)
    /* Outil du simulateur : modèle de coûts, un cache par CPU */
    static CacheSim cache[PLATFORM_CPU_COUNT];
    static int cacheReady[PLATFORM_CPU_COUNT];
    if (!cacheReady[cpu]) {
        cachesim_init(&cache[cpu], 4);
        cacheReady[cpu] = 1;
    }
    Mode7Cost cost = {0};
#endif

    for (int y = first; y < screenHeight; y += PLATFORM_CPU_COUNT) {
        Mode7Line line;
        mode7_line_params(&scene->view, y, &line);
        mode7_draw_line(fb_offset(0, y), plane, &line);
#if !defined(__sh__)
        mode7_count_line(plane, &line, &cache[cpu], &cost);
#endif
    }

#if !defined(__sh__)
    vdp_count_cpu_cycles(cost_cycles(&cost));
#endif
}

/* Demande l'échange des frame buffers et attend qu'il soit fait.

   Sur 32X comme dans le simulateur, une demande faite pendant l'affichage
   prend effet au début du VBlank suivant, mais une demande faite PENDANT
   le VBlank prend effet aussitôt. Si une image est dessinée plus vite que
   le VBlank (c'est le cas sur l'hôte), plusieurs échanges auraient lieu
   dans le même VBlank : on tournerait à plus de 60 images/s.
   On attend donc la fin du VBlank avant de demander l'échange : il a
   lieu au VBlank suivant, une fois par image au plus. */
static int flipFrameBuffer(void) {
    while (vdp_read_fbcr() & FBCR_VBLK)
        if (!platform_running())
            return 0;

    const uint16_t wanted = (vdp_read_fbcr() ^ FBCR_FS) & FBCR_FS;
    vdp_write_fbcr(wanted);

    while ((vdp_read_fbcr() & FBCR_FS) != wanted)
        if (!platform_running())
            return 0;
    return 1;
}
