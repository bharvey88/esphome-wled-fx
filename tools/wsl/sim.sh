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

# The Python checks need numpy and Pillow, which the distribution's own python3
# does not have; the ESPHome virtualenv bootstrap.sh builds does. Prefer it and
# fall back to python3, which is what CI has, with the packages installed.
PY="$WFX_HOME/esphome-venv/bin/python"
[ -x "$PY" ] || PY="python3"

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

  bench)
    # Sanitizers off and optimisation on, because this is a timing measurement.
    configure_and_build "$BUILD_PLAIN" OFF
    # In the repository, so --write and --check take a path the way somebody
    # reading the command would expect.
    cd "$REPO"
    "$BUILD_PLAIN/wled_fx_bench" "$@"
    ;;

  golden)
    configure_and_build "$BUILD_PLAIN" OFF
    "$BUILD_PLAIN/wled_fx_bench" --check "$REPO/tools/sim/golden.txt" "$@"
    ;;

  optbench)
    # The measurement behind the `optimize:` option: the same engine built at
    # -Os, which is what ESPHome compiles a firmware with, and at -O2, which is
    # what `optimize: speed` asks the hot translation units for. Two build
    # directories so neither can reuse the other's objects.
    for level in Os O2; do
      dir="$WFX_HOME/sim-$level"
      cmake -S "$REPO/tools/sim" -B "$dir" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_CXX_FLAGS_RELEASE="-$level -DNDEBUG" \
        -DWLED_FX_SANITIZE=OFF >/dev/null
      cmake --build "$dir" -j "$jobs" --target wled_fx_bench >/dev/null
    done
    echo "=== -Os ==="
    "$WFX_HOME/sim-Os/wled_fx_bench" --repeats 3 "$@" | tail -5
    echo "=== -O2 ==="
    "$WFX_HOME/sim-O2/wled_fx_bench" --repeats 3 "$@" | tail -5
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
    echo "=== golden frames ==="
    "$BUILD_ASAN/wled_fx_bench" --check "$REPO/tools/sim/golden.txt" | tail -3
    # A 256x64 frame is sixteen times a 64x64 one, which under the sanitizers is
    # hours rather than minutes. The canvas guard bands cover it instead, which
    # is the same split CI makes between its two jobs.
    echo "=== building, sanitizers off, for the large geometries ==="
    configure_and_build "$BUILD_PLAIN" OFF
    for size in 128x64 256x64 1000x1; do
      "$BUILD_PLAIN/wled_fx_sim" --size "$size" --no-images --out "$OUT"
    done
    echo "=== python side ==="
    "$PY" "$REPO/tools/compare/test_metrics.py"
    "$PY" "$REPO/tools/gen_readme_effects.py" --check
    # Against the build that was just made, not the stale wled_fx_sim.exe left in
    # the checkout, which WSL would happily run through binfmt interop.
    WLED_FX_SIM="$BUILD_ASAN/wled_fx_sim" "$PY" "$REPO/tools/check_effect_names.py"
    echo
    echo "SWEEP OK"
    ;;

  *)
    echo "unknown command '$cmd'. One of: sweep, build, run, audio, effect, bench, golden, optbench, clean" >&2
    exit 2
    ;;
esac
