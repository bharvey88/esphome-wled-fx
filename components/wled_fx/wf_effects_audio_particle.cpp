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

/* Effects that are both audio reactive and particle based. They read seg.audio()
 * and run on the particle system in wf_particle.h. See the "Particle effects" and
 * "Audio effects" sections of PORTING.md for the init pattern and the pitfalls. */

#include <algorithm>
#include <utility>

#include "wf_effects.h"
#include "wf_particle.h"

// Whole-file guard. Every WLED_FX_FX_* macro this file can provide goes in the
// list, one per effect, so a YAML allow-list that names any of them links this
// translation unit in.
#define WLED_FX_GROUP_AUDIO_PARTICLE                                                                 \
  (WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_SPRAY || WLED_FX_FX_PS_GEQ_2D || WLED_FX_FX_PS_GEQ_NOVA || \
   WLED_FX_FX_PS_BLOBS || WLED_FX_FX_PS_GEQ_1D || WLED_FX_FX_PS_SONIC_STREAM ||                      \
   WLED_FX_FX_PS_SONIC_BOOM || WLED_FX_FX_PS_SPRINGY)

#if WLED_FX_GROUP_AUDIO_PARTICLE

namespace esphome {
namespace wled_fx {
namespace {

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_SPRAY
/*
  Particle Spray, just a particle spray with many parameters
  Uses palette for particle color
  by DedeHai (Damian Schneider)
*/
void mode_particlespray(Segment &seg) {
  ParticleSystem2D *PartSys = nullptr;
  const uint8_t hardness = 200; // collision hardness is fixed

  if (seg.call == 0) { // initialization
    if (!initParticleSystem2D(seg, PartSys, 1)) // init, no additional data needed
      FX_FALLBACK_STATIC; // allocation failed or not 2D
    PartSys->setKillOutOfBounds(true); // out of bounds particles dont return (except on top, taken care of by gravity setting)
    PartSys->setBounceY(true);
    PartSys->setMotionBlur(200); // anable motion blur
    PartSys->setSmearBlur(10); // anable motion blur
    PartSys->sources[0].source.hue = hw_random16();
    PartSys->sources[0].sourceFlags.collide = true; // seeded particles will collide (if enabled)
    PartSys->sources[0].var = 3;
  }
  else
    PartSys = reinterpret_cast<ParticleSystem2D *>(seg.data); // if not first call, just set the pointer to the PS

  if (PartSys == nullptr)
    FX_FALLBACK_STATIC; // something went wrong, no data!

  // Particle System settings
  PartSys->updateSystem(); // update system properties (dimensions and data pointers)
  PartSys->setBounceX(!seg.check2);
  PartSys->setWrapX(seg.check2);
  PartSys->setWallHardness(hardness);
  PartSys->setGravity(8 * seg.check1); // enable gravity if checked (8 is default strength)
  //numSprays = min(PartSys->numSources, (uint8_t)1); // number of sprays

  if (seg.check3) // collisions enabled
    PartSys->enableParticleCollisions(true, hardness); // enable collisions and set particle collision hardness
  else
    PartSys->enableParticleCollisions(false);

  //position according to sliders
  PartSys->sources[0].source.x = wf_map(seg.custom1, 0, 255, 0, PartSys->maxX);
  PartSys->sources[0].source.y = wf_map(seg.custom2, 0, 255, 0, PartSys->maxY);
  uint16_t angle = (256 - (((int32_t)seg.custom3 + 1) << 3)) << 8;

  /* Upstream's `UsermodManager::getUMData(..., USERMOD_ID_AUDIOREACTIVE)` is
   * seg.has_real_audio() here: it asks whether a microphone is attached, not
   * whether there is a frame to read. seg.audio() always has a frame, because it
   * falls back to simulateSound() (PORTING.md deviation 10), so without this
   * distinction the non-audio branch would never run. */
  if (seg.has_real_audio()) {  // get AR data, do not use simulated data
    AudioData &audio = seg.audio();
    uint32_t volumeSmth  = (uint8_t)audio.volume_smth; //0 to 255
    uint32_t volumeRaw   = (int16_t)audio.volume_raw; //0 to 255
    PartSys->sources[0].minLife = 30;

    if (seg.call % 20 == 0 || seg.call % (11 - volumeSmth / 25) == 0) { // defines interval of particle emit
      PartSys->sources[0].maxLife = (volumeSmth >> 1) + (seg.intensity >> 1); // lifetime in frames
      PartSys->sources[0].var = 1 + ((volumeRaw * seg.speed)  >> 12);
      uint32_t emitspeed = (seg.speed >> 2) + (volumeRaw >> 3);
      PartSys->sources[0].source.hue += volumeSmth/30;
      PartSys->angleEmit(PartSys->sources[0], angle, emitspeed);
    }
  }
  else { //no AR data, fall back to normal mode
    // change source properties
    if (seg.call % (11 - (seg.intensity / 25)) == 0) { // every nth frame, cycle color and emit particles
      PartSys->sources[0].maxLife = 300 + seg.intensity; // lifetime in frames
      PartSys->sources[0].minLife = 150 + seg.intensity;
      PartSys->sources[0].source.hue++; // = hw_random16(); //change hue of spray source
      PartSys->angleEmit(PartSys->sources[0], angle, seg.speed >> 2);
    }
  }

  PartSys->update(); // update and render
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_GEQ_2D
/*
  Particle base Graphical Equalizer
  Uses palette for particle color
  by DedeHai (Damian Schneider)
*/
void mode_particleGEQ(Segment &seg) {
  ParticleSystem2D *PartSys = nullptr;

  if (seg.call == 0) { // initialization
    if (!initParticleSystem2D(seg, PartSys, 1))
      FX_FALLBACK_STATIC; // allocation failed or not 2D
    PartSys->setKillOutOfBounds(true);
    PartSys->setUsedParticles(170); // use 2/3 of available particles
  }
  else
    PartSys = reinterpret_cast<ParticleSystem2D *>(seg.data); // if not first call, just set the pointer to the PS
  if (PartSys == nullptr)
    FX_FALLBACK_STATIC; // something went wrong, no data!

  uint32_t i;
  // set particle system properties
  PartSys->updateSystem(); // update system properties (dimensions and data pointers)
  PartSys->setWrapX(seg.check1);
  PartSys->setBounceX(seg.check2);
  PartSys->setBounceY(seg.check3);
  //PartSys->enableParticleCollisions(false);
  PartSys->setWallHardness(seg.custom2);
  PartSys->setGravity(seg.custom3 << 2); // set gravity strength

  AudioData &audio = seg.audio();
  uint8_t *fftResult = audio.fft_result; // 16 bins with FFT data, log mapped already, each band contains frequency amplitude 0-255

  //map the bands into 16 positions on x axis, emit some particles according to frequency loudness
  i = 0;
  uint32_t binwidth = (PartSys->maxX + 1)>>4; //emit poisition variation for one bin (+/-) is equal to width/16 (for 16 bins)
  uint32_t threshold = 300 - seg.intensity;
  uint32_t emitparticles = 0;

  for (uint32_t bin = 0; bin < 16; bin++) {
    uint32_t xposition = binwidth*bin + (binwidth>>1); // emit position according to frequency band
    uint8_t emitspeed = ((uint32_t)fftResult[bin] * (uint32_t)seg.speed) >> 9; // emit speed according to loudness of band (127 max!)
    emitparticles = 0;

    if (fftResult[bin] > threshold) {
      emitparticles = 1;// + (fftResult[bin]>>6);
    }
    else if (fftResult[bin] > 0) { // band has low volue
      uint32_t restvolume = ((threshold - fftResult[bin])>>2) + 2;
      if (hw_random16() % restvolume == 0)
        emitparticles = 1;
    }

    while (i < PartSys->usedParticles && emitparticles > 0) { // emit particles if there are any left, low frequencies take priority
      if (PartSys->particles[i].ttl == 0) { // find a dead particle
        //set particle properties TODO: could also use the spray...
        PartSys->particles[i].ttl = 20 + wf_map(seg.intensity, 0,255, emitspeed>>1, emitspeed + hw_random16(emitspeed)) ; // set particle alive, particle lifespan is in number of frames
        PartSys->particles[i].x = xposition + hw_random16(binwidth) - (binwidth>>1); // position randomly, deviating half a bin width
        PartSys->particles[i].y = 0; // start at the bottom
        PartSys->particles[i].vx = hw_random16(seg.custom1>>1)-(seg.custom1>>2) ; //x-speed variation: +/- custom1/4
        PartSys->particles[i].vy = emitspeed;
        PartSys->particles[i].hue = (bin<<4) + hw_random16(17) - 8; // color from palette according to bin
        emitparticles--;
      }
      i++;
    }
  }

  PartSys->update(); // update and render
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_GEQ_NOVA
/*
  Particle rotating GEQ
  Particles sprayed from center with rotating spray
  Uses palette for particle color
  by DedeHai (Damian Schneider)
*/
void mode_particlecenterGEQ(Segment &seg) {
  constexpr uint32_t NUMBEROFSOURCES = 16;
  ParticleSystem2D *PartSys = nullptr;
  uint8_t numSprays;
  uint32_t i;

  if (seg.call == 0) { // initialization
    if (!initParticleSystem2D(seg, PartSys, NUMBEROFSOURCES))  // init, request 16 sources
      FX_FALLBACK_STATIC; // allocation failed or not 2D

    numSprays = std::min<uint32_t>(PartSys->numSources, (uint32_t)NUMBEROFSOURCES);
    for (i = 0; i < numSprays; i++) {
      PartSys->sources[i].source.x = (PartSys->maxX + 1) >> 1; // center
      PartSys->sources[i].source.y = (PartSys->maxY + 1) >> 1; // center
      PartSys->sources[i].source.hue = i * 16; // even color distribution
      PartSys->sources[i].maxLife = 400;
      PartSys->sources[i].minLife = 200;
    }
    PartSys->setKillOutOfBounds(true);
  }
  else
    PartSys = reinterpret_cast<ParticleSystem2D *>(seg.data); // if not first call, just set the pointer to the PS

  if (PartSys == nullptr)
    FX_FALLBACK_STATIC; // something went wrong, no data!

  PartSys->updateSystem(); // update system properties (dimensions and data pointers)
  numSprays = std::min<uint32_t>(PartSys->numSources, (uint32_t)NUMBEROFSOURCES);

  AudioData &audio = seg.audio();
  uint8_t *fftResult = audio.fft_result; // 16 bins with FFT data, log mapped already, each band contains frequency amplitude 0-255
  uint32_t threshold = 300 - seg.intensity;

  if (seg.check2)
    seg.aux0 += seg.custom1 << 2;
  else
    seg.aux0 -= seg.custom1 << 2;

  uint16_t angleoffset = (uint16_t)0xFFFF / (uint16_t)numSprays;
  uint32_t j = hw_random16(numSprays); // start with random spray so all get a chance to emit a particle if maximum number of particles alive is reached.
  for (i = 0; i < numSprays; i++) {
    if (seg.call % (32 - (seg.custom2 >> 3)) == 0 && seg.custom2 > 0)
      PartSys->sources[j].source.hue += 1 + (seg.custom2 >> 4);

    PartSys->sources[j].var = seg.custom3 >> 2;
    int8_t emitspeed = 5 + (((uint32_t)fftResult[j] * ((uint32_t)seg.speed + 20)) >> 10); // emit speed according to loudness of band
    uint16_t emitangle = j * angleoffset + seg.aux0;

    uint32_t emitparticles = 0;
    if (fftResult[j] > threshold)
      emitparticles = 1;
    else if (fftResult[j] > 0) { // band has low value
      uint32_t restvolume = ((threshold - fftResult[j]) >> 2) + 2;
      if (hw_random16() % restvolume == 0)
        emitparticles = 1;
    }
    if (emitparticles)
      PartSys->angleEmit(PartSys->sources[j], emitangle, emitspeed);

    j = (j + 1) % numSprays;
  }
  PartSys->update(); // update and render
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_BLOBS
/*
  PS Blobs: large particles bouncing around, changing size and form
  Uses palette for particle color
  by DedeHai (Damian Schneider)
*/
void mode_particleblobs(Segment &seg) {
  ParticleSystem2D *PartSys = nullptr;

  if (seg.call == 0) {
    if (!initParticleSystem2D(seg, PartSys, 0, 0, true, true)) //init, no additional bytes, advanced size & size control
      FX_FALLBACK_STATIC; // allocation failed or not 2D
    PartSys->setBounceX(true);
    PartSys->setBounceY(true);
    PartSys->setWallHardness(255);
    PartSys->setWallRoughness(255);
    PartSys->setCollisionHardness(255);
    PartSys->perParticleSize = true; // enable per particle size control
  }
  else
    PartSys = reinterpret_cast<ParticleSystem2D *>(seg.data); // if not first call, just set the pointer to the PS

  if (PartSys == nullptr)
    FX_FALLBACK_STATIC; // something went wrong, no data!

  PartSys->updateSystem(); // update system properties (dimensions and data pointers)
  PartSys->setUsedParticles(wf_map(seg.intensity, 0, 255, 25, 128)); // minimum 10%, maximum 50% of available particles (note: PS ensures at least 1)
  PartSys->enableParticleCollisions(seg.check2);

  for (uint32_t i = 0; i < PartSys->usedParticles; i++) { // update particles
    if (seg.aux0 != seg.speed || PartSys->particles[i].ttl == 0) { // speed changed or dead
      PartSys->particles[i].vx = (int8_t)hw_random16(seg.speed >> 1) - (seg.speed >> 2); // +/- speed/4
      PartSys->particles[i].vy = (int8_t)hw_random16(seg.speed >> 1) - (seg.speed >> 2);
    }
    if (seg.aux1 != seg.custom1 || PartSys->particles[i].ttl == 0) // size changed or dead
      PartSys->advPartSize[i].maxsize = 60 + (seg.custom1 >> 1) + hw_random16((seg.custom1 >> 2)); // set each particle to slightly randomized size

    //PartSys->particles[i].perpetual = seg.check2; //infinite life if set
    if (PartSys->particles[i].ttl == 0) { // find dead particle, renitialize
      PartSys->particles[i].ttl = 300 + hw_random16(((uint16_t)seg.custom2 << 3) + 100);
      PartSys->particles[i].x = hw_random(PartSys->maxX);
      PartSys->particles[i].y = hw_random16(PartSys->maxY);
      PartSys->particles[i].hue = hw_random16(); // set random color
      PartSys->particleFlags[i].collide = true; // enable collision for particle
      PartSys->advPartProps[i].size = 0; // start out small
      PartSys->advPartSize[i].asymmetry = hw_random16(220);
      PartSys->advPartSize[i].asymdir = hw_random16(255);
      // set advanced size control properties
      PartSys->advPartSize[i].grow = true;
      PartSys->advPartSize[i].growspeed = 1 + hw_random16(9);
      PartSys->advPartSize[i].shrinkspeed = 1 + hw_random16(9);
      PartSys->advPartSize[i].wobblespeed = 1 + hw_random16(3);
    }
    //PartSys->advPartSize[i].asymmetry++;
    PartSys->advPartSize[i].pulsate = seg.check3;
    PartSys->advPartSize[i].wobble = seg.check1;
  }
  seg.aux0 = seg.speed; //write state back
  seg.aux1 = seg.custom1;

  /* See the note on mode_particlespray. Upstream has no else branch here: with no
   * microphone the sizes are left to the advanced size control set in the loop
   * above, so Pulsate still pulses the particles on the size controller's own
   * grow and shrink cycle, it just stops tracking the volume. */
  if (seg.has_real_audio()) {  // get AR data if available, do not use simulated data
    AudioData &audio = seg.audio();
    uint8_t volumeSmth = (uint8_t)audio.volume_smth;
    for (uint32_t i = 0; i < PartSys->usedParticles; i++) { // update particles
      if (seg.check3) //pulsate selected
        PartSys->advPartProps[i].size = volumeSmth;
    }
  }

  PartSys->setMotionBlur(((seg.custom3) << 3) + 7);
  PartSys->update(); // update and render
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_GEQ_1D
/*
  Particle based 1D GEQ effect, each frequency bin gets an emitter, distributed over the strip
  Uses palette for particle color
  by DedeHai (Damian Schneider)
*/
void mode_particle1DGEQ(Segment &seg) {
  ParticleSystem1D *PartSys = nullptr;
  uint32_t numSources;
  uint32_t i;

  if (seg.call == 0) { // initialization
    if (!initParticleSystem1D(seg, PartSys, 16, 255, 0, true)) // init, no additional data needed
      FX_FALLBACK_STATIC; // allocation failed or is single pixel
  }
  else
    PartSys = reinterpret_cast<ParticleSystem1D *>(seg.data); // if not first call, just set the pointer to the PS
  if (PartSys == nullptr)
    FX_FALLBACK_STATIC; // something went wrong, no data!

  // Particle System settings
  PartSys->updateSystem(); // update system properties (dimensions and data pointers)
  numSources = PartSys->numSources;
  PartSys->setMotionBlur(seg.custom2); // anable motion blur

  uint32_t spacing = PartSys->maxX / numSources;
  for (i = 0; i < numSources; i++) {
    PartSys->sources[i].source.hue = i * 16; // hw_random16();   //TODO: make adjustable, maybe even colorcycle?
    PartSys->sources[i].var = seg.speed >> 2;
    PartSys->sources[i].minLife = 180 + (seg.intensity >> 1);
    PartSys->sources[i].maxLife = 240 + seg.intensity;
    PartSys->sources[i].sat = 255;
    PartSys->sources[i].size = seg.custom1;
    PartSys->sources[i].source.x = (spacing >> 1) + spacing * i; //distribute evenly
  }

  for (i = 0; i < PartSys->usedParticles; i++) {
    if (PartSys->particles[i].ttl > 20) PartSys->particles[i].ttl -= 20; //ttl is linked to brightness, this allows to use higher brightness but still a short lifespan
    else PartSys->particles[i].ttl = 0;
  }

  AudioData &audio = seg.audio();
  uint8_t *fftResult = audio.fft_result; // 16 bins with FFT data, log mapped already, each band contains frequency amplitude 0-255

  //map the bands into 16 positions on x axis, emit some particles according to frequency loudness
  i = 0;
  uint32_t bin = hw_random16(numSources); //current bin , start with random one to distribute available particles fairly
  uint32_t threshold = 300 - seg.intensity;

  for (i = 0; i < numSources; i++) {
    bin++;
    bin = bin % numSources;
    uint32_t emitparticle = 0;
    // uint8_t emitspeed = ((uint32_t)fftResult[bin] * (uint32_t)seg.speed) >> 10; // emit speed according to loudness of band (127 max!)
    if (fftResult[bin] > threshold) {
      emitparticle = 1;
    }
    else if (fftResult[bin] > 0) { // band has low volue
      uint32_t restvolume = ((threshold - fftResult[bin]) >> 2) + 2;
      if (hw_random() % restvolume == 0) {
        emitparticle = 1;
      }
    }

    if (emitparticle)
      PartSys->sprayEmit(PartSys->sources[bin]);
  }
  //TODO: add color control?

  PartSys->update(); // update and render
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_SONIC_STREAM
/*
  Particle based AR effect, swoop particles along the strip with selected frequency loudness
  by DedeHai (Damian Schneider)
*/
void mode_particle1DsonicStream(Segment &seg) {
  ParticleSystem1D *PartSys = nullptr;

  if (seg.call == 0) { // initialization
    if (!initParticleSystem1D(seg, PartSys, 1, 255, 0, true)) // init, no additional data needed
      FX_FALLBACK_STATIC; // allocation failed or is single pixel
    PartSys->setKillOutOfBounds(true);
    PartSys->sources[0].source.x = 0; // at start
    //PartSys->sources[1].source.x = PartSys->maxX; // at end
    PartSys->sources[0].var = 0;//seg.custom1 >> 3;
  }
  else
    PartSys = reinterpret_cast<ParticleSystem1D *>(seg.data); // if not first call, just set the pointer to the PS
  if (PartSys == nullptr)
    FX_FALLBACK_STATIC; // something went wrong, no data!

  // Particle System settings
  PartSys->updateSystem(); // update system properties (dimensions and data pointers)
  PartSys->setMotionBlur(20 + (seg.custom2 >> 1)); // anable motion blur
  PartSys->setSmearBlur(200); // smooth out the edges
  PartSys->sources[0].v = 5 + (seg.speed >> 2);

  // FFT processing
  AudioData &audio = seg.audio();
  uint8_t *fftResult = audio.fft_result; // 16 bins with FFT data, log mapped already, each band contains frequency amplitude 0-255
  uint32_t loudness;
  uint32_t baseBin = seg.custom3 >> 1; // 0 - 15 wf_map(seg.custom3, 0, 31, 0, 14);

  loudness = fftResult[baseBin];// + fftResult[baseBin + 1];
  int mids = 0;
  if (seg.check1) mids = sqrt32_bw((int)fftResult[5] + (int)fftResult[6] + (int)fftResult[7] + (int)fftResult[8] + (int)fftResult[9] + (int)fftResult[10]); // average the mids, bin 5 is ~500Hz, bin 10 is ~2kHz (see audio_reactive.h)
  if (baseBin > 12)
    loudness = loudness << 2; // double loudness for high frequencies (better detecion)

  uint32_t threshold = 140 - (seg.intensity >> 1);
  if (seg.check2) { // enable low pass filter for dynamic threshold
    seg.step = (seg.step * 31500 + loudness * (32768 - 31500)) >> 15; // low pass filter for simple beat detection: add average to base threshold
    threshold = 20 + (threshold >> 1) + seg.step; // add average to threshold
  }

  // color
  uint32_t hueincrement = (seg.custom1 >> 3); // 0-31
  PartSys->sources[0].sat = seg.custom1 > 0 ? 255 : 0; // color slider at zero: set to white
  PartSys->setColorByPosition(seg.custom1 == 255);

  // particle manipulation
  for (uint32_t i = 0; i < PartSys->usedParticles; i++) {
    if (PartSys->sources[0].sourceFlags.perpetual == false) { // age faster if not perpetual
      if (PartSys->particles[i].ttl > 2) {
        PartSys->particles[i].ttl -= 2; //ttl is linked to brightness, this allows to use higher brightness but still a short lifespan
      }
      else PartSys->particles[i].ttl = 0;
    }
    if (seg.check1) { // modulate colors by mid frequencies
      // x is signed and goes negative off the left edge, so upstream's left
      // shift is undefined there. Multiply by the same power of two.
      PartSys->particles[i].hue += (mids * perlin8(PartSys->particles[i].x * 4, seg.step << 2)) >> 9; // color by perlin noise from mid frequencies
    }
  }

  if (loudness > threshold) {
    seg.aux0 += hueincrement; // change color
    PartSys->sources[0].minLife = 100 + (((unsigned)seg.intensity * loudness * loudness) >> 13);
    PartSys->sources[0].maxLife = PartSys->sources[0].minLife;
    PartSys->sources[0].source.hue = seg.aux0;
    PartSys->sources[0].size = seg.speed;
    /* Deliberate deviation from upstream. seg.aux1 is an index into
     * PartSys->particles, carried across frames, and nothing upstream re-checks
     * it against usedParticles. usedParticles is recomputed from numParticles on
     * every init, and numParticles itself drops when the allocation retry loop in
     * initParticleSystem1D() halves it, so a shorter system can inherit an index
     * that is now out of range and the read below goes past the array. Clamping
     * costs one comparison and only ever changes which particle is checked for
     * spacing, which the next emit overwrites anyway. */
    if (seg.aux1 >= PartSys->usedParticles)
      seg.aux1 = 0;
    if (PartSys->particles[seg.aux1].x > 3 * PS_P_RADIUS_1D || PartSys->particles[seg.aux1].ttl == 0) { // only emit if last particle is far enough away or dead
      int partindex = PartSys->sprayEmit(PartSys->sources[0]); // emit a particle
      if (partindex >= 0) seg.aux1 = partindex; // track last emitted particle
    }
  }
  else loudness = 0; // required for push mode

  PartSys->update(); // update and render (needs to be done before manipulation for initial particle spacing to be right)

  if (seg.check3) { // push mode
    PartSys->sources[0].sourceFlags.perpetual = true; // emitted particles dont age
    PartSys->applyFriction(1); //slow down particles
    int32_t movestep = (((int)seg.speed + 2) * loudness) >> 10;
    if (movestep) {
      for (uint32_t i = 0; i < PartSys->usedParticles; i++) {
        if (PartSys->particles[i].ttl) {
          PartSys->particles[i].x += movestep; // push particles
          PartSys->particles[i].vx = 10 + (seg.speed >> 4) ; // give particles some speed for smooth movement (friction will slow them down)
        }
      }
    }
  }
  else {
    PartSys->sources[0].sourceFlags.perpetual = false; // emitted particles age
    // move all particles (again) to allow faster speeds
    for (uint32_t i = 0; i < PartSys->usedParticles; i++) {
      if (PartSys->particles[i].vx == 0)
        PartSys->particles[i].vx = PartSys->sources[0].v; // move static particles (after disabling push mode)
      PartSys->particleMoveUpdate(PartSys->particles[i], PartSys->particleFlags[i], nullptr, &PartSys->advPartProps[i]);
    }
  }
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_SONIC_BOOM
/*
  Particle based AR effect, creates exploding particles on beats
  by DedeHai (Damian Schneider)
*/
void mode_particle1DsonicBoom(Segment &seg) {
  ParticleSystem1D *PartSys = nullptr;
  if (seg.call == 0) { // initialization
    if (!initParticleSystem1D(seg, PartSys, 1, 255, 0, true)) // init, no additional data needed
      FX_FALLBACK_STATIC; // allocation failed or is single pixel
    PartSys->setKillOutOfBounds(true);
  }
  else
    PartSys = reinterpret_cast<ParticleSystem1D *>(seg.data); // if not first call, just set the pointer to the PS
  if (PartSys == nullptr)
    FX_FALLBACK_STATIC; // something went wrong, no data!

  // Particle System settings
  PartSys->updateSystem(); // update system properties (dimensions and data pointers)
  PartSys->setMotionBlur(180 * seg.check3);
  PartSys->setSmearBlur(64 * seg.check3);
  PartSys->sources[0].var = wf_map(seg.speed, 0, 255, 10, 127);

  // FFT processing
  AudioData &audio = seg.audio();
  uint8_t *fftResult = audio.fft_result; // 16 bins with FFT data, log mapped already, each band contains frequency amplitude 0-255
  uint32_t loudness;
  uint32_t baseBin = seg.custom3 >> 1; // 0 - 15 wf_map(seg.custom3, 0, 31, 0, 14);
  loudness = fftResult[baseBin];// + fftResult[baseBin + 1];
  int mids = 0;
  if (seg.check1) mids = sqrt32_bw((int)fftResult[5] + (int)fftResult[6] + (int)fftResult[7] + (int)fftResult[8] + (int)fftResult[9] + (int)fftResult[10]); // average the mids, bin 5 is ~500Hz, bin 10 is ~2kHz (see audio_reactive.h)

  if (baseBin > 12)
    loudness = loudness << 2; // double loudness for high frequencies (better detecion)
  uint32_t threshold = 150 - (seg.intensity >> 1);
  if (seg.check2) { // enable low pass filter for dynamic threshold
    seg.step = (seg.step * 31500 + loudness * (32768 - 31500)) >> 15; // low pass filter for simple beat detection: add average to base threshold
    threshold = 20 + (threshold >> 1) + seg.step; // add average to threshold
  }

  // particle manipulation
  for (uint32_t i = 0; i < PartSys->usedParticles; i++) {
    if (seg.check1) { // modulate colors by mid frequencies
      // x is signed and goes negative off the left edge, so upstream's left
      // shift is undefined there. Multiply by the same power of two.
      PartSys->particles[i].hue += (mids * perlin8(PartSys->particles[i].x * 4, seg.step << 2)) >> 9; // color by perlin noise from mid frequencies
    }
    if (PartSys->particles[i].ttl > 16) {
      PartSys->particles[i].ttl -= 16; //ttl is linked to brightness, this allows to use higher brightness but still a (very) short lifespan
    }
  }

  if (loudness > threshold) {
    if (seg.aux1 == 0) { // edge detected, code only runs once per "beat"
      // update position
      if (seg.custom2 < 128) // fixed position
        PartSys->sources[0].source.x = wf_map(seg.custom2, 0, 127, 0, PartSys->maxX);
      else if (seg.custom2 < 255) { // advances on each "beat"
        int32_t step = PartSys->maxX / (((270 - seg.custom2) >> 3)); // step: 2 - 33 steps for full segment width
        PartSys->sources[0].source.x = (PartSys->sources[0].source.x + step) % PartSys->maxX;
        if (PartSys->sources[0].source.x < step) // align to be symmetrical by making the first position half a step from start
          PartSys->sources[0].source.x = step >> 1;
      }
      else // position set to max, use random postion per beat
        PartSys->sources[0].source.x = hw_random(PartSys->maxX);

      // update color
      //PartSys->setColorByPosition(seg.custom1 == 255);     // color slider at max: particle color by position
      PartSys->sources[0].sat = seg.custom1 > 0 ? 255 : 0; // color slider at zero: set to white
      if (seg.custom1 == 255) // emit color by position
        seg.aux0 = wf_map(PartSys->sources[0].source.x , 0, PartSys->maxX, 0, 255);
      else if (seg.custom1 > 0)
        seg.aux0 += (seg.custom1 >> 1); // change emit color per "beat"
    }
    seg.aux1 = 1; // track edge detection

    PartSys->sources[0].minLife = 200;
    PartSys->sources[0].maxLife = PartSys->sources[0].minLife + (((unsigned)seg.intensity * loudness * loudness) >> 13);
    PartSys->sources[0].source.hue = seg.aux0;
    uint32_t explosionsize = 4 + (PartSys->maxXpixel >> 2);
    explosionsize = hw_random16((explosionsize * loudness) >> 10);
    for (uint32_t e = 0; e < explosionsize; e++) { // emit explosion particles
        PartSys->sprayEmit(PartSys->sources[0]); // emit a particle
      }
  }
  else
    seg.aux1 = 0; // reset edge detection

  PartSys->update(); // update and render (needs to be done before manipulation for initial particle spacing to be right)
}
#endif

#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_SPRINGY
/*
Particles bound by springs
by DedeHai (Damian Schneider)
*/
void mode_particleSpringy(Segment &seg) {
  ParticleSystem1D *PartSys = nullptr;
  if (seg.call == 0) { // initialization
    /* Upstream puts the spring force array on the stack as a variable length
     * array sized from usedParticles, which this port does not allow. It is asked
     * for as additional particle system bytes instead and read back from
     * PSdataEnd, which stays 4 byte aligned because numParticles is a multiple of
     * 4. The request uses the pre-retry particle count, an upper bound on what the
     * system ends up allocating, so the region is always large enough.
     *
     * It costs about 4 bytes per particle that upstream does not spend, so on a
     * board short of heap the retry loop can halve the particle count one step
     * further than upstream would, and the strip then shows fewer, more widely
     * spaced particles at the same Density setting. */
    if (!initParticleSystem1D(seg, PartSys, 1, 128, calculateNumberOfParticles1D(seg, 128, true) * sizeof(int), true)) // init with advanced properties (used for spring forces)
      FX_FALLBACK_STATIC; // allocation failed or is single pixel
    seg.aux0 = seg.aux1 = 0xFFFF; // invalidate settings
  }
  else
    PartSys = reinterpret_cast<ParticleSystem1D *>(seg.data); // if not first call, just set the pointer to the PS
  if (PartSys == nullptr)
    FX_FALLBACK_STATIC; // something went wrong, no data!
  // Particle System settings
  PartSys->updateSystem(); // update system properties (dimensions and data pointers)
  PartSys->setMotionBlur(220 * seg.check1); // anable motion blur
  PartSys->setSmearBlur(50); // smear a little
  PartSys->setUsedParticles(wf_map(seg.custom1, 0, 255, 30 >> seg.check2, 255  >> (seg.check2*2))); // depends on density and particle size
  //PartSys->enableParticleCollisions(true, 140); // enable particle collisions, can not be set too hard or impulses will not strech the springs if soft.
  int32_t springlength = PartSys->maxX / (PartSys->usedParticles); // spring length (spacing between particles)
  int32_t springK = wf_map(seg.speed, 0, 255, 5, 35); // spring constant (stiffness)

  uint32_t settingssum = seg.custom1 + seg.check2;
  PartSys->setParticleSize(seg.check2 ? 120 : 1); // large or small particles

  if (seg.aux0 != settingssum) { // number of particles changed, update distribution
    for (int32_t i = 0; i < (int32_t)PartSys->usedParticles; i++) {
      PartSys->advPartProps[i].sat = 255; // full saturation
      //PartSys->particleFlags[i].collide = true; // enable collision for particles -> results in chaos, removed for now
      PartSys->particles[i].x = (i+1) * ((PartSys->maxX) / (PartSys->usedParticles)); // distribute
      //PartSys->particles[i].vx = 0; //reset speed
      //PartSys->advPartProps[i].size = seg.check2 ? 190 : 2; // set size, small or big -> use global size
    }
    seg.aux0 = settingssum;
  }
  int dxlimit = (2 + ((255 - seg.speed) >> 5)) * springlength; // limit for spring length to avoid overstretching

  /* Spring forces, in the additional bytes asked for in the init branch. The
   * arithmetic checks out against allocateParticleSystemMemory1D(): the request
   * is numParticles ints for the pre-retry particle count, the retry loop only
   * ever halves numParticles, and setUsedParticles() caps usedParticles at
   * numParticles, so usedParticles ints always fit. Alignment holds because every
   * block updatePSpointers() walks past is a multiple of 4 bytes long, including
   * the 3 byte PSadvancedParticle1D array, whose length is a multiple of 4
   * particles. That is a long chain of invariants in another file, so the bound is
   * checked rather than trusted: updateSystem() also recomputes PSdataEnd from the
   * live canvas size on every frame while the allocation does not move. */
  static_assert(alignof(int) <= 4, "spring forces are carved from a 4 byte aligned region");
  int *springforce = reinterpret_cast<int *>(PartSys->PSdataEnd);
  /* The region fits exactly, with no slack, so the comparison has to be > and not
   * >=: one past the end is the correct end of the allocation. Nothing reaches
   * this today, and an effect cannot log once per frame, so it falls back to a
   * solid colour rather than say anything. */
  if (reinterpret_cast<uint8_t *>(springforce + PartSys->usedParticles) > seg.data + seg.data_size())
    FX_FALLBACK_STATIC; // spring force region does not fit, do not write past the allocation
  memset(springforce, 0, PartSys->usedParticles * sizeof(int)); // reset spring forces

  // calculate spring forces and limit particle positions
  if (PartSys->particles[0].x < -springlength)
    PartSys->particles[0].x = -springlength; // limit the spring length
  else if (PartSys->particles[0].x > dxlimit)
    PartSys->particles[0].x = dxlimit; // limit the spring length
  springforce[0] += ((springlength >> 1) - (PartSys->particles[0].x)) * springK; // first particle anchors to x=0

  for (uint32_t i = 1; i < PartSys->usedParticles; i++) {
    // reorder particles if they are out of order to prevent chaos
    if (PartSys->particles[i].x < PartSys->particles[i-1].x)
        std::swap(PartSys->particles[i].x, PartSys->particles[i-1].x); // swap particle positions to maintain order
    int dx = PartSys->particles[i].x - PartSys->particles[i-1].x; // distance, always positive
    if (dx > dxlimit) { // limit the spring length
      PartSys->particles[i].x = PartSys->particles[i-1].x + dxlimit;
      dx = dxlimit;
    }
    int dxleft = (springlength - dx); // offset from spring resting position
    springforce[i] += dxleft * springK;
    springforce[i-1] -= dxleft * springK;
    if (i == (PartSys->usedParticles - 1)) {
     if (PartSys->particles[i].x >= PartSys->maxX + springlength)
        PartSys->particles[i].x = PartSys->maxX + springlength;
      int dxright = (springlength >> 1) - (PartSys->maxX - PartSys->particles[i].x); // last particle anchors to x=maxX
      springforce[i] -= dxright * springK;
    }
  }
  // apply spring forces to particles
  bool dampenoscillations = (seg.call % (9 - (seg.speed >> 5))) == 0; // dampen oscillation if particles are slow, more damping on stiffer springs
  for (uint32_t i = 0; i < PartSys->usedParticles; i++) {
    springforce[i] = springforce[i] / 64; // scale spring force (cannot use shifts because of negative values)
    int maxforce = 120; // limit spring force
    springforce[i] = springforce[i] > maxforce ? maxforce : springforce[i] < -maxforce ? -maxforce : springforce[i]; // limit spring force
    PartSys->applyForce(PartSys->particles[i], springforce[i], PartSys->advPartProps[i].forcecounter);
    //dampen slow particles to avoid persisting oscillations on higher stiffness
    if (dampenoscillations) {
      if (std::abs(PartSys->particles[i].vx) < 3 && std::abs(springforce[i]) < (springK >> 2))
        PartSys->particles[i].vx = (PartSys->particles[i].vx * 254) / 256; // take out some energy
    }
    PartSys->particles[i].ttl = 300; // reset ttl, cannot use perpetual
  }

  if (seg.call % ((65 - ((seg.intensity * (1 + (seg.speed>>3))) >> 7))) == 0) // more damping for higher stiffness
    PartSys->applyFriction((seg.intensity >> 2));

  // add a small resetting force so particles return to resting position even under high damping
  for (uint32_t i = 1; i < PartSys->usedParticles - 1; i++) {
    int restposition = (springlength >> 1) + i * springlength; // resting position
    int dx = restposition - PartSys->particles[i].x; // distance, always positive
    PartSys->applyForce(PartSys->particles[i], dx > 0 ? 1 : (dx < 0 ? -1 : 0), PartSys->advPartProps[i].forcecounter);
  }

  // Modes
  if (seg.check3) { // use AR, custom 3 becomes frequency band to use, applies velocity to center particle according to loudness
    AudioData &audio = seg.audio();
    uint8_t *fftResult = audio.fft_result; // 16 bins with FFT data, log mapped already, each band contains frequency amplitude 0-255
    uint32_t baseBin = wf_map(seg.custom3, 0, 31, 0, 14);
    uint32_t loudness = fftResult[baseBin] + fftResult[baseBin+1];
    uint32_t threshold = 80; //150 - (seg.intensity >> 1);
    if (loudness > threshold) {
        int offset = (PartSys->maxX >> 1) - PartSys->particles[PartSys->usedParticles>>1].x; // offset from center
        if (std::abs(offset) < PartSys->maxX >> 5) // push particle around in center sector
          PartSys->particles[PartSys->usedParticles>>1].vx = ((PartSys->particles[PartSys->usedParticles>>1].vx > 0 ? 1 : -1)) * (loudness >> 3);
    }
  }
  else{
    if (seg.custom3 <= 10) { // periodic pulse: 0-5 apply at start, 6-10 apply at center
      if (seg.now > seg.step) {
        int speed = (seg.custom3 > 5) ? (seg.custom3 - 6) : seg.custom3;
        seg.step = seg.now + 7500 - ((seg.speed << 3) + (speed << 10));
        int amplitude = 40 + (seg.custom1 >> 2);
        int index = (seg.custom3 > 5) ? (PartSys->usedParticles / 2) : 0; // center or start particle
        PartSys->particles[index].vx += amplitude;
      }
    }
    else if (seg.custom3 <= 30) { // sinusoidal wave: 11-20 apply at start, 21-30 apply at center
      int index = (seg.custom3 > 20) ? (PartSys->usedParticles / 2) : 0; // center or start particle
      int restposition = 0;
      if (index > 0) restposition = PartSys->maxX >> 1; // center
      //int amplitude = 5 + (seg.speed >> 3) + (seg.custom1 >> 2); // amplitude depends on density
      int amplitude = 5 + (seg.custom1 >> 2); // amplitude depends on density
      int speed = seg.custom3 - 10 - (index ? 10 : 0); // map 11-20 and 21-30 to 1-10
      int phase = seg.now * ((1 + (seg.speed >> 4)) * speed);
      if (seg.check2) amplitude <<= 1; // double amplitude for XL particles
      PartSys->particles[index].x = restposition + ((sin16_t(phase) * amplitude) >> 12); // apply position
    }
    else {
      if (hw_random16() < 656) { // ~1% chance to add a pulse
        int amplitude = 60;
        if (seg.check2) amplitude <<= 1; // double amplitude for XL particles
        PartSys->particles[PartSys->usedParticles >> 1].vx += hw_random16(amplitude << 1) - amplitude; // apply acceleration
      }
    }
  }

  for (uint32_t i = 0; i < PartSys->usedParticles; i++) {
    if (seg.custom2 == 255) { // map speed to hue
       int speedclr = ((int8_t(std::abs(PartSys->particles[i].vx))) >> 2) << 4; // scale for greater color variation, dump small values to avoid flickering
       //int speed = PartSys->particles[i].vx << 2; // +/- 512
       if (speedclr > 240) speedclr = 240; // limit color to non-wrapping part of palette
       PartSys->particles[i].hue = speedclr;
    }
    else if (seg.custom2 > 0)
      PartSys->particles[i].hue = i * (seg.custom2 >> 2); // gradient distribution
    else {
      // map hue to particle density
      int deviation;
      if (i == 0) // First particle: measure density based on distance to anchor point
        deviation = springlength/2 - PartSys->particles[i].x;
      else if (i == PartSys->usedParticles - 1) // Last particle: measure density based on distance to right boundary
        deviation = springlength/2 - (PartSys->maxX - PartSys->particles[i].x);
      else {
        // Middle particles: average of compression/expansion from both sides
        int leftDx = PartSys->particles[i].x - PartSys->particles[i-1].x;
        int rightDx = PartSys->particles[i+1].x - PartSys->particles[i].x;
        int avgDistance = (leftDx + rightDx) >> 1;
        if (avgDistance < 0) avgDistance = 0; // avoid negative distances (not sure why this happens)
        deviation = (springlength - avgDistance);
      }
      deviation = constrain(deviation, -127, 112); // limit deviation to -127..112 (do not go intwo wrapping part of palette)
      PartSys->particles[i].hue = 127 + deviation; // map density to hue
    }
  }
  PartSys->update(); // update and render
}
#endif

const EffectInfo ENTRIES[] = {
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_SPRAY
    {"PS Spray@Speed,!,Left/Right,Up/Down,Angle,Gravity,Cylinder/Square,Collide;;!;2v;pal=0,sx=150,ix=150,c1=220,c2=30,"
     "c3=21",
     mode_particlespray},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_GEQ_2D
    {"PS GEQ 2D@Speed,Intensity,Diverge,Bounce,Gravity,Cylinder,Walls,Floor;;!;2f;pal=0,sx=155,ix=200,c1=0",
     mode_particleGEQ},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_GEQ_NOVA
    {"PS GEQ Nova@Speed,Intensity,Rotation Speed,Color Change,Nozzle,,Direction;;!;2f;pal=13,ix=180,c1=0,c2=0,c3=8",
     mode_particlecenterGEQ},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_BLOBS
    {"PS Blobs@Speed,Blobs,Size,Life,Blur,Wobble,Collide,Pulsate;;!;2v;sx=30,ix=64,c1=200,c2=130,c3=0,o3=1",
     mode_particleblobs},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_GEQ_1D
    {"PS GEQ 1D@Speed,!,Size,Blur,,,,;,!;!;1f;pal=0,sx=50,ix=200,c1=0,c2=0,c3=0,o1=1,o2=1", mode_particle1DGEQ},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_SONIC_STREAM
    {"PS Sonic Stream@!,!,Color,Blur,Bin,Mod,Filter,Push;,!;!;1f;c3=0,o2=1", mode_particle1DsonicStream},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_SONIC_BOOM
    {"PS Sonic Boom@!,!,Color,Position,Bin,Mod,Filter,Blur;,!;!;1f;c2=63,c3=0,o2=1", mode_particle1DsonicBoom},
#endif
#if WLED_FX_DEFAULT_ENABLE || WLED_FX_FX_PS_SPRINGY
    {"PS Springy@Stiffness,Damping,Density,Hue,Mode,Smear,XL,AR;,!;!;1f;pal=54,c2=0,c3=23", mode_particleSpringy},
#endif
};
constexpr size_t ENTRY_COUNT = sizeof(ENTRIES) / sizeof(ENTRIES[0]);

}  // namespace

// extern first: a bare `const` at namespace scope has internal linkage and the
// generated group table would not be able to see it.
extern const EffectGroup EFFECT_GROUP_AUDIO_PARTICLE;
const EffectGroup EFFECT_GROUP_AUDIO_PARTICLE{"audio_particle", ENTRIES, ENTRY_COUNT};

}  // namespace wled_fx
}  // namespace esphome

#endif  // WLED_FX_GROUP_AUDIO_PARTICLE
