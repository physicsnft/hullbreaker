#include "audio.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#include "miniaudio.h"

namespace {

const float SR = 48000.0f;
const float TAU = 6.28318530718f;

inline float clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }
inline float sstep(float a, float b, float x) {
  float t = clamp01((x - a) / (b - a));
  return t * t * (3 - 2 * t);
}
inline float mtof(float midi) { return 440.0f * std::pow(2.0f, (midi - 69.0f) / 12.0f); }

struct Lcg {
  uint32_t s = 12345;
  uint32_t u32() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
  float uni() { return (float)(u32() >> 8) * (1.0f / 16777216.0f); }
  float bi() { return uni() * 2 - 1; }
  int range(int a, int b) { return a + (int)(u32() % (uint32_t)(b - a + 1)); }
  bool chance(float p) { return uni() < p; }
};

struct OnePole {
  float z = 0;
  float lp(float x, float fc) { z += (1.0f - std::exp(-TAU * fc / SR)) * (x - z); return z; }
  float lpa(float x, float a) { z += a * (x - z); return z; }
};
// state-variable low-pass (trapezoidal), stable under fast cutoff changes
struct SVF {
  float ic1 = 0, ic2 = 0;
  float lp(float x, float g, float k) {
    float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
    float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2 * v1 - ic1;
    ic2 = 2 * v2 - ic2;
    return v2;
  }
};
inline float svfG(float fc) { return std::tan(3.14159265f * (fc < 20 ? 20 : (fc > 16000 ? 16000 : fc)) / SR); }

inline float polyblep(float t, float dt) {
  if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
  if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
  return 0.0f;
}
struct Saw {
  float ph = 0;
  float next(float freq) {
    float dt = freq / SR;
    float v = 2.0f * ph - 1.0f - polyblep(ph, dt);
    ph += dt;
    if (ph >= 1.0f) ph -= 1.0f;
    return v;
  }
};
struct Sine {
  float ph = 0;
  float next(float freq) {
    float v = std::sin(ph * TAU);
    ph += freq / SR;
    if (ph >= 1.0f) ph -= 1.0f;
    return v;
  }
};

// =====================================================================================
// Sound effects: each is synthesized once into a mono buffer
// =====================================================================================
typedef std::vector<float> Buf;

template <class F>
Buf synth(float dur, F f) {
  int n = (int)(dur * SR);
  Buf b((size_t)n);
  for (int i = 0; i < n; ++i) b[(size_t)i] = f((float)i / SR);
  // short fade-out so nothing ends with a click, then normalise
  int fade = std::min(n, 480);
  for (int i = 0; i < fade; ++i) b[(size_t)(n - 1 - i)] *= (float)i / (float)fade;
  float peak = 1e-6f;
  for (float v : b) peak = std::max(peak, std::fabs(v));
  for (float& v : b) v *= 0.9f / peak;
  return b;
}
// sum of decaying inharmonic partials: the sound of struck metal
struct Metal {
  const float* f; const float* d; int n; float ph[8] = {0};
  float next(float t, float pitch = 1.0f) {
    float s = 0;
    for (int i = 0; i < n; ++i) {
      s += std::sin(ph[i] * TAU) * std::exp(-t * d[i]) / (1.0f + i * 0.35f);
      ph[i] += f[i] * pitch / SR;
      if (ph[i] >= 1) ph[i] -= 1;
    }
    return s;
  }
};

