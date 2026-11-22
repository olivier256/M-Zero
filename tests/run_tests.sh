#!/bin/sh
# =========================================================================
# Tous les tests, depuis la racine du dépôt :  tests/run_tests.sh
#
# 1. test_mode7 : tables, paramètres de ligne contre la géométrie en
#    double, et dessin de près de 500 000 lignes au pixel près contre un
#    rendu de référence naïf (voir tests/test_mode7.c).
# 2. Le simulateur complet, sans fenêtre (deux CPU, VBlank, programme
#    68000 de la démo) : la manette suit un scénario, l'image capturée
#    doit être identique, octet pour octet, à l'image de référence
#    calculée par test_mode7 pour la caméra attendue.
#    Le simulateur tourne en temps réel : environ 20 secondes en tout.
# =========================================================================
set -e

BUILD=${BUILD:-build}
OUT=$BUILD/tests
mkdir -p "$OUT"

cmake -S . -B "$BUILD" >/dev/null
cmake --build "$BUILD" --target test_mode7 mzero_headless >/dev/null

echo "== mode 7"
"$BUILD/test_mode7"

# Valeurs de départ de la démo (demo/md68k.c)
HEIGHT=14336 # 56 pixels, en 8.8
HEIGHT_STEP=128
HORIZON=63

# scenario NOM IMAGES MANETTE CAPTURE POSITION HAUTEUR HORIZON
#   La manette met la démo en pause (B maintenu à partir de l'image P) :
#   la caméra reste sur la position P de la trajectoire (voir
#   demo/md68k.c), et la capture est prise une fois l'image dessinée.
scenario() {
  name=$1 frames=$2 pad=$3 shot=$4 position=$5 height=$6 horizon=$7
  padopt=""
  for step in $pad; do padopt="$padopt --pad $step"; done
  # shellcheck disable=SC2086
  "$BUILD/mzero_headless" --frames "$frames" $padopt \
    --shot "$shot:$OUT/$name.ppm" 2>"$OUT/$name.log"
  "$BUILD/test_mode7" --ppm "$position" "$height" "$horizon" \
    "$OUT/$name.ref.ppm"
  if cmp -s "$OUT/$name.ppm" "$OUT/$name.ref.ppm"; then
    echo "OK    $name (position $position, hauteur $height/256, horizon $horizon)"
  else
    echo "ECHEC $name : $OUT/$name.ppm differe de $OUT/$name.ref.ppm"
    exit 1
  fi
  grep -E "acces bus|cycles CPU|\] derniere image :" "$OUT/$name.log" |
    head -3 | sed 's/^/      /'
}

echo "== simulateur complet"
scenario depart 60 "0-59:B" 40 0 $HEIGHT $HORIZON
scenario virage 160 "100-159:B" 140 100 $HEIGHT $HORIZON
scenario rapide 160 "0-49:C 50-159:B" 140 100 $HEIGHT $HORIZON
# Haut pendant 40 images (de l'image 10 à 49) : +40 x 128 en 8.8
scenario haute 160 "10-49:U 120-159:B" 150 120 $((HEIGHT + 40 * HEIGHT_STEP)) $HORIZON
# Gauche pendant 30 images : horizon 30 lignes plus haut
scenario horizon 160 "10-39:L 120-159:B" 150 120 $HEIGHT $((HORIZON - 30))

echo "Tous les tests sont passes."
