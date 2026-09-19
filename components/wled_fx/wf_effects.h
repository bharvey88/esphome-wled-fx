#pragma once

/* Shared prologue for every effect translation unit. Effect files include this and
 * nothing else from the engine. See PORTING.md for the transform rules. */

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "wf_audio.h"
#include "wf_color.h"
#include "wf_font.h"
#include "wf_fx_shared.h"
#include "wf_math.h"
#include "wf_palettes.h"
#include "wf_registry.h"
#include "wf_segment.h"

// Compile-time effect selection. ESPHome codegen defines WLED_FX_ALL_EFFECTS when
// the YAML asks for every effect (the default), or WLED_FX_FX_<NAME>=1 for each
// effect on the allow-list. An undefined macro evaluates to 0 inside #if, so the
// guard below needs no `defined()`.
#ifdef WLED_FX_ALL_EFFECTS
#define WLED_FX_DEFAULT_ENABLE 1
#else
#define WLED_FX_DEFAULT_ENABLE 0
#endif

// WLED's early-out for effects that cannot run on the current geometry. Kept as a
// macro so effect bodies stay byte-for-byte comparable with FX.cpp.
#define FX_FALLBACK_STATIC \
  { \
    seg.fill(seg.color(0)); \
    return; \
  }
