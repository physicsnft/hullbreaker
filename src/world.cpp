#include "world.h"
#include <cstdio>

const Difficulty DIFFICULTY[DIFFICULTY_COUNT] = {
    {"ROOKIE", "2-3 RAIDERS, POOR SHOTS, ONE ATTACKS AT A TIME", 2, 3, 0.15f, 0.20f, 0.50f, 1, 30.0f, 1.5f, 2.6f},
    {"PILOT", "3-4 RAIDERS, TWO ATTACK AT A TIME", 3, 4, 0.21f, 0.40f, 0.80f, 2, 40.0f, 1.2f, 2.2f},
    {"VETERAN", "4-5 SKILLED RAIDERS, HARDER HITS", 4, 5, 0.30f, 0.55f, 0.95f, 2, 50.0f, 1.0f, 1.9f},
    {"ACE", "5-6 ACES, THREE ATTACK AT ONCE", 5, 6, 0.40f, 0.75f, 1.00f, 3, 65.0f, 0.85f, 1.6f},
};

const char* const SECTOR_NAMES[SEC_COUNT] = {"ASTEROID BELT", "NEBULA", "PLANETARY ORBIT", "STELLAR CORONA", "ICE FIELD", "SHIP GRAVEYARD"};

namespace {
const Vec3 kRed(1.0f, 0.35f, 0.3f), kAmber(1.0f, 0.75f, 0.25f), kCyan(0.5f, 0.9f, 1.0f), kGreen(0.45f, 1.0f, 0.6f);

std::string sectorName(Rng& r) {
  static const char* a[] = {"KEP", "VOR", "ANT", "ZEL", "ORI", "THA", "MIR", "CYG", "DRA", "HEL", "NYX", "TAU", "SOL", "ERI", "LYR", "QUA"};
  static const char* b[] = {"RA", "TIS", "ARES", "DAN", "ON", "LIS", "AX", "ENE", "IUM", "OS", "ETH", "URA", "IS", "ARA"};
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%s%s-%d", a[r.irange(0, 15)], b[r.irange(0, 13)], r.irange(11, 989));
  return buf;
}
} // namespace

void World::say(const std::string& text, Vec3 color, float ttl) {
  messages.push_back({text, ttl, color});
  if (logMessages) std::printf("    [%6.1f] %s\n", time, text.c_str());
  if (messages.size() > 7) messages.erase(messages.begin());
}

// =====================================================================================
// Sector generation
// =====================================================================================
void World::generateSector(uint32_t seed, int type) {
  Rng r(seed * 7919u + 13u);
  Sector& S = sector;
  S = Sector();
  S.seed = seed;
  S.type = type < 0 ? r.irange(0, SEC_COUNT - 1) : type % SEC_COUNT;
  S.name = sectorName(r);
  S.skySeed = r.range(0, 100);
  S.sunDir = normalize(Vec3(r.range(-1, 1), r.range(0.15f, 0.7f), r.range(-1, 1)));
  float hue = r.uniform();
  auto planet = [&](float dist, float radius, int ptype, bool ring) {
    Planet p;
    Vec3 dir = normalize(cross(S.sunDir, r.unit()) + S.sunDir * r.range(-0.3f, 0.5f) + Vec3(0, r.range(-0.5f, 0.1f), 0));
    p.pos = dir * dist;
    p.radius = radius;
    p.type = ptype;
    p.ring = ring;
    p.seed = r.range(0, 50);
    p.tiltX = r.range(-0.6f, 0.6f);
    p.tiltZ = r.range(-0.6f, 0.6f);
    float h = r.uniform();
    switch (ptype) {
      case 1: p.colA = hsv(h, 0.45f, 0.75f); p.colB = hsv(h + r.range(0.03f, 0.12f), 0.3f, 0.95f); p.atmo = 0.5f; break;
      case 2: p.colA = Vec3(0.05f, 0.2f, 0.5f); p.colB = hsv(r.range(0.2f, 0.38f), 0.55f, 0.45f); p.atmo = 1.0f; break;
      case 3: p.colA = Vec3(0.12f, 0.08f, 0.07f); p.colB = Vec3(1.0f, 0.35f, 0.05f); p.atmo = 0.35f; break;
      case 4: p.colA = Vec3(0.55f, 0.75f, 0.95f); p.colB = Vec3(0.92f, 0.96f, 1.0f); p.atmo = 0.6f; break;
      default: p.colA = hsv(h, 0.15f, 0.3f); p.colB = hsv(h + 0.05f, 0.2f, 0.6f); p.atmo = 0.05f; break;
    }
    S.planets.push_back(p);
  };
  switch (S.type) {
    case SEC_BELT:
      S.sunColor = Vec3(1.0f, 0.93f, 0.82f); S.sunIntensity = 3.2f; S.sunSize = 0.03f;
      S.ambient = Vec3(0.045f, 0.05f, 0.065f);
      S.nebA = hsv(hue, 0.6f, 0.35f); S.nebB = hsv(hue + 0.12f, 0.5f, 0.25f); S.nebC = hsv(hue + 0.5f, 0.4f, 0.2f);
      S.nebDensity = 0.55f; S.nebScale = 2.2f;
      S.fogColor = Vec3(0.02f, 0.02f, 0.03f); S.fogDensity = 0.000025f;
      planet(160000, r.range(30000, 45000), 1, r.chance(0.7f));
      if (r.chance(0.6f)) planet(90000, r.range(5000, 9000), 0, false);
      break;
    case SEC_NEBULA: {
      Vec3 c = hsv(hue, 0.65f, 1.0f);
      S.sunColor = lerpv(Vec3(1, 1, 1), c, 0.4f); S.sunIntensity = 2.2f; S.sunSize = 0.04f;
      S.fogColor = c * 0.07f; S.fogDensity = r.range(0.00035f, 0.00055f);
      S.ambient = c * 0.13f + Vec3(0.02f);
      S.nebA = c * 0.4f; S.nebB = hsv(hue + 0.1f, 0.7f, 0.4f); S.nebC = hsv(hue - 0.15f, 0.6f, 0.3f);
      S.nebDensity = 1.0f; S.nebScale = 1.6f;
      if (r.chance(0.5f)) planet(140000, r.range(15000, 30000), r.irange(0, 4), false);
    } break;
    case SEC_PLANET: {
      S.sunColor = Vec3(1.0f, 0.96f, 0.9f); S.sunIntensity = 3.4f; S.sunSize = 0.028f;
      S.ambient = Vec3(0.05f, 0.06f, 0.08f);
      S.nebA = hsv(hue, 0.5f, 0.2f); S.nebB = hsv(hue + 0.2f, 0.4f, 0.15f); S.nebC = hsv(hue + 0.6f, 0.3f, 0.12f);
      S.nebDensity = 0.3f; S.nebScale = 2.5f;
      S.fogColor = Vec3(0.015f, 0.02f, 0.03f); S.fogDensity = 0.00001f;
      int pt = r.irange(1, 4);
      planet(105000, r.range(62000, 76000), pt, pt == 1 && r.chance(0.5f));
      S.planets.back().pos.y = -std::fabs(S.planets.back().pos.y) - 30000;
      planet(210000, r.range(9000, 15000), 0, false);
      if (S.planets[0].type == 2) S.ambient = Vec3(0.05f, 0.075f, 0.11f);
      if (S.planets[0].type == 3) S.ambient = Vec3(0.09f, 0.055f, 0.04f);
    } break;
    case SEC_STAR: {
      bool blue = r.chance(0.35f);
      S.sunColor = blue ? Vec3(0.7f, 0.85f, 1.0f) : Vec3(1.0f, 0.62f, 0.28f);
      S.sunIntensity = 4.6f; S.sunSize = r.range(0.2f, 0.3f);
      S.sunDir = normalize(Vec3(r.range(-1, 1), r.range(-0.1f, 0.35f), r.range(-1, 1)));
      S.ambient = S.sunColor * 0.07f;
      S.nebA = S.sunColor * 0.35f; S.nebB = blue ? Vec3(0.2f, 0.3f, 0.6f) : Vec3(0.6f, 0.15f, 0.05f); S.nebC = Vec3(0.3f, 0.1f, 0.3f);
      S.nebDensity = 0.5f; S.nebScale = 1.8f;
      S.fogColor = S.sunColor * 0.06f; S.fogDensity = 0.00006f;
      S.exposure = 0.85f;
      planet(150000, r.range(8000, 14000), 3, false);
    } break;
    case SEC_ICE:
      S.sunColor = Vec3(0.82f, 0.92f, 1.0f); S.sunIntensity = 3.0f; S.sunSize = 0.025f;
      S.ambient = Vec3(0.05f, 0.08f, 0.11f);
      S.nebA = Vec3(0.1f, 0.4f, 0.6f); S.nebB = Vec3(0.15f, 0.2f, 0.6f); S.nebC = Vec3(0.3f, 0.6f, 0.6f);
      S.nebDensity = 0.8f; S.nebScale = 2.0f;
      S.fogColor = Vec3(0.03f, 0.06f, 0.09f); S.fogDensity = 0.00008f;
      planet(150000, r.range(35000, 50000), 4, true);
      break;
    default: // graveyard
      S.sunColor = Vec3(0.9f, 1.0f, 0.85f); S.sunIntensity = 2.6f; S.sunSize = 0.03f;
      S.ambient = Vec3(0.04f, 0.06f, 0.05f);
      S.nebA = Vec3(0.1f, 0.4f, 0.3f); S.nebB = Vec3(0.3f, 0.35f, 0.1f); S.nebC = Vec3(0.4f, 0.15f, 0.1f);
      S.nebDensity = 0.75f; S.nebScale = 2.4f;
      S.fogColor = Vec3(0.02f, 0.035f, 0.03f); S.fogDensity = 0.00006f;
      planet(130000, r.range(20000, 32000), 0, false);
      break;
  }
}