void buildSfx(Buf* out, float* baseGain) {
  Lcg rng;
  for (int i = 0; i < SFX_COUNT; ++i) baseGain[i] = 0.5f;

  { // pulse laser: a hard energy discharge, not a "pew": gritty falling body, crack, sub thump
    Saw a, b; Sine sub; SVF f; OnePole hp;
    out[SFX_PULSE] = synth(0.34f, [&](float t) {
      float fr = 92.0f + 640.0f * std::exp(-t * 30.0f);
      float s = f.lp(a.next(fr) + b.next(fr * 1.007f) * 0.8f, svfG(600.0f + 5200.0f * std::exp(-t * 18.0f)), 0.4f);
      float n = rng.bi();
      float crack = (n - hp.lp(n, 1800)) * std::exp(-t * 70.0f);
      float thump = sub.next(50.0f + 60.0f * std::exp(-t * 40.0f)) * std::exp(-t * 13.0f);
      return std::tanh((s * std::exp(-t * 13.0f) * 1.2f + crack * 0.9f + thump * 1.4f) * 2.2f);
    });
    baseGain[SFX_PULSE] = 0.44f;
  }
  { // gatling: autocannon report with a chest thump
    OnePole lp, lp2; Sine th, th2;
    out[SFX_GATLING] = synth(0.22f, [&](float t) {
      float n = rng.bi();
      float crack = lp.lp(n, 2600) * std::exp(-t * 38.0f);
      float thump = th.next(46.0f + 95.0f * std::exp(-t * 55.0f)) * std::exp(-t * 19.0f) * 1.7f;
      float body = th2.next(138.0f) * std::exp(-t * 42.0f) * 0.5f;
      float tail = lp2.lp(n, 480) * std::exp(-t * 13.0f) * 0.5f;
      return std::tanh((crack * 2.0f + thump * 0.8f + body + tail) * 2.5f);
    });
    baseGain[SFX_GATLING] = 0.44f;
  }
  { // ion blaster: low electric growl with crackle
    float cp = 0, mp = 0; Sine sub; OnePole lp;
    out[SFX_ION] = synth(0.46f, [&](float t) {
      float fc = 68.0f + 190.0f * std::exp(-t * 9.0f);
      float env = std::min(t / 0.004f, 1.0f) * std::exp(-t * 8.0f);
      mp += fc * 1.41f / SR;
      cp += fc / SR;
      float s = std::sin(cp * TAU + (5.0f + 6.0f * env) * std::sin(mp * TAU));
      float crackle = lp.lp(rng.bi(), 3000) * (rng.chance(0.02f) ? 1.0f : 0.15f) * std::exp(-t * 10.0f);
      float thump = sub.next(44.0f) * std::exp(-t * 12.0f);
      return std::tanh((s * env + crackle * 0.6f + thump * 0.9f) * 1.9f);
    });
    baseGain[SFX_ION] = 0.44f;
  }
  { // railgun: supersonic crack, falling body, long rolling tail
    OnePole hp, lp; Sine body, sub;
    out[SFX_RAIL] = synth(1.5f, [&](float t) {
      float n = rng.bi();
      float crack = (n - hp.lp(n, 2200)) * std::exp(-t * 70.0f);
      float b = body.next(42.0f + 900.0f * std::exp(-t * 24.0f)) * std::exp(-t * 6.0f);
      float tail = lp.lp(n, 420) * std::exp(-t * 3.2f) * 1.1f;
      float s = sub.next(36.0f + 20.0f * std::exp(-t * 8.0f)) * std::exp(-t * 3.6f) * 1.2f;
      return std::tanh((crack * 1.2f + b * 1.2f + tail + s) * 2.2f);
    });
    baseGain[SFX_RAIL] = 0.82f;
  }
  { // plasma cannon: deep wet whoomp
    Saw a, b; SVF f; OnePole lp; Sine sub;
    out[SFX_PLASMA] = synth(0.95f, [&](float t) {
      float fr = 36.0f + 110.0f * std::exp(-t * 6.0f);
      float s = a.next(fr) + b.next(fr * 1.51f) * 0.6f;
      float env = std::min(t / 0.012f, 1.0f) * std::exp(-t * 4.2f);
      float y = f.lp(s, svfG(180.0f + 2200.0f * std::exp(-t * 9.0f)), 0.5f) * (0.75f + 0.25f * std::sin(t * TAU * 27.0f));
      float th = sub.next(40.0f + 40.0f * std::exp(-t * 20.0f)) * std::exp(-t * 6.0f);
      return std::tanh((y * env * 1.6f + lp.lp(rng.bi(), 1500) * 0.5f * std::exp(-t * 11.0f) + th) * 2.2f);
    });
    baseGain[SFX_PLASMA] = 0.70f;
  }
  { // missile launch: ignition thud, then rocket roar
    SVF f; OnePole lp; Sine rum, th;
    out[SFX_MISSILE] = synth(1.2f, [&](float t) {
      float n = rng.bi();
      float w = f.lp(n, svfG(350.0f + 1900.0f * sstep(0.0f, 0.5f, t)), 0.35f) - lp.lp(n, 120);
      float env = std::min(t / 0.03f, 1.0f) * std::exp(-t * 3.0f);
      float thud = th.next(40.0f + 70.0f * std::exp(-t * 30.0f)) * std::exp(-t * 9.0f) * 1.4f;
      return std::tanh(((w * 0.9f + rum.next(52.0f) * 0.7f) * env + thud) * 1.8f);
    });
    baseGain[SFX_MISSILE] = 0.62f;
  }
  { // shield hit: heavy electric thud with a low ringing field
    static const float fr[] = {196, 293, 441, 662, 990}, dc[] = {10, 12, 15, 19, 25};
    Metal m{fr, dc, 5}; Sine hum; OnePole lp, hp;
    out[SFX_SHIELD_HIT] = synth(0.6f, [&](float t) {
      float n = rng.bi();
      float buzz = (lp.lp(n, 2400) - hp.lp(n, 300)) * std::exp(-t * 22.0f) * (0.6f + 0.4f * std::sin(t * TAU * 90.0f));
      float thump = hum.next(56.0f + 50.0f * std::exp(-t * 30.0f)) * std::exp(-t * 10.0f) * 1.4f;
      return std::tanh((m.next(t) * 0.6f + buzz + thump) * 1.9f);
    });
    baseGain[SFX_SHIELD_HIT] = 0.52f;
  }
  { // hull hit: a slug slamming into thick plating
    static const float fr[] = {137, 229, 373, 601, 977, 1580}, dc[] = {11, 13, 16, 20, 27, 36};
    Metal m{fr, dc, 6}; Sine th; OnePole lp;
    out[SFX_HULL_HIT] = synth(0.55f, [&](float t) {
      float thump = th.next(38.0f + 90.0f * std::exp(-t * 35.0f)) * std::exp(-t * 11.0f) * 1.7f;
      float n = lp.lp(rng.bi(), 1800) * std::exp(-t * 30.0f);
      return std::tanh((m.next(t) * 1.0f + thump * 0.85f + n * 1.3f) * 2.1f);
    });
    baseGain[SFX_HULL_HIT] = 0.66f;
  }
  { // heavy hull hit: the whole frame rings
    static const float fr[] = {82, 139, 227, 371, 603, 980}, dc[] = {4, 5, 7, 9, 13, 18};
    Metal m{fr, dc, 6}; Sine th; OnePole lp, lp2;
    out[SFX_HULL_HEAVY] = synth(1.4f, [&](float t) {
      float n = rng.bi();
      float burst = lp.lp(n, 900) * std::exp(-t * 9.0f);
      float rumble = lp2.lp(n, 140) * std::exp(-t * 3.0f) * 3.0f;
      float thump = th.next(27.0f + 70.0f * std::exp(-t * 22.0f)) * std::exp(-t * 4.5f) * 1.9f;
      return std::tanh((m.next(t) * 0.7f + thump + burst + rumble) * 2.2f);
    });
    baseGain[SFX_HULL_HEAVY] = 1.0f;
  }
  { // part torn off: crunching metal and a small blast
    static const float fr[] = {220, 367, 590, 911, 1480}, dc[] = {9, 11, 14, 19, 25};
    Metal m{fr, dc, 5}; SVF f; Sine th; float grain = 0; int hold = 0;
    out[SFX_PART_LOST] = synth(0.9f, [&](float t) {
      if (--hold <= 0) { hold = rng.range(120, 900); grain = rng.uni(); }
      float n = f.lp(rng.bi(), svfG(300.0f + 2400.0f * std::exp(-t * 5.0f)), 0.6f) * (0.3f + 0.7f * grain);
      float boom = th.next(40.0f + 60.0f * std::exp(-t * 20.0f)) * std::exp(-t * 7.0f);
      return std::tanh((n * std::exp(-t * 4.5f) * 1.2f + m.next(t) * 0.5f + boom) * 1.4f);
    });
    baseGain[SFX_PART_LOST] = 0.62f;
  }
  auto explosion = [&](float dur, float decay, float subHz, float gain, int id) {
    SVF f; Sine sub; OnePole lp2; float crack = 0; int hold = 0;
    out[id] = synth(dur, [&](float t) {
      float n = rng.bi();
      float cut = 140.0f + 3200.0f * std::exp(-t * decay * 1.6f);
      float body = f.lp(n, svfG(cut), 0.7f) * std::exp(-t * decay);
      if (--hold <= 0) { hold = rng.range(400, 5000); crack = rng.chance(0.4f) ? rng.uni() : 0.0f; }
      float crackle = lp2.lp(n, 2500) * crack * std::exp(-t * decay * 0.6f) * 0.35f * sstep(0.1f, 0.3f, t);
      float s = sub.next(subHz * (0.55f + 0.45f * std::exp(-t * 6.0f))) * std::exp(-t * decay * 0.9f);
      return std::tanh((body * 1.5f + crackle + s * 1.2f) * 1.8f);
    });
    baseGain[id] = gain;
  };
  explosion(1.8f, 2.6f, 50.0f, 0.85f, SFX_EXPLOSION);
  explosion(3.0f, 1.4f, 38.0f, 1.1f, SFX_EXPLOSION_BIG);
  { // round hitting rock
    OnePole lp; Sine th;
    out[SFX_ROCK_HIT] = synth(0.2f, [&](float t) {
      return lp.lp(rng.bi(), 1300) * std::exp(-t * 28.0f) + th.next(110.0f) * std::exp(-t * 35.0f) * 0.6f;
    });
    baseGain[SFX_ROCK_HIT] = 0.30f;
  }
  { // collision: big thud and scrape
    static const float fr[] = {131, 211, 347, 560, 905}, dc[] = {6, 8, 10, 14, 20};
    Metal m{fr, dc, 5}; Sine th; SVF f;
    out[SFX_COLLISION] = synth(0.9f, [&](float t) {
      float scrape = f.lp(rng.bi(), svfG(900.0f + 500.0f * std::sin(t * 40.0f)), 0.4f) * std::exp(-t * 6.0f) * 0.7f;
      float thump = th.next(30.0f + 60.0f * std::exp(-t * 25.0f)) * std::exp(-t * 6.0f) * 1.5f;
      return std::tanh((m.next(t) * 0.6f + thump + scrape) * 1.6f);
    });
    baseGain[SFX_COLLISION] = 0.75f;
  }
  { // shields down: power-down sweep
    float ph = 0;
    out[SFX_SHIELD_DOWN] = synth(0.8f, [&](float t) {
      float f = 110.0f + 620.0f * std::exp(-t * 4.5f);
      ph += f / SR;
      float s = std::sin(ph * TAU) + 0.4f * std::sin(ph * TAU * 3.0f) + 0.2f * std::sin(ph * TAU * 5.0f);
      return s * std::exp(-t * 3.0f) * (0.7f + 0.3f * std::sin(t * TAU * 22.0f));
    });
    baseGain[SFX_SHIELD_DOWN] = 0.36f;
  }
  { // misfire: dry click and fizz
    OnePole hp;
    out[SFX_MISFIRE] = synth(0.22f, [&](float t) {
      float n = rng.bi();
      return (n - hp.lp(n, 3500)) * (std::exp(-t * 90.0f) + 0.35f * std::exp(-t * 14.0f));
    });
    baseGain[SFX_MISFIRE] = 0.30f;
  }
  { // alarm: two beeps
    float ph = 0;
    out[SFX_ALARM] = synth(0.46f, [&](float t) {
      ph += 932.0f / SR;
      float s = std::sin(ph * TAU) + 0.33f * std::sin(ph * TAU * 3.0f);
      float gate = (t < 0.14f || (t > 0.22f && t < 0.36f)) ? 1.0f : 0.0f;
      float tt = t < 0.2f ? t : t - 0.22f;
      return s * gate * std::min(tt / 0.005f, 1.0f) * std::min((0.14f - tt) / 0.01f, 1.0f);
    });
    baseGain[SFX_ALARM] = 0.22f;
  }
  { Sine a; out[SFX_UI_MOVE] = synth(0.05f, [&](float t) { return a.next(1250.0f) * std::exp(-t * 70.0f); }); baseGain[SFX_UI_MOVE] = 0.16f; }
  { Sine a; out[SFX_UI_SELECT] = synth(0.2f, [&](float t) {
      return a.next(t < 0.07f ? 660.0f : 990.0f) * std::exp(-(t < 0.07f ? t : t - 0.07f) * 22.0f);
    });
    baseGain[SFX_UI_SELECT] = 0.22f;
  }
  { // launch: catapult whoosh
    SVF f; Saw s;
    out[SFX_LAUNCH] = synth(1.8f, [&](float t) {
      float up = sstep(0.0f, 1.1f, t);
      float n = f.lp(rng.bi(), svfG(200.0f + 3800.0f * up * up), 0.4f);
      float tone = s.next(55.0f + 260.0f * up) * 0.25f;
      return (n + tone) * std::min(t / 0.2f, 1.0f) * std::exp(-std::max(t - 1.1f, 0.0f) * 5.0f);
    });
    baseGain[SFX_LAUNCH] = 0.45f;
  }
}

