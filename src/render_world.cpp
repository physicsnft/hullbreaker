#include "render_world.h"
#include <cstring>

namespace {
void set4(float* d, const Vec3& v, float w) { d[0] = v.x; d[1] = v.y; d[2] = v.z; d[3] = w; }

void addParticle(FrameData& fd, const Vec3& pos, float size, const float* rgba, const Vec3& axis, int kind) {
  ParticleData p;
  p.pos[0] = pos.x; p.pos[1] = pos.y; p.pos[2] = pos.z; p.size = size;
  std::memcpy(p.color, rgba, sizeof(float) * 4);
  p.axis[0] = axis.x; p.axis[1] = axis.y; p.axis[2] = axis.z; p.kind = (float)kind;
  fd.particles.push_back(p);
}
void addGlow(FrameData& fd, const Vec3& pos, float size, const Vec3& c, const Vec3& axis = Vec3(0, 0, 0), int kind = 0) {
  float rgba[4] = {c.x, c.y, c.z, 0};
  addParticle(fd, pos, size, rgba, axis, kind);
}
} // namespace

void addInstance(FrameData& fd, bool additive, int mesh, const Mat4& m, Vec3 color, float emissive, int material, float damage, float seed, float intensity) {
  InstanceData d;
  std::memcpy(d.model, m.m, sizeof(d.model));
  d.color[0] = color.x; d.color[1] = color.y; d.color[2] = color.z; d.color[3] = emissive;
  d.params[0] = (float)material; d.params[1] = damage; d.params[2] = seed; d.params[3] = intensity;
  (additive ? fd.additive : fd.opaque)[mesh].push_back(d);
}

void renderEnvironment(FrameData& fd, const Sector& s, const Camera& cam, float time, bool hangar) {
  GlobalUBO& u = fd.ubo;
  std::memset(&u, 0, sizeof(u));
  std::memcpy(u.viewProj, cam.viewProj.m, sizeof(u.viewProj));
  float tanY = std::tan(cam.fovY * 0.5f);
  set4(u.camPos, cam.pos, time);
  set4(u.camRight, cam.right, tanY * cam.aspect);
  set4(u.camUp, cam.up, tanY);
  set4(u.camFwd, cam.fwd, s.skySeed);
  set4(u.sunDir, s.sunDir, s.sunSize);
  set4(u.skySun, s.sunDir, 0.0f);
  set4(u.sunColor, s.sunColor, s.sunIntensity);
  set4(u.ambient, s.ambient, 0.0f);
  set4(u.fog, s.fogColor, s.fogDensity);
  set4(u.nebA, s.nebA, 0); set4(u.nebB, s.nebB, 0); set4(u.nebC, s.nebC, 0);
  u.nebParams[0] = s.nebDensity; u.nebParams[1] = s.nebScale;
  // far planets first so nearer ones draw over them
  std::vector<const Planet*> order;
  for (auto& p : s.planets) order.push_back(&p);
  std::sort(order.begin(), order.end(), [&](const Planet* a, const Planet* b) { return length2(a->pos - cam.pos) > length2(b->pos - cam.pos); });
  int n = std::min((int)order.size(), 4);
  for (int i = 0; i < n; ++i) {
    const Planet& p = *order[i];
    set4(u.planetPos[i], p.pos, p.radius);
    set4(u.planetColA[i], p.colA, (float)p.type);
    set4(u.planetColB[i], p.colB, p.atmo);
    u.planetParams[i][0] = p.ring ? 1.0f : 0.0f; u.planetParams[i][1] = p.tiltX; u.planetParams[i][2] = p.tiltZ; u.planetParams[i][3] = p.seed;
  }
  u.misc[0] = (float)n;
  u.misc[1] = s.exposure;
  if (hangar) { // inside the station: floodlights instead of the star; the sky is only seen through the bay door
    set4(u.sunDir, normalize(Vec3(0.25f, 0.9f, 0.4f)), s.sunSize);
    set4(u.sunColor, Vec3(1.0f, 0.97f, 0.92f), 2.0f);
    set4(u.ambient, Vec3(0.085f, 0.095f, 0.115f), 1.0f);
    set4(u.fog, Vec3(0.035f, 0.045f, 0.06f), 0.0022f);
    set4(u.nebA, lerpv(s.nebA, Vec3(0.10f, 0.22f, 0.34f), 0.75f), 0);
    set4(u.nebB, lerpv(s.nebB, Vec3(0.06f, 0.14f, 0.26f), 0.75f), 0);
    u.misc[1] = 1.0f;
  }
}