void World::addAsteroid(Rng& r, Vec3 pos, float radius, int kind) {
  Asteroid a;
  a.pos = pos;
  a.rot = normalize(Quat(r.range(-1, 1), r.range(-1, 1), r.range(-1, 1), r.range(-1, 1)));
  a.vel = r.unit() * r.range(0, 2.5f);
  a.angVel = r.unit() * r.range(0.0f, 0.18f) * std::min(1.0f, 30.0f / radius);
  a.seed = r.uniform();
  int v = r.irange(0, AST_VARIANTS - 1);
  float density = 2600;
  if (kind == 2) { // ice crystal
    a.mesh = a.meshLo = M_CRYSTAL;
    float len = radius * 3.2f;
    a.scale = Vec3(len * r.range(0.7f, 1.0f), len * r.range(1.3f, 2.0f), len * r.range(0.7f, 1.0f));
    a.radius = radius;
    a.material = MAT_ICE;
    a.color = lerpv(Vec3(0.45f, 0.7f, 0.95f), Vec3(0.7f, 0.9f, 1.0f), r.uniform());
    density = 900;
  } else if (kind == 3) { // wreckage
    static const int meshes[] = {M_BOX, M_BOX, M_PRISM, M_CYL, M_TAPER, M_WING_R, M_WEDGE};
    a.mesh = a.meshLo = meshes[r.irange(0, 6)];
    float s = radius * 2.0f;
    a.scale = Vec3(s * r.range(0.45f, 1.0f), s * r.range(0.3f, 0.8f), s * r.range(0.6f, 1.0f));
    a.radius = radius * 0.85f;
    a.material = MAT_HULL;
    a.color = lerpv(Vec3(0.22f, 0.24f, 0.26f), Vec3(0.4f, 0.3f, 0.2f), r.uniform());
    a.damage = r.range(0.25f, 0.9f);
    density = 400;
  } else {
    a.mesh = M_AST0 + v;
    a.meshLo = M_ASTLO0 + v;
    a.scale = Vec3(radius / g_meshRadius[a.mesh]);
    a.radius = radius;
    if (kind == 1) {
      a.material = MAT_ICE;
      a.color = lerpv(Vec3(0.4f, 0.6f, 0.8f), Vec3(0.75f, 0.85f, 0.95f), r.uniform());
    } else {
      a.material = MAT_ROCK;
      Vec3 base = sector.type == SEC_STAR ? Vec3(0.28f, 0.2f, 0.17f) : (sector.type == SEC_GRAVEYARD ? Vec3(0.3f, 0.32f, 0.28f) : Vec3(0.42f, 0.38f, 0.34f));
      a.color = base * r.range(0.7f, 1.3f) + Vec3(r.range(-0.03f, 0.03f), 0, r.range(-0.03f, 0.03f));
    }
  }
  a.mass = 4.19f * radius * radius * radius * density;
  // strength grows with size, but less than mass does: big rocks are riddled with flaws
  a.hp = a.maxHp = (kind == 3 ? 3.0f : (kind == 0 ? 6.0f : 4.5f)) * std::pow(radius, 1.5f);
  asteroids.push_back(a);
}

void World::populate(Rng& r) {
  auto clear = [&](Vec3 p, float rad) {
    for (auto& s : ships)
      if (length2(p - s.pos) < sqr(rad + 140)) return false;
    for (auto& a : asteroids)
      if (length2(p - a.pos) < sqr(rad + a.radius + 6)) return false;
    return true;
  };
  auto place = [&](int n, int kind, float rmin, float rmax, float power, int layout, Vec3 center, float spread) {
    for (int i = 0; i < n; ++i) {
      float rad = rmin + (rmax - rmin) * std::pow(r.uniform(), power);
      for (int tries = 0; tries < 12; ++tries) {
        Vec3 p;
        if (layout == 0) p = Vec3(r.gauss() * spread, r.gauss() * spread * 0.12f, r.gauss() * spread); // disc
        else if (layout == 1) p = r.inSphere() * spread;                                               // ball
        else p = center + r.inSphere() * spread;                                                       // cluster
        if (clear(p, rad)) { addAsteroid(r, p, rad, kind); break; }
      }
    }
  };
  switch (sector.type) {
    case SEC_BELT:
      place(5, 0, 140, 300, 1.0f, 0, Vec3(), 1800);
      place(230, 0, 5, 80, 3.2f, 0, Vec3(), 1500);
      break;
    case SEC_NEBULA:
      place(60, 0, 6, 70, 2.5f, 1, Vec3(), 2400);
      break;
    case SEC_PLANET:
      place(60, 0, 5, 60, 3.0f, 1, Vec3(), 2600);
      for (int c = 0; c < 2; ++c) place(22, 0, 5, 45, 2.5f, 2, r.inSphere() * 1500.0f, 350);
      break;
    case SEC_STAR:
      place(3, 0, 120, 220, 1.0f, 1, Vec3(), 2200);
      place(110, 0, 6, 70, 3.0f, 1, Vec3(), 2400);
      break;
    case SEC_ICE:
      place(4, 1, 100, 200, 1.0f, 0, Vec3(), 1800);
      place(90, 2, 5, 40, 2.2f, 0, Vec3(), 1500);
      place(80, 1, 5, 70, 3.0f, 0, Vec3(), 1600);
      break;
    default:
      for (int c = 0; c < 5; ++c) place(22, 3, 5, 38, 1.6f, 2, r.inSphere() * 1700.0f, 320);
      place(40, 3, 4, 20, 2.0f, 1, Vec3(), 2300);
      place(60, 0, 6, 70, 3.0f, 1, Vec3(), 2400);
      break;
  }
}

void World::spawnEnemies(Rng& r) {
  static const char* names[] = {"RAIDER", "VULTURE", "JACKAL", "REAVER", "MARAUDER", "CORSAIR"};
  const Difficulty& D = DIFFICULTY[difficulty];
  int n = r.irange(D.minEnemies, D.maxEnemies);
  Vec3 groupDir = normalize(Vec3(r.range(-0.7f, 0.7f), r.range(-0.25f, 0.25f), -1.0f));
  for (int i = 0; i < n; ++i) {
    ShipEntity e;
    e.id = (int)ships.size();
    e.team = 1;
    int w0 = r.irange(0, 2);                        // every raider carries one light weapon...
    int w1 = r.chance(0.6f) ? r.irange(3, 5) : r.irange(0, 2); // ...and usually a heavy one
    if (r.chance(0.5f)) std::swap(w0, w1);
    e.ship.build(PAL_ENEMY[r.irange(0, 2)], w0, w1, (uint32_t)r.next());
    if (i % 2 == 0) groupDir = normalize(Vec3(r.range(-1, 1), r.range(-0.3f, 0.3f), r.range(-1.0f, 0.35f)));
    e.pos = groupDir * r.range(1300, 2100) + r.unit() * 90.0f;
    e.rot = lookRotation(-e.pos);
    e.vel = rotate(e.rot, Vec3(0, 0, -60));
    e.shieldMax = e.shield = D.shield;
    e.skill = r.range(D.skillLo, D.skillHi);
    e.aiTimer = r.range(0, 2);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%s-%d", names[r.irange(0, 5)], i + 1);
    e.callsign = buf;
    e.target = 0;
    ships.push_back(std::move(e));
  }
  enemiesTotal = enemiesAlive = n;
}

