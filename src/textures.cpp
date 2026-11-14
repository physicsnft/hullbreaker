#include "textures.h"
#include "math.h"
#include <functional>
#include <thread>

namespace {

// ---- tileable noise: lattice coordinates wrap at `per`, so the result repeats exactly
inline uint32_t hsh(int x, int y, uint32_t s) {
  uint32_t h = (uint32_t)x * 0x8da6b343u ^ (uint32_t)y * 0xd8163841u ^ s * 0xcb1ab31fu;
  h ^= h >> 15; h *= 0x2c1b3c6du; h ^= h >> 12; h *= 0x297a2d39u; h ^= h >> 15;
  return h;
}
inline float h01(int x, int y, uint32_t s) { return (float)(hsh(x, y, s) & 0xFFFFFF) / 16777215.0f; }
inline int wrapi(int a, int per) { a %= per; return a < 0 ? a + per : a; }

float vnP(float x, float y, int perX, int perY, uint32_t seed) {
  int xi = (int)std::floor(x), yi = (int)std::floor(y);
  float fx = x - xi, fy = y - yi;
  fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
  int x0 = wrapi(xi, perX), x1 = wrapi(xi + 1, perX), y0 = wrapi(yi, perY), y1 = wrapi(yi + 1, perY);
  return lerpf(lerpf(h01(x0, y0, seed), h01(x1, y0, seed), fx), lerpf(h01(x0, y1, seed), h01(x1, y1, seed), fx), fy);
}
float fbmP(float u, float v, int per, int oct, uint32_t seed) {
  float a = 0.5f, s = 0, n = 0;
  for (int o = 0; o < oct; ++o) {
    int p = per << o;
    s += a * vnP(u * p, v * p, p, p, seed + o * 31);
    n += a;
    a *= 0.5f;
  }
  return s / n;
}
float ridgedP(float u, float v, int per, int oct, uint32_t seed) {
  float a = 0.5f, s = 0, n = 0;
  for (int o = 0; o < oct; ++o) {
    int p = per << o;
    float r = 1.0f - std::fabs(2.0f * vnP(u * p, v * p, p, p, seed + o * 17) - 1.0f);
    s += a * r * r;
    n += a;
    a *= 0.5f;
  }
  return s / n;
}
// tileable cellular noise: distances to the nearest and second-nearest feature point
void worleyP(float u, float v, int per, uint32_t seed, float& f1, float& f2, uint32_t& id) {
  float x = u * per, y = v * per;
  int xi = (int)std::floor(x), yi = (int)std::floor(y);
  f1 = f2 = 9.0f;
  id = 0;
  for (int dy = -1; dy <= 1; ++dy)
    for (int dx = -1; dx <= 1; ++dx) {
      int cx = xi + dx, cy = yi + dy, wx = wrapi(cx, per), wy = wrapi(cy, per);
      float px = cx + h01(wx, wy, seed), py = cy + h01(wx, wy, seed + 7);
      float d = std::sqrt(sqr(px - x) + sqr(py - y));
      if (d < f1) { f2 = f1; f1 = d; id = hsh(wx, wy, seed + 13); }
      else if (d < f2) f2 = d;
    }
}

struct Texel { float h = 0, r = 1, g = 1, b = 1, rough = 0.5f, wear = 0, ao = 1; };
typedef std::vector<Texel> Layer;

void parallelRows(int size, const std::function<void(int)>& row) {
  unsigned n = std::max(1u, std::min(16u, std::thread::hardware_concurrency()));
  std::vector<std::thread> pool;
  for (unsigned t = 0; t < n; ++t)
    pool.emplace_back([=, &row] { for (int y = (int)t; y < size; y += (int)n) row(y); });
  for (auto& t : pool) t.join();
}

// Thin lines scored into the surface: bright bare metal, slightly sunk, smoother.
void scratches(Layer& L, int S, int count, uint32_t seed, float tileMetres, float maxLenM) {
  Rng r(seed);
  for (int k = 0; k < count; ++k) {
    float x = r.range(0, (float)S), y = r.range(0, (float)S), a = r.chance(0.6f) ? r.range(-0.25f, 0.25f) + (r.chance(0.5f) ? 0.0f : PI * 0.5f) : r.range(0, PI);
    float len = r.range(0.04f, maxLenM) / tileMetres * S, curve = r.range(-0.0015f, 0.0015f);
    float strength = r.range(0.2f, 1.0f) * r.range(0.3f, 1.0f);
    bool wide = r.chance(0.18f);
    for (float t = 0; t < len; t += 0.5f) {
      a += curve;
      x += std::cos(a) * 0.5f; y += std::sin(a) * 0.5f;
      float fade = std::min(1.0f, std::min(t, len - t) / 6.0f) * strength;
      for (int w = 0; w < (wide ? 2 : 1); ++w) {
        Texel& p = L[(size_t)wrapi((int)y + w, S) * S + wrapi((int)x, S)];
        p.h -= 0.0005f * fade;
        p.rough = lerpf(p.rough, 0.2f, 0.6f * fade);
        float lift = 1.0f + 0.12f * fade;
        p.r *= lift; p.g *= lift; p.b *= lift;
        p.wear = std::max(p.wear, 0.85f * fade);
      }
    }
  }
}

// ---------------------------------------------------------------- hull plating (6 m tile)
struct Panel { int x0, y0, x1, y1; float h, tint, rough; bool rivets, hatch, vent, raised; int dir; };

void genHull(Layer& L, int S) {
  const int G = 48; // layout grid: one cell = 12.5 cm
  const float M = 6.0f;
  Rng r(4242);
  std::vector<Panel> panels;
  std::function<void(int, int, int, int, int)> split = [&](int x0, int y0, int x1, int y1, int depth) {
    int w = x1 - x0, h = y1 - y0;
    if (depth < 3 && (w >= 8 || h >= 8) && r.chance(depth == 0 ? 0.95f : 0.6f)) {
      if ((w > h && w >= 8) || h < 8) { int c = x0 + r.irange(3, w - 3); split(x0, y0, c, y1, depth + 1); split(c, y0, x1, y1, depth + 1); }
      else { int c = y0 + r.irange(3, h - 3); split(x0, y0, x1, c, depth + 1); split(x0, c, x1, y1, depth + 1); }
      return;
    }
    Panel p{x0, y0, x1, y1, r.range(-1, 1), r.range(0.93f, 1.05f), r.range(-0.05f, 0.07f), r.chance(0.55f), false, false, r.chance(0.14f), r.irange(0, 1)};
    if (w >= 9 && h >= 9) { p.hatch = r.chance(0.45f); p.vent = !p.hatch && r.chance(0.3f); }
    panels.push_back(p);
  };
  for (int by = 0; by < 2; ++by)
    for (int bx = 0; bx < 2; ++bx) split(bx * 24, by * 24, bx * 24 + 24, by * 24 + 24, 0);
  std::vector<int> cell((size_t)G * G, 0);
  for (size_t i = 0; i < panels.size(); ++i)
    for (int y = panels[i].y0; y < panels[i].y1; ++y)
      for (int x = panels[i].x0; x < panels[i].x1; ++x) cell[(size_t)y * G + x] = (int)i;

  parallelRows(S, [&](int py) {
    for (int px = 0; px < S; ++px) {
      float u = (px + 0.5f) / S, v = (py + 0.5f) / S;
      const Panel& p = panels[(size_t)cell[(size_t)std::min(G - 1, (int)(v * G)) * G + std::min(G - 1, (int)(u * G))]];
      float x0 = p.x0 * M / G, x1 = p.x1 * M / G, y0 = p.y0 * M / G, y1 = p.y1 * M / G, xm = u * M, ym = v * M;
      float dx = std::min(xm - x0, x1 - xm), dy = std::min(ym - y0, y1 - ym), d = std::min(dx, dy);
      float groove = 1.0f - smoothstepf(0.0025f, 0.008f, d), bevel = 1.0f - smoothstepf(0.008f, 0.032f, d);
      Texel t;
      t.h = p.h * 0.0012f + (p.raised ? 0.004f : 0.0f) - groove * 0.006f - bevel * 0.0012f;
      float alb = p.tint, rough = 0.30f + p.rough, ao = 1.0f - 0.8f * groove - 0.15f * bevel;
      // fasteners along the panel edges
      if (p.rivets) {
        const float inset = 0.045f, pitch = 0.11f, rad = 0.011f;
        float best = 9.0f;
        if (dy > inset * 0.5f) best = std::min(best, std::hypot(dx - inset, (ym / pitch - std::floor(ym / pitch) - 0.5f) * pitch));
        if (dx > inset * 0.5f) best = std::min(best, std::hypot(dy - inset, (xm / pitch - std::floor(xm / pitch) - 0.5f) * pitch));
        if (best < rad) { t.h += 0.0028f * std::sqrt(1.0f - sqr(best / rad)); alb *= 1.06f; rough -= 0.06f; }
        else if (best < rad * 1.6f) { ao -= 0.3f; alb *= 0.9f; }
      }
      float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f, hw = (x1 - x0) * 0.5f, hh = (y1 - y0) * 0.5f;
      if (p.hatch) { // access hatch: a rounded outline with four screws
        float bw = hw * 0.5f, bh = hh * 0.45f, rr = 0.04f;
        float qx = std::fabs(xm - cx) - (bw - rr), qy = std::fabs(ym - cy) - (bh - rr);
        float sd = std::hypot(std::max(qx, 0.0f), std::max(qy, 0.0f)) + std::min(std::max(qx, qy), 0.0f) - rr;
        float line = 1.0f - smoothstepf(0.002f, 0.006f, std::fabs(sd));
        t.h -= line * 0.004f; ao -= line * 0.7f; alb *= 1.0f - 0.5f * line;
        if (sd < 0) { t.h += 0.0015f; alb *= 0.97f; }
        float sx = std::fabs(std::fabs(xm - cx) - (bw - 0.06f)), sy = std::fabs(std::fabs(ym - cy) - (bh - 0.06f)), sdist = std::hypot(sx, sy);
        if (sdist < 0.013f) { t.h += 0.002f * (1 - sdist / 0.013f) - (std::fabs(sx - sy) < 0.002f ? 0.002f : 0.0f); alb *= 1.05f; }
      } else if (p.vent) { // louvred vent
        float bw = hw * 0.45f, bh = hh * 0.35f;
        if (std::fabs(xm - cx) < bw && std::fabs(ym - cy) < bh) {
          float s = (ym - cy) / 0.028f, fr = s - std::floor(s);
          float slot = smoothstepf(0.45f, 0.55f, fr) * (1 - smoothstepf(0.9f, 1.0f, fr)) * smoothstepf(0.0f, 0.02f, bw - std::fabs(xm - cx));
          t.h -= slot * 0.006f - (1 - slot) * fr * 0.002f; alb *= 1.0f - 0.7f * slot; ao -= 0.75f * slot; rough += 0.2f * slot;
        }
      }
      // brushing: fine streaks along the panel's grain
      float bu = p.dir ? v : u, bv = p.dir ? u : v;
      float brush = vnP(bu * 6, bv * 1536, 6, 1536, 11) * 0.6f + vnP(bu * 24, bv * 512, 24, 512, 12) * 0.4f;
      rough += (brush - 0.5f) * 0.20f;
      t.h += (brush - 0.5f) * 0.00025f;
      // shallow dents and waviness in the sheet
      t.h += (fbmP(u, v, 3, 4, 21) - 0.5f) * 0.005f;
      // grime gathers along seams and in patches; streaks run across the tile
      float gr = fbmP(u, v, 6, 6, 33), streak = vnP(u * 96, v * 4, 96, 4, 44);
      float grime = clampf(bevel * (0.35f + 0.9f * gr) + smoothstepf(0.56f, 0.8f, gr) * 0.55f + smoothstepf(0.7f, 0.95f, streak) * 0.3f * gr, 0.0f, 1.0f);
      alb *= 1.0f - 0.34f * grime;
      rough += 0.3f * grime;
      alb *= 1.0f - 0.8f * groove;
      rough += 0.4f * groove;
      // oxidation blooms: warmer, rougher, and paint will not hold there
      float ox = smoothstepf(0.6f, 0.76f, fbmP(u, v, 5, 6, 55)) * (0.35f + 0.65f * smoothstepf(0.3f, 0.7f, fbmP(u, v, 24, 3, 66)));
      t.r = alb * lerpf(1.0f, 0.95f, ox); t.g = alb * lerpf(1.0f, 0.70f, ox); t.b = alb * lerpf(1.0f, 0.50f, ox);
      rough += 0.3f * ox;
      // fine pitting keeps the surface from looking airbrushed up close
      float pit = vnP(u * 1024, v * 1024, 1024, 1024, 77);
      t.h -= smoothstepf(0.86f, 1.0f, pit) * 0.0006f;
      t.rough = clampf(rough + (pit - 0.5f) * 0.05f, 0.08f, 1.0f);
      t.wear = clampf(ox * 0.9f + smoothstepf(0.72f, 0.9f, fbmP(u, v, 12, 5, 88)) * 0.8f, 0.0f, 1.0f);
      t.ao = clampf(ao, 0.0f, 1.0f);
      L[(size_t)py * S + px] = t;
    }
  });
  scratches(L, S, 240, 99, M, 0.45f);
}

// ---------------------------------------------------------------- rock
void genRock(Layer& L, int S) {
  parallelRows(S, [&](int py) {
    for (int px = 0; px < S; ++px) {
      float u = (px + 0.5f) / S, v = (py + 0.5f) / S;
      float n1 = fbmP(u, v, 4, 8, 101), n2 = fbmP(u, v, 3, 5, 202), ridge = ridgedP(u + 0.05f * n2, v + 0.05f * n1, 3, 7, 303);
      Texel t;
      t.h = 0.07f * ridge + 0.05f * n1;
      float lum = 0.62f + 0.55f * (n1 - 0.5f) + 0.25f * (ridge - 0.45f), ao = 0.75f + 0.9f * (ridge - 0.5f);
      // impact pits at two sizes, each with a raised rim
      for (int s = 0; s < 2; ++s) {
        float f1, f2; uint32_t id;
        worleyP(u, v, s ? 13 : 5, 400 + s, f1, f2, id);
        if ((id & 7) > (s ? 3u : 2u)) continue; // most cells stay unscarred
        float rad = 0.12f + 0.33f * sqr((float)((id >> 8) & 255) / 255.0f), k = f1 / rad, depth = (s ? 0.012f : 0.03f);
        if (k < 1) { float bowl = 1 - k * k; t.h -= depth * bowl; lum -= 0.16f * bowl; ao -= 0.3f * bowl; }
        else if (k < 1.3f) t.h += depth * 0.3f * (1 - (k - 1) / 0.3f);
      }
      // layered strata and mineral veins
      float strata = vnP(u * 3 + n1 * 2.0f, v * 40 + n2 * 6.0f, 3, 40, 505);
      lum += 0.10f * (strata - 0.5f);
      float vein = 1.0f - smoothstepf(0.0f, 0.035f, std::fabs(fbmP(u, v, 6, 4, 606) - 0.5f));
      lum += 0.16f * vein * smoothstepf(0.5f, 0.7f, n2);
      float grain = vnP(u * 512, v * 512, 512, 512, 707), grain2 = vnP(u * 160, v * 160, 160, 160, 808);
      t.h += 0.0035f * (grain - 0.5f) + 0.006f * (grain2 - 0.5f);
      lum += 0.10f * (grain - 0.5f) + 0.08f * (grain2 - 0.5f);
      lum = clampf(lum, 0.12f, 1.25f);
      float warm = n2 - 0.5f;
      t.r = lum * (1.02f + 0.16f * warm); t.g = lum; t.b = lum * (0.97f - 0.18f * warm);
      t.rough = clampf(0.86f + 0.12f * (grain - 0.5f) - 0.2f * vein, 0.3f, 1.0f);
      t.ao = clampf(ao, 0.15f, 1.0f);
      L[(size_t)py * S + px] = t;
    }
  });
}

// ---------------------------------------------------------------- ice
void genIce(Layer& L, int S) {
  parallelRows(S, [&](int py) {
    for (int px = 0; px < S; ++px) {
      float u = (px + 0.5f) / S, v = (py + 0.5f) / S;
      float wob = fbmP(u, v, 6, 4, 11) - 0.5f;
      float f1, f2, g1, g2; uint32_t id, id2;
      worleyP(u + wob * 0.03f, v - wob * 0.03f, 5, 21, f1, f2, id);
      worleyP(u - wob * 0.02f, v + wob * 0.02f, 17, 22, g1, g2, id2);
      float crack = 1.0f - smoothstepf(0.0f, 0.03f, f2 - f1), fine = (1.0f - smoothstepf(0.0f, 0.05f, g2 - g1)) * 0.5f;
      float frost = smoothstepf(0.42f, 0.7f, fbmP(u, v, 4, 7, 31));
      float facet = (float)(id & 255) / 255.0f, sparkle = vnP(u * 700, v * 700, 700, 700, 41);
      Texel t;
      t.h = 0.03f * fbmP(u, v, 3, 5, 51) + 0.012f * facet - 0.02f * crack - 0.006f * fine + 0.002f * frost * (sparkle - 0.5f);
      float lum = 0.78f + 0.12f * facet + 0.22f * frost - 0.35f * crack - 0.15f * fine + 0.06f * frost * (sparkle - 0.5f);
      t.r = lum * 0.96f; t.g = lum; t.b = lum * 1.04f;
      t.rough = clampf(0.10f + 0.55f * frost + 0.3f * crack + 0.15f * fine, 0.05f, 1.0f);
      t.wear = frost;
      t.ao = clampf(1.0f - 0.6f * crack - 0.25f * fine, 0.0f, 1.0f);
      L[(size_t)py * S + px] = t;
    }
  });
}

// ---------------------------------------------------------------- station plating (4 m tile)
void genIndustrial(Layer& L, int S) {
  const float M = 4.0f;
  parallelRows(S, [&](int py) {
    for (int px = 0; px < S; ++px) {
      float u = (px + 0.5f) / S, v = (py + 0.5f) / S, xm = u * M, ym = v * M;
      // 2 m plates, the second row offset by half a plate
      float row = std::floor(ym / 2.0f), xo = xm + (((int)row & 1) ? 1.0f : 0.0f);
      float lx = xo - std::floor(xo / 2.0f) * 2.0f, ly = ym - row * 2.0f;
      float dx = std::min(lx, 2.0f - lx), dy = std::min(ly, 2.0f - ly), d = std::min(dx, dy);
      float groove = 1.0f - smoothstepf(0.003f, 0.010f, d), bevel = 1.0f - smoothstepf(0.010f, 0.05f, d);
      uint32_t pid = hsh((int)std::floor(xo / 2.0f) & 1, (int)row & 1, 5);
      Texel t;
      float alb = 0.92f + 0.12f * (float)(pid & 255) / 255.0f, rough = 0.52f, ao = 1.0f - 0.8f * groove - 0.12f * bevel;
      t.h = -0.006f * groove - 0.001f * bevel + (fbmP(u, v, 2, 4, 9) - 0.5f) * 0.004f;
      // bolts in the plate corners and midway along the long edges
      float bx = std::fabs(dx - 0.09f), by = std::fabs(dy - 0.09f), bm = std::fabs(ly - 1.0f), bolt = std::min(std::hypot(bx, by), std::hypot(bx, bm));
      if (bolt < 0.022f) { t.h += 0.004f * (bolt < 0.014f ? 1.0f : (0.022f - bolt) / 0.008f); alb *= 1.08f; rough -= 0.12f; }
      else if (bolt < 0.034f) { ao -= 0.35f; alb *= 0.85f; }
      // scuffs, tyre and boot marks, stains
      float n = fbmP(u, v, 5, 7, 19), scuff = smoothstepf(0.55f, 0.75f, fbmP(u, v, 9, 5, 29));
      float drag = smoothstepf(0.72f, 0.95f, vnP(u * 5, v * 160, 5, 160, 39)) * smoothstepf(0.4f, 0.6f, n);
      float stain = smoothstepf(0.6f, 0.85f, fbmP(u, v, 3, 5, 49));
      alb *= (0.86f + 0.26f * n) * (1.0f - 0.22f * scuff) * (1.0f - 0.25f * drag) * (1.0f - 0.3f * stain) * (1.0f - 0.75f * groove) * (1.0f - 0.2f * bevel * n);
      rough += 0.25f * scuff - 0.2f * stain + 0.3f * groove + 0.1f * (n - 0.5f);
      float pit = vnP(u * 800, v * 800, 800, 800, 59);
      t.h += (pit - 0.5f) * 0.0008f;
      t.r = alb; t.g = alb; t.b = alb * (1.0f + 0.03f * stain);
      t.rough = clampf(rough + (pit - 0.5f) * 0.08f, 0.1f, 1.0f);
      t.wear = scuff;
      t.ao = clampf(ao, 0.0f, 1.0f);
      L[(size_t)py * S + px] = t;
    }
  });
  scratches(L, S, 160, 69, M, 0.9f);
}

// Pack one layer: normals come from the height field's wrapped central differences.
void encode(const Layer& L, int S, float tileMetres, uint8_t* A, uint8_t* B) {
  const float inv = (float)S / (2.0f * tileMetres);
  parallelRows(S, [&](int y) {
    for (int x = 0; x < S; ++x) {
      const Texel& t = L[(size_t)y * S + x];
      float hx = (L[(size_t)y * S + wrapi(x + 1, S)].h - L[(size_t)y * S + wrapi(x - 1, S)].h) * inv;
      float hy = (L[(size_t)wrapi(y + 1, S) * S + x].h - L[(size_t)wrapi(y - 1, S) * S + x].h) * inv;
      float l = 1.0f / std::sqrt(hx * hx + hy * hy + 1.0f);
      size_t o = ((size_t)y * S + x) * 4;
      auto q = [](float f) { return (uint8_t)(clampf(f, 0.0f, 1.0f) * 255.0f + 0.5f); };
      A[o] = q(t.r / 1.3f); A[o + 1] = q(t.g / 1.3f); A[o + 2] = q(t.b / 1.3f); A[o + 3] = q(t.rough);
      B[o] = q(-hx * l * 0.5f + 0.5f); B[o + 1] = q(-hy * l * 0.5f + 0.5f); B[o + 2] = q(t.ao); B[o + 3] = q(t.wear);
    }
  });
}

void downsample(const std::vector<uint8_t>& src, int w, int layers, std::vector<uint8_t>& dst) {
  int h = w / 2;
  dst.resize((size_t)layers * h * h * 4);
  for (int l = 0; l < layers; ++l) {
    const uint8_t* s = src.data() + (size_t)l * w * w * 4;
    uint8_t* d = dst.data() + (size_t)l * h * h * 4;
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < h; ++x)
        for (int c = 0; c < 4; ++c) {
          const uint8_t* p = s + ((size_t)(2 * y) * w + 2 * x) * 4 + c;
          d[((size_t)y * h + x) * 4 + c] = (uint8_t)((p[0] + p[4] + p[(size_t)w * 4] + p[(size_t)w * 4 + 4] + 2) / 4);
        }
  }
}

} // namespace

