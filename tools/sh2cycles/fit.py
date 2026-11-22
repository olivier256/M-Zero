#!/usr/bin/env python3
"""
Banc sh2cycles : relie le temps mesuré sur le code SH2 (greffon qemu) aux
compteurs du modèle de coûts (mzero/cost.h), et en déduit les coûts
unitaires par moindres carrés.

    python3 fit.py DOSSIER

DOSSIER contient, pour CPU = master et slave :
    costs_CPU.csv   compteurs du modèle (programme hôte costs)
    cycles_CPU.txt  temps mesurés (greffon : lignes « sh2cycles;... »)
    prints_CPU.txt  empreintes des images dessinées par le code SH2

Modèle : cycles = CYCLES_FRAME + lignes x CYCLES_LINE
                  + défauts de cache x CYCLES_MISS
Une ligne fait toujours 320 pixels : le coût d'un pixel est compris dans
celui de la ligne. Les défauts de cache sont ceux du modèle de l'hôte
(cachesim.h), comparés ici à ceux que compte le greffon.
"""

import sys
import numpy as np

FITTED = [("one", "CYCLES_FRAME"), ("lines", "CYCLES_LINE"),
          ("misses", "CYCLES_MISS")]
WORDS_PER_LINE = 160
FRAME_CYCLES = 384000
TRAJECTORY_FRAMES = 96  # voir frames.h


def load(folder, cpu):
    costs = []
    for line in open("%s/costs_%s.csv" % (folder, cpu)):
        p = line.strip().split(";")
        costs.append({"one": 1, "lines": int(p[1]), "misses": int(p[2]),
                      "model": int(p[3]), "print": p[4]})
    measured = []
    for line in open("%s/cycles_%s.txt" % (folder, cpu)):
        if line.startswith("sh2cycles;"):
            p = line.strip().split(";")
            measured.append({"cycles": int(p[2]), "insns": int(p[3]),
                             "misses": int(p[4]), "fb": int(p[5]),
                             "codeMisses": int(p[6]) if len(p) > 6 else 0})
    prints = [l.strip() for l in open("%s/prints_%s.txt" % (folder, cpu))]
    if not (len(costs) == len(measured) == len(prints)):
        sys.exit("%s : nombres d'images differents (%d, %d, %d)"
                 % (cpu, len(costs), len(measured), len(prints)))
    bad = [i for i in range(len(costs)) if costs[i]["print"] != prints[i]]
    if bad:
        sys.exit("%s : le code SH2 ne dessine pas comme l'hote (images %s)"
                 % (cpu, bad[:10]))
    for i, m in enumerate(measured):
        expected = costs[i]["lines"] * WORDS_PER_LINE
        if m["fb"] != expected:
            sys.exit("%s : image %d, %d ecritures FB au lieu de %d"
                     % (cpu, i, m["fb"], expected))
    return costs, measured


def summary(label, cycles, fb, lines):
    print("  %s : moyenne %d, pire %d cycles ; %.0f cycles par ligne, "
          "%.1f par pixel"
          % (label, cycles.mean(), cycles.max(), (cycles / lines).mean(),
             (cycles / lines).mean() / 320))
    for wlabel, per in (("3", 3), ("5", 5)):
        total = cycles + fb * per
        print("      + ecritures FB a %s cycles : pire %d / %d = %.2f image(s) "
              "de 1/60 s" % (wlabel, total.max(), FRAME_CYCLES,
                             total.max() / FRAME_CYCLES))


def main():
    folder = sys.argv[1] if len(sys.argv) > 1 else "."
    rows, ys = [], []
    for cpu in ("master", "slave"):
        costs, measured = load(folder, cpu)
        print("%s : %d images, empreintes identiques a l'hote, 160 ecritures "
              "FB par ligne" % (cpu, len(costs)))
        rows += costs
        ys += measured

    cycles = np.array([m["cycles"] for m in ys], dtype=float)
    insns = np.array([m["insns"] for m in ys], dtype=float)
    fb = np.array([m["fb"] for m in ys], dtype=float)
    lines = np.array([r["lines"] for r in rows], dtype=float)
    misses = np.array([m["misses"] for m in ys], dtype=float)
    codeMisses = np.array([m["codeMisses"] for m in ys], dtype=float)
    modelMisses = np.array([r["misses"] for r in rows], dtype=float)
    oldModel = np.array([r["model"] for r in rows], dtype=float)
    n = len(rows) // 2
    traj = np.array([(i % n) < TRAJECTORY_FRAMES for i in range(len(rows))])

    print()
    print("Mesure SH2 (code compile, cache simule), par CPU et par image,")
    print("hors ecritures dans le frame buffer :")
    print("  instructions : %.1f par pixel en moyenne (%d par image)"
          % ((insns / lines).mean() / 320, insns.mean()))
    print("  defauts de cache : moyenne %d, dont instructions %d ; modele de "
          "l'hote (donnees) : moyenne %d"
          % (misses.mean(), codeMisses.mean(), modelMisses.mean()))
    dataMisses = misses - codeMisses
    print("      donnees mesurees / modele : %.2f (de %.2f a %.2f)"
          % (dataMisses.sum() / max(modelMisses.sum(), 1),
             (dataMisses / np.maximum(modelMisses, 1)).min(),
             (dataMisses / np.maximum(modelMisses, 1)).max()))
    summary("trajectoire de la demo (80 lignes)", cycles[traj], fb[traj],
            lines[traj])
    summary("cameras quelconques", cycles[~traj], fb[~traj], lines[~traj])

    print()
    err = (oldModel - cycles) / cycles
    print("Modele actuel (cost.h) : ecart moyen %+.1f %%, de %+.1f %% a "
          "%+.1f %%" % (100 * err.mean(), 100 * err.min(), 100 * err.max()))

    names = [f for f, _ in FITTED]
    A = np.array([[r[f] for f in names] for r in rows], dtype=float)
    # Moindres carrés à coûts positifs ou nuls : un coût négatif est fixé
    # à 0 et l'on ajuste à nouveau les autres.
    active = list(range(len(names)))
    while True:
        sub, *_ = np.linalg.lstsq(A[:, active], cycles, rcond=None)
        if (sub >= 0).all():
            break
        active = [j for j, k in zip(active, sub) if k >= 0]
    coef = np.zeros(len(names))
    coef[active] = sub
    err = (A @ coef - cycles) / cycles
    print("Nouveau modele (moindres carres, couts >= 0) : ecart moyen %+.2f %%, "
          "de %+.2f %% a %+.2f %%, ecart-type %.2f %%"
          % (100 * err.mean(), 100 * err.min(), 100 * err.max(),
             100 * err.std()))
    print()
    print("Couts unitaires ajustes (cycles) :")
    for (field, const), k in zip(FITTED, coef):
        print("  #define %-14s %d   /* ajuste : %.2f */" % (const, round(k), k))
    rounded = np.round(coef)
    err = (A @ rounded - cycles) / cycles
    print("Avec les valeurs arrondies : ecart de %+.2f %% a %+.2f %%"
          % (100 * err.min(), 100 * err.max()))


if __name__ == "__main__":
    main()
