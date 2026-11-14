#include "ship.h"
#include "weapons.h"
#include <string>

const Palette PAL_PLAYER = {Vec3(0.40f, 0.43f, 0.47f), Vec3(0.25f, 0.28f, 0.33f), Vec3(0.78f, 0.28f, 0.05f),
                            Vec3(0.07f, 0.075f, 0.085f), Vec3(0.10f, 0.35f, 0.55f), Vec3(0.25f, 0.6f, 1.0f)};
const Palette PAL_ENEMY[3] = {
    {Vec3(0.20f, 0.20f, 0.23f), Vec3(0.12f, 0.12f, 0.14f), Vec3(0.75f, 0.06f, 0.05f), Vec3(0.04f, 0.04f, 0.05f), Vec3(0.5f, 0.12f, 0.05f), Vec3(1.0f, 0.35f, 0.12f)},
    {Vec3(0.30f, 0.24f, 0.17f), Vec3(0.17f, 0.14f, 0.10f), Vec3(0.85f, 0.7f, 0.1f), Vec3(0.05f, 0.045f, 0.04f), Vec3(0.5f, 0.4f, 0.05f), Vec3(1.0f, 0.5f, 0.1f)},
    {Vec3(0.17f, 0.24f, 0.20f), Vec3(0.10f, 0.14f, 0.12f), Vec3(0.6f, 0.1f, 0.6f), Vec3(0.04f, 0.05f, 0.045f), Vec3(0.4f, 0.1f, 0.45f), Vec3(0.9f, 0.3f, 1.0f)},
};

const char* sideName(int sys) { return sys == 0 ? "PORT" : "STBD"; }

namespace {

float meshFill(int mesh) {
  switch (mesh) {
    case M_CYL: return 0.78f;
    case M_CONE: return 0.26f;
    case M_SPHERE: return 0.52f;
    case M_WEDGE: return 0.5f;
    case M_TAPER: return 0.5f;
    case M_PRISM: return 0.65f;
    case M_WING_R: case M_WING_L: case M_FIN: return 0.45f;
    case M_RBOX: return 0.97f;
    case M_SQPRISM: return 0.9f;
    case M_NOSE: return 0.4f;
    case M_NACELLE: return 0.75f;
    case M_NOZZLE: return 0.45f;
    case M_TUBE: case M_MUZZLE: return 0.4f;
    case M_RING: return 0.2f;
    case M_POD: return 0.55f;
    case M_S_NOSE: return 0.3f;
    case M_S_FWD: case M_S_MID: case M_S_AFT: case M_S_TAIL: return 0.62f;
    case M_S_CANOPY: case M_S_SILL: case M_S_SPINE: return 0.5f;
    case M_S_NACELLE: return 0.7f;
    case M_S_INTAKE: case M_S_NOZZLE: return 0.25f;
    default: return mesh >= M_S_ARMOR0 && mesh <= M_S_ARMOR_LAST ? 0.07f : 1.0f; // armour plates are thin shells in a big box
  }
}
float density(Role r) {
  switch (r) {
    case Role::Hull: return 240;
    case Role::Armor: return 1100;
    case Role::Cockpit: return 300;
    case Role::Glass: return 120;
    case Role::Reactor: return 1800;
    case Role::ShieldGen: return 900;
    case Role::Wing: case Role::Control: case Role::Fin: return 210;
    case Role::RCS: return 500;
    case Role::EnginePylon: return 450;
    case Role::EngineCore: return 360;
    case Role::EngineNozzle: return 500;
    case Role::EngineAux: return 400;
    case Role::CannonMount: case Role::CannonBody: return 650;
    case Role::CannonBarrel: case Role::CannonAux: return 900;
    default: return 300;
  }
}

struct Builder {
  Ship& s;
  Rng rng;
  Builder(Ship& ship, uint32_t seed) : s(ship), rng(seed) {}

