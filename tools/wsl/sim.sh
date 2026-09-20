#!/usr/bin/env bash
# Build and run the host simulator inside WSL.
#
#   sim.sh sweep            the whole CI sweep, sanitizers on, exactly what CI runs
#   sim.sh build            configure and build only
#   sim.sh run [args...]    one run, arguments passed straight to wled_fx_sim
#   sim.sh audio            the audio pipeline test
#   sim.sh effect           the effect and control behaviour test
#   sim.sh clean            throw the build directories away
#
# WLED_FX_SANITIZE=OFF turns ASan and UBSan off for a `run`; `sweep` always runs
# the sanitized build for the small geometries and the plain one for the large.
#
# The source stays on /mnt/c and only the build directory is on the Linux
# filesystem, so there is one copy of the repo and edits on Windows are picked
# up with no sync step.

set -euo pipefail

REPO="${WFX_REPO:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
WFX_HOME="${WFX_HOME:-/root/wfx}"
OUT="${WFX_OUT:-$WFX_HOME/out}"
SANITIZE="${WLED_FX_SANITIZE:-ON}"

BUILD_ASAN="$WFX_HOME/sim-asan"
BUILD_PLAIN="$WFX_HOME/sim-plain"

jobs="$(nproc)"

configure_and_build() {
  local dir="$1" sanitize="$2"
  cmake -S "$REPO/tools/sim" -B "$dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DWLED_FX_SANITIZE="$sanitize" >/dev/null
  cmake --build "$dir" -j "$jobs"
}

cmd="${1:-sweep}"
shift || true

case "$cmd" in
  clean)
    rm -rf "$BUILD_ASAN" "$BUILD_PLAIN" "$OUT"
    echo "removed $BUILD_ASAN, $BUILD_PLAIN and $OUT"
    ;;

  build)
    configure_and_build "$BUILD_ASAN" "$SANITIZE"
    echo "built $BUILD_ASAN"
    ;;

  run)
    configure_and_build "$BUILD_ASAN" "$SANITIZE"
    mkdir -p "$OUT"
    "$BUILD_ASAN/wled_fx_sim" --out "$OUT" "$@"
    ;;

  audio)
    configure_and_build "$BUILD_ASAN" "$SANITIZE"
    "$BUILD_ASAN/wled_fx_audio_test" "$@"
    ;;

  effect)
    configure_and_build "$BUILD_ASAN" "$SANITIZE"
    "$BUILD_ASAN/wled_fx_effect_test" "$@"
    ;;

  sweep)
    mkdir -p "$OUT"
    echo "=== building, sanitizers on ==="
    configure_and_build "$BUILD_ASAN" ON
    echo "=== registered effects and their parsed defaults ==="
    "$BUILD_ASAN/wled_fx_sim" --list >/dev/null
    echo "=== every effect at every default geometry ==="
    "$BUILD_ASAN/wled_fx_sim" --no-images --out "$OUT"
    echo "=== every effect at the smallest geometries ==="
    for size in 1x1 1x2 2x1 3x1; do
      "$BUILD_ASAN/wled_fx_sim" --size "$size" --no-images --out "$OUT"
    done
    echo "=== every effect through every 1D to 2D mapping mode ==="
    for map in 0 1 2 3 4; do
      "$BUILD_ASAN/wled_fx_sim" --map "$map" --single-pass --no-images --out "$OUT"
    done
    echo "=== audio pipeline test ==="
    "$BUILD_ASAN/wled_fx_audio_test"
    echo "=== effect and control behaviour tests ==="
    "$BUILD_ASAN/wled_fx_effect_test"
    # A 256x64 frame is sixteen times a 64x64 one, which under the sanitizers is
    # hours rather than minutes. The canvas guard bands cover it instead, which
    # is the same split CI makes between its two jobs.
    echo "=== building, sanitizers off, for the large geometries ==="
    configure_and_build "$BUILD_PLAIN" OFF
    for size in 128x64 256x64 1000x1; do
      "$BUILD_PLAIN/wled_fx_sim" --size "$size" --no-images --out "$OUT"
    done
    echo "=== python side ==="
    python3 "$REPO/tools/compare/test_metrics.py"
    python3 "$REPO/tools/gen_readme_effects.py" --check
    # Against the build that was just made, not the stale wled_fx_sim.exe left in
    # the checkout, which WSL would happily run through binfmt interop.
    WLED_FX_SIM="$BUILD_ASAN/wled_fx_sim" python3 "$REPO/tools/check_effect_names.py"
    echo
    echo "SWEEP OK"
    ;;

  *)
    echo "unknown command '$cmd'. One of: sweep, build, run, audio, effect, clean" >&2
    exit 2
    ;;
esac
