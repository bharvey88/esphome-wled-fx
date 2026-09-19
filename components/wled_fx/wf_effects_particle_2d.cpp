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

/* Particle system 2D effects, non-audio. See the "Particle effects" section of
 * PORTING.md for the init pattern and the pitfalls. */

#include "wf_effects.h"
#include "wf_particle.h"

// Whole-file guard. Every WLED_FX_FX_* macro this file can provide goes in the
// list, one per effect, so a YAML allow-list that names any of them links this
// translation unit in.
#define WLED_FX_GROUP_PARTICLE_2D \
  (WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_FIREWORKS || WLED_FX_FX_PS_FIRE)

#if WLED_FX_GROUP_PARTICLE_2D

namespace esphome {
namespace wled_fx {
namespace {

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_FIREWORKS
/*
  Particle Fireworks
  Rockets shoot up and explode in a random color, sometimes in a defined pattern
  by DedeHai (Damian Schneider)
*/
void mode_particlefireworks(Segment &seg) {
  constexpr uint32_t NUMBEROFSOURCES = 8;
  ParticleSystem2D *PartSys = nullptr;
  uint32_t numRockets;

  if (seg.call == 0) {  // initialization
    if (!initParticleSystem2D(seg, PartSys, NUMBEROFSOURCES))
      FX_FALLBACK_STATIC;  // allocation failed

    PartSys->setKillOutOfBounds(true);  // out of bounds particles dont return (except on top, gravity handles that)
    PartSys->setWallHardness(120);      // ground bounce is fixed
    numRockets = PartSys->numSources < NUMBEROFSOURCES ? PartSys->numSources : NUMBEROFSOURCES;
    for (uint32_t j = 0; j < numRockets; j++) {
      PartSys->sources[j].source.ttl = 500 * j;  // first rocket starts immediately, others follow soon
      PartSys->sources[j].source.vy = -1;  // at negative speed no particles are emitted, a dead rocket relaunches
    }
  } else {
    PartSys = reinterpret_cast<ParticleSystem2D *>(seg.data);  // if not the first call, just set the pointer
  }

  if (PartSys == nullptr)
    FX_FALLBACK_STATIC;  // something went wrong, no data!

  PartSys->updateSystem();  // update system properties (dimensions and data pointers)
  numRockets = wf_map(seg.speed, 0, 255, 4,
                      PartSys->numSources < NUMBEROFSOURCES ? PartSys->numSources : NUMBEROFSOURCES);

  PartSys->setWrapX(seg.check1);
  PartSys->setBounceY(seg.check2);
  // if bounded, set gravity to a minimum of 1 or they will bounce at the top
  PartSys->setGravity(wf_map(seg.custom3, 0, 31, seg.check2 ? 1 : 0, 10));
  PartSys->setMotionBlur(wf_map(seg.custom2, 0, 255, 0, 245));  // enable motion blur

  // update the rockets, set the speed state
  for (uint32_t j = 0; j < numRockets; j++) {
    PartSys->applyGravity(PartSys->sources[j].source);
    PartSys->particleMoveUpdate(PartSys->sources[j].source, PartSys->sources[j].sourceFlags);
    if (PartSys->sources[j].source.ttl == 0) {
      if (PartSys->sources[j].source.vy > 0) {  // rocket died moving up, stop it so it will explode
        PartSys->sources[j].source.vy = 0;
      } else if (PartSys->sources[j].source.vy < 0) {  // rocket exploded and time is up, relaunch it
        PartSys->sources[j].source.y = PS_P_RADIUS;    // start from bottom
        PartSys->sources[j].source.x = (PartSys->maxX >> 2) + hw_random(PartSys->maxX >> 1);  // centered half
        PartSys->sources[j].source.vy = (seg.custom3) + hw_random16(seg.custom1 >> 3) + 5;    // rocket speed
        PartSys->sources[j].source.vx = hw_random16(7) - 3;  // not perfectly straight up
        PartSys->sources[j].source.sat = 30;                 // low saturation -> exhaust is off-white
        PartSys->sources[j].source.ttl = hw_random16(seg.custom1) + (seg.custom1 >> 1);  // set fuse time
        PartSys->sources[j].maxLife = 40;                                                // exhaust particle life
        PartSys->sources[j].minLife = 10;
        PartSys->sources[j].vx = 0;   // emitting speed
        PartSys->sources[j].vy = -5;  // emitting speed
        PartSys->sources[j].var = 4;  // speed variation around vx,vy (+/- var)
      }
    }
  }
  // check each rocket's state and emit particles: moving up = exhaust, at top = explode, falling = standby
  uint32_t emitparticles, frequency, baseangle, hueincrement;
  // variables for circular explosions
  [[maybe_unused]] int32_t speed, currentspeed, speedvariation, percircle;
  int32_t counter = 0;
  [[maybe_unused]] uint16_t angle;
  [[maybe_unused]] unsigned angleincrement;
  bool circularexplosion = false;

  // emit particles for each rocket
  for (uint32_t j = 0; j < numRockets; j++) {
    // determine the rocket state by its speed:
    if (PartSys->sources[j].source.vy > 0) {  // moving up, emit exhaust
      emitparticles = 1;
    } else if (PartSys->sources[j].source.vy < 0) {  // falling down, standby time
      emitparticles = 0;
    } else {                                              // speed is zero, explode!
      PartSys->sources[j].source.hue = hw_random16();     // random color
      PartSys->sources[j].source.sat = hw_random16(55) + 200;
      PartSys->sources[j].maxLife = 200;
      PartSys->sources[j].minLife = 100;
      // standby time til the next launch
      PartSys->sources[j].source.ttl =
          hw_random16((2000 - (static_cast<uint32_t>(seg.speed) << 2))) + 550 - (seg.speed << 1);
      PartSys->sources[j].var = ((seg.intensity >> 4) + 5);  // speed variation around vx,vy (+/- var)
      PartSys->sources[j].source.vy = -1;  // negative speed: no more particles until relaunch
      emitparticles = hw_random16(seg.intensity >> 2) + (seg.intensity >> 2) + 5;  // size of the explosion

      if (hw_random() & 1) {  // 50% chance for a circular explosion
        circularexplosion = true;
        speed = 2 + hw_random16(3) + ((seg.intensity >> 6));
        currentspeed = speed;
        angleincrement = 2730 + hw_random16(5461);  // minimum 15 degrees + random(30 degrees)
        angle = hw_random16();                      // random start angle
        baseangle = angle;                          // save the base angle for modulation
        percircle = 0xFFFF / angleincrement + 1;    // number of particles to make complete circles
        hueincrement = hw_random16() & 127;         // &127 is equivalent to %128
        int circles = 1 + hw_random16(3) + ((seg.intensity >> 6));
        frequency = hw_random16() & 127;  // modulation frequency (= "waves per circle"), x.4 fixed point
        emitparticles = percircle * circles;
        PartSys->sources[j].var = angle & 1;  // 0 or 1 variation, angle is random
      }
    }
    uint32_t i;
    for (i = 0; i < emitparticles; i++) {
      if (circularexplosion) {
        // shifted to positive values
        int32_t sineMod = 0xEFFF + sin16_t(static_cast<uint16_t>(((angle * frequency) >> 4) + baseangle));
        currentspeed = (speed / 2 + ((sineMod * speed) >> 16)) >> 1;  // sine modulation on the emit angle speed
        PartSys->angleEmit(PartSys->sources[j], angle, currentspeed);
        counter++;
        if (counter > percircle) {  // full circle completed, increase speed
          counter = 0;
          speed += 3 + ((seg.intensity >> 6));           // increase speed to form a second wave
          PartSys->sources[j].source.hue += hueincrement;  // new color for the next circle
          PartSys->sources[j].source.sat = 100 + hw_random16(156);
        }
        angle += angleincrement;  // set the angle for the next particle
      } else {                    // random explosion or exhaust
        PartSys->sprayEmit(PartSys->sources[j]);
        if ((j % 3) == 0) {
          PartSys->sources[j].source.hue = hw_random16();  // random color for each particle
        }
      }
    }
    if (i == 0)                                 // no particles emitted, this rocket is falling
      PartSys->sources[j].source.y = 1000;      // reset so gravity wont pull it down and bounce it
    circularexplosion = false;                  // reset for the next rocket
  }
  if (seg.check3) {  // fast speed, move particles twice
    for (uint32_t i = 0; i < PartSys->usedParticles; i++) {
      PartSys->particleMoveUpdate(PartSys->particles[i], PartSys->particleFlags[i], nullptr, nullptr);
    }
  }
  PartSys->update();  // update and render
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_FIRE
/*
  Particle Fire
  realistic fire effect using particles. heat based and using perlin-noise for wind
  by DedeHai (Damian Schneider)
*/
void mode_particlefire(Segment &seg) {
  ParticleSystem2D *PartSys = nullptr;
  uint32_t i;         // index variable
  uint32_t numFlames;  // number of flames: depends on fire width

  if (seg.call == 0) {  // initialization
    // maximum number of sources (PS may limit based on segment size), 4 additional bytes for a uint32_t lastcall
    if (!initParticleSystem2D(seg, PartSys, seg.width(), 4))
      FX_FALLBACK_STATIC;    // allocation failed or not 2D
    seg.aux0 = hw_random16();  // aux0 is the wind position (index) in the perlin noise
  } else {
    PartSys = reinterpret_cast<ParticleSystem2D *>(seg.data);  // if not the first call, just set the pointer
  }

  if (PartSys == nullptr)
    FX_FALLBACK_STATIC;  // something went wrong, no data!

  PartSys->updateSystem();  // update system properties (dimensions and data pointers)
  PartSys->setWrapX(seg.check2);
  PartSys->setMotionBlur(seg.check1 * 170);  // enable/disable motion blur
  PartSys->setSmearBlur(!seg.check1 * 60);   // enable smear blur if motion blur is not enabled

  // limit speed to 100 minimum, reduce the frame rate to make it slower (slower than 100 does not look nice)
  uint32_t firespeed = seg.speed > 100 ? seg.speed : 100;
  if (seg.speed < 100) {  // slow, limit FPS
    uint32_t *lastcall = reinterpret_cast<uint32_t *>(PartSys->PSdataEnd);
    uint32_t period = seg.now - *lastcall;
    if (period < static_cast<uint32_t>(wf_map(seg.speed, 0, 99, 50, 10))) {  // limit to 90FPS - 20FPS
      seg.call--;  // skipping a frame, decrement the counter
      return;      // do not update this frame
    }
    *lastcall = seg.now;
  }

  uint32_t spread = (PartSys->maxX >> 5) * (seg.custom3 + 1);  // fire around the segment center (subpixels)
  // number of flames used depends on the spread width, a good value is (fire width in pixel) * 2
  const uint32_t wantedflames = 4 + ((spread / PS_P_RADIUS) << 1);
  numFlames = PartSys->numSources < wantedflames ? PartSys->numSources : wantedflames;
  uint32_t percycle = (numFlames * 2) / 3;  // maximum number of particles emitted per cycle

  // update the flame sprays:
  for (i = 0; i < numFlames; i++) {
    if (seg.call & 1 && PartSys->sources[i].source.ttl > 0) {  // every second frame
      PartSys->sources[i].source.ttl--;
    } else {  // flame source is dead: initialize a new flame, set the properties of the source
      // change flame position: distribute randomly on the chosen width
      PartSys->sources[i].source.x = (PartSys->maxX >> 1) - (spread >> 1) + hw_random(spread);
      PartSys->sources[i].source.y = -(PS_P_RADIUS << 2);  // set the source below the frame
      // 'hotness' of the fire, faster flames reduce the effect or the flame height scales too much with speed
      PartSys->sources[i].source.ttl =
          20 + hw_random16((seg.custom1 * seg.custom1) >> 8) / (1 + (firespeed >> 5));
      // defines the flame height together with the vy speed
      PartSys->sources[i].maxLife = hw_random16(seg.height() >> 1) + 16;
      PartSys->sources[i].minLife = PartSys->sources[i].maxLife >> 1;
      PartSys->sources[i].vx = hw_random16(5) - 2;  // emitting speed (sideways)
      // emitting speed (upwards)
      PartSys->sources[i].vy = (seg.height() >> 1) + (firespeed >> 4) + (seg.custom1 >> 4);
      PartSys->sources[i].var = 2 + hw_random16(2 + (firespeed >> 4));  // speed variation around vx,vy
    }
  }

  if (seg.call % 3 == 0) {  // update the noise position and add wind
    seg.aux0++;             // position in the perlin noise matrix for wind generation
    if (seg.call % 10 == 0)
      seg.aux1++;  // move in the noise y direction so the noise does not repeat as often
    // add wind force to all particles
    int8_t windspeed = (static_cast<int16_t>(perlin8(seg.aux0, seg.aux1) - 127) * seg.custom2) >> 7;
    PartSys->applyForce(windspeed, 0);
  }
  seg.step++;

  if (seg.check3) {  // add turbulance (parameters and algorithm found by experimentation)
    if (seg.call % wf_map(firespeed, 0, 255, 4, 15) == 0) {
      for (i = 0; i < PartSys->usedParticles; i++) {
        if (PartSys->particles[i].y < PartSys->maxY / 4) {  // the bottom quarter seems a good balance
          int32_t curl =
              (static_cast<int32_t>(perlin8(PartSys->particles[i].x, PartSys->particles[i].y, seg.step << 4)) - 127);
          PartSys->particles[i].vx += (curl * (firespeed + 10)) >> 9;
        }
      }
    }
  }

  // emit faster sparks at the first flame position, amount and speed mostly depend on intensity
  if (hw_random8() < 10 + (seg.intensity >> 2)) {
    for (i = 0; i < PartSys->usedParticles; i++) {
      if (PartSys->particles[i].ttl == 0) {  // find a dead particle
        PartSys->particles[i].ttl = hw_random16(seg.height()) + 30;
        PartSys->particles[i].x = PartSys->sources[0].source.x;
        PartSys->particles[i].y = PartSys->sources[0].source.y;
        PartSys->particles[i].vx = PartSys->sources[0].source.vx;
        // emitting speed (upwards)
        PartSys->particles[i].vy =
            (seg.height() >> 1) + (firespeed >> 4) + ((30 + (seg.intensity >> 1) + seg.custom1) >> 4);
        break;  // emit only one particle
      }
    }
  }

  uint8_t j = hw_random16();  // start with a random flame, so each flame gets a chance to emit a particle
  for (i = 0; i < percycle; i++) {
    j = (j + 1) % numFlames;
    PartSys->flameEmit(PartSys->sources[j]);
  }

  PartSys->updateFire(seg.intensity);  // update and render the fire
}
#endif

const EffectInfo ENTRIES[] = {
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_FIRE
    {"PS Fire@Speed,Intensity,Flame Height,Wind,Spread,Smooth,Cylinder,Turbulence;;!;2;pal=35,sx=110,c1=110,c2=50,"
     "c3=31,o1=1",
     mode_particlefire},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_FIREWORKS
    {"PS Fireworks@Launches,Explosion Size,Fuse,Blur,Gravity,Cylinder,Ground,Fast;;!;2;pal=11,ix=50,c1=40,c2=0,c3=12",
     mode_particlefireworks},
#endif
};
constexpr size_t ENTRY_COUNT = sizeof(ENTRIES) / sizeof(ENTRIES[0]);

}  // namespace

// extern first: a bare `const` at namespace scope has internal linkage and the
// generated group table would not be able to see it.
extern const EffectGroup EFFECT_GROUP_PARTICLE_2D;
const EffectGroup EFFECT_GROUP_PARTICLE_2D{"particle_2d", ENTRIES, ENTRY_COUNT};

}  // namespace wled_fx
}  // namespace esphome

#endif  // WLED_FX_GROUP_PARTICLE_2D
