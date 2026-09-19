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
#define WLED_FX_GROUP_PARTICLE_1D \
  (WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_DRIPDROP || WLED_FX_FX_PS_PINBALL || \
   WLED_FX_FX_PS_DANCING_SHADOWS || WLED_FX_FX_PS_FIREWORKS_1D || WLED_FX_FX_PS_SPARKLER || \
   WLED_FX_FX_PS_HOURGLASS || WLED_FX_FX_PS_SPRAY_1D || WLED_FX_FX_PS_1D_BALANCE || \
   WLED_FX_FX_PS_CHASE || WLED_FX_FX_PS_STARBURST || WLED_FX_FX_PS_FIRE_1D)

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

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_PINBALL
/*
  Particle Version of "Bouncing Balls by Aircoookie"
  Also does rolling balls and juggle (and popcorn)
  Uses palette for particle color
  by DedeHai (Damian Schneider)
*/
void mode_particlePinball(Segment &seg) {
  ParticleSystem1D *PartSys = nullptr;

  if (seg.call == 0) {  // initialization
    if (!initParticleSystem1D(seg, PartSys, 1, 128, 0, true))  // init
      FX_FALLBACK_STATIC;  // allocation failed or is single pixel
    PartSys->sources[0].sourceFlags.collide = true;  // seeded particles will collide (if enabled)
    PartSys->sources[0].source.x = -1000;            // shoot up from below
    seg.aux0 = 1;
    seg.aux1 = 5000;  // set settings out of range to ensure update on first call
  } else {
    PartSys = reinterpret_cast<ParticleSystem1D *>(seg.data);  // if not the first call, just set the pointer
  }

  if (PartSys == nullptr)
    FX_FALLBACK_STATIC;  // something went wrong, no data!

  // Particle System settings
  PartSys->updateSystem();  // update system properties (dimensions and data pointers)
  PartSys->setGravity(wf_map(seg.custom3, 0, 31, 0, 8));  // set gravity (8 is default strength)
  PartSys->setBounce(seg.custom3);                        // disables bounce if no gravity is used
  PartSys->setMotionBlur(seg.custom2);                    // enable motion blur
  PartSys->enableParticleCollisions(seg.check1, 255);  // enable collisions and set particle collision to high hardness
  PartSys->setColorByPosition(seg.check3);
  // max particles depends on intensity and rolling balls mode + size
  uint32_t maxParticles = std::max<int>(20, seg.intensity / (1 + (seg.check2 * (seg.custom1 >> 5))));
  if (seg.custom1 < 255) {
    PartSys->setParticleSize(seg.custom1);  // set size globally
  } else {
    PartSys->perParticleSize = true;  // use random individual particle size (see below)
    maxParticles *= 2;                // use more particles if individual size is used as there is more space
  }
  PartSys->setUsedParticles(maxParticles);  // reduce if using larger size and rolling balls mode

  bool updateballs = false;
  // user settings change or more particles are available
  if (seg.aux1 != seg.speed + seg.intensity + seg.check2 + seg.custom1 + PartSys->usedParticles) {
    seg.step = seg.call;  // reset delay
    updateballs = true;
    // maximum lifetime in frames/2 (very long if not using gravity, enough to travel 4000 pixels at min speed)
    PartSys->sources[0].maxLife = seg.custom3 ? 1000 : 0xFFFF;
    PartSys->sources[0].minLife = PartSys->sources[0].maxLife >> 1;
  }

  if (seg.check2) {  // rolling balls
    PartSys->setGravity(0);
    PartSys->setWallHardness(255);
    int speedsum = 0;
    for (uint32_t i = 0; i < PartSys->usedParticles; i++) {
      PartSys->particles[i].ttl = 500;  // keep particles alive
      if (updateballs) {                // speed changed or particle is dead, set particle properties
        PartSys->particleFlags[i].collide = true;
        if (PartSys->particles[i].x == 0) {                           // still at initial position
          PartSys->particles[i].x = hw_random16(PartSys->maxX);       // random initial position for all particles
          PartSys->particles[i].vx = (hw_random16() & 0x01) ? 1 : -1;  // random initial direction
        }
        PartSys->particles[i].hue = hw_random8();  // set ball colors to random
        PartSys->advPartProps[i].sat = 255;
        PartSys->advPartProps[i].size = hw_random8();  // set ball size for individual size mode
      }
      speedsum += abs(PartSys->particles[i].vx);
    }
    int32_t avgSpeed = speedsum / PartSys->usedParticles;
    int32_t setSpeed = 2 + (seg.speed >> 2);
    if (avgSpeed < setSpeed) {  // if balls are slow, speed up some of them at random to keep the animation going
      for (int i = 0; i < setSpeed - avgSpeed; i++) {
        int idx = hw_random16(PartSys->usedParticles);
        if (abs(PartSys->particles[idx].vx) < PS_P_MAXSPEED)
          PartSys->particles[idx].vx += PartSys->particles[idx].vx >= 0 ? 1 : -1;  // add 1, keep direction
      }
    } else if (avgSpeed > setSpeed + 8) {  // if avg speed is too high, apply friction to slow them down
      PartSys->applyFriction(1);
    }
  } else {  // bouncing balls
    PartSys->setWallHardness(220);
    PartSys->sources[0].var = seg.speed >> 3;
    int32_t newspeed = 2 + (seg.speed >> 1) - (seg.speed >> 3);
    PartSys->sources[0].v = newspeed;
    // check for balls that are 'laying on the ground' and remove them
    for (uint32_t i = 0; i < PartSys->usedParticles; i++) {
      if (PartSys->particles[i].ttl < 50)
        PartSys->particles[i].ttl = 0;  // no dark particles
      else if (PartSys->particles[i].vx == 0 && PartSys->particles[i].x < (PS_P_RADIUS_1D + seg.custom1))
        PartSys->particles[i].ttl -= 50;  // age fast

      if (updateballs) {
        if (seg.custom3 == 0)  // gravity off, update speed
          PartSys->particles[i].vx = PartSys->particles[i].vx > 0 ? newspeed : -newspeed;  // keep the direction
      }
    }

    // every nth frame emit a ball
    if (seg.call > seg.step) {
      int interval = 260 - (static_cast<int>(seg.intensity));
      seg.step += interval + hw_random16(interval);
      PartSys->sources[0].source.hue = hw_random16();  // set ball color
      PartSys->sources[0].sat = 255;
      PartSys->sources[0].size = hw_random8();  // set ball size
      PartSys->sprayEmit(PartSys->sources[0]);
    }
  }
  seg.aux1 = seg.speed + seg.intensity + seg.check2 + seg.custom1 + PartSys->usedParticles;

  PartSys->update();  // update and render
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_DANCING_SHADOWS
// The SPOT_TYPE_* shapes are in wf_fx_shared.h; the original Dancing Shadows
// uses the same set from its own translation unit.

/*
  Particle Replacement for original Dancing Shadows:
  "Spotlights moving back and forth that cast dancing shadows.
  Shine this through tree branches/leaves or other close-up objects that cast
  interesting shadows onto a ceiling or tarp.
  By Steve Pomeroy @xxv"
  Uses palette for particle color
  by DedeHai (Damian Schneider)
*/
void mode_particleDancingShadows(Segment &seg) {
  ParticleSystem1D *PartSys = nullptr;

  if (seg.call == 0) {  // initialization
    if (!initParticleSystem1D(seg, PartSys, 1))                 // init, one source
      FX_FALLBACK_STATIC;                                        // allocation failed or is single pixel
    PartSys->sources[0].maxLife = 1000;  // set long life (kill out of bounds is done in a custom way)
    PartSys->sources[0].minLife = PartSys->sources[0].maxLife;
  } else {
    PartSys = reinterpret_cast<ParticleSystem1D *>(seg.data);  // if not the first call, just set the pointer
  }

  if (PartSys == nullptr)
    FX_FALLBACK_STATIC;  // something went wrong, no data!

  // Particle System settings
  PartSys->updateSystem();  // update system properties (dimensions and data pointers)
  PartSys->setMotionBlur(seg.custom1);
  if (seg.check1)
    PartSys->setSmearBlur(120);  // enable smear blur
  else
    PartSys->setSmearBlur(0);  // disable smear blur
  PartSys->setParticleSize(seg.check3);                         // 1 or 2 pixel rendering
  PartSys->setColorByPosition(seg.check2);                      // color fixed by position
  PartSys->setUsedParticles(wf_map(seg.intensity, 0, 255, 10, 255));  // set percentage of particles to use

  uint32_t deadparticles = 0;
  // kill out of bounds and moving away plus change color
  for (uint32_t i = 0; i < PartSys->usedParticles; i++) {
    // check if an out of bounds particle moves away from the strip, only update every 8th frame
    if (((seg.call & 0x07) == 0) && PartSys->particleFlags[i].outofbounds) {
      if (static_cast<int32_t>(PartSys->particles[i].vx) * PartSys->particles[i].x > 0)
        PartSys->particles[i].ttl = 0;  // particle is moving away, kill it
    }
    PartSys->particleFlags[i].perpetual = true;  // particles do not age
    if (seg.call % (32 / (1 + (seg.custom2 >> 3))) == 0)
      PartSys->particles[i].hue += 2 + (seg.custom2 >> 5);
    // note: updating speed on the fly is not accurately possible, since it is unknown which particles
    // are assigned to which spot
    if (seg.aux0 != seg.speed) {  // speed changed
      // update all particle speed by setting them to current value
      PartSys->particles[i].vx = PartSys->particles[i].vx > 0 ? seg.speed >> 3 : -seg.speed >> 3;
    }
    if (PartSys->particles[i].ttl == 0)
      deadparticles++;  // count dead particles
  }
  seg.aux0 = seg.speed;

  // generate a spotlight: generates particles just outside of view
  if (deadparticles > 5 && (seg.call & 0x03) == 0) {
    // random color, random type
    uint32_t type = hw_random16(SPOT_TYPES_COUNT);
    int8_t speed = 2 + hw_random16(2 + (seg.speed >> 1)) + (seg.speed >> 4);
    int32_t width = hw_random16(1, 10);
    // ttl is particle brightness (perpetual is set below so it does not age, i.e. ttl stays at this value)
    uint32_t ttl = 300;
    int32_t position;
    // choose random start position, left and right from the segment
    if (hw_random() & 0x01) {
      position = PartSys->maxXpixel;
      speed = -speed;
    } else {
      position = -width;
    }

    PartSys->sources[0].v = speed;                  // emitted particle speed
    PartSys->sources[0].source.hue = hw_random8();  // random spotlight color
    for (int32_t i = 0; i < width; i++) {
      if (width > 1) {
        switch (type) {
          case SPOT_TYPE_SOLID:
            // nothing to do
            break;

          case SPOT_TYPE_GRADIENT:
            ttl = cubicwave8(wf_map(i, 0, width - 1, 0, 255));
            ttl = ttl * ttl >> 8;  // make gradient more pronounced
            break;

          case SPOT_TYPE_2X_GRADIENT:
            ttl = cubicwave8(2 * wf_map(i, 0, width - 1, 0, 255));
            ttl = ttl * ttl >> 8;
            break;

          case SPOT_TYPE_2X_DOT:
            if (i > 0)
              position++;  // skip one pixel
            i++;
            break;

          case SPOT_TYPE_3X_DOT:
            if (i > 0)
              position += 2;  // skip two pixels
            i += 2;
            break;

          case SPOT_TYPE_4X_DOT:
            if (i > 0)
              position += 3;  // skip three pixels
            i += 3;
            break;
        }
      }
      // emit particle
      // set the particle source position:
      PartSys->sources[0].source.x = position * PS_P_RADIUS_1D;
      // Deviation from upstream: sprayEmit() returns -1 when no dead particle is left, and upstream
      // stores that in a uint32_t and indexes the particle array with it. Only five dead particles
      // are needed to enter this block but up to nine can be emitted, so the out of range write is
      // reachable. Skipping the brightness assignment is what upstream means to happen.
      int32_t partidx = PartSys->sprayEmit(PartSys->sources[0]);
      if (partidx >= 0)
        PartSys->particles[partidx].ttl = ttl;
      position++;  // do the next pixel
    }
  }

  PartSys->update();  // update and render
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_FIREWORKS_1D
/*
  Particle Fireworks 1D replacement
  Uses palette for particle color
  by DedeHai (Damian Schneider)
*/
void mode_particleFireworks1D(Segment &seg) {
  ParticleSystem1D *PartSys = nullptr;
  uint8_t *forcecounter;

  if (seg.call == 0) {  // initialization
    if (!initParticleSystem1D(seg, PartSys, 4, 150, 4, true))  // init advanced particle system
      FX_FALLBACK_STATIC;  // allocation failed or is single pixel
    PartSys->setKillOutOfBounds(true);
    PartSys->sources[0].sourceFlags.custom1 = 1;  // set rocket state to standby
  } else {
    PartSys = reinterpret_cast<ParticleSystem1D *>(seg.data);  // if not the first call, just set the pointer
  }
  if (PartSys == nullptr)
    FX_FALLBACK_STATIC;  // something went wrong, no data!

  // Particle System settings
  PartSys->updateSystem();  // update system properties (dimensions and data pointers)
  forcecounter = PartSys->PSdataEnd;
  PartSys->setMotionBlur(seg.custom2);            // enable motion blur
  int32_t gravity = (1 + (seg.speed >> 3));       // gravity value used for rocket speed calculation
  PartSys->setGravity(seg.speed ? gravity : 0);   // set gravity
  PartSys->setParticleSize(seg.check3);  // 1 or 2 pixel rendering (global size, disables per particle size)

  if (PartSys->sources[0].sourceFlags.custom1 == 1) {  // rocket is on standby
    PartSys->sources[0].source.ttl--;
    if (PartSys->sources[0].source.ttl == 0) {  // time is up, relaunch

      if (hw_random8() < seg.custom1)  // randomly choose direction according to slider, fire at start if true
        seg.aux0 = 1;
      else
        seg.aux0 = 0;

      PartSys->sources[0].sourceFlags.custom1 = 0;      // flag used for rocket state
      PartSys->sources[0].source.hue = hw_random16();   // different color for each launch
      PartSys->sources[0].var = 10 * seg.check2;        // emit variation, 0 if trail mode is off
      PartSys->sources[0].v = -10 * seg.check2;         // emit speed, 0 if trail mode is off
      PartSys->sources[0].minLife = 180;
      PartSys->sources[0].maxLife = seg.check2 ? 700 : 240;   // exhaust particle life
      PartSys->sources[0].source.x = seg.aux0 * PartSys->maxX;  // start from bottom or top
      // set speed such that the rocket explodes in frame
      uint32_t speed = sqrt((gravity * ((PartSys->maxX >> 2) + hw_random16(PartSys->maxX >> 1))) >> 4);
      PartSys->sources[0].source.vx = std::min<uint32_t>(speed, 127);
      PartSys->sources[0].source.ttl = 4000;
      PartSys->sources[0].sat = 30;                           // low saturation exhaust
      PartSys->sources[0].sourceFlags.reversegrav = false;    // normal gravity

      if (seg.aux0) {  // inverted rockets launch from end
        PartSys->sources[0].sourceFlags.reversegrav = true;
        PartSys->sources[0].source.vx = -PartSys->sources[0].source.vx;  // revert direction
        PartSys->sources[0].v = -PartSys->sources[0].v;                  // invert exhaust emit speed
      }
    }
  } else {  // rocket is launched
    int32_t rocketgravity = -gravity;
    int32_t currentspeed = PartSys->sources[0].source.vx;
    if (seg.aux0) {  // negative speed rocket
      rocketgravity = -rocketgravity;
      currentspeed = -currentspeed;
    }
    PartSys->applyForce(PartSys->sources[0].source, rocketgravity, forcecounter[0]);
    PartSys->particleMoveUpdate(PartSys->sources[0].source, PartSys->sources[0].sourceFlags);
    // increase rocket speed by calling the move function twice, also ages twice
    PartSys->particleMoveUpdate(PartSys->sources[0].source, PartSys->sources[0].sourceFlags);
    uint32_t rocketheight = seg.aux0 ? PartSys->maxX - PartSys->sources[0].source.x : PartSys->sources[0].source.x;

    if (currentspeed < 0 && PartSys->sources[0].source.ttl > 50)  // reached apogee
      PartSys->sources[0].source.ttl = 50 - gravity;              // alive for a few more frames

    if (PartSys->sources[0].source.ttl < 2) {         // explode
      PartSys->sources[0].sourceFlags.custom1 = 1;    // set standby state
      // set explosion particle speed
      PartSys->sources[0].var =
          5 + ((((PartSys->maxX >> 1) + rocketheight) * (20 + (seg.intensity << 1))) / (PartSys->maxX << 2));
      PartSys->sources[0].minLife = 1200;
      PartSys->sources[0].maxLife = 2600;
      PartSys->sources[0].source.ttl = 100 + hw_random16(64 - (seg.speed >> 2));  // standby time til next launch
      PartSys->sources[0].sat = seg.custom3 < 16 ? 10 + (seg.custom3 << 4) : 255;  // color saturation
      PartSys->sources[0].size = seg.check3 ? hw_random16(seg.intensity) : 0;  // random particle size in explosion
      uint32_t explosionsize =
          8 + (PartSys->maxXpixel >> 2) + (PartSys->sources[0].source.x >> (PS_P_RADIUS_SHIFT_1D - 1));
      explosionsize += hw_random16((explosionsize * seg.intensity) >> 8);
      PartSys->setColorByAge(false);       // disable
      PartSys->setColorByPosition(false);  // disable
      for (uint32_t e = 0; e < explosionsize; e++) {         // emit explosion particles
        int idx = PartSys->sprayEmit(PartSys->sources[0]);  // emit a particle
        if (idx < 0)
          break;  // no more particles available
        if (seg.custom3 > 23) {
          if (seg.custom3 == 31) {                    // highest slider value
            PartSys->setColorByAge(seg.check1);       // color by age if colorful mode is enabled
            PartSys->setColorByPosition(!seg.check1);  // color by position otherwise
          } else {  // if custom3 is set to a high value (but not the highest), set the color by initial speed
            // set hue according to speed, use a random amount of palette width
            PartSys->particles[idx].hue =
                wf_map(abs(PartSys->particles[idx].vx), 0, PartSys->sources[0].var, 0, 16 + hw_random16(200));
            PartSys->particles[idx].hue += PartSys->sources[0].source.hue;  // add the rocket hue offset
          }
        } else {
          if (seg.check1)                                     // colorful mode
            PartSys->sources[0].source.hue = hw_random16();  // random color for each particle
        }
      }
    }
  }
  // every second frame and not in standby
  if ((seg.call & 0x01) == 0 && PartSys->sources[0].sourceFlags.custom1 == false)
    PartSys->sprayEmit(PartSys->sources[0]);  // emit exhaust particle

  if ((seg.call & 0x03) == 0)  // every fourth frame
    PartSys->applyFriction(1);  // apply friction to all particles

  PartSys->update();  // update and render

  for (uint32_t i = 0; i < PartSys->usedParticles; i++) {
    // ttl is linked to brightness, this allows a higher brightness with a short spark lifespan
    if (PartSys->particles[i].ttl > 20)
      PartSys->particles[i].ttl -= 20;
    else
      PartSys->particles[i].ttl = 0;
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_SPARKLER
/*
  Particle based Sparkle effect
  Uses palette for particle color
  by DedeHai (Damian Schneider)
*/
void mode_particleSparkler(Segment &seg) {
  ParticleSystem1D *PartSys = nullptr;
  uint32_t numSparklers;
  PSsettings1D sparklersettings;
  sparklersettings.asByte = 0;  // PS settings for the sparkler (set below)

  if (seg.call == 0) {  // initialization
    if (!initParticleSystem1D(seg, PartSys, 16, 128, 0, true))  // init, no additional data needed
      FX_FALLBACK_STATIC;  // allocation failed or is single pixel
  } else {
    PartSys = reinterpret_cast<ParticleSystem1D *>(seg.data);  // if not the first call, just set the pointer
  }
  if (PartSys == nullptr)
    FX_FALLBACK_STATIC;  // something went wrong, no data!

  // Particle System settings
  PartSys->updateSystem();  // update system properties (dimensions and data pointers)

  sparklersettings.wrap = !seg.check2;
  sparklersettings.bounce = seg.check2;  // note: bounce always takes priority over wrap

  numSparklers = PartSys->numSources;
  PartSys->setMotionBlur(seg.custom2);                 // enable motion blur/overlay
  PartSys->setParticleSize(seg.check3 ? 60 : 0);       // single pixel or large particle rendering

  for (uint32_t i = 0; i < numSparklers; i++) {
    PartSys->sources[i].source.hue = hw_random16();
    PartSys->sources[i].var = 0;  // sparks stationary
    PartSys->sources[i].minLife = 150 + seg.intensity;
    PartSys->sources[i].maxLife = 250 + (seg.intensity << 1);
    int32_t speed = seg.speed >> 1;
    if (seg.check1)  // sparks move (slide option)
      PartSys->sources[i].var = seg.intensity >> 3;
    // update speed, do not change direction
    PartSys->sources[i].source.vx = PartSys->sources[i].source.vx > 0 ? speed : -speed;
    PartSys->sources[i].source.ttl = 400;  // replenish its life (setting it perpetual uses more code)
    PartSys->sources[i].sat = seg.custom1;  // color saturation
    if (seg.speed == 255)                   // random position at the highest speed setting
      PartSys->sources[i].source.x = hw_random(PartSys->maxX);
    else
      PartSys->particleMoveUpdate(PartSys->sources[i].source, PartSys->sources[i].sourceFlags,
                                  &sparklersettings);  // move sparkler
  }

  numSparklers = std::min<int>(1 + (seg.custom3 >> 1), static_cast<int>(numSparklers));  // used sparklers, 1 to 16

  if (seg.aux0 != seg.custom3) {  // number of used sparklers changed, redistribute
    for (uint32_t i = 1; i < numSparklers; i++) {
      PartSys->sources[i].source.x =
          (PartSys->sources[0].source.x + (PartSys->maxX / numSparklers) * i) % PartSys->maxX;  // distribute evenly
    }
  }
  seg.aux0 = seg.custom3;

  for (uint32_t i = 0; i < numSparklers; i++) {
    if (hw_random() % (((271 - seg.intensity) >> 4)) == 0)
      PartSys->sprayEmit(PartSys->sources[i]);  // emit a particle
  }

  PartSys->update();  // update and render

  for (uint32_t i = 0; i < PartSys->usedParticles; i++) {
    // ttl is linked to brightness, this allows a higher brightness with a short spark lifespan
    if (PartSys->particles[i].ttl > (64 - (seg.intensity >> 2)))
      PartSys->particles[i].ttl -= (64 - (seg.intensity >> 2));
    else
      PartSys->particles[i].ttl = 0;
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_HOURGLASS
/*
  Particle based Hourglass, particles falling at defined intervals
  Uses palette for particle color
  by DedeHai (Damian Schneider)
*/
void mode_particleHourglass(Segment &seg) {
  ParticleSystem1D *PartSys = nullptr;
  constexpr int positionOffset = PS_P_RADIUS_1D / 2;  // resting position offset
  bool *direction;
  uint32_t *settingTracker;
  if (seg.call == 0) {  // initialization
    if (!initParticleSystem1D(seg, PartSys, 0, 255, 8, false))  // init
      FX_FALLBACK_STATIC;  // allocation failed or is single pixel
    PartSys->setBounce(true);
    PartSys->setWallHardness(100);
  } else {
    PartSys = reinterpret_cast<ParticleSystem1D *>(seg.data);  // if not the first call, just set the pointer
  }
  if (PartSys == nullptr)
    FX_FALLBACK_STATIC;  // something went wrong, no data!

  // Particle System settings
  PartSys->updateSystem();  // update system properties (dimensions and data pointers)
  settingTracker = reinterpret_cast<uint32_t *>(PartSys->PSdataEnd);  // assign data pointer
  direction = reinterpret_cast<bool *>(PartSys->PSdataEnd + 4);       // assign data pointer
  PartSys->setUsedParticles(1 + ((seg.intensity * 255) >> 8));
  PartSys->setMotionBlur(seg.custom2);  // enable motion blur
  PartSys->setGravity(wf_map(seg.custom3, 0, 31, 1, 30));
  PartSys->enableParticleCollisions(true, 64);  // hardness value (found by experimentation on different settings)

  uint32_t colormode = seg.custom1 >> 5;  // 0-7

  if (seg.intensity != *settingTracker) {  // initialize
    *settingTracker = seg.intensity;
    for (uint32_t i = 0; i < PartSys->usedParticles; i++) {
      PartSys->particleFlags[i].reversegrav = true;  // resting particles dont fall
      *direction = 0;                                // down
      seg.aux1 = 1;                                  // initialize below
    }
    seg.aux0 = PartSys->usedParticles - 1;  // initial state, start with the highest number particle
  }

  // re-order particles in case heavy collisions flipped them (highest number index particle is on the "bottom")
  for (uint32_t i = 0; i < PartSys->usedParticles - 1; i++) {
    if (PartSys->particles[i].x < PartSys->particles[i + 1].x && PartSys->particleFlags[i].fixed == false &&
        PartSys->particleFlags[i + 1].fixed == false) {
      std::swap(PartSys->particles[i].x, PartSys->particles[i + 1].x);
    }
  }
  // calculate target position depending on direction
  auto calcTargetPos = [&](size_t i) {
    return PartSys->particleFlags[i].reversegrav ? PartSys->maxX - i * PS_P_RADIUS_1D - positionOffset
                                                 : (PartSys->usedParticles - i) * PS_P_RADIUS_1D - positionOffset;
  };

  for (uint32_t i = 0; i < PartSys->usedParticles; i++) {  // check if a particle reached its target after falling
    if (PartSys->particleFlags[i].fixed == false && abs(PartSys->particles[i].vx) < 5) {
      int32_t targetposition = calcTargetPos(i);
      bool belowtarget = PartSys->particleFlags[i].reversegrav ? (PartSys->particles[i].x > targetposition)
                                                               : (PartSys->particles[i].x < targetposition);
      bool closeToTarget = abs(targetposition - PartSys->particles[i].x) < PS_P_RADIUS_1D;
      if (belowtarget || closeToTarget) {            // overshot target or close to target and slow speed
        PartSys->particles[i].x = targetposition;    // set exact position
        PartSys->particleFlags[i].fixed = true;      // pin particle
      }
    }
    if (colormode == 7) {
      PartSys->setColorByPosition(true);  // color fixed by position
    } else {
      PartSys->setColorByPosition(false);
      uint8_t basehue = ((seg.custom1 & 0x1F) << 3);  // use 5 LSBs to select color
      switch (colormode) {
        case 0:
          PartSys->particles[i].hue = 120;
          break;  // fixed at 120, if flip is activated this can make red and green (use palette 34)
        case 1:
          PartSys->particles[i].hue = basehue;
          break;  // fixed selectable color
        case 2:   // 2 colors interleaved (same code as 3)
        case 3:
          PartSys->particles[i].hue = ((seg.custom1 & 0x1F) << 1) + (i % 3) * 74;
          break;  // 3 interleaved colors
        case 4:
          PartSys->particles[i].hue = basehue + (i * 255) / PartSys->usedParticles;
          break;  // gradient palette colors
        case 5:
          PartSys->particles[i].hue = basehue + (i * 1024) / PartSys->usedParticles;
          break;  // multi gradient palette colors
        case 6:
          PartSys->particles[i].hue = i + (seg.now >> 3);
          break;  // disco! moving color gradient
        default:
          break;  // use color by position
      }
    }
    if (seg.check1 && !PartSys->particleFlags[i].reversegrav)  // flip color when fallen
      PartSys->particles[i].hue += 120;
  }

  if (seg.aux1 == 1) {  // last countdown call before dropping starts, reset all particles
    for (uint32_t i = 0; i < PartSys->usedParticles; i++) {
      PartSys->particleFlags[i].collide = true;
      PartSys->particleFlags[i].perpetual = true;
      PartSys->particles[i].ttl = 260;
      PartSys->particles[i].x = calcTargetPos(i);
      PartSys->particleFlags[i].fixed = true;
    }
  }

  if (seg.aux1 == 0) {          // countdown passed, run
    if (seg.now >= seg.step) {  // drop a particle
      // set next drop time
      if (seg.check3 && *direction)     // fast reset
        seg.step = seg.now + 100;       // drop one particle every 100ms
      else                              // normal interval
        seg.step = seg.now + std::max<int>(100, seg.speed * 100);  // map speed slider from 0.1s to 25.5s
      if (seg.aux0 < PartSys->usedParticles) {
        PartSys->particleFlags[seg.aux0].reversegrav = *direction;  // let this particle fall or rise
        PartSys->particleFlags[seg.aux0].fixed = false;             // unpin
      } else {                                                      // overflow
        *direction = !(*direction);                                 // flip direction
        // set restart countdown, make it short if auto start is unchecked
        seg.aux1 = (seg.check2) * seg.length() + 100;
      }
      if (*direction == 0)  // down, start dropping the highest number particle
        seg.aux0--;         // next particle
      else
        seg.aux0++;
    }
  } else if (seg.check2) {  // auto start/reset
    seg.aux1--;             // countdown
  }

  PartSys->update();  // update and render
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_SPRAY_1D
/*
  Particle based Spray effect (like a volcano, possible replacement for popcorn)
  Uses palette for particle color
  by DedeHai (Damian Schneider)
*/
void mode_particle1Dspray(Segment &seg) {
  ParticleSystem1D *PartSys = nullptr;

  if (seg.call == 0) {  // initialization
    if (!initParticleSystem1D(seg, PartSys, 1))
      FX_FALLBACK_STATIC;  // allocation failed or is single pixel
    PartSys->setKillOutOfBounds(true);
    PartSys->setWallHardness(150);
    PartSys->setParticleSize(1);
  } else {
    PartSys = reinterpret_cast<ParticleSystem1D *>(seg.data);  // if not the first call, just set the pointer
  }
  if (PartSys == nullptr)
    FX_FALLBACK_STATIC;  // something went wrong, no data!

  // Particle System settings
  PartSys->updateSystem();  // update system properties (dimensions and data pointers)
  PartSys->setBounce(seg.check2);
  PartSys->setMotionBlur(seg.custom2);  // enable motion blur
  // gravity setting, 0-15 is positive (down), 17 - 31 is negative (up)
  int32_t gravity = -(static_cast<int32_t>(seg.custom3) - 16);
  // use the reversegrav setting to invert gravity (for proper 'floor' and out of bounds handling)
  PartSys->setGravity(abs(gravity));

  PartSys->sources[0].source.hue = seg.aux0;
  PartSys->sources[0].var = 20;
  PartSys->sources[0].minLife = 200;
  PartSys->sources[0].maxLife = 400;
  PartSys->sources[0].source.x = wf_map(seg.custom1, 0, 255, 0, PartSys->maxX);  // spray position
  // particle emit speed
  PartSys->sources[0].v =
      wf_map(seg.speed, 0, 255, -127 + PartSys->sources[0].var, 127 - PartSys->sources[0].var);
  PartSys->sources[0].sourceFlags.reversegrav = gravity < 0 ? true : false;

  if (hw_random() % (1 + ((255 - seg.intensity) >> 3)) == 0) {
    PartSys->sprayEmit(PartSys->sources[0]);  // emit a particle
    seg.aux0++;                               // increment hue
  }

  // update color settings
  PartSys->setColorByAge(seg.check1);  // overruled by 'color by position'
  PartSys->setColorByPosition(seg.check3);
  for (uint32_t i = 0; i < PartSys->usedParticles; i++) {
    // update gravity direction
    PartSys->particleFlags[i].reversegrav = PartSys->sources[0].sourceFlags.reversegrav;
  }
  PartSys->update();  // update and render
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_1D_BALANCE
/*
  Particle based balance: particles move back and forth (1D pendent to 2D particle box)
  Uses palette for particle color
  by DedeHai (Damian Schneider)
*/
void mode_particleBalance(Segment &seg) {
  ParticleSystem1D *PartSys = nullptr;
  uint32_t i;

  if (seg.call == 0) {  // initialization
    if (!initParticleSystem1D(seg, PartSys, 1, 128))  // init, no additional data needed, use half of max particles
      FX_FALLBACK_STATIC;  // allocation failed or is single pixel
    PartSys->setParticleSize(1);
  } else {
    PartSys = reinterpret_cast<ParticleSystem1D *>(seg.data);  // if not the first call, just set the pointer
  }
  if (PartSys == nullptr)
    FX_FALLBACK_STATIC;  // something went wrong, no data!

  // Particle System settings
  PartSys->updateSystem();  // update system properties (dimensions and data pointers)
  PartSys->setMotionBlur(seg.custom2);  // enable motion blur
  PartSys->setBounce(!seg.check2);
  PartSys->setWrap(seg.check2);
  // set hardness, make the walls hard if collisions are disabled
  uint8_t hardness = seg.custom1 > 0 ? wf_map(seg.custom1, 0, 255, 50, 250) : 200;
  PartSys->enableParticleCollisions(seg.custom1, hardness);  // enable collisions if custom1 > 0
  PartSys->setWallHardness(200);
  PartSys->setUsedParticles(wf_map(seg.intensity, 0, 255, 10, 255));
  if (PartSys->usedParticles > seg.aux1) {  // more particles, reinitialize
    for (i = 0; i < PartSys->usedParticles; i++) {
      PartSys->particles[i].x = i * PS_P_RADIUS_1D;
      PartSys->particles[i].ttl = 300;
      PartSys->particleFlags[i].perpetual = true;
      PartSys->particleFlags[i].collide = true;
    }
  }
  seg.aux1 = PartSys->usedParticles;

  // re-order particles in case collisions flipped particles
  for (i = 0; i < PartSys->usedParticles - 1; i++) {
    if (PartSys->particles[i].x > PartSys->particles[i + 1].x) {
      if (seg.check2) {  // check for wrap around
        if (PartSys->particles[i].x - PartSys->particles[i + 1].x > 3 * PS_P_RADIUS_1D)
          continue;
      }
      std::swap(PartSys->particles[i].x, PartSys->particles[i + 1].x);
    }
  }

  if (seg.call % (((255 - seg.speed) >> 6) + 1) == 0) {  // how often the force is applied depends on the speed
    int32_t xgravity;
    int32_t increment = (seg.speed >> 6) + 1;
    seg.aux0 += increment;
    if (seg.check3)  // random, use perlin noise
      xgravity = (static_cast<int16_t>(perlin8(seg.aux0)) - 128);
    else  // sinusoidal
      xgravity = static_cast<int16_t>(cos8_t(seg.aux0)) - 128;
    // scale the force
    xgravity = (xgravity * ((seg.custom3 + 1) << 2)) / 128;  // xgravity: -127 to +127
    PartSys->applyForce(xgravity);
  }

  uint32_t randomindex = hw_random16(PartSys->usedParticles);
  // apply friction to a random particle to reduce clumping
  PartSys->particles[randomindex].vx = (static_cast<int32_t>(PartSys->particles[randomindex].vx) * 200) / 255;

  // apply friction every 16th frame to smooth things out (except for low tilt)
  if ((seg.call & 0x0F) == 0 && seg.custom3 > 4)
    PartSys->applyFriction(1);  // apply friction to all particles

  // update colors
  PartSys->setColorByPosition(seg.check1);
  if (!seg.check1) {
    for (i = 0; i < PartSys->usedParticles; i++) {
      PartSys->particles[i].hue = (1024 * i) / PartSys->usedParticles;  // color by particle index
    }
  }
  PartSys->update();  // update and render
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_CHASE
/*
Particle based Chase effect
Uses palette for particle color
by DedeHai (Damian Schneider)
*/
void mode_particleChase(Segment &seg) {
  ParticleSystem1D *PartSys = nullptr;
  if (seg.call == 0) {  // initialization
    if (!initParticleSystem1D(seg, PartSys, 1, 191, 2, true))  // init
      FX_FALLBACK_STATIC;  // allocation failed or is single pixel
    seg.aux0 = 0xFFFF;                  // invalidate
    *PartSys->PSdataEnd = 1;            // huedir
    *(PartSys->PSdataEnd + 1) = 1;      // sizedir
  } else {
    PartSys = reinterpret_cast<ParticleSystem1D *>(seg.data);  // if not the first call, just set the pointer
  }
  if (PartSys == nullptr)
    FX_FALLBACK_STATIC;  // something went wrong, no data!
  // Particle System settings
  PartSys->updateSystem();  // update system properties (dimensions and data pointers)
  PartSys->setColorByPosition(seg.check3);
  PartSys->setMotionBlur(7 + ((seg.custom3) << 3));  // enable motion blur
  // depends on intensity and particle size (custom1), minimum 1
  uint32_t numParticles =
      1 + wf_map(seg.intensity, 0, 255, 0, PartSys->usedParticles / (1 + (seg.custom1 >> 5)));
  numParticles = std::min<uint32_t>(numParticles, PartSys->usedParticles);  // limit to available particles
  int32_t huestep = 1 + (((static_cast<uint32_t>(seg.custom2) << 19) / numParticles) >> 16);  // hue increment
  uint32_t settingssum =
      seg.speed + seg.intensity + seg.custom1 + seg.custom2 + seg.check1 + seg.check2 + seg.check3;
  if (seg.aux0 != settingssum) {  // settings changed, update
    if (seg.check1) {
      seg.step = PartSys->advPartProps[0].size / 2 + (PartSys->maxX / numParticles);
    } else {
      seg.step = (PartSys->maxX + (PS_P_RADIUS_1D << 6)) / numParticles;  // spacing between particles
      // round down to the nearest multiple of the particle subpixel unit so they align to the pixel
      // grid (makes them move in union)
      seg.step = (seg.step / PS_P_RADIUS_1D) * PS_P_RADIUS_1D;
    }
    for (int32_t i = 0; i < static_cast<int32_t>(PartSys->usedParticles); i++) {
      PartSys->advPartProps[i].sat = 255;
      PartSys->particles[i].x = (i - 1) * seg.step;  // distribute evenly (starts out of frame for i=0)
      PartSys->particles[i].vx = seg.speed >> 2;
      PartSys->advPartProps[i].size = seg.custom1;
      if (seg.custom2 < 255)
        PartSys->particles[i].hue = i * huestep;  // gradient distribution
      else
        PartSys->particles[i].hue = hw_random16();
    }
    seg.aux0 = settingssum;
  }

  if (seg.check1) {
    // changes gradient spread (scale hue step)
    huestep = 1 + (std::max<int>(static_cast<int>(huestep), 3) * ((int(sin16_t(seg.now * 3) + 32767))) >> 15);
  }

  // wrap around (cannot use particle system wrap if distributing colors manually, it also wraps
  // rendering which does not look good)
  // check from the back, the last particle wraps first, multiple particles can overrun per frame
  for (int32_t i = static_cast<int32_t>(PartSys->usedParticles) - 1; i >= 0; i--) {
    if (PartSys->particles[i].x > PartSys->maxX + PS_P_RADIUS_1D + PartSys->advPartProps[i].size) {  // wrap it around
      uint32_t nextindex = (i + 1) % PartSys->usedParticles;
      PartSys->particles[i].x = PartSys->particles[nextindex].x - static_cast<int>(seg.step);
      if (seg.check1)  // playful mode, vary size
        PartSys->advPartProps[i].size =
            std::max<int>(1 + (seg.custom1 >> 1), ((int(sin16_t(seg.now << 1) + 32767)) >> 8));  // cycle size
      if (seg.custom2 < 255)
        PartSys->particles[i].hue = PartSys->particles[nextindex].hue - huestep;
      else
        PartSys->particles[i].hue = hw_random16();
    }
    // reset ttl, cannot use perpetual because memmanager can change the pointer at any time
    PartSys->particles[i].ttl = 300;
  }

  if (seg.check1) {  // playful mode, changes hue, size, speed, density dynamically
    int8_t *huedir = reinterpret_cast<int8_t *>(PartSys->PSdataEnd);  // assign data pointer
    int8_t *stepdir = reinterpret_cast<int8_t *>(PartSys->PSdataEnd + 1);
    if (*stepdir == 0)
      *stepdir = 1;  // initialize directions
    if (*huedir == 0)
      *huedir = 1;
    if (seg.step >= (PartSys->advPartProps[0].size + PS_P_RADIUS_1D * 4) + PartSys->maxX / numParticles)
      *stepdir = -1;  // increase density (decrease space between particles)
    else if (seg.step <= (PartSys->advPartProps[0].size >> 1) + ((PartSys->maxX / numParticles)))
      *stepdir = 1;  // decrease density
    if (seg.aux1 > 512)
      *huedir = -1;
    else if (seg.aux1 < 50)
      *huedir = 1;
    if (seg.call % (1024 / (1 + (seg.speed >> 2))) == 0)
      seg.aux1 += *huedir;
    int8_t globalhuestep = 0;  // global hue increment
    if (seg.call % (1 + (int(sin16_t(seg.now) + 32767) >> 12)) == 0)
      globalhuestep = 2;  // global hue change to add some color variation
    if ((seg.call & 0x1F) == 0)
      seg.step += *stepdir;  // change density
    for (uint32_t i = 0; i < PartSys->usedParticles; i++) {
      PartSys->particles[i].hue -= globalhuestep;  // shift global hue (both directions)
      PartSys->particles[i].vx =
          1 + (seg.speed >> 2) + ((int32_t(sin16_t(seg.now >> 1) + 32767) * (seg.speed >> 2)) >> 16);
    }
  }

  PartSys->update();  // update and render
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_STARBURST
/*
  Particle Fireworks Starburst replacement (smoother rendering, more settings)
  Uses palette for particle color
  by DedeHai (Damian Schneider)
*/
void mode_particleStarburst(Segment &seg) {
  ParticleSystem1D *PartSys = nullptr;

  if (seg.call == 0) {  // initialization
    if (!initParticleSystem1D(seg, PartSys, 1, 200, 0, true))  // init
      FX_FALLBACK_STATIC;  // allocation failed or is single pixel
    PartSys->setKillOutOfBounds(true);
    PartSys->enableParticleCollisions(true, 200);
    PartSys->sources[0].source.ttl = 1;  // set initial standby time
    PartSys->sources[0].sat = 0;         // emitted particles start out white
  } else {
    PartSys = reinterpret_cast<ParticleSystem1D *>(seg.data);  // if not the first call, just set the pointer
  }
  if (PartSys == nullptr)
    FX_FALLBACK_STATIC;  // something went wrong, no data!

  // Particle System settings
  PartSys->updateSystem();  // update system properties (dimensions and data pointers)
  PartSys->setMotionBlur(seg.custom2);   // enable motion blur
  PartSys->setGravity(seg.check1 * 8);   // enable gravity

  if (PartSys->sources[0].source.ttl-- == 0) {  // standby time elapsed
    uint32_t explosionsize = 4 + hw_random16(seg.intensity >> 2);
    PartSys->sources[0].source.hue = hw_random16();
    PartSys->sources[0].var = 10 + (explosionsize << 1);
    PartSys->sources[0].minLife = 150;
    PartSys->sources[0].maxLife = 300;
    PartSys->sources[0].source.x = hw_random(PartSys->maxX);  // random explosion position
    PartSys->sources[0].source.ttl = 10 + hw_random16(255 - seg.speed);
    PartSys->sources[0].size = seg.custom1;  // Fragment size
    PartSys->sources[0].sourceFlags.collide = seg.check3;
    for (uint32_t e = 0; e < explosionsize; e++) {  // emit particles
      if (seg.check2)
        PartSys->sources[0].source.hue = hw_random16();  // random color for each particle
      PartSys->sprayEmit(PartSys->sources[0]);           // emit a particle
    }
  }
  // shrink all particles
  for (uint32_t i = 0; i < PartSys->usedParticles; i++) {
    if (PartSys->advPartProps[i].size)
      PartSys->advPartProps[i].size--;
    if (PartSys->advPartProps[i].sat < 250)
      PartSys->advPartProps[i].sat += 2 + (seg.custom3 >> 3);
  }

  if (seg.call % 5 == 0) {
    PartSys->applyFriction(1);  // slow down particles
  }

  PartSys->update();  // update and render
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_FIRE_1D
/*
  Particle based Fire effect
  Uses palette for particle color
  by DedeHai (Damian Schneider)
*/
void mode_particleFire1D(Segment &seg) {
  ParticleSystem1D *PartSys = nullptr;

  if (seg.call == 0) {  // initialization
    if (!initParticleSystem1D(seg, PartSys, 5))  // init
      FX_FALLBACK_STATIC;  // allocation failed or is single pixel
    PartSys->setKillOutOfBounds(true);
    PartSys->setParticleSize(1);
  } else {
    PartSys = reinterpret_cast<ParticleSystem1D *>(seg.data);  // if not the first call, just set the pointer
  }
  if (PartSys == nullptr)
    FX_FALLBACK_STATIC;  // something went wrong, no data!

  // Particle System settings
  PartSys->updateSystem();  // update system properties (dimensions and data pointers)
  PartSys->setMotionBlur(128 + (seg.custom2 >> 1));  // enable motion blur
  PartSys->setColorByAge(true);
  uint32_t emitparticles = 1;
  uint32_t j = hw_random16();
  for (uint32_t i = 0; i < 3; i++) {  // 3 base flames
    if (PartSys->sources[i].source.ttl > 50)
      PartSys->sources[i].source.ttl -= 10;
    else
      PartSys->sources[i].source.ttl = 100 + hw_random16(200);
  }
  for (uint32_t i = 0; i < PartSys->numSources; i++) {
    j = (j + 1) % PartSys->numSources;
    PartSys->sources[j].source.x = 0;
    PartSys->sources[j].var = 2 + (seg.speed >> 4);
    // base flames
    if (j > 2) {
      PartSys->sources[j].minLife = 150 + seg.intensity + (j << 2);
      PartSys->sources[j].maxLife = 200 + seg.intensity + (j << 3);
      PartSys->sources[j].v = (seg.speed >> (2 + (j << 1)));
      if (emitparticles) {
        emitparticles--;
        PartSys->sprayEmit(PartSys->sources[j]);  // emit a particle
      }
    } else {
      PartSys->sources[j].minLife = PartSys->sources[j].source.ttl + seg.intensity;
      PartSys->sources[j].maxLife = PartSys->sources[j].minLife + 50;
      PartSys->sources[j].v = seg.speed >> 2;
      if (seg.call & 0x01)                        // every second frame
        PartSys->sprayEmit(PartSys->sources[j]);  // emit a particle
    }
  }

  for (uint32_t i = 0; i < PartSys->usedParticles; i++) {
    PartSys->particles[i].x += PartSys->particles[i].ttl >> 7;  // 'hot' particles are faster, add extra velocity
    if (PartSys->particles[i].ttl > 3 + ((255 - seg.custom1) >> 1))
      PartSys->particles[i].ttl -= wf_map(seg.custom1, 0, 255, 1, 3);  // age faster
  }

  PartSys->update();  // update and render
}
#endif

const EffectInfo ENTRIES[] = {
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_DRIPDROP
    {"PS DripDrop@Speed,!,Splash,Blur,Gravity,Rain,PushSplash,Smooth;,!;!;1;pal=0,sx=150,ix=25,c1=220,c2=30,c3=21",
     mode_particleDrip},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_PINBALL
    {"PS Pinball@Speed,!,Size,Blur,Gravity,Collide,Rolling,Position Color;,!;!;1;pal=0,ix=220,c2=0,c3=8,o1=1",
     mode_particlePinball},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_DANCING_SHADOWS
    {"PS Dancing Shadows@Speed,!,Blur,Color Cycle,,Smear,Position Color,Smooth;,!;!;1;sx=100,ix=180,c1=0,c2=0",
     mode_particleDancingShadows},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_FIREWORKS_1D
    {"PS Fireworks 1D@Gravity,Explosion,Firing side,Blur,Color,Colorful,Trail,Smooth;,!;!;1;c2=30,o1=1",
     mode_particleFireworks1D},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_SPARKLER
    {"PS Sparkler@Move,!,Saturation,Blur,Sparklers,Slide,Bounce,Large;,!;!;1;pal=0,sx=255,c1=0,c2=0,c3=6",
     mode_particleSparkler},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_HOURGLASS
    {"PS Hourglass@Interval,!,Color,Blur,Gravity,Colorflip,Start,Fast Reset;,!;!;1;pal=34,sx=5,ix=200,c1=140,c2=80,"
     "c3=4,o1=1,o2=1,o3=1",
     mode_particleHourglass},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_SPRAY_1D
    {"PS Spray 1D@Speed(+/-),!,Position,Blur,Gravity(+/-),AgeColor,Bounce,Position Color;,!;!;1;sx=200,ix=220,c1=0,"
     "c2=0",
     mode_particle1Dspray},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_1D_BALANCE
    {"PS 1D Balance@!,!,Hardness,Blur,Tilt,Position Color,Wrap,Random;,!;!;1;pal=18,c2=0,c3=4,o1=1",
     mode_particleBalance},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_CHASE
    {"PS Chase@!,Density,Size,Hue,Blur,Playful,,Position Color;,!;!;1;pal=11,sx=50,c2=5,c3=0", mode_particleChase},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_STARBURST
    {"PS Starburst@Chance,Fragments,Size,Blur,Cooling,Gravity,Colorful,Push;,!;!;1;pal=52,sx=150,ix=150,c1=120,c2=0,"
     "c3=21",
     mode_particleStarburst},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_FIRE_1D
    {"PS Fire 1D@!,!,Cooling,Blur;,!;!;1;pal=35,sx=100,ix=50,c1=80,c2=100,c3=28,o1=1,o2=1", mode_particleFire1D},
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