// =====================================================================================
// Reverb and delay
// =====================================================================================
struct DelayLine {
  std::vector<float> b; size_t w = 0;
  void init(size_t n) { b.assign(n, 0.0f); w = 0; }
  float tap(size_t d) const { return b[(w + b.size() - d) % b.size()]; }
  void push(float x) { b[w] = x; w = (w + 1) % b.size(); }
};
struct Reverb { // four damped combs and two all-passes per channel
  DelayLine comb[2][4], ap[2][2];
  float damp[2][4] = {{0}};
  void init() {
    static const int cl[4] = {1687, 1931, 2053, 2251}, al[2] = {347, 113};
    for (int c = 0; c < 2; ++c) {
      for (int i = 0; i < 4; ++i) comb[c][i].init((size_t)(cl[i] + c * 37));
      for (int i = 0; i < 2; ++i) ap[c][i].init((size_t)(al[i] + c * 11));
    }
  }
  void process(float inL, float inR, float& outL, float& outR) {
    float in[2] = {inL, inR}, out[2];
    for (int c = 0; c < 2; ++c) {
      float s = 0;
      for (int i = 0; i < 4; ++i) {
        DelayLine& d = comb[c][i];
        float y = d.tap(d.b.size() - 1);
        damp[c][i] += 0.35f * (y - damp[c][i]);
        d.push(in[c] + damp[c][i] * 0.86f);
        s += y;
      }
      s *= 0.25f;
      for (int i = 0; i < 2; ++i) {
        DelayLine& d = ap[c][i];
        float y = d.tap(d.b.size() - 1);
        float x = s + y * 0.5f;
        d.push(x);
        s = y - x * 0.5f;
      }
      out[c] = s;
    }
    outL = out[0];
    outR = out[1];
  }
};

