#pragma once

/* Compiles this translation unit for speed instead of for size.
 *
 * ESPHome builds a firmware with -Os, on both frameworks: PlatformIO's espressif32
 * platform sets it in the board build, and the esp-idf side inherits
 * CONFIG_COMPILER_OPTIMIZATION_SIZE. That is the right default for a firmware,
 * which is mostly setup code and one-off handlers. It is the wrong default for
 * this component, which is a per-pixel loop run 43 times a second. WLED builds
 * its own effects for speed for the same reason.
 *
 * There is no way for an external component to change the optimisation level of
 * its own files alone through platformio.ini or the esp-idf CMake: ESPHome copies
 * every component into one `src/` tree and compiles it with one set of flags, so
 * `build_flags` and `build_src_flags` would change the whole firmware. What is
 * left is GCC's own per-function control, which is what this is: `#pragma GCC
 * optimize` sets the optimisation options for every function defined after it in
 * the translation unit, including the inline functions that come out of headers
 * included after it. Include this first in a wled_fx source file and that file,
 * and nothing else in the user's firmware, is built at -O2.
 *
 * It is included by every wled_fx source file rather than only the hot ones, and
 * that is deliberate. The engine's inline helpers live in headers, so each source
 * file emits its own copy of the ones it uses and the linker keeps one of them.
 * If half the files were -Os and half -O2, which copy survives would depend on
 * link order. All of them or none of them is the only answer that is the same
 * every build.
 *
 * WLED_FX_OPTIMIZE_SPEED is defined by codegen from the `optimize:` option, which
 * defaults to `speed`. `optimize: size` leaves it undefined and this header does
 * nothing, which is the setting for a board that is short of flash; README.md has
 * the cost. The host builds ignore it and take whatever CMake was told.
 *
 * Clang is excluded: it accepts the pragma name and ignores it with a warning,
 * and nothing here has ever been built with it for a device.
 */

#if defined(WLED_FX_OPTIMIZE_SPEED) && defined(__GNUC__) && !defined(__clang__) && !defined(WLED_FX_HOST_BUILD)
#pragma GCC optimize("O2")
#endif
