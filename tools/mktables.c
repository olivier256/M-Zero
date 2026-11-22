/* =========================================================================
   mktables : écrit les tables du mode 7 (voir mzero/mode7.h).

   Usage : mktables SORTIE.c

   - mode7_sine[i] = sin(2 pi i / 1024), en Q2.30 (1,0 = 2^30), arrondi ;
   - mode7_reciprocal[dy] = 2^32 / dy, arrondi par défaut, pour dy de 1 à
     M7_MAX_DY ; pour dy = 1, 2^32 - 1 (2^32 ne tient pas sur 32 bits).

   Calculées sur l'hôte, à la compilation : le SH2 n'a ni virgule
   flottante ni division, et la table tient en ROM (ou en SDRAM).
   ========================================================================= */

#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "mode7.h"

#define PI 3.14159265358979323846

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "usage : mktables SORTIE.c\n");
        return 2;
    }
    FILE *f = fopen(argv[1], "w");
    if (f == NULL) {
        perror(argv[1]);
        return 1;
    }

    fprintf(f, "/* Produit par tools/mktables : ne pas modifier. */\n\n"
               "#include \"mode7.h\"\n\n");

    fprintf(f, "const int32_t mode7_sine[M7_SINE_SIZE] = {");
    for (int i = 0; i < M7_SINE_SIZE; i++) {
        const double s = sin(2.0 * PI * i / M7_SINE_SIZE);
        fprintf(f, "%s%ld,", i % 6 ? " " : "\n    ",
                (long)lround(s * (double)(1L << 30)));
    }
    fprintf(f, "\n};\n\n");

    fprintf(f, "const uint32_t mode7_reciprocal[M7_MAX_DY + 1] = {\n    0u,");
    for (int dy = 1; dy <= M7_MAX_DY; dy++) {
        const uint64_t r = dy == 1 ? 0xFFFFFFFFu : (1ULL << 32) / (unsigned)dy;
        fprintf(f, "%s%lluu,", dy % 6 ? " " : "\n    ", (unsigned long long)r);
    }
    fprintf(f, "\n};\n");

    return fclose(f) == 0 ? 0 : 1;
}
