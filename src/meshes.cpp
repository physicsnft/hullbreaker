#include "meshes.h"
#include <map>

float g_meshRadius[MESH_COUNT];

namespace {

uint32_t hashI(int x, int y, int z, uint32_t s) {
  uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)z * 2147483647u + s * 362437u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}
float vnoise(const Vec3& p, uint32_t seed) {
  int xi = (int)std::floor(p.x), yi = (int)std::floor(p.y), zi = (int)std::floor(p.z);
  float fx = p.x - xi, fy = p.y - yi, fz = p.z - zi;
  fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy); fz = fz * fz * (3 - 2 * fz);
  auto h = [&](int dx, int dy, int dz) { return (float)(hashI(xi + dx, yi + dy, zi + dz, seed) & 0xFFFFFF) / 16777215.0f; };
  float a = lerpf(lerpf(h(0, 0, 0), h(1, 0, 0), fx), lerpf(h(0, 1, 0), h(1, 1, 0), fx), fy);
  float b = lerpf(lerpf(h(0, 0, 1), h(1, 0, 1), fx), lerpf(h(0, 1, 1), h(1, 1, 1), fx), fy);
  return lerpf(a, b, fz);
}
float fbm3(Vec3 p, uint32_t seed, int oct) {
  float a = 0.5f, s = 0, n = 0;
  for (int i = 0; i < oct; ++i) {
    s += a * vnoise(p, seed + i * 17);
    n += a;
    p = p * 2.03f + Vec3(13.1f, 7.7f, 3.3f);
    a *= 0.5f;
  }
  return s / n;
}

// Mesh builder. Winding is fixed automatically: every face is oriented away from `center`,
// which works because all meshes here are star-shaped around it.
struct MB {
  MeshData m;
  Vec3 center{0, 0, 0};
  void push(const Vec3& p, const Vec3& n) {
    m.idx.push_back((uint32_t)m.verts.size());
    m.verts.push_back({p.x, p.y, p.z, n.x, n.y, n.z});
  }
  void tri(Vec3 a, Vec3 b, Vec3 c) { // flat shaded
    Vec3 n = normalize(cross(b - a, c - a), Vec3(0, 1, 0));
    Vec3 ctr = (a + b + c) / 3.0f - center;
    if (dot(n, ctr) < 0) { std::swap(b, c); n = -n; }
    push(a, n); push(b, n); push(c, n);
  }
  void quad(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d) { tri(a, b, c); tri(a, c, d); }
  void triS(Vec3 a, Vec3 na, Vec3 b, Vec3 nb, Vec3 c, Vec3 nc) { // smooth shaded
    Vec3 n = cross(b - a, c - a);
    Vec3 ctr = (a + b + c) / 3.0f - center;
    if (dot(n, ctr) < 0) { std::swap(b, c); std::swap(nb, nc); }
    push(a, normalize(na)); push(b, normalize(nb)); push(c, normalize(nc));
  }
};