void World::generate(uint32_t seed, int sectorType, int w0, int w1, bool withEnemies) {
  rng = Rng(seed ^ 0xA5A5u);
  difficulty = difficulty < 0 ? 0 : (difficulty >= DIFFICULTY_COUNT ? DIFFICULTY_COUNT - 1 : difficulty);
  enemyDamageMul = DIFFICULTY[difficulty].damage;
  sounds.clear();
  time = 0; result = 0; endTimer = 0; shake = 0;
  ships.clear(); asteroids.clear(); projectiles.clear(); debris.clear(); particles.clear(); messages.clear();
  enemiesTotal = enemiesAlive = 0;
  generateSector(seed, sectorType);
  Rng r(seed * 31u + 7u);

  ShipEntity p;
  p.id = 0; p.team = 0; p.player = true;
  p.ship.build(PAL_PLAYER, w0, w1, seed);
  p.shieldMax = p.shield = 140;
  p.callsign = "KESTREL";
  p.skill = 1.0f;
  ships.push_back(std::move(p));
  if (withEnemies) spawnEnemies(r);
  populate(r);
  lastPlayerPos = Vec3();
  if (withEnemies) {
    targetNearest();
    char buf[96];
    std::snprintf(buf, sizeof(buf), "SECTOR %s  -  %s", sector.name.c_str(), SECTOR_NAMES[sector.type]);
    say(buf, kCyan, 7);
    std::snprintf(buf, sizeof(buf), "%d HOSTILES DETECTED", enemiesTotal);
    say(buf, kAmber, 7);
  }
}

// =====================================================================================
// Effects
// =====================================================================================
void World::emit(Vec3 pos, Vec3 vel, float life, float s0, float s1, Vec3 c0, float a0, Vec3 c1, float a1, int kind, float stretch, float drag) {
  if (particles.size() > 14000) return;
  Particle p;
  p.pos = pos; p.vel = vel; p.life = p.maxLife = life; p.size0 = s0; p.size1 = s1;
  p.c0[0] = c0.x; p.c0[1] = c0.y; p.c0[2] = c0.z; p.c0[3] = a0;
  p.c1[0] = c1.x; p.c1[1] = c1.y; p.c1[2] = c1.z; p.c1[3] = a1;
  p.kind = kind; p.stretch = stretch; p.drag = drag;
  particles.push_back(p);
}
void World::fxSparks(Vec3 pos, Vec3 baseVel, Vec3 dir, Vec3 color, int n, float speed) {
  for (int i = 0; i < n; ++i) {
    Vec3 v = normalize(dir * 0.6f + rng.unit()) * rng.range(0.3f, 1.0f) * speed;
    emit(pos, baseVel + v, rng.range(0.25f, 0.8f), 0.07f, 0.02f, color * 2.5f, 0, color * 0.3f, 0, 2, 0.035f, 1.5f);
  }
}
void World::fxSmoke(Vec3 pos, Vec3 vel, float size) {
  float g = rng.range(0.02f, 0.07f);
  emit(pos, vel + rng.unit() * 1.5f, rng.range(1.2f, 2.6f), size * 0.5f, size * 2.6f, Vec3(g) * 0.55f, 0.55f, Vec3(g) * 0.0f, 0.0f, 3, 0, 0.6f);
}
void World::fxExplosion(Vec3 pos, Vec3 vel, float size) {
  emit(pos, vel, 0.16f, size * 5.0f, size * 8.0f, Vec3(1.0f, 0.85f, 0.6f) * 2.0f, 0, Vec3(1.0f, 0.4f, 0.1f) * 0.0f, 0, 0);
  int n = (int)(8 + size * 7);
  for (int i = 0; i < n; ++i) {
    Vec3 v = rng.inSphere() * size * 7.0f;
    emit(pos + v * 0.03f, vel + v, rng.range(0.35f, 1.0f), size * rng.range(0.5f, 1.1f), size * rng.range(1.4f, 2.4f),
         Vec3(1.0f, 0.55f, 0.15f) * 1.3f, 0, Vec3(0.5f, 0.06f, 0.0f) * 0.0f, 0, 0, 0, 2.0f);
  }
  for (int i = 0; i < n / 2; ++i) fxSmoke(pos + rng.inSphere() * size, vel + rng.inSphere() * size * 3.0f, size * 1.2f);
  fxSparks(pos, vel, Vec3(0, 0, 0), Vec3(1.0f, 0.7f, 0.3f), n, size * 22.0f);
  if (size > 2.5f) emit(pos, vel, 0.7f, size * 2.0f, size * 22.0f, Vec3(1.0f, 0.75f, 0.5f) * 1.2f, 0, Vec3(0.3f, 0.1f, 0.05f) * 0.0f, 0, 1);
}

// =====================================================================================
// Damage
// =====================================================================================
void World::spawnDebris(const ShipEntity& s, const std::vector<int>& idx, Vec3 dir, float speed) {
  Debris d;
  float m = 0;
  Vec3 c;
  for (int i : idx) {
    const Part& p = s.ship.parts[i];
    if (p.internal) continue;
    m += p.mass;
    c += p.pos * p.mass;
  }
  if (m <= 0) return;
  c = c / m;
  for (int i : idx) {
    const Part& p = s.ship.parts[i];
    if (p.internal) continue;
    d.pieces.push_back({p.mesh, p.pos - c, p.rot, p.scale, p.color, p.material, p.seed, std::max(0.55f, 1.0f - clampf(p.hp / p.maxHp, 0.0f, 1.0f)), p.emissive * 0.2f});
  }
  d.pos = s.pos + rotate(s.rot, c);
  d.rot = s.rot;
  d.vel = s.vel + rotate(s.rot, cross(s.angVel, c)) + dir * speed + rng.unit() * speed * 0.4f;
  d.angVel = rng.unit() * rng.range(0.4f, 2.2f);
  d.life = d.maxLife = rng.range(22, 40);
  if (debris.size() > 240) debris.erase(debris.begin());
  debris.push_back(std::move(d));
}

void World::destroyPart(ShipEntity& s, int idx, Vec3 dirWorld) {
  std::vector<int> sub;
  s.ship.subtree(idx, sub);
  Part& root = s.ship.parts[idx];
  Vec3 wp = s.pos + rotate(s.rot, root.pos);
  Vec3 outward = normalize(rotate(s.rot, root.pos - s.ship.stats.com) + dirWorld * 0.5f, dirWorld);
  for (int i : sub) s.ship.parts[i].alive = false;
  s.partsLost += (int)sub.size();
  root.hp = 0;

  float size = std::max(0.5f, std::cbrt(root.scale.x * root.scale.y * root.scale.z));
  fxExplosion(wp, s.vel, std::min(size, 2.4f));
  sound(SFX_PART_LOST, wp, clampf(size * 0.45f, 0.35f, 1.2f), s.id);
  if (!root.internal && rng.chance(0.7f)) spawnDebris(s, {idx}, outward, rng.range(8, 20));
  if (sub.size() > 1) {
    std::vector<int> rest(sub.begin() + 1, sub.end());
    spawnDebris(s, rest, outward, rng.range(3, 9));
  }

  ShipStats old = s.ship.stats;
  s.ship.recompute();
  const ShipStats& st = s.ship.stats;
  if (s.player) {
    shake = std::max(shake, 0.6f);
    if (root.role != Role::Detail && root.role != Role::Light) say("LOST: " + root.name, kRed);
    for (int e = 0; e < 2; ++e) {
      if (old.engine[e] > 0 && st.engine[e] <= 0) say(std::string(sideName(e)) + " ENGINE OFFLINE", kRed, 7);
      if (old.weapon[e] > 0 && st.weapon[e] <= 0) say(std::string("WEAPON ") + (e ? "2" : "1") + " OFFLINE: " + WEAPONS[s.ship.weapon[e]].name, kRed, 7);
    }
    if (old.canopy && !st.canopy) say("CANOPY BREACHED", kRed, 7);
    if (old.shieldGen > 0 && st.shieldGen <= 0) say("SHIELD GENERATOR DESTROYED", kRed, 7);
    if (old.sensors > 0.01f && st.sensors <= 0.01f) say("SENSORS OFFLINE - NO TARGET LEAD", kAmber, 7);
  }
  if (st.dead) explodeShip(s);
}

void World::damagePart(ShipEntity& s, int idx, float dmg, Vec3 dirWorld, bool bleed) {
  if (!s.alive || idx < 0) return;
  Part& p = s.ship.parts[idx];
  if (!p.alive || dmg <= 0) return;
  if (p.role == Role::Armor) dmg *= 0.7f;
  if (p.role == Role::Cockpit && !s.ship.stats.canopy) dmg *= 1.3f;
  p.hp -= dmg;
  p.flash = 0.12f;
  s.hitFlash = 0.25f;
  // bleed-through into components housed inside this part once its structure is compromised
  if (bleed) {
    float open = 1.0f - clampf(p.hp / p.maxHp, 0.0f, 1.0f);
    if (open > 0.35f)
      for (int i = 0; i < (int)s.ship.parts.size(); ++i) {
        Part& c = s.ship.parts[i];
        if (c.alive && c.internal && c.parent == idx) {
          c.hp -= dmg * open * 0.55f;
          if (c.hp <= 0) { destroyPart(s, i, dirWorld); if (!s.alive) return; }
        }
      }
  }
  if (p.hp <= 0) destroyPart(s, idx, dirWorld);
  else s.ship.recompute();
}

