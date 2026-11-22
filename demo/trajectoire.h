#ifndef TRAJECTOIRE_H
#define TRAJECTOIRE_H

#include <stdint.h>

/* =========================================================================
   Trajectoire de la caméra de la démo, une position par image : une table
   en ROM pour le programme 68000, produite par tools/trajectoire.py.
   Formats des registres COMM (voir mzero/shared.h) : x et y en 1/8 de
   pixel du plan, angle sur 65536 par tour. La trajectoire boucle.
   ========================================================================= */

typedef struct {
    uint16_t x, y, angle;
} TrajectoryPoint;

extern const uint16_t trajectory_length;
extern const TrajectoryPoint trajectory[];

#endif