MeshData makeBox() {
  MB b;
  const float h = 0.5f;
  Vec3 v[8] = {{-h, -h, -h}, {h, -h, -h}, {h, h, -h}, {-h, h, -h}, {-h, -h, h}, {h, -h, h}, {h, h, h}, {-h, h, h}};
  b.quad(v[0], v[1], v[2], v[3]); b.quad(v[4], v[5], v[6], v[7]);
  b.quad(v[0], v[1], v[5], v[4]); b.quad(v[3], v[2], v[6], v[7]);
  b.quad(v[0], v[3], v[7], v[4]); b.quad(v[1], v[2], v[6], v[5]);
  return b.m;
}
[[maybe_unused]] MeshData makeTaper() {
  MB b;
  const float h = 0.5f, f = 0.2f;
  Vec3 v[8] = {{-f, -f, -h}, {f, -f, -h}, {f, f, -h}, {-f, f, -h}, {-h, -h, h}, {h, -h, h}, {h, h, h}, {-h, h, h}};
  b.quad(v[0], v[1], v[2], v[3]); b.quad(v[4], v[5], v[6], v[7]);
  b.quad(v[0], v[1], v[5], v[4]); b.quad(v[3], v[2], v[6], v[7]);
  b.quad(v[0], v[3], v[7], v[4]); b.quad(v[1], v[2], v[6], v[5]);
  return b.m;
}
MeshData makeWedge() {
  MB b;
  b.center = Vec3(0, -1.0f / 6, 1.0f / 6);
  const float h = 0.5f;
  Vec3 a0(-h, -h, h), a1(-h, h, h), a2(-h, -h, -h); // left triangle
  Vec3 b0(h, -h, h), b1(h, h, h), b2(h, -h, -h);    // right triangle
  b.tri(a0, a1, a2); b.tri(b0, b1, b2);
  b.quad(a0, b0, b1, a1); // back
  b.quad(a0, b0, b2, a2); // bottom
  b.quad(a1, b1, b2, a2); // slope
  return b.m;
}
MeshData makeCyl(int n, bool smooth) {
  MB b;
  for (int i = 0; i < n; ++i) {
    float a0 = 2 * PI * i / n, a1 = 2 * PI * (i + 1) / n;
    Vec3 n0(std::cos(a0), std::sin(a0), 0), n1(std::cos(a1), std::sin(a1), 0);
    Vec3 p0 = n0 * 0.5f, p1 = n1 * 0.5f;
    Vec3 f0 = p0 + Vec3(0, 0, -0.5f), f1 = p1 + Vec3(0, 0, -0.5f), k0 = p0 + Vec3(0, 0, 0.5f), k1 = p1 + Vec3(0, 0, 0.5f);
    if (smooth) { b.triS(f0, n0, f1, n1, k1, n1); b.triS(f0, n0, k1, n1, k0, n0); }
    else b.quad(f0, f1, k1, k0);
    b.tri(Vec3(0, 0, -0.5f), f0, f1);
    b.tri(Vec3(0, 0, 0.5f), k0, k1);
  }
  return b.m;
}
MeshData makeCone(int n) {
  MB b;
  b.center = Vec3(0, 0, 0.2f);
  Vec3 tip(0, 0, -0.5f);
  for (int i = 0; i < n; ++i) {
    float a0 = 2 * PI * i / n, a1 = 2 * PI * (i + 1) / n, am = (a0 + a1) * 0.5f;
    Vec3 n0(std::cos(a0), std::sin(a0), -0.5f), n1(std::cos(a1), std::sin(a1), -0.5f), nm(std::cos(am), std::sin(am), -0.5f);
    Vec3 p0(std::cos(a0) * 0.5f, std::sin(a0) * 0.5f, 0.5f), p1(std::cos(a1) * 0.5f, std::sin(a1) * 0.5f, 0.5f);
    b.triS(tip, nm, p0, n0, p1, n1);
    b.tri(Vec3(0, 0, 0.5f), p0, p1);
  }
  return b.m;
}
[[maybe_unused]] MeshData makeWing(bool right) {
  MB b;
  b.center = Vec3(0, 0, 0.08f);
  float s = right ? 1.0f : -1.0f;
  // root (inboard) and tip (outboard) sections; leading edge swept back, thickness tapers
  Vec3 r0(-0.5f * s, -0.5f, -0.5f), r1(-0.5f * s, 0.5f, -0.5f), r2(-0.5f * s, 0.5f, 0.5f), r3(-0.5f * s, -0.5f, 0.5f);
  Vec3 t0(0.5f * s, -0.3f, -0.05f), t1(0.5f * s, 0.3f, -0.05f), t2(0.5f * s, 0.3f, 0.4f), t3(0.5f * s, -0.3f, 0.4f);
  b.quad(r0, r1, r2, r3); b.quad(t0, t1, t2, t3);
  b.quad(r1, r2, t2, t1); b.quad(r0, r3, t3, t0);
  b.quad(r0, r1, t1, t0); b.quad(r3, r2, t2, t3);
  return b.m;
}
MeshData makeCrystal() {
  MB b;
  Vec3 top(0, 0.5f, 0), bot(0, -0.5f, 0);
  for (int i = 0; i < 6; ++i) {
    float a0 = 2 * PI * i / 6, a1 = 2 * PI * (i + 1) / 6;
    Vec3 p0(std::cos(a0) * 0.3f, 0.12f, std::sin(a0) * 0.3f), p1(std::cos(a1) * 0.3f, 0.12f, std::sin(a1) * 0.3f);
    Vec3 q0(std::cos(a0) * 0.22f, -0.2f, std::sin(a0) * 0.22f), q1(std::cos(a1) * 0.22f, -0.2f, std::sin(a1) * 0.22f);
    b.tri(top, p0, p1);
    b.quad(p0, p1, q1, q0);
    b.tri(bot, q0, q1);
  }
  return b.m;
}

void icosphere(int level, std::vector<Vec3>& v, std::vector<uint32_t>& idx) {
  const float t = (1.0f + std::sqrt(5.0f)) * 0.5f;
  Vec3 base[12] = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t},
                   {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
  static const uint32_t f[60] = {0, 11, 5, 0, 5, 1, 0, 1, 7, 0, 7, 10, 0, 10, 11, 1, 5, 9, 5, 11, 4, 11, 10, 2, 10, 7, 6, 7, 1, 8,
                                 3, 9, 4, 3, 4, 2, 3, 2, 6, 3, 6, 8, 3, 8, 9, 4, 9, 5, 2, 4, 11, 6, 2, 10, 8, 6, 7, 9, 8, 1};
  v.clear();
  for (auto& b : base) v.push_back(normalize(b));
  idx.assign(f, f + 60);
  for (int l = 0; l < level; ++l) {
    std::map<uint64_t, uint32_t> cache;
    auto mid = [&](uint32_t a, uint32_t b) {
      uint64_t key = a < b ? ((uint64_t)a << 32) | b : ((uint64_t)b << 32) | a;
      auto it = cache.find(key);
      if (it != cache.end()) return it->second;
      v.push_back(normalize((v[a] + v[b]) * 0.5f));
      return cache[key] = (uint32_t)v.size() - 1;
    };
    std::vector<uint32_t> n;
    for (size_t i = 0; i < idx.size(); i += 3) {
      uint32_t a = idx[i], b = idx[i + 1], c = idx[i + 2];
      uint32_t ab = mid(a, b), bc = mid(b, c), ca = mid(c, a);
      uint32_t t4[12] = {a, ab, ca, b, bc, ab, c, ca, bc, ab, bc, ca};
      n.insert(n.end(), t4, t4 + 12);
    }
    idx.swap(n);
  }
}