void buildTextures(int size, TextureSet& out) {
  int S = 256;
  while (S * 2 <= size && S < 4096) S *= 2;
  out.size = S;
  out.layers = TEX_LAYERS;
  out.mipsA.assign(1, std::vector<uint8_t>((size_t)TEX_LAYERS * S * S * 4));
  out.mipsB.assign(1, std::vector<uint8_t>((size_t)TEX_LAYERS * S * S * 4));
  static const float tile[TEX_LAYERS] = {6.0f, 1.0f, 1.0f, 4.0f};
  for (int l = 0; l < TEX_LAYERS; ++l) {
    Layer L((size_t)S * S);
    if (l == TEX_HULL) genHull(L, S);
    else if (l == TEX_ROCK) genRock(L, S);
    else if (l == TEX_ICE) genIce(L, S);
    else genIndustrial(L, S);
    encode(L, S, tile[l], out.mipsA[0].data() + (size_t)l * S * S * 4, out.mipsB[0].data() + (size_t)l * S * S * 4);
  }
  for (int w = S; w > 1; w /= 2) {
    out.mipsA.emplace_back(); out.mipsB.emplace_back();
    downsample(out.mipsA[out.mipsA.size() - 2], w, TEX_LAYERS, out.mipsA.back());
    downsample(out.mipsB[out.mipsB.size() - 2], w, TEX_LAYERS, out.mipsB.back());
  }
  out.mipCount = (int)out.mipsA.size();
}
