#include "game.h"
#include "interface.h"
#include "platform.h"
#include "vdp.h"

#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* =========================================================================
   Plate-forme sans fenêtre, pour les tests et les captures.

   Mêmes règles que platform_minifb.c (threads Master et Slave, balayage de
   262 lignes à 60 Hz en temps réel, V_INT par signal), mais l'image n'est
   pas affichée : certaines images sont enregistrées en PPM, et la manette
   suit un scénario donné en ligne de commande. Les compteurs d'écritures
   dans le frame buffer sont affichés à chaque capture et à la fin.

   Usage :
     mzero_headless [--frames N] [--shot IMAGE:fichier.ppm]...
                    [--pad DEBUT-FIN:BOUTONS]...
   Boutons : U D L R (croix), A B C, S (Start). Exemple :
     mzero_headless --frames 120 --pad 10-60:R --shot 119:fin.ppm
   ========================================================================= */

#define SCREEN_HEIGHT 224
#define VBLANK_LINES 38
#define FRAME_RATE 60
#define MAX_SHOTS 16
#define MAX_PAD_STEPS 32

static volatile int _running = 1;
static __thread int _cpuId = -1;

static int _totalLines = SCREEN_HEIGHT + VBLANK_LINES;
static long _frameLimit = 300;
static volatile long _frame = 0; /* image en cours de balayage */
static volatile int64_t _vblankStartNs = 0;

static uint32_t *_rgb = NULL;

static struct {
    long frame;
    const char *path;
} _shots[MAX_SHOTS];
static int _shotCount = 0;

static struct {
    long first, last;
    unsigned buttons;
} _pad[MAX_PAD_STEPS];
static int _padCount = 0;

static pthread_t _cpuThread[PLATFORM_CPU_COUNT];
static volatile int _cpuReady[PLATFORM_CPU_COUNT];

#define INTERRUPT_SIGNAL SIGUSR1

/* ==== Fonctions de platform.h ============================================ */

static int64_t nowNs(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000000000LL + t.tv_nsec;
}

/* Latence des interruptions. Sur la console, un SH2 prend une
   interruption en quelques cycles (s'il ne l'a pas masquée). Ici, le
   thread du CPU doit d'abord obtenir un processeur de l'hôte : sur une
   machine chargée (ou à 2 cœurs), cela peut prendre plus d'une ligne, et
   deux H_INT se confondraient. Le simulateur imite donc la garantie du
   matériel : après une H_INT, la synchro vidéo attend que sa routine
   l'ait acquittée, en cédant le processeur, au plus HINT_WAIT_NS. Au-delà,
   la H_INT reste en attente et sera comptée en retard au HBlank suivant :
   c'est alors une vraie faute du jeu (interruptions masquées, routine trop
   longue). */
#define HINT_WAIT_NS 50000 /* 50 µs, moins d'une ligne (63,5 µs) */

static void waitHintServed(void) {
    const int64_t limit = nowNs() + HINT_WAIT_NS;
    while (interface_hint_waiting() && nowNs() < limit)
        sched_yield();
}

/* Appelée dans chaque boucle d'attente du jeu : on y cède le processeur
   de l'hôte, pour qu'un CPU qui attend (le Slave entre deux images, par
   exemple) ne prive pas l'autre, ni la synchro vidéo, d'un cœur. Sur une
   machine à 2 cœurs, sans cela, le Master pouvait prendre ses H_INT avec
   plus d'une ligne de retard. */
int platform_running(void) {
    sched_yield();
    return _running;
}
int platform_cpu_id(void) { return _cpuId; }
void platform_sync(void) { __sync_synchronize(); }

double platform_vblank_elapsed_ms(void) {
    return (nowNs() - _vblankStartNs) / 1000000.0;
}

unsigned platform_read_pad(void) {
    unsigned pad = 0;
    for (int i = 0; i < _padCount; i++)
        if (_frame >= _pad[i].first && _frame <= _pad[i].last)
            pad |= _pad[i].buttons;
    return pad;
}

void platform_raise_interrupt(int cpu) {
    if (cpu < 0 || cpu >= PLATFORM_CPU_COUNT || !_cpuReady[cpu])
        return;
    pthread_kill(_cpuThread[cpu], INTERRUPT_SIGNAL);
}

static void interruptHandler(int sig) {
    (void)sig;
    int cpu = _cpuId;
    if (cpu < 0)
        return;

    /* V_INT d'abord (niveau 12), puis H_INT (niveau 10), comme le SH2 */
    while (_running) {
        if (interface_vint_active(cpu)) {
            if (cpu == PLATFORM_CPU_MASTER)
                master_vint();
            else
                slave_vint();
            interface_count_handled(cpu, !interface_vint_active(cpu));
        } else if (interface_hint_active(cpu)) {
            if (cpu == PLATFORM_CPU_MASTER)
                master_hint();
            else
                slave_hint();
            interface_count_hint_handled(cpu);
        } else {
            break;
        }
    }
}

/* ==== CPU ================================================================ */

static void *cpuThread(void *arg) {
    int cpu = (int)(intptr_t)arg;
    _cpuId = cpu;
    _cpuThread[cpu] = pthread_self();
    _cpuReady[cpu] = 1;
    if (cpu == PLATFORM_CPU_MASTER)
        master_main();
    else
        slave_main();
    _cpuReady[cpu] = 0;
    return NULL;
}

/* ==== Captures =========================================================== */