MeshData smoothFromIndexed(std::vector<Vec3>& p, std::vector<uint32_t>& idx) {
  std::vector<Vec3> nrm(p.size());
  for (size_t i = 0; i < idx.size(); i += 3) {
    Vec3 a = p[idx[i]], b = p[idx[i + 1]], c = p[idx[i + 2]];
    Vec3 n = cross(b - a, c - a);
    if (dot(n, a + b + c) < 0) { std::swap(idx[i + 1], idx[i + 2]); n = -n; }
    nrm[idx[i]] += n; nrm[idx[i + 1]] += n; nrm[idx[i + 2]] += n;
  }
  MeshData m;
  for (size_t i = 0; i < p.size(); ++i) {
    Vec3 n = normalize(nrm[i], normalize(p[i]));
    m.verts.push_back({p[i].x, p[i].y, p[i].z, n.x, n.y, n.z});
  }
  m.idx = idx;
  return m;
}

MeshData makeSphere(int level) {
  std::vector<Vec3> v; std::vector<uint32_t> idx;
  icosphere(level, v, idx);
  for (auto& p : v) p = p * 0.5f;
  return smoothFromIndexed(v, idx);
}

MeshData makeAsteroid(uint32_t seed, int level, float& meanRadius) {
  std::vector<Vec3> v; std::vector<uint32_t> idx;
  icosphere(level, v, idx);
  Rng r(seed * 977 + 5);
  struct Crater { Vec3 c; float a, depth; };
  Crater cr[7];
  for (auto& c : cr) { c.c = r.unit(); c.a = r.range(0.25f, 0.65f); c.depth = r.range(0.04f, 0.13f); }
  Vec3 squash(r.range(0.72f, 1.0f), r.range(0.62f, 1.0f), r.range(0.72f, 1.0f));
  float maxR = 0;
  for (auto& d : v) {
    float rad = 0.62f + 0.55f * fbm3(d * 1.6f + Vec3((float)seed * 3.1f), seed, 4);
    rad += 0.10f * (fbm3(d * 5.5f, seed + 99, 3) - 0.5f);
    for (auto& c : cr) {
      float ang = std::acos(clampf(dot(d, c.c), -1, 1));
      if (ang < c.a) {
        float t = ang / c.a;
        rad -= c.depth * (1 - t * t) * 1.6f;
        rad += c.depth * 0.35f * smoothstepf(0.7f, 1.0f, t) * (1 - smoothstepf(0.9f, 1.0f, t));
      }
    }
    d = mulv(d * std::max(rad, 0.25f), squash);
    maxR = std::max(maxR, length(d));
  }
  float sum = 0;
  for (auto& d : v) { d = d * (0.5f / maxR); sum += length(d); }
  meanRadius = sum / (float)v.size();
  return smoothFromIndexed(v, idx);
}