void World::applyHit(ShipEntity& s, int part, Vec3 hitLocal, const WeaponDef& w, float dmgScale, Vec3 dirWorld) {
  float dmg = w.damage * dmgScale;
  Vec3 hitWorld = s.pos + rotate(s.rot, hitLocal);
  if (s.shield > 0) {
    float need = dmg * w.shieldMul;
    s.shieldFlash = 1.0f;
    s.shieldDelay = 5.0f;
    sound(SFX_SHIELD_HIT, hitWorld, w.heavy ? 1.3f : 0.8f, s.id);
    emit(hitWorld, s.vel, 0.22f, 1.2f, 3.5f, Vec3(0.3f, 0.6f, 1.0f) * 1.5f, 0, Vec3(0.1f, 0.3f, 1.0f) * 0.0f, 0, 0);
    if (s.shield >= need) { s.shield -= need; return; }
    dmg *= 1.0f - s.shield / need;
    s.shield = 0;
    if (s.player) { say("SHIELDS DOWN", kAmber, 3); sound(SFX_SHIELD_DOWN, hitWorld, 1.0f, s.id); }
  }
  s.shieldDelay = 5.0f;
  dmg *= w.hullMul;
  sound(w.heavy ? SFX_HULL_HEAVY : SFX_HULL_HIT, hitWorld, 1.0f, s.id);
  fxSparks(hitWorld, s.vel, -dirWorld, lerpv(w.color, Vec3(1.0f, 0.7f, 0.3f), 0.6f), w.heavy ? 22 : 5, w.heavy ? 45.0f : 25.0f);
  if (w.heavy) emit(hitWorld, s.vel, 0.2f, 2.0f, 5.0f, w.color * 1.8f, 0, w.color * 0.0f, 0, 0);
  if (s.player) shake = std::max(shake, w.heavy ? 1.0f : 0.3f);
  damagePart(s, part, dmg, dirWorld);
  if (w.splash > 0 && s.alive) {
    fxExplosion(hitWorld, s.vel, w.splash * 0.45f);
    sound(SFX_EXPLOSION, hitWorld, 0.8f, s.id);
    for (int i = 0; i < (int)s.ship.parts.size() && s.alive; ++i) {
      Part& p = s.ship.parts[i];
      if (!p.alive || p.internal || i == part) continue;
      float d = length(p.pos - hitLocal) - 0.3f * std::max(p.scale.x, std::max(p.scale.y, p.scale.z));
      if (d < w.splash) damagePart(s, i, dmg * 0.55f * (1.0f - std::max(d, 0.0f) / w.splash), dirWorld, false);
    }
  }
}

void World::explodeShip(ShipEntity& s) {
  if (!s.alive) return;
  s.alive = false;
  // break what is left into chunks along the structural tree
  std::vector<int> sub, loose;
  for (int i = 0; i < (int)s.ship.parts.size(); ++i) {
    Part& p = s.ship.parts[i];
    if (!p.alive || p.parent != 0) continue;
    s.ship.subtree(i, sub);
    Vec3 dir = normalize(rotate(s.rot, p.pos) + rng.unit() * 0.5f);
    spawnDebris(s, sub, dir, rng.range(10, 30));
    for (int k : sub) s.ship.parts[k].alive = false;
  }
  for (int i = 0; i < (int)s.ship.parts.size(); ++i)
    if (s.ship.parts[i].alive) { loose.push_back(i); s.ship.parts[i].alive = false; }
  if (!loose.empty()) spawnDebris(s, loose, rng.unit(), 6);
  fxExplosion(s.pos, s.vel, 5.5f);
  sound(SFX_EXPLOSION_BIG, s.pos, 1.3f, s.id);
  for (int i = 0; i < 4; ++i) fxExplosion(s.pos + rng.inSphere() * 6.0f, s.vel + rng.inSphere() * 10.0f, rng.range(2.0f, 3.5f));
  float d = length(s.pos - lastPlayerPos);
  shake = std::max(shake, s.player ? 3.0f : clampf(250.0f / (d + 30.0f), 0.0f, 2.0f));
  if (s.player) {
    say("SHIP DESTROYED", kRed, 10);
  } else {
    enemiesAlive = std::max(0, enemiesAlive - 1);
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%s DESTROYED  -  %d REMAINING", s.callsign.c_str(), enemiesAlive);
    say(buf, kGreen, 6);
  }
}

void World::damageAsteroid(size_t idx, float dmg, Vec3 hitPos, Vec3 dir) {
  const bool ice = asteroids[idx].material == MAT_ICE, wreck = asteroids[idx].material == MAT_HULL;
  Vec3 dust = ice ? Vec3(0.6f, 0.8f, 1.0f) : (wreck ? Vec3(1.0f, 0.7f, 0.35f) : Vec3(0.8f, 0.6f, 0.4f));
  fxSparks(hitPos, asteroids[idx].vel, -dir, dust, 4, 18.0f);
  sound(SFX_ROCK_HIT, hitPos, 0.8f);
  emit(hitPos, asteroids[idx].vel - dir * 4.0f, 0.9f, 0.8f, 3.0f, dust * 0.08f, 0.25f, dust * 0.0f, 0.0f, 3, 0, 1.0f);
  float overkill;
  {
    Asteroid& a = asteroids[idx];
    a.hp -= dmg;
    overkill = -a.hp;
    if (!wreck) a.damage = std::max(a.damage, 1.0f - clampf(a.hp / a.maxHp, 0.0f, 1.0f)); // cracks spread as it weakens
    if (a.hp > 0) {
      // spall: a solid hit knocks chips off the surface
      if (a.radius > 5 && rng.chance(clampf(dmg / 50.0f, 0.1f, 1.0f))) {
        int chips = dmg > 50 ? 3 : 1;
        for (int i = 0; i < chips && debris.size() < 240; ++i) {
          Debris d;
          float s = rng.range(0.5f, 1.6f) * (dmg > 50 ? 1.4f : 1.0f);
          int mesh = a.mesh == M_CRYSTAL ? M_CRYSTAL : (wreck ? M_BOX : M_ASTLO0 + rng.irange(0, AST_VARIANTS - 1));
          d.pieces.push_back({mesh, Vec3(), Quat(), Vec3(s, s * rng.range(0.6f, 1.0f), s * rng.range(0.6f, 1.0f)), a.color, a.material, rng.uniform(), wreck ? 0.6f : 0.0f, 0.0f});
          d.pos = hitPos;
          d.rot = normalize(Quat(rng.range(-1, 1), rng.range(-1, 1), rng.range(-1, 1), rng.range(-1, 1)));
          d.vel = a.vel + normalize(-dir + rng.unit() * 0.9f) * rng.range(6, 22);
          d.angVel = rng.unit() * rng.range(1, 5);
          d.life = d.maxLife = rng.range(8, 16);
          d.smoke = 100; // chips do not trail smoke
          debris.push_back(std::move(d));
        }
      }
      return;
    }
  }
  fractureAsteroid(idx, dir, overkill);
}