static void savePPM(const char *path) {
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        perror(path);
        return;
    }
    fprintf(f, "P6\n%d %d\n255\n", VDP_SCREEN_WIDTH, SCREEN_HEIGHT);
    for (int i = 0; i < VDP_SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
        uint8_t rgb[3] = {(uint8_t)(_rgb[i] >> 16), (uint8_t)(_rgb[i] >> 8),
                          (uint8_t)_rgb[i]};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    fprintf(stderr, "image %ld -> %s\n", _frame, path);
}

/* ==== Balayage ===========================================================
   Une ligne toutes les 63,6 µs, en temps réel : les CPU voient le même
   rythme que dans la fenêtre. On dort jusqu'à chaque ligne. */

static void sleepUntil(int64_t t) {
    int64_t d = t - nowNs();
    if (d <= 0)
        return;
    struct timespec ts = {(time_t)(d / 1000000000LL), (long)(d % 1000000000LL)};
    nanosleep(&ts, NULL);
}

static void runVideo(void) {
    const int64_t origin = nowNs();
    const int64_t linesPerSecond = (int64_t)_totalLines * FRAME_RATE;

    for (long frame = 0; frame < _frameLimit; frame++) {
        _frame = frame;
        for (int line = 0; line < _totalLines; line++) {
            int64_t lineNo = frame * _totalLines + line;
            int64_t start = origin + lineNo * 1000000000LL / linesPerSecond;
            sleepUntil(start);

            if (line == 0)
                vdp_end_vblank();

            if (line < SCREEN_HEIGHT) {
                vdp_render_line(line, _rgb + (size_t)line * VDP_SCREEN_WIDTH);
                interface_hblank(0); /* H_INT : voir platform_minifb.c */
                waitHintServed();
            } else {
                interface_hblank(1);
            }
            if (line == SCREEN_HEIGHT) {
                /* L'image 'frame' est complète : capture éventuelle */
                for (int i = 0; i < _shotCount; i++)
                    if (_shots[i].frame == frame) {
                        savePPM(_shots[i].path);
                        char label[32];
                        snprintf(label, sizeof label, "image %ld", frame);
                        vdp_print_write_stats(label);
                        interface_stats is;
                        interface_get_stats(&is);
                        fprintf(stderr,
                                "[%s] H_INT levees %lu | master traitees %lu "
                                "(en retard %lu) | slave traitees %lu (en "
                                "retard %lu)\n",
                                label, is.hintRaised, is.hintHandled[0],
                                is.hintLate[0], is.hintHandled[1],
                                is.hintLate[1]);
                    }

                _vblankStartNs = start;
                md_vint(); /* avant l'échange : voir platform_minifb.c */
                vdp_begin_vblank();
                interface_raise_vint();
            }
        }
    }
}

/* ==== Ligne de commande ================================================== */

static unsigned parseButtons(const char *s) {
    unsigned b = 0;
    for (; *s; s++) {
        switch (*s) {
        case 'U':
            b |= PAD_UP;
            break;
        case 'D':
            b |= PAD_DOWN;
            break;
        case 'L':
            b |= PAD_LEFT;
            break;
        case 'R':
            b |= PAD_RIGHT;
            break;
        case 'A':
            b |= PAD_A;
            break;
        case 'B':
            b |= PAD_B;
            break;
        case 'C':
            b |= PAD_C;
            break;
        case 'S':
            b |= PAD_START;
            break;
        default:
            fprintf(stderr, "bouton inconnu : %c\n", *s);
            exit(1);
        }
    }
    return b;
}

static void usage(void) {
    fprintf(stderr,
            "usage : mzero_headless [--frames N] "
            "[--shot IMAGE:fichier.ppm]... [--pad DEBUT-FIN:BOUTONS]...\n");
    exit(1);
}

static void parseArgs(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (i + 1 >= argc)
            usage();
        const char *opt = argv[i];
        char *val = argv[++i];

        if (strcmp(opt, "--frames") == 0) {
            _frameLimit = atol(val);
        } else if (strcmp(opt, "--shot") == 0 && _shotCount < MAX_SHOTS) {
            char *colon = strchr(val, ':');
            if (colon == NULL)
                usage();
            *colon = '\0';
            _shots[_shotCount].frame = atol(val);
            _shots[_shotCount].path = colon + 1;
            _shotCount++;
        } else if (strcmp(opt, "--pad") == 0 && _padCount < MAX_PAD_STEPS) {
            long first, last;
            char buttons[16];
            if (sscanf(val, "%ld-%ld:%15s", &first, &last, buttons) != 3)
                usage();
            _pad[_padCount].first = first;
            _pad[_padCount].last = last;
            _pad[_padCount].buttons = parseButtons(buttons);
            _padCount++;
        } else {
            usage();
        }
    }
}

int main(int argc, char *argv[]) {
    parseArgs(argc, argv);

    _rgb = calloc((size_t)VDP_SCREEN_WIDTH * SCREEN_HEIGHT, sizeof(uint32_t));
    if (_rgb == NULL)
        return 1;

    vdp_init(SCREEN_HEIGHT);

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = interruptHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(INTERRUPT_SIGNAL, &sa, NULL) != 0)
        return 1;

    pthread_t threads[PLATFORM_CPU_COUNT];
    for (int cpu = 0; cpu < PLATFORM_CPU_COUNT; cpu++)
        if (pthread_create(&threads[cpu], NULL, cpuThread,
                           (void *)(intptr_t)cpu))
            return 1;

    runVideo();
    vdp_print_write_stats("fin");

    _running = 0;
    for (int cpu = 0; cpu < PLATFORM_CPU_COUNT; cpu++)
        pthread_join(threads[cpu], NULL);

    free(_rgb);
    return 0;
}
