/* Effect bodies ported from WLED 16.0.1 wled00/FX.cpp with the mechanical
 * transform described in PORTING.md.
 *
 * Copyright (c) 2016 Harm Aldick.
 * Copyright (c) 2016-present Christian Schwinne and individual WLED contributors.
 * Licensed under the EUPL v. 1.2 or later, distributed here under GPLv3 per the
 * EUPL Article 5 compatibility clause.
 * Adapted from code originally licensed under the MIT license.
 *
 * Per-effect credits are kept on the effect they belong to.
 */

/* Particle system 1D effects, non-audio. See the "Particle effects" section of
 * PORTING.md for the init pattern and the pitfalls. */

#include "wf_effects.h"
#include "wf_particle.h"

// Whole-file guard. Every WLED_FX_FX_* macro this file can provide goes in the
// list, one per effect, so a YAML allow-list that names any of them links this
// translation unit in.
#define WLED_FX_GROUP_PARTICLE_1D (WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_DRIPDROP)

#if WLED_FX_GROUP_PARTICLE_1D

namespace esphome {
namespace wled_fx {
namespace {

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_DRIPDROP
/*
  Particle version of Drip and Rain
  Uses palette for particle color
  by DedeHai (Damian Schneider)
*/
void mode_particleDrip(Segment &seg) {
  ParticleSystem1D *PartSys = nullptr;
  if (seg.call == 0) {  // initialization
    if (!initParticleSystem1D(seg, PartSys, 4))
      FX_FALLBACK_STATIC;             // allocation failed or single pixel
    PartSys->setKillOutOfBounds(true);  // out of bounds particles dont return, gravity takes care of the top
    PartSys->sources[0].source.hue = hw_random16();
    seg.aux1 = 0xFFFF;  // invalidate
  } else {
    PartSys = reinterpret_cast<ParticleSystem1D *>(seg.data);  // if not the first call, just set the pointer
  }

  if (PartSys == nullptr)
    FX_FALLBACK_STATIC;  // something went wrong, no data!

  // Particle System settings
  PartSys->updateSystem();  // update system properties (dimensions and data pointers)
  PartSys->setBounce(true);
  PartSys->setWallHardness(50);

  PartSys->setMotionBlur(seg.custom2);        // enable motion blur
  PartSys->setGravity(seg.custom3 >> 1);      // set gravity (8 is default strength)
  PartSys->setParticleSize(seg.check3);       // 1 or 2 pixel rendering

  if (seg.check2) {  // collisions enabled
    PartSys->enableParticleCollisions(true);  // enable, full hardness
  } else {
    PartSys->enableParticleCollisions(false);
  }

  PartSys->sources[0].sourceFlags.collide = false;  // drops do not collide

  if (seg.check1) {            // rain mode, emit at random position, short life
    if (seg.custom1 == 0)      // splash disabled, do not bounce raindrops
      PartSys->setBounce(false);
    PartSys->sources[0].var = 5;
    PartSys->sources[0].v = -(8 + (seg.speed >> 2));  // speed + var must be < 128, inverted speed (=down)
    // lifetime in frames
    PartSys->sources[0].minLife = 30;
    PartSys->sources[0].maxLife = 200;
    PartSys->sources[0].source.x = hw_random(PartSys->maxX);  // random emit position
  } else {                                                    // drip
    PartSys->sources[0].var = 0;
    PartSys->sources[0].v = -(seg.speed >> 1);  // speed + var must be < 128, inverted speed (=down)
    PartSys->sources[0].minLife = 3000;
    PartSys->sources[0].maxLife = 3000;
    PartSys->sources[0].source.x = PartSys->maxX - PS_P_RADIUS_1D;
  }

  if (seg.aux1 != seg.intensity)  // slider changed
    seg.aux0 = 1;                 // must not be zero or "% 0" happens below

  seg.aux1 = seg.intensity;  // save state

  // every nth frame emit a particle
  if (seg.call % seg.aux0 == 0) {
    int32_t interval = 300 / ((seg.intensity) + 1);
    seg.aux0 = interval + hw_random(interval + 5);
    PartSys->sources[0].source.hue = hw_random8();  // set random color
    PartSys->sprayEmit(PartSys->sources[0]);
  }

  for (uint32_t i = 0; i < PartSys->usedParticles; i++) {  // check all particles
    if (PartSys->particles[i].ttl) {
      if (PartSys->particleFlags[i].collide == false) {  // use the collision flag to identify splash particles
        if (PartSys->particles[i].x < (PS_P_RADIUS_1D << 1)) {  // reached bottom
          if (PartSys->particles[i].ttl > 120)                  // short life: make the drop fade out and die
            PartSys->particles[i].ttl = 120;
          if (seg.custom1 > 0) {                 // splash enabled
            PartSys->particles[i].ttl = 0;       // kill the drop particle, replace it with a splash
            PartSys->sources[0].maxLife = 160;
            PartSys->sources[0].minLife = 40;
            PartSys->sources[0].var = 10 + (seg.custom1 >> 3);
            PartSys->sources[0].v = 0;
            PartSys->sources[0].source.hue = PartSys->particles[i].hue;
            PartSys->sources[0].source.x = PS_P_RADIUS_1D;
            PartSys->sources[0].sourceFlags.collide = true;  // splashes do collide if enabled
            for (int j = 0; j < 2 + (seg.custom1 >> 2); j++) {
              PartSys->sprayEmit(PartSys->sources[0]);
            }
          }
        }
      } else {
        PartSys->particles[i].ttl--;  // age splash particles faster (allows for higher splash brightness)
      }
    }

    if (seg.check1) {  // rain mode, fade hue to max
      if (PartSys->particles[i].hue < 245)
        PartSys->particles[i].hue += 8;
    }
    // increase speed on high settings by calling the move function twice. this can lead to missed collisions
    if (seg.speed > 200)
      PartSys->particleMoveUpdate(PartSys->particles[i], PartSys->particleFlags[i]);
  }

  PartSys->update();  // update and render
}
#endif

const EffectInfo ENTRIES[] = {
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_DRIPDROP
    {"PS DripDrop@Speed,!,Splash,Blur,Gravity,Rain,PushSplash,Smooth;,!;!;1;pal=0,sx=150,ix=25,c1=220,c2=30,c3=21",
     mode_particleDrip},
#endif
};
constexpr size_t ENTRY_COUNT = sizeof(ENTRIES) / sizeof(ENTRIES[0]);

}  // namespace

// extern first: a bare `const` at namespace scope has internal linkage and the
// generated group table would not be able to see it.
extern const EffectGroup EFFECT_GROUP_PARTICLE_1D;
const EffectGroup EFFECT_GROUP_PARTICLE_1D{"particle_1d", ENTRIES, ENTRY_COUNT};

}  // namespace wled_fx
}  // namespace esphome

#endif  // WLED_FX_GROUP_PARTICLE_1D
