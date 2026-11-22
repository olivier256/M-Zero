# Raccourcis ; la compilation elle-même est décrite dans CMakeLists.txt.
.PHONY: all clean rebuild run stats headless test bench

all:
	cmake -S . -B build
	cmake --build build

clean:
	cmake -E rm -rf build

rebuild: clean all

# La démo, dans une fenêtre
run: all
	./build/mzero

# La même, avec les compteurs (toutes les 5 secondes, sur le terminal)
stats: all
	VDP_STATS=1 ./build/mzero

# Sans fenêtre : 5 secondes, capture de la dernière image
headless: all
	./build/mzero_headless --frames 300 --shot 299:build/capture.ppm

# Tous les tests (environ 15 secondes)
test:
	tests/run_tests.sh

# Banc SH2 (Linux, outils construits par tools/sh2cycles/build-tools.sh)
bench:
	tools/sh2cycles/run.sh