// Break an asteroid apart. Volume (and so mass) is shared out among the fragments, with a
// little lost as dust; the fragments separate across the line of the shot, carry the
// parent's motion and spin, and their separation velocities sum to zero momentum.
void World::fractureAsteroid(size_t idx, Vec3 dir, float overkill) {
  const Asteroid a = asteroids[idx];
  asteroids[idx].alive = false;
  const bool ice = a.material == MAT_ICE, wreck = a.material == MAT_HULL;
  Vec3 dustCol = ice ? Vec3(0.55f, 0.7f, 0.85f) : (wreck ? Vec3(0.25f, 0.22f, 0.2f) : Vec3(0.42f, 0.34f, 0.27f));
  float R = a.radius;
  // dust cloud, flash and flying grit
  emit(a.pos, a.vel, 0.18f, R * 1.2f, R * 2.4f, Vec3(1.0f, 0.85f, 0.65f) * 1.2f, 0, Vec3(0), 0, 0);
  int puffs = (int)clampf(R * 1.2f, 8.0f, 46.0f);
  for (int i = 0; i < puffs; ++i) {
    Vec3 o = rng.inSphere();
    emit(a.pos + o * R * 0.8f, a.vel + o * rng.range(2.0f, 9.0f), rng.range(1.5f, 3.5f), R * 0.3f, R * rng.range(0.7f, 1.3f), dustCol * 0.35f, 0.45f, dustCol * 0.0f, 0.0f, 3, 0, 0.5f);
  }
  fxSparks(a.pos, a.vel, Vec3(0, 0, 0), ice ? Vec3(0.7f, 0.9f, 1.0f) : Vec3(1.0f, 0.75f, 0.45f), puffs, 12.0f + R);
  if (wreck) fxExplosion(a.pos, a.vel, std::min(R * 0.3f, 3.0f));
  sound(SFX_EXPLOSION, a.pos, clampf(R / 22.0f, 0.5f, 1.4f));
  if (R > 60) shake = std::max(shake, clampf(R * 40.0f / (length(a.pos - lastPlayerPos) + 50.0f), 0.0f, 2.0f));
  if (R < 4.5f || asteroids.size() > 700) return; // too small to leave more than rubble

  int n = 2 + (rng.chance(0.55f) ? 1 : 0) + (R > 35 && rng.chance(0.6f) ? 1 : 0);
  float f[4], sum = 0;
  for (int i = 0; i < n; ++i) { f[i] = rng.range(0.35f, 1.0f); sum += f[i]; }
  // fragments part across the shot line, like a rock split by a wedge
  Vec3 u1 = normalize(cross(dir, rng.unit()), Vec3(1, 0, 0)), u2 = normalize(cross(dir, u1), Vec3(0, 1, 0));
  float speed = clampf(2.5f + overkill * 0.04f + 12.0f / std::sqrt(R), 2.5f, 16.0f);
  float phase = rng.range(0, 2 * PI);
  int kind = a.mesh == M_CRYSTAL ? 2 : (ice ? 1 : (wreck ? 3 : 0));
  Rng r(rng.next());
  size_t first = asteroids.size();
  for (int i = 0; i < n; ++i) {
    float frac = f[i] / sum * 0.88f;
    float ri = R * std::cbrt(frac);
    if (ri < 2.2f) continue;
    float ang = phase + 2 * PI * i / n + rng.range(-0.35f, 0.35f);
    Vec3 d = normalize(u1 * std::cos(ang) + u2 * std::sin(ang) + dir * rng.range(-0.1f, 0.45f));
    Vec3 pos = a.pos + d * (R - ri) * 0.95f;
    size_t before = asteroids.size();
    addAsteroid(r, pos, wreck ? ri / 0.85f : ri, kind);
    if (asteroids.size() == before) continue;
    Asteroid& c = asteroids.back();
    c.mass = a.mass * frac;
    c.color = a.color;
    c.vel = a.vel + cross(a.angVel, pos - a.pos) + d * speed * rng.range(0.7f, 1.3f);
    c.angVel = a.angVel + rng.unit() * rng.range(0.1f, 0.9f) * std::min(1.0f, 12.0f / ri);
    c.hp = c.maxHp * 0.8f; // already cracked
  }
  // remove any net momentum the separation added: the pieces as a whole keep moving as the parent did
  float m = 0;
  Vec3 mom;
  for (size_t i = first; i < asteroids.size(); ++i) { m += asteroids[i].mass; mom += (asteroids[i].vel - a.vel) * asteroids[i].mass; }
  if (m > 0)
    for (size_t i = first; i < asteroids.size(); ++i) asteroids[i].vel -= mom / m;
}

bool World::debugHitAsteroid(size_t idx, float dmg, Vec3 dir) {
  if (idx >= asteroids.size() || !asteroids[idx].alive) return false;
  damageAsteroid(idx, dmg, asteroids[idx].pos - dir * asteroids[idx].radius, dir);
  return true;
}

// =====================================================================================
// Flight physics
// =====================================================================================
void World::stepShip(ShipEntity& s, float dt) {
  const ShipStats& st = s.ship.stats;
  const Controls& in = s.in;
  const float bm = s.ship.baseMass;
  Quat q = s.rot;
  Vec3 vl = rotate(conj(q), s.vel);

  // ---- available forces come from the surviving hardware
  float engSum = 0.5f * (st.engine[0] + st.engine[1]);
  s.boosting = in.boost && s.energy > 2.0f && in.thrust > 0.1f && engSum > 0;
  float boostMul = s.boosting ? kBoostThrust : 1.0f;
  float Fmain = kMainAccel * bm * engSum * boostMul;
  float Fretro = kRetroAccel * bm * st.rcs;
  float Flat = kRcsAccel * bm * st.rcs;

  Vec3 F(0, 0, 0);
  if (in.assist || in.brake) {
    // flight assist: the computer fires thrusters to chase a target velocity. It is still
    // bound by the thrust that is physically available.
    float vmax = s.boosting ? kBoostSpeed : kMaxSpeed;
    Vec3 want(in.strafeX * kStrafeSpeed, in.strafeY * kStrafeSpeed, in.thrust > 0 ? -in.thrust * vmax : -in.thrust * kReverseSpeed);
    if (in.brake) want = Vec3(0, 0, 0);
    F = (want - vl) * (2.6f * st.mass);
  } else {
    // decoupled: pure Newtonian. Thrust only changes velocity; nothing slows you down.
    F = Vec3(in.strafeX * Flat, in.strafeY * Flat, in.thrust > 0 ? -in.thrust * Fmain : -in.thrust * Fretro);
  }
  F.z = F.z < 0 ? std::max(F.z, -Fmain) : std::min(F.z, Fretro);
  F.x = clampf(F.x, -Flat, Flat);
  F.y = clampf(F.y, -Flat, Flat);
  s.mainOut = Fmain > 1 ? clampf(-F.z / Fmain, 0.0f, 1.0f) : 0.0f;

  // ---- each engine pushes at its own nozzle: uneven thrust twists the ship
  Vec3 torque(0, 0, 0);
  for (int e = 0; e < 2; ++e) {
    float Fe = s.mainOut * kMainAccel * bm * 0.5f * st.engine[e] * boostMul;
    // the drive keeps burning at part power while the throttle is held, even when the computer needs little thrust
    float burn = std::max(s.mainOut, in.thrust > 0 ? 0.38f * in.thrust : 0.0f);
    s.engineOut[e] = burn * st.engine[e] * (s.boosting ? 1.5f : 1.0f);
    // The engines are trimmed to push through the undamaged centre of mass, so only the sideways
    // arm and any shift of the centre of mass caused by lost parts produce a turning moment.
    Vec3 arm(st.enginePos[e].x - st.com.x, s.ship.baseCom.y - st.com.y, 0);
    torque += cross(arm, Vec3(0, 0, -Fe)) * 0.3f; // gimbals absorb part of it
  }

  // ---- attitude control: rate command, limited by surviving control authority
  Vec3 w = s.angVel;
  // with weak control authority the flight computer also limits the rate it will command
  Vec3 wWant(in.pitch * kTurnRate[0] * (0.45f + 0.55f * st.authority[0]), -in.yaw * kTurnRate[1] * (0.45f + 0.55f * st.authority[1]),
             -in.roll * kTurnRate[2] * (0.45f + 0.55f * st.authority[2]));
  Vec3 I = st.inertia;
  float cap[3];
  for (int a = 0; a < 3; ++a) {
    cap[a] = kAngAccel[a] * s.ship.baseInertia[a] * st.authority[a];
    torque[a] += clampf((wWant[a] - w[a]) * 10.0f * I[a], -cap[a], cap[a]);
  }
  // lopsided wings: manoeuvring loads roll the ship toward the damaged side
  float load = std::fabs(in.pitch) + 0.5f * std::fabs(in.yaw) + 0.3f * s.mainOut;
  torque.z += st.asym * kAngAccel[2] * s.ship.baseInertia.z * 0.3f * load;

  // Euler's rigid-body equations in the body frame
  Vec3 Iw = mulv(I, w);
  Vec3 alpha = divv(torque - cross(w, Iw), I);
  s.angVel += alpha * dt;
  s.vel += rotate(q, F) * (dt / st.mass);
  s.pos += s.vel * dt;
  s.rot = integrateLocal(s.rot, s.angVel, dt);

  // ---- energy and shields
  s.energy = std::min(100.0f, s.energy + 14.0f * (0.25f + 0.75f * st.reactor) * dt);
  if (s.boosting) s.energy = std::max(0.0f, s.energy - 30.0f * dt);
  s.shieldDelay -= dt;
  float cap2 = s.shieldMax * st.shieldGen;
  if (s.shieldDelay <= 0 && s.shield < cap2) s.shield = std::min(cap2, s.shield + (s.player ? 12.0f : 5.0f) * dt);
  if (s.shield > cap2) s.shield = cap2;
  s.shieldFlash = std::max(0.0f, s.shieldFlash - dt * 3.5f);
  s.hitFlash = std::max(0.0f, s.hitFlash - dt);
  for (auto& p : s.ship.parts)
    if (p.flash > 0) p.flash -= dt;

  // ---- exhaust and damage effects
  Vec3 back = rotate(q, Vec3(0, 0, 1));
  s.trailT += dt;
  if (s.trailT > 1.0f / 70.0f) {
    s.trailT = 0;
    for (int e = 0; e < 2; ++e) {
      float out = s.engineOut[e];
      if (out < 0.04f) continue;
      Vec3 np = s.pos + rotate(q, st.enginePos[e] + Vec3(0, 0, 0.9f));
      Vec3 g = s.ship.glow;
      emit(np + rng.inSphere() * 0.15f, s.vel + back * (45.0f + 60.0f * out) + rng.inSphere() * 1.5f, 0.10f + 0.16f * out, 0.34f, 0.06f,
           g * 0.8f, 0, Vec3(0.25f, 0.1f, 0.5f) * 0.0f, 0, 0);
      if (st.engine[e] < 0.6f && rng.chance(0.25f)) fxSmoke(np, s.vel + back * 12.0f, 0.7f);
    }
  }
  s.smokeT += dt;
  if (s.smokeT > 0.1f) {
    s.smokeT = 0;
    const Part& p = s.ship.parts[rng.irange(0, (int)s.ship.parts.size() - 1)];
    if (p.alive && !p.internal && p.role != Role::Light && p.hp < p.maxHp * 0.45f) {
      Vec3 wp = s.pos + rotate(q, p.pos);
      fxSmoke(wp, s.vel, 0.7f);
      if (rng.chance(0.3f)) fxSparks(wp, s.vel, rng.unit(), Vec3(1.0f, 0.7f, 0.3f), 3, 9.0f);
    }
  }
}

