cmake_minimum_required(VERSION 3.16)

project(MZero C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Threads REQUIRED)

# ---- Simulateur 32X « pixel » ----------------------------------------------
# VDP, interface (COMM, V_INT) et plates-formes : sans rapport avec M-Zero.

set(PIXEL_SOURCES pixel/vdp.c pixel/interface.c)

# Fenêtre : MiniFB (sous-module git). Sans lui, seules les cibles sans
# fenêtre sont construites (tests, captures).
set(HAVE_MINIFB OFF)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/minifb/CMakeLists.txt)
	enable_language(CXX)
	if(APPLE)
		enable_language(OBJC)
	endif()
	set(MINIFB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
	set(MINIFB_BUILD_TESTS OFF CACHE BOOL "" FORCE)
	add_subdirectory(minifb)
	set(HAVE_MINIFB ON)
else()
	message(STATUS "MiniFB absent (git submodule update --init) : pas de fenetre")
endif()

set(GENERATED ${CMAKE_CURRENT_BINARY_DIR}/generated)

# ---- Tables du mode 7 (sinus, inverses) ------------------------------------

add_executable(mktables tools/mktables.c)
target_include_directories(mktables PRIVATE mzero)
target_compile_options(mktables PRIVATE -Wall -Wextra)
find_library(MATH_LIBRARY m)
if(MATH_LIBRARY)
	target_link_libraries(mktables PRIVATE ${MATH_LIBRARY})
endif()

add_custom_command(
	OUTPUT ${GENERATED}/mode7_tables.c
	COMMAND ${CMAKE_COMMAND} -E make_directory ${GENERATED}
	COMMAND mktables ${GENERATED}/mode7_tables.c
	DEPENDS mktables
	COMMENT "Tables du mode 7"
)

# ---- Outil : BMP 8 bits -> plan de mode 7 en C -----------------------------

add_executable(bmp2m7 tools/bmp2m7.c)
target_include_directories(bmp2m7 PRIVATE mzero)
target_compile_options(bmp2m7 PRIVATE -Wall -Wextra)

set(DEMO_BMP ${CMAKE_CURRENT_SOURCE_DIR}/demo/assets/bigblue.bmp)

add_custom_command(
	OUTPUT ${GENERATED}/demo_assets.c ${GENERATED}/demo_assets.h
	COMMAND ${CMAKE_COMMAND} -E make_directory ${GENERATED}
	COMMAND bmp2m7 ${DEMO_BMP} bigblue ${GENERATED}/demo_assets
	DEPENDS bmp2m7 ${DEMO_BMP}
	COMMENT "Conversion du plan de la demo"
)

# Données de la démo et tables, partagées par les exécutables
add_library(mzero_data STATIC ${GENERATED}/demo_assets.c
	${GENERATED}/mode7_tables.c demo/trajectoire.c)
target_include_directories(mzero_data PUBLIC mzero demo ${GENERATED})

# ---- M-Zero + démo ---------------------------------------------------------

set(MZERO_SOURCES
	mzero/fzero.c
	mzero/mode7.c
	mzero/mode7_cost.c
	mzero/framebuffer.c
	demo/content.c
	demo/md68k.c
	${PIXEL_SOURCES}
)
set(MZERO_INCLUDES pixel mzero demo ${GENERATED})

# Avec fenêtre
if(HAVE_MINIFB)
	add_executable(mzero ${MZERO_SOURCES} pixel/platform_minifb.c)
	target_include_directories(mzero PRIVATE ${MZERO_INCLUDES})
	target_compile_options(mzero PRIVATE -Wall -Wextra)
	target_link_libraries(mzero PRIVATE mzero_data minifb Threads::Threads)
	set_target_properties(mzero PROPERTIES LINKER_LANGUAGE CXX)
endif()

# Sans fenêtre : tests et captures (voir pixel/platform_headless.c)
add_executable(mzero_headless ${MZERO_SOURCES} pixel/platform_headless.c)
target_include_directories(mzero_headless PRIVATE ${MZERO_INCLUDES})
target_compile_options(mzero_headless PRIVATE -Wall -Wextra)
target_link_libraries(mzero_headless PRIVATE mzero_data Threads::Threads)

# ---- Tests -----------------------------------------------------------------
# test_mode7 relit le BMP lui-même : sa référence ne doit rien à bmp2m7.

add_executable(test_mode7 tests/test_mode7.c mzero/mode7.c)
target_include_directories(test_mode7 PRIVATE ${MZERO_INCLUDES})
target_compile_options(test_mode7 PRIVATE -Wall -Wextra)
target_compile_definitions(test_mode7 PRIVATE
	DEMO_BMP_PATH="${DEMO_BMP}")
target_link_libraries(test_mode7 PRIVATE mzero_data)
if(MATH_LIBRARY)
	target_link_libraries(test_mode7 PRIVATE ${MATH_LIBRARY})
endif()

enable_testing()
add_test(NAME mode7 COMMAND test_mode7)
