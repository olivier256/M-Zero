#!/usr/bin/env python3
"""
Prépare le plan de mode 7 de la démo : un crop de 1024 x 1024 pixels du
circuit Big Blue (F-Zero, SNES), recalé sur la grille des tiles 8 x 8
d'origine, enregistré en BMP 8 bits indexé pour tools/bmp2m7.

    python3 tools/crop2bmp.py demo/assets/source/bigblue_1024.png \\
        demo/assets/bigblue.bmp

Il faut Pillow (pip install pillow). Le script ne sert qu'à refaire le BMP
si l'image source change : le BMP produit est dans le dépôt.

Recalage. Le crop source n'est pas aligné sur la grille des tiles du
circuit : découpé tel quel, il donne 276 tiles différentes. Le script
cherche le décalage (0 à 7 pixels dans chaque direction) qui en donne le
moins : (1, 3) pour la démo, soit 110 tiles. Le plan commence donc au
pixel (1, 3) de la source ; la dernière colonne et les 3 dernières lignes,
absentes de la source, sont complétées tile par tile : la dernière rangée
et la dernière colonne de tiles reprennent, parmi les tiles complètes, la
plus fréquente qui s'accorde avec les pixels connus.

Palette : index 0 réservé (transparent : la Mega Drive dessine le ciel ;
le simulateur l'affiche avec la couleur SKY). Les couleurs du circuit
suivent, triées.
"""

import sys
from collections import Counter

from PIL import Image

SIZE = 1024
TILE = 8
SKY = (96, 160, 248)  # index 0, vu seulement dans le simulateur


def tiles_of(pixels, ox, oy, width, height):
    """Tiles complètes de la grille décalée de (ox, oy)."""
    found = []
    for ty in range((height - oy) // TILE):
        for tx in range((width - ox) // TILE):
            found.append(tuple(pixels[(oy + ty * TILE + y) * width +
                                      ox + tx * TILE + x]
                               for y in range(TILE) for x in range(TILE)))
    return found


def main():
    if len(sys.argv) != 3:
        sys.exit("usage : crop2bmp.py SOURCE.png SORTIE.bmp")
    src = Image.open(sys.argv[1]).convert("RGB")
    w, h = src.size
    raw = src.tobytes()
    pixels = [tuple(raw[i:i + 3]) for i in range(0, len(raw), 3)]

    # Décalage qui donne le moins de tiles différentes
    best = None
    for oy in range(TILE):
        for ox in range(TILE):
            n = len(set(tiles_of(pixels, ox, oy, w, h)))
            if best is None or n < best[0]:
                best = (n, ox, oy)
    n, ox, oy = best
    print("decalage (%d, %d) : %d tiles completes differentes" % (ox, oy, n))

    known = Counter(tiles_of(pixels, ox, oy, w, h))

    def source(x, y):
        """Pixel (x, y) du plan, ou None s'il manque dans la source."""
        sx, sy = x + ox, y + oy
        return pixels[sy * w + sx] if sx < w and sy < h else None

    out = [[None] * SIZE for _ in range(SIZE)]
    for ty in range(SIZE // TILE):
        for tx in range(SIZE // TILE):
            cells = [source(tx * TILE + x, ty * TILE + y)
                     for y in range(TILE) for x in range(TILE)]
            if None in cells:
                # Tile incomplète : la tile connue la plus fréquente qui
                # donne les mêmes pixels là où la source en a
                matches = [(c, t) for t, c in known.items()
                           if all(p is None or p == q
                                  for p, q in zip(cells, t))]
                if not matches:
                    sys.exit("tile (%d, %d) : aucune tile ne convient"
                             % (tx, ty))
                cells = max(matches)[1]
            for i, p in enumerate(cells):
                out[ty * TILE + i // TILE][tx * TILE + i % TILE] = p

    colors = sorted({p for row in out for p in row})
    if len(colors) > 255:
        sys.exit("plus de 255 couleurs")
    palette = [SKY] + colors
    index = {c: i for i, c in enumerate(palette)}

    img = Image.new("P", (SIZE, SIZE))
    img.putdata([index[p] for row in out for p in row])
    flat = [v for c in palette for v in c]
    img.putpalette(flat + [0] * (768 - len(flat)))
    img.save(sys.argv[2])
    print("%s : %d x %d, %d couleurs (index 0 = ciel)"
          % (sys.argv[2], SIZE, SIZE, len(palette)))


if __name__ == "__main__":
    main()