void World::fireWeapons(ShipEntity& s, float dt) {
  const ShipStats& st = s.ship.stats;
  for (int k = 0; k < 2; ++k) {
    s.reload[k] -= dt;
    s.misfire[k] -= dt;
    if (!s.in.fire[k] || s.reload[k] > 0) continue;
    float cond = st.weapon[k];
    if (cond <= 0) continue;
    int wid = s.ship.weapon[k];
    const WeaponDef& w = WEAPONS[wid];
    if (s.energy < w.energy) continue;
    // a damaged weapon cycles slower, scatters its shots and sometimes misfires
    float rl = w.reload / (0.2f + 0.8f * cond * cond);
    if (!s.player) rl *= (w.heavy ? 2.2f : 1.6f) * DIFFICULTY[difficulty].reload;
    s.reload[k] = s.reloadMax[k] = rl;
    s.energy -= w.energy;
    Vec3 mw = s.pos + rotate(s.rot, s.ship.muzzle[k]);
    if (rng.uniform() < (1.0f - cond) * 0.3f) {
      fxSparks(mw, s.vel, rng.unit(), Vec3(1.0f, 0.8f, 0.4f), 6, 10.0f);
      s.misfire[k] = 0.6f;
      if (s.player) sound(SFX_MISFIRE, mw, 1.0f, s.id);
      continue;
    }
    Vec3 fwd = rotate(s.rot, Vec3(0, 0, -1));
    Vec3 dir = normalize(s.pos + fwd * kConvergence - mw);
    float spread = w.spread + (1.0f - cond) * 0.05f + (s.player ? 0.0f : (1.0f - s.skill) * 0.012f);
    dir = normalize(dir + rng.inSphere() * spread);
    Projectile p;
    p.pos = p.prev = mw;
    p.vel = s.vel + dir * w.speed;
    p.life = w.life;
    p.weapon = wid; p.owner = s.id; p.team = s.team; p.target = s.target;
    p.dmgScale = s.player ? DIFFICULTY[difficulty].playerDamage : enemyDamageMul;
    projectiles.push_back(p);
    s.shotsFired++;
    sound(wid, mw, 1.0f, s.id);
    // recoil: momentum is conserved
    Vec3 J = dir * (-w.projMass * w.speed);
    s.vel += J / st.mass;
    Vec3 tl = rotate(conj(s.rot), cross(mw - s.pos, J));
    s.angVel += divv(tl, st.inertia);
    emit(mw, s.vel, 0.07f, w.heavy ? 2.2f : 0.9f, w.heavy ? 3.5f : 1.4f, w.color * 2.0f, 0, w.color * 0.0f, 0, 0);
    if (s.player && w.heavy) shake = std::max(shake, 0.5f);
  }
}

// =====================================================================================
// AI
// =====================================================================================
void World::updateAI(ShipEntity& s, ShipEntity* t, float dt) {
  Controls& c = s.in;
  c = Controls();
  if (!t || !t->alive) { c.brake = true; return; }
  const ShipStats& st = s.ship.stats;
  Vec3 rel = t->pos - s.pos;
  float dist = length(rel);
  Vec3 toT = rel / std::max(dist, 1.0f);

  int lead = st.weapon[0] > 0 ? 0 : 1;
  bool noGuns = st.weapon[0] <= 0 && st.weapon[1] <= 0;
  float ps = WEAPONS[s.ship.weapon[lead]].speed;
  if (WEAPONS[s.ship.weapon[lead]].homing) ps = 400;
  float tt = std::min(dist / ps, 3.0f);
  s.noiseT -= dt;
  if (s.noiseT <= 0) { s.noiseT = rng.range(0.4f, 0.9f); s.aimNoise = rng.inSphere() * (1.0f - s.skill) * 0.03f; }
  Vec3 aimDir = normalize(t->pos + (t->vel - s.vel) * tt + s.aimNoise * dist - s.pos);
  Vec3 moveDir = aimDir;
  float thrust = 1.0f;
  bool boost = false;

  s.aiTimer -= dt;
  switch (s.aiState) {
    case AI_ATTACK:
      thrust = dist > 700 ? 1.0f : (dist > 300 ? 0.75f : 0.5f);
      boost = dist > 1600;
      if (s.aiHold && !noGuns) { // circle at stand-off range until a slot opens
        Vec3 tangent = normalize(cross(toT, Vec3(0.2f, 1.0f, 0.1f)));
        float want = 750.0f;
        moveDir = normalize(tangent + toT * clampf((dist - want) / 300.0f, -1.0f, 1.0f));
        thrust = 0.7f;
        break;
      }
      if (noGuns) { thrust = 1.0f; boost = dist < 500; moveDir = normalize(rel + (t->vel - s.vel) * 0.5f); } // nothing left but ramming
      else if (dist < 110 + 60 * (1.0f - s.skill) + std::max(0.0f, -dot(t->vel - s.vel, toT)) * 1.1f) {
        s.aiState = AI_BREAK;
        s.aiTimer = rng.range(1.6f, 3.2f);
        s.aiVec = normalize(cross(toT, rng.unit()) + toT * 0.25f);
      } else if (s.shield <= 0 && st.hullIntegrity < 0.75f && s.aiTimer < -6 && rng.chance(0.3f * dt)) {
        s.aiState = AI_EVADE;
        s.aiTimer = rng.range(2.0f, 4.0f);
        s.aiVec = rng.unit();
      }
      break;
    case AI_BREAK:
      moveDir = s.aiVec;
      thrust = 1.0f;
      boost = true;
      if ((s.aiTimer < 0 && dist > 320) || s.aiTimer < -3) { s.aiState = AI_ATTACK; s.aiTimer = 0; }
      break;
    case AI_EVADE:
      if (std::fmod(s.aiTimer, 1.0f) < dt) s.aiVec = normalize(s.aiVec + rng.unit() * 0.9f);
      moveDir = normalize(s.aiVec - toT * 0.3f);
      boost = true;
      if (s.aiTimer < 0) { s.aiState = AI_ATTACK; s.aiTimer = 0; }
      break;
  }

  // ---- obstacle avoidance: look along the flight path and steer around what is in it
  Vec3 fwd = rotate(s.rot, Vec3(0, 0, -1));
  float speed = length(s.vel);
  Vec3 velDir = speed > 8 ? s.vel / speed : fwd;
  Vec3 avoid(0, 0, 0);
  float look = 220 + speed * 2.2f;
  for (auto& a : asteroids) {
    Vec3 d = a.pos - s.pos;
    float along = dot(d, velDir);
    float reach = look + a.radius;
    if (along < -a.radius || along > reach) continue;
    Vec3 perp = d - velDir * along;
    float pd = length(perp), clearR = a.radius + 28;
    if (pd < clearR) {
      Vec3 away = pd > 0.5f ? perp * (-1.0f / pd) : normalize(cross(velDir, Vec3(0.3f, 1, 0.2f)));
      avoid += away * ((1.0f - pd / clearR) * (1.0f - std::max(along, 0.0f) / reach) * 4.0f + (length(d) < a.radius + 40 ? 3.0f : 0.0f));
    }
  }
  for (auto& o : ships) {
    if (!o.alive || o.id == s.id || (o.id == t->id && noGuns)) continue;
    Vec3 d = s.pos - o.pos;
    float dl = length(d);
    if (dl < 70 && dl > 0.1f && o.id != t->id) avoid += d * ((70 - dl) / (70 * dl)) * 2.0f;
  }
  float avoidMag = length(avoid);
  Vec3 steer = normalize(moveDir + avoid * 1.6f, fwd);
  if (avoidMag > 1.5f) thrust = std::max(thrust, 0.8f);

  // ---- turn toward the steering direction (rate command; the ship's own limits apply)
  Vec3 l = rotate(conj(s.rot), steer);
  float yawErr = std::atan2(l.x, -l.z);
  float pitchErr = std::atan2(l.y, std::sqrt(l.x * l.x + l.z * l.z));
  float gain = 3.0f + 3.0f * s.skill;
  // feed-forward: match the rate at which the line of sight is rotating, then correct the error
  Vec3 losRate(0, 0, 0);
  if (s.aiState == AI_ATTACK && avoidMag < 0.5f && !s.aiHold)
    losRate = rotate(conj(s.rot), cross(toT, t->vel - s.vel) * (1.0f / std::max(dist, 30.0f)));
  c.yaw = clampf(yawErr * gain / kTurnRate[1] - losRate.y / kTurnRate[1], -1, 1);
  c.pitch = clampf(pitchErr * gain / kTurnRate[0] + losRate.x / kTurnRate[0], -1, 1);
  c.roll = clampf(yawErr * 0.9f, -1, 1) * (std::fabs(yawErr) > 0.15f ? 1.0f : 0.0f);
  float ang = std::acos(clampf(l.z * -1.0f, -1.0f, 1.0f));
  c.thrust = ang > 1.3f ? thrust * 0.45f : thrust; // slow down for hard turns
  c.boost = boost && ang < 0.6f;
  c.assist = true;

  // ---- shoot in bursts when the solution is good
  s.burst -= dt;
  if (s.burst < -(0.7f + 1.3f * (1.0f - s.skill))) s.burst = rng.range(0.8f, 1.8f);
  if (s.aiState == AI_ATTACK && avoidMag < 2.5f && !s.aiHold) {
    Vec3 la = rotate(conj(s.rot), aimDir);
    float aimAng = std::acos(clampf(-la.z, -1.0f, 1.0f));
    for (int k = 0; k < 2; ++k) {
      if (st.weapon[k] <= 0) continue;
      const WeaponDef& w = WEAPONS[s.ship.weapon[k]];
      float range = w.homing ? 1600.0f : std::min(w.speed * w.life * 0.7f, 1300.0f);
      float cone = w.homing ? 0.3f : (w.heavy ? 0.004f : 0.008f) + (w.heavy ? 3.5f : 5.0f) / std::max(dist, 50.0f) + (1.0f - s.skill) * 0.01f;
      bool ok = dist < range && aimAng < cone && (w.heavy || s.burst > 0 || s.player);
      if (w.homing && dist < 180) ok = false;
      c.fire[k] = ok;
    }
  }
}

