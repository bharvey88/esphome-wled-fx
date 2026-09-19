#pragma once

/* Particle system with functions for particle generation, particle movement and
 * particle rendering to an RGB matrix. Ported from WLED 16.0.1
 * wled00/FXparticleSystem.h with the transform described in PORTING.md.
 *
 * by DedeHai (Damian Schneider) 2013-2024
 * Copyright (c) 2024 Damian Schneider
 * Licensed under the EUPL v. 1.2 or later, distributed here under GPLv3 per the
 * EUPL Article 5 compatibility clause.
 *
 * WLED's struct, field and method names are kept verbatim (PORTING.md deviation
 * 6), because that is what lets the 31 particle effect bodies be copied rather
 * than rewritten. See the "Particle effects" section of PORTING.md for how an
 * effect uses this.
 */

#include <cstddef>
#include <cstdint>

#include "wf_color.h"
#include "wf_segment.h"

namespace esphome {
namespace wled_fx {

// Maximum speed a particle can have. vx/vy are int8, so this stays below 127 to
// avoid overflows in collisions caused by rounding errors.
inline constexpr int32_t PS_P_MAXSPEED = 120;

// Limit the speed of particles (used in 1D and 2D).
inline int32_t limitSpeed(const int32_t speed) {
  return speed > PS_P_MAXSPEED ? PS_P_MAXSPEED : (speed < -PS_P_MAXSPEED ? -PS_P_MAXSPEED : speed);
}

// ---------------------------------------------------------------------------
// 2D particle system
// ---------------------------------------------------------------------------

// Memory allocation limits, based on a reasonable segment size and the available
// FX memory. Upstream picks these with the same preprocessor tests.
#if defined(ESP8266)
inline constexpr uint32_t MAXPARTICLES_2D = 256;
inline constexpr uint32_t MAXSOURCES_2D = 24;
inline constexpr uint32_t SOURCEREDUCTIONFACTOR = 8;
#elif defined(ARDUINO_ARCH_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S2)
inline constexpr uint32_t MAXPARTICLES_2D = 1024;
inline constexpr uint32_t MAXSOURCES_2D = 64;
inline constexpr uint32_t SOURCEREDUCTIONFACTOR = 6;
#else
inline constexpr uint32_t MAXPARTICLES_2D = 2048;
inline constexpr uint32_t MAXSOURCES_2D = 128;
inline constexpr uint32_t SOURCEREDUCTIONFACTOR = 4;
#endif

// Particle dimensions (subpixel division).
inline constexpr int32_t PS_P_RADIUS = 64;  // each pixel is divided by this, must be a power of two
inline constexpr int32_t PS_P_HALFRADIUS = PS_P_RADIUS >> 1;
inline constexpr int32_t PS_P_RADIUS_SHIFT = 6;
inline constexpr int32_t PS_P_SURFACE = 12;  // shift: 2^PS_P_SURFACE = PS_P_RADIUS^2
inline constexpr int32_t PS_P_MINHARDRADIUS = 64;        // minimum hard surface radius for collisions
inline constexpr int32_t PS_P_MINSURFACEHARDNESS = 128;  // below this hardness particles become sticky

// Settings shared by the whole 2D system.
union PSsettings2D {
  struct {  // one byte bit field for 2D settings
    bool wrapX : 1;
    bool wrapY : 1;
    bool bounceX : 1;
    bool bounceY : 1;
    bool killoutofbounds : 1;  // if set, out of bound particles are killed immediately
    bool useGravity : 1;       // set to 1 if gravity is used, disables bounceY at the top
    bool useCollisions : 1;
    bool colorByAge : 1;  // if set, particle hue is set by ttl value in the render function
  };
  uint8_t asByte;  // access as a byte, order is: LSB is the first entry in the list above
};

// A single particle (10 bytes).
struct PSparticle {
  int16_t x;     // x position in the particle system
  int16_t y;     // y position in the particle system
  uint16_t ttl;  // time to live in frames
  int8_t vx;     // horizontal velocity
  int8_t vy;     // vertical velocity
  uint8_t hue;   // color hue
  uint8_t sat;   // particle color saturation
};

// Particle flags, kept separate from the particle struct to save RAM alignment.
union PSparticleFlags {
  struct {  // 1 byte
    bool outofbounds : 1;  // set when the particle is outside the display area
    bool collide : 1;      // if set, the particle takes part in collisions
    bool perpetual : 1;    // if set, the particle does not age (it still dies from killoutofbounds)
    bool custom1 : 1;      // unused custom flags, an FX can use them to track particle states
    bool custom2 : 1;
    bool custom3 : 1;
    bool custom4 : 1;
    bool custom5 : 1;
  };
  uint8_t asByte;
};

// Additional particle settings (option, 2 bytes).
struct PSadvancedParticle {
  uint8_t size;          // particle size, 255 means 10 pixels in diameter, needs perParticleSize
  uint8_t forcecounter;  // counter for applying forces to individual particles
};

// Advanced particle size control (option, 8 bytes).
struct PSsizeControl {
  uint8_t asymmetry;  // asymmetrical size (0 = symmetrical, 255 fully asymmetric)
  uint8_t asymdir;    // direction of asymmetry, 64 is x, 192 is y (0 and 128 are symmetrical)
  uint8_t maxsize;    // target size for growing
  uint8_t minsize;    // target size for shrinking
  uint8_t sizecounter : 4;  // counters used for size control (grow/shrink/wobble)
  uint8_t wobblecounter : 4;
  uint8_t growspeed : 4;
  uint8_t shrinkspeed : 4;
  uint8_t wobblespeed : 4;
  bool grow : 1;  // flags
  bool shrink : 1;
  bool pulsate : 1;  // grows and shrinks and grows and ...
  bool wobble : 1;   // alternate x and y size
};

// A particle source (20 bytes).
struct PSsource {
  uint16_t minLife;             // minimum ttl of emitted particles
  uint16_t maxLife;             // maximum ttl of emitted particles
  PSparticle source;            // a particle used as the emitter (speed, position, color)
  PSparticleFlags sourceFlags;  // flags for the source particle
  int8_t var;                   // variation of emitted speed (adds random(+/- var) to speed)
  int8_t vx;                    // emitting speed
  int8_t vy;
  uint8_t size;  // particle size (advanced property), global size is added on top of this
};

// The class itself uses approximately 70 bytes.
class ParticleSystem2D {
 public:
  ParticleSystem2D(Segment &segment, const uint32_t width, const uint32_t height, const uint32_t numberofparticles,
                   const uint32_t numberofsources, const uint32_t binarrayentries, const bool isadvanced = false,
                   const bool sizecontrol = false);
  // note: memory is allocated in the FX function, no destructor needed
  void update();                            // update the particles according to the set options and render them
  void updateFire(const uint8_t intensity);  // update function for fire
  void updateSystem();                       // call at the beginning of every FX, updates pointers and dimensions
  void particleMoveUpdate(PSparticle &part, PSparticleFlags &partFlags, PSsettings2D *options = nullptr,
                          PSadvancedParticle *advancedproperties = nullptr);
  // particle emitters
  int32_t sprayEmit(const PSsource &emitter);
  void flameEmit(const PSsource &emitter);
  int32_t angleEmit(PSsource &emitter, const uint16_t angle, const int32_t speed);
  // particle physics
  void applyGravity(PSparticle &part);  // applies gravity to a single particle (use this for sources)
  [[gnu::hot]] void applyForce(PSparticle &part, const int8_t xforce, const int8_t yforce, uint8_t &counter);
  [[gnu::hot]] void applyForce(const uint32_t particleindex, const int8_t xforce, const int8_t yforce);
  void applyForce(const int8_t xforce, const int8_t yforce);  // apply a force to all particles
  void applyAngleForce(PSparticle &part, const int8_t force, const uint16_t angle, uint8_t &counter);
  void applyAngleForce(const uint32_t particleindex, const int8_t force, const uint16_t angle);
  void applyAngleForce(const int8_t force, const uint16_t angle);  // apply an angular force to all particles
  void applyFriction(PSparticle &part, const int32_t coefficient);
  void applyFriction(const int32_t coefficient);  // apply friction to all used particles
  void pointAttractor(const uint32_t particleindex, PSparticle &attractor, const uint8_t strength, const bool swallow);
  // set options. note: inlining the set functions uses more flash, so do not optimize
  void setUsedParticles(const uint8_t percentage);   // percentage of particles used, 255 = 100%
  void setCollisionHardness(const uint8_t hardness);  // 255 means fully hard
  void setWallHardness(const uint8_t hardness);       // hardness for bouncing on a wall if bounceXY is set
  void setWallRoughness(const uint8_t roughness);     // wall roughness randomizes wall collisions
  void setMatrixSize(const uint32_t x, const uint32_t y);
  void setWrapX(const bool enable);
  void setWrapY(const bool enable);
  void setBounceX(const bool enable);
  void setBounceY(const bool enable);
  void setKillOutOfBounds(const bool enable);  // if enabled, particles outside the matrix instantly die
  void setSaturation(const uint8_t sat);       // set the global color saturation
  void setColorByAge(const bool enable);
  void setMotionBlur(const uint8_t bluramount);  // note: only usable if 'particlesize' is zero
  void setSmearBlur(const uint8_t bluramount);   // 2D smeared blurring of the full frame
  void setParticleSize(const uint8_t size);
  void setGravity(const int8_t force = 8);
  void enableParticleCollisions(const bool enable, const uint8_t hardness = 255);