// ---- smooth hull shapes -------------------------------------------------------------
// A loft sweeps a superellipse cross-section along Z. Exponent 2 is an ellipse, higher
// values approach a rectangle with rounded corners. Rings share vertices, so the skin is
// smooth-shaded; the end caps get their own vertices so the rims stay crisp.
struct Station { float z, w, h, y, n; };
MeshData makeLoft(const std::vector<Station>& st, int segs = 32) {
  std::vector<Vec3> p; std::vector<uint32_t> idx;
  auto ring = [&](const Station& s) {
    uint32_t base = (uint32_t)p.size();
    for (int i = 0; i < segs; ++i) {
      float a = 2 * PI * (i + 0.5f) / segs, c = std::cos(a), sn = std::sin(a), e = 2.0f / s.n;
      p.push_back(Vec3(s.w * (c < 0 ? -1.0f : 1.0f) * std::pow(std::fabs(c), e), s.y + s.h * (sn < 0 ? -1.0f : 1.0f) * std::pow(std::fabs(sn), e), s.z));
    }
    return base;
  };
  std::vector<uint32_t> rings;
  for (auto& s : st) rings.push_back(ring(s));
  for (size_t k = 0; k + 1 < rings.size(); ++k)
    for (int i = 0; i < segs; ++i) {
      uint32_t a = rings[k] + i, b = rings[k] + (i + 1) % segs, c = rings[k + 1] + (i + 1) % segs, d = rings[k + 1] + i;
      uint32_t q[6] = {a, b, c, a, c, d};
      idx.insert(idx.end(), q, q + 6);
    }
  for (size_t end : {(size_t)0, st.size() - 1}) {
    uint32_t base = ring(st[end]), centre = (uint32_t)p.size();
    p.push_back(Vec3(0, st[end].y, st[end].z));
    for (int i = 0; i < segs; ++i) { idx.push_back(centre); idx.push_back(base + i); idx.push_back(base + (i + 1) % segs); }
  }
  return smoothFromIndexed(p, idx);
}
// A blade is a tapered, swept surface with an airfoil section: rounded leading edge, sharp
// trailing edge. mode 0: wing with its root at -X, 1: mirrored, 2: upright fin with its root at -Y.
MeshData makeBlade(float rootZ0, float rootZ1, float tipZ0, float tipZ1, float rootT, float tipT, int mode) {
  const int C = 14, S = 4;
  std::vector<Vec3> p; std::vector<uint32_t> idx;
  auto place = [&](float s, float th, float z) { return mode == 0 ? Vec3(s, th, z) : (mode == 1 ? Vec3(-s, th, z) : Vec3(th, s, z)); };
  auto ring = [&](float t) {
    uint32_t base = (uint32_t)p.size();
    float z0 = lerpf(rootZ0, tipZ0, t), z1 = lerpf(rootZ1, tipZ1, t), T = lerpf(rootT, tipT, t), s = t - 0.5f;
    for (int k = 0; k < 2 * C; ++k) {
      float u = k <= C ? (float)k / C : (float)(2 * C - k) / C; // out along the top, back along the bottom
      float side = k <= C ? 1.0f : -1.0f;
      float uu = u * u * (3 - 2 * u) * 0.35f + u * 0.65f;        // a few more samples near the edges
      float th = T * 2.598f * std::sqrt(uu) * (1 - uu);
      p.push_back(place(s, th * side, lerpf(z0, z1, uu)));
    }
    return base;
  };
  std::vector<uint32_t> rings;
  for (int i = 0; i <= S; ++i) rings.push_back(ring((float)i / S));
  const int R = 2 * C;
  for (int k = 0; k < S; ++k)
    for (int i = 0; i < R; ++i) {
      uint32_t a = rings[k] + i, b = rings[k] + (i + 1) % R, c = rings[k + 1] + (i + 1) % R, d = rings[k + 1] + i;
      uint32_t q[6] = {a, b, c, a, c, d};
      idx.insert(idx.end(), q, q + 6);
    }
  for (float t : {0.0f, 1.0f}) {
    uint32_t base = ring(t), centre = (uint32_t)p.size();
    p.push_back(place(t - 0.5f, 0, (lerpf(rootZ0, tipZ0, t) + lerpf(rootZ1, tipZ1, t)) * 0.5f));
    for (int i = 0; i < R; ++i) { idx.push_back(centre); idx.push_back(base + i); idx.push_back(base + (i + 1) % R); }
  }
  return smoothFromIndexed(p, idx);
}
MeshData makeHullTaper() { // full section at +Z easing down to 40% at -Z
  std::vector<Station> st;
  for (int i = 0; i <= 10; ++i) {
    float t = i / 10.0f, k = lerpf(1.0f, 0.4f, t * t * (3 - 2 * t));
    st.push_back({0.5f - t, 0.5f * k, 0.5f * k, 0, 4.0f});
  }
  return makeLoft(st);
}
MeshData makeNose() { // ogive: tangent to the fuselage at its base, closing to a point
  std::vector<Station> st;
  for (int i = 0; i <= 12; ++i) {
    float t = i / 12.0f, r = 0.5f * std::sqrt(std::max(0.0f, 1 - std::pow(t, 1.7f))) * (1 - 0.25f * t);
    st.push_back({0.5f - t, std::max(r, 0.012f), std::max(r, 0.012f), 0, lerpf(4.0f, 2.2f, t)});
  }
  return makeLoft(st);
}
MeshData makePrism(float n, float chamfer) { // rounded-corner bar with eased ends
  std::vector<Station> st;
  const float c = chamfer;
  static const float e[4][2] = {{0.0f, 1.0f}, {0.3f, 0.55f}, {0.7f, 0.2f}, {1.0f, 0.0f}};
  for (auto& k : e) st.push_back({-0.5f + c * k[0], 0.5f - c * k[1] * 0.6f, 0.5f - c * k[1] * 0.6f, 0, n});
  for (int i = 3; i >= 0; --i) st.push_back({0.5f - c * e[i][0], 0.5f - c * e[i][1] * 0.6f, 0.5f - c * e[i][1] * 0.6f, 0, n});
  return makeLoft(st);
}
MeshData makeNacelle() {
  static const float pr[9][2] = {{-0.5f, 0.40f}, {-0.46f, 0.455f}, {-0.38f, 0.49f}, {-0.2f, 0.5f}, {0.15f, 0.5f}, {0.32f, 0.485f}, {0.42f, 0.46f}, {0.48f, 0.43f}, {0.5f, 0.40f}};
  std::vector<Station> st;
  for (auto& k : pr) st.push_back({k[0], k[1], k[1], 0, 2.0f});
  return makeLoft(st);
}
MeshData makeNozzle() { // bell, narrow at the throat (-Z), flaring aft
  std::vector<Station> st;
  for (int i = 0; i <= 8; ++i) {
    float t = i / 8.0f, r = 0.27f + 0.23f * std::sin(t * PI * 0.5f);
    st.push_back({-0.5f + t, r, r, 0, 2.0f});
  }
  return makeLoft(st);
}

// =====================================================================================
// The fighter's own meshes. They are modelled in ship coordinates (metres; -Z forward,
// +Y up), then each is normalised into the unit cube. shipMeshInfo() gives the box each
// one occupies on the ship, which is where the matching part is placed.
// =====================================================================================
struct SMesh {
  std::vector<Vec3> p, n;
  std::vector<uint32_t> idx;
  uint32_t v(Vec3 a, Vec3 normal = Vec3()) { p.push_back(a); n.push_back(normal); return (uint32_t)p.size() - 1; }
  // `out` is roughly the outward direction; it fixes the winding. With `accumulate` the
  // face normal is added to its vertices (smooth shading across shared vertices).
  void tri(uint32_t a, uint32_t b, uint32_t c, Vec3 out, bool accumulate = true) {
    Vec3 g = cross(p[b] - p[a], p[c] - p[a]);
    if (dot(g, out) < 0) { std::swap(b, c); g = -g; }
    if (accumulate) { n[a] += g; n[b] += g; n[c] += g; }
    idx.push_back(a); idx.push_back(b); idx.push_back(c);
  }
  void quad(uint32_t a, uint32_t b, uint32_t c, uint32_t d, Vec3 out, bool accumulate = true) { tri(a, b, c, out, accumulate); tri(a, c, d, out, accumulate); }
  MeshData finish(ShipMeshInfo& info) {
    Vec3 lo(1e9f), hi(-1e9f);
    for (auto& q : p) for (int a = 0; a < 3; ++a) { lo[a] = std::min(lo[a], q[a]); hi[a] = std::max(hi[a], q[a]); }
    info.center = (lo + hi) * 0.5f;
    info.size = hi - lo;
    for (int a = 0; a < 3; ++a) info.size[a] = std::max(info.size[a], 0.01f);
    MeshData m;
    for (size_t i = 0; i < p.size(); ++i) {
      Vec3 u = divv(p[i] - info.center, info.size);
      Vec3 nn = normalize(mulv(normalize(n[i], Vec3(0, 1, 0)), info.size), Vec3(0, 1, 0)); // normals scale inversely to positions
      m.verts.push_back({u.x, u.y, u.z, nn.x, nn.y, nn.z});
    }
    m.idx = idx;
    return m;
  }
};

