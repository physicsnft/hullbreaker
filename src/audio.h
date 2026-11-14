// Audio: a small software synthesizer. Nothing is loaded from disk; sound effects are
// synthesized into buffers at start-up and the music is composed while it plays.
#pragma once
#include "sfx.h"
#include <cstdint>
#include <memory>

class Audio {
 public:
  Audio();
  ~Audio();
  // Opens the default output device. With `device == false` the synth still works and can be
  // rendered offline with render() (used for testing and --render-audio).
  bool init(bool device);
  void shutdown();
  bool hasDevice() const;

  void play(int sfx, float gain = 1.0f, float pan = 0.0f, float pitch = 1.0f);
  void setEngine(float thrust, float boost); // 0..1 each; drives the engine rumble
  // Start a new piece. The seed picks key, mode, tempo, chords and patterns.
  void newSong(uint32_t seed);
  // 0 = drifting ambience, 1 = full combat. The arrangement follows this smoothly.
  void setIntensity(float intensity);
  void setMusicVolume(float v);
  void setSfxVolume(float v);
  float musicVolume() const;
  float sfxVolume() const;
  const char* songInfo() const; // e.g. "D DORIAN 104 BPM"

  // Fills `frames` interleaved stereo float samples at 48 kHz.
  void render(float* out, int frames);

 private:
  struct Impl;
  std::unique_ptr<Impl> d;
};

// Renders `seconds` of music plus a run of every sound effect to a 16-bit stereo WAV file.
int renderAudioToWav(const char* path, float seconds, uint32_t seed);