  PSparticle *particles;            // pointer to the particle array
  PSparticleFlags *particleFlags;   // pointer to the particle flags array
  PSsource *sources;                // pointer to the sources
  PSadvancedParticle *advPartProps;  // advanced particle properties (can be null)
  PSsizeControl *advPartSize;        // advanced particle size control (can be null)
  uint8_t *PSdataEnd;  // first available byte after the PS memory, set in updatePSpointers(); FX custom data
  int32_t maxX, maxY;  // system size, i.e. width-1 / height-1 in subpixels. Signed, they are compared to coordinates
  int32_t maxXpixel, maxYpixel;  // last physical pixel that can be drawn to, equal to width-1 / height-1
  uint32_t numSources;           // number of sources
  uint32_t usedParticles;        // number of particles used in the animation, relative to 'numParticles'
  bool perParticleSize;          // use individual particle sizes from advPartProps if available

 private:
  // rendering functions
  void render();
  [[gnu::hot]] void renderParticle(const uint32_t particleindex, const uint8_t brightness, const CRGBW &color,
                                   const bool wrapX, const bool wrapY);
  void renderLargeParticle(const uint32_t size, const uint32_t particleindex, const uint8_t brightness,
                           const CRGBW &color, const bool wrapX, const bool wrapY);
  // particle physics applied by the system if the flags are set
  void applyGravity();  // applies gravity to all particles
  void handleCollisions();
  void collideParticles(PSparticle &particle1, PSparticle &particle2, int32_t dx, int32_t dy,
                        const uint32_t collDistSq, int32_t massratio1, int32_t massratio2);
  void fireParticleupdate();
  // utility functions
  void updatePSpointers(const bool isadvanced, const bool sizecontrol);
  bool updateSize(PSadvancedParticle *advprops, PSsizeControl *advsize);
  void getParticleXYsize(PSadvancedParticle *advprops, PSsizeControl *advsize, uint32_t &xsize, uint32_t &ysize);
  [[gnu::hot]] void bounce(int8_t &incomingspeed, int8_t &parallelspeed, int32_t &position,
                           const uint32_t maxposition);
  // note: variables that are accessed often are 32 bit for speed
  Segment *seg;           // the segment this system renders into, replaces WLED's global SEGMENT
  uint32_t *framebuffer;  // frame buffer for rendering, the segment's canvas
  // Collision binning scratch. Upstream puts this on the stack as a variable
  // length array, which this port does not allow, so it lives in the PS memory
  // block. See PORTING.md, "Particle effects".
  uint16_t *binIndices;
  uint32_t binArrayEntries;
  PSsettings2D particlesettings;  // settings used when updating particles, use the set functions above
  uint32_t numParticles;          // total number of particles allocated by this system
  uint32_t emitIndex;             // index used to count through particles to emit, so finding a dead one is faster
  int32_t collisionHardness;
  uint32_t wallHardness;
  uint32_t wallRoughness;       // randomizes wall collisions
  uint32_t particleHardRadius;  // hard surface radius of a particle, used for collision detection
  uint16_t collisionStartIdx;   // particle array start index for collision detection
  uint8_t fireIntesity = 0;     // fire intensity, used for fire mode
  uint8_t forcecounter;         // counter for globally applied forces
  uint8_t gforcecounter;        // counter for global gravity
  int8_t gforce;                // gravity strength, default 8 (negative is allowed, positive is downwards)
  // global particle properties for basic particles
  uint8_t particlesize;  // 0 = 1 pixel, 1 = 2 pixels, 255 = 10 pixels, added to individually sized particles
  uint8_t motionBlur;    // values > 100 give smoother animations, does not work if particlesize > 0
  uint8_t smearBlur;     // 2D smeared blurring of the full frame
};

// Initialization functions, not part of the class. `seg` replaces WLED's global
// SEGMENT: the system keeps the reference and renders into that segment's canvas.
bool initParticleSystem2D(Segment &seg, ParticleSystem2D *&PartSys, const uint32_t requestedsources,
                          const uint32_t additionalbytes = 0, const bool advanced = false,
                          const bool sizecontrol = false);
uint32_t calculateNumberOfParticles2D(const uint32_t pixels, const bool advanced, const bool sizecontrol);
uint32_t calculateNumberOfSources2D(const uint32_t pixels, const uint32_t requestedsources);
uint32_t calculateBinArrayEntries2D(const uint32_t numparticles);
bool allocateParticleSystemMemory2D(Segment &seg, const uint32_t numparticles, const uint32_t numsources,
                                    const uint32_t binarrayentries, const bool advanced, const bool sizecontrol,
                                    const uint32_t additionalbytes);

// Distance based brightness for ellipse rendering, returns brightness 0 to 255
// from the distance to the ellipse centre.
inline uint8_t calculateEllipseBrightness(int32_t dx, int32_t dy, int32_t rxsq, int32_t rysq, uint8_t maxBrightness) {
  // square the distances
  uint32_t dx_sq = dx * dx;
  uint32_t dy_sq = dy * dy;

  // normalized squared distance in fixed point: (dx^2/rx^2) * 256 + (dy^2/ry^2) * 256
  uint32_t dist_sq = ((dx_sq << 8) / rxsq) + ((dy_sq << 8) / rysq);

  if (dist_sq >= 256)
    return 0;  // pixel is outside the ellipse, unit radius in fixed point: 256 = 1.0
  int32_t falloff = 256 - dist_sq;
  return (maxBrightness * falloff) >> 8;  // linear falloff
}

// ---------------------------------------------------------------------------
// 1D particle system
// ---------------------------------------------------------------------------

#if defined(ESP8266)
inline constexpr uint32_t MAXPARTICLES_1D = 320;
inline constexpr uint32_t MAXSOURCES_1D = 16;
#elif defined(ARDUINO_ARCH_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S2)
inline constexpr uint32_t MAXPARTICLES_1D = 1300;
inline constexpr uint32_t MAXSOURCES_1D = 32;
#else
inline constexpr uint32_t MAXPARTICLES_1D = 2600;
inline constexpr uint32_t MAXSOURCES_1D = 64;
#endif

// Particle dimensions (subpixel division).
inline constexpr int32_t PS_P_RADIUS_1D = 32;
inline constexpr int32_t PS_P_HALFRADIUS_1D = PS_P_RADIUS_1D >> 1;
inline constexpr int32_t PS_P_RADIUS_SHIFT_1D = 5;
inline constexpr int32_t PS_P_SURFACE_1D = 5;  // shift: 2^PS_P_SURFACE_1D = PS_P_RADIUS_1D
// Do not change PS_P_MINHARDRADIUS_1D or the hourglass effect breaks.
inline constexpr int32_t PS_P_MINHARDRADIUS_1D = 32;
inline constexpr int32_t PS_P_MINSURFACEHARDNESS_1D = 120;

union PSsettings1D {
  struct {  // one byte bit field for 1D settings
    bool wrap : 1;
    bool bounce : 1;
    bool killoutofbounds : 1;  // if set, out of bound particles are killed immediately
    bool useGravity : 1;       // set to 1 if gravity is used, disables bounce at the top
    bool useCollisions : 1;
    bool colorByAge : 1;       // if set, particle hue is set by ttl value in the render function
    bool colorByPosition : 1;  // if set, particle hue is set by its position in the strip segment
    bool unused : 1;
  };
  uint8_t asByte;
};

// A single particle (8 bytes).
struct PSparticle1D {
  int32_t x;     // x position in the particle system
  uint16_t ttl;  // time to live in frames
  int8_t vx;     // horizontal velocity
  uint8_t hue;   // color hue
};

union PSparticleFlags1D {
  struct {  // 1 byte
    bool outofbounds : 1;
    bool collide : 1;
    bool perpetual : 1;      // if set, the particle does not age
    bool reversegrav : 1;    // if set, gravity is reversed on this particle
    bool forcedirection : 1;  // direction the force was applied, 1 is positive x
    bool fixed : 1;           // if set, the particle does not move
    bool custom1 : 1;         // unused custom flags, an FX can use them to track particle states
    bool custom2 : 1;
  };
  uint8_t asByte;
};

// Additional particle settings (optional, 3 bytes).
struct PSadvancedParticle1D {
  uint8_t sat;           // color saturation
  uint8_t size;          // particle size, 255 means 10 pixels in diameter, overrides the global size
  uint8_t forcecounter;  // counter for applying forces to individual particles
};

// A particle source (20 bytes, 3 bytes of padding are added at the end).
struct PSsource1D {
  uint16_t minLife;
  uint16_t maxLife;
  PSparticle1D source;
  PSparticleFlags1D sourceFlags;
  int8_t var;    // variation of emitted speed (adds random(+/- var) to speed)
  int8_t v;      // emitting speed
  uint8_t sat;   // color saturation (advanced property)
  uint8_t size;  // particle size (advanced property)
};

class ParticleSystem1D {
 public:
  ParticleSystem1D(Segment &segment, const uint32_t length, const uint32_t numberofparticles,
                   const uint32_t numberofsources, const uint32_t binarrayentries, const bool isadvanced = false);
  // note: memory is allocated in the FX function, no destructor needed
  void update();        // update the particles according to the set options and render them
  void updateSystem();  // call at the beginning of every FX, updates pointers and dimensions
  // particle emitters
  int32_t sprayEmit(const PSsource1D &emitter);
  void particleMoveUpdate(PSparticle1D &part, PSparticleFlags1D &partFlags, PSsettings1D *options = nullptr,
                          PSadvancedParticle1D *advancedproperties = nullptr);
  // particle physics
  [[gnu::hot]] void applyForce(PSparticle1D &part, const int8_t xforce, uint8_t &counter);
  void applyForce(const int8_t xforce);  // apply a force to all particles
  void applyGravity(PSparticle1D &part, PSparticleFlags1D &partFlags);
  void applyFriction(const int32_t coefficient);  // apply friction to all used particles
  // set options
  void setUsedParticles(const uint8_t percentage);
  void setWallHardness(const uint8_t hardness);
  void setSize(const uint32_t x);  // set the particle system size (= strip length)
  void setWrap(const bool enable);
  void setBounce(const bool enable);
  void setKillOutOfBounds(const bool enable);
  void setColorByAge(const bool enable);
  void setColorByPosition(const bool enable);
  void setMotionBlur(const uint8_t bluramount);
  void setSmearBlur(const uint8_t bluramount);
  void setParticleSize(const uint8_t size);
  void setGravity(int8_t force = 8);
  void enableParticleCollisions(bool enable, const uint8_t hardness = 255);

