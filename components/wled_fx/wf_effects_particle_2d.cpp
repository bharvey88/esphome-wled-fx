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
  (WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_FIREWORKS || WLED_FX_FX_PS_FIRE || WLED_FX_FX_PS_VORTEX || \
   WLED_FX_FX_PS_VOLCANO || WLED_FX_FX_PS_BALLPIT || WLED_FX_FX_PS_WATERFALL || WLED_FX_FX_PS_BOX || \
   WLED_FX_FX_PS_FUZZY_NOISE || WLED_FX_FX_PS_IMPACT || WLED_FX_FX_PS_ATTRACTOR || \
   WLED_FX_FX_PS_GHOST_RIDER || WLED_FX_FX_PS_GALAXY)

#if WLED_FX_GROUP_PARTICLE_2D

namespace esphome {
namespace wled_fx {
namespace {

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_VORTEX
/*
  Particle System Vortex
  Particles sprayed from center with a rotating spray
  Uses palette for particle color
  by DedeHai (Damian Schneider)
*/
void mode_particlevortex(Segment &seg) {
  constexpr uint32_t NUMBEROFSOURCES = 8;
  const unsigned seg_len = seg.length();
  if (seg_len == 1)
    FX_FALLBACK_STATIC;
  ParticleSystem2D *PartSys = nullptr;
  uint32_t i, j;

  if (seg.call == 0) {  // initialization
    if (!initParticleSystem2D(seg, PartSys, NUMBEROFSOURCES))
      FX_FALLBACK_STATIC;  // allocation failed
#ifdef ESP8266
    PartSys->setMotionBlur(180);
#else
    PartSys->setMotionBlur(130);
#endif
    for (i = 0; i < std::min<uint32_t>(PartSys->numSources, NUMBEROFSOURCES); i++) {
      PartSys->sources[i].source.x = (PartSys->maxX + 1) >> 1;  // center
      PartSys->sources[i].source.y = (PartSys->maxY + 1) >> 1;  // center
      PartSys->sources[i].maxLife = 900;
      PartSys->sources[i].minLife = 800;
    }
    PartSys->setKillOutOfBounds(true);
  } else {
    PartSys = reinterpret_cast<ParticleSystem2D *>(seg.data);  // if not the first call, just set the pointer
  }

  if (PartSys == nullptr)
    FX_FALLBACK_STATIC;  // something went wrong, no data!

  PartSys->updateSystem();  // update system properties (dimensions and data pointers)
  // number of sprays to display, 1-8
  uint32_t spraycount = std::min<uint32_t>(PartSys->numSources, 1 + (seg.custom1 >> 5));
#ifdef ESP8266
  // need static particles in the center to reduce blinking (would be black every other frame without this
  // hack), just set them there fixed
  for (i = 1; i < 4; i++) {
    int partindex = static_cast<int>(PartSys->usedParticles) - static_cast<int>(i);
    if (partindex >= 0) {
      PartSys->particles[partindex].x = (PartSys->maxX + 1) >> 1;  // center
      PartSys->particles[partindex].y = (PartSys->maxY + 1) >> 1;  // center
      PartSys->particles[partindex].sat = 230;
      PartSys->particles[partindex].ttl = 256;  // keep alive
    }
  }
#endif

  if (seg.check1)
    PartSys->setSmearBlur(90);  // enable smear blur
  else
    PartSys->setSmearBlur(0);  // disable smear blur

  // update colors of the sprays
  for (i = 0; i < spraycount; i++) {
    uint32_t coloroffset = 0xFF / spraycount;
    PartSys->sources[i].source.hue = coloroffset * i;
  }

  // set rotation direction and speed
  // can use direction flag to determine current direction
  bool direction = seg.check2;  // no automatic direction change, set it to flag
  int32_t currentspeed = static_cast<int32_t>(seg.step);  // make a signed integer out of step

  if (seg.custom2 > 0) {  // automatic direction change enabled
    uint32_t changeinterval = 1040 - (static_cast<uint32_t>(seg.custom2) << 2);
    direction = seg.aux1 & 0x01;  // set direction according to flag

    if (seg.check3)  // random interval
      changeinterval = 20 + changeinterval + hw_random16(changeinterval);

    if (seg.call % changeinterval == 0) {  // flip direction on next frame
      seg.aux1 |= 0x02;                    // set the update flag (for random interval update)
      if (direction)
        seg.aux1 &= ~0x01;  // clear the direction flag
      else
        seg.aux1 |= 0x01;  // set the direction flag
    }
  }

  int32_t targetspeed = (direction ? 1 : -1) * (seg.speed << 3);
  int32_t speeddiff = targetspeed - currentspeed;
  int32_t speedincrement = speeddiff / 50;

  if (speedincrement == 0) {  // if speeddiff is not zero, make the increment at least 1 so it reaches target speed
    if (speeddiff < 0)
      speedincrement = -1;
    else if (speeddiff > 0)
      speedincrement = 1;
  }

  currentspeed += speedincrement;
  seg.aux0 += currentspeed;
  seg.step = static_cast<uint32_t>(currentspeed);  // save it back

  uint16_t angleoffset = 0xFFFF / spraycount;  // angle offset for an even distribution
  // intensity is emit speed, emit less on low speeds
  uint32_t skip = PS_P_HALFRADIUS / (seg.intensity + 1) + 1;
  if (seg.call % skip == 0) {
    // start with random spray so all get a chance to emit a particle if the maximum number of particles
    // alive is reached
    j = hw_random16(spraycount);
    for (i = 0; i < spraycount; i++) {  // emit one particle per spray (if available)
      PartSys->sources[j].var = (seg.custom3 >> 1);  // update speed variation
#ifdef ESP8266
      if (seg.call & 0x01)  // every other frame, do not emit to save particles
#endif
        PartSys->angleEmit(PartSys->sources[j], seg.aux0 + angleoffset * j, (seg.intensity >> 2) + 1);
      j = (j + 1) % spraycount;
    }
  }
  PartSys->update();  // update all particles and render to frame
}
#endif

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

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_VOLCANO
/*
  Particle Volcano
  Particles are sprayed from below, spray moves back and forth if option is set
  Uses palette for particle color
  by DedeHai (Damian Schneider)
*/
void mode_particlevolcano(Segment &seg) {
  constexpr uint32_t NUMBEROFSOURCES = 1;
  ParticleSystem2D *PartSys = nullptr;
  PSsettings2D volcanosettings;
  volcanosettings.asByte = 0b00000100;  // PS settings for volcano movement: bounceX is enabled
  uint8_t numSprays;  // note: so far only one tested but more is possible
  uint32_t i = 0;

  if (seg.call == 0) {                                   // initialization
    if (!initParticleSystem2D(seg, PartSys, NUMBEROFSOURCES))  // init, no additional data needed
      FX_FALLBACK_STATIC;                                // allocation failed or not 2D

    PartSys->setBounceY(true);
    PartSys->setGravity();             // enable with default gforce
    PartSys->setKillOutOfBounds(true);  // out of bounds particles dont return (except on top, gravity handles that)
    PartSys->setMotionBlur(230);        // anable motion blur

    numSprays = std::min<uint32_t>(PartSys->numSources, NUMBEROFSOURCES);  // number of sprays
    for (i = 0; i < numSprays; i++) {
      PartSys->sources[i].source.hue = hw_random16();
      PartSys->sources[i].source.x = PartSys->maxX / (numSprays + 1) * (i + 1);  // distribute evenly
      PartSys->sources[i].maxLife = 300;  // lifetime in frames
      PartSys->sources[i].minLife = 250;
      PartSys->sources[i].sourceFlags.collide = true;    // seeded particles will collide (if enabled)
      PartSys->sources[i].sourceFlags.perpetual = true;  // source never dies
    }
  } else {
    PartSys = reinterpret_cast<ParticleSystem2D *>(seg.data);  // if not the first call, just set the pointer
  }

  if (PartSys == nullptr)
    FX_FALLBACK_STATIC;  // something went wrong, no data!

  numSprays = std::min<uint32_t>(PartSys->numSources, NUMBEROFSOURCES);  // number of volcanoes

  // change source emitting color from time to time, emit one particle per spray
  // every nth frame, cycle color and emit particles (and update the sources)
  if (seg.call % (11 - (seg.intensity / 25)) == 0) {
    for (i = 0; i < numSprays; i++) {
      // reset to just above the lower edge that is allowed for bouncing particles, if zero, particles
      // already 'bounce' at start and loose speed
      PartSys->sources[i].source.y = PS_P_RADIUS + 5;
      // reset speed (so no extra particlesettin is required to keep the source 'afloat')
      PartSys->sources[i].source.vy = 0;
      PartSys->sources[i].source.hue++;  // = hw_random16(); //change hue of spray source (random looks bad)
      // set moving speed but keep the direction given by PS
      PartSys->sources[i].source.vx =
          PartSys->sources[i].source.vx > 0 ? (seg.custom1 >> 2) : -(seg.custom1 >> 2);
      PartSys->sources[i].vy = seg.speed >> 2;  // emitting speed (upwards)
      PartSys->sources[i].vx = 0;
      PartSys->sources[i].var = seg.custom3 >> 1;  // emiting variation = nozzle size (custom 3 goes from 0-31)
      PartSys->sprayEmit(PartSys->sources[i]);
      PartSys->setWallHardness(255);  // full hardness for source bounce
      // move the source
      PartSys->particleMoveUpdate(PartSys->sources[i].source, PartSys->sources[i].sourceFlags, &volcanosettings);
    }
  }

  // Particle System settings
  PartSys->updateSystem();  // update system properties (dimensions and data pointers)
  PartSys->setColorByAge(seg.check1);
  PartSys->setBounceX(seg.check2);
  PartSys->setWallHardness(seg.custom2);

  if (seg.check3)  // collisions enabled
    PartSys->enableParticleCollisions(true, seg.custom2);  // enable collisions and set particle collision hardness
  else
    PartSys->enableParticleCollisions(false);

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

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_BALLPIT
/*
  PS Ballpit: particles falling down, user can enable these three options: X-wraparound, side bounce, ground
  bounce sliders control falling speed, intensity (number of particles spawned), inter-particle collision
  hardness (0 means no particle collisions) and render saturation
  this is quite versatile, can be made to look like rain or snow or confetti etc.
  Uses palette for particle color
  by DedeHai (Damian Schneider)
*/
void mode_particlepit(Segment &seg) {
  ParticleSystem2D *PartSys = nullptr;

  if (seg.call == 0) {                                          // initialization
    if (!initParticleSystem2D(seg, PartSys, 0, 0, true, false))  // init
      FX_FALLBACK_STATIC;                                       // allocation failed or not 2D
    PartSys->setKillOutOfBounds(true);
    PartSys->setGravity();           // enable with default gravity
    PartSys->setUsedParticles(170);  // use 75% of available particles
  } else {
    PartSys = reinterpret_cast<ParticleSystem2D *>(seg.data);  // if not the first call, just set the pointer
  }
  if (PartSys == nullptr)
    FX_FALLBACK_STATIC;  // something went wrong, no data!

  PartSys->updateSystem();  // update system properties (dimensions and data pointers)

  PartSys->setWrapX(seg.check1);
  PartSys->setBounceX(seg.check2);
  PartSys->setBounceY(seg.check3);
  // limit to 100 min (if collisions are disabled, still want bouncy)
  PartSys->setWallHardness(std::min<uint8_t>(seg.custom2, 150));
  if (seg.custom2 > 0)
    PartSys->enableParticleCollisions(true, seg.custom2);  // enable collisions and set particle collision hardness
  else
    PartSys->enableParticleCollisions(false);

  uint32_t i;
  // every nth frame emit particles, stop emitting if set to zero
  if (seg.call % (128 - (seg.intensity >> 1)) == 0 && seg.intensity > 0) {
    for (i = 0; i < PartSys->usedParticles; i++) {  // emit particles
      if (PartSys->particles[i].ttl == 0) {         // find a dead particle
        // emit particle at random position over the top of the matrix (random16 is not random enough)
        // if speed is higher, make them die sooner
        PartSys->particles[i].ttl = 1500 - (seg.speed << 2) + hw_random16(500);
        PartSys->particles[i].x = hw_random(PartSys->maxX);  // random(PartSys->maxX >> 1) + (PartSys->maxX >> 2);
        // particles appear somewhere above the matrix, maximum is double the height
        PartSys->particles[i].y = (PartSys->maxY << 1);
        // side speed is +/-
        PartSys->particles[i].vx = static_cast<int16_t>(hw_random16(seg.speed >> 1)) - (seg.speed >> 2);
        PartSys->particles[i].vy = wf_map(seg.speed, 0, 255, -5, -100);  // downward speed
        PartSys->particles[i].hue = hw_random16();                       // set random color
        PartSys->particleFlags[i].collide = true;                        // enable collision for particle
        PartSys->particles[i].sat = ((seg.custom3) << 3) + 7;
        // set particle size
        if (seg.custom1 == 255) {
          PartSys->perParticleSize = true;
          PartSys->advPartProps[i].size = hw_random16(seg.custom1);  // set each particle to random size
        } else {
          PartSys->setParticleSize(seg.custom1);             // set global size
          PartSys->advPartProps[i].size = seg.custom1;       // also set individual size for consistency
        }
        break;  // emit only one particle per round
      }
    }
  }

  uint32_t frictioncoefficient = 1 + seg.check1;  // need more friction if wrapX is set, see below note
  if (seg.speed < 50)                             // for low speeds, apply more friction
    frictioncoefficient = 50 - seg.speed;

  // (3 + max(3, (seg.speed >> 2))) == 0) // note: if friction is too low, hard particles uncontrollably
  // 'wander' left and right if wrapX is enabled
  if (seg.call % 6 == 0)
    PartSys->applyFriction(frictioncoefficient);

  PartSys->update();  // update and render
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_WATERFALL
/*
  Particle Waterfall
  Uses palette for particle color, spray source at top emitting particles, many config options
  by DedeHai (Damian Schneider)
*/
void mode_particlewaterfall(Segment &seg) {
  ParticleSystem2D *PartSys = nullptr;
  uint8_t numSprays;
  uint32_t i = 0;

  if (seg.call == 0) {                            // initialization
    if (!initParticleSystem2D(seg, PartSys, 12))  // init, request 12 sources, no additional data needed
      FX_FALLBACK_STATIC;                         // allocation failed or not 2D

    PartSys->setGravity();              // enable with default gforce
    PartSys->setKillOutOfBounds(true);  // out of bounds particles dont return (except on top, gravity handles that)
    PartSys->setMotionBlur(190);        // anable motion blur
    PartSys->setSmearBlur(30);          // enable 2D blurring (smearing)
    for (i = 0; i < PartSys->numSources; i++) {
      PartSys->sources[i].source.hue = i * 90;
      PartSys->sources[i].sourceFlags.collide = true;  // seeded particles will collide
#ifdef ESP8266
      // lifetime in frames (ESP8266 has less particles, make them short lived to keep the water flowing)
      PartSys->sources[i].maxLife = 250;
      PartSys->sources[i].minLife = 100;
#else
      PartSys->sources[i].maxLife = 400;  // lifetime in frames
      PartSys->sources[i].minLife = 150;
#endif
    }
  } else {
    PartSys = reinterpret_cast<ParticleSystem2D *>(seg.data);  // if not the first call, just set the pointer
  }
  if (PartSys == nullptr)
    FX_FALLBACK_STATIC;  // something went wrong, no data!

  // Particle System settings
  PartSys->updateSystem();        // update system properties (dimensions and data pointers)
  PartSys->setWrapX(seg.check1);  // cylinder
  PartSys->setBounceX(seg.check2);  // walls
  PartSys->setBounceY(seg.check3);  // ground
  PartSys->setWallHardness(seg.custom2);
  // number of sprays depends on segment width
  numSprays = std::min<int32_t>(static_cast<int32_t>(PartSys->numSources), std::max<int32_t>(PartSys->maxXpixel / 6, 2));
  if (seg.custom2 > 0)                                     // collisions enabled
    PartSys->enableParticleCollisions(true, seg.custom2);  // enable collisions and set particle collision hardness
  else {
    PartSys->enableParticleCollisions(false);
    PartSys->setWallHardness(120);  // set hardness (for ground bounce) to fixed value if not using collisions
  }

  for (i = 0; i < numSprays; i++) {
    PartSys->sources[i].source.hue += 1 + hw_random16(seg.custom1 >> 1);  // change hue of spray source
  }

  // every nth frame, emit particles, do not emit if intensity is zero
  if (seg.call % (12 - (seg.intensity >> 5)) == 0 && seg.intensity > 0) {
    for (i = 0; i < numSprays; i++) {
      PartSys->sources[i].vy = -seg.speed >> 3;  // emitting speed, down
      // PartSys->sources[i].source.x = wf_map(seg.custom3, 0, 31, 0, (PartSys->maxXpixel - numSprays * 2) *
      // PS_P_RADIUS) + i * PS_P_RADIUS * 2; // emitter position
      PartSys->sources[i].source.x =
          wf_map(seg.custom3, 0, 31, 0, (PartSys->maxXpixel - numSprays) * PS_P_RADIUS) + i * PS_P_RADIUS * 2;
      // source y position, few pixels above the top to increase spreading before entering the matrix
      PartSys->sources[i].source.y = PartSys->maxY + (PS_P_RADIUS * ((i << 2) + 4));
      PartSys->sources[i].var = (seg.custom1 >> 3);  // emiting variation 0-32
      PartSys->sprayEmit(PartSys->sources[i]);
    }
  }

  if (seg.call % 20 == 0)
    PartSys->applyFriction(1);  // add just a tiny amount of friction to help smooth things

  PartSys->update();  // update and render
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_BOX
/*
  Particle Box, applies gravity to particles in either a random direction or random but only downwards
  (sloshing)
  Uses palette for particle color
  by DedeHai (Damian Schneider)
*/
void mode_particlebox(Segment &seg) {
  ParticleSystem2D *PartSys = nullptr;
  uint32_t i;

  if (seg.call == 0) {                                   // initialization
    if (!initParticleSystem2D(seg, PartSys, 1, 0, true))  // init
      FX_FALLBACK_STATIC;                                // allocation failed or not 2D
    PartSys->setBounceX(true);
    PartSys->setBounceY(true);
    seg.aux0 = hw_random16();  // position in perlin noise
  } else {
    PartSys = reinterpret_cast<ParticleSystem2D *>(seg.data);  // if not the first call, just set the pointer
  }

  if (PartSys == nullptr)
    FX_FALLBACK_STATIC;  // something went wrong, no data!

  PartSys->updateSystem();  // update system properties (dimensions and data pointers)
  PartSys->setWallHardness(std::min<uint8_t>(seg.custom2, 200));  // wall hardness is 200 or more
  // enable collisions and set particle collision hardness
  PartSys->enableParticleCollisions(true, std::max<int>(2, static_cast<int>(seg.custom2)));
  // max particle size based on matrix size
  int maxParticleSize = std::min<unsigned>((seg.width() * seg.height()) >> 2, 255U);
  unsigned currentParticleSize = wf_map(seg.custom3, 0, 31, 0, maxParticleSize);
  // 1% - 60%, reduce if using larger size
  PartSys->setUsedParticles(wf_map(seg.intensity, 0, 255, 2, 153) / (1 + (currentParticleSize >> 4)));
  if (seg.custom3 < 31)
    PartSys->setParticleSize(currentParticleSize);  // set global size if not max (resets perParticleSize)
  else
    PartSys->perParticleSize = true;  // per particle size, uses advPartProps.size (randomized below)

  // add in new particles if amount has changed
  for (i = 0; i < PartSys->usedParticles; i++) {
    if (PartSys->particles[i].ttl < 260) {  // initialize dead particles
      PartSys->particles[i].ttl = 260;      // full brigthness
      PartSys->particles[i].x = hw_random16(PartSys->maxX);
      PartSys->particles[i].y = hw_random16(PartSys->maxY);
      PartSys->particles[i].hue = hw_random8();     // make it colorful
      PartSys->particleFlags[i].perpetual = true;   // never die
      PartSys->particleFlags[i].collide = true;     // all particles colllide
      // random size, used only if size is set to max (seg.custom3=31)
      PartSys->advPartProps[i].size = hw_random8(maxParticleSize);
      break;  // only spawn one particle per frame for less chaotic transitions
    }
  }

  // how often the force is applied depends on speed setting
  if (seg.call % (((255 - seg.speed) >> 6) + 1) == 0 && seg.speed > 0) {
    int32_t xgravity;
    int32_t ygravity;
    int32_t increment = (seg.speed >> 6) + 1;

    if (seg.check2) {  // washing machine
      int speed = tristate_square8(seg.now >> 7, 90, 15) / ((400 - seg.speed) >> 3);
      seg.aux0 += speed;
      if (speed == 0)
        seg.aux0 = 190;  // down (= 270 degrees)
    } else {
      seg.aux0 -= increment;
    }

    if (seg.check1) {  // random, use perlin noise
      xgravity = (static_cast<int16_t>(perlin8(seg.aux0)) - 127);
      ygravity = (static_cast<int16_t>(perlin8(seg.aux0 + 10000)) - 127);
      // scale the gravity force
      xgravity = (xgravity * seg.custom1) / 128;
      ygravity = (ygravity * seg.custom1) / 128;
    } else {  // go in a circle
      xgravity = (static_cast<int32_t>(seg.custom1) * cos16_t(seg.aux0 << 8)) / 0xFFFF;
      ygravity = (static_cast<int32_t>(seg.custom1) * sin16_t(seg.aux0 << 8)) / 0xFFFF;
    }
    if (seg.check3) {  // sloshing, y force is always downwards
      if (ygravity > 0)
        ygravity = -ygravity;
    }

    PartSys->applyForce(xgravity, ygravity);
  }

  if ((seg.call & 0x0F) == 0)  // every 16th frame
    PartSys->applyFriction(1);

  PartSys->update();  // update and render
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_FUZZY_NOISE
/*
  Fuzzy Noise: Perlin noise 'gravity' mapping as in particles on 'noise hills' viewed from above
  calculates slope gradient at the particle positions and applies 'downhill' force, resulting in a fuzzy
  perlin noise display
  by DedeHai (Damian Schneider)
*/
void mode_particleperlin(Segment &seg) {
  ParticleSystem2D *PartSys = nullptr;
  uint32_t i;

  if (seg.call == 0) {                                   // initialization
    if (!initParticleSystem2D(seg, PartSys, 1, 0, true))  // init with 1 source and advanced properties
      FX_FALLBACK_STATIC;                                // allocation failed or not 2D

    PartSys->setKillOutOfBounds(true);  // should never happen, but lets make sure there are no stray particles
    PartSys->setMotionBlur(230);        // anable motion blur
    PartSys->setBounceY(true);
    seg.aux0 = rand();
  } else {
    PartSys = reinterpret_cast<ParticleSystem2D *>(seg.data);  // if not the first call, just set the pointer
  }

  if (PartSys == nullptr)
    FX_FALLBACK_STATIC;  // something went wrong, no data!

  PartSys->updateSystem();  // update system properties (dimensions and data pointers)
  PartSys->setWrapX(seg.check1);
  PartSys->setBounceX(!seg.check1);
  PartSys->setWallHardness(seg.custom1);  // wall hardness
  // enable collisions and set particle collision hardness
  PartSys->enableParticleCollisions(seg.check3, seg.custom1);
  PartSys->setUsedParticles(wf_map(seg.intensity, 0, 255, 25, 128));  // min is 10%, max is 50%
  PartSys->setSmearBlur(seg.check2 * 15);                             // enable 2D blurring (smearing)

  // apply 'gravity' from a 2D perlin noise map
  seg.aux0 += 1 + (seg.speed >> 5);  // noise z-position
  // update position in noise
  for (i = 0; i < PartSys->usedParticles; i++) {
    // revive dead particles (do not keep them alive forever, they can clump up, need to reseed)
    if (PartSys->particles[i].ttl == 0) {
      PartSys->particles[i].ttl = hw_random16(500) + 200;
      PartSys->particles[i].x = hw_random(PartSys->maxX);
      PartSys->particles[i].y = hw_random(PartSys->maxY);
      PartSys->particleFlags[i].collide = true;  // particle colllides
    }
    uint32_t scale = 16 - ((31 - seg.custom3) >> 1);
    uint16_t xnoise = PartSys->particles[i].x / scale;  // position in perlin noise, scaled by slider
    uint16_t ynoise = PartSys->particles[i].y / scale;
    int16_t baseheight = perlin8(xnoise, ynoise, seg.aux0);  // noise value at particle position
    PartSys->particles[i].hue = baseheight;                  // color particles to perlin noise value
    if (seg.call % 8 == 0) {  // do not apply the force every frame, is too chaotic
      int8_t xslope = (baseheight + static_cast<int16_t>(perlin8(xnoise - 10, ynoise, seg.aux0)));
      int8_t yslope = (baseheight + static_cast<int16_t>(perlin8(xnoise, ynoise - 10, seg.aux0)));
      PartSys->applyForce(i, xslope, yslope);
    }
  }

  if (seg.call % (16 - (seg.custom2 >> 4)) == 0)
    PartSys->applyFriction(2);

  PartSys->update();  // update and render
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_IMPACT
/*
  Particle smashing down like meteors and exploding as they hit the ground, has many parameters to play with
  by DedeHai (Damian Schneider)
*/
void mode_particleimpact(Segment &seg) {
  constexpr uint32_t NUMBEROFSOURCES = 8;
  ParticleSystem2D *PartSys = nullptr;
  uint32_t numMeteors;
  PSsettings2D meteorsettings;
  meteorsettings.asByte = 0b00101000;  // PS settings for meteors: bounceY and gravity enabled

  if (seg.call == 0) {                                   // initialization
    if (!initParticleSystem2D(seg, PartSys, NUMBEROFSOURCES))  // init, no additional data needed
      FX_FALLBACK_STATIC;                                // allocation failed or not 2D
    PartSys->setKillOutOfBounds(true);
    PartSys->setGravity();           // enable default gravity
    PartSys->setBounceY(true);       // always use ground bounce
    PartSys->setWallRoughness(220);  // high roughness
    numMeteors = std::min<uint32_t>(PartSys->numSources, NUMBEROFSOURCES);
    for (uint32_t i = 0; i < numMeteors; i++) {
      PartSys->sources[i].source.ttl = hw_random16(10 * i);  // set initial delay for meteors
      // at positive speeds, no particles are emitted and if particle dies, it will be relaunched
      PartSys->sources[i].source.vy = 10;
    }
  } else {
    PartSys = reinterpret_cast<ParticleSystem2D *>(seg.data);  // if not the first call, just set the pointer
  }

  if (PartSys == nullptr)
    FX_FALLBACK_STATIC;  // something went wrong, no data!

  // Particle System settings
  PartSys->updateSystem();  // update system properties (dimensions and data pointers)
  PartSys->setWrapX(seg.check1);
  PartSys->setBounceX(seg.check2);
  PartSys->setMotionBlur(seg.custom3 << 3);
  uint8_t hardness = wf_map(seg.custom2, 0, 255, PS_P_MINSURFACEHARDNESS - 2, 255);
  PartSys->setWallHardness(hardness);
  // enable collisions and set particle collision hardness
  PartSys->enableParticleCollisions(seg.check3, hardness);
  numMeteors = std::min<uint32_t>(PartSys->numSources, NUMBEROFSOURCES);
  uint32_t emitparticles;  // number of particles to emit for each rocket's state

  for (uint32_t i = 0; i < numMeteors; i++) {
    // determine meteor state by its speed:
    if (PartSys->sources[i].source.vy < 0)  // moving down, emit sparks
      emitparticles = 1;
    else if (PartSys->sources[i].source.vy > 0)  // moving up means meteor is on 'standby'
      emitparticles = 0;
    else {                                 // speed is zero, explode!
      PartSys->sources[i].source.vy = 10;  // set source speed positive so it goes into timeout and launches again
      // defines the size of the explosion
      emitparticles = wf_map(seg.intensity, 0, 255, 10, hw_random16(PartSys->usedParticles >> 2));
    }
    for (int e = emitparticles; e > 0; e--) {
      PartSys->sprayEmit(PartSys->sources[i]);
    }
  }

  // update the meteors, set the speed state
  for (uint32_t i = 0; i < numMeteors; i++) {
    if (PartSys->sources[i].source.ttl) {
      PartSys->sources[i].source.ttl--;  // note: this saves an if statement, but moving down particles age twice
      if (PartSys->sources[i].source.vy < 0) {  // move down
        PartSys->applyGravity(PartSys->sources[i].source);
        PartSys->particleMoveUpdate(PartSys->sources[i].source, PartSys->sources[i].sourceFlags, &meteorsettings);

        // if source reaches the bottom, set speed to 0 so it will explode on next function call (handled above)
        if (PartSys->sources[i].source.y < PS_P_RADIUS << 1) {  // reached the bottom pixel on its way down
          PartSys->sources[i].source.vy = 0;                    // set speed zero so it will explode
          PartSys->sources[i].source.vx = 0;
          PartSys->sources[i].sourceFlags.collide = true;
#ifdef ESP8266
          PartSys->sources[i].maxLife = 900;
          PartSys->sources[i].minLife = 100;
#else
          PartSys->sources[i].maxLife = 1250;
          PartSys->sources[i].minLife = 250;
#endif
          // standby time til next launch (in frames)
          PartSys->sources[i].source.ttl = hw_random16((768 - (seg.speed << 1))) + 40;
          PartSys->sources[i].vy = (seg.custom1 >> 2);   // emitting speed y
          PartSys->sources[i].var = (seg.custom1 >> 2);  // speed variation around vx,vy (+/- var)
        }
      }
    } else if (PartSys->sources[i].source.vy > 0) {  // meteor exploded and time is up, relaunch it
      // reinitialize meteor
      PartSys->sources[i].source.y = PartSys->maxY + (PS_P_RADIUS << 2);  // start 4 pixels above the top
      PartSys->sources[i].source.x = hw_random(PartSys->maxX);
      PartSys->sources[i].source.vy = -hw_random16(30) - 30;  // meteor downward speed
      // TODO: make this dependent on position so they do not move out of frame
      PartSys->sources[i].source.vx = hw_random16(50) - 25;
      PartSys->sources[i].source.hue = hw_random16();   // random color
      PartSys->sources[i].source.ttl = 500;             // long life, will explode at bottom
      PartSys->sources[i].sourceFlags.collide = false;  // trail particles will not collide
      PartSys->sources[i].maxLife = 300;                // spark particle life
      PartSys->sources[i].minLife = 100;
      PartSys->sources[i].vy = -9;  // emitting speed (down)
      PartSys->sources[i].var = 3;  // speed variation around vx,vy (+/- var)
    }
  }

  for (uint32_t i = 0; i < PartSys->usedParticles; i++) {
    // ttl is linked to brightness, this allows to use higher brightness but still a short spark lifespan
    if (PartSys->particles[i].ttl > 5)
      PartSys->particles[i].ttl -= 5;
  }

  PartSys->update();  // update and render
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_ATTRACTOR
/*
  Particle Attractor, a particle attractor sits in the matrix center, a spray bounces around and seeds
  particles
  uses inverse square law like in planetary motion
  Uses palette for particle color
  by DedeHai (Damian Schneider)
*/
void mode_particleattractor(Segment &seg) {
  ParticleSystem2D *PartSys = nullptr;
  PSsettings2D sourcesettings;
  // PS settings for bounceY, bounceY used for source movement (it always bounces whereas particles do not)
  sourcesettings.asByte = 0b00001100;
  PSparticleFlags attractorFlags;
  attractorFlags.asByte = 0;  // no flags set
  PSparticle *attractor;      // particle pointer to the attractor
  if (seg.call == 0) {        // initialization
    // init using 1 source and advanced particle settings
    if (!initParticleSystem2D(seg, PartSys, 1, sizeof(PSparticle), true))
      FX_FALLBACK_STATIC;  // allocation failed or not 2D
    PartSys->sources[0].source.hue = hw_random16();
    PartSys->sources[0].source.vx = -7;                // will collied with wall and get random bounce direction
    PartSys->sources[0].sourceFlags.collide = true;    // seeded particles will collide
    PartSys->sources[0].sourceFlags.perpetual = true;  // source does not age
#ifdef ESP8266
    PartSys->sources[0].maxLife = 200;  // lifetime in frames (ESP8266 has less particles)
    PartSys->sources[0].minLife = 30;
#else
    PartSys->sources[0].maxLife = 350;  // lifetime in frames
    PartSys->sources[0].minLife = 50;
#endif
    PartSys->sources[0].var = 4;     // emiting variation
    PartSys->setWallHardness(255);   // bounce forever
    PartSys->setWallRoughness(200);  // randomize wall bounce
  } else {
    PartSys = reinterpret_cast<ParticleSystem2D *>(seg.data);  // if not the first call, just set the pointer
  }

  if (PartSys == nullptr)
    FX_FALLBACK_STATIC;  // something went wrong, no data!

  // Particle System settings
  PartSys->updateSystem();  // update system properties (dimensions and data pointers)
  PartSys->setColorByAge(seg.check1);
  PartSys->setParticleSize(seg.custom1 >> 1);  // set size globally
  PartSys->setUsedParticles(wf_map(seg.intensity, 0, 255, 25, 190));
  attractor = reinterpret_cast<PSparticle *>(PartSys->PSdataEnd);
  // set attractor properties
  attractor->ttl = 100;  // never dies
  if (seg.check2) {
    if ((seg.call % 3) == 0)  // move slowly
      PartSys->particleMoveUpdate(*attractor, attractorFlags, &sourcesettings);  // move the attractor
  } else {
    attractor->x = PartSys->maxX >> 1;  // set to center
    attractor->y = PartSys->maxY >> 1;
  }
  if (seg.call == 0) {
    attractor->vx = PartSys->sources[0].source.vy;  // set to spray movemement but reverse x and y
    attractor->vy = PartSys->sources[0].source.vx;
  }

  if (seg.custom2 > 0)  // collisions enabled
    // enable collisions and set particle collision hardness
    PartSys->enableParticleCollisions(true, wf_map(seg.custom2, 1, 255, 120, 255));
  else
    PartSys->enableParticleCollisions(false);

  if (seg.call % 5 == 0)
    PartSys->sources[0].source.hue++;

  seg.aux0 += 256;          // emitting angle, one full turn in 255 frames (0xFFFF is 360 degrees)
  if (seg.call % 2 == 0)    // alternate direction of emit
    PartSys->angleEmit(PartSys->sources[0], seg.aux0, 12);
  else
    PartSys->angleEmit(PartSys->sources[0], seg.aux0 + 0x7FFF, 12);  // emit at 180 degrees as well
  // apply force
  uint32_t strength = seg.speed;
  // upstream asks the audioreactive usermod for real data and deliberately skips the simulation;
  // seg.has_real_audio() is that question, because seg.audio() falls back to simulateSound()
  if (seg.has_real_audio()) {  // AR active, do not use simulated data
    uint32_t volumeSmth = static_cast<uint32_t>(seg.audio().volume_smth);  // 0-255
    strength = (seg.speed * volumeSmth) >> 8;
  }
  for (uint32_t i = 0; i < PartSys->usedParticles; i++) {
    PartSys->pointAttractor(i, *attractor, strength, seg.check3);
  }

  if (seg.call % (33 - seg.custom3) == 0)
    PartSys->applyFriction(2);
  // move the source
  PartSys->particleMoveUpdate(PartSys->sources[0].source, PartSys->sources[0].sourceFlags, &sourcesettings);
  PartSys->update();  // update and render
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_GHOST_RIDER
/*
  Particle replacement of Ghost Rider by DedeHai (Damian Schneider), original FX by stepko adapted by Blaz
  Kristan (AKA blazoncek)
*/
void mode_particleghostrider(Segment &seg) {
  constexpr int32_t MAXANGLESTEP = 2200;  // 32767 means 180 degrees
  ParticleSystem2D *PartSys = nullptr;
  PSsettings2D ghostsettings;
  ghostsettings.asByte = 0b0000011;  // enable wrapX and wrapY

  if (seg.call == 0) {                           // initialization
    if (!initParticleSystem2D(seg, PartSys, 1))  // init, no additional data needed
      FX_FALLBACK_STATIC;                        // allocation failed or not 2D
    PartSys->setKillOutOfBounds(true);  // out of bounds particles dont return (except on top, gravity handles that)
    PartSys->sources[0].maxLife = 260;  // lifetime in frames
    PartSys->sources[0].minLife = 250;
    PartSys->sources[0].source.x = hw_random16(PartSys->maxX);
    PartSys->sources[0].source.y = hw_random16(PartSys->maxY);
    seg.step = hw_random16(MAXANGLESTEP) - (MAXANGLESTEP >> 1);  // angle increment
  } else {
    PartSys = reinterpret_cast<ParticleSystem2D *>(seg.data);  // if not the first call, just set the pointer
  }

  if (PartSys == nullptr)
    FX_FALLBACK_STATIC;  // something went wrong, no data!

  if (seg.intensity > 0) {  // spiraling
    if (seg.aux1) {
      seg.step += seg.intensity >> 3;
      if (static_cast<int32_t>(seg.step) > MAXANGLESTEP)
        seg.aux1 = 0;
    } else {
      seg.step -= seg.intensity >> 3;
      if (static_cast<int32_t>(seg.step) < -MAXANGLESTEP)
        seg.aux1 = 1;
    }
  }
  // Particle System settings
  PartSys->updateSystem();  // update system properties (dimensions and data pointers)
  PartSys->setMotionBlur(seg.custom1);
  PartSys->sources[0].var = seg.custom3 >> 1;

  // color by age (PS 'color by age' always starts with hue = 255, don't want that here)
  if (seg.check1) {
    for (uint32_t i = 0; i < PartSys->usedParticles; i++) {
      PartSys->particles[i].hue = PartSys->sources[0].source.hue + (PartSys->particles[i].ttl << 2);
    }
  }

  // enable/disable walls
  ghostsettings.bounceX = seg.check2;
  ghostsettings.bounceY = seg.check2;

  seg.aux0 += static_cast<int32_t>(seg.step);  // step is angle increment
  uint16_t emitangle = seg.aux0 + 32767;       // +180 degrees
  int32_t speed = wf_map(seg.speed, 0, 255, 12, 64);
  PartSys->sources[0].source.vx = (static_cast<int32_t>(cos16_t(seg.aux0)) * speed) / static_cast<int32_t>(32767);
  PartSys->sources[0].source.vy = (static_cast<int32_t>(sin16_t(seg.aux0)) * speed) / static_cast<int32_t>(32767);
  // source never dies (note: setting 'perpetual' is not needed if replenished each frame)
  PartSys->sources[0].source.ttl = 500;
  PartSys->particleMoveUpdate(PartSys->sources[0].source, PartSys->sources[0].sourceFlags, &ghostsettings);
  // set head (steal one of the particles)
  PartSys->particles[PartSys->usedParticles - 1].x = PartSys->sources[0].source.x;
  PartSys->particles[PartSys->usedParticles - 1].y = PartSys->sources[0].source.y;
  PartSys->particles[PartSys->usedParticles - 1].ttl = 255;
  PartSys->particles[PartSys->usedParticles - 1].sat = 0;  // white
  // emit two particles
  PartSys->angleEmit(PartSys->sources[0], emitangle, speed);
  PartSys->angleEmit(PartSys->sources[0], emitangle, speed);
  if (seg.call % (11 - (seg.custom2 / 25)) == 0) {  // every nth frame, cycle color and emit particles
    PartSys->sources[0].source.hue++;
  }
  if (seg.custom2 > 190)  // fast color change
    PartSys->sources[0].source.hue += (seg.custom2 - 190) >> 2;

  PartSys->update();  // update and render
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_GALAXY
/*
  Particle Galaxy, particles spiral like in a galaxy
  Uses palette for particle color
  by DedeHai (Damian Schneider)
*/
void mode_particlegalaxy(Segment &seg) {
  ParticleSystem2D *PartSys = nullptr;
  PSsettings2D sourcesettings;
  // PS settings for bounceY, bounceY used for source movement (it always bounces whereas particles do not)
  sourcesettings.asByte = 0b00001100;
  if (seg.call == 0) {  // initialization
    // init using 1 source and advanced particle settings
    if (!initParticleSystem2D(seg, PartSys, 1, 0, true))
      FX_FALLBACK_STATIC;                 // allocation failed or not 2D
    PartSys->sources[0].source.vx = -4;   // will collide with wall and get random bounce direction
    PartSys->sources[0].source.x = PartSys->maxX >> 1;  // start in the center
    PartSys->sources[0].source.y = PartSys->maxY >> 1;
    PartSys->sources[0].sourceFlags.perpetual = true;  // source does not age
    PartSys->sources[0].maxLife = 4000;                // lifetime in frames
    PartSys->sources[0].minLife = 800;
    PartSys->sources[0].source.hue = hw_random16();  // start with random color
    PartSys->setWallHardness(255);                   // bounce forever
    PartSys->setWallRoughness(200);                  // randomize wall bounce
  } else {
    PartSys = reinterpret_cast<ParticleSystem2D *>(seg.data);  // if not the first call, just set the pointer
  }
  if (PartSys == nullptr)
    FX_FALLBACK_STATIC;  // something went wrong, no data!
  // Particle System settings
  PartSys->updateSystem();  // update system properties (dimensions and data pointers)
  uint8_t particlesize = seg.custom1;
  PartSys->setParticleSize(particlesize);  // set size globally
  // adds trails to single/quad pixel particles, no effect if size > 1
  PartSys->setMotionBlur(250 * seg.check3);

  if ((seg.call % ((33 - seg.custom3) >> 1)) == 0)  // change hue of emitted particles
    PartSys->sources[0].source.hue += 2;

  if (hw_random8() < (10 + (seg.intensity >> 1)))  // 5%-55% chance to emit a particle in this frame
    PartSys->sprayEmit(PartSys->sources[0]);

  if ((seg.call & 0x3) == 0)  // every 4th frame, move the emitter
    PartSys->particleMoveUpdate(PartSys->sources[0].source, PartSys->sources[0].sourceFlags, &sourcesettings);

  // move alive particles in a spiral motion (or almost straight in fast starfield mode)
  int32_t centerx = PartSys->maxX >> 1;  // center of matrix in subpixel coordinates
  int32_t centery = PartSys->maxY >> 1;
  if (seg.check2) {  // starfield mode
    PartSys->setKillOutOfBounds(true);
    PartSys->sources[0].var = 7;             // emiting variation
    PartSys->sources[0].source.x = centerx;  // set emitter to center
    PartSys->sources[0].source.y = centery;
  } else {
    PartSys->setKillOutOfBounds(false);
    PartSys->sources[0].var = 1;  // emiting variation
  }
  for (uint32_t i = 0; i < PartSys->usedParticles; i++) {  // check all particles
    if (PartSys->particles[i].ttl == 0)
      continue;  // skip dead particles
    // (dx/dy): vector pointing from particle to center
    int32_t dx = centerx - PartSys->particles[i].x;
    int32_t dy = centery - PartSys->particles[i].y;
    // speed towards center:
    int32_t distance = sqrt32_bw(dx * dx + dy * dy);  // absolute distance to center
    if (distance < 20)
      distance = 20;  // avoid division by zero, keep a minimum
    int32_t speedfactor;
    if (seg.check2) {  // starfield mode
      speedfactor = 1 + (1 + (seg.speed >> 1)) * distance;  // speed increases towards edge
      // apply velocity
      PartSys->particles[i].x += (-speedfactor * dx) / 400000 - (dy >> 6);
      PartSys->particles[i].y += (-speedfactor * dy) / 400000 + (dx >> 6);
    } else {
      speedfactor = 2 + (((50 + seg.speed) << 6) / distance);  // speed increases towards center
      // rotate clockwise
      int32_t tempVx = (-speedfactor * dy);  // speed is orthogonal to center vector
      int32_t tempVy = (speedfactor * dx);
      // add speed towards center to make particles spiral in
      // subtract value from distance to make the pull-in force a bit stronger (helps on faster speeds)
      // dx and dy are signed offsets from the centre, so upstream's left shift
      // is undefined for half the panel. Multiply by the same power of two.
      int vxc = (dx * 512) / (distance - 19);
      int vyc = (dy * 512) / (distance - 19);
      // apply velocity
      PartSys->particles[i].x += (tempVx + vxc) / 1024;  // note: cannot use bit shift, asymmetric rounding
      PartSys->particles[i].y += (tempVy + vyc) / 1024;

      if (distance < 128) {  // close to center
        if (PartSys->particles[i].ttl > 3)
          PartSys->particles[i].ttl -= 4;         // age fast
        PartSys->particles[i].sat = distance << 1;  // turn white towards center
      }
    }
    // color by age but mapped to 1024 as particles have a long life, since age is random, this gives more
    // or less random colors
    if (seg.custom3 == 31)
      PartSys->particles[i].hue = PartSys->particles[i].ttl >> 2;
    else if (seg.custom3 == 0)  // color by distance
      PartSys->particles[i].hue = wf_map(distance, 20, (PartSys->maxX + PartSys->maxY) >> 2, 0, 180);
  }

  PartSys->update();  // update and render
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
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_VORTEX
    {"PS Vortex@Rotation Speed,Particle Speed,Arms,Flip,Nozzle,Smear,Direction,Random Flip;;!;2;pal=27,c1=200,c2=0,"
     "c3=0",
     mode_particlevortex},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_VOLCANO
    {"PS Volcano@Speed,Intensity,Move,Bounce,Spread,AgeColor,Walls,Collide;;!;2;pal=35,sx=100,ix=190,c1=0,c2=160,"
     "c3=6,o1=1",
     mode_particlevolcano},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_BALLPIT
    {"PS Ballpit@Speed,Intensity,Size,Hardness,Saturation,Cylinder,Walls,Ground;;!;2;pal=11,sx=100,ix=220,c1=70,"
     "c2=180,c3=31,o3=1",
     mode_particlepit},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_WATERFALL
    {"PS Waterfall@Speed,Intensity,Variation,Collide,Position,Cylinder,Walls,Ground;;!;2;pal=9,sx=15,ix=200,c1=32,"
     "c2=160,o3=1",
     mode_particlewaterfall},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_BOX
    {"PS Box@!,Particles,Tilt,Hardness,Size,Random,Washing Machine,Sloshing;;!;2;pal=53,ix=50,c3=1,o1=1",
     mode_particlebox},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_FUZZY_NOISE
    {"PS Fuzzy Noise@Speed,Particles,Bounce,Friction,Scale,Cylinder,Smear,Collide;;!;2;pal=64,sx=50,ix=200,c1=130,"
     "c2=30,c3=5",
     mode_particleperlin},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_IMPACT
    {"PS Impact@Launches,!,Force,Hardness,Blur,Cylinder,Walls,Collide;;!;2;pal=0,sx=32,ix=85,c1=70,c2=130,c3=0,o3=1",
     mode_particleimpact},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_ATTRACTOR
    {"PS Attractor@Mass,Particles,Size,Collide,Friction,AgeColor,Move,Swallow;;!;2;pal=9,sx=100,ix=82,c1=2,c2=0",
     mode_particleattractor},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_GHOST_RIDER
    {"PS Ghost Rider@Speed,Spiral,Blur,Color Cycle,Spread,AgeColor,Walls;;!;2;pal=1,sx=70,ix=0,c1=220,c2=30,c3=21,"
     "o1=1",
     mode_particleghostrider},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_GALAXY
    {"PS Galaxy@!,!,Size,,Color,,Starfield,Trace;;!;2;pal=59,sx=80,c1=1,c3=4", mode_particlegalaxy},
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