// =====================================================================================
// Generative music
// =====================================================================================
const int kScales[4][7] = {{0, 2, 3, 5, 7, 8, 10}, {0, 2, 3, 5, 7, 9, 10}, {0, 1, 3, 5, 7, 8, 10}, {0, 2, 4, 6, 7, 9, 11}};
const char* const kScaleNames[4] = {"MINOR", "DORIAN", "PHRYGIAN", "LYDIAN"};
const char* const kNoteNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
const int kProgs[8][4] = {{0, 5, 2, 6}, {0, 3, 5, 4}, {0, 6, 5, 6}, {0, 2, 6, 3}, {0, 5, 3, 4}, {0, 0, 5, 6}, {0, 3, 0, 4}, {5, 6, 0, 0}};

struct Music {
  Lcg rng;
  // composition
  float bpm = 100; int root = 38, scale = 0; int progA = 0, progB = 1;
  int arp[16], bass[16]; uint16_t kick = 0, snare = 0, hat = 0;
  float leadDensity = 0.3f; int arpOct = 0;
  char info[48] = "";
  // sequencer
  double samplesToStep = 0; int step = 0, bar = 0;
  int chord[4] = {0, 2, 4, 6};
  // voices
  Saw padSaw[4][2]; float padFreq[4] = {110, 110, 110, 110}, padTarget[4] = {110, 110, 110, 110}; SVF padF[2];
  float padSwell = 0;
  Saw bassSaw; Sine bassSub; SVF bassF; float bassFreq = 55, bassEnv = 0, bassGate = 0, bassCut = 0;
  Saw arpSaw; SVF arpF; float arpFreq = 220, arpEnv = 0, arpPan = 0;
  Sine leadA, leadB; float leadFreq = 440, leadTarget = 440, leadEnv = 0, leadGate = 0; int leadDeg = 9, leadLeft = 0; float vib = 0;
  float kickT = 1, snareT = 1, hatT = 1, hatDecay = 70; Sine kickOsc, snareOsc; OnePole hatHp, snareHp; Lcg noise;
  DelayLine dl, dr; OnePole dlp, drp; Reverb rev;
  float lfo = 0;

