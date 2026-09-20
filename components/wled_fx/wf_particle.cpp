/* Particle system with functions for particle generation, particle movement and
 * particle rendering to an RGB matrix. Ported from WLED 16.0.1
 * wled00/FXparticleSystem.cpp with the transform described in PORTING.md.
 *
 * by DedeHai (Damian Schneider) 2013-2024
 * Copyright (c) 2024 Damian Schneider
 * Licensed under the EUPL v. 1.2 or later, distributed here under GPLv3 per the
 * EUPL Article 5 compatibility clause.
 */

#include "wf_particle.h"

#include <algorithm>
#include <cstring>
#include <new>

#include "wf_math.h"

namespace esphome {
namespace wled_fx {

namespace {

/* WLED's global gamma switch, which defaults to true (wled00/wled.h:412) and is
 * what turns on the matched gamma and inverse-gamma pair this renderer is built
 * around: the per-particle brightness is gamma corrected before it is spread over
 * the sub-pixels, and each sub-pixel weight then gets the inverse, so the spatial
 * falloff is linear once the output stage applies gamma again.
 *
 * Both halves live inside the render and both are visible in WLED's pre-output
 * buffer, so the engine reproduces them. Dropping them is not the same as turning
 * gamma off: it leaves the sub-pixel weights uncompensated, which costs a particle
 * effect between 15 and 57 percent of its brightness and hardens every edge. See
 * wf_color.h for where the output stage lives instead. */
constexpr bool gammaCorrectCol = true;

// Arduino turns `abs` into a macro, which eats std::abs at the call site, so the
// engine uses its own. See PORTING.md section 6.
inline int32_t ps_abs(const int32_t v) { return v < 0 ? -v : v; }

// local shared functions (used both in the 1D and the 2D system)

// Calculate the delta speed (dV) value and update the counter for force
// calculation. Force is in 3.4 fixed point notation, +/-127.
int32_t calcForce_dv(const int8_t force, uint8_t &counter) {
  if (force == 0)
    return 0;
  int32_t force_abs = ps_abs(force);
  int32_t dv = 0;
  // for small forces, need to use a delay counter, apply force only if it overflows
  if (force_abs < 16) {
    counter += force_abs;
    if (counter > 15) {
      counter -= 16;
      dv = force < 0 ? -1 : 1;  // force is either 1 or -1 if it is small (zero force is handled above)
    }
  } else {
    dv = force / 16;  // MSBs, note: cannot use a bitshift as dv can be negative
  }

  return dv;
}

// Check if a particle is out of bounds and wrap it around if required, returns
// false if out of bounds.
bool checkBoundsAndWrap(int32_t &position, const int32_t max, const int32_t particleradius, const bool wrap) {
  if ((uint32_t) position > (uint32_t) max) {  // cast to uint32_t saves the negative check, max is always positive
    if (wrap) {
      position = position % (max + 1);  // cannot optimize the modulo, particles can be far out when wrapping
      if (position < 0)
        position += max + 1;
    } else if (((position < -particleradius) || (position > max + particleradius))) {
      return false;  // particle has fully left the boundaries
    }
  }
  return true;  // particle is in bounds
}

// Fast RGB colour adding ignoring the white channel (the PS does not handle
// white), including scaling of the second colour.
uint32_t fast_color_scaleAdd(const uint32_t c1, const uint32_t c2, const uint8_t scale = 255) {
  constexpr uint32_t MASK_RB = 0x00FF00FF;  // red and blue mask
  constexpr uint32_t MASK_G = 0x0000FF00;   // green mask

  uint32_t rb = c2 & MASK_RB;  // 0x00RR00BB
  uint32_t g = c2 & MASK_G;    // 0x0000GG00
  // scale second color
  rb = ((rb * scale) >> 8) & MASK_RB;
  g = ((g * scale) >> 8) & MASK_G;
  // add colors
  rb = (c1 & MASK_RB) + rb;
  g = ((c1 & MASK_G) + g);

  // check for overflow by looking at the 9th bit of each channel
  if ((rb | (g >> 8)) & 0x01000100) {
    // find max among the three 16 bit values
    g = g >> 8;                     // shift to get 0x000000GG
    uint32_t max_val = (rb >> 16);  // red
    max_val = ((rb & 0xFFFF) > max_val) ? rb & 0xFFFF : max_val;  // blue
    max_val = (g > max_val) ? g : max_val;                        // green
    // scale down to avoid saturation
    uint32_t scale_factor = (255 << 8) / max_val;
    rb = ((rb * scale_factor) >> 8) & MASK_RB;
    g = (g * scale_factor) & MASK_G;
  }
  return rb | g;
}

}  // namespace

// WLED's Segment::maxMappingLength(), the diagonal or the pinwheel length,
// whichever is longer.
uint32_t particleMaxMappingLength(const Segment &seg) {
  const uint32_t vW = seg.width();
  const uint32_t vH = seg.height();
  const uint32_t diagonal = sqrt32_bw(vH * vH + vW * vW);
  const uint32_t pinwheel = static_cast<uint32_t>(Segment::pinwheel_length(vW, vH));
  return diagonal > pinwheel ? diagonal : pinwheel;
}

////////////////////////
// 2D Particle System //
////////////////////////

ParticleSystem2D::ParticleSystem2D(Segment &segment, uint32_t width, uint32_t height, uint32_t numberofparticles,
                                   uint32_t numberofsources, uint32_t binarrayentries, bool isadvanced,
                                   bool sizecontrol) {
  seg = &segment;
  numSources = numberofsources;      // number of sources allocated in init
  numParticles = numberofparticles;  // number of particles allocated in init
  usedParticles = numParticles;      // use all particles by default
  binArrayEntries = binarrayentries;
  advPartProps = nullptr;  // make sure we start out with null pointers (just in case memory was not cleared)
  advPartSize = nullptr;
  setMatrixSize(width, height);
  updatePSpointers(isadvanced, sizecontrol);  // set the particle and source pointers
  setWallHardness(255);                       // set default wall hardness to max
  setWallRoughness(0);                        // smooth walls by default
  setGravity(0);                              // gravity disabled by default
  setParticleSize(1);                         // 2x2 rendering size by default
  motionBlur = 0;                             // no fading by default
  smearBlur = 0;                              // no smearing by default
  emitIndex = 0;
  collisionStartIdx = 0;
  forcecounter = 0;
  gforcecounter = 0;

  // initialize some default non-zero values most FX use
  for (uint32_t i = 0; i < numParticles; i++) {
    particles[i].sat = 255;  // full saturation
  }
  for (uint32_t i = 0; i < numSources; i++) {
    sources[i].source.sat = 255;      // set saturation to max by default
    sources[i].source.ttl = 1;        // set source alive
    sources[i].sourceFlags.asByte = 0;  // all flags disabled
  }
  perParticleSize = isadvanced;  // enable per particle size by default if using advanced properties
}

// update function applies gravity, moves the particles, handles collisions and renders the particles
void ParticleSystem2D::update() {
  // apply gravity globally if enabled
  if (particlesettings.useGravity)
    applyGravity();

  // update size settings before handling collisions
  if (advPartSize != nullptr) {
    for (uint32_t i = 0; i < usedParticles; i++) {
      if (updateSize(&advPartProps[i], &advPartSize[i]) == false) {  // if particle shrinks to 0 size
        particles[i].ttl = 0;                                        // kill particle
      }
    }
  }

  // handle collisions (can push particles, must be done before updating particles or they can render out of bounds)
  if (particlesettings.useCollisions)
    handleCollisions();

  // move all particles
  for (uint32_t i = 0; i < usedParticles; i++) {
    particleMoveUpdate(particles[i], particleFlags[i], nullptr, advPartProps ? &advPartProps[i] : nullptr);
  }

  render();
}

// update function for fire animation
void ParticleSystem2D::updateFire(const uint8_t intensity) {
  fireParticleupdate();
  fireIntesity = intensity > 0 ? intensity : 1;  // minimum of 1, zero checking is used in the render function
  render();
}

// set percentage of used particles as uint8_t i.e 127 means 50% for example
void ParticleSystem2D::setUsedParticles(uint8_t percentage) {
  usedParticles = std::max<uint32_t>(1, (numParticles * (static_cast<int>(percentage) + 1)) >> 8);
}

void ParticleSystem2D::setWallHardness(uint8_t hardness) { wallHardness = hardness; }

void ParticleSystem2D::setWallRoughness(uint8_t roughness) { wallRoughness = roughness; }

void ParticleSystem2D::setCollisionHardness(uint8_t hardness) { collisionHardness = static_cast<int>(hardness) + 1; }

void ParticleSystem2D::setMatrixSize(uint32_t x, uint32_t y) {
  maxXpixel = x - 1;  // last physical pixel that can be drawn to
  maxYpixel = y - 1;
  maxX = x * PS_P_RADIUS - 1;  // particle system boundary for movements
  maxY = y * PS_P_RADIUS - 1;  // this value is often needed (also by FX) to calculate positions
}

void ParticleSystem2D::setWrapX(bool enable) { particlesettings.wrapX = enable; }

void ParticleSystem2D::setWrapY(bool enable) { particlesettings.wrapY = enable; }

void ParticleSystem2D::setBounceX(bool enable) { particlesettings.bounceX = enable; }

void ParticleSystem2D::setBounceY(bool enable) { particlesettings.bounceY = enable; }

void ParticleSystem2D::setKillOutOfBounds(bool enable) { particlesettings.killoutofbounds = enable; }

void ParticleSystem2D::setColorByAge(bool enable) { particlesettings.colorByAge = enable; }

void ParticleSystem2D::setMotionBlur(uint8_t bluramount) { motionBlur = bluramount; }

void ParticleSystem2D::setSmearBlur(uint8_t bluramount) { smearBlur = bluramount; }

// set the global colour saturation of all particles
void ParticleSystem2D::setSaturation(uint8_t sat) {
  for (uint32_t i = 0; i < numParticles; i++) {
    particles[i].sat = sat;
  }
}

// set global particle size
void ParticleSystem2D::setParticleSize(uint8_t size) {
  particlesize = size;
  particleHardRadius = PS_P_MINHARDRADIUS;  // ~1 pixel
  perParticleSize = false;                  // disable per particle size control if global size is set
  if (particlesize > 1) {
    // use 1 pixel + 80% of size for the hard radius (slight overlap with the borders so they do not "float")
    particleHardRadius = PS_P_MINHARDRADIUS + ((particlesize * 52) >> 6);
  } else if (particlesize == 0) {
    particleHardRadius = PS_P_MINHARDRADIUS >> 1;  // single pixel particles have half the radius
  }
}

// enable/disable gravity, optionally set the force (force=8 is default) can be -127 to +127, 0 is disable
// force is in 3.4 fixed point notation so force=16 means apply v+1 each frame, the default of 8 is every other frame
void ParticleSystem2D::setGravity(int8_t force) {
  if (force) {
    gforce = force;
    particlesettings.useGravity = true;
  } else {
    particlesettings.useGravity = false;
  }
}

void ParticleSystem2D::enableParticleCollisions(bool enable, uint8_t hardness) {
  particlesettings.useCollisions = enable;
  collisionHardness = static_cast<int>(hardness) + 1;
}

// emit one particle with variation, returns index of emitted particle (or -1 if no particle emitted)
int32_t ParticleSystem2D::sprayEmit(const PSsource &emitter) {
  bool success = false;
  for (uint32_t i = 0; i < usedParticles; i++) {
    emitIndex++;
    if (emitIndex >= usedParticles)
      emitIndex = 0;
    if (particles[emitIndex].ttl == 0) {  // find a dead particle
      success = true;
      int32_t dx = hw_random16(emitter.var << 1) - emitter.var;
      int32_t dy = hw_random16(emitter.var << 1) - emitter.var;
      if (emitter.var > 5) {  // use circular random distribution for large variance for nicer "explosions"
        while (dx * dx + dy * dy > emitter.var * emitter.var) {  // reject points outside the circle
          dx = hw_random16(emitter.var << 1) - emitter.var;
          dy = hw_random16(emitter.var << 1) - emitter.var;
        }
      }
      particles[emitIndex].vx = emitter.vx + dx;
      particles[emitIndex].vy = emitter.vy + dy;
      particles[emitIndex].x = emitter.source.x;
      particles[emitIndex].y = emitter.source.y;
      particles[emitIndex].hue = emitter.source.hue;
      particles[emitIndex].sat = emitter.source.sat;
      particleFlags[emitIndex].collide = emitter.sourceFlags.collide;
      particles[emitIndex].ttl = hw_random16(emitter.minLife, emitter.maxLife);
      if (advPartProps != nullptr)
        advPartProps[emitIndex].size = emitter.size;
      break;
    }
  }
  if (success)
    return emitIndex;
  else
    return -1;
}

// Spray emitter for particles used for flames (particle TTL depends on source TTL)
void ParticleSystem2D::flameEmit(const PSsource &emitter) {
  int emitIndex = sprayEmit(emitter);
  if (emitIndex > 0)
    particles[emitIndex].ttl += emitter.source.ttl;
}

// Emits a particle at the given angle and speed, angle is 0-65535 (=0-360deg), speed is also affected by emitter->var
// angle = 0 means in positive x-direction (i.e. to the right)
int32_t ParticleSystem2D::angleEmit(PSsource &emitter, const uint16_t angle, const int32_t speed) {
  // cos16_t() and sin16_t() return signed 16 bit, the division should be 32767 but 32600 rounds slightly better
  emitter.vx = (static_cast<int32_t>(cos16_t(angle)) * speed) / static_cast<int32_t>(32600);
  // note: cannot use bit shifts, shifting is asymmetrical (1>>1=0 / -1>>1=-1) and this needs to be accurate
  emitter.vy = (static_cast<int32_t>(sin16_t(angle)) * speed) / static_cast<int32_t>(32600);
  return sprayEmit(emitter);
}

// particle moves, decays and dies, if killoutofbounds is set, out of bounds particles are set to ttl=0
// uses the passed settings to set bounce or wrap, if useGravity is enabled it will never bounce at the top
void ParticleSystem2D::particleMoveUpdate(PSparticle &part, PSparticleFlags &partFlags, PSsettings2D *options,
                                          PSadvancedParticle *advancedproperties) {
  if (options == nullptr)
    options = &particlesettings;  // use PS system settings by default

  if (part.ttl > 0) {
    if (!partFlags.perpetual)
      part.ttl--;  // age
    if (options->colorByAge)
      part.hue = std::min<uint16_t>(part.ttl, 255);  // set color to ttl

    // used to check out of bounds: more than half a radius out and it renders to x = -2/-1 or x = max/max+1
    int32_t renderradius = PS_P_HALFRADIUS - 1 + particlesize;
    int32_t newX = part.x + static_cast<int32_t>(part.vx);
    int32_t newY = part.y + static_cast<int32_t>(part.vy);
    partFlags.outofbounds = false;  // reset out of bounds (the particle may have been created outside the matrix)

    if (perParticleSize && advancedproperties != nullptr) {  // using individual particle size
      renderradius = PS_P_HALFRADIUS - 1 + advancedproperties->size;
      if (advancedproperties->size > 0)
        particleHardRadius = PS_P_MINHARDRADIUS + ((advancedproperties->size * 52) >> 6);
      else  // single pixel particles use half the collision distance for walls
        particleHardRadius = PS_P_MINHARDRADIUS >> 1;
    }
    // note: if wall collisions are enabled, bounce them before they reach the edge, it looks much nicer
    if (options->bounceY) {
      if ((newY < static_cast<int32_t>(particleHardRadius)) ||
          ((newY > static_cast<int32_t>(maxY - particleHardRadius)) && !options->useGravity)) {  // floor / ceiling
        bounce(part.vy, part.vx, newY, maxY);
      }
    }

    // check out of bounds. this must not be skipped: with gravity enabled particles never bounce at the top
    if (!checkBoundsAndWrap(newY, maxY, renderradius, options->wrapY)) {
      partFlags.outofbounds = true;
      if (options->killoutofbounds) {
        if (newY < 0)  // if gravity is enabled, only kill particles below ground
          part.ttl = 0;
        else if (!options->useGravity)
          part.ttl = 0;
      }
    }

    if (part.ttl) {  // check x direction only if still alive
      if (options->bounceX) {
        if ((newX < static_cast<int32_t>(particleHardRadius)) ||
            (newX > static_cast<int32_t>(maxX - particleHardRadius)))  // reached a wall
          bounce(part.vx, part.vy, newX, maxX);
      } else if (!checkBoundsAndWrap(newX, maxX, renderradius, options->wrapX)) {  // check out of bounds
        partFlags.outofbounds = true;
        if (options->killoutofbounds)
          part.ttl = 0;
      }
    }

    part.x = static_cast<int16_t>(newX);  // set new position
    part.y = static_cast<int16_t>(newY);  // set new position
  }
}

// move function for fire particles
void ParticleSystem2D::fireParticleupdate() {
  for (uint32_t i = 0; i < usedParticles; i++) {
    if (particles[i].ttl > 0) {
      particles[i].ttl--;  // age
      // younger particles move faster upward as they are hotter
      int32_t newY = particles[i].y + static_cast<int32_t>(particles[i].vy) + (particles[i].ttl >> 2);
      int32_t newX = particles[i].x + static_cast<int32_t>(particles[i].vx);
      particleFlags[i].outofbounds = 0;  // reset out of bounds flag
      // fire particles start below the frame, so lots are out of bounds in y: only check x if y is in frame
      if (newY < -PS_P_HALFRADIUS)
        particleFlags[i].outofbounds = 1;
      else if (newY > static_cast<int32_t>(maxY + PS_P_HALFRADIUS))  // particle moved out at the top
        particles[i].ttl = 0;
      else {  // particle is in frame in y direction, also check x direction now
        if ((newX < 0) || (newX > static_cast<int32_t>(maxX))) {  // handle out of bounds and wrap
          if (particlesettings.wrapX) {
            newX = newX % (maxX + 1);
            if (newX < 0)  // handle negative modulo
              newX += maxX + 1;
          } else if ((newX < -PS_P_HALFRADIUS) || (newX > static_cast<int32_t>(maxX + PS_P_HALFRADIUS))) {
            particles[i].ttl = 0;  // fully out of view
          }
        }
        particles[i].x = newX;
      }
      particles[i].y = newY;
    }
  }
}

// update advanced particle size control, returns false if the particle shrinks to 0 size
bool ParticleSystem2D::updateSize(PSadvancedParticle *advprops, PSsizeControl *advsize) {
  if (advsize == nullptr)  // safety check
    return false;
  // grow/shrink particle
  int32_t newsize = advprops->size;
  uint32_t counter = advsize->sizecounter;
  uint32_t increment = 0;
  // calculate grow speed using 0-8 for low speeds and 9-15 for higher speeds
  if (advsize->grow)
    increment = advsize->growspeed;
  else if (advsize->shrink)
    increment = advsize->shrinkspeed;
  if (increment < 9) {  // 8 means +1 every frame
    counter += increment;
    if (counter > 7) {
      counter -= 8;
      increment = 1;
    } else {
      increment = 0;
    }
    advsize->sizecounter = counter;
  } else {
    increment = (increment - 8) << 1;  // 9 means +2, 10 means +4 etc. 15 means +14
  }

  if (advsize->grow) {
    if (newsize < advsize->maxsize) {
      newsize += increment;
      if (newsize >= advsize->maxsize) {
        advsize->grow = false;       // stop growing, shrink from now on if enabled
        newsize = advsize->maxsize;  // limit
        if (advsize->pulsate)
          advsize->shrink = true;
      }
    }
  } else if (advsize->shrink) {
    if (newsize > advsize->minsize) {
      newsize -= increment;
      if (newsize <= advsize->minsize) {
        if (advsize->minsize == 0)
          return false;            // particle shrunk to zero
        advsize->shrink = false;   // disable shrinking
        newsize = advsize->minsize;  // limit
        if (advsize->pulsate)
          advsize->grow = true;
      }
    }
  }
  advprops->size = newsize;
  // handle wobbling
  if (advsize->wobble) {
    advsize->asymdir += advsize->wobblespeed;
  }
  return true;
}

// calculate x and y size for asymmetrical particles (advanced size control)
void ParticleSystem2D::getParticleXYsize(PSadvancedParticle *advprops, PSsizeControl *advsize, uint32_t &xsize,
                                         uint32_t &ysize) {
  if (advsize == nullptr)  // if advsize is valid, the advanced properties pointer is valid too
    return;
  int32_t size = advprops->size;
  int32_t asymdir = advsize->asymdir;
  // deviation from symmetrical size
  int32_t deviation = (static_cast<uint32_t>(size) * static_cast<uint32_t>(advsize->asymmetry) + 255) >> 8;
  // calculate x and y size from deviation and direction (0 is symmetrical, 64 is x, 128 symmetrical, 192 is y)
  if (asymdir < 64) {
    deviation = (asymdir * deviation) >> 6;
  } else if (asymdir < 192) {
    deviation = ((128 - asymdir) * deviation) >> 6;
  } else {
    deviation = ((asymdir - 255) * deviation) >> 6;
  }
  // limit to 255, the rendering function cannot handle larger sizes
  xsize = std::min<int32_t>(size - deviation, 255);
  ysize = std::min<int32_t>(size + deviation, 255);
}

// bounce a particle off a wall using the set parameters (wallHardness and wallRoughness)
void ParticleSystem2D::bounce(int8_t &incomingspeed, int8_t &parallelspeed, int32_t &position,
                              const uint32_t maxposition) {
  incomingspeed = -incomingspeed;
  incomingspeed = (incomingspeed * wallHardness + 128) >> 8;  // energy is lost on a non-hard surface
  if (position < static_cast<int32_t>(particleHardRadius))
    position = particleHardRadius;  // fast particles never reach the edge if the position is inverted
  else
    position = maxposition - particleHardRadius;
  if (wallRoughness) {
    int32_t incomingspeed_abs = ps_abs(static_cast<int32_t>(incomingspeed));
    int32_t totalspeed = incomingspeed_abs + ps_abs(static_cast<int32_t>(parallelspeed));
    // transfer an amount of the incoming speed to the parallel speed
    int32_t donatespeed = ((hw_random16(incomingspeed_abs << 1) - incomingspeed_abs) *
                           static_cast<int32_t>(wallRoughness)) /
                          static_cast<int32_t>(255);
    parallelspeed = limitSpeed(static_cast<int32_t>(parallelspeed) + donatespeed);
    // give the remainder of the speed to the perpendicular speed
    donatespeed = int8_t(totalspeed - ps_abs(parallelspeed));  // keep total speed the same
    incomingspeed = incomingspeed > 0 ? donatespeed : -donatespeed;
  }
}

// apply a force in x,y direction to an individual particle
// the caller needs to provide an 8 bit counter (for each particle) that holds its value between calls
// force is in 3.4 fixed point notation so force=16 means apply v+1 each frame, the default 8 is every other frame
void ParticleSystem2D::applyForce(PSparticle &part, const int8_t xforce, const int8_t yforce, uint8_t &counter) {
  // for small forces, need to use a delay counter
  uint8_t xcounter = counter & 0x0F;  // lower four bits
  uint8_t ycounter = counter >> 4;    // upper four bits

  // velocity increase
  int32_t dvx = calcForce_dv(xforce, xcounter);
  int32_t dvy = calcForce_dv(yforce, ycounter);

  // save counter values back
  counter = xcounter & 0x0F;          // write lower four bits, make sure not to write more than 4 bits
  counter |= (ycounter << 4) & 0xF0;  // write upper four bits

  // apply the force to the particle
  part.vx = limitSpeed(static_cast<int32_t>(part.vx) + dvx);
  part.vy = limitSpeed(static_cast<int32_t>(part.vy) + dvy);
}

// apply a force in x,y direction to an individual particle using advanced particle properties
void ParticleSystem2D::applyForce(const uint32_t particleindex, const int8_t xforce, const int8_t yforce) {
  if (advPartProps == nullptr)
    return;  // no advanced properties available
  applyForce(particles[particleindex], xforce, yforce, advPartProps[particleindex].forcecounter);
}

// apply a force in x,y direction to all particles
void ParticleSystem2D::applyForce(const int8_t xforce, const int8_t yforce) {
  // for small forces, need to use a delay counter
  uint8_t tempcounter = forcecounter;
  for (uint32_t i = 0; i < usedParticles; i++) {
    tempcounter = forcecounter;
    applyForce(particles[i], xforce, yforce, tempcounter);
  }
  forcecounter = tempcounter;  // save value back
}

// apply a force in an angular direction to a single particle
// angle is 0-65535 (=0-360deg), angle = 0 means in the positive x direction
void ParticleSystem2D::applyAngleForce(PSparticle &part, const int8_t force, const uint16_t angle, uint8_t &counter) {
  int8_t xforce = (static_cast<int32_t>(force) * cos16_t(angle)) / 32767;  // force is +/- 127
  int8_t yforce = (static_cast<int32_t>(force) * sin16_t(angle)) / 32767;
  applyForce(part, xforce, yforce, counter);
}

void ParticleSystem2D::applyAngleForce(const uint32_t particleindex, const int8_t force, const uint16_t angle) {
  if (advPartProps == nullptr)
    return;  // no advanced properties available
  applyAngleForce(particles[particleindex], force, angle, advPartProps[particleindex].forcecounter);
}

// apply a force in an angular direction to all particles
void ParticleSystem2D::applyAngleForce(const int8_t force, const uint16_t angle) {
  int8_t xforce = (static_cast<int32_t>(force) * cos16_t(angle)) / 32767;  // force is +/- 127
  int8_t yforce = (static_cast<int32_t>(force) * sin16_t(angle)) / 32767;
  applyForce(xforce, yforce);
}

// apply gravity to all particles using the PS global gforce setting
void ParticleSystem2D::applyGravity() {
  int32_t dv = calcForce_dv(gforce, gforcecounter);
  if (dv == 0)
    return;
  for (uint32_t i = 0; i < usedParticles; i++) {
    // not checking whether the particle is dead is faster, as most are usually alive
    particles[i].vy = limitSpeed(static_cast<int32_t>(particles[i].vy) - dv);
  }
}

// apply gravity to a single particle using the system settings (use this for sources)
// the function does not increment the gravity counter
void ParticleSystem2D::applyGravity(PSparticle &part) {
  uint32_t counterbkp = gforcecounter;  // backup PS gravity counter
  int32_t dv = calcForce_dv(gforce, gforcecounter);
  gforcecounter = counterbkp;  // save it back
  part.vy = limitSpeed(static_cast<int32_t>(part.vy) - dv);
}

// slow a particle down by friction. the higher the speed, the higher the friction. 255 means an instant stop
// note: a coefficient smaller than 0 speeds them up (a feature, not a bug), larger than 255 inverts the speed
void ParticleSystem2D::applyFriction(PSparticle &part, const int32_t coefficient) {
  int32_t friction = 255 - coefficient;
  part.vx = (static_cast<int32_t>(part.vx) * friction) / 255;
  part.vy = (static_cast<int32_t>(part.vy) * friction) / 255;
}

// apply friction to all particles
void ParticleSystem2D::applyFriction(const int32_t coefficient) {
  int32_t friction = 255 - coefficient;
  for (uint32_t i = 0; i < usedParticles; i++) {
    particles[i].vx = (static_cast<int32_t>(particles[i].vx) * friction) / 255;
    particles[i].vy = (static_cast<int32_t>(particles[i].vy) * friction) / 255;
  }
}

// attract a particle to an attractor particle using the inverse square law
void ParticleSystem2D::pointAttractor(const uint32_t particleindex, PSparticle &attractor, const uint8_t strength,
                                      const bool swallow) {
  if (advPartProps == nullptr)
    return;  // no advanced properties available

  // calculate the distance between the particle and the attractor
  int32_t dx = attractor.x - particles[particleindex].x;
  int32_t dy = attractor.y - particles[particleindex].y;

  // calculate the force based on the inverse square law
  int32_t distanceSquared = dx * dx + dy * dy;
  if (distanceSquared < 8192) {
    if (swallow) {  // particle is close, age it fast so it fades out, do not attract further
      if (particles[particleindex].ttl > 7)
        particles[particleindex].ttl -= 8;
      else {
        particles[particleindex].ttl = 0;
        return;
      }
    }
    distanceSquared = 2 * PS_P_RADIUS * PS_P_RADIUS;  // limit the distance to avoid very high forces
  }

  int32_t force = (static_cast<int32_t>(strength) << 16) / distanceSquared;
  int8_t xforce = (force * dx) / 1024;  // scale to a lower value, found by experimenting
  int8_t yforce = (force * dy) / 1024;
  applyForce(particleindex, xforce, yforce);
}

// render particles to the LED buffer (uses the palette to render the 8 bit particle color value)
// if wrap is set, particles half out of bounds are rendered to the other side of the matrix
// warning: do not render out of bounds particles, rendering does not check whether a particle is out of bounds
void ParticleSystem2D::render() {
  if (framebuffer == nullptr) {
    return;
  }
  CRGBW baseRGB;
  uint32_t brightness;            // particle brightness, fades if dying
  TBlendType blend = LINEARBLEND;  // default color rendering: wrap palette
  if (particlesettings.colorByAge) {
    blend = LINEARBLEND_NOWRAP;
  }

  if (motionBlur) {  // motion blurring active
    for (int32_t y = 0; y <= maxYpixel; y++) {
      int index = y * (maxXpixel + 1);
      for (int32_t x = 0; x <= maxXpixel; x++) {
        framebuffer[index] = fast_color_scale(framebuffer[index], motionBlur);
        index++;
      }
    }
  } else {  // no blurring: clear buffer
    memset(framebuffer, 0, (maxXpixel + 1) * (maxYpixel + 1) * sizeof(uint32_t));
  }

  // go over particles and render them to the buffer
  for (uint32_t i = 0; i < usedParticles; i++) {
    if (particles[i].ttl == 0 || particleFlags[i].outofbounds)
      continue;
    // generate RGB values for the particle
    if (fireIntesity) {  // fire mode
      brightness = static_cast<uint32_t>(particles[i].ttl) * (3 + (fireIntesity >> 5)) + 5;
      brightness = std::min<uint32_t>(brightness, 255);
      baseRGB = ColorFromPalette(seg->palette_ref(), brightness, 255, LINEARBLEND_NOWRAP);
    } else {
      brightness = std::min<int>(particles[i].ttl << 1, 255);
      baseRGB = ColorFromPalette(seg->palette_ref(), particles[i].hue, 255, blend);
      if (particles[i].sat < 255) {
        CHSV32 baseHSV = baseRGB;
        baseHSV.s = std::min<uint8_t>(baseHSV.s, particles[i].sat);  // set the saturation but don't increase it
        hsv2rgb_spectrum(baseHSV, baseRGB);                          // convert back to RGB
      }
    }
    if (gammaCorrectCol)
      brightness = gamma8(brightness);  // gamma correction, used for gamma-inverted brightness distribution
    renderParticle(i, brightness, baseRGB, particlesettings.wrapX, particlesettings.wrapY);
  }

  // apply 2D blur to the rendered frame
  if (smearBlur) {
    seg->blur2d(smearBlur, smearBlur, true);
  }
}

// calculate pixel positions and brightness distribution and render the particle to the buffer
void ParticleSystem2D::renderParticle(const uint32_t particleindex, const uint8_t brightness, const CRGBW &color,
                                      const bool wrapX, const bool wrapY) {
  uint32_t size = particlesize;

  if (perParticleSize && advPartProps != nullptr)  // use advanced size properties
    size = 1 + advPartProps[particleindex].size;   // add 1 to avoid single pixel particles (collisions need this)

  if (size == 0) {  // single pixel rendering
    uint32_t x = particles[particleindex].x >> PS_P_RADIUS_SHIFT;
    uint32_t y = particles[particleindex].y >> PS_P_RADIUS_SHIFT;
    if (x <= static_cast<uint32_t>(maxXpixel) && y <= static_cast<uint32_t>(maxYpixel)) {
      // flip y coordinate (0,0 is bottom left in the PS but top left in the framebuffer)
      uint32_t index = x + (maxYpixel - y) * (maxXpixel + 1);
      framebuffer[index] = fast_color_scaleAdd(framebuffer[index], color, brightness);
    }
    return;
  }

  if (size > 1) {  // size > 1: render as an ellipse
    renderLargeParticle(size, particleindex, brightness, color, wrapX, wrapY);
    return;
  }

  // size = 1: standard 2x2 pixel rendering using bilinear interpolation (20% faster than ellipse rendering)
  uint8_t pxlbrightness[4];  // brightness values for the four pixels representing a particle
  struct {
    int32_t x, y;
  } pixco[4];  // pixel coordinates, the order is bottom left [0], bottom right [1], top right [2], top left [3]
  bool pixelvalid[4] = {true, true, true, true};  // set to false if the pixel is out of bounds

  // add half a radius as the rendering algorithm always starts at the bottom left, this leaves things positive
  int32_t xoffset = particles[particleindex].x + PS_P_HALFRADIUS;
  int32_t yoffset = particles[particleindex].y + PS_P_HALFRADIUS;
  int32_t dx = xoffset & (PS_P_RADIUS - 1);  // relative particle position in subpixel space
  int32_t dy = yoffset & (PS_P_RADIUS - 1);  // modulo replaced with a bitwise AND, the radius is a power of 2
  int32_t x = (xoffset >> PS_P_RADIUS_SHIFT);
  int32_t y = (yoffset >> PS_P_RADIUS_SHIFT);

  // set the four raw pixel coordinates
  pixco[1].x = pixco[2].x = x;  // bottom right & top right
  pixco[2].y = pixco[3].y = y;  // top right & top left
  x--;                          // shift by a full pixel here, this is skipped above to not do -1 and then +1
  y--;
  pixco[0].x = pixco[3].x = x;  // bottom left & top left
  pixco[0].y = pixco[1].y = y;  // bottom left & bottom right

  // calculate brightness values for all four pixels using linear interpolation
  int32_t precal1 = static_cast<int32_t>(PS_P_RADIUS) - dx;
  int32_t precal2 = (static_cast<int32_t>(PS_P_RADIUS) - dy) * brightness;
  int32_t precal3 = dy * brightness;
  pxlbrightness[0] = (precal1 * precal2) >> PS_P_SURFACE;  // bottom left
  pxlbrightness[1] = (dx * precal2) >> PS_P_SURFACE;       // bottom right
  pxlbrightness[2] = (dx * precal3) >> PS_P_SURFACE;       // top right
  pxlbrightness[3] = (precal1 * precal3) >> PS_P_SURFACE;  // top left
  // adjust brightness so the distribution is linear after gamma correction
  if (gammaCorrectCol) {
    for (uint32_t i = 0; i < 4; i++) {
      pxlbrightness[i] = gamma8inv(pxlbrightness[i]);
    }
  }

  // standard rendering (2x2 pixels)
  // check for out of frame pixels and wrap them if required: x,y is the bottom left pixel of the particle
  if (pixco[0].x < 0) {  // left pixels out of frame
    if (wrapX) {         // wrap x to the other side if required
      pixco[0].x = pixco[3].x = maxXpixel;
    } else {
      pixelvalid[0] = pixelvalid[3] = false;  // out of bounds
      if (pixco[0].x < -1)
        return;  // both left pixels out of bounds, no need to continue (safety check)
    }
  } else if (pixco[1].x > static_cast<int32_t>(maxXpixel)) {  // right pixels
    if (wrapX) {
      pixco[1].x = pixco[2].x = 0;
    } else {
      pixelvalid[1] = pixelvalid[2] = false;  // out of bounds
      if (pixco[0].x > static_cast<int32_t>(maxXpixel))
        return;  // both pixels out of bounds, no need to continue (safety check)
    }
  }

  if (pixco[0].y < 0) {  // bottom pixels out of frame
    if (wrapY) {
      pixco[0].y = pixco[1].y = maxYpixel;
    } else {
      pixelvalid[0] = pixelvalid[1] = false;  // out of bounds
      if (pixco[0].y < -1)
        return;  // both bottom pixels out of bounds, no need to continue (safety check)
    }
  } else if (pixco[2].y > maxYpixel) {  // top pixels
    if (wrapY) {
      pixco[2].y = pixco[3].y = 0;
    } else {
      pixelvalid[2] = pixelvalid[3] = false;  // out of bounds
      if (pixco[2].y > static_cast<int32_t>(maxYpixel) + 1)
        return;  // both top pixels out of bounds, no need to continue (safety check)
    }
  }
  for (uint32_t i = 0; i < 4; i++) {
    if (pixelvalid[i]) {
      // flip y coordinate (0,0 is bottom left in the PS but top left in the framebuffer)
      uint32_t idx = pixco[i].x + (maxYpixel - pixco[i].y) * (maxXpixel + 1);
      framebuffer[idx] = fast_color_scaleAdd(framebuffer[idx], color, pxlbrightness[i]);
    }
  }
}

// render a particle as an ellipse/circle with linear brightness falloff and sub-pixel precision
void ParticleSystem2D::renderLargeParticle(const uint32_t size, const uint32_t particleindex,
                                           const uint8_t brightness, const CRGBW &color, const bool wrapX,
                                           const bool wrapY) {
  // particle position with sub-pixel precision
  int32_t x_subcenter = particles[particleindex].x;
  int32_t y_subcenter = particles[particleindex].y;

  // for x = 128 a particle is exactly between pixel 1 and 2. with a radius of 2 pixels we draw pixels 0-3.
  // when calculating dx we need to account for that, so half a radius is added:
  // dx = pixel_x * PS_P_RADIUS - x_subcenter + PS_P_HALFRADIUS

  // integer pixel position, rounded down
  int32_t x_center = (x_subcenter) >> PS_P_RADIUS_SHIFT;
  int32_t y_center = (y_subcenter) >> PS_P_RADIUS_SHIFT;

  // ellipse radii in pixels
  uint32_t xsize = size;
  uint32_t ysize = size;
  if (advPartSize != nullptr && advPartSize[particleindex].asymmetry > 0) {
    getParticleXYsize(&advPartProps[particleindex], &advPartSize[particleindex], xsize, ysize);
  }

  int32_t rx_subpixel = xsize + PS_P_RADIUS + 1;  // size = 1 means a radius of just over 1 pixel
  int32_t ry_subpixel = ysize + PS_P_RADIUS + 1;  // size = 255 is a radius of 5: 65+255=320, 320>>6=5 pixels

  // rendering bounding box in pixels
  int32_t rx_pixels = (rx_subpixel >> PS_P_RADIUS_SHIFT);
  int32_t ry_pixels = (ry_subpixel >> PS_P_RADIUS_SHIFT);

  int32_t x_min = x_center - rx_pixels;  // the "+1" extension needed for 1D is not required for 2D
  int32_t x_max = x_center + rx_pixels;
  int32_t y_min = y_center - ry_pixels;
  int32_t y_max = y_center + ry_pixels;

  // cache for speed
  uint32_t matrixX = maxXpixel + 1;
  uint32_t matrixY = maxYpixel + 1;
  uint32_t rx_sq = rx_subpixel * rx_subpixel;
  uint32_t ry_sq = ry_subpixel * ry_subpixel;

  // iterate over the bounding box and render each pixel
  for (int32_t py = y_min; py <= y_max; py++) {
    for (int32_t px = x_min; px <= x_max; px++) {
      // check bounds and apply wrapping
      int32_t render_x = px;
      int32_t render_y = py;
      if (render_x < 0) {
        if (!wrapX)
          continue;
        render_x += matrixX;
      } else if (render_x > maxXpixel) {
        if (!wrapX)
          continue;
        render_x -= matrixX;
      }

      if (render_y < 0) {
        if (!wrapY)
          continue;
        render_y += matrixY;
      } else if (render_y > maxYpixel) {
        if (!wrapY)
          continue;
        render_y -= matrixY;
      }

      /* distance from the particle center, explanation see above
       * Upstream shifts px and py left here. Both are signed and go negative for
       * a particle left of or below the origin, and shifting a negative value
       * left is undefined before C++20, which a sanitizer build reports. The
       * multiply is the same arithmetic and the same instruction. */
      int32_t dx_subpixel = (px * (1 << PS_P_RADIUS_SHIFT)) - x_subcenter + PS_P_HALFRADIUS;
      int32_t dy_subpixel = (py * (1 << PS_P_RADIUS_SHIFT)) - y_subcenter + PS_P_HALFRADIUS;

      // calculate brightness from the squared distance to the ellipse center
      uint8_t pixel_brightness = calculateEllipseBrightness(dx_subpixel, dy_subpixel, rx_sq, ry_sq, brightness);

      if (pixel_brightness == 0)
        continue;  // skip black pixels

      // apply inverse gamma correction if needed, without it particles flicker as total brightness changes
      if (gammaCorrectCol) {
        pixel_brightness = gamma8inv(pixel_brightness);
      }
      // render pixel, flip y coordinate (0,0 is bottom left in the PS but top left in the framebuffer)
      uint32_t idx = render_x + (maxYpixel - render_y) * matrixX;
      framebuffer[idx] = fast_color_scaleAdd(framebuffer[idx], color, pixel_brightness);
    }
  }
}

// detect collisions in an array of particles and handle them
// uses binning by dividing the frame into slices in x direction, which is efficient when gravity acts in y
void ParticleSystem2D::handleCollisions() {
  uint32_t collDistSq = particleHardRadius << 1;  // distance is double the radius
  collDistSq = collDistSq * collDistSq;           // square it for a faster comparison
  // particles are binned on the x axis, the assumption is that no more than half of them are in the same bin.
  // if they are, collisionStartIdx is increased so each particle collides at least every second frame.
  int binWidth = 6 * PS_P_RADIUS;                  // width of a bin in sub-pixels
  int32_t overlap = particleHardRadius << 1;       // overlap bins to include edge particles in neighbouring bins
  if (perParticleSize && advPartProps != nullptr)
    overlap = 512;  // max overlap for per-particle size, enough to catch all particles even at max speed

  uint32_t maxBinParticles = std::max<uint32_t>(50, (usedParticles + 1) / 2);
  if (maxBinParticles > binArrayEntries)
    maxBinParticles = binArrayEntries;  // the bin array is sized for numParticles, this is a safety net
  uint32_t numBins = (maxX + (binWidth - 1)) / binWidth;  // number of bins in x direction
  if (usedParticles < maxBinParticles) {
    numBins = 1;  // use a single bin for a small number of particles
    binWidth = maxX + 1;
  }
  uint32_t binParticleCount;                                // number of particles in the current bin
  uint32_t nextFrameStartIdx = hw_random16(usedParticles);  // first particle in the next frame
  uint32_t pidx = collisionStartIdx;  // start index in case a bin is full, process the rest next frame

  for (uint32_t bin = 0; bin < numBins; bin++) {
    binParticleCount = 0;                         // reset for this bin
    int32_t binStart = bin * binWidth - overlap;  // the first bin extends to negative, out of bounds are ignored
    int32_t binEnd = binStart + binWidth + (overlap << 1);  // twice the overlap as the start is start-overlap

    // fill the binIndices array for this bin
    for (uint32_t i = 0; i < usedParticles; i++) {
      if (particles[pidx].ttl > 0) {  // is alive
        if (particles[pidx].x >= binStart && particles[pidx].x <= binEnd) {  // include particles on the bin edge
          if (particleFlags[pidx].outofbounds == 0 && particleFlags[pidx].collide) {  // in frame and colliding
            if (binParticleCount >= maxBinParticles) {  // bin is full, do the rest next frame
              nextFrameStartIdx = pidx;
              break;
            }
            binIndices[binParticleCount++] = pidx;
          }
        }
      }
      pidx++;
      if (pidx >= usedParticles)
        pidx = 0;  // wrap around
    }

    int32_t massratio1 = 0;  // 0 means don't use a mass ratio (equal mass)
    int32_t massratio2 = 0;
    for (uint32_t i = 0; i < binParticleCount; i++) {
      uint32_t idx_i = binIndices[i];
      for (uint32_t j = i + 1; j < binParticleCount; j++) {  // check against higher numbered particles
        uint32_t idx_j = binIndices[j];
        if (perParticleSize && advPartProps != nullptr) {  // using individual particle size
          // collision distance, use 80% of size for tighter stacking (slight overlap)
          collDistSq = (PS_P_MINHARDRADIUS << 1) + (((static_cast<uint32_t>(advPartProps[idx_i].size) +
                                                      static_cast<uint32_t>(advPartProps[idx_j].size)) *
                                                     52) >>
                                                    6);
          collDistSq = collDistSq * collDistSq;  // square it for a faster comparison
          // calculate the mass ratio for the collision response
          uint32_t mass1 = PS_P_RADIUS + advPartProps[idx_i].size;
          uint32_t mass2 = PS_P_RADIUS + advPartProps[idx_j].size;
          mass1 = mass1 * mass1;  // mass proportional to area
          mass2 = mass2 * mass2;
          uint32_t totalmass = mass1 + mass2;
          massratio1 = (mass2 << 8) / totalmass;  // if 2 is heavier it has a higher velocity impact on 1
          massratio2 = (mass1 << 8) / totalmass;
        }
        // distance with lookahead
        int32_t dx = (particles[idx_j].x + particles[idx_j].vx) - (particles[idx_i].x + particles[idx_i].vx);
        if (dx * dx < static_cast<int32_t>(collDistSq)) {  // check x direction, if close check y as well
          int32_t dy = (particles[idx_j].y + particles[idx_j].vy) - (particles[idx_i].y + particles[idx_i].vy);
          if (dy * dy < static_cast<int32_t>(collDistSq))  // particles are close
            collideParticles(particles[idx_i], particles[idx_j], dx, dy, collDistSq, massratio1, massratio2);
        }
      }
    }
  }
  collisionStartIdx = nextFrameStartIdx;  // set the start index for the next frame
}

// handle a collision if close proximity is detected, i.e. dx and/or dy smaller than 2*PS_P_RADIUS
void ParticleSystem2D::collideParticles(PSparticle &particle1, PSparticle &particle2, int32_t dx, int32_t dy,
                                        const uint32_t collDistSq, int32_t massratio1, int32_t massratio2) {
  int32_t distanceSquared = dx * dx + dy * dy;
  // calculate relative velocity
  int32_t relativeVx = static_cast<int32_t>(particle2.vx) - static_cast<int32_t>(particle1.vx);
  int32_t relativeVy = static_cast<int32_t>(particle2.vy) - static_cast<int32_t>(particle1.vy);

  // if dx and dy are zero (same position) give them an offset, if the speeds are also zero, offset them too
  if (distanceSquared == 0) {
    // adjust positions based on the relative velocity direction
    dx = -1;
    if (relativeVx < 0)  // if true, particle2 is on the right side
      dx = 1;
    else if (relativeVx == 0)
      relativeVx = 1;

    dy = -1;
    if (relativeVy < 0)
      dy = 1;
    else if (relativeVy == 0)
      relativeVy = 1;

    distanceSquared = 2;  // 1 + 1
  }

  // dot product of the relative velocity and the relative distance, always negative if moving towards each other
  int32_t dotProduct = (dx * relativeVx + dy * relativeVy);

  if (dotProduct < 0) {  // particles are moving towards each other
    // integer math is much faster than floats (float divisions are slow on all ESPs)
    // overflow check: dx/dy are 7 bit, relativV are 8 bit -> dotproduct is 15 bit, a 15 bit shift is safe
    // note: cannot use right shifts, shifting right is asymmetrical (1>>1=0 / -1>>1=-1) and this must be accurate
    // if particles are soft, the impulse must stay above a limit or collisions slip through at higher speeds
    int32_t surfacehardness = std::max<int32_t>(collisionHardness, PS_P_MINSURFACEHARDNESS >> 1);
    int32_t impulse = (((((-dotProduct) << 15) / distanceSquared) * surfacehardness) >> 8);

    int32_t ximpulse = (impulse * dx) / 32767;
    int32_t yimpulse = (impulse * dy) / 32767;
    // if the particles are not the same size, use a mass ratio. it is 0 if they are the same size
    if (massratio1) {
      int32_t vx1 = static_cast<int32_t>(particle1.vx) - ((ximpulse * massratio1) >> 7);
      int32_t vy1 = static_cast<int32_t>(particle1.vy) - ((yimpulse * massratio1) >> 7);
      int32_t vx2 = static_cast<int32_t>(particle2.vx) + ((ximpulse * massratio2) >> 7);
      int32_t vy2 = static_cast<int32_t>(particle2.vy) + ((yimpulse * massratio2) >> 7);
      // limit speeds, required when a lot of impulse is transferred from a large to a small particle
      particle1.vx = limitSpeed(vx1);
      particle1.vy = limitSpeed(vy1);
      particle2.vx = limitSpeed(vx2);
      particle2.vy = limitSpeed(vy2);
    } else {
      particle1.vx -= ximpulse;  // note: impulse is inverted, so subtracting it
      particle1.vy -= yimpulse;
      particle2.vx += ximpulse;
      particle2.vy += yimpulse;
    }
    // if particles are soft they become 'sticky', i.e. apply some friction (they pile more nicely)
    if (collisionHardness < PS_P_MINSURFACEHARDNESS && (seg->call & 0x07) == 0) {
      const uint32_t coeff = collisionHardness + (255 - PS_P_MINSURFACEHARDNESS);
      particle1.vx = (static_cast<int32_t>(particle1.vx) * coeff) / 255;
      particle1.vy = (static_cast<int32_t>(particle1.vy) * coeff) / 255;
      particle2.vx = (static_cast<int32_t>(particle2.vx) * coeff) / 255;
      particle2.vy = (static_cast<int32_t>(particle2.vy) * coeff) / 255;
    }
  }
  // particles have volume, push them apart if they are too close.
  // what works best is to give one particle a little velocity. hard pushing tends to oscillate.
  // oscillation gets worse if both are pushed, so one is chosen somewhat randomly.
  // softer collisions are not perfect on purpose: soft particles should pile up and overlap slightly.

  if (distanceSquared < static_cast<int32_t>(collDistSq) &&
      (relativeVx * relativeVx + relativeVy * relativeVy < 50)) {  // too close and slow, push them apart
    bool fairlyrandom = dotProduct & 0x01;  // the dotproduct LSB is somewhat random, no need for a random number
    // found by experimentation: push by 1, push more when overlapping by more than 1.4 physical pixels
    int32_t pushamount = 1 + ((collDistSq - distanceSquared) >> 13);
    int8_t pushx = dx > 0 ? -pushamount : pushamount;  // particle 1 is on the left
    int8_t pushy = dy > 0 ? -pushamount : pushamount;  // particle 1 is below particle 2

    // if they are very soft, stop slow particles completely to make them stick to each other
    if (collisionHardness < 5) {
      if (fairlyrandom) {  // do not stop them every frame to avoid groups of particles hanging mid-air
        particle1.vx = 0;
        particle1.vy = 0;
        particle2.vx = 0;
        particle2.vy = 0;
        // hard-push particle 1 only: if both are pushed, this oscillates ever so slightly
        particle1.x += pushx;
        particle1.y += pushy;
      }
    } else {
      if (fairlyrandom) {
        particle1.vx += pushx;
        particle1.vy += pushy;
      } else {
        particle2.vx -= pushx;
        particle2.vy -= pushy;
      }
    }
  }
}

// update size and pointers (the memory location and size can change dynamically)
// note: do not access the PS class in an FX before running this function
void ParticleSystem2D::updateSystem() {
  setMatrixSize(seg->width(), seg->height());
  updatePSpointers(advPartProps != nullptr, advPartSize != nullptr);
}

// set the pointers for the class
// Note on memory alignment: a pointer MUST be 4 byte aligned. by making sure that the number of sources and
// particles is a multiple of 4, padding can be skipped here, independent of the struct sizes.
void ParticleSystem2D::updatePSpointers(bool isadvanced, bool sizecontrol) {
  particles = reinterpret_cast<PSparticle *>(this + 1);                            // pointer to particles
  particleFlags = reinterpret_cast<PSparticleFlags *>(particles + numParticles);    // pointer to particle flags
  sources = reinterpret_cast<PSsource *>(particleFlags + numParticles);             // pointer to source(s)
  binIndices = reinterpret_cast<uint16_t *>(sources + numSources);  // collision binning scratch, see the header
  framebuffer = seg->canvas()->pixels();                            // pointer to framebuffer
  // first available byte after the PS for FX additional data (already aligned to a 4 byte boundary)
  PSdataEnd = reinterpret_cast<uint8_t *>(binIndices + binArrayEntries);
  if (isadvanced) {
    advPartProps = reinterpret_cast<PSadvancedParticle *>(PSdataEnd);
    PSdataEnd = reinterpret_cast<uint8_t *>(advPartProps + numParticles);
    if (sizecontrol) {
      advPartSize = reinterpret_cast<PSsizeControl *>(PSdataEnd);
      PSdataEnd = reinterpret_cast<uint8_t *>(advPartSize + numParticles);
    }
  }
}

// non class functions to use for initialization
uint32_t calculateNumberOfParticles2D(uint32_t const pixels, const bool isadvanced, const bool sizecontrol) {
  uint32_t numberofParticles = pixels;         // 1 particle per pixel (for example 512 particles on 32x16)
  uint32_t particlelimit = MAXPARTICLES_2D;    // maximum number of particles allowed
  numberofParticles = std::max<uint32_t>(4, std::min<uint32_t>(numberofParticles, particlelimit));
  if (isadvanced)  // the advanced property array needs RAM, reduce the particle count to use the same amount
    numberofParticles = (numberofParticles * sizeof(PSparticle)) / (sizeof(PSparticle) + sizeof(PSadvancedParticle));
  if (sizecontrol)  // advanced size control needs much fewer particles
    numberofParticles /= 8;

  // make sure it is a multiple of 4 for proper memory alignment
  numberofParticles = (numberofParticles + 3) & ~0x03;
  return numberofParticles;
}

uint32_t calculateNumberOfSources2D(uint32_t pixels, uint32_t requestedsources) {
  uint32_t numberofSources = std::min<uint32_t>(pixels / SOURCEREDUCTIONFACTOR, requestedsources);
  numberofSources = std::max<uint32_t>(1, std::min<uint32_t>(numberofSources, MAXSOURCES_2D));  // limit
  // make sure it is a multiple of 4 for proper memory alignment
  numberofSources = (numberofSources + 3) & ~0x03;
  return numberofSources;
}

// Number of entries in the collision bin index array. Upstream sizes this on the
// stack from usedParticles; here it lives in the PS memory block, so it is sized
// from the allocated particle count, which is always at least as large.
uint32_t calculateBinArrayEntries2D(const uint32_t numparticles) {
  uint32_t entries = std::max<uint32_t>(50, (numparticles + 1) / 2);
  return (entries + 1) & ~0x01u;  // keep the byte size a multiple of 4
}

// allocate memory for the particle system class, particles, sprays plus additional memory requested by the FX
bool allocateParticleSystemMemory2D(Segment &seg, uint32_t numparticles, uint32_t numsources,
                                    uint32_t binarrayentries, bool isadvanced, bool sizecontrol,
                                    uint32_t additionalbytes) {
  uint32_t requiredmemory = sizeof(ParticleSystem2D);
  // the functions above make sure numparticles is a multiple of 4 (to avoid alignment issues)
  requiredmemory += sizeof(PSparticleFlags) * numparticles;
  requiredmemory += sizeof(PSparticle) * numparticles;
  if (isadvanced)
    requiredmemory += sizeof(PSadvancedParticle) * numparticles;
  if (sizecontrol)
    requiredmemory += sizeof(PSsizeControl) * numparticles;
  requiredmemory += sizeof(PSsource) * numsources;
  requiredmemory += sizeof(uint16_t) * binarrayentries;
  requiredmemory += additionalbytes;
  return seg.allocate_data(requiredmemory);
}

// initialize the particle system, allocate additional bytes if needed (read the pointer from PSdataEnd)
bool initParticleSystem2D(Segment &seg, ParticleSystem2D *&PartSys, uint32_t requestedsources,
                          uint32_t additionalbytes, bool advanced, bool sizecontrol) {
  if (!seg.is_2d()) {
    seg.deallocate_data();  // make sure data is null, there is no valid PS in it
    return false;           // only for 2D
  }
  uint32_t cols = seg.width();
  uint32_t rows = seg.height();
  uint32_t pixels = cols * rows;
  if (sizecontrol)
    advanced = true;  // size control needs advanced properties, prevent wrong usage

  uint32_t numparticles = calculateNumberOfParticles2D(pixels, advanced, sizecontrol);
  uint32_t numsources = calculateNumberOfSources2D(pixels, requestedsources);
  bool allocsuccess = false;
  uint32_t binarrayentries = 0;
  while (numparticles >= 5) {  // make sure we have at least 5 particles or quit
    binarrayentries = calculateBinArrayEntries2D(numparticles);
    if (allocateParticleSystemMemory2D(seg, numparticles, numsources, binarrayentries, advanced, sizecontrol,
                                       additionalbytes)) {
      allocsuccess = true;
      break;  // allocation succeeded
    }
    numparticles = ((numparticles / 2) + 3) & ~0x03;  // halve the particle count and try again, 4 byte aligned
  }
  if (!allocsuccess) {
    return false;  // allocation failed
  }

  PartSys = new (seg.data)
      ParticleSystem2D(seg, cols, rows, numparticles, numsources, binarrayentries, advanced, sizecontrol);

  return true;
}

////////////////////////
// 1D Particle System //
////////////////////////

ParticleSystem1D::ParticleSystem1D(Segment &segment, uint32_t length, uint32_t numberofparticles,
                                   uint32_t numberofsources, uint32_t binarrayentries, bool isadvanced) {
  seg = &segment;
  numSources = numberofsources;
  numParticles = numberofparticles;  // number of particles allocated in init
  usedParticles = numParticles;      // use all particles by default
  binArrayEntries = binarrayentries;
  advPartProps = nullptr;  // make sure we start out with null pointers
  setSize(length);
  updatePSpointers(isadvanced);  // set the particle and source pointers
  setWallHardness(255);          // set default wall hardness to max
  setGravity(0);                 // gravity disabled by default
  setParticleSize(0);            // 1 pixel size by default
  motionBlur = 0;                // no fading by default
  smearBlur = 0;                 // no smearing by default
  emitIndex = 0;
  collisionStartIdx = 0;
  forcecounter = 0;
  gforcecounter = 0;
  // initialize some default non-zero values most FX use
  for (uint32_t i = 0; i < numSources; i++) {
    sources[i].source.ttl = 1;          // set source alive
    sources[i].sourceFlags.asByte = 0;  // all flags disabled
  }
  perParticleSize = isadvanced;  // enable per particle size by default if using advanced properties
  if (isadvanced) {
    for (uint32_t i = 0; i < numParticles; i++) {
      advPartProps[i].sat = 255;  // set full saturation
    }
  }
}

// update function applies gravity, moves the particles, handles collisions and renders the particles
void ParticleSystem1D::update() {
  // apply gravity globally if enabled
  if (particlesettings.useGravity)
    applyGravity();

  // handle collisions (can push particles, must be done before updating particles)
  if (particlesettings.useCollisions) {
    handleCollisions();
    if (perParticleSize)
      handleCollisions();  // second pass for per particle size, improves "slip through" for small particles
  }

  // move all particles
  for (uint32_t i = 0; i < usedParticles; i++) {
    particleMoveUpdate(particles[i], particleFlags[i], nullptr, advPartProps ? &advPartProps[i] : nullptr);
  }

  if (particlesettings.colorByPosition) {
    uint32_t scale = (255 << 16) / maxX;
    for (uint32_t i = 0; i < usedParticles; i++) {
      particles[i].hue = (scale * particles[i].x) >> 16;  // note: x is > 0 if not out of bounds
    }
  }

  render();
}

// set percentage of used particles as uint8_t i.e 127 means 50% for example
void ParticleSystem1D::setUsedParticles(const uint8_t percentage) {
  usedParticles = std::max<uint32_t>(1, (numParticles * (static_cast<int>(percentage) + 1)) >> 8);
}

void ParticleSystem1D::setWallHardness(const uint8_t hardness) { wallHardness = hardness; }

void ParticleSystem1D::setSize(const uint32_t x) {
  maxXpixel = x - 1;                 // last physical pixel that can be drawn to
  maxX = x * PS_P_RADIUS_1D - 1;     // particle system boundary for movements
}

void ParticleSystem1D::setWrap(const bool enable) { particlesettings.wrap = enable; }

void ParticleSystem1D::setBounce(const bool enable) { particlesettings.bounce = enable; }

void ParticleSystem1D::setKillOutOfBounds(const bool enable) { particlesettings.killoutofbounds = enable; }

void ParticleSystem1D::setColorByAge(const bool enable) { particlesettings.colorByAge = enable; }

void ParticleSystem1D::setColorByPosition(const bool enable) { particlesettings.colorByPosition = enable; }

void ParticleSystem1D::setMotionBlur(const uint8_t bluramount) { motionBlur = bluramount; }

void ParticleSystem1D::setSmearBlur(const uint8_t bluramount) { smearBlur = bluramount; }

// render size, 0 = 1 pixel, 1 = 2 pixel (interpolated), 255 = 18 pixel diameter
void ParticleSystem1D::setParticleSize(const uint8_t size) {
  particlesize = size;
  particleHardRadius = PS_P_MINHARDRADIUS_1D;  // ~1 pixel
  perParticleSize = false;                     // disable per particle size control if a global size is set
  if (particlesize > 1) {
    particleHardRadius = PS_P_MINHARDRADIUS_1D + ((particlesize * 52) >> 6);  // 1 pixel + 80% of size
  } else if (particlesize == 0) {
    particleHardRadius = PS_P_MINHARDRADIUS_1D >> 1;  // single pixel particles have half the radius
  }
}

// enable/disable gravity, optionally set the force (force=8 is default) can be -127 to +127, 0 is disable
void ParticleSystem1D::setGravity(const int8_t force) {
  if (force) {
    gforce = force;
    particlesettings.useGravity = true;
  } else {
    particlesettings.useGravity = false;
  }
}

void ParticleSystem1D::enableParticleCollisions(const bool enable, const uint8_t hardness) {
  particlesettings.useCollisions = enable;
  collisionHardness = hardness;
}

// emit one particle with variation, returns the index of the last emitted particle (or -1 if none was emitted)
int32_t ParticleSystem1D::sprayEmit(const PSsource1D &emitter) {
  for (uint32_t i = 0; i < usedParticles; i++) {
    emitIndex++;
    if (emitIndex >= usedParticles)
      emitIndex = 0;
    if (particles[emitIndex].ttl == 0) {  // find a dead particle
      particles[emitIndex].vx = emitter.v + hw_random16(emitter.var << 1) - emitter.var;  // random(-var,var)
      particles[emitIndex].x = emitter.source.x;
      particles[emitIndex].hue = emitter.source.hue;
      particles[emitIndex].ttl = hw_random16(emitter.minLife, emitter.maxLife);
      particleFlags[emitIndex].collide = emitter.sourceFlags.collide;
      particleFlags[emitIndex].reversegrav = emitter.sourceFlags.reversegrav;
      particleFlags[emitIndex].perpetual = emitter.sourceFlags.perpetual;
      if (advPartProps) {
        advPartProps[emitIndex].sat = emitter.sat;
        advPartProps[emitIndex].size = emitter.size;
      }
      return emitIndex;
    }
  }
  return -1;
}

// particle moves, decays and dies, if killoutofbounds is set, out of bounds particles are set to ttl=0
void ParticleSystem1D::particleMoveUpdate(PSparticle1D &part, PSparticleFlags1D &partFlags, PSsettings1D *options,
                                          PSadvancedParticle1D *advancedproperties) {
  if (options == nullptr)
    options = &particlesettings;  // use PS system settings by default

  if (part.ttl > 0) {
    if (!partFlags.perpetual)
      part.ttl--;  // age
    if (options->colorByAge)
      part.hue = std::min<uint16_t>(part.ttl, 255);  // set color to ttl

    int32_t renderradius = PS_P_HALFRADIUS_1D - 1 + particlesize;  // default for 2 pixel rendering
    int32_t newX = part.x + static_cast<int32_t>(part.vx);
    partFlags.outofbounds = false;  // reset out of bounds

    if (perParticleSize && advancedproperties != nullptr) {  // using individual particle size?
      renderradius = PS_P_HALFRADIUS_1D - 1 + advancedproperties->size;
      if (advancedproperties->size > 1)
        particleHardRadius = PS_P_MINHARDRADIUS_1D + ((advancedproperties->size * 52) >> 6);
      else  // single pixel particles use half the collision distance for walls
        particleHardRadius = PS_P_MINHARDRADIUS_1D >> 1;
    }

    // if wall collisions are enabled, bounce them before they reach the edge, it looks much nicer
    if (options->bounce) {
      if ((newX < static_cast<int32_t>(particleHardRadius)) ||
          ((newX > static_cast<int32_t>(maxX - particleHardRadius)))) {  // reached a wall
        bool bouncethis = true;
        if (options->useGravity) {
          if (partFlags.reversegrav) {  // skip bouncing at x = 0
            if (newX < static_cast<int32_t>(particleHardRadius))
              bouncethis = false;
          } else if (newX > static_cast<int32_t>(particleHardRadius)) {  // skip bouncing at x = max
            bouncethis = false;
          }
        }
        if (bouncethis) {
          part.vx = -part.vx;  // invert speed
          part.vx = (static_cast<int32_t>(part.vx) * static_cast<int32_t>(wallHardness)) / 255;
          if (newX < static_cast<int32_t>(particleHardRadius))
            newX = particleHardRadius;  // fast particles never reach the edge if the position is inverted
          else
            newX = maxX - particleHardRadius;
        }
      }
    }

    if (!checkBoundsAndWrap(newX, maxX, renderradius, options->wrap)) {  // must not be skipped, it can crash
      partFlags.outofbounds = true;
      if (options->killoutofbounds) {
        bool killthis = true;
        if (options->useGravity) {         // if gravity is used, only kill below 'floor level'
          if (partFlags.reversegrav) {     // skip at x = 0, do not skip far out of bounds
            if (newX < 0 || newX > maxX << 2)
              killthis = false;
          } else {  // skip at x = max, do not skip far out of bounds
            if (newX > 0 && newX < maxX << 2)
              killthis = false;
          }
        }
        if (killthis)
          part.ttl = 0;
      }
    }

    if (!partFlags.fixed)
      part.x = newX;  // set new position
    else
      part.vx = 0;  // set speed to zero, a fixed particle should not speed away after a collision
  }
}

// apply a force in x direction to an individual particle (or source)
void ParticleSystem1D::applyForce(PSparticle1D &part, const int8_t xforce, uint8_t &counter) {
  int32_t dv = calcForce_dv(xforce, counter);            // velocity increase
  part.vx = limitSpeed(static_cast<int32_t>(part.vx) + dv);  // apply the force to the particle
}

// apply a force to all particles
void ParticleSystem1D::applyForce(const int8_t xforce) {
  int32_t dv = calcForce_dv(xforce, forcecounter);  // velocity increase
  for (uint32_t i = 0; i < usedParticles; i++) {
    particles[i].vx = limitSpeed(static_cast<int32_t>(particles[i].vx) + dv);
  }
}

// apply gravity to all particles using the PS global gforce setting
void ParticleSystem1D::applyGravity() {
  int32_t dv_raw = calcForce_dv(gforce, gforcecounter);
  for (uint32_t i = 0; i < usedParticles; i++) {
    int32_t dv = dv_raw;
    if (particleFlags[i].reversegrav)
      dv = -dv_raw;
    particles[i].vx = limitSpeed(static_cast<int32_t>(particles[i].vx) - dv);
  }
}

// apply gravity to a single particle using the system settings (use this for sources)
void ParticleSystem1D::applyGravity(PSparticle1D &part, PSparticleFlags1D &partFlags) {
  uint32_t counterbkp = gforcecounter;
  int32_t dv = calcForce_dv(gforce, gforcecounter);
  if (partFlags.reversegrav)
    dv = -dv;
  gforcecounter = counterbkp;  // save it back
  part.vx = limitSpeed(static_cast<int32_t>(part.vx) - dv);
}

// slow particles down by friction, the higher the speed the higher the friction. 255 means an instant stop
void ParticleSystem1D::applyFriction(int32_t coefficient) {
  int32_t friction = 255 - coefficient;
  for (uint32_t i = 0; i < usedParticles; i++) {
    if (particles[i].ttl)
      particles[i].vx = (static_cast<int32_t>(particles[i].vx) * friction) / 255;
  }
}

// render particles to the LED buffer (uses the palette to render the 8 bit particle color value)
void ParticleSystem1D::render() {
  if (framebuffer == nullptr) {
    return;
  }
  CRGBW baseRGB;
  uint32_t brightness;             // particle brightness, fades if dying
  TBlendType blend = LINEARBLEND;  // default color rendering: wrap palette
  if (particlesettings.colorByAge || particlesettings.colorByPosition) {
    blend = LINEARBLEND_NOWRAP;
  }

  if (motionBlur) {  // blurring active
    for (int32_t x = 0; x <= maxXpixel; x++) {
      framebuffer[x] = fast_color_scale(framebuffer[x], motionBlur);
    }
  } else {  // no blurring: clear buffer
    memset(framebuffer, 0, (maxXpixel + 1) * sizeof(uint32_t));
  }

  // go over particles and render them to the buffer
  for (uint32_t i = 0; i < usedParticles; i++) {
    if (particles[i].ttl == 0 || particleFlags[i].outofbounds)
      continue;

    // generate RGB values for the particle
    brightness = std::min<int>(particles[i].ttl << 1, 255);
    baseRGB = ColorFromPalette(seg->palette_ref(), particles[i].hue, 255, blend);
    if (advPartProps != nullptr) {  // saturation is an advanced property in the 1D system
      if (advPartProps[i].sat < 255) {
        CHSV32 baseHSV = baseRGB;
        baseHSV.s = advPartProps[i].sat;     // set the saturation
        hsv2rgb_spectrum(baseHSV, baseRGB);  // convert back to RGB
      }
    }
    if (gammaCorrectCol)
      brightness = gamma8(brightness);
    renderParticle(i, brightness, baseRGB, particlesettings.wrap);
  }
  // apply smear-blur to the rendered frame
  if (smearBlur) {
    seg->blur(smearBlur, true);
  }

  // add background color
  CRGBW bg_color = seg->color(1);
  if (bg_color > 0) {  // if not black
    for (int32_t i = 0; i <= maxXpixel; i++) {
      framebuffer[i] = fast_color_scaleAdd(framebuffer[i], bg_color);
    }
  }
  // transfer the local buffer to the segment if using a 1D-to-2D mapping
  if (localbuffer) {
    // `int`, not int32_t: on Xtensa int32_t is `long`, which makes the call ambiguous
    // between the int and the unsigned overload of set_pixel_color().
    for (int x = 0; x <= maxXpixel; x++) {
      seg->set_pixel_color(x, framebuffer[x]);  // this applies the mapping
    }
  }
}

// calculate pixel positions and brightness distribution and render the particle to the buffer
void ParticleSystem1D::renderParticle(const uint32_t particleindex, const uint8_t brightness, const CRGBW &color,
                                      const bool wrap) {
  uint32_t size = particlesize;
  if (perParticleSize && advPartProps != nullptr)  // use advanced size properties
    size = 1 + advPartProps[particleindex].size;   // add 1 to avoid single pixel particles

  if (size == 0) {  // single pixel particle, out of bounds checking is made for 2-pixel particles
    uint32_t x = particles[particleindex].x >> PS_P_RADIUS_SHIFT_1D;
    if (x <= static_cast<uint32_t>(maxXpixel)) {  // x is unsigned, so no need to check < 0
      framebuffer[x] = fast_color_scaleAdd(framebuffer[x], color, brightness);
    }
    return;
  }
  // render larger particles
  if (size > 1) {  // size > 1: render as a gradient line
    renderLargeParticle(size, particleindex, brightness, color, wrap);
    return;
  }

  // standard rendering (2 pixels per particle)
  bool pxlisinframe[2] = {true, true};
  int32_t pxlbrightness[2];
  int32_t pixco[2];  // physical pixel coordinates of the two pixels representing a particle

  // add half a radius as the rendering algorithm always starts at the bottom left, this leaves things positive
  int32_t xoffset = particles[particleindex].x + PS_P_HALFRADIUS_1D;
  int32_t dx = xoffset & (PS_P_RADIUS_1D - 1);  // relative particle position in subpixel space
  int32_t x = xoffset >> PS_P_RADIUS_SHIFT_1D;  // a bitshift of a negative number stays negative

  // set the raw pixel coordinates
  pixco[1] = x;  // right pixel
  x--;           // shift by a full pixel here, this is skipped above to not do -1 and then +1
  pixco[0] = x;  // left pixel

  // calculate the brightness values for both pixels using linear interpolation
  pxlbrightness[0] = ((static_cast<int32_t>(PS_P_RADIUS_1D) - dx) * brightness) >> PS_P_SURFACE_1D;
  pxlbrightness[1] = (dx * brightness) >> PS_P_SURFACE_1D;
  // adjust brightness so the distribution is linear after gamma correction
  if (gammaCorrectCol) {
    pxlbrightness[0] = gamma8inv(pxlbrightness[0]);
    pxlbrightness[1] = gamma8inv(pxlbrightness[1]);
  }

  // check if any pixels are out of frame
  if (pixco[0] < 0) {  // left pixel out of frame
    if (wrap)          // wrap x to the other side if required
      pixco[0] = maxXpixel;
    else {
      pxlisinframe[0] = false;  // pixel is out of the matrix boundaries, do not render
      if (pixco[0] < -1)
        return;  // both pixels out of frame (safety check)
    }
  } else if (pixco[1] > static_cast<int32_t>(maxXpixel)) {  // right pixel
    if (wrap)
      pixco[1] = 0;
    else {
      pxlisinframe[1] = false;
      if (pixco[0] > static_cast<int32_t>(maxXpixel))
        return;  // both pixels out of frame (safety check)
    }
  }
  for (uint32_t i = 0; i < 2; i++) {
    if (pxlisinframe[i]) {
      framebuffer[pixco[i]] = fast_color_scaleAdd(framebuffer[pixco[i]], color, pxlbrightness[i]);
    }
  }
}

// render a particle as a line with linear brightness falloff and sub-pixel precision, size is 0-255
void ParticleSystem1D::renderLargeParticle(const uint32_t size, const uint32_t particleindex,
                                           const uint8_t brightness, const CRGBW &color, const bool wrap) {
  int32_t x_subcenter = particles[particleindex].x;  // particle position in sub-pixel space

  int32_t x_center = x_subcenter >> PS_P_RADIUS_SHIFT_1D;  // integer pixel position, rounded down

  // particle radius in pixels, size = 1 means a radius of just over 1 pixel
  int32_t r_subpixel = size + PS_P_RADIUS_1D + 1;  // size = 255 is a radius of 9: 33+255=288, 288>>5=9 pixels
  // rendering bounding box in pixels
  int32_t r_pixels = r_subpixel >> PS_P_RADIUS_SHIFT_1D;

  int32_t x_min = x_center - r_pixels - 1;  // extend by one for much smoother movement
  int32_t x_max = x_center + r_pixels + 1;

  // cache for speed
  uint32_t matrixX = maxXpixel + 1;

  // iterate over the bounding box and render each pixel
  for (int32_t px = x_min; px <= x_max; px++) {
    // check bounds and apply wrapping
    int32_t render_x = px;
    if (render_x < 0) {
      if (!wrap)
        continue;  // skip out of frame pixels
      render_x += matrixX;
    } else if (render_x > maxXpixel) {
      if (!wrap)
        continue;
      render_x -= matrixX;
    }
    // squared distance from the particle center
    int32_t dx_sq = ((px << PS_P_RADIUS_SHIFT_1D) - x_subcenter + PS_P_HALFRADIUS_1D);
    dx_sq = dx_sq * dx_sq;
    int32_t rx_sq = r_subpixel * r_subpixel;
    uint32_t dist_sq = (dx_sq << 8) / rx_sq;  // normalized squared distance in fixed point (0-256)

    // brightness from the distance to the particle center, with linear falloff
    uint8_t pixel_brightness = dist_sq >= 256 ? 0 : ((256 - dist_sq) * brightness) >> 8;

    framebuffer[render_x] = fast_color_scaleAdd(framebuffer[render_x], color, pixel_brightness);
  }
}

// detect collisions in an array of particles and handle them
void ParticleSystem1D::handleCollisions() {
  uint32_t collisiondistance = particleHardRadius << 1;  // twice the radius is the min distance
  uint32_t checkDistSq = std::max<int32_t>(2 * PS_P_MAXSPEED, static_cast<int32_t>(collisiondistance));
  if (perParticleSize && advPartProps != nullptr)  // using individual particle size
    checkDistSq = std::max<int32_t>(2 * PS_P_MAXSPEED, (512 * 52) >> 6);
  checkDistSq = checkDistSq * checkDistSq;  // square it for the distance comparison
  // particles are binned by position, the assumption is that no more than a quarter are in the same bin
  int binWidth = 64 * PS_P_RADIUS_1D;                                     // a compromise between speed and accuracy
  int32_t overlap = collisiondistance + (2 * PS_P_MAXSPEED);              // overlap bins, plus a speed look-ahead
  if (perParticleSize && advPartProps != nullptr)
    overlap = 512;  // 2 * max radius, enough to catch all collisions even at full speed
  uint32_t maxBinParticles = std::max<uint32_t>(50, (usedParticles + 1) / 4);
  if (maxBinParticles > binArrayEntries)
    maxBinParticles = binArrayEntries;  // the bin array is sized for numParticles, this is a safety net
  uint32_t numBins = (maxX + (binWidth - 1)) / binWidth;  // calculate the number of bins
  if (usedParticles < maxBinParticles) {
    numBins = 1;  // use a single bin for a small number of particles
    binWidth = maxX + 1;
  }
  uint32_t binParticleCount;                                // number of particles in the current bin
  uint32_t nextFrameStartIdx = hw_random16(usedParticles);  // first particle in the next frame
  uint32_t pidx = collisionStartIdx;  // start index in case a bin is full, process the rest next frame
  for (uint32_t bin = 0; bin < numBins; bin++) {
    binParticleCount = 0;                         // reset for this bin
    int32_t binStart = bin * binWidth - overlap;  // the first bin extends to negative, that is fine
    int32_t binEnd = binStart + binWidth + (overlap << 1);

    // fill the binIndices array for this bin
    for (uint32_t i = 0; i < usedParticles; i++) {
      if (particles[pidx].ttl > 0) {  // alive
        if (particles[pidx].x >= binStart && particles[pidx].x <= binEnd) {  // include particles on the bin edge
          if (particleFlags[pidx].outofbounds == 0 && particleFlags[pidx].collide) {
            if (binParticleCount >= maxBinParticles) {  // bin is full, do the rest next frame
              nextFrameStartIdx = pidx;
              break;
            }
            binIndices[binParticleCount++] = pidx;
          }
        }
      }
      pidx++;
      if (pidx >= usedParticles)
        pidx = 0;  // wrap around
    }

    for (uint32_t i = 0; i < binParticleCount; i++) {
      uint32_t idx_i = binIndices[i];
      for (uint32_t j = i + 1; j < binParticleCount; j++) {  // check against higher numbered particles
        uint32_t idx_j = binIndices[j];
        int32_t dx = particles[idx_j].x - particles[idx_i].x;  // distance between particles
        uint32_t dx_sq = dx * dx;                              // square distance
        if (dx_sq <= checkDistSq) {                            // possible collision imminent, check properly
          collideParticles(idx_i, idx_j, dx, collisiondistance);
        }
      }
    }
  }
  collisionStartIdx = nextFrameStartIdx;  // set the start index for the next frame
}

// handle a collision if close proximity is detected, i.e. dx smaller than 2*radius + speed look-ahead
void ParticleSystem1D::collideParticles(uint32_t partIdx1, uint32_t partIdx2, int32_t dx,
                                        uint32_t collisiondistance) {
  int32_t massratio1 = 0;  // 0 means don't use a mass ratio (equal mass)
  int32_t massratio2 = 0;
  if (perParticleSize && advPartProps != nullptr) {  // advanced size properties: collision distance and mass ratio
    collisiondistance = (PS_P_MINHARDRADIUS_1D * 2) + (((static_cast<uint32_t>(advPartProps[partIdx1].size) +
                                                         static_cast<uint32_t>(advPartProps[partIdx2].size)) *
                                                        52) >>
                                                       6);
    // calculate the mass ratio for the collision response
    uint32_t mass1 = PS_P_RADIUS_1D + advPartProps[partIdx1].size;
    uint32_t mass2 = PS_P_RADIUS_1D + advPartProps[partIdx2].size;
    uint32_t totalmass = mass1 + mass2 - 2;  // -2 to account for rounding
    massratio1 = (mass2 << 8) / totalmass;   // if 2 is heavier it has a higher velocity impact on 1
    massratio2 = (mass1 << 8) / totalmass;
  }
  int32_t dv = static_cast<int>(particles[partIdx2].vx) - static_cast<int>(particles[partIdx1].vx);
  int32_t absdv = ps_abs(dv);
  int32_t dotProduct = (dx * dv);  // always negative if moving towards each other
  uint32_t dx_abs = ps_abs(dx);

  if (dotProduct < 0) {  // particles are moving towards each other
    uint32_t lookaheadDistance = collisiondistance + absdv;  // collide if reaching collisiondistance this frame
    if (dx_abs <= lookaheadDistance) {
      // if one of the particles is fixed, invert the other one's velocity and put it at the fixed one's edge
      if (particleFlags[partIdx1].fixed) {
        particles[partIdx2].vx = -(particles[partIdx2].vx * collisionHardness) / 255;
        particles[partIdx2].x = particles[partIdx1].x + (dx < 0 ? -collisiondistance : collisiondistance);
        return;
      } else if (particleFlags[partIdx2].fixed) {
        particles[partIdx1].vx = -(particles[partIdx1].vx * collisionHardness) / 255;
        particles[partIdx1].x = particles[partIdx2].x + (dx < 0 ? collisiondistance : -collisiondistance);
        return;
      }
      // if particles are soft the impulse must stay above a limit or collisions slip through
      int32_t surfacehardness = std::max<int32_t>(collisionHardness, PS_P_MINSURFACEHARDNESS_1D);
      // new velocities after the collision. not using a dot product like in 2D, the impulse is purely speed based
      int32_t impulse = (dv * surfacehardness) / 255;

      // if the particles are not the same size, use a mass ratio. it is 0 if they are the same size
      if (massratio1) {
        int vx1 = static_cast<int>(particles[partIdx1].vx) + ((impulse * massratio1) >> 7);
        int vx2 = static_cast<int>(particles[partIdx2].vx) - ((impulse * massratio2) >> 7);
        // limit speeds, a lot of impulse can be transferred from a large to a small particle
        particles[partIdx1].vx = limitSpeed(vx1);
        particles[partIdx2].vx = limitSpeed(vx2);
      } else {
        particles[partIdx1].vx += impulse;
        particles[partIdx2].vx -= impulse;
      }

      // if particles are soft they become 'sticky', i.e. apply some friction
      if (collisionHardness < PS_P_MINSURFACEHARDNESS_1D && (seg->call & 0x07) == 0) {
        const uint32_t coeff = collisionHardness + (250 - PS_P_MINSURFACEHARDNESS_1D);
        particles[partIdx1].vx = (static_cast<int32_t>(particles[partIdx1].vx) * coeff) / 255;
        particles[partIdx2].vx = (static_cast<int32_t>(particles[partIdx2].vx) * coeff) / 255;
      }
    } else {
      return;  // not close enough yet
    }
  }
  // particles have volume, push them apart if they are too close.
  // like in 2D, pushing by a distance makes softer piles collapse, giving particles speed prevents that.

  if (dx_abs < collisiondistance) {  // too close, force push the particles so they do not collapse
    // push by an eighth of the deviation (plus 1 to push at least a little), too much leads to pass-throughs
    int32_t pushamount = 1 + ((collisiondistance - dx_abs) >> 3);
    int32_t addspeed = 1;
    if (dx < 0) {  // particle2.x < particle1.x
      pushamount = -pushamount;
      addspeed = -addspeed;
    }
    if (absdv < 4) {  // low relative speed, add speed to help with the pushing (less collapsing piles)
      particles[partIdx1].vx -= addspeed;
      particles[partIdx2].vx += addspeed;
    }
    // push only one particle to avoid oscillations
    bool fairlyrandom = dotProduct & 0x01;
    if (fairlyrandom) {
      particles[partIdx1].x -= pushamount;
    } else {
      particles[partIdx2].x += pushamount;
    }
  }
}

// update size and pointers (the memory location and size can change dynamically)
void ParticleSystem1D::updateSystem() {
  setSize(seg->length());  // update size
  updatePSpointers(advPartProps != nullptr);
}

// set the pointers for the class, see the alignment note on the 2D version
void ParticleSystem1D::updatePSpointers(bool isadvanced) {
  particles = reinterpret_cast<PSparticle1D *>(this + 1);                          // pointer to particles
  particleFlags = reinterpret_cast<PSparticleFlags1D *>(particles + numParticles);  // pointer to particle flags
  sources = reinterpret_cast<PSsource1D *>(particleFlags + numParticles);           // pointer to source(s)
  binIndices = reinterpret_cast<uint16_t *>(sources + numSources);  // collision binning scratch, see the header
  PSdataEnd = reinterpret_cast<uint8_t *>(binIndices + binArrayEntries);
  localbuffer = seg->is_2d() && seg->map1d2d != M12_PIXELS;
  if (localbuffer) {
    framebuffer = reinterpret_cast<uint32_t *>(PSdataEnd);  // local framebuffer for 1D-to-2D mapping
    PSdataEnd = reinterpret_cast<uint8_t *>(framebuffer + particleMaxMappingLength(*seg));
  } else {
    framebuffer = seg->canvas()->pixels();  // use the segment buffer for standard 1D rendering
  }

  if (isadvanced) {
    advPartProps = reinterpret_cast<PSadvancedParticle1D *>(PSdataEnd);
    // numParticles is a multiple of 4, so this stays 4 byte aligned
    PSdataEnd = reinterpret_cast<uint8_t *>(advPartProps + numParticles);
  }
}

// non class functions to use for initialization, fraction is uint8_t: 255 means 100%
uint32_t calculateNumberOfParticles1D(const Segment &seg, const uint32_t fraction, const bool isadvanced) {
  uint32_t numberofParticles = seg.length();  // one particle per pixel (if possible)
  uint32_t particlelimit = MAXPARTICLES_1D;   // maximum number of particles allowed
  numberofParticles = std::min<uint32_t>(numberofParticles, particlelimit);
  if (isadvanced)  // the advanced property array needs RAM, reduce the particle count to use the same amount
    numberofParticles =
        (numberofParticles * sizeof(PSparticle1D)) / (sizeof(PSparticle1D) + sizeof(PSadvancedParticle1D));
  numberofParticles = (numberofParticles * (fraction + 1)) >> 8;  // calculate the fraction of particles
  numberofParticles = numberofParticles < 10 ? 10 : numberofParticles;  // 10 minimum
  // make sure it is a multiple of 4 for proper memory alignment
  numberofParticles = (numberofParticles + 3) & ~0x03;
  return numberofParticles;
}

uint32_t calculateNumberOfSources1D(const uint32_t requestedsources) {
  uint32_t numberofSources = std::max<uint32_t>(1, std::min<uint32_t>(requestedsources, MAXSOURCES_1D));  // limit
  // make sure it is a multiple of 4 for proper memory alignment (so the minimum is actually 4)
  numberofSources = (numberofSources + 3) & ~0x03;
  return numberofSources;
}

// see calculateBinArrayEntries2D; the 1D system bins a quarter of the particles
uint32_t calculateBinArrayEntries1D(const uint32_t numparticles) {
  uint32_t entries = std::max<uint32_t>(50, (numparticles + 1) / 4);
  return (entries + 1) & ~0x01u;  // keep the byte size a multiple of 4
}

// allocate memory for the particle system class, particles, sprays plus additional memory requested by the FX
bool allocateParticleSystemMemory1D(Segment &seg, const uint32_t numparticles, const uint32_t numsources,
                                    const uint32_t binarrayentries, const bool isadvanced,
                                    const uint32_t additionalbytes) {
  uint32_t requiredmemory = sizeof(ParticleSystem1D);
  // the functions above make sure these are a multiple of 4 bytes (to avoid alignment issues)
  requiredmemory += sizeof(PSparticleFlags1D) * numparticles;
  requiredmemory += sizeof(PSparticle1D) * numparticles;
  requiredmemory += sizeof(PSsource1D) * numsources;
  requiredmemory += sizeof(uint16_t) * binarrayentries;
  if (seg.is_2d())
    requiredmemory += sizeof(uint32_t) * particleMaxMappingLength(seg);  // local buffer for mapped rendering
  requiredmemory += additionalbytes;
  if (isadvanced)
    requiredmemory += sizeof(PSadvancedParticle1D) * numparticles;
  return seg.allocate_data(requiredmemory);
}

// initialize the particle system, allocate additional bytes if needed (read the pointer from PSdataEnd)
// percentofparticles is a uint8_t, for example 191 means 75%, the default 255 means one particle per pixel
bool initParticleSystem1D(Segment &seg, ParticleSystem1D *&PartSys, const uint32_t requestedsources,
                          const uint8_t fractionofparticles, const uint32_t additionalbytes, const bool advanced) {
  if (seg.length() <= 1) {
    seg.deallocate_data();  // make sure data is null, there is no valid PS in it
    return false;           // a single pixel is not supported
  }
  uint32_t numparticles = calculateNumberOfParticles1D(seg, fractionofparticles, advanced);
  uint32_t numsources = calculateNumberOfSources1D(requestedsources);
  bool allocsuccess = false;
  uint32_t binarrayentries = 0;
  while (numparticles >= 10) {  // make sure we have at least 10 particles or quit
    binarrayentries = calculateBinArrayEntries1D(numparticles);
    if (allocateParticleSystemMemory1D(seg, numparticles, numsources, binarrayentries, advanced, additionalbytes)) {
      allocsuccess = true;
      break;  // allocation succeeded
    }
    numparticles = ((numparticles / 2) + 3) & ~0x03;  // halve the particle count and try again, 4 byte aligned
  }
  if (!allocsuccess) {
    return false;  // allocation failed
  }
  PartSys = new (seg.data) ParticleSystem1D(seg, seg.length(), numparticles, numsources, binarrayentries, advanced);
  return true;
}

}  // namespace wled_fx
}  // namespace esphome
