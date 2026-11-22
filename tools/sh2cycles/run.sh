#!/bin/sh
# =========================================================================
# Banc sh2cycles : mesure le temps de mzero/mode7.c compilé pour SH2, et
# étalonne le modèle de coûts du simulateur (mzero/cost.h).
#
# 1. Compile mzero/mode7.c pour SH2 (sh-elf-gcc -m2, -O2), avec le banc
#    bench.c, aux adresses de la 32X.
# 2. L'exécute sous qemu-sh4eb avec le greffon sh2cycles, qui applique le
#    modèle de temps du SH2 (cycles par instruction, branchements,
#    dépendances, cache et SDRAM). Un programme par CPU.
#    Cache : par défaut en 4 voies (4 Ko), tout en SDRAM (bench.ld) : le
#    mode 7 lit beaucoup de données (carte et tiles), il a besoin de tout
#    le cache. CACHE=ram : mode « 2 voies + RAM », le code de mode7.c dans
#    la RAM interne (bench_ram.ld), pour comparer.
# 3. Calcule sur l'hôte les compteurs du modèle pour les mêmes images
#    (dont les défauts de cache du modèle mzero/cachesim.h), vérifie que
#    le code SH2 dessine les mêmes pixels, et ajuste les coûts unitaires
#    (fit.py).
#
# Il faut (Linux ; qemu en mode utilisateur n'existe pas sous macOS) :
# - la chaîne sh-elf et qemu-sh4eb avec les greffons, construits par
#   build-tools.sh dans $PREFIX (par défaut ~/opt) ;
# - glib (en-têtes), Python 3 et numpy.
#
# Usage : tools/sh2cycles/run.sh   (depuis la racine du dépôt)
# =========================================================================
set -e

if [ "$(uname -s)" != "Linux" ]; then
  echo "run.sh : Linux uniquement. Sur Mac : tools/sh2cycles/docker.sh"
  exit 1
fi

PREFIX=${PREFIX:-$HOME/opt}
SH_ELF=${SH_ELF:-$PREFIX/sh-elf/bin/sh-elf-}
QEMU=${QEMU:-$PREFIX/qemu/bin/qemu-sh4eb}
QEMU_INCLUDE=${QEMU_INCLUDE:-$PREFIX/qemu/include}
for tool in "${SH_ELF}gcc" "$QEMU"; do
  if [ ! -x "$tool" ]; then
    echo "run.sh : $tool introuvable. Construire les outils :"
    echo "  tools/sh2cycles/build-tools.sh   (ou PREFIX=... si ailleurs)"
    exit 1
  fi
done
OUT=build/sh2cycles
T=tools/sh2cycles
mkdir -p "$OUT"

# Plan de la démo et tables, produits par les outils compilés pour l'hôte
cc -std=c99 -O2 -Imzero -o "$OUT/bmp2m7" tools/bmp2m7.c
"$OUT/bmp2m7" demo/assets/bigblue.bmp bigblue "$OUT/demo_assets" 2>/dev/null
cc -std=c99 -O2 -Imzero -o "$OUT/mktables" tools/mktables.c -lm
"$OUT/mktables" "$OUT/mode7_tables.c"

INC="-Ipixel -Imzero -Idemo -I$T -I$OUT"
DATA="$OUT/demo_assets.c $OUT/mode7_tables.c demo/trajectoire.c"

if [ "${CACHE:-4}" = ram ]; then
  WAYS=2
  LDSCRIPT="-T $T/bench_ram.ld -Wl,--defsym,RAM_BASE=0x50000000"
  PLUGIN_OPTIONS=",ways=2,ram=0x50000000"
  echo "Cache : 2 voies (2 Ko) + RAM interne (2 Ko) avec le code de mode7.c"
else
  WAYS=4
  LDSCRIPT="-T $T/bench.ld"
  PLUGIN_OPTIONS=""
  echo "Cache : 4 voies (4 Ko), tout le programme en SDRAM"
fi

# ---- Hôte : compteurs du modèle -------------------------------------------
cc -std=c99 -O2 -Wall $INC -o "$OUT/costs" $T/costs.c mzero/mode7.c \
  mzero/mode7_cost.c $DATA
"$OUT/costs" 0 $WAYS >"$OUT/costs_master.csv"
"$OUT/costs" 1 $WAYS >"$OUT/costs_slave.csv"

# ---- SH2 : le banc ----------------------------------------------------------
CFLAGS="-m2 -mb -O2 -std=c99 -ffreestanding -fno-builtin -Wall $INC"
DATA_OBJECTS=""
for src in $DATA; do
  obj="$OUT/$(basename "$src" .c).o"
  "${SH_ELF}gcc" $CFLAGS -c "$src" -o "$obj"
  DATA_OBJECTS="$DATA_OBJECTS $obj"
done
"${SH_ELF}gcc" -m2 -mb -Wa,--isa=sh2 -c $T/start.S -o "$OUT/start.o"
"${SH_ELF}gcc" $CFLAGS -c mzero/mode7.c -o "$OUT/mode7.o"
for cpu in master slave; do
  first=0
  [ $cpu = slave ] && first=1
  "${SH_ELF}gcc" $CFLAGS -DBENCH_FIRST_LINE=$first -c $T/bench.c \
    -o "$OUT/bench_$cpu.o"
  # Segments alignés sur 4 Ko : qemu (mode utilisateur Linux) les projette
  # en mémoire page par page.
  "${SH_ELF}gcc" -m2 -mb -nostdlib $LDSCRIPT -Wl,-z,max-page-size=0x1000 \
    -Wl,--no-warn-rwx-segments -o "$OUT/bench_$cpu.elf" \
    "$OUT/start.o" "$OUT/bench_$cpu.o" "$OUT/mode7.o" $DATA_OBJECTS -lgcc
done

# Pas d'appel à libgcc dans le code du moteur (division, décalage
# variable, multiplication 64 bits...)
if "${SH_ELF}nm" "$OUT/mode7.o" | grep -q ' U __'; then
  echo "mode7.o appelle libgcc :"
  "${SH_ELF}nm" "$OUT/mode7.o" | grep ' U __'
  exit 1
fi

# Taille en SDRAM (256 Ko sur 32X) : code + données + plan + pile ; et
# code en RAM interne (2 Ko au plus, vérifié par bench_ram.ld)
"${SH_ELF}size" -A "$OUT/bench_master.elf" | awk '
  $1 ~ /^\.(text|rodata|data|bss|stack)$/ { n += $2 }
  $1 == ".ram" { ram = $2 }
  END { printf "SDRAM occupee par le banc : %d octets sur 262144 (%.0f %%)\n", n, 100*n/262144
        if (ram) printf "Code en RAM interne : %d octets sur 2048\n", ram }'

# ---- Greffon qemu -----------------------------------------------------------
cc -O2 -shared -fPIC -Wall -I"$QEMU_INCLUDE" $(pkg-config --cflags glib-2.0) \
  -o "$OUT/libsh2cycles.so" $T/sh2cycles_plugin.c

# ---- Mesure -----------------------------------------------------------------
for cpu in master slave; do
  if ! "$QEMU" -plugin "$OUT/libsh2cycles.so$PLUGIN_OPTIONS" "$OUT/bench_$cpu.elf" \
    >"$OUT/prints_$cpu.txt" 2>"$OUT/cycles_$cpu.txt"; then
    echo "qemu : echec du banc $cpu :"
    grep -v '^sh2cycles;' "$OUT/cycles_$cpu.txt" | head -5
    exit 1
  fi
done

python3 $T/fit.py "$OUT"