  void init() {
    dl.init(48000); dr.init(48000);
    rev.init();
  }
  int degToMidi(int deg, int octaveBase) const {
    int o = deg >= 0 ? deg / 7 : -((6 - deg) / 7);
    int d = deg - o * 7;
    return octaveBase + root % 12 + kScales[scale][d] + 12 * o;
  }
  void rollArp() {
    int style = rng.range(0, 3);
    for (int i = 0; i < 16; ++i) {
      int v;
      if (style == 0) v = i % 4;                              // up
      else if (style == 1) { static const int ud[6] = {0, 1, 2, 3, 2, 1}; v = ud[i % 6]; } // up-down
      else if (style == 2) { static const int p[8] = {0, 2, 1, 3, 0, 3, 1, 2}; v = p[i % 8]; }
      else v = rng.range(0, 3);
      if (rng.chance(0.25f)) v += 4;                           // jump an octave
      arp[i] = rng.chance(0.14f) ? -1 : v;
    }
  }
  void rollBass() {
    for (int i = 0; i < 16; ++i) bass[i] = 0;
    int style = rng.range(0, 2);
    for (int i = 0; i < 16; i += 2) {
      if (style == 0) bass[i] = 1;                             // driving eighths
      else if (style == 1) bass[i] = (i % 8 == 6) ? 2 : (i % 4 == 0 ? 1 : (rng.chance(0.5f) ? 1 : 0));
      else bass[i] = (i == 0 || i == 6 || i == 10) ? 1 : (i == 14 ? 3 : 0); // syncopated
    }
    if (rng.chance(0.4f)) bass[rng.range(0, 7) * 2 + 1] = 2;
  }
  void rollDrums() {
    static const uint16_t kicks[4] = {0x1111, 0x0511, 0x1105, 0x4111};
    kick = kicks[rng.range(0, 3)];
    snare = rng.chance(0.75f) ? 0x1010 : 0x9010;
    hat = rng.chance(0.5f) ? 0x4444 : (rng.chance(0.5f) ? 0x5555 : 0xDDDD);
  }
  void compose(uint32_t seed) {
    rng.s = seed * 2654435761u + 977u;
    if (rng.s == 0) rng.s = 1;
    for (int i = 0; i < 8; ++i) rng.u32();
    bpm = (float)rng.range(86, 122);
    root = rng.range(0, 11);
    scale = rng.chance(0.4f) ? 0 : rng.range(0, 3);
    progA = rng.range(0, 7);
    do progB = rng.range(0, 7); while (progB == progA);
    leadDensity = 0.2f + 0.25f * rng.uni();
    arpOct = rng.range(0, 1);
    rollArp(); rollBass(); rollDrums();
    std::snprintf(info, sizeof(info), "%s %s  %d BPM", kNoteNames[root], kScaleNames[scale], (int)bpm);
    step = 0; bar = 0; samplesToStep = 0;
    leadDeg = 7 + rng.range(0, 6);
    setChord(kProgs[progA][0]);
    for (int i = 0; i < 4; ++i) padFreq[i] = padTarget[i];
  }
  void setChord(int degree) {
    for (int i = 0; i < 4; ++i) chord[i] = degree + 2 * i;
    // pad voicing: keep it in one octave around middle C so chords move smoothly
    for (int i = 0; i < 4; ++i) {
      int m = degToMidi(chord[i] % 7, 48);
      if (i == 0 && m > 55) m -= 12;
      padTarget[i] = mtof((float)m);
    }
    padSwell = 0.55f;
  }
  void onStep(float I) {
    if (step == 0) {
      int section = (bar / 8) % 4; // A A B A
      const int* prog = kProgs[section == 2 ? progB : progA];
      setChord(prog[(bar / (I < 0.3f ? 2 : 1)) % 4]);
      if (bar % 8 == 0 && bar > 0) {
        if (rng.chance(0.6f)) rollArp();
        if (rng.chance(0.3f)) rollBass();
        if (rng.chance(0.3f)) rollDrums();
        leadDensity = 0.2f + 0.25f * rng.uni();
      }
    }
    bool fill = (bar % 8 == 7) && step >= 12;

    // ---- bass
    int b = bass[step];
    if (I < 0.45f) b = (step == 0) ? 1 : (step == 8 && I > 0.25f ? 1 : 0);
    if (b) {
      int deg = chord[0] % 7;
      int m = degToMidi(b == 3 ? deg + 4 : deg, 24) + (b == 2 ? 12 : 0);
      while (m < 28) m += 12;
      while (m > 47) m -= 12;
      bassFreq = mtof((float)m);
      bassEnv = 1; bassCut = 1;
      bassGate = I < 0.45f ? 3.4f : 0.22f;
    }
    // ---- arpeggio
    int a = arp[step];
    float arpAmt = sstep(0.18f, 0.55f, I);
    if (a >= 0 && arpAmt > 0.01f && (I > 0.5f || (step % 2 == 0 && rng.chance(0.3f + I)))) {
      int deg = chord[a % 4] % 7 + 7 * (a / 4);
      arpFreq = mtof((float)degToMidi(deg, 60 + 12 * arpOct));
      arpEnv = 1;
      arpPan = (step % 2) ? 0.5f : -0.5f;
    }
    // ---- lead: a slow wandering melody, thinning out as the fight heats up
    if (leadLeft > 0 && --leadLeft == 0) leadGate = 0;
    float leadAmt = 1.0f - sstep(0.55f, 0.9f, I);
    if (step % 2 == 0 && leadLeft == 0 && rng.chance(leadDensity * (0.4f + leadAmt))) {
      static const int moves[7] = {-2, -1, -1, 1, 1, 2, 3};
      leadDeg += moves[rng.range(0, 6)];
      if (step == 0 || rng.chance(0.35f)) { // land on a chord tone
        int best = leadDeg, bd = 99;
        for (int o = 0; o < 4; ++o)
          for (int k = 0; k < 3; ++k) {
            int c = chord[k] % 7 + 7 * o;
            if (std::abs(c - leadDeg) < bd) { bd = std::abs(c - leadDeg); best = c; }
          }
        leadDeg = best;
      }
      if (leadDeg < 7) leadDeg += 7;
      if (leadDeg > 20) leadDeg -= 7;
      leadTarget = mtof((float)degToMidi(leadDeg, 60));
      leadGate = 1;
      leadLeft = rng.range(2, 7);
    }
    // ---- drums
    float drumAmt = sstep(0.42f, 0.7f, I);
    if (drumAmt > 0.01f) {
      if ((kick >> step) & 1) kickT = 0;
      if (((snare >> step) & 1) && I > 0.55f) snareT = 0;
      if (fill && I > 0.6f && (step % 2 == 0 || rng.chance(0.5f))) snareT = 0;
      if ((hat >> step) & 1) { hatT = 0; hatDecay = (step % 8 == 6 && rng.chance(0.5f)) ? 16.0f : 75.0f; }
    }
    if (++step >= 16) { step = 0; ++bar; }
  }