  int add(const std::string& name, Role role, int mesh, Vec3 pos, Vec3 scale, Vec3 eulerDeg, Vec3 color, float hp,
          int parent, int sys = -1, int material = MAT_HULL, float emissive = 0) {
    Part p;
    p.name = name; p.role = role; p.mesh = mesh; p.pos = pos; p.scale = scale;
    p.rot = fromEuler(radians(eulerDeg.x), radians(eulerDeg.y), radians(eulerDeg.z));
    p.invRot = conj(p.rot);
    p.color = color; p.maxHp = p.hp = hp; p.parent = parent; p.sys = sys;
    p.material = material; p.emissive = emissive;
    p.mass = std::max(2.0f, scale.x * scale.y * scale.z * meshFill(mesh) * density(role));
    p.seed = rng.uniform();
    s.parts.push_back(p);
    return (int)s.parts.size() - 1;
  }
  // mirrored pair; `pos`/`euler` describe the starboard (+X) part. Returns the port index; starboard is +1.
  int pair(const std::string& name, Role role, int mesh, Vec3 pos, Vec3 scale, Vec3 e, Vec3 color, float hp,
           int parentPort, int parentStbd, int material = MAT_HULL, float emissive = 0) {
    int meshPort = mesh == M_WING_R ? M_WING_L : mesh;
    int i = add("PORT " + name, role, meshPort, Vec3(-pos.x, pos.y, pos.z), scale, Vec3(e.x, -e.y, -e.z), color, hp, parentPort, 0, material, emissive);
    add("STBD " + name, role, mesh, pos, scale, e, color, hp, parentStbd, 1, material, emissive);
    return i;
  }
  void orient(int i, Quat q) { s.parts[i].rot = q; s.parts[i].invRot = conj(q); }
  void w(int i, float pitch, float yaw, float roll) { s.parts[i].wPitch = pitch; s.parts[i].wYaw = yaw; s.parts[i].wRoll = roll; }
  void w2(int i, float pitch, float yaw, float roll) { w(i, pitch, yaw, roll); w(i + 1, pitch, yaw, roll); }
};

// One weapon assembly hanging under a wing pylon. Every piece is its own part: barrels,
// cooling rings, coils, capacitors, feeds and brackets can each be shot off, and the damage
// model reads the survivors to decide how well (or whether) the weapon still works.
void buildCannon(Builder& b, const Palette& pal, int sys, int weapon, int pylon) {
  float xs = sys == 0 ? -1.0f : 1.0f;
  Vec3 c(xs * 3.0f, -1.2f, -0.6f);
  const Vec3 Z(0, 0, 0);
  const Vec3 wc = WEAPONS[weapon].color, steel(0.30f, 0.31f, 0.33f), copper(0.55f, 0.32f, 0.14f);
  std::string n = WEAPONS[weapon].name;
  auto P = [&](const char* name, Role role, int mesh, Vec3 at, Vec3 size, Vec3 color, float hp, int parent, Vec3 euler = Vec3(), int mat = MAT_HULL, float em = 0) {
    return b.add(n + " " + name, role, mesh, c + at, size, euler, color, hp, parent, sys, mat, em);
  };
  auto around = [](float radius, float deg) { return Vec3(std::cos(radians(deg)) * radius, std::sin(radians(deg)) * radius, 0); };
  float muzzleZ = -3.0f;
  int body = -1;
  switch (weapon) {
    case W_PULSE: {
      body = P("HOUSING", Role::CannonBody, M_POD, Vec3(0, 0, 0.1f), Vec3(0.5f, 0.5f, 2.4f), pal.hull2, 60, pylon);
      int bar = P("BARREL", Role::CannonBarrel, M_TUBE, Vec3(0, 0, -2.25f), Vec3(0.15f, 0.15f, 2.7f), pal.dark, 36, body);
      P("MUZZLE", Role::CannonAux, M_MUZZLE, Vec3(0, 0, -3.7f), Vec3(0.25f, 0.25f, 0.34f), pal.dark, 12, bar);
      P("LENS", Role::CannonAux, M_SPHERE, Vec3(0, 0, -3.56f), Vec3(0.11f), wc, 8, bar, Z, MAT_EMISSIVE, 2.5f);
      for (int i = 0; i < 7; ++i) P("COOLING RING", Role::CannonAux, M_RING, Vec3(0, 0, -1.25f - 0.31f * i), Vec3(0.3f, 0.3f, 0.07f), steel, 6, bar);
      P("FOCUS COLLAR", Role::CannonAux, M_RING, Vec3(0, 0, -1.02f), Vec3(0.4f, 0.4f, 0.16f), pal.hull, 12, body);
      for (int s = -1; s <= 1; s += 2) {
        int cap = P("CAPACITOR", Role::CannonAux, M_POD, Vec3(s * 0.31f, 0.02f, 0.3f), Vec3(0.2f, 0.2f, 1.4f), steel, 18, body);
        for (int k = 0; k < 2; ++k) P("CAPACITOR BAND", Role::Detail, M_RING, Vec3(s * 0.31f, 0.02f, 0.0f + 0.6f * k), Vec3(0.24f, 0.24f, 0.05f), wc, 4, cap, Z, MAT_EMISSIVE, 1.0f);
      }
      int opt = P("SIGHT", Role::Detail, M_RBOX, Vec3(0, -0.3f, -0.85f), Vec3(0.12f, 0.1f, 0.32f), pal.dark, 8, body);
      P("SIGHT LENS", Role::Detail, M_SPHERE, Vec3(0, -0.3f, -1.02f), Vec3(0.07f), Vec3(0.2f, 0.5f, 0.9f), 3, opt, Z, MAT_GLASS);
      P("POWER CONDUIT", Role::Detail, M_CYL, Vec3(0.13f, 0.27f, 0.0f), Vec3(0.05f, 0.05f, 1.3f), pal.dark, 5, body);
      muzzleZ = -3.9f;
    } break;
    case W_GATLING: {
      body = P("HOUSING", Role::CannonBody, M_POD, Vec3(0, 0, 0.2f), Vec3(0.58f, 0.58f, 2.0f), pal.hull2, 70, pylon);
      for (int i = 0; i < 6; ++i) P("BARREL", Role::CannonBarrel, M_TUBE, around(0.125f, 60.0f * i) + Vec3(0, 0, -2.15f), Vec3(0.075f, 0.075f, 2.8f), pal.dark, 15, body);
      for (int i = 0; i < 3; ++i) P("BARREL CLAMP", Role::CannonAux, M_RING, Vec3(0, 0, -1.15f - 0.95f * i), Vec3(0.4f, 0.4f, 0.08f), steel, 10, body);
      P("FLASH HIDER", Role::CannonAux, M_MUZZLE, Vec3(0, 0, -3.62f), Vec3(0.46f, 0.46f, 0.3f), pal.dark, 14, body);
      int drum = P("AMMO DRUM", Role::CannonAux, M_CYL, Vec3(0, -0.36f, 0.55f), Vec3(0.56f, 0.56f, 0.6f), pal.hull, 26, body, Vec3(0, 90, 0));
      for (int s = -1; s <= 1; s += 2) P("DRUM CAP", Role::Detail, M_RING, Vec3(s * 0.31f, -0.36f, 0.55f), Vec3(0.6f, 0.6f, 0.05f), steel, 5, drum, Vec3(0, 90, 0));
      P("FEED CHUTE", Role::CannonAux, M_RBOX, Vec3(0, -0.2f, 0.05f), Vec3(0.14f, 0.3f, 0.26f), pal.dark, 10, body, Vec3(-25, 0, 0));
      P("DRIVE MOTOR", Role::CannonAux, M_CYL, Vec3(0, 0, 1.28f), Vec3(0.3f, 0.3f, 0.36f), pal.dark, 14, body);
      P("EJECTION PORT", Role::Detail, M_RBOX, Vec3(0.27f, 0.0f, -0.2f), Vec3(0.06f, 0.14f, 0.3f), pal.dark, 5, body);
      muzzleZ = -3.8f;
    } break;
    case W_ION: {
      body = P("HOUSING", Role::CannonBody, M_POD, Vec3(0, 0, 0.15f), Vec3(0.54f, 0.54f, 2.1f), pal.hull2, 60, pylon);
      int bar = P("EMITTER", Role::CannonBarrel, M_PRISM, Vec3(0, 0, -2.0f), Vec3(0.24f, 0.24f, 2.4f), pal.dark, 40, body);
      for (int i = 0; i < 5; ++i) P("FOCUS RING", Role::CannonAux, M_RING, Vec3(0, 0, -1.2f - 0.44f * i), Vec3(0.46f, 0.46f, 0.07f), wc, 7, bar, Z, MAT_EMISSIVE, 1.8f);
      for (int i = 0; i < 4; ++i) P("ELECTRODE", Role::CannonAux, M_RBOX, around(0.27f, 45.0f + 90.0f * i) + Vec3(0, 0, -2.05f), Vec3(0.04f, 0.04f, 2.1f), steel, 8, body, Vec3(0, 0, 45.0f + 90.0f * i));
      P("EMITTER TIP", Role::CannonAux, M_SPHERE, Vec3(0, 0, -3.22f), Vec3(0.16f), wc, 6, bar, Z, MAT_EMISSIVE, 3.0f);
      int cell = P("POWER CELL", Role::CannonAux, M_POD, Vec3(0, -0.36f, 0.35f), Vec3(0.3f, 0.3f, 1.2f), steel, 20, body);
      for (int k = 0; k < 2; ++k) P("CELL BAND", Role::Detail, M_RING, Vec3(0, -0.36f, 0.1f + 0.5f * k), Vec3(0.34f, 0.34f, 0.05f), wc, 4, cell, Z, MAT_EMISSIVE, 1.0f);
      for (int s = -1; s <= 1; s += 2) P("COOLANT LINE", Role::Detail, M_CYL, Vec3(s * 0.2f, -0.24f, -0.5f), Vec3(0.045f, 0.045f, 1.5f), copper, 4, body);
      muzzleZ = -3.35f;
    } break;
    case W_RAIL: {
      body = P("BREECH", Role::CannonBody, M_SQPRISM, Vec3(0, 0, 0.25f), Vec3(0.46f, 0.6f, 2.6f), pal.hull2, 80, pylon);
      P("UPPER RAIL", Role::CannonBarrel, M_RBOX, Vec3(0, 0.19f, -3.3f), Vec3(0.1f, 0.1f, 4.5f), pal.dark, 40, body);
      P("LOWER RAIL", Role::CannonBarrel, M_RBOX, Vec3(0, -0.19f, -3.3f), Vec3(0.1f, 0.1f, 4.5f), pal.dark, 40, body);
      for (int i = 0; i < 9; ++i) P("RAIL BRACE", Role::CannonAux, M_RBOX, Vec3(0, 0, -1.35f - 0.47f * i), Vec3(0.2f, 0.56f, 0.06f), steel, 6, body);
      for (int s = -1; s <= 1; s += 2)
        for (int k = 0; k < 3; ++k) {
          int cap = P("CAPACITOR", Role::CannonAux, M_CYL, Vec3(s * 0.33f, 0.0f, -0.45f + 0.72f * k), Vec3(0.22f, 0.22f, 0.6f), steel, 12, body);
          P("CHARGE BAND", Role::Detail, M_RING, Vec3(s * 0.33f, 0.0f, -0.45f + 0.72f * k), Vec3(0.26f, 0.26f, 0.05f), wc, 3, cap, Z, MAT_EMISSIVE, 1.4f);
        }
      P("MUZZLE SHROUD", Role::CannonAux, M_RBOX, Vec3(0, 0, -5.5f), Vec3(0.22f, 0.62f, 0.22f), pal.dark, 14, body);
      P("RECOIL DAMPER", Role::Detail, M_CYL, Vec3(0, -0.38f, 0.2f), Vec3(0.12f, 0.12f, 1.4f), steel, 8, body);
      muzzleZ = -5.75f;
    } break;
    case W_PLASMA: {
      body = P("HOUSING", Role::CannonBody, M_POD, Vec3(0, 0, 0.3f), Vec3(0.8f, 0.8f, 2.2f), pal.hull2, 85, pylon);
      P("CHAMBER", Role::CannonAux, M_SPHERE, Vec3(0, -0.3f, 0.3f), Vec3(0.56f), wc, 26, body, Z, MAT_EMISSIVE, 2.0f);
      int bar = P("BARREL", Role::CannonBarrel, M_TUBE, Vec3(0, 0, -1.7f), Vec3(0.46f, 0.46f, 1.8f), pal.dark, 50, body);
      P("MUZZLE", Role::CannonAux, M_MUZZLE, Vec3(0, 0, -2.72f), Vec3(0.62f, 0.62f, 0.4f), pal.dark, 14, bar);
      for (int i = 0; i < 5; ++i) P("COIL", Role::CannonAux, M_RING, Vec3(0, 0, -1.05f - 0.33f * i), Vec3(0.66f, 0.66f, 0.13f), copper, 9, bar);
      for (int i = 0; i < 3; ++i) P("INJECTOR", Role::Detail, M_CYL, around(0.36f, 90.0f + 120.0f * i) + Vec3(0, 0, -0.55f), Vec3(0.07f, 0.07f, 1.1f), steel, 5, body);
      for (int i = 0; i < 6; ++i) P("RADIATOR FIN", Role::CannonAux, M_RBOX, around(0.47f, 30.0f + 60.0f * i) + Vec3(0, 0, 0.75f), Vec3(0.26f, 0.03f, 0.9f), pal.dark, 5, body, Vec3(0, 0, 30.0f + 60.0f * i));
      muzzleZ = -2.98f;
    } break;
    default: { // missile pod
      body = P("POD", Role::CannonBody, M_SQPRISM, Vec3(0, 0, 0), Vec3(0.95f, 0.72f, 2.6f), pal.hull2, 90, pylon);
      for (int i = 0; i < 6; ++i) {
        Vec3 o((i % 3 - 1) * 0.29f, i < 3 ? 0.165f : -0.165f, 0);
        int tube = P("TUBE", Role::CannonBarrel, M_TUBE, o + Vec3(0, 0, -1.32f), Vec3(0.25f, 0.25f, 0.5f), pal.dark, 15, body);
        P("WARHEAD", Role::CannonAux, M_NOSE, o + Vec3(0, 0, -1.33f), Vec3(0.16f, 0.16f, 0.36f), pal.accent, 5, tube);
      }
      for (int s = -1; s <= 1; s += 2) {
        P("TUBE FRAME", Role::Detail, M_RBOX, Vec3(0, s * 0.36f, -1.5f), Vec3(0.98f, 0.05f, 0.12f), steel, 6, body);
        P("EXHAUST VENT", Role::CannonAux, M_RBOX, Vec3(s * 0.23f, 0, 1.32f), Vec3(0.36f, 0.5f, 0.08f), pal.dark, 9, body);
      }
      P("SEEKER DOME", Role::Detail, M_SPHERE, Vec3(0, -0.4f, -0.9f), Vec3(0.22f), Vec3(0.3f, 0.2f, 0.1f), 4, body, Z, MAT_GLASS);
      muzzleZ = -1.78f;
    } break;
  }
  // how it hangs from the pylon
  for (int k = 0; k < 2; ++k) P("MOUNT LUG", Role::Detail, M_RBOX, Vec3(0, 0.27f, -0.35f + 0.8f * k), Vec3(0.1f, 0.14f, 0.18f), pal.dark, 8, body);
  P("DATA CABLE", Role::Detail, M_CYL, Vec3(-0.09f, 0.3f, 0.75f), Vec3(0.035f, 0.035f, 0.6f), pal.dark, 3, body, Vec3(35, 0, 0));
  b.s.muzzle[sys] = c + Vec3(0, 0, muzzleZ);
}

} // namespace