// ---- fuselage master shape: cross-sections at control stations, interpolated smoothly
struct Sec { float z, w, ht, hb, yc, n; };
const Sec kFuse[] = {
    {-10.6f, 0.02f, 0.02f, 0.02f, -0.08f, 2.0f}, {-10.0f, 0.15f, 0.13f, 0.12f, -0.08f, 2.2f}, {-9.0f, 0.31f, 0.27f, 0.23f, -0.07f, 2.4f},
    {-7.5f, 0.53f, 0.43f, 0.37f, -0.05f, 2.6f},  {-6.0f, 0.75f, 0.58f, 0.50f, -0.02f, 2.8f},  {-5.0f, 0.89f, 0.72f, 0.58f, 0.0f, 2.8f},
    {-4.0f, 1.00f, 0.86f, 0.66f, 0.0f, 2.7f},    {-3.0f, 1.09f, 0.92f, 0.73f, 0.0f, 2.7f},    {-2.0f, 1.17f, 0.92f, 0.79f, 0.0f, 3.0f},
    {-1.0f, 1.25f, 0.89f, 0.83f, 0.0f, 3.3f},    {0.0f, 1.31f, 0.87f, 0.86f, 0.0f, 3.5f},     {2.0f, 1.37f, 0.85f, 0.86f, 0.0f, 3.5f},
    {3.5f, 1.40f, 0.83f, 0.82f, 0.02f, 3.3f},    {5.0f, 1.30f, 0.75f, 0.72f, 0.05f, 3.0f},    {6.2f, 1.00f, 0.58f, 0.54f, 0.10f, 2.6f},
    {7.2f, 0.60f, 0.36f, 0.30f, 0.14f, 2.3f},    {7.7f, 0.30f, 0.18f, 0.14f, 0.16f, 2.0f}};