  // one stereo sample of music at intensity I
  void next(float I, float& L, float& R) {
    if (samplesToStep <= 0) {
      double stepLen = (double)SR * 60.0 / (double)bpm / 4.0;
      samplesToStep += (step % 2 == 0) ? stepLen * 1.08 : stepLen * 0.92; // light swing
      onStep(I);
    }
    samplesToStep -= 1.0;
    lfo += 0.11f / SR;
    if (lfo >= 1) lfo -= 1;
    float slow = std::sin(lfo * TAU);

    // ---- pad
    float padL = 0, padR = 0;
    for (int i = 0; i < 4; ++i) {
      padFreq[i] += (padTarget[i] - padFreq[i]) * 0.0006f;
      float s = padSaw[i][0].next(padFreq[i] * 1.004f) + padSaw[i][1].next(padFreq[i] * 0.996f);
      float pan = (i % 2) ? 0.8f : 0.2f;
      padL += s * (1 - pan);
      padR += s * pan;
    }
    padSwell += (1.0f - padSwell) * 0.00004f;
    float pg = svfG(420.0f + 1500.0f * I + 260.0f * slow + 500.0f * padSwell);
    padL = padF[0].lp(padL, pg, 0.9f) * 0.105f * padSwell;
    padR = padF[1].lp(padR, pg, 0.9f) * 0.105f * padSwell;

    // ---- bass
    bassGate -= 1.0f / (SR * 60.0f / bpm);
    if (bassGate < 0) bassEnv *= 0.9992f;
    bassCut *= 0.99975f;
    float bs = bassSaw.next(bassFreq) * 0.6f + bassSub.next(bassFreq * 0.5f) * 0.45f;
    bs = bassF.lp(bs, svfG(120.0f + (300.0f + 1500.0f * I) * bassCut), 0.6f) * bassEnv * (0.15f + 0.08f * I);

    // ---- arp
    arpEnv *= 0.99986f - 0.00012f * (1.0f - I);
    float as = arpF.lp(arpSaw.next(arpFreq), svfG(500.0f + 5200.0f * arpEnv * arpEnv), 0.5f) * arpEnv * 0.24f * sstep(0.18f, 0.55f, I);

    // ---- lead
    leadFreq += (leadTarget - leadFreq) * 0.0015f;
    leadEnv += ((leadGate > 0 ? 1.0f : 0.0f) - leadEnv) * (leadGate > 0 ? 0.0012f : 0.00008f);
    vib += 5.2f / SR;
    if (vib >= 1) vib -= 1;
    float lf = leadFreq * (1.0f + 0.004f * std::sin(vib * TAU) * leadEnv);
    float ls = (leadA.next(lf) + 0.3f * leadB.next(lf * 2.0f)) * leadEnv * 0.17f * (1.0f - 0.7f * sstep(0.55f, 0.9f, I));

    // ---- drums
    float drumAmt = sstep(0.42f, 0.7f, I);
    float dt = 1.0f / SR;
    float kickS = 0, snareS = 0, hatS = 0;
    if (kickT < 0.5f) { kickS = kickOsc.next(44.0f + 120.0f * std::exp(-kickT * 38.0f)) * std::exp(-kickT * 8.0f); kickT += dt; }
    float n = noise.bi();
    if (snareT < 0.4f) { snareS = ((n - snareHp.lp(n, 1400)) * 0.7f + snareOsc.next(190.0f) * 0.5f * std::exp(-snareT * 30.0f)) * std::exp(-snareT * 19.0f); snareT += dt; }
    if (hatT < 0.3f) { hatS = (n - hatHp.lp(n, 7000)) * std::exp(-hatT * hatDecay); hatT += dt; }
    kickS *= 0.55f * drumAmt; snareS *= 0.26f * drumAmt; hatS *= 0.16f * sstep(0.35f, 0.6f, I);

    // ---- delay (ping-pong, three sixteenths) on arp and lead
    size_t dTime = (size_t)((double)SR * 60.0 / (double)bpm * 0.75);
    if (dTime > 47000) dTime = 47000;
    float dL = dl.tap(dTime), dR = dr.tap(dTime);
    float aL = as * (0.5f - arpPan * 0.5f), aR = as * (0.5f + arpPan * 0.5f);
    dl.push(dlp.lp(aL + ls * 0.6f + dR * 0.42f, 3500));
    dr.push(drp.lp(aR + ls * 0.4f + dL * 0.42f, 3500));

    // ---- reverb
    float rL, rR;
    rev.process(padL * 0.5f + ls * 0.7f + aL * 0.4f + dL * 0.4f + snareS * 0.3f, padR * 0.5f + ls * 0.7f + aR * 0.4f + dR * 0.4f + snareS * 0.3f, rL, rR);
    float wet = 0.55f - 0.2f * I;

    float centre = bs + kickS + snareS + hatS * 0.8f + ls * 0.7f;
    L = padL + aL + dL * 0.5f + centre + rL * wet + hatS * 0.3f;
    R = padR + aR + dR * 0.5f + centre + rR * wet - hatS * 0.1f;
  }
};

} // namespace