// =====================================================================================
// The station hangar. Everything is assembled from primitives each frame; the robots
// are animated from the clock alone, so there is no state to keep.
// =====================================================================================
void renderHangar(FrameData& fd, const Ship& ship, const Ship* parked, float time) {
  Rng r(77);
  const Quat I;
  const Quat up = axisAngle(Vec3(1, 0, 0), PI * 0.5f);   // turns a Z-axis cylinder upright
  const Quat side = axisAngle(Vec3(0, 1, 0), PI * 0.5f); // turns a Z-axis cylinder along X
  const Vec3 wall(0.17f, 0.18f, 0.20f), dark(0.07f, 0.075f, 0.085f), trim(0.85f, 0.55f, 0.06f), steel(0.5f, 0.52f, 0.55f);
  const Vec3 cyan(0.25f, 0.75f, 1.0f), amber(1.0f, 0.6f, 0.1f), white(1.0f, 0.96f, 0.9f);

  auto box = [&](Vec3 p, Vec3 s, Vec3 c, float metal = 0.6f, Quat q = Quat(), int mat = MAT_STRUCT) {
    addInstance(fd, false, M_BOX, fromTRS(p, q, s), c, 0, mat, 0, r.uniform(), metal);
  };
  auto cyl = [&](Vec3 p, float radius, float len, Quat q, Vec3 c, float metal = 0.9f, int mesh = M_CYL) {
    addInstance(fd, false, mesh, fromTRS(p, q, Vec3(radius * 2, radius * 2, len)), c, 0, MAT_STRUCT, 0, r.uniform(), metal);
  };
  auto seg = [&](Vec3 a, Vec3 b, float radius, Vec3 c, float metal = 0.9f) { // cylinder between two points
    Vec3 d = b - a;
    addInstance(fd, false, M_CYL, fromTRS((a + b) * 0.5f, lookRotation(d, std::fabs(normalize(d).y) > 0.95f ? Vec3(1, 0, 0) : Vec3(0, 1, 0)), Vec3(radius * 2, radius * 2, length(d))), c, 0, MAT_STRUCT, 0, r.uniform(), metal);
  };
  auto ball = [&](Vec3 p, float radius, Vec3 c, float metal = 0.9f) {
    addInstance(fd, false, M_SPHERE, fromTRS(p, I, Vec3(radius * 2)), c, 0, MAT_STRUCT, 0, r.uniform(), metal);
  };
  auto lamp = [&](Vec3 p, Vec3 s, Vec3 c, float e, int mesh = M_BOX, Quat q = Quat()) {
    addInstance(fd, false, mesh, fromTRS(p, q, s), c, e, MAT_EMISSIVE, 0, 0, 1);
  };
  auto container = [&](Vec3 p, float yaw, Vec3 c) {
    Quat q = axisAngle(Vec3(0, 1, 0), yaw);
    addInstance(fd, false, M_BOX, fromTRS(p, q, Vec3(2.5f, 2.6f, 6.1f)), c, 0, MAT_CONTAINER, 0, r.uniform(), 1);
    for (int e = -1; e <= 1; e += 2) box(p + rotate(q, Vec3(0, 0, e * 3.0f)), Vec3(2.62f, 2.72f, 0.22f), c * 0.45f, 0.5f, q);
    box(p + Vec3(0, -1.33f, 0), Vec3(2.6f, 0.14f, 6.2f), dark, 0.5f, q);
  };

  // ---------------------------------------------------------------- shell
  addInstance(fd, false, M_BOX, fromTRS(Vec3(0, -0.5f, -20), I, Vec3(100, 1, 140)), Vec3(0.21f, 0.22f, 0.24f), 0, MAT_FLOOR, 0, 0.3f, 1);
  box(Vec3(0, 26.5f, -12), Vec3(100, 1, 120), dark, 0.4f);
  for (float z = -64; z <= 40; z += 13) box(Vec3(0, 25.1f, z), Vec3(96, 1.8f, 1.0f), wall, 0.7f);
  for (float x = -24; x <= 24; x += 24) box(Vec3(x, 24.4f, -12), Vec3(1.2f, 0.9f, 116), wall, 0.7f);
  for (float x = -36; x <= 36; x += 18)
    for (float z = -56; z <= 32; z += 22) {
      lamp(Vec3(x, 23.9f, z), Vec3(1.3f, 0.22f, 11), white, 3.2f);
      box(Vec3(x, 24.1f, z), Vec3(1.9f, 0.3f, 11.6f), dark, 0.5f);
    }
  for (int sx = -1; sx <= 1; sx += 2) {
    float x = sx * 48.5f;
    box(Vec3(x, 13, -12), Vec3(1, 26, 120), wall, 0.45f);
    box(Vec3(sx * 47.8f, 1.3f, -12), Vec3(0.5f, 2.6f, 118), dark, 0.5f);
    for (float z = -62; z <= 40; z += 14.5f) {
      box(Vec3(sx * 47.3f, 13, z), Vec3(1.8f, 26, 2.4f), wall * 0.8f, 0.7f);
      lamp(Vec3(sx * 46.35f, 9, z), Vec3(0.12f, 12, 0.45f), cyan, 2.2f);
    }
    cyl(Vec3(sx * 47.2f, 5.6f, -12), 0.32f, 116, I, steel);
    cyl(Vec3(sx * 47.2f, 6.5f, -12), 0.2f, 116, I, trim * 0.8f, 0.3f);
    cyl(Vec3(sx * 47.0f, 20.5f, -12), 0.5f, 116, I, steel * 0.7f);
  }
  // catwalk along the starboard wall, control room on the port wall
  box(Vec3(44.6f, 11, -12), Vec3(5, 0.3f, 100), dark, 0.6f);
  box(Vec3(42.2f, 12.2f, -12), Vec3(0.1f, 0.1f, 100), trim, 0.4f);
  for (float z = -60; z <= 36; z += 6) box(Vec3(42.2f, 11.6f, z), Vec3(0.1f, 1.2f, 0.1f), trim, 0.4f);
  for (float z = -55; z <= 35; z += 30) box(Vec3(44.6f, 5.5f, z), Vec3(0.5f, 11, 0.5f), wall, 0.7f);
  box(Vec3(-46.6f, 15, -22), Vec3(3.2f, 7, 30), wall * 1.15f, 0.5f);
  lamp(Vec3(-44.95f, 15.6f, -22), Vec3(0.1f, 3.0f, 26), Vec3(1.0f, 0.8f, 0.45f), 1.4f);
  for (float z = -33; z <= -11; z += 5.5f) box(Vec3(-44.9f, 15.6f, z), Vec3(0.16f, 3.2f, 0.3f), dark, 0.5f);
  box(Vec3(-45.5f, 11.2f, -22), Vec3(5, 0.4f, 31), dark, 0.6f);

  // ---------------------------------------------------------------- bay door, open to space
  for (int sx = -1; sx <= 1; sx += 2) {
    box(Vec3(sx * 38.5f, 13, -72.5f), Vec3(21, 26, 1), wall, 0.45f);
    box(Vec3(sx * 28.7f, 9.75f, -71.5f), Vec3(1.8f, 19.5f, 2.0f), trim, 0.3f);
    lamp(Vec3(sx * 27.75f, 9.75f, -71.2f), Vec3(0.14f, 18, 0.3f), cyan, 3.0f);
    for (float y = 2.5f; y < 19; y += 4) box(Vec3(sx * 28.7f, y, -70.45f), Vec3(1.82f, 1.0f, 0.12f), dark, 0.4f);
  }
  box(Vec3(0, 22.75f, -72.5f), Vec3(56, 6.5f, 1), wall, 0.45f);
  box(Vec3(0, 19.9f, -71.5f), Vec3(59.2f, 1.4f, 2.0f), trim, 0.3f);
  lamp(Vec3(0, 19.1f, -71.2f), Vec3(54, 0.14f, 0.3f), cyan, 3.0f);
  for (float x = -24; x <= 24; x += 12) {
    bool on = std::fmod(time * 1.2f + (x + 24) / 60.0f, 1.0f) < 0.5f;
    lamp(Vec3(x, 21.6f, -71.9f), Vec3(0.8f), on ? Vec3(1.0f, 0.2f, 0.1f) : Vec3(0.3f, 0.05f, 0.03f), on ? 3.0f : 0.2f, M_SPHERE);
  }
  addInstance(fd, true, M_BOX, fromTRS(Vec3(0, 9.6f, -72.0f), I, Vec3(55.6f, 19.2f, 0.05f)), Vec3(0.3f, 0.6f, 1.0f), 0, MAT_FIELD, 0, 0, 1.0f);
  // inner blast door behind the camera side
  box(Vec3(0, 13, 46.5f), Vec3(98, 26, 1), wall, 0.45f);
  for (int sx = -1; sx <= 1; sx += 2) box(Vec3(sx * 10.1f, 9, 45.7f), Vec3(20, 18, 0.7f), wall * 1.25f, 0.8f);
  box(Vec3(0, 9, 45.3f), Vec3(0.5f, 18, 0.2f), dark, 0.5f);
  box(Vec3(0, 18.5f, 45.6f), Vec3(42, 1.0f, 1.0f), trim, 0.3f);
  lamp(Vec3(0, 17.7f, 45.2f), Vec3(38, 0.2f, 0.2f), Vec3(1.0f, 0.25f, 0.1f), 2.0f);

  // ---------------------------------------------------------------- service pad and cradle
  cyl(Vec3(0, 0.12f, 0), 14.2f, 0.24f, up, Vec3(0.15f, 0.16f, 0.18f), 0.8f);
  for (int i = 0; i < 24; ++i) {
    float a = i * 2 * PI / 24;
    bool on = std::fmod(time * 0.6f - i / 24.0f + 1.0f, 1.0f) < 0.12f;
    lamp(Vec3(std::cos(a) * 14.7f, 0.1f, std::sin(a) * 14.7f), Vec3(0.5f, 0.16f, 0.5f), amber, on ? 3.5f : 0.5f);
  }
  const Vec3 shipPos(0, 2.9f, 0);
  for (int sx = -1; sx <= 1; sx += 2) {
    box(Vec3(sx * 0.7f, 0.34f, 1.0f), Vec3(1.3f, 0.2f, 4.4f), dark, 0.7f);
    for (float z = -0.2f; z <= 2.3f; z += 2.4f) {
      box(Vec3(sx * 0.7f, 1.0f, z), Vec3(0.45f, 1.3f, 0.45f), trim, 0.3f);
      box(Vec3(sx * 0.7f, 1.62f, z), Vec3(0.8f, 0.1f, 0.9f), dark, 0.7f);
    }
  }
  renderShipModel(fd, ship, shipPos, I, time);

  // ---------------------------------------------------------------- cargo
  static const Vec3 paint[6] = {Vec3(0.72f, 0.22f, 0.07f), Vec3(0.08f, 0.26f, 0.55f), Vec3(0.12f, 0.42f, 0.24f),
                                Vec3(0.78f, 0.58f, 0.08f), Vec3(0.45f, 0.46f, 0.48f), Vec3(0.42f, 0.09f, 0.09f)};
  for (int i = 0; i < 7; ++i) { // rows along the port wall
    int h = r.irange(1, 3);
    for (int k = 0; k < h; ++k) container(Vec3(-42.5f + r.range(-0.15f, 0.15f), 1.42f + k * 2.75f, -58 + i * 7.0f), r.range(-0.03f, 0.03f), paint[r.irange(0, 5)]);
    if (i % 2 == 0) {
      int h2 = r.irange(1, 2);
      for (int k = 0; k < h2; ++k) container(Vec3(-38.6f, 1.42f + k * 2.75f, -58 + i * 7.0f + 0.4f), r.range(-0.05f, 0.05f), paint[r.irange(0, 5)]);
    }
  }
  for (int i = 0; i < 4; ++i) { // a block by the door, turned sideways
    int h = r.irange(1, 3);
    for (int k = 0; k < h; ++k) container(Vec3(36.0f, 1.42f + k * 2.75f, -64 + i * 3.1f), PI * 0.5f + r.range(-0.03f, 0.03f), paint[r.irange(0, 5)]);
  }
  for (int i = 0; i < 3; ++i) container(Vec3(-20.0f + i * 3.4f, 1.42f, 38.5f), r.range(-0.2f, 0.2f), paint[r.irange(0, 5)]);
  // barrels and crates near the pad
  for (int i = 0; i < 7; ++i) {
    Vec3 p(-17.5f + (i % 3) * 1.1f, 0.62f, 10.0f + (i / 3) * 1.1f);
    cyl(p, 0.48f, 1.24f, up, i % 2 ? Vec3(0.1f, 0.3f, 0.6f) : Vec3(0.75f, 0.55f, 0.05f), 0.3f);
    cyl(p + Vec3(0, 0.2f, 0), 0.5f, 0.07f, up, dark, 0.6f);
  }
  for (int i = 0; i < 5; ++i) box(Vec3(15.5f + (i % 2) * 1.5f, 0.55f + (i / 4) * 1.1f, 12.5f + (i / 2 % 2) * 1.4f), Vec3(1.3f, 1.1f, 1.2f), i % 2 ? Vec3(0.5f, 0.1f, 0.08f) : Vec3(0.3f, 0.32f, 0.3f), 0.25f, axisAngle(Vec3(0, 1, 0), r.range(-0.3f, 0.3f)));
  // tool trolley
  box(Vec3(-9.0f, 0.9f, 8.0f), Vec3(1.6f, 1.0f, 0.9f), Vec3(0.6f, 0.08f, 0.06f), 0.3f);
  box(Vec3(-9.0f, 1.45f, 8.0f), Vec3(1.7f, 0.08f, 1.0f), steel, 0.9f);
  for (int sx = -1; sx <= 1; sx += 2) cyl(Vec3(-9.0f + sx * 0.6f, 0.2f, 8.0f), 0.2f, 1.0f, I, dark, 0.3f);

  // ---------------------------------------------------------------- fuel farm with a hose to the ship
  for (int i = 0; i < 3; ++i) {
    Vec3 p(40.5f, 2.6f + (i == 2 ? 3.9f : 0.0f), 8.0f + (i == 2 ? 2.4f : i * 4.8f));
    cyl(p, 2.0f, 11.0f, side, Vec3(0.78f, 0.8f, 0.82f), 0.25f);
    for (int e = -1; e <= 1; e += 2) {
      ball(p + Vec3(e * 5.5f, 0, 0), 1.98f, Vec3(0.78f, 0.8f, 0.82f), 0.25f);
      cyl(p + Vec3(e * 3.2f, 0, 0), 2.06f, 0.5f, side, trim, 0.3f);
      if (i < 2) box(p + Vec3(e * 3.2f, -1.9f, 0), Vec3(0.6f, 1.6f, 3.4f), dark, 0.6f);
    }
  }
  box(Vec3(33.5f, 1.1f, 10.4f), Vec3(1.6f, 2.2f, 2.4f), trim, 0.3f); // pump
  lamp(Vec3(32.65f, 1.7f, 10.4f), Vec3(0.06f, 0.5f, 1.2f), Vec3(0.3f, 1.0f, 0.5f), 2.0f);
  seg(Vec3(35, 1.0f, 10.4f), Vec3(38.5f, 1.6f, 10.4f), 0.22f, steel);
  Vec3 hose[6] = {Vec3(32.7f, 0.7f, 10.4f), Vec3(27, 0.16f, 9.5f), Vec3(19, 0.16f, 11.5f), Vec3(10, 0.4f, 8.5f), Vec3(4.2f, 0.4f, 7.2f), Vec3(2.1f, 2.4f, 5.6f)};
  for (int i = 0; i < 5; ++i) {
    seg(hose[i], hose[i + 1], 0.13f, Vec3(0.05f, 0.05f, 0.06f), 0.1f);
    ball(hose[i + 1], 0.15f, Vec3(0.05f, 0.05f, 0.06f), 0.1f);
  }

  // ---------------------------------------------------------------- overhead crane carrying a spare engine
  {
    const float zc = -24.0f;
    float xt = 12.0f * std::sin(time * 0.11f), hookY = 13.5f + 2.5f * std::sin(time * 0.17f);
    box(Vec3(0, 22.6f, zc), Vec3(94, 1.5f, 1.8f), trim, 0.3f);
    for (int sx = -1; sx <= 1; sx += 2) box(Vec3(sx * 46.0f, 22.6f, zc), Vec3(1.6f, 2.2f, 5.0f), dark, 0.6f);
    box(Vec3(xt, 21.4f, zc), Vec3(3.4f, 1.3f, 3.0f), dark, 0.7f);
    lamp(Vec3(xt, 20.7f, zc + 1.55f), Vec3(1.2f, 0.2f, 0.1f), amber, std::fmod(time, 1.0f) < 0.5f ? 3.0f : 0.3f);
    for (int sx = -1; sx <= 1; sx += 2) seg(Vec3(xt + sx * 1.0f, 20.8f, zc), Vec3(xt + sx * 1.0f, hookY + 1.4f, zc), 0.05f, steel);
    box(Vec3(xt, hookY + 1.2f, zc), Vec3(2.8f, 0.35f, 0.5f), trim, 0.3f);
    cyl(Vec3(xt, hookY, zc), 0.85f, 4.4f, side, steel, 1.0f);
    cyl(Vec3(xt - 1.2f, hookY, zc), 0.92f, 0.3f, side, trim, 0.3f);
    cyl(Vec3(xt + 1.2f, hookY, zc), 0.92f, 0.3f, side, dark, 0.8f);
    cyl(Vec3(xt + 2.7f, hookY, zc), 0.95f, 1.2f, axisAngle(Vec3(0, 1, 0), -PI * 0.5f), dark, 0.9f, M_CONE);
  }

  // ---------------------------------------------------------------- a second fighter under maintenance
  if (parked) {
    Vec3 pp(29.0f, 2.9f, -40.0f);
    Quat pq = axisAngle(Vec3(0, 1, 0), radians(205.0f));
    renderShipModel(fd, *parked, pp, pq, time);
    for (int sx = -1; sx <= 1; sx += 2)
      for (float z = -0.2f; z <= 2.3f; z += 2.4f) box(pp + rotate(pq, Vec3(sx * 0.7f, -1.95f, z)), Vec3(0.45f, 1.9f, 0.45f), trim, 0.3f, pq);
    // scaffold tower beside it
    for (int i = 0; i < 4; ++i) box(Vec3(21.0f + (i % 2) * 2.4f, 2.6f, -44.0f + (i / 2) * 2.4f), Vec3(0.12f, 5.2f, 0.12f), steel, 0.9f);
    for (float y = 1.6f; y < 5.5f; y += 1.8f) box(Vec3(22.2f, y, -42.8f), Vec3(2.7f, 0.1f, 2.7f), dark, 0.7f);
  }

  // ---------------------------------------------------------------- robots
  { // welder on tracks, working under the starboard wing
    Vec3 b(6.6f, 0, 7.0f);
    box(b + Vec3(0, 0.55f, 0), Vec3(1.5f, 0.5f, 2.3f), trim, 0.3f);
    for (int sx = -1; sx <= 1; sx += 2) {
      box(b + Vec3(sx * 0.95f, 0.36f, 0), Vec3(0.45f, 0.72f, 2.7f), dark, 0.4f);
      for (float z = -1.0f; z <= 1.0f; z += 1.0f) cyl(b + Vec3(sx * 1.19f, 0.36f, z), 0.3f, 0.06f, side, steel, 1.0f);
    }
    cyl(b + Vec3(0, 1.1f, -0.3f), 0.5f, 0.7f, up, dark, 0.8f);
    float sway = std::sin(time * 0.6f);
    Vec3 shoulder = b + Vec3(0, 1.5f, -0.3f), tip(6.2f + 0.7f * sway, 2.52f, 3.9f);
    Vec3 elbow = (shoulder + tip) * 0.5f + Vec3(0.5f, 1.3f + 0.15f * sway, 0.6f);
    seg(shoulder, elbow, 0.15f, trim, 0.3f);
    seg(elbow, tip + Vec3(0, -0.25f, 0.1f), 0.11f, steel, 1.0f);
    ball(shoulder, 0.26f, dark); ball(elbow, 0.22f, dark);
    box(b + Vec3(0, 1.75f, 0.35f), Vec3(0.7f, 0.5f, 0.6f), Vec3(0.75f, 0.77f, 0.8f), 0.4f);
    lamp(b + Vec3(0, 1.8f, 0.03f), Vec3(0.45f, 0.12f, 0.06f), cyan, 3.0f);
    lamp(b + Vec3(0.5f, 2.15f, 0.5f), Vec3(0.18f), amber, std::fmod(time * 1.5f, 1.0f) < 0.5f ? 3.0f : 0.2f, M_SPHERE);
    bool welding = std::sin(time * 0.8f) > -0.35f;
    if (welding) {
      uint32_t hh = (uint32_t)(time * 40.0f) * 2654435761u;
      float fl = 0.5f + 0.5f * (float)((hh >> 8) & 255) / 255.0f;
      lamp(tip, Vec3(0.14f), Vec3(0.7f, 0.85f, 1.0f), 6.0f * fl, M_SPHERE);
      addGlow(fd, tip, 1.6f * fl, Vec3(0.45f, 0.65f, 1.0f) * 1.2f);
      addGlow(fd, tip, 0.5f, Vec3(1.0f, 1.0f, 1.0f) * 2.0f * fl);
      for (int i = 0; i < 22; ++i) { // sparks fall in arcs
        float ph = std::fmod(time * (1.3f + (i % 5) * 0.17f) + i * 0.173f, 1.0f);
        uint32_t k = (uint32_t)(i * 7919 + (int)(time * (1.3f + (i % 5) * 0.17f) + i * 0.173f) * 104729) * 2654435761u;
        Vec3 dir(((k & 255) / 255.0f - 0.5f) * 2.6f, -0.4f - ((k >> 8) & 255) / 255.0f, ((k >> 16) & 255) / 255.0f * 2.2f - 0.4f);
        Vec3 v = dir * 2.2f + Vec3(0, -6.0f * ph, 0);
        Vec3 p = tip + dir * (2.2f * ph) + Vec3(0, -3.0f * ph * ph, 0);
        if (p.y > 0.05f) addGlow(fd, p, 0.035f, Vec3(1.0f, 0.65f, 0.25f) * (2.5f * (1.0f - ph)), v * 0.05f, 2);
      }
    }
  }
  for (int k = 0; k < 2; ++k) { // inspection drones circling the ship
    float a = time * 0.33f + k * PI;
    Vec3 p(std::cos(a) * 11.5f, 7.6f + 1.1f * std::sin(time * 0.9f + k * 2.0f) - k * 2.2f, std::sin(a) * 9.5f - 1.0f);
    Quat q = lookRotation(Vec3(-std::sin(a), 0, std::cos(a) * 0.83f)) * axisAngle(Vec3(0, 0, 1), 0.12f * std::sin(time * 1.3f + k));
    addInstance(fd, false, M_SPHERE, fromTRS(p, q, Vec3(1.0f, 0.7f, 1.3f)), Vec3(0.8f, 0.82f, 0.85f), 0, MAT_HULL, 0, 0.3f + k, 0.5f);
    addInstance(fd, false, M_BOX, fromTRS(p, q, Vec3(2.0f, 0.1f, 0.3f)), dark, 0, MAT_HULL, 0, 0.5f, 0.8f);
    addInstance(fd, false, M_BOX, fromTRS(p, q, Vec3(0.3f, 0.1f, 2.0f)), dark, 0, MAT_HULL, 0, 0.6f, 0.8f);
    for (int i = 0; i < 4; ++i) {
      Vec3 o = rotate(q, Vec3((i < 2 ? 1.0f : 0.0f) * (i % 2 ? 1 : -1), 0, (i >= 2 ? 1.0f : 0.0f) * (i % 2 ? 1 : -1)));
      addInstance(fd, false, M_CYL, fromTRS(p + o, q * up, Vec3(0.5f, 0.5f, 0.3f)), trim, 0, MAT_HULL, 0, 0.2f * i, 0.3f);
      lamp(p + o + Vec3(0, -0.18f, 0), Vec3(0.3f, 0.05f, 0.3f), cyan, 3.0f);
      addGlow(fd, p + o + Vec3(0, -0.4f, 0), 0.6f, cyan * 0.35f);
    }
    lamp(p + rotate(q, Vec3(0, -0.05f, -0.6f)), Vec3(0.24f), Vec3(1.0f, 0.3f, 0.15f), 4.0f, M_SPHERE);
    // scanning light sweeping over the hull
    Vec3 target(std::cos(a) * 3.0f, 3.2f, std::sin(a * 1.0f + std::sin(time * 1.7f + k)) * 6.0f);
    addGlow(fd, (p + target) * 0.5f, 0.05f, Vec3(1.0f, 0.25f, 0.12f) * 0.8f, target - p, 2);
    addGlow(fd, target, 0.45f, Vec3(1.0f, 0.3f, 0.15f) * 0.7f);
  }
  { // cargo loader shuttling a container along the port lane
    float u = std::sin(time * 0.11f);
    Vec3 b(-31.5f, 0, -10.0f + 30.0f * u);
    float bob = 0.02f * std::sin(time * 9.0f);
    box(b + Vec3(0, 1.25f + bob, 0.8f), Vec3(2.3f, 1.3f, 3.0f), trim, 0.3f);
    box(b + Vec3(0, 2.45f + bob, 1.3f), Vec3(1.9f, 1.2f, 1.6f), dark, 0.6f);
    addInstance(fd, false, M_BOX, fromTRS(b + Vec3(0, 2.5f + bob, 0.45f), I, Vec3(1.7f, 0.9f, 0.08f)), Vec3(0.1f, 0.35f, 0.55f), 0, MAT_GLASS, 0, 0, 1);
    for (int sx = -1; sx <= 1; sx += 2) {
      for (float z = -0.4f; z <= 2.0f; z += 2.4f) cyl(b + Vec3(sx * 1.25f, 0.6f, z), 0.6f, 0.5f, side, Vec3(0.04f, 0.04f, 0.045f), 0.1f);
      box(b + Vec3(sx * 0.8f, 2.6f, -0.85f), Vec3(0.2f, 4.6f, 0.25f), dark, 0.8f);
      box(b + Vec3(sx * 0.8f, 0.7f, -2.6f), Vec3(0.25f, 0.12f, 3.4f), steel, 1.0f);
    }
    box(b + Vec3(0, 4.9f, -0.85f), Vec3(1.9f, 0.2f, 0.3f), dark, 0.8f);
    container(b + Vec3(0, 2.12f, -4.2f), PI * 0.5f, paint[3]);
    bool on = std::fmod(time * 2.0f, 1.0f) < 0.5f;
    lamp(b + Vec3(0, 3.2f + bob, 1.3f), Vec3(0.3f), amber, on ? 4.0f : 0.3f, M_SPHERE);
    if (on) addGlow(fd, b + Vec3(0, 3.3f, 1.3f), 1.3f, amber * 0.5f);
    for (int sx = -1; sx <= 1; sx += 2) lamp(b + Vec3(sx * 0.85f, 1.3f, u * std::cos(time * 0.11f) > 0 ? 2.32f : -0.72f), Vec3(0.3f, 0.2f, 0.05f), white, 3.0f);
  }
  { // a two-legged service unit by the tool trolley, checking its work
    Vec3 b(-8.6f, 0, 6.2f);
    float look = 0.5f * std::sin(time * 0.4f), bend = 0.04f * std::sin(time * 1.1f);
    Quat q = axisAngle(Vec3(0, 1, 0), radians(155.0f));
    auto P = [&](Vec3 l) { return b + rotate(q, l); };
    for (int sx = -1; sx <= 1; sx += 2) {
      box(P(Vec3(sx * 0.22f, 0.5f, 0)), Vec3(0.2f, 1.0f, 0.24f), steel, 0.9f, q);
      box(P(Vec3(sx * 0.22f, 0.06f, -0.08f)), Vec3(0.26f, 0.12f, 0.45f), dark, 0.6f, q);
      seg(P(Vec3(sx * 0.42f, 1.62f + bend, 0)), P(Vec3(sx * 0.5f, 1.1f + bend, -0.25f)), 0.07f, steel, 0.9f);
      ball(P(Vec3(sx * 0.42f, 1.62f + bend, 0)), 0.12f, dark);
    }
    box(P(Vec3(0, 1.35f + bend, 0)), Vec3(0.66f, 0.72f, 0.4f), trim, 0.3f, q);
    box(P(Vec3(0, 1.36f + bend, 0.24f)), Vec3(0.4f, 0.5f, 0.14f), dark, 0.6f, q);
    Quat hq = q * axisAngle(Vec3(0, 1, 0), look);
    addInstance(fd, false, M_BOX, fromTRS(P(Vec3(0, 1.95f + bend, 0)), hq, Vec3(0.38f, 0.32f, 0.36f)), Vec3(0.78f, 0.8f, 0.82f), 0, MAT_HULL, 0, 0.4f, 0.5f);
    lamp(P(Vec3(0, 1.97f + bend, 0)) + rotate(hq, Vec3(0, 0, -0.19f)), Vec3(0.28f, 0.07f, 0.03f), cyan, 3.5f, M_BOX, hq);
  }
}