const int kFuseN = sizeof(kFuse) / sizeof(kFuse[0]);
Sec fuse(float z) {
  int i = 0;
  while (i < kFuseN - 2 && z > kFuse[i + 1].z) ++i;
  const Sec &a = kFuse[std::max(i - 1, 0)], &b = kFuse[i], &c = kFuse[i + 1], &d = kFuse[std::min(i + 2, kFuseN - 1)];
  float t = clampf((z - b.z) / (c.z - b.z), 0.0f, 1.0f);
  auto cr = [&](float Sec::*f) { // Catmull-Rom with tangents scaled for uneven spacing
    float m0 = (c.*f - a.*f) / std::max(c.z - a.z, 1e-3f) * (c.z - b.z), m1 = (d.*f - b.*f) / std::max(d.z - b.z, 1e-3f) * (c.z - b.z);
    float t2 = t * t, t3 = t2 * t;
    return (2 * t3 - 3 * t2 + 1) * (b.*f) + (t3 - 2 * t2 + t) * m0 + (-2 * t3 + 3 * t2) * (c.*f) + (t3 - t2) * m1;
  };
  Sec s{z, std::max(cr(&Sec::w), 0.02f), std::max(cr(&Sec::ht), 0.02f), std::max(cr(&Sec::hb), 0.02f), cr(&Sec::yc), std::max(cr(&Sec::n), 2.0f)};
  return s;
}
Vec3 secPoint(const Sec& s, float ang, float grow = 0) {
  float c = std::cos(ang), sn = std::sin(ang), e = 2.0f / s.n;
  Vec3 q(s.w * (c < 0 ? -1.0f : 1.0f) * std::pow(std::fabs(c), e), (sn >= 0 ? s.ht : s.hb) * (sn < 0 ? -1.0f : 1.0f) * std::pow(std::fabs(sn), e), 0);
  if (grow != 0) q += normalize(Vec3(q.x / (s.w * s.w), q.y / sqr(sn >= 0 ? s.ht : s.hb), 0), Vec3(0, 1, 0)) * grow;
  return Vec3(q.x, q.y + s.yc, s.z);
}
// a closed body lofted through sections given by `sec`, between z0 and z1
template <class F>
MeshData loftBody(F sec, float z0, float z1, ShipMeshInfo& info, int segs = 56, float dz = 0.16f) {
  SMesh m;
  int rings = std::max(2, (int)std::ceil((z1 - z0) / dz) + 1);
  std::vector<uint32_t> base;
  for (int k = 0; k < rings; ++k) {
    Sec s = sec(lerpf(z0, z1, (float)k / (rings - 1)));
    base.push_back((uint32_t)m.p.size());
    for (int i = 0; i < segs; ++i) m.v(secPoint(s, 2 * PI * (i + 0.5f) / segs));
  }
  for (int k = 0; k + 1 < rings; ++k)
    for (int i = 0; i < segs; ++i) {
      uint32_t a = base[k] + i, b = base[k] + (i + 1) % segs, c = base[k + 1] + (i + 1) % segs, d = base[k + 1] + i;
      Sec s = sec(lerpf(z0, z1, (k + 0.5f) / (rings - 1)));
      Vec3 out = (m.p[a] + m.p[c]) * 0.5f - Vec3(0, s.yc, (m.p[a].z + m.p[c].z) * 0.5f);
      m.quad(a, b, c, d, out);
    }
  for (int end = 0; end < 2; ++end) { // caps: only seen when the neighbouring section has been shot away
    Sec s = sec(end ? z1 : z0);
    Vec3 nrm(0, 0, end ? 1.0f : -1.0f);
    uint32_t first = (uint32_t)m.p.size();
    for (int i = 0; i < segs; ++i) m.v(secPoint(s, 2 * PI * (i + 0.5f) / segs), nrm);
    uint32_t centre = m.v(Vec3(0, s.yc, s.z), nrm);
    for (int i = 0; i < segs; ++i) m.tri(centre, first + i, first + (i + 1) % segs, nrm, false);
  }
  return m.finish(info);
}
// an armour plate that follows the hull: the master surface between two angles, raised by `thick`
MeshData hullShell(float a0deg, float a1deg, float z0, float z1, float thick, ShipMeshInfo& info) {
  SMesh m;
  const float a0 = radians(a0deg), a1 = radians(a1deg);
  const int na = std::max(4, (int)((a1deg - a0deg) / 4.0f)), nz = std::max(2, (int)((z1 - z0) / 0.16f));
  const float bevel = 0.012f; // the plate's edges slope down to the hull
  auto at = [&](int i, int k, float grow, float inset) {
    float ta = (float)i / na, tz = (float)k / nz;
    float a = lerpf(a0 + inset * 0.012f, a1 - inset * 0.012f, ta), z = lerpf(z0 + inset * bevel, z1 - inset * bevel, tz);
    return secPoint(fuse(z), a, grow);
  };
  std::vector<uint32_t> top((size_t)(na + 1) * (nz + 1));
  for (int k = 0; k <= nz; ++k)
    for (int i = 0; i <= na; ++i) top[(size_t)k * (na + 1) + i] = m.v(at(i, k, thick, 1));
  auto outAt = [&](Vec3 q) { Sec s = fuse(q.z); return q - Vec3(0, s.yc, q.z); };
  for (int k = 0; k < nz; ++k)
    for (int i = 0; i < na; ++i) {
      uint32_t a = top[(size_t)k * (na + 1) + i], b = top[(size_t)k * (na + 1) + i + 1], c = top[(size_t)(k + 1) * (na + 1) + i + 1], d = top[(size_t)(k + 1) * (na + 1) + i];
      m.quad(a, b, c, d, outAt(m.p[a]));
    }
  // bevelled rim, with its own vertices so the edge reads as a crisp step
  auto rim = [&](int i0, int k0, int i1, int k1, Vec3 side) {
    Vec3 pa = at(i0, k0, thick, 1), pb = at(i1, k1, thick, 1), qa = at(i0, k0, -0.01f, 0), qb = at(i1, k1, -0.01f, 0);
    Vec3 out = normalize(outAt((pa + pb) * 0.5f)) + side * 1.5f;
    uint32_t a = m.v(pa), b = m.v(pb), c = m.v(qb), d = m.v(qa);
    m.quad(a, b, c, d, out);
  };
  Vec3 mid = at(na / 2, nz / 2, 0, 0);
  for (int i = 0; i < na; ++i) { rim(i, 0, i + 1, 0, Vec3(0, 0, -1)); rim(i, nz, i + 1, nz, Vec3(0, 0, 1)); }
  for (int k = 0; k < nz; ++k) {
    rim(0, k, 0, k + 1, normalize(at(0, k, 0, 0) - mid));
    rim(na, k, na, k + 1, normalize(at(na, k, 0, 0) - mid));
  }
  return m.finish(info);
}
// A surface of revolution about the Z axis through a profile of (z, radius) points. The
// surface faces left of the direction of travel, so an outer skin is listed front to back
// and an inner wall back to front: one profile can run out along a nozzle, round its lip
// and back down the inside, which is how the hollow intakes and nozzles are made.
MeshData lathe(const std::vector<Vec3>& prof, ShipMeshInfo& info, float capZ = 1e9f, float capR = 0, float capDir = 0, int segs = 48) {
  SMesh m;
  std::vector<Vec3> sn; // per profile segment: (radial, 0, axial) normal
  for (size_t k = 0; k + 1 < prof.size(); ++k) {
    float dz = prof[k + 1].x - prof[k].x, dr = prof[k + 1].y - prof[k].y, l = std::max(std::sqrt(dz * dz + dr * dr), 1e-6f);
    sn.push_back(Vec3(dz / l, 0, -dr / l));
  }
  for (size_t k = 0; k + 1 < prof.size(); ++k) {
    Vec3 n0 = sn[k], n1 = sn[k];
    if (k > 0 && dot(sn[k - 1], sn[k]) > 0.6f) n0 = normalize(sn[k - 1] + sn[k]);
    if (k + 2 < prof.size() && dot(sn[k + 1], sn[k]) > 0.6f) n1 = normalize(sn[k + 1] + sn[k]);
    uint32_t base = (uint32_t)m.p.size();
    for (int i = 0; i <= segs; ++i) {
      float a = 2 * PI * i / segs, c = std::cos(a), s = std::sin(a);
      m.v(Vec3(prof[k].y * c, prof[k].y * s, prof[k].x), Vec3(n0.x * c, n0.x * s, n0.z));
      m.v(Vec3(prof[k + 1].y * c, prof[k + 1].y * s, prof[k + 1].x), Vec3(n1.x * c, n1.x * s, n1.z));
    }
    for (int i = 0; i < segs; ++i) {
      float a = 2 * PI * (i + 0.5f) / segs;
      Vec3 out(sn[k].x * std::cos(a), sn[k].x * std::sin(a), sn[k].z);
      m.quad(base + i * 2, base + i * 2 + 1, base + i * 2 + 3, base + i * 2 + 2, out, false);
    }
  }
  if (capR > 0) { // a flat disc across the bore (compressor face, bulkhead)
    Vec3 nrm(0, 0, capDir);
    uint32_t centre = m.v(Vec3(0, 0, capZ), nrm), first = (uint32_t)m.p.size();
    for (int i = 0; i <= segs; ++i) m.v(Vec3(capR * std::cos(2 * PI * i / segs), capR * std::sin(2 * PI * i / segs), capZ), nrm);
    for (int i = 0; i < segs; ++i) m.tri(centre, first + i, first + i + 1, nrm, false);
  }
  return m.finish(info);
}