// =====================================================================================
struct Audio::Impl {
  ma_device device;
  bool deviceOk = false;
  Buf sfx[SFX_COUNT];
  float baseGain[SFX_COUNT];

  struct Voice { const Buf* buf; double pos; float step, gl, gr; };
  std::vector<Voice> voices, pending;
  std::mutex mtx;

  std::atomic<float> thrust{-1.0f}, boost{0.0f}, intensity{0.2f}, musicVol{0.5f}, sfxVol{0.9f};
  std::atomic<uint32_t> wantSeed{1};
  uint32_t haveSeed = 0;
  float songFade = 0, I = 0.2f;
  Music music;
  Reverb sfxRev; // a short tail gives the effects weight and places them in one space
  char info[48] = "";
  std::mutex infoMtx;

  // engine
  float eThrust = 0, eBoost = 0, eGain = 0;
  Saw eSaw[2]; SVF eF; OnePole eNoise, eRoar; Lcg eRng;

  static void callback(ma_device* dev, void* out, const void*, ma_uint32 frames) {
    ((Audio::Impl*)dev->pUserData)->render((float*)out, (int)frames);
  }

  void render(float* out, int frames) {
    if (mtx.try_lock()) {
      for (auto& v : pending) {
        if (voices.size() >= 56) voices.erase(voices.begin());
        voices.push_back(v);
      }
      pending.clear();
      mtx.unlock();
    }
    const float mv = musicVol.load(), sv = sfxVol.load();
    const float tI = intensity.load();
    const float tThrust = thrust.load(), tBoost = boost.load();
    const uint32_t ws = wantSeed.load();

    for (int i = 0; i < frames; ++i) {
      // ---- music, with a short fade when the song changes
      if (ws != haveSeed) {
        songFade -= 1.0f / (0.6f * SR);
        if (songFade <= 0) {
          songFade = 0;
          haveSeed = ws;
          music.compose(ws);
          std::lock_guard<std::mutex> lk(infoMtx);
          std::memcpy(info, music.info, sizeof(info));
        }
      } else if (songFade < 1) {
        songFade = std::min(1.0f, songFade + 1.0f / (1.5f * SR));
      }
      I += (tI - I) * (tI > I ? 0.00004f : 0.000012f); // rises in about half a second, relaxes slowly
      float mL = 0, mR = 0;
      music.next(I, mL, mR);
      float L = mL * mv * songFade, R = mR * mv * songFade;

      // ---- engine rumble
      float want = tThrust < 0 ? 0.0f : 0.012f + 0.05f * tThrust + 0.04f * tBoost;
      eGain += (want - eGain) * 0.0003f;
      if (eGain > 0.0005f) {
        eThrust += (std::max(tThrust, 0.0f) - eThrust) * 0.0002f;
        eBoost += (tBoost - eBoost) * 0.0002f;
        float f = 41.0f * (1.0f + 0.7f * eThrust + 0.5f * eBoost);
        float n = eRng.bi();
        float s = eSaw[0].next(f) + eSaw[1].next(f * 1.503f) * 0.6f + eNoise.lp(n, 90.0f) * 9.0f;
        s = eF.lp(s, svfG(140.0f + 520.0f * eThrust + 900.0f * eBoost), 0.8f);
        s += (n - eRoar.lp(n, 900.0f)) * 0.25f * eBoost;
        L += s * eGain * sv;
        R += s * eGain * sv;
      }

      // ---- sound effect voices
      float sL = 0, sR = 0;
      for (size_t v = 0; v < voices.size();) {
        Voice& vo = voices[v];
        size_t ip = (size_t)vo.pos;
        if (ip + 1 >= vo.buf->size()) { voices[v] = voices.back(); voices.pop_back(); continue; }
        float fr = (float)(vo.pos - (double)ip);
        float s = (*vo.buf)[ip] * (1 - fr) + (*vo.buf)[ip + 1] * fr;
        sL += s * vo.gl;
        sR += s * vo.gr;
        vo.pos += vo.step;
        ++v;
      }
      float wL, wR;
      sfxRev.process(sL, sR, wL, wR);
      L += (sL + wL * 0.3f) * sv;
      R += (sR + wR * 0.3f) * sv;
      out[i * 2] = std::tanh(L);
      out[i * 2 + 1] = std::tanh(R);
    }
  }
};

Audio::Audio() : d(new Impl()) {}
Audio::~Audio() { shutdown(); }

