#!/bin/sh
# Build, verify and install Capture Burn on the cyberdeck.
#
# Works on Raspberry Pi OS Bookworm or Trixie, 32- or 64-bit, on a Pi 3/4/5
# Model B. Needs: build-essential, cmake, pkg-config, libsdl2-dev (installed
# below if missing).
#
#   sh scripts/install-pi.sh              build, test, install to ~/.local
#   sh scripts/install-pi.sh --autostart  also launch the game at login
#   PREFIX=/usr/local sudo -E sh scripts/install-pi.sh   system-wide
set -eu
cd "$(dirname "$0")/.."

PREFIX="${PREFIX:-$HOME/.local}"
AUTOSTART=0
for a in "$@"; do
    case "$a" in
        --autostart) AUTOSTART=1 ;;
        *) echo "unknown option: $a" >&2; exit 2 ;;
    esac
done

need=""
command -v cmake >/dev/null 2>&1 || need="$need cmake"
command -v cc >/dev/null 2>&1 || need="$need build-essential"
command -v pkg-config >/dev/null 2>&1 || need="$need pkg-config"
if ! pkg-config --exists sdl2 2>/dev/null; then need="$need libsdl2-dev"; fi
if [ -n "$need" ]; then
    echo "Installing:$need (needs sudo)"
    sudo apt-get update
    # shellcheck disable=SC2086
    sudo apt-get install -y $need
fi

echo "== Building ($(uname -m), SDL2 $(pkg-config --modversion sdl2))"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX"
cmake --build build -j"$(nproc)"

echo "== Verifying on this device"
echo "   Physics tests, then 10 golden replays recorded on another machine."
echo "   Matching hashes prove this deck runs the exact same simulation."
echo "   (About a minute on a Pi 4, longer on a Pi 3.)"
(cd build && ctest --output-on-failure)

echo "== Installing to $PREFIX"
cmake --install build

if [ "$AUTOSTART" = 1 ]; then
    mkdir -p "$HOME/.config/autostart"
    cp "$PREFIX/share/applications/capture-burn.desktop" "$HOME/.config/autostart/"
    echo "Autostart enabled: remove ~/.config/autostart/capture-burn.desktop to undo."
fi

echo
echo "Done. Launch it from the Games menu, or run: $PREFIX/bin/capture-burn"
echo "Measure frame times on this deck:          $PREFIX/bin/capture-burn --bench 60"
echo "Without a desktop (from a console/TTY):    SDL_VIDEODRIVER=kmsdrm $PREFIX/bin/capture-burn"