ShipMeshInfo g_info[MESH_COUNT];
bool g_infoReady = false;

void buildShipMeshes(std::vector<MeshData>& out) {
  // fuselage, cut into the sections that can be shot away separately
  static const float cuts[6] = {-10.6f, -7.4f, -2.4f, 3.0f, 6.0f, 7.7f};
  for (int i = 0; i < 5; ++i) out[M_S_NOSE + i] = loftBody(fuse, cuts[i], cuts[i + 1], g_info[M_S_NOSE + i]);
  // canopy: a teardrop bubble sunk into the hull, and the coaming it sits in
  auto bubble = [](float z, float grow, float low) {
    Sec f = fuse(z);
    float t = clampf((z + 5.75f) / 3.5f, 0.0f, 1.0f), shape = std::pow(std::sin(PI * std::pow(t, 0.72f)), 0.62f);
    Sec s{z, std::max(0.56f * shape * grow + (grow > 1 ? 0.04f : 0.0f), 0.02f), std::max(0.66f * shape * low, 0.02f), 0.12f, f.yc + f.ht - 0.14f, grow > 1 ? 3.0f : 2.15f};
    return s;
  };
  out[M_S_CANOPY] = loftBody([&](float z) { return bubble(z, 1.0f, 1.0f); }, -5.75f, -2.25f, g_info[M_S_CANOPY], 40, 0.1f);
  out[M_S_SILL] = loftBody([&](float z) { return bubble(z, 1.14f, 0.3f); }, -5.75f, -2.25f, g_info[M_S_SILL], 40, 0.1f);
  // dorsal spine fairing running aft from the canopy
  out[M_S_SPINE] = loftBody([](float z) {
    Sec f = fuse(z);
    float t = clampf((z + 2.5f) / 8.6f, 0.0f, 1.0f), ends = std::pow(std::sin(PI * clampf(t * 0.94f + 0.03f, 0.0f, 1.0f)), 0.4f);
    Sec s{z, std::max(lerpf(0.40f, 0.20f, t) * ends, 0.02f), std::max(lerpf(0.44f, 0.14f, t) * ends, 0.02f), 0.12f, f.yc + f.ht - 0.13f, 2.6f};
    return s;
  }, -2.5f, 6.1f, g_info[M_S_SPINE], 32, 0.2f);
  // skin plates: the whole fuselage surface, cut into bands and sectors
  for (int band = 0; band < SKIN_BANDS; ++band)
    for (int sec = 0; sec < SKIN_SECTORS; ++sec) {
      if (!skinPlateExists(band, sec)) continue;
      float z0 = lerpf(-9.6f, 7.4f, (float)band / SKIN_BANDS), z1 = lerpf(-9.6f, 7.4f, (float)(band + 1) / SKIN_BANDS);
      int id = M_S_ARMOR0 + band * SKIN_SECTORS + sec;
      out[id] = hullShell(-22.5f + 45.0f * sec + 0.35f, 22.5f + 45.0f * sec - 0.35f, z0 + 0.008f, z1 - 0.008f, 0.035f, g_info[id]);
    }
  // weapon and fitting shapes (unit meshes; the bounding boxes are not needed)
  ShipMeshInfo unused;
  out[M_TUBE] = lathe({Vec3(0.45f, 0.36f, 0), Vec3(-0.5f, 0.36f, 0), Vec3(-0.5f, 0.5f, 0), Vec3(0.5f, 0.5f, 0), Vec3(0.5f, 0.02f, 0)}, unused, 0.45f, 0.36f, -1.0f, 24);
  out[M_MUZZLE] = lathe({Vec3(0.5f, 0.30f, 0), Vec3(-0.5f, 0.38f, 0), Vec3(-0.5f, 0.5f, 0), Vec3(-0.2f, 0.5f, 0), Vec3(0.1f, 0.42f, 0), Vec3(0.5f, 0.40f, 0), Vec3(0.5f, 0.30f, 0)}, unused, 1e9f, 0, 0, 24);
  {
    std::vector<Vec3> ring;
    for (int i = 0; i <= 12; ++i) { float t = -2 * PI * i / 12; ring.push_back(Vec3(0.5f * std::cos(t), 0.43f + 0.07f * std::sin(t), 0)); }
    out[M_RING] = lathe(ring, unused, 1e9f, 0, 0, 32);
  }
  out[M_POD] = lathe({Vec3(-0.5f, 0.02f, 0), Vec3(-0.44f, 0.2f, 0), Vec3(-0.34f, 0.35f, 0), Vec3(-0.18f, 0.46f, 0), Vec3(0.02f, 0.5f, 0), Vec3(0.24f, 0.48f, 0), Vec3(0.4f, 0.4f, 0), Vec3(0.5f, 0.27f, 0), Vec3(0.5f, 0.02f, 0)}, unused, 1e9f, 0, 0, 24);
  // engine pod (built on its own axis; the part is placed at the pod's position)
  out[M_S_NACELLE] = lathe({Vec3(3.15f, 0.56f, 0), Vec3(3.15f, 0.80f, 0), Vec3(3.6f, 0.855f, 0), Vec3(4.3f, 0.88f, 0), Vec3(5.2f, 0.875f, 0), Vec3(6.0f, 0.84f, 0), Vec3(6.7f, 0.78f, 0), Vec3(6.7f, 0.5f, 0)}, g_info[M_S_NACELLE]);
  out[M_S_INTAKE] = lathe({Vec3(3.2f, 0.56f, 0), Vec3(2.7f, 0.585f, 0), Vec3(2.4f, 0.61f, 0), Vec3(2.27f, 0.635f, 0), Vec3(2.2f, 0.685f, 0), Vec3(2.25f, 0.735f, 0), Vec3(2.45f, 0.775f, 0), Vec3(2.8f, 0.795f, 0), Vec3(3.2f, 0.805f, 0)},
                          g_info[M_S_INTAKE], 3.18f, 0.57f, -1.0f);
  out[M_S_NOZZLE] = lathe({Vec3(6.62f, 0.60f, 0), Vec3(6.62f, 0.79f, 0), Vec3(6.9f, 0.80f, 0), Vec3(7.3f, 0.765f, 0), Vec3(7.7f, 0.69f, 0), Vec3(8.02f, 0.615f, 0), Vec3(8.06f, 0.585f, 0), Vec3(8.0f, 0.555f, 0),
                           Vec3(7.7f, 0.50f, 0), Vec3(7.35f, 0.44f, 0), Vec3(7.1f, 0.46f, 0), Vec3(6.7f, 0.60f, 0)}, g_info[M_S_NOZZLE]);
}

} // namespace

