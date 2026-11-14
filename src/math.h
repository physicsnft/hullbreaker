// Small math library: vectors, quaternions, matrices, RNG.
// Conventions: right-handed world, ship-local forward = -Z, up = +Y, right = +X.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

constexpr float PI = 3.14159265358979f;

inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float sqr(float x) { return x * x; }
inline float radians(float d) { return d * PI / 180.0f; }
inline float smoothstepf(float a, float b, float x) {
  float t = clampf((x - a) / (b - a), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

struct Vec3 {
  float x = 0, y = 0, z = 0;
  Vec3() {}
  Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
  explicit Vec3(float s) : x(s), y(s), z(s) {}
  float& operator[](int i) { return i == 0 ? x : (i == 1 ? y : z); }
  float operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }
  Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
  Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
  Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
  Vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
  Vec3 operator-() const { return {-x, -y, -z}; }
  Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
  Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
  Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
};
inline Vec3 operator*(float s, const Vec3& v) { return v * s; }
inline Vec3 mulv(const Vec3& a, const Vec3& b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
inline Vec3 divv(const Vec3& a, const Vec3& b) { return {a.x / b.x, a.y / b.y, a.z / b.z}; }
inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length2(const Vec3& v) { return dot(v, v); }
inline float length(const Vec3& v) { return std::sqrt(dot(v, v)); }
inline Vec3 normalize(const Vec3& v, const Vec3& fallback = Vec3(0, 0, -1)) {
  float l = length(v);
  return l > 1e-8f ? v / l : fallback;
}
inline Vec3 lerpv(const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; }
inline Vec3 absv(const Vec3& v) { return {std::fabs(v.x), std::fabs(v.y), std::fabs(v.z)}; }

struct Quat {
  float w = 1, x = 0, y = 0, z = 0;
  Quat() {}
  Quat(float w_, float x_, float y_, float z_) : w(w_), x(x_), y(y_), z(z_) {}
};
inline Quat operator*(const Quat& a, const Quat& b) {
  return {a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
          a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
          a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
          a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w};
}
inline Quat conj(const Quat& q) { return {q.w, -q.x, -q.y, -q.z}; }
inline Quat normalize(const Quat& q) {
  float l = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
  if (l < 1e-12f) return Quat();
  return {q.w / l, q.x / l, q.y / l, q.z / l};
}
inline Vec3 rotate(const Quat& q, const Vec3& v) {
  Vec3 u(q.x, q.y, q.z);
  Vec3 t = cross(u, v) * 2.0f;
  return v + t * q.w + cross(u, t);
}
inline Quat axisAngle(const Vec3& axis, float ang) {
  float s = std::sin(ang * 0.5f);
  return {std::cos(ang * 0.5f), axis.x * s, axis.y * s, axis.z * s};
}
// pitch about X, yaw about Y, roll about Z (radians); applied roll, then pitch, then yaw
inline Quat fromEuler(float pitch, float yaw, float roll) {
  return axisAngle(Vec3(0, 1, 0), yaw) * axisAngle(Vec3(1, 0, 0), pitch) * axisAngle(Vec3(0, 0, 1), roll);
}
inline Quat nlerp(const Quat& a, const Quat& b, float t) {
  float d = a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
  float s = d < 0 ? -1.0f : 1.0f;
  return normalize(Quat(a.w + (b.w * s - a.w) * t, a.x + (b.x * s - a.x) * t,
                        a.y + (b.y * s - a.y) * t, a.z + (b.z * s - a.z) * t));
}
// integrate orientation with angular velocity expressed in the body (local) frame
inline Quat integrateLocal(const Quat& q, const Vec3& wLocal, float dt) {
  Quat d = q * Quat(0, wLocal.x, wLocal.y, wLocal.z);
  return normalize(Quat(q.w + d.w * 0.5f * dt, q.x + d.x * 0.5f * dt, q.y + d.y * 0.5f * dt, q.z + d.z * 0.5f * dt));
}
// integrate orientation with angular velocity expressed in the world frame
inline Quat integrateWorld(const Quat& q, const Vec3& w, float dt) {
  Quat d = Quat(0, w.x, w.y, w.z) * q;
  return normalize(Quat(q.w + d.w * 0.5f * dt, q.x + d.x * 0.5f * dt, q.y + d.y * 0.5f * dt, q.z + d.z * 0.5f * dt));
}
// shortest rotation taking -Z to dir
inline Quat lookRotation(const Vec3& dir, const Vec3& upHint = Vec3(0, 1, 0)) {
  Vec3 f = normalize(dir);
  Vec3 r = cross(f, upHint);
  if (length2(r) < 1e-6f) r = cross(f, Vec3(1, 0, 0));
  r = normalize(r);
  Vec3 u = cross(r, f);
  // rotation matrix columns: right, up, -forward
  float m00 = r.x, m01 = u.x, m02 = -f.x;
  float m10 = r.y, m11 = u.y, m12 = -f.y;
  float m20 = r.z, m21 = u.z, m22 = -f.z;
  float tr = m00 + m11 + m22;
  Quat q;
  if (tr > 0) {
    float s = std::sqrt(tr + 1.0f) * 2;
    q = Quat(0.25f * s, (m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s);
  } else if (m00 > m11 && m00 > m22) {
    float s = std::sqrt(1.0f + m00 - m11 - m22) * 2;
    q = Quat((m21 - m12) / s, 0.25f * s, (m01 + m10) / s, (m02 + m20) / s);
  } else if (m11 > m22) {
    float s = std::sqrt(1.0f + m11 - m00 - m22) * 2;
    q = Quat((m02 - m20) / s, (m01 + m10) / s, 0.25f * s, (m12 + m21) / s);
  } else {
    float s = std::sqrt(1.0f + m22 - m00 - m11) * 2;
    q = Quat((m10 - m01) / s, (m02 + m20) / s, (m12 + m21) / s, 0.25f * s);
  }
  return normalize(q);
}

// column-major 4x4: m[col*4+row]
struct Mat4 {
  float m[16];
};
inline Mat4 identity() {
  Mat4 r{};
  r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1;
  return r;
}
inline Mat4 operator*(const Mat4& a, const Mat4& b) {
  Mat4 r;
  for (int c = 0; c < 4; ++c)
    for (int row = 0; row < 4; ++row) {
      float s = 0;
      for (int k = 0; k < 4; ++k) s += a.m[k * 4 + row] * b.m[c * 4 + k];
      r.m[c * 4 + row] = s;
    }
  return r;
}
inline Mat4 fromTRS(const Vec3& p, const Quat& q, const Vec3& s) {
  Vec3 cx = rotate(q, Vec3(1, 0, 0)) * s.x;
  Vec3 cy = rotate(q, Vec3(0, 1, 0)) * s.y;
  Vec3 cz = rotate(q, Vec3(0, 0, 1)) * s.z;
  Mat4 r{};
  r.m[0] = cx.x; r.m[1] = cx.y; r.m[2] = cx.z;
  r.m[4] = cy.x; r.m[5] = cy.y; r.m[6] = cy.z;
  r.m[8] = cz.x; r.m[9] = cz.y; r.m[10] = cz.z;
  r.m[12] = p.x; r.m[13] = p.y; r.m[14] = p.z; r.m[15] = 1;
  return r;
}
inline Mat4 viewFrom(const Vec3& pos, const Quat& rot) {
  Vec3 r = rotate(rot, Vec3(1, 0, 0)), u = rotate(rot, Vec3(0, 1, 0)), b = rotate(rot, Vec3(0, 0, 1));
  Mat4 v{};
  v.m[0] = r.x; v.m[4] = r.y; v.m[8] = r.z;  v.m[12] = -dot(r, pos);
  v.m[1] = u.x; v.m[5] = u.y; v.m[9] = u.z;  v.m[13] = -dot(u, pos);
  v.m[2] = b.x; v.m[6] = b.y; v.m[10] = b.z; v.m[14] = -dot(b, pos);
  v.m[15] = 1;
  return v;
}
// Vulkan clip space (Y down, depth 0..1), reversed-Z: near plane -> 1, far plane -> 0
inline Mat4 perspectiveRevZ(float fovY, float aspect, float zn, float zf) {
  float f = 1.0f / std::tan(fovY * 0.5f);
  Mat4 p{};
  p.m[0] = f / aspect;
  p.m[5] = -f;
  p.m[10] = zn / (zf - zn);
  p.m[11] = -1.0f;
  p.m[14] = zn * zf / (zf - zn);
  return p;
}

struct Rng {
  uint64_t s;
  explicit Rng(uint64_t seed = 1) : s(seed * 0x9E3779B97F4A7C15ull + 0x1234567ull) {}
  uint64_t next() {
    uint64_t z = (s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
  }
  float uniform() { return (float)(next() >> 40) * (1.0f / 16777216.0f); }
  float range(float a, float b) { return a + (b - a) * uniform(); }
  int irange(int a, int b) { return a + (int)(next() % (uint64_t)(b - a + 1)); }
  bool chance(float p) { return uniform() < p; }
  Vec3 unit() {
    float z = range(-1, 1), a = range(0, 2 * PI), r = std::sqrt(std::max(0.0f, 1 - z * z));
    return {r * std::cos(a), r * std::sin(a), z};
  }
  Vec3 inSphere() { return unit() * std::cbrt(uniform()); }
  float gauss() { return (uniform() + uniform() + uniform() + uniform() - 2.0f) * 1.732f; }
};

inline Vec3 hsv(float h, float s, float v) {
  h = h - std::floor(h);
  float i = std::floor(h * 6), f = h * 6 - i;
  float p = v * (1 - s), q = v * (1 - f * s), t = v * (1 - (1 - f) * s);
  switch ((int)i % 6) {
    case 0: return {v, t, p};
    case 1: return {q, v, p};
    case 2: return {p, v, t};
    case 3: return {p, q, v};
    case 4: return {t, p, v};
    default: return {v, p, q};
  }
}
