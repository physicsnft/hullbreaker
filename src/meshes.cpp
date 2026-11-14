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
MeshData makeTaper() {
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
MeshData makeWing(bool right) {
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

} // namespace

void buildMeshes(std::vector<MeshData>& out) {
  out.assign(MESH_COUNT, MeshData());
  out[M_BOX] = makeBox();
  out[M_CYL] = makeCyl(24, true);
  out[M_CONE] = makeCone(24);
  out[M_SPHERE] = makeSphere(3);
  out[M_WEDGE] = makeWedge();
  out[M_TAPER] = makeTaper();
  out[M_PRISM] = makeCyl(6, false);
  out[M_WING_R] = makeWing(true);
  out[M_WING_L] = makeWing(false);
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