void Ship::build(const Palette& pal, int w0, int w1, uint32_t seed) {
  parts.clear();
  weapon[0] = w0; weapon[1] = w1;
  glow = pal.glow;
  Builder b(*this, seed);
  const Vec3 Z(0, 0, 0);
  using R = Role;
  // a part whose mesh was modelled in ship space: it goes where its mesh says it belongs
  auto S = [&](const std::string& name, Role role, int mesh, Vec3 color, float hp, int parent, int sys = -1, int material = MAT_HULL, Vec3 offset = Vec3()) {
    const ShipMeshInfo& mi = shipMeshInfo(mesh);
    return b.add(name, role, mesh, mi.center + offset, mi.size, Z, color, hp, parent, sys, material);
  };

  // ---------------------------------------------------------------- fuselage: one continuous body in five sections.
  // These are the structural frame; the painted skin is the layer of plates added further down.
  const Vec3 frame(0.13f, 0.135f, 0.15f);
  int keel = S("KEEL", R::Hull, M_S_MID, frame, 320, -1, -1, MAT_STRUCT);
  parts[keel].critical = true;
  int fwd = S("FORWARD FUSELAGE", R::Hull, M_S_FWD, frame, 200, keel, -1, MAT_STRUCT);
  int nose = S("NOSE CONE", R::Hull, M_S_NOSE, pal.hull2, 90, fwd);
  b.add("SENSOR PROBE", R::Sensor, M_CYL, Vec3(0, -0.08f, -11.2f), Vec3(0.05f, 0.05f, 1.4f), Z, pal.dark, 14, nose);
  int aft = S("AFT FUSELAGE", R::Hull, M_S_AFT, frame, 240, keel, -1, MAT_STRUCT);
  int tail = S("TAIL BOOM", R::Hull, M_S_TAIL, frame, 90, aft, -1, MAT_STRUCT);
  int reactor = b.add("REACTOR CORE", R::Reactor, M_SPHERE, Vec3(0, 0, 0.6f), Vec3(1.2f), Z, pal.glow, 170, keel, -1, MAT_EMISSIVE, 2.0f);
  parts[reactor].internal = true; parts[reactor].critical = true;
  int shgen = b.add("SHIELD GENERATOR", R::ShieldGen, M_PRISM, Vec3(0, 0, 4.2f), Vec3(0.9f, 0.9f, 1.2f), Z, pal.glow, 90, aft);
  parts[shgen].internal = true;

  // ---------------------------------------------------------------- cockpit
  int tub = S("COCKPIT", R::Cockpit, M_S_SILL, pal.dark, 400, fwd);
  parts[tub].critical = true;
  int glass = S("CANOPY", R::Glass, M_S_CANOPY, pal.glass, 60, tub, -1, MAT_GLASS);
  b.add("CANOPY BOW", R::Detail, M_RBOX, Vec3(0, 1.22f, -4.75f), Vec3(1.02f, 0.62f, 0.05f), Vec3(-12, 0, 0), pal.dark, 20, glass);
  b.add("CANOPY BOW", R::Detail, M_RBOX, Vec3(0, 1.33f, -3.3f), Vec3(1.08f, 0.6f, 0.05f), Vec3(8, 0, 0), pal.dark, 20, glass);

  // ---------------------------------------------------------------- dorsal
  int spine = S("DORSAL SPINE", R::Hull, M_S_SPINE, pal.hull, 110, keel);
  b.add("COMM MAST", R::Sensor, M_FIN, Vec3(0, 1.62f, 2.4f), Vec3(0.05f, 0.75f, 0.5f), Z, pal.dark, 10, spine);
  b.add("SENSOR DISH", R::Sensor, M_SPHERE, Vec3(0, 1.3f, 0.6f), Vec3(0.5f, 0.22f, 0.9f), Z, pal.dark, 14, spine);
  b.add("SPINE STRIPE", R::Detail, M_RBOX, Vec3(0, 1.21f, 3.4f), Vec3(0.2f, 0.02f, 1.2f), Vec3(-2.5f, 0, 0), pal.accent, 10, spine);

  // ---------------------------------------------------------------- fittings
  b.pair("LANDING SKID", R::Detail, M_SQPRISM, Vec3(0.7f, -1.03f, 1.0f), Vec3(0.18f, 0.4f, 2.6f), Z, pal.dark, 40, keel, keel);
  b.add("VENTRAL SENSOR POD", R::Sensor, M_SPHERE, Vec3(0, -0.72f, -3.6f), Vec3(0.55f, 0.36f, 1.1f), Z, pal.dark, 25, fwd);
  b.pair("PITOT TUBE", R::Detail, M_CYL, Vec3(0.36f, 0.08f, -8.9f), Vec3(0.03f, 0.03f, 0.9f), Z, pal.dark, 6, nose, nose);
  b.pair("FORMATION LIGHT", R::Light, M_RBOX, Vec3(1.25f, 0.42f, -1.2f), Vec3(0.03f, 0.05f, 0.9f), Z, pal.glow, 6, keel, keel, MAT_EMISSIVE, 1.5f);
  int canard = b.pair("CANARD", R::Control, M_WING_R, Vec3(1.35f, -0.12f, -5.2f), Vec3(1.5f, 0.1f, 1.15f), Z, pal.hull2, 30, fwd, fwd);
  b.w2(canard, 1.2f, 0.2f, 0.3f);
  int noseRcs = b.pair("NOSE RCS", R::RCS, M_CYL, Vec3(0.62f, 0.05f, -7.0f), Vec3(0.2f, 0.2f, 0.1f), Vec3(0, 78, 0), pal.dark, 30, fwd, fwd);
  b.w2(noseRcs, 1.5f, 1.5f, 0);
  int aftRcs = b.pair("AFT MANEUVERING JET", R::RCS, M_CYL, Vec3(1.2f, -0.52f, 5.6f), Vec3(0.24f, 0.24f, 0.12f), Vec3(40, 90, 0), pal.dark, 28, aft, aft);
  b.w2(aftRcs, 0.8f, 1.0f, 0.3f);

  // ---------------------------------------------------------------- wings
  int wroot = b.pair("WING ROOT", R::Wing, M_WING_R, Vec3(2.45f, -0.25f, 1.2f), Vec3(2.6f, 0.5f, 4.8f), Z, pal.hull, 150, keel, keel);
  b.w2(wroot, 0.3f, 0.3f, 1.0f);
  int wmid = b.pair("WING MID", R::Wing, M_WING_R, Vec3(5.05f, -0.25f, 2.04f), Vec3(2.6f, 0.3f, 2.16f), Z, pal.hull, 105, wroot, wroot + 1);
  b.w2(wmid, 0.3f, 0.3f, 1.5f);
  int wtip = b.pair("WING TIP", R::Wing, M_WING_R, Vec3(7.35f, -0.25f, 2.415f), Vec3(2.0f, 0.18f, 0.97f), Z, pal.accent, 70, wmid, wmid + 1);
  b.w2(wtip, 0.3f, 0.3f, 2.0f);
  int winglet = b.pair("WINGLET", R::Fin, M_FIN, Vec3(8.36f, 0.42f, 2.6f), Vec3(0.07f, 1.3f, 1.1f), Vec3(0, 0, -10), pal.hull2, 35, wtip, wtip + 1);
  b.w2(winglet, 0, 0.6f, 0.3f);
  int aileron = b.pair("AILERON", R::Control, M_RBOX, Vec3(5.05f, -0.25f, 3.2f), Vec3(2.3f, 0.05f, 0.42f), Vec3(0, 5, 0), pal.hull2, 30, wmid, wmid + 1);
  b.w2(aileron, 0, 0, 1.5f);
  int flap = b.pair("FLAP", R::Control, M_RBOX, Vec3(2.5f, -0.25f, 3.56f), Vec3(2.2f, 0.06f, 0.5f), Vec3(0, 10.5f, 0), pal.hull2, 35, wroot, wroot + 1);
  b.w2(flap, 0.6f, 0, 0.3f);
  int pod = b.pair("WINGTIP RCS POD", R::RCS, M_NACELLE, Vec3(8.33f, -0.25f, 2.1f), Vec3(0.34f, 0.34f, 1.5f), Z, pal.dark, 45, wtip, wtip + 1);
  b.w2(pod, 0, 1.0f, 2.0f);
  int strake = b.pair("WING STRAKE", R::Wing, M_WING_R, Vec3(1.75f, -0.25f, -2.6f), Vec3(1.3f, 0.3f, 3.0f), Z, pal.hull, 50, keel, keel);
  b.w2(strake, 0.3f, 0, 0.2f);
  int rail = b.pair("STORES RAIL", R::Detail, M_SQPRISM, Vec3(5.4f, -0.46f, 2.1f), Vec3(0.1f, 0.2f, 1.3f), Z, pal.dark, 16, wmid, wmid + 1);
  b.pair("ECM POD", R::Sensor, M_NACELLE, Vec3(5.4f, -0.7f, 2.0f), Vec3(0.3f, 0.3f, 1.9f), Z, pal.hull2, 22, rail, rail + 1);
  int nav = b.pair("NAV LIGHT", R::Light, M_SPHERE, Vec3(8.47f, 1.02f, 2.92f), Vec3(0.14f), Z, Vec3(0.1f, 1.0f, 0.2f), 5, winglet, winglet + 1, MAT_EMISSIVE, 4.0f);
  parts[nav].color = Vec3(1.0f, 0.08f, 0.05f); // port is red, starboard green
  int pylonW = b.pair("WEAPON PYLON", R::CannonMount, M_SQPRISM, Vec3(3.0f, -0.7f, -0.1f), Vec3(0.22f, 0.55f, 1.7f), Z, pal.dark, 70, wroot, wroot + 1);

  // ---------------------------------------------------------------- engines: pods above the wing roots
  const float ex = 2.45f, ey = 0.62f;
  auto E = [&](const std::string& name, Role role, int mesh, Vec3 color, float hp, int pp, int ps, int material = MAT_HULL) {
    const ShipMeshInfo& mi = shipMeshInfo(mesh);
    int i = b.add("PORT " + name, role, mesh, Vec3(-ex, ey, mi.center.z), mi.size, Z, color, hp, pp, 0, material);
    b.add("STBD " + name, role, mesh, Vec3(ex, ey, mi.center.z), mi.size, Z, color, hp, ps, 1, material);
    return i;
  };
  int epyl = b.pair("ENGINE PYLON", R::EnginePylon, M_SQPRISM, Vec3(1.55f, 0.55f, 4.7f), Vec3(1.2f, 0.6f, 3.2f), Z, pal.hull, 90, aft, aft);
  int nac = E("ENGINE NACELLE", R::EngineCore, M_S_NACELLE, pal.hull, 160, epyl, epyl + 1);
  int intake = E("ENGINE INTAKE", R::EngineAux, M_S_INTAKE, pal.hull2, 50, nac, nac + 1, MAT_DUCT);
  b.pair("INTAKE SPIKE", R::EngineAux, M_CONE, Vec3(ex, ey, 2.72f), Vec3(0.62f, 0.62f, 1.0f), Z, pal.dark, 30, intake, intake + 1);
  int noz = E("ENGINE NOZZLE", R::EngineNozzle, M_S_NOZZLE, Vec3(0.21f, 0.20f, 0.19f), 75, nac, nac + 1, MAT_DUCT);
  b.pair("INJECTOR PLATE", R::EngineAux, M_CYL, Vec3(ex, ey, 6.76f), Vec3(1.18f, 1.18f, 0.06f), Z, pal.glow, 25, noz, noz + 1, MAT_EMISSIVE, 0.3f);
  b.pair("NACELLE STRAKE", R::EngineAux, M_FIN, Vec3(ex + 0.62f, ey + 0.72f, 5.3f), Vec3(0.05f, 0.34f, 2.2f), Vec3(0, 0, -40), pal.hull2, 20, nac, nac + 1);
  b.pair("NACELLE STRAKE", R::EngineAux, M_FIN, Vec3(ex + 0.62f, ey - 0.72f, 5.3f), Vec3(0.05f, 0.34f, 2.2f), Vec3(0, 0, -140), pal.hull2, 20, nac, nac + 1);

  // ---------------------------------------------------------------- tail
  int stab = b.pair("TAIL FIN", R::Fin, M_FIN, Vec3(1.0f, 1.75f, 5.5f), Vec3(0.1f, 2.3f, 2.5f), Vec3(0, 0, -22), pal.hull, 65, aft, aft);
  b.w2(stab, 1.0f, 1.5f, 0);
  b.pair("FIN STRIPE", R::Detail, M_RBOX, Vec3(1.36f, 2.62f, 6.25f), Vec3(0.105f, 0.22f, 0.7f), Vec3(0, 0, -22), pal.accent, 8, stab, stab + 1);
  int vfin = b.add("VENTRAL FIN", R::Fin, M_FIN, Vec3(0, -1.15f, 5.4f), Vec3(0.09f, 1.0f, 1.9f), Vec3(0, 0, 180), pal.hull2, 45, aft);
  b.w(vfin, 0, 1.0f, 0);
  int tailRcs = b.add("TAIL RCS CLUSTER", R::RCS, M_S_NOZZLE, Vec3(0, 0.16f, 7.72f), Vec3(0.42f, 0.42f, 0.4f), Z, Vec3(0.21f, 0.20f, 0.19f), 45, tail, -1, MAT_DUCT);
  b.w(tailRcs, 2.0f, 2.0f, 0);
  b.add("TAIL BEACON", R::Light, M_SPHERE, Vec3(0, 0.55f, 7.3f), Vec3(0.12f), Z, Vec3(1, 1, 1), 5, tail, -1, MAT_EMISSIVE, 4.0f);

  // ================================================================ detail: several hundred small parts, each with its own hit box
  // ---- skin: the fuselage is covered in separate plates; shoot one off and the frame beneath shows
  static const char* sectorName[SKIN_SECTORS] = {"STBD", "STBD UPPER", "DORSAL", "PORT UPPER", "PORT", "PORT LOWER", "VENTRAL", "STBD LOWER"};
  for (int band = 0; band < SKIN_BANDS; ++band)
    for (int sec = 0; sec < SKIN_SECTORS; ++sec) {
      if (!skinPlateExists(band, sec)) continue;
      int mesh = M_S_ARMOR0 + band * SKIN_SECTORS + sec;
      float zc = shipMeshInfo(mesh).center.z;
      int parent = zc < -7.4f ? nose : (zc < -2.4f ? fwd : (zc < 3.0f ? keel : (zc < 6.0f ? aft : tail)));
      Vec3 col = b.rng.chance(0.22f) ? pal.hull2 : pal.hull;
      S(std::string("SKIN PLATE ") + sectorName[sec] + " " + std::to_string(band + 1), R::Skin, mesh, col, 40, parent);
    }
  // ---- wings: skin panels above and below each section
  struct WingSeg { int part; int spans, chords; };
  const WingSeg segs[3] = {{wroot, 3, 2}, {wmid, 3, 2}, {wtip, 2, 1}};
  for (int side = 0; side < 2; ++side)
    for (const WingSeg& ws : segs) {
      const Part seg = parts[ws.part + side];
      float sx = side == 0 ? -1.0f : 1.0f;
      for (int i = 0; i < ws.spans; ++i)
        for (int k = 0; k < ws.chords; ++k)
          for (int top = 0; top < 2; ++top) {
            float t = (i + 0.5f) / ws.spans, uu = ws.chords == 1 ? 0.42f : 0.27f + 0.3f * k;
            float z0 = lerpf(-0.5f, -0.05f, t), z1 = lerpf(0.5f, 0.4f, t), th = lerpf(0.5f, 0.3f, t) * 2.598f * std::sqrt(uu) * (1 - uu);
            Vec3 at(seg.pos.x + sx * (t - 0.5f) * seg.scale.x, seg.pos.y + (top ? 1.0f : -1.0f) * (th * seg.scale.y - 0.006f), seg.pos.z + lerpf(z0, z1, uu) * seg.scale.z);
            Vec3 size(seg.scale.x / ws.spans - 0.05f, 0.03f, (z1 - z0) * seg.scale.z * (ws.chords == 1 ? 0.5f : 0.27f));
            b.add(std::string(sideName(side)) + " WING SKIN " + (top ? "UPPER " : "LOWER ") + std::to_string(ws.part == wroot ? i + 1 : (ws.part == wmid ? i + 4 : i + 7)) + (k ? "B" : "A"),
                  R::Skin, M_RBOX, at, size, Z, b.rng.chance(0.25f) ? pal.hull2 : seg.color, 20, ws.part + side, side);
          }
    }
  for (int side = 0; side < 2; ++side) {
    float sx = side == 0 ? -1.0f : 1.0f;
    const char* sn = sideName(side);
    for (int i = 0; i < 4; ++i) // static dischargers along the trailing edge
      b.add(std::string(sn) + " STATIC WICK", R::Detail, M_CYL, Vec3(sx * (4.3f + 0.75f * i), -0.25f, 3.22f - 0.05f * i), Vec3(0.02f, 0.02f, 0.4f), Z, pal.dark, 2, wmid + side, side);
    for (int i = 0; i < 3; ++i) // flap-track fairings under the wing
      b.add(std::string(sn) + " FLAP TRACK FAIRING", R::Detail, M_POD, Vec3(sx * (1.9f + 1.25f * i), -0.46f + 0.03f * i, 2.95f - 0.08f * i), Vec3(0.13f, 0.16f, 1.0f), Z, pal.hull2, 8, (i < 2 ? wroot : wmid) + side, side);
    b.add(std::string(sn) + " WING FENCE", R::Detail, M_FIN, Vec3(sx * 3.78f, 0.02f, 1.6f), Vec3(0.03f, 0.22f, 1.5f), Z, pal.hull2, 6, wroot + side, side);
    // manoeuvring jets on the wingtip pod and around the nose
    static const float jet[3][3] = {{0, 90, 0}, {0, -90, 0}, {0, 0, 1}};
    for (int j = 0; j < 3; ++j) {
      Vec3 dir = j == 2 ? Vec3(sx, 0, 0) : Vec3(0, j == 0 ? 1.0f : -1.0f, 0);
      int q = b.add(std::string(sn) + " WINGTIP JET", R::Detail, M_S_NOZZLE, Vec3(sx * 8.33f, -0.25f, 1.75f + 0.3f * j) + dir * 0.19f, Vec3(0.13f, 0.13f, 0.14f), Z, Vec3(0.21f, 0.20f, 0.19f), 4, pod + side, side, MAT_DUCT);
      b.orient(q, lookRotation(-dir, Vec3(0, 0, 1)));
      (void)jet;
    }
    // engine: nozzle petals and their actuators, intake guide vanes, frame rings, plumbing
    Vec3 axis(sx * ex, ey, 0);
    for (int i = 0; i < 12; ++i) {
      float a = 2 * PI * (i + 0.5f) / 12;
      Vec3 rad(std::cos(a), std::sin(a), 0);
      int petal = b.add(std::string(sn) + " NOZZLE PETAL", R::EngineAux, M_RBOX, axis + rad * 0.735f + Vec3(0, 0, 7.42f), Vec3(0.34f, 0.022f, 1.16f), Z, Vec3(0.11f, 0.11f, 0.115f), 5, noz + side, side);
      b.orient(petal, axisAngle(Vec3(0, 0, 1), a - PI * 0.5f) * axisAngle(Vec3(1, 0, 0), radians(9.5f)));
      float a2 = 2 * PI * i / 12;
      b.add(std::string(sn) + " PETAL ACTUATOR", R::Detail, M_CYL, axis + Vec3(std::cos(a2), std::sin(a2), 0) * 0.85f + Vec3(0, 0, 6.95f), Vec3(0.045f, 0.045f, 0.62f), Z, Vec3(0.3f, 0.31f, 0.33f), 3, noz + side, side);
    }
    for (int i = 0; i < 6; ++i) {
      float a = 2 * PI * i / 6;
      int vane = b.add(std::string(sn) + " INTAKE VANE", R::EngineAux, M_RBOX, axis + Vec3(std::cos(a), std::sin(a), 0) * 0.34f + Vec3(0, 0, 2.95f), Vec3(0.03f, 0.44f, 0.4f), Z, pal.dark, 5, intake + side, side);
      b.orient(vane, axisAngle(Vec3(0, 0, 1), a - PI * 0.5f));
    }
    for (int i = 0; i < 2; ++i)
      b.add(std::string(sn) + " NACELLE FRAME RING", R::EngineAux, M_RING, axis + Vec3(0, 0, 3.55f + 2.8f * i), Vec3(1.79f - 0.1f * i, 1.79f - 0.1f * i, 0.07f), Z, pal.hull2, 14, nac + side, side);
    for (int i = 0; i < 3; ++i) {
      float a = radians(230.0f + 25.0f * i);
      b.add(std::string(sn) + " COOLANT PIPE", R::Detail, M_CYL, axis + Vec3(std::cos(a) * sx, std::sin(a), 0) * 0.905f + Vec3(0, 0, 4.9f), Vec3(0.06f, 0.06f, 2.5f), Z, Vec3(0.3f, 0.31f, 0.33f), 4, nac + side, side);
    }
    // tail fin: rudder, tip pod, leading-edge strip
    const Part fin = parts[stab + side];
    int rud = b.add(std::string(sn) + " RUDDER", R::Control, M_RBOX, fin.pos + rotate(fin.rot, Vec3(0, -0.1f, 1.06f)), Vec3(0.05f, 1.5f, 0.42f), Z, pal.hull2, 22, stab + side, side);
    b.orient(rud, fin.rot);
    b.w(rud, 0, 0.8f, 0);
    int tp = b.add(std::string(sn) + " FIN TIP POD", R::Sensor, M_POD, fin.pos + rotate(fin.rot, Vec3(0, 1.17f, 0.85f)), Vec3(0.13f, 0.13f, 0.9f), Z, pal.dark, 8, stab + side, side);
    b.orient(tp, fin.rot);
    // fuselage sides
    for (int i = 0; i < 3; ++i) {
      static const float bz[3] = {-8.3f, -6.3f, 2.3f};
      b.add(std::string(sn) + " SENSOR BLISTER", R::Sensor, M_SPHERE, Vec3(sx * (i == 0 ? 0.43f : (i == 1 ? 0.74f : 1.42f)), 0.12f, bz[i]), Vec3(0.12f, 0.3f, 0.5f), Z, pal.dark, 6, i < 2 ? fwd : keel, side);
    }
    for (int i = 0; i < 4; ++i)
      b.add(std::string(sn) + " HEAT LOUVRE", R::Detail, M_RBOX, Vec3(sx * 1.44f, 0.3f, 3.5f + 0.34f * i), Vec3(0.05f, 0.34f, 0.2f), Vec3(0, sx * 25.0f, 0), pal.dark, 4, aft, side);
    for (int i = 0; i < 2; ++i)
      b.add(std::string(sn) + " COUNTERMEASURE DISPENSER", R::Detail, M_RBOX, Vec3(sx * 0.55f, -0.8f, 4.3f + 0.7f * i), Vec3(0.34f, 0.08f, 0.44f), Z, pal.dark, 8, aft, side);
    b.add(std::string(sn) + " CANOPY RAIL", R::Detail, M_CYL, Vec3(sx * 0.62f, 0.86f, -3.9f), Vec3(0.045f, 0.045f, 3.0f), Vec3(2.5f, 0, 0), pal.dark, 6, tub, side);
    b.add(std::string(sn) + " STRAKE VORTEX TAB", R::Detail, M_FIN, Vec3(sx * 1.05f, 0.5f, -4.9f), Vec3(0.03f, 0.2f, 0.5f), Vec3(0, 0, sx * -55.0f), pal.hull2, 4, fwd, side);
  }
  // ---- centreline fittings
  {
    static const float dz[3] = {-1.4f, 4.3f, 5.5f}, vz[3] = {-5.6f, -0.8f, 3.2f};
    for (int i = 0; i < 3; ++i) b.add("DORSAL ANTENNA", R::Sensor, M_FIN, Vec3(0, i == 0 ? 1.42f : 1.22f - 0.06f * i, dz[i]), Vec3(0.035f, 0.42f, 0.4f), Z, pal.dark, 5, spine);
    for (int i = 0; i < 3; ++i) b.add("VENTRAL ANTENNA", R::Sensor, M_FIN, Vec3(0, i == 0 ? -0.8f : -1.08f, vz[i]), Vec3(0.035f, 0.38f, 0.4f), Vec3(0, 0, 180), pal.dark, 5, i == 0 ? fwd : keel);
    for (int i = 0; i < 2; ++i) {
      int j = b.add(i ? "NOSE JET VENTRAL" : "NOSE JET DORSAL", R::RCS, M_S_NOZZLE, Vec3(0, i ? -0.42f : 0.3f, -8.7f), Vec3(0.14f, 0.14f, 0.14f), Z, Vec3(0.21f, 0.20f, 0.19f), 12, nose, -1, MAT_DUCT);
      b.orient(j, lookRotation(Vec3(0, i ? 1.0f : -1.0f, 0), Vec3(0, 0, 1)));
      b.w(j, 1.0f, 0, 0);
    }
    b.add("TARGETING TURRET", R::Sensor, M_SPHERE, Vec3(0, 0.6f, -6.35f), Vec3(0.26f), Z, Vec3(0.25f, 0.18f, 0.1f), 10, fwd, -1, MAT_GLASS);
    b.add("REFUELLING PROBE", R::Detail, M_CYL, Vec3(0.52f, 0.5f, -6.9f), Vec3(0.045f, 0.045f, 1.3f), Vec3(-3, -4, 0), pal.dark, 5, fwd);
    b.add("PROBE TIP", R::Detail, M_MUZZLE, Vec3(0.565f, 0.535f, -7.58f), Vec3(0.09f, 0.09f, 0.12f), Vec3(-3, -4, 0), Vec3(0.3f, 0.31f, 0.33f), 3, fwd);
    b.add("CANOPY BOW", R::Detail, M_RBOX, Vec3(0, 1.38f, -4.0f), Vec3(1.1f, 0.62f, 0.04f), Z, pal.dark, 20, glass);
    b.add("EJECTION SEAT RAIL", R::Detail, M_RBOX, Vec3(0, 1.0f, -2.75f), Vec3(0.3f, 0.5f, 0.12f), Vec3(14, 0, 0), pal.dark, 12, tub);
    b.add("TAIL HOOK", R::Detail, M_CYL, Vec3(0, -0.5f, 6.5f), Vec3(0.05f, 0.05f, 1.3f), Vec3(-8, 0, 0), Vec3(0.3f, 0.31f, 0.33f), 6, tail);
    b.add("LANDING LIGHT", R::Light, M_SPHERE, Vec3(0, -0.62f, -6.1f), Vec3(0.12f), Z, Vec3(1.0f, 0.95f, 0.8f), 4, fwd, -1, MAT_EMISSIVE, 2.5f);
    b.add("BELLY BEACON", R::Light, M_SPHERE, Vec3(0, -0.9f, 1.8f), Vec3(0.11f), Z, Vec3(1.0f, 0.15f, 0.1f), 4, keel, -1, MAT_EMISSIVE, 4.0f);
    for (int i = 0; i < 2; ++i) b.add("FUEL DUMP PIPE", R::Detail, M_TUBE, Vec3(i ? 0.25f : -0.25f, -0.2f, 7.55f), Vec3(0.07f, 0.07f, 0.5f), Vec3(0, 180, 0), Vec3(0.3f, 0.31f, 0.33f), 4, tail);
    for (int i = 0; i < 2; ++i) { // landing skid struts
      for (int sdx = -1; sdx <= 1; sdx += 2) b.add("SKID STRUT", R::Detail, M_CYL, Vec3(sdx * 0.7f, -0.93f, 0.1f + 1.8f * i), Vec3(0.07f, 0.07f, 0.3f), Vec3(90, 0, 0), Vec3(0.3f, 0.31f, 0.33f), 8, keel);
    }
  }

  // ---------------------------------------------------------------- weapons
  buildCannon(b, pal, 0, w0, pylonW);
  buildCannon(b, pal, 1, w1, pylonW + 1);

  recompute();
  baseMass = stats.mass;
  baseInertia = stats.inertia;
  baseCom = stats.com;
}

