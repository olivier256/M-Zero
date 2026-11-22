#!/usr/bin/env python3
"""
Calcule la trajectoire de la caméra de la démo, image par image, et
l'écrit en C (demo/trajectoire.c) : le programme 68000 la lit comme une
table en ROM.

    python3 tools/trajectoire.py demo/trajectoire.c

Le script ne sert qu'à refaire la table si le parcours change : la table
produite est dans le dépôt.

Le parcours suit le circuit du plan de la démo (bigblue.bmp, 1024 x 1024).
Le plan boucle : vu en mosaïque, ses deux morceaux de piste se raccordent
à peu près. Le morceau du haut sort par la droite et continue par celui
de gauche, qui descend et sort par le bas, puis rejoint le morceau du
haut. Le parcours est donc fermé sur le tore : il revient à son point de
départ, décalé de (1024, 1024).

- Points de passage (pixels du plan, coordonnées « déroulées ») relevés
  sur le plan, au milieu de la piste.
- Courbe de Catmull-Rom fermée par ces points, parcourue à vitesse
  constante : SPEED pixels par image.
- Orientation : vers le point situé LOOK_AHEAD pixels plus loin sur la
  courbe (le regard anticipe les virages).

Sortie, pour chaque image : x et y en 1/8 de pixel (13.3), modulo 1024
pixels ; angle, un tour = 65536, 0 vers le haut du plan, sens des
aiguilles d'une montre (voir mzero/mode7.h).
"""

import math
import sys

PLANE = 1024
WAYPOINTS = [(505, -40), (560, 30), (705, 90), (860, 190), (980, 270),
             (1040, 340), (1074, 400), (1084, 480), (1099, 560), (1144, 630),
             (1224, 700), (1324, 790), (1384, 880), (1424, 950)]
LOOP_SHIFT = (PLANE, PLANE)  # le dernier point rejoint le premier, décalé
SPEED = 4.0                  # pixels du plan par image
LOOK_AHEAD = 48.0            # pixels
SAMPLES = 64                 # échantillons par tronçon, pour l'abscisse


def point(i):
    """Point de passage i, la boucle déroulée dans les deux sens."""
    n = len(WAYPOINTS)
    turns, k = divmod(i, n)
    x, y = WAYPOINTS[k]
    return (x + turns * LOOP_SHIFT[0], y + turns * LOOP_SHIFT[1])


def catmull_rom(i, t):
    p0, p1, p2, p3 = point(i - 1), point(i), point(i + 1), point(i + 2)
    return tuple(0.5 * (2 * b + (-a + c) * t + (2 * a - 5 * b + 4 * c - d) * t * t
                        + (-a + 3 * b - 3 * c + d) * t ** 3)
                 for a, b, c, d in zip(p0, p1, p2, p3))


def main():
    if len(sys.argv) != 2:
        sys.exit("usage : trajectoire.py SORTIE.c")

    # Courbe échantillonnée finement, avec son abscisse curviligne
    curve = [catmull_rom(i, k / SAMPLES)
             for i in range(len(WAYPOINTS)) for k in range(SAMPLES)]
    curve.append(point(len(WAYPOINTS)))
    length = [0.0]
    for a, b in zip(curve, curve[1:]):
        length.append(length[-1] + math.dist(a, b))
    total = length[-1]

    def at(s):
        """Point de la courbe à l'abscisse s (modulo un tour)."""
        turns, s = divmod(s, total)
        lo, hi = 0, len(length) - 1
        while hi - lo > 1:
            mid = (lo + hi) // 2
            if length[mid] <= s:
                lo = mid
            else:
                hi = mid
        f = (s - length[lo]) / (length[hi] - length[lo])
        x = curve[lo][0] + f * (curve[hi][0] - curve[lo][0])
        y = curve[lo][1] + f * (curve[hi][1] - curve[lo][1])
        return (x + turns * LOOP_SHIFT[0], y + turns * LOOP_SHIFT[1])

    frames = int(round(total / SPEED))
    step = total / frames
    rows = []
    for n in range(frames):
        x, y = at(n * step)
        ax, ay = at(n * step + LOOK_AHEAD)
        angle = math.atan2(ax - x, -(ay - y))  # 0 vers le haut, sens horaire
        rows.append((int(round(x * 8)) % (PLANE * 8),
                     int(round(y * 8)) % (PLANE * 8),
                     int(round(angle / (2 * math.pi) * 65536)) % 65536))

    with open(sys.argv[1], "w") as f:
        f.write("/* Produit par tools/trajectoire.py : ne pas modifier. */\n\n"
                "#include \"trajectoire.h\"\n\n")
        f.write("/* %d images : %.0f pixels a %.0f pixels par image */\n"
                % (frames, total, SPEED))
        f.write("const uint16_t trajectory_length = %d;\n\n" % frames)
        f.write("const TrajectoryPoint trajectory[] = {\n")
        for x, y, a in rows:
            f.write("    {%d, %d, %d},\n" % (x, y, a))
        f.write("};\n")
    print("%s : %d images, %.0f pixels" % (sys.argv[1], frames, total))


if __name__ == "__main__":
    main()