  PSparticle1D *particles;
  PSparticleFlags1D *particleFlags;
  PSsource1D *sources;
  PSadvancedParticle1D *advPartProps;
  uint8_t *PSdataEnd;
  int32_t maxX;       // system size, i.e. width-1 in subpixels
  int32_t maxXpixel;  // last physical pixel that can be drawn to, equal to width-1
  uint32_t numSources;
  uint32_t usedParticles;
  bool perParticleSize;

 private:
  // rendering functions
  void render();
  void renderParticle(const uint32_t particleindex, const uint8_t brightness, const CRGBW &color, const bool wrap);
  void renderLargeParticle(const uint32_t size, const uint32_t particleindex, const uint8_t brightness,
                           const CRGBW &color, const bool wrap);
  // particle physics applied by the system if the flags are set
  void applyGravity();
  void handleCollisions();
  void collideParticles(uint32_t partIdx1, uint32_t partIdx2, int32_t dx, uint32_t collisiondistance);
  // utility functions
  void updatePSpointers(const bool isadvanced);
  // note: upstream declares a bounce() here that it never defines; the 1D wall bounce is inlined in
  // particleMoveUpdate, so this port leaves it out.

  Segment *seg;
  uint32_t *framebuffer;  // the segment's canvas, or a local buffer when rendering through a 1D-to-2D mapping
  bool localbuffer;       // true when framebuffer is the local mapped buffer and has to be transferred
  uint16_t *binIndices;   // collision binning scratch, see the 2D note above
  uint32_t binArrayEntries;
  PSsettings1D particlesettings;
  uint32_t numParticles;
  uint32_t emitIndex;
  int32_t collisionHardness;
  uint32_t particleHardRadius;
  uint32_t wallHardness;
  uint8_t gforcecounter;
  int8_t gforce;
  uint8_t forcecounter;
  uint16_t collisionStartIdx;
  // global particle properties for basic particles
  uint8_t particlesize;
  uint8_t motionBlur;
  uint8_t smearBlur;
};

bool initParticleSystem1D(Segment &seg, ParticleSystem1D *&PartSys, const uint32_t requestedsources,
                          const uint8_t fractionofparticles = 255, const uint32_t additionalbytes = 0,
                          const bool advanced = false);
uint32_t calculateNumberOfParticles1D(const Segment &seg, const uint32_t fraction, const bool isadvanced);
uint32_t calculateNumberOfSources1D(const uint32_t requestedsources);
uint32_t calculateBinArrayEntries1D(const uint32_t numparticles);
bool allocateParticleSystemMemory1D(Segment &seg, const uint32_t numparticles, const uint32_t numsources,
                                    const uint32_t binarrayentries, const bool isadvanced,
                                    const uint32_t additionalbytes);

// WLED's Segment::maxMappingLength(): the longest a mapped 1D segment can get,
// used to size the local render buffer. Upstream has it on Segment; it is here so
// no engine file has to change.
uint32_t particleMaxMappingLength(const Segment &seg);

}  // namespace wled_fx
}  // namespace esphome