// =====================================================================================
// Projectiles and collisions
// =====================================================================================
void World::stepProjectiles(float dt) {
  for (size_t i = 0; i < projectiles.size(); ++i) {
    Projectile p = projectiles[i];
    const WeaponDef& w = WEAPONS[p.weapon];
    p.prev = p.pos;
    if (w.homing) {
      float sp = length(p.vel);
      Vec3 cur = p.vel / std::max(sp, 1.0f);
      if (p.target >= 0 && p.target < (int)ships.size() && ships[p.target].alive && p.life < w.life - 0.25f) {
        const ShipEntity& t = ships[p.target];
        float d = length(t.pos - p.pos);
        Vec3 want = normalize(t.pos + t.vel * std::min(d / std::max(sp, 50.0f), 2.0f) - p.pos);
        Vec3 delta = want - cur;
        float dl = length(delta), maxTurn = 1.5f * dt;
        if (dl > maxTurn) delta = delta * (maxTurn / dl);
        cur = normalize(cur + delta);
      }
      sp = std::min(sp + 260.0f * dt, 520.0f);
      p.vel = cur * sp;
      p.trail += dt;
      if (p.trail > 0.02f) {
        p.trail = 0;
        emit(p.pos, p.vel * 0.1f + rng.inSphere() * 2.0f, 0.5f, 0.35f, 1.3f, Vec3(0.12f) * 0.4f, 0.4f, Vec3(0), 0, 3, 0, 1.0f);
        emit(p.pos, p.vel * 0.3f, 0.12f, 0.7f, 0.2f, Vec3(1.0f, 0.6f, 0.2f) * 1.5f, 0, Vec3(0), 0, 0);
      }
    }
    p.pos += p.vel * dt;
    p.life -= dt;

    Vec3 seg = p.pos - p.prev;
    float segLen2 = dot(seg, seg);
    float bestT = 1.0f;
    int hitAst = -1, hitShip = -1, hitPart = -1;
    Vec3 hitLocal;
    for (size_t ai = 0; ai < asteroids.size(); ++ai) {
      const Asteroid& a = asteroids[ai];
      if (!a.alive) continue;
      Vec3 oc = p.prev - a.pos;
      float C = dot(oc, oc) - a.radius * a.radius;
      float t;
      if (C <= 0) t = 0;
      else {
        float B = dot(oc, seg);
        if (B >= 0) continue;
        float disc = B * B - segLen2 * C;
        if (disc < 0) continue;
        t = (-B - std::sqrt(disc)) / segLen2;
      }
      if (t <= bestT) { bestT = t; hitAst = (int)ai; }
    }
    for (auto& s : ships) {
      if (!s.alive || s.id == p.owner || s.team == p.team) continue;
      Vec3 rel0 = p.prev - s.pos + s.vel * dt, rel1 = p.pos - s.pos;
      Vec3 d = rel1 - rel0;
      float tc = clampf(-dot(rel0, d) / std::max(dot(d, d), 1e-6f), 0.0f, 1.0f);
      if (length2(rel0 + d * tc) > sqr(kShipRadius)) continue;
      Quat inv = conj(s.rot);
      Vec3 lo = rotate(inv, rel0), ld = rotate(inv, d);
      float t;
      int part = s.ship.raycast(lo, ld, bestT, t);
      if (part >= 0) {
        bestT = t; hitShip = s.id; hitPart = part; hitLocal = lo + ld * t; hitAst = -1;
      } else if (w.homing && bestT >= 1.0f && length2(rel1) < sqr(7.0f)) { // proximity fuse
        Vec3 lp = rotate(inv, rel1);
        int np = s.ship.nearestPart(lp);
        if (np >= 0) { hitShip = s.id; hitPart = np; hitLocal = lp; hitAst = -1; }
      }
    }

    if (hitShip >= 0) {
      ShipEntity& s = ships[hitShip];
      Vec3 relVel = p.vel - s.vel;
      Vec3 dirW = normalize(relVel);
      Vec3 hitWorld = s.pos + rotate(s.rot, hitLocal);
      // momentum transfer: hits shove and spin the target
      Vec3 J = relVel * w.projMass + dirW * (w.splash > 0 ? w.damage * 60.0f : 0.0f);
      s.vel += J / s.ship.stats.mass;
      s.angVel += divv(rotate(conj(s.rot), cross(hitWorld - s.pos, J)), s.ship.stats.inertia);
      if (p.owner >= 0 && p.owner < (int)ships.size()) ships[p.owner].shotsHit++;
      applyHit(s, hitPart, hitLocal, w, p.dmgScale, dirW);
      if (w.penetrate > 0 && s.alive && s.shield <= 0) { // the slug keeps going through the ship
        Vec3 ld = normalize(rotate(conj(s.rot), dirW));
        int ignore[8] = {hitPart};
        int n = 1;
        float scale = 0.6f;
        for (int k = 0; k < w.penetrate && s.alive; ++k) {
          float t;
          int np = s.ship.raycast(hitLocal, ld, 14.0f, t, ignore, n);
          if (np < 0) break;
          damagePart(s, np, w.damage * p.dmgScale * scale, dirW);
          fxSparks(s.pos + rotate(s.rot, hitLocal + ld * t), s.vel, dirW, Vec3(1.0f, 0.8f, 0.5f), 8, 30.0f);
          ignore[n++] = np;
          scale *= 0.6f;
        }
      }
      p.alive = false;
    } else if (hitAst >= 0) {
      Vec3 hp = p.prev + seg * bestT;
      Asteroid& a = asteroids[hitAst];
      a.vel += (p.vel - a.vel) * (w.projMass / a.mass);
      if (w.splash > 0) { fxExplosion(hp, a.vel, w.splash * 0.4f); sound(SFX_EXPLOSION, hp, 0.8f); }
      damageAsteroid((size_t)hitAst, w.damage * (w.splash > 0 ? 1.7f : 1.0f), hp, normalize(p.vel));
      p.alive = false;
    } else if (p.life <= 0) {
      if (w.splash > 0) fxExplosion(p.pos, p.vel * 0.1f, 1.0f);
      p.alive = false;
    }
    projectiles[i] = p;
  }
  projectiles.erase(std::remove_if(projectiles.begin(), projectiles.end(), [](const Projectile& p) { return !p.alive; }), projectiles.end());
}