bool skinPlateExists(int band, int sector) {
  float z0 = lerpf(-9.6f, 7.4f, (float)band / SKIN_BANDS), z1 = lerpf(-9.6f, 7.4f, (float)(band + 1) / SKIN_BANDS);
  if (sector == 2 && z1 > -5.8f && z0 < -2.2f) return false; // the canopy sits here
  return true;
}

const ShipMeshInfo& shipMeshInfo(int mesh) {
  if (!g_infoReady) { std::vector<MeshData> tmp; buildMeshes(tmp); }
  return g_info[mesh];
}

namespace {
} // namespace

void buildMeshes(std::vector<MeshData>& out) {
  out.assign(MESH_COUNT, MeshData());
  out[M_BOX] = makeBox();
  out[M_CYL] = makeCyl(24, true);
  out[M_CONE] = makeCone(24);
  out[M_SPHERE] = makeSphere(3);
  out[M_WEDGE] = makeWedge();
  out[M_TAPER] = makeHullTaper();
  out[M_PRISM] = makeCyl(6, false);
  out[M_WING_R] = makeBlade(-0.5f, 0.5f, -0.05f, 0.4f, 0.5f, 0.3f, 0);
  out[M_WING_L] = makeBlade(-0.5f, 0.5f, -0.05f, 0.4f, 0.5f, 0.3f, 1);
  out[M_RBOX] = makePrism(9.0f, 0.10f);
  out[M_SQPRISM] = makePrism(4.0f, 0.06f);
  out[M_NOSE] = makeNose();
  out[M_FIN] = makeBlade(-0.5f, 0.5f, 0.12f, 0.5f, 0.5f, 0.3f, 2);
  out[M_NACELLE] = makeNacelle();
  out[M_NOZZLE] = makeNozzle();
  buildShipMeshes(out);
  g_infoReady = true;
  out[M_CRYSTAL] = makeCrystal();
  for (int i = 0; i < MESH_COUNT; ++i) g_meshRadius[i] = 0.5f;
  g_meshRadius[M_CRYSTAL] = 0.3f;
  for (int i = 0; i < AST_VARIANTS; ++i) {
    float mr = 0.4f, mr2 = 0.4f;
    out[M_AST0 + i] = makeAsteroid(100 + i, 4, mr);
    out[M_ASTLO0 + i] = makeAsteroid(100 + i, 2, mr2);
    g_meshRadius[M_AST0 + i] = mr;
    g_meshRadius[M_ASTLO0 + i] = mr;
  }
}