void Ship::recompute() {
  ShipStats& S = stats;
  // ---- mass properties from whatever is still bolted on
  float mass = 0;
  Vec3 com;
  for (auto& p : parts)
    if (p.alive) { mass += p.mass; com += p.pos * p.mass; }
  mass = std::max(mass, 100.0f);
  com = com / mass;
  Vec3 I(0, 0, 0);
  for (auto& p : parts) {
    if (!p.alive) continue;
    Vec3 d = p.pos - com;
    Vec3 s2 = mulv(p.scale, p.scale) * (1.0f / 12.0f);
    I.x += p.mass * (d.y * d.y + d.z * d.z + s2.y + s2.z);
    I.y += p.mass * (d.x * d.x + d.z * d.z + s2.x + s2.z);
    I.z += p.mass * (d.x * d.x + d.y * d.y + s2.x + s2.y);
  }
  S.mass = mass;
  S.com = com;
  S.inertia = Vec3(std::max(I.x, 500.0f), std::max(I.y, 500.0f), std::max(I.z, 500.0f));

  // ---- engines
  for (int e = 0; e < 2; ++e) {
    float pyl = 0, core = 0, noz = 0, auxHp = 0, auxMax = 0;
    for (auto& p : parts) {
      if (p.sys != e) continue;
      if (p.role == Role::EnginePylon) pyl = p.frac();
      else if (p.role == Role::EngineCore) core = p.frac();
      else if (p.role == Role::EngineNozzle) { noz = p.frac(); S.enginePos[e] = p.pos; }
      else if (p.role == Role::EngineAux) { auxMax += p.maxHp; if (p.alive) auxHp += p.hp; }
    }
    float aux = auxMax > 0 ? auxHp / auxMax : 1;
    S.engine[e] = (pyl > 0 && core > 0 && noz > 0) ? (0.25f + 0.75f * core) * (0.25f + 0.75f * noz) * (0.6f + 0.4f * aux) * (0.5f + 0.5f * pyl) : 0.0f;
  }

  // ---- steering authority, RCS, sensors
  float sum[3] = {0, 0, 0}, tot[3] = {0, 0, 0}, rollL = 0, rollR = 0, rollTot = 0;
  float rcsHp = 0, rcsMax = 0, senHp = 0, senMax = 0, hp = 0, hpMax = 0;
  S.reactor = S.shieldGen = 0;
  S.canopy = false;
  bool critLost = false;
  for (auto& p : parts) {
    float f = p.frac();
    tot[0] += p.wPitch; tot[1] += p.wYaw; tot[2] += p.wRoll;
    sum[0] += p.wPitch * f; sum[1] += p.wYaw * f; sum[2] += p.wRoll * f;
    if (p.sys == 0) rollL += p.wRoll * f;
    if (p.sys == 1) rollR += p.wRoll * f;
    if (p.sys >= 0) rollTot += p.wRoll;
    if (p.role == Role::RCS) { rcsMax += p.maxHp; rcsHp += p.alive ? p.hp : 0; }
    if (p.role == Role::Sensor) { senMax += p.maxHp; senHp += p.alive ? p.hp : 0; }
    if (p.role == Role::Reactor) S.reactor = f;
    if (p.role == Role::ShieldGen) S.shieldGen = f;
    if (p.role == Role::Glass) S.canopy = p.alive;
    if (p.role != Role::Light && p.role != Role::Detail && p.role != Role::Skin) { hpMax += p.maxHp; hp += p.alive ? p.hp : 0; }
    if (p.critical && !p.alive) critLost = true;
  }
  for (int a = 0; a < 3; ++a) S.authority[a] = std::max(0.08f, tot[a] > 0 ? sum[a] / tot[a] : 1.0f);
  S.asym = rollTot > 0 ? (rollR - rollL) / (rollTot * 0.5f) : 0;
  S.rcs = std::max(0.15f, rcsMax > 0 ? rcsHp / rcsMax : 1.0f);
  S.sensors = senMax > 0 ? senHp / senMax : 1.0f;
  S.hullIntegrity = hpMax > 0 ? hp / hpMax : 0;

  // ---- weapons
  for (int k = 0; k < 2; ++k) {
    float mount = 0, body = 0, barHp = 0, barMax = 0, auxHp = 0, auxMax = 0;
    for (auto& p : parts) {
      if (p.sys != k) continue;
      if (p.role == Role::CannonMount) mount = p.frac();
      else if (p.role == Role::CannonBody) body = p.frac();
      else if (p.role == Role::CannonBarrel) { barMax += p.maxHp; if (p.alive) barHp += p.hp; }
      else if (p.role == Role::CannonAux) { auxMax += p.maxHp; if (p.alive) auxHp += p.hp; }
    }
    float bar = barMax > 0 ? barHp / barMax : 0, aux = auxMax > 0 ? auxHp / auxMax : 1;
    S.weapon[k] = (mount > 0 && body > 0 && bar > 0) ? (0.1f + 0.25f * body + 0.5f * bar + 0.15f * aux) * (0.6f + 0.4f * mount) : 0.0f;
  }

  S.dead = critLost || S.hullIntegrity < kDeathIntegrity;
}