void World::collisions(float) {
  auto impact = [&](ShipEntity& s, Vec3 nWorld, float closing) { // nWorld points from the obstacle to the ship
    float dmg = std::min(std::max(0.0f, closing - 6.0f) * 1.7f, 170.0f);
    if (dmg <= 0) return;
    if (s.shield > 0) { s.shield = std::max(0.0f, s.shield - dmg * 1.5f); s.shieldFlash = 1; dmg *= 0.4f; }
    s.shieldDelay = 4;
    Vec3 cl = rotate(conj(s.rot), nWorld * -kShipCollide);
    fxSparks(s.pos - nWorld * kShipCollide, s.vel, nWorld, Vec3(1.0f, 0.7f, 0.3f), 14, 20.0f);
    sound(SFX_COLLISION, s.pos, clampf(dmg / 60.0f, 0.3f, 1.2f), s.id);
    if (s.player) { shake = std::max(shake, 1.5f); say("COLLISION", kAmber, 2); }
    int np = s.ship.nearestPart(cl);
    if (np < 0) return;
    Vec3 c = s.ship.parts[np].pos;
    for (int i = 0; i < (int)s.ship.parts.size() && s.alive; ++i) {
      Part& p = s.ship.parts[i];
      if (!p.alive || p.internal) continue;
      float d = length(p.pos - c);
      if (d < 3.5f) damagePart(s, i, dmg * (1.0f - d / 3.5f), -nWorld, i == np);
    }
  };

  for (auto& s : ships) {
    if (!s.alive) continue;
    for (auto& a : asteroids) {
      if (!a.alive) continue;
      Vec3 d = s.pos - a.pos;
      float R = kShipCollide + a.radius, d2 = length2(d);
      if (d2 >= R * R) continue;
      float dist = std::sqrt(d2);
      Vec3 n = dist > 1e-3f ? d / dist : Vec3(0, 1, 0);
      float vn = dot(s.vel - a.vel, n);
      float ms = s.ship.stats.mass;
      s.pos += n * (R - dist);
      if (vn < 0) {
        float j = -(1.3f) * vn / (1.0f / ms + 1.0f / a.mass);
        s.vel += n * (j / ms);
        a.vel -= n * (j / a.mass);
        s.angVel += rng.unit() * std::min(-vn * 0.01f, 0.8f);
        impact(s, n, -vn);
        if (!s.alive) break;
      }
    }
  }
  for (size_t i = 0; i < ships.size(); ++i)
    for (size_t k = i + 1; k < ships.size(); ++k) {
      ShipEntity &a = ships[i], &b = ships[k];
      if (!a.alive || !b.alive) continue;
      Vec3 d = a.pos - b.pos;
      float R = kShipCollide * 2, d2 = length2(d);
      if (d2 >= R * R) continue;
      float dist = std::sqrt(d2);
      Vec3 n = dist > 1e-3f ? d / dist : Vec3(0, 1, 0);
      float vn = dot(a.vel - b.vel, n), ma = a.ship.stats.mass, mb = b.ship.stats.mass;
      a.pos += n * ((R - dist) * 0.5f);
      b.pos -= n * ((R - dist) * 0.5f);
      if (vn < 0) {
        float j = -1.3f * vn / (1.0f / ma + 1.0f / mb);
        a.vel += n * (j / ma);
        b.vel -= n * (j / mb);
        impact(a, n, -vn);
        impact(b, -n, -vn);
      }
    }
  // asteroid vs asteroid
  for (size_t i = 0; i < asteroids.size(); ++i) {
    Asteroid& a = asteroids[i];
    if (!a.alive) continue;
    for (size_t k = i + 1; k < asteroids.size(); ++k) {
      Asteroid& b = asteroids[k];
      if (!b.alive) continue;
      Vec3 d = a.pos - b.pos;
      float R = a.radius + b.radius;
      if (std::fabs(d.x) > R || std::fabs(d.y) > R || std::fabs(d.z) > R) continue;
      float d2 = length2(d);
      if (d2 >= R * R || d2 < 1e-4f) continue;
      float dist = std::sqrt(d2);
      Vec3 n = d / dist;
      float vn = dot(a.vel - b.vel, n);
      float wa = b.mass / (a.mass + b.mass);
      a.pos += n * ((R - dist) * wa);
      b.pos -= n * ((R - dist) * (1 - wa));
      if (vn < 0) {
        float j = -1.5f * vn / (1.0f / a.mass + 1.0f / b.mass);
        a.vel += n * (j / a.mass);
        b.vel -= n * (j / b.mass);
      }
    }
  }
}

// =====================================================================================
// Targeting and the main step
// =====================================================================================
bool World::debugDestroy(const std::string& partName) {
  ShipEntity& p = player();
  for (int i = 0; i < (int)p.ship.parts.size(); ++i)
    if (p.ship.parts[i].alive && p.ship.parts[i].name == partName) { destroyPart(p, i, Vec3(0, 1, 0)); return true; }
  return false;
}

void World::targetNearest() {
  ShipEntity& p = player();
  float best = 1e30f;
  p.target = -1;
  for (auto& s : ships) {
    if (!s.alive || s.team == p.team) continue;
    float d = length2(s.pos - p.pos);
    if (d < best) { best = d; p.target = s.id; }
  }
}
void World::cycleTarget(int dir) {
  ShipEntity& p = player();
  int n = (int)ships.size();
  int start = p.target < 0 ? 0 : p.target;
  for (int i = 1; i <= n; ++i) {
    int k = ((start + dir * i) % n + n) % n;
    if (ships[k].alive && ships[k].team != p.team) { p.target = k; return; }
  }
  p.target = -1;
}

void World::update(float dt) {
  time += dt;
  ShipEntity& pl = player();
  if (pl.alive) lastPlayerPos = pl.pos;
  if (pl.alive && (pl.target < 0 || !ships[pl.target].alive)) targetNearest();

  // hand out attack slots by distance to the player
  for (auto& s : ships) {
    if (s.player || !s.alive) continue;
    int closer = 0;
    float d = length2(s.pos - pl.pos);
    for (auto& o : ships)
      if (!o.player && o.alive && o.id != s.id && length2(o.pos - pl.pos) < d - (o.aiHold ? 0.0f : 150.0f * 150.0f)) ++closer;
    s.aiHold = closer >= DIFFICULTY[difficulty].attackers;
  }
  for (size_t i = 0; i < ships.size(); ++i) {
    ShipEntity& s = ships[i];
    if (!s.alive) continue;
    if (s.player) {
      if (autopilot) updateAI(s, s.target >= 0 ? &ships[s.target] : nullptr, dt);
    } else {
      updateAI(s, pl.alive ? &pl : nullptr, dt);
      // a ship with no engines and no guns is abandoned by its crew
      const ShipStats& st = s.ship.stats;
      if (st.engine[0] <= 0 && st.engine[1] <= 0 && st.weapon[0] <= 0 && st.weapon[1] <= 0) {
        s.abandonT += dt;
        if (s.abandonT > 4.0f) { explodeShip(s); continue; }
      }
    }
    stepShip(s, dt);
    fireWeapons(s, dt);
  }

  for (auto& a : asteroids) {
    a.pos += a.vel * dt;
    a.rot = integrateWorld(a.rot, a.angVel, dt);
  }
  collisions(dt);
  stepProjectiles(dt);
  if (!asteroids.empty())
    asteroids.erase(std::remove_if(asteroids.begin(), asteroids.end(), [](const Asteroid& a) { return !a.alive; }), asteroids.end());

  for (auto& d : debris) {
    d.pos += d.vel * dt;
    d.rot = integrateWorld(d.rot, d.angVel, dt);
    d.life -= dt;
    d.smoke += dt;
    if (d.smoke > 0.15f && d.life > d.maxLife - 5.0f) { d.smoke = 0; fxSmoke(d.pos, d.vel, 0.6f); }
  }
  debris.erase(std::remove_if(debris.begin(), debris.end(), [](const Debris& d) { return d.life <= 0; }), debris.end());

  for (auto& p : particles) {
    p.life -= dt;
    if (p.drag > 0) p.vel *= std::max(0.0f, 1.0f - p.drag * dt);
    p.pos += p.vel * dt;
  }
  particles.erase(std::remove_if(particles.begin(), particles.end(), [](const Particle& p) { return p.life <= 0; }), particles.end());

  for (auto& m : messages) m.ttl -= dt;
  messages.erase(std::remove_if(messages.begin(), messages.end(), [](const Message& m) { return m.ttl <= 0; }), messages.end());
  shake = std::max(0.0f, shake - dt * 3.0f);

  if (result == 0 && enemiesTotal > 0) {
    if (!pl.alive) { endTimer += dt; if (endTimer > 2.8f) result = 2; }
    else if (enemiesAlive <= 0) { endTimer += dt; if (endTimer > 2.5f) result = 1; }
  }
}
