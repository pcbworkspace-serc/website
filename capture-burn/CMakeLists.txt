cmake_minimum_required(VERSION 3.13)
project(capture_burn VERSION 1.0 LANGUAGES C)

option(CB_BUILD_GAME "Build the SDL2 game (off for headless/cross test builds)" ON)
option(CB_SANITIZE "Build with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Determinism: never fuse a*b+c into FMA (aarch64 GCC does by default in
# GNU mode), never reassociate, and keep doubles at double precision.
include(CheckCCompilerFlag)
add_compile_options(-Wall -Wextra -ffp-contract=off -fno-fast-math -fno-math-errno)
# 32-bit Pi OS: 64-bit file offsets/inodes, or readdir() fails with EOVERFLOW.
add_compile_definitions(_FILE_OFFSET_BITS=64)
check_c_compiler_flag(-fexcess-precision=standard CB_HAS_EXCESS_PREC)
if(CB_HAS_EXCESS_PREC)
  add_compile_options(-fexcess-precision=standard)
endif()

if(CB_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
  add_link_options(-fsanitize=address,undefined)
endif()

# ---- deterministic core: no SDL, no libm --------------------------------
add_library(cbcore STATIC
  src/core/detmath.c
  src/core/rng.c
  src/core/sim.c
  src/core/replay.c
  src/core/bot.c)
target_include_directories(cbcore PUBLIC src/core)

# ---- tests & tools (headless; run on any arch, incl. under qemu) ---------
add_executable(cb-test tests/test_core.c)
target_link_libraries(cb-test cbcore m)

add_executable(cb-bench tools/bench.c)
target_link_libraries(cb-bench cbcore)

add_executable(cb-replay tools/replaytool.c)
target_link_libraries(cb-replay cbcore)

enable_testing()
add_test(NAME core COMMAND cb-test)
add_test(NAME golden COMMAND cb-replay verify ${CMAKE_CURRENT_SOURCE_DIR}/golden)

# ---- the game -----------------------------------------------------------
if(CB_BUILD_GAME)
  find_package(PkgConfig QUIET)
  if(PKG_CONFIG_FOUND)
    pkg_check_modules(SDL2 sdl2>=2.0.10)
  endif()
  if(NOT SDL2_FOUND)
    find_package(SDL2 REQUIRED CONFIG)
    set(SDL2_LIBRARIES SDL2::SDL2)
  endif()
  add_executable(capture-burn
    src/game/main.c
    src/game/render.c
    src/game/font.c
    src/game/audio.c)
  target_include_directories(capture-burn PRIVATE ${SDL2_INCLUDE_DIRS})
  target_link_libraries(capture-burn cbcore ${SDL2_LIBRARIES} m)

  include(GNUInstallDirs)
  configure_file(dist/capture-burn.desktop.in capture-burn.desktop @ONLY)
  install(TARGETS capture-burn DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/capture-burn.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
  install(FILES dist/capture-burn.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
endif()