bool Audio::init(bool device) {
  buildSfx(d->sfx, d->baseGain);
  d->music.init();
  d->sfxRev.init();
  d->voices.reserve(64);
  d->pending.reserve(64);
  if (!device) return true;

  ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
  cfg.playback.format = ma_format_f32;
  cfg.playback.channels = 2;
  cfg.sampleRate = 48000;
  cfg.dataCallback = Impl::callback;
  cfg.pUserData = d.get();
  const char* env = std::getenv("HULLBREAKER_AUDIO");
  ma_backend nullBackend[1] = {ma_backend_null};
  bool forceNull = env && std::strcmp(env, "null") == 0;
  if (ma_device_init_ex(forceNull ? nullBackend : nullptr, forceNull ? 1 : 0, nullptr, &cfg, &d->device) != MA_SUCCESS) {
    std::fprintf(stderr, "audio: no output device found, running silent\n");
    return true;
  }
  if (ma_device_start(&d->device) != MA_SUCCESS) {
    ma_device_uninit(&d->device);
    std::fprintf(stderr, "audio: could not start the output device, running silent\n");
    return true;
  }
  d->deviceOk = true;
  return true;
}
void Audio::shutdown() {
  if (d && d->deviceOk) {
    ma_device_uninit(&d->device);
    d->deviceOk = false;
  }
}
bool Audio::hasDevice() const { return d->deviceOk; }

void Audio::play(int sfx, float gain, float pan, float pitch) {
  if (sfx < 0 || sfx >= SFX_COUNT || gain <= 0.003f) return;
  pan = pan < -1 ? -1 : (pan > 1 ? 1 : pan);
  float g = gain * d->baseGain[sfx];
  float a = (pan * 0.5f + 0.5f) * 1.5707963f; // equal-power pan
  Impl::Voice v{&d->sfx[sfx], 0.0, pitch, g * std::cos(a) * 1.4142f, g * std::sin(a) * 1.4142f};
  std::lock_guard<std::mutex> lk(d->mtx);
  if (d->pending.size() < 48) d->pending.push_back(v);
}
void Audio::setEngine(float thrust, float boost) { d->thrust = thrust; d->boost = boost; }
void Audio::newSong(uint32_t seed) { d->wantSeed = seed ? seed : 1; }
void Audio::setIntensity(float i) { d->intensity = clamp01(i); }
void Audio::setMusicVolume(float v) { d->musicVol = clamp01(v); }
void Audio::setSfxVolume(float v) { d->sfxVol = clamp01(v); }
float Audio::musicVolume() const { return d->musicVol.load(); }
float Audio::sfxVolume() const { return d->sfxVol.load(); }
const char* Audio::songInfo() const {
  static thread_local char buf[48];
  std::lock_guard<std::mutex> lk(d->infoMtx);
  std::memcpy(buf, d->info, sizeof(buf));
  return buf;
}
void Audio::render(float* out, int frames) { d->render(out, frames); }

// =====================================================================================
int renderAudioToWav(const char* path, float seconds, uint32_t seed) {
  Audio a;
  a.init(false);
  a.newSong(seed);
  const int sr = 48000, block = 480;
  const float sfxTime = SFX_COUNT * 3.2f;
  const int total = (int)((seconds + sfxTime) * sr);
  std::vector<int16_t> pcm;
  pcm.reserve((size_t)total * 2);
  std::vector<float> buf((size_t)block * 2);
  int nextSfx = 0;
  for (int done = 0; done < total; done += block) {
    float t = (float)done / (float)sr;
    if (t < seconds) {
      // calm -> full combat -> calm, so one file shows the whole arrangement
      float u = t / seconds;
      a.setIntensity(u < 0.25f ? 0.12f : (u < 0.75f ? 0.12f + 0.88f * std::min((u - 0.25f) / 0.25f, 1.0f) : 0.2f));
      a.setEngine(-1, 0);
    } else {
      a.setIntensity(0.1f);
      a.setMusicVolume(0.0f);
      int idx = (int)((t - seconds) / 3.2f);
      if (idx >= nextSfx && idx < SFX_COUNT) { a.play(idx, 1.0f, 0.0f); nextSfx = idx + 1; }
    }
    a.render(buf.data(), block);
    for (float v : buf) pcm.push_back((int16_t)(std::max(-1.0f, std::min(1.0f, v)) * 32767.0f));
  }
  FILE* f = std::fopen(path, "wb");
  if (!f) { std::fprintf(stderr, "cannot write %s\n", path); return 1; }
  uint32_t dataBytes = (uint32_t)(pcm.size() * 2), u32;
  uint16_t u16;
  std::fwrite("RIFF", 1, 4, f); u32 = 36 + dataBytes; std::fwrite(&u32, 4, 1, f);
  std::fwrite("WAVEfmt ", 1, 8, f); u32 = 16; std::fwrite(&u32, 4, 1, f);
  u16 = 1; std::fwrite(&u16, 2, 1, f); u16 = 2; std::fwrite(&u16, 2, 1, f);
  u32 = (uint32_t)sr; std::fwrite(&u32, 4, 1, f); u32 = (uint32_t)sr * 4; std::fwrite(&u32, 4, 1, f);
  u16 = 4; std::fwrite(&u16, 2, 1, f); u16 = 16; std::fwrite(&u16, 2, 1, f);
  std::fwrite("data", 1, 4, f); std::fwrite(&dataBytes, 4, 1, f);
  std::fwrite(pcm.data(), 2, pcm.size(), f);
  std::fclose(f);
  std::printf("wrote %s: %.0f s of music (%s) followed by %d sound effects\n", path, seconds, a.songInfo(), (int)SFX_COUNT);
  return 0;
}