void renderShipModel(FrameData& fd, const Ship& ship, const Vec3& pos, const Quat& rot, float time) {
  for (const Part& p : ship.parts) {
    if (!p.alive || p.internal) continue;
    Mat4 m = fromTRS(pos + rotate(rot, p.pos), rot * p.rot, p.scale);
    Vec3 col = p.color;
    float em = p.emissive;
    if (p.role == Role::Light) em *= std::fmod(time * 0.8f + p.seed, 1.0f) < 0.75f ? 1.0f : 0.05f;
    if (p.flash > 0) { col = lerpv(col, Vec3(1.0f, 0.9f, 0.7f), 0.7f); em += 1.5f; }
    addInstance(fd, false, p.mesh, m, col, em, p.material, 1.0f - p.hp / p.maxHp, p.seed, 1.0f);
  }
}

void renderWorld(FrameData& fd, const World& w, const Camera& cam) {
  auto visible = [&](const Vec3& p, float r) {
    Vec3 d = p - cam.pos;
    float z = dot(d, cam.fwd);
    if (z < -r) return false;
    float lim = (z + r) * 1.6f + r; // generous cone test
    return std::fabs(dot(d, cam.right)) < lim * cam.aspect && std::fabs(dot(d, cam.up)) < lim;
  };

  // ---- ships
  for (const ShipEntity& s : w.ships) {
    if (!s.alive) continue;
    if (!visible(s.pos, 14)) continue;
    renderShipModel(fd, s.ship, s.pos, s.rot, w.time);
    // exhaust plumes: additive cones, tip trailing aft
    for (int e = 0; e < 2; ++e) {
      float out = s.engineOut[e];
      if (out < 0.03f) continue;
      float health = s.ship.stats.engine[e];
      float flick = health < 0.6f ? (0.55f + 0.45f * std::sin(w.time * 47.0f + e * 2.0f + s.id) * std::sin(w.time * 13.0f + e)) : 1.0f;
      float power = clampf(out, 0.0f, 1.5f);
      float len = (2.5f + 11.0f * power) * flick;
      float seed = (float)(s.id * 2 + e) * 0.37f;
      Vec3 nozzle = s.ship.stats.enginePos[e] + Vec3(0, 0, 0.62f);
      Quat aft = s.rot * axisAngle(Vec3(0, 1, 0), PI);
      // hot core jet, then a wider, shorter, cooler envelope around it
      addInstance(fd, true, M_CONE, fromTRS(s.pos + rotate(s.rot, nozzle + Vec3(0, 0, len * 0.5f)), aft, Vec3(1.15f, 1.15f, len)), s.ship.glow, 0,
                  MAT_FLAME, 0.0f, seed, 0.55f + 0.75f * power);
      float len2 = len * 0.62f;
      addInstance(fd, true, M_CONE, fromTRS(s.pos + rotate(s.rot, nozzle + Vec3(0, 0, len2 * 0.5f)), aft, Vec3(2.1f, 2.1f, len2)), s.ship.glow, 0,
                  MAT_FLAME, 1.0f, seed + 0.5f, 0.5f + 0.6f * power);
      // the nozzle throat glows white-hot
      addInstance(fd, false, M_SPHERE, fromTRS(s.pos + rotate(s.rot, nozzle + Vec3(0, 0, -0.25f)), s.rot, Vec3(1.3f, 1.3f, 0.5f)),
                  lerpv(s.ship.glow, Vec3(1, 1, 1), 0.6f), 1.0f + 3.0f * power * flick, MAT_EMISSIVE, 0, 0, 1);
      addGlow(fd, s.pos + rotate(s.rot, nozzle + Vec3(0, 0, 0.3f)), 2.0f + 2.6f * power, lerpv(s.ship.glow, Vec3(1, 1, 1), 0.35f) * (0.45f * power * flick));
      addGlow(fd, s.pos + rotate(s.rot, nozzle + Vec3(0, 0, len * 0.3f)), 1.5f + 3.5f * power, s.ship.glow * (0.16f * power * flick));
      // camera-facing streaks down the axis: the dense centre of the jet, visible from any side
      Vec3 axis = rotate(s.rot, Vec3(0, 0, 1));
      addGlow(fd, s.pos + rotate(s.rot, nozzle + Vec3(0, 0, len * 0.22f)), 0.30f, lerpv(s.ship.glow, Vec3(1, 1, 1), 0.7f) * (1.3f * power * flick), axis * (len * 0.42f), 2);
      addGlow(fd, s.pos + rotate(s.rot, nozzle + Vec3(0, 0, len * 0.40f)), 0.75f, s.ship.glow * (0.45f * power * flick), axis * (len * 0.75f), 2);
    }
    if (s.shieldFlash > 0.01f) {
      Mat4 m = fromTRS(s.pos + rotate(s.rot, Vec3(0, 0, -0.8f)), s.rot, Vec3(20.0f, 8.5f, 23.0f));
      addInstance(fd, true, M_SPHERE, m, Vec3(0.25f, 0.55f, 1.0f), 0, MAT_SHIELD, 0, 0, s.shieldFlash);
    }
  }

  // ---- asteroids and wreckage
  for (const Asteroid& a : w.asteroids) {
    float bound = std::max(a.scale.x, std::max(a.scale.y, a.scale.z)) * 0.6f;
    if (!visible(a.pos, bound)) continue;
    float dist = length(a.pos - cam.pos);
    if (w.sector.fogDensity * dist > 6.0f) continue;
    int mesh = (bound / std::max(dist, 1.0f) < 0.05f) ? a.meshLo : a.mesh;
    addInstance(fd, false, mesh, fromTRS(a.pos, a.rot, a.scale), a.color, 0, a.material, a.damage, a.seed, 1.0f);
  }

  // ---- debris
  for (const Debris& d : w.debris) {
    if (!visible(d.pos, 12)) continue;
    float fade = clampf(d.life / 3.0f, 0.0f, 1.0f);
    for (const DebrisPiece& p : d.pieces) {
      Mat4 m = fromTRS(d.pos + rotate(d.rot, p.pos), d.rot * p.rot, p.scale * fade);
      addInstance(fd, false, p.mesh, m, p.color, p.emissive, p.material, p.damage, p.seed, 1.0f);
    }
  }

  // ---- projectiles
  for (const Projectile& p : w.projectiles) {
    const WeaponDef& wd = WEAPONS[p.weapon];
    Vec3 rel = p.vel;
    if (p.owner >= 0 && p.owner < (int)w.ships.size()) rel = p.vel - w.ships[p.owner].vel * 0.0f;
    Vec3 dir = normalize(rel);
    switch (p.weapon) {
      case W_PULSE: addGlow(fd, p.pos, 0.28f, wd.color * 3.0f, dir * 9.0f, 2); break;
      case W_GATLING: addGlow(fd, p.pos, 0.16f, wd.color * 2.5f, dir * 5.0f, 2); break;
      case W_ION: addGlow(fd, p.pos, 0.4f, wd.color * 3.0f, dir * 4.0f, 2); addGlow(fd, p.pos, 1.0f, wd.color * 0.8f); break;
      case W_RAIL: addGlow(fd, p.pos - dir * 30.0f, 0.3f, wd.color * 3.5f, dir * 70.0f, 2); break;
      case W_PLASMA: addGlow(fd, p.pos, 1.5f, wd.color * 2.5f); addGlow(fd, p.pos, 3.2f, wd.color * 0.6f); break;
      default: {
        Mat4 m = fromTRS(p.pos, lookRotation(dir), Vec3(0.3f, 0.3f, 1.6f));
        addInstance(fd, false, M_CONE, m, Vec3(0.7f, 0.7f, 0.7f), 0, MAT_HULL, 0, 0.3f, 1.0f);
        addGlow(fd, p.pos - dir * 1.0f, 1.3f, wd.color * 2.5f);
      } break;
    }
  }

  // ---- particles
  for (const Particle& p : w.particles) {
    float t = 1.0f - p.life / p.maxLife;
    float size = lerpf(p.size0, p.size1, t);
    if (!visible(p.pos, size + 2)) continue;
    float c[4];
    for (int i = 0; i < 4; ++i) c[i] = lerpf(p.c0[i], p.c1[i], t);
    Vec3 axis = p.kind == 2 ? p.vel * p.stretch : Vec3(0, 0, 0);
    addParticle(fd, p.pos, size, c, axis, p.kind);
  }

  // ---- drifting dust: static motes that give a sense of speed and scale
  const ShipEntity& pl = w.player();
  Vec3 v = pl.alive ? pl.vel : Vec3(0, 0, 0);
  float speed = length(v);
  const float L = 70.0f;
  for (int i = 0; i < 260; ++i) {
    uint32_t h = (uint32_t)i * 2654435761u;
    Vec3 base((float)(h & 1023) / 1023.0f, (float)((h >> 10) & 1023) / 1023.0f, (float)((h >> 20) & 1023) / 1023.0f);
    base = base * (2 * L);
    Vec3 rel(std::fmod(base.x - cam.pos.x, 2 * L), std::fmod(base.y - cam.pos.y, 2 * L), std::fmod(base.z - cam.pos.z, 2 * L));
    for (int a = 0; a < 3; ++a) { if (rel[a] < 0) rel[a] += 2 * L; rel[a] -= L; }
    float d = length(rel);
    if (d > L || d < 3 || dot(rel, cam.fwd) < 0) continue;
    float fade = (1.0f - d / L) * 0.5f;
    Vec3 col = (Vec3(0.55f, 0.65f, 0.8f) + w.sector.fogColor * 4.0f) * fade;
    if (speed > 12) addGlow(fd, cam.pos + rel, 0.035f, col, v * 0.022f, 2);
    else addGlow(fd, cam.pos + rel, 0.07f, col);
  }
}