int Ship::raycast(const Vec3& o, const Vec3& d, float tMax, float& tHit, const int* ignore, int nIgnore) const {
  int best = -1;
  float bestT = tMax;
  for (int i = 0; i < (int)parts.size(); ++i) {
    const Part& p = parts[i];
    if (!p.alive || p.internal) continue;
    bool skip = false;
    for (int k = 0; k < nIgnore; ++k) skip |= ignore[k] == i;
    if (skip) continue;
    Vec3 lo = rotate(p.invRot, o - p.pos), ld = rotate(p.invRot, d);
    Vec3 h = p.scale * 0.5f;
    float t0 = 0, t1 = bestT;
    bool hit = true;
    for (int a = 0; a < 3 && hit; ++a) {
      if (std::fabs(ld[a]) < 1e-9f) {
        if (std::fabs(lo[a]) > h[a]) hit = false;
      } else {
        float inv = 1.0f / ld[a];
        float ta = (-h[a] - lo[a]) * inv, tb = (h[a] - lo[a]) * inv;
        if (ta > tb) std::swap(ta, tb);
        t0 = std::max(t0, ta);
        t1 = std::min(t1, tb);
        if (t0 > t1) hit = false;
      }
    }
    if (hit && t0 <= bestT) { bestT = t0; best = i; }
  }
  tHit = bestT;
  return best;
}

int Ship::nearestPart(const Vec3& pt) const {
  int best = -1;
  float bd = 1e30f;
  for (int i = 0; i < (int)parts.size(); ++i) {
    const Part& p = parts[i];
    if (!p.alive || p.internal) continue;
    Vec3 l = rotate(p.invRot, pt - p.pos), h = p.scale * 0.5f;
    Vec3 q(std::max(std::fabs(l.x) - h.x, 0.0f), std::max(std::fabs(l.y) - h.y, 0.0f), std::max(std::fabs(l.z) - h.z, 0.0f));
    float d = length2(q);
    if (d < bd) { bd = d; best = i; }
  }
  return best;
}

void Ship::subtree(int idx, std::vector<int>& out) const {
  out.clear();
  out.push_back(idx);
  for (size_t k = 0; k < out.size(); ++k)
    for (int i = 0; i < (int)parts.size(); ++i)
      if (parts[i].parent == out[k] && parts[i].alive) out.push_back(i);
}

int Ship::aliveCount() const {
  int n = 0;
  for (auto& p : parts) n += p.alive ? 1 : 0;
  return n;
}
