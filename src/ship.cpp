#include "ship.h"
#include "weapons.h"

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
  void w(int i, float pitch, float yaw, float roll) { s.parts[i].wPitch = pitch; s.parts[i].wYaw = yaw; s.parts[i].wRoll = roll; }
  void w2(int i, float pitch, float yaw, float roll) { w(i, pitch, yaw, roll); w(i + 1, pitch, yaw, roll); }
};

// One weapon assembly hanging under a wing pylon. Its parts are what the damage model
// looks at to decide how well (or whether) the weapon still works.
void buildCannon(Builder& b, const Palette& pal, int sys, int weapon, int pylon) {
  float xs = sys == 0 ? -1.0f : 1.0f;
  Vec3 c(xs * 3.0f, -1.2f, -0.6f);
  auto at = [&](float x, float y, float z) { return c + Vec3(x, y, z); };
  const Vec3 Z(0, 0, 0);
  const Vec3 wc = WEAPONS[weapon].color;
  std::string n = WEAPONS[weapon].name;
  float muzzleZ = -3.0f;
  switch (weapon) {
    case W_PULSE: {
      int body = b.add(n + " HOUSING", Role::CannonBody, M_SQPRISM, at(0, 0, 0), Vec3(0.5f, 0.5f, 2.4f), Z, pal.hull2, 60, pylon, sys);
      int bar = b.add(n + " BARREL", Role::CannonBarrel, M_CYL, at(0, 0, -2.3f), Vec3(0.2f, 0.2f, 2.4f), Z, pal.dark, 36, body, sys);
      b.add(n + " LENS", Role::CannonAux, M_SPHERE, at(0, 0, -3.52f), Vec3(0.27f), Z, wc, 10, bar, sys, MAT_EMISSIVE, 2.5f);
      b.add(n + " CAPACITOR", Role::CannonAux, M_CYL, at(0, 0.38f, 0.2f), Vec3(0.3f, 0.3f, 1.5f), Z, pal.hull2, 25, body, sys);
      b.add(n + " COOLING FIN", Role::CannonAux, M_RBOX, at(0.3f, 0, -0.3f), Vec3(0.06f, 0.55f, 1.2f), Z, pal.dark, 12, body, sys);
      b.add(n + " COOLING FIN", Role::CannonAux, M_RBOX, at(-0.3f, 0, -0.3f), Vec3(0.06f, 0.55f, 1.2f), Z, pal.dark, 12, body, sys);
      muzzleZ = -3.75f;
    } break;
    case W_GATLING: {
      int body = b.add(n + " HOUSING", Role::CannonBody, M_SQPRISM, at(0, 0, 0), Vec3(0.62f, 0.62f, 1.9f), Z, pal.hull2, 70, pylon, sys);
      for (int i = 0; i < 4; ++i) {
        float ox = (i & 1) ? 0.13f : -0.13f, oy = (i & 2) ? 0.13f : -0.13f;
        b.add(n + " BARREL", Role::CannonBarrel, M_CYL, at(ox, oy, -2.3f), Vec3(0.11f, 0.11f, 2.8f), Z, pal.dark, 18, body, sys);
      }
      b.add(n + " SHROUD", Role::CannonAux, M_CYL, at(0, 0, -1.25f), Vec3(0.5f, 0.5f, 0.3f), Z, pal.hull2, 22, body, sys);
      b.add(n + " AMMO DRUM", Role::CannonAux, M_CYL, at(0, 0.45f, 0.3f), Vec3(0.6f, 0.6f, 0.7f), Vec3(0, 90, 0), pal.hull, 30, body, sys);
      muzzleZ = -3.8f;
    } break;
    case W_ION: {
      int body = b.add(n + " HOUSING", Role::CannonBody, M_SQPRISM, at(0, 0, 0), Vec3(0.55f, 0.55f, 2.0f), Z, pal.hull2, 60, pylon, sys);
      int bar = b.add(n + " EMITTER", Role::CannonBarrel, M_PRISM, at(0, 0, -2.0f), Vec3(0.34f, 0.34f, 2.2f), Z, pal.dark, 40, body, sys);
      for (int i = 0; i < 3; ++i)
        b.add(n + " FOCUS RING", Role::CannonAux, M_CYL, at(0, 0, -1.5f - 0.6f * i), Vec3(0.52f, 0.52f, 0.1f), Z, wc, 10, bar, sys, MAT_EMISSIVE, 1.8f);
      b.add(n + " POWER CELL", Role::CannonAux, M_CYL, at(0, 0.38f, 0.2f), Vec3(0.34f, 0.34f, 1.2f), Z, pal.hull2, 22, body, sys);
      muzzleZ = -3.2f;
    } break;
    case W_RAIL: {
      int body = b.add(n + " BREECH", Role::CannonBody, M_SQPRISM, at(0, 0, 0), Vec3(0.5f, 0.65f, 2.8f), Z, pal.hull2, 80, pylon, sys);
      b.add(n + " UPPER RAIL", Role::CannonBarrel, M_RBOX, at(0, 0.2f, -3.4f), Vec3(0.12f, 0.12f, 4.2f), Z, pal.dark, 40, body, sys);
      b.add(n + " LOWER RAIL", Role::CannonBarrel, M_RBOX, at(0, -0.2f, -3.4f), Vec3(0.12f, 0.12f, 4.2f), Z, pal.dark, 40, body, sys);
      b.add(n + " RAIL BRACE", Role::CannonAux, M_RBOX, at(0, 0, -2.6f), Vec3(0.22f, 0.62f, 0.12f), Z, pal.hull2, 12, body, sys);
      b.add(n + " RAIL BRACE", Role::CannonAux, M_RBOX, at(0, 0, -4.4f), Vec3(0.22f, 0.62f, 0.12f), Z, pal.hull2, 12, body, sys);
      b.add(n + " CAPACITOR", Role::CannonAux, M_CYL, at(0.34f, 0.1f, 0.3f), Vec3(0.28f, 0.28f, 1.6f), Z, wc * 0.6f, 25, body, sys);
      b.add(n + " CAPACITOR", Role::CannonAux, M_CYL, at(-0.34f, 0.1f, 0.3f), Vec3(0.28f, 0.28f, 1.6f), Z, wc * 0.6f, 25, body, sys);
      muzzleZ = -5.6f;
    } break;
    case W_PLASMA: {
      int body = b.add(n + " HOUSING", Role::CannonBody, M_SQPRISM, at(0, 0, 0), Vec3(0.8f, 0.8f, 2.0f), Z, pal.hull2, 85, pylon, sys);
      b.add(n + " CHAMBER", Role::CannonAux, M_SPHERE, at(0, 0.3f, 0.2f), Vec3(0.72f), Z, wc, 30, body, sys, MAT_EMISSIVE, 2.0f);
      int bar = b.add(n + " BARREL", Role::CannonBarrel, M_CYL, at(0, 0, -1.75f), Vec3(0.52f, 0.52f, 1.6f), Z, pal.dark, 50, body, sys);
      for (int i = 0; i < 3; ++i)
        b.add(n + " COIL", Role::CannonAux, M_CYL, at(0, 0, -1.25f - 0.5f * i), Vec3(0.68f, 0.68f, 0.14f), Z, pal.hull2, 12, bar, sys);
      muzzleZ = -2.7f;
    } break;
    default: { // missile pod
      int body = b.add(n + " POD", Role::CannonBody, M_SQPRISM, at(0, 0, 0), Vec3(0.95f, 0.75f, 2.6f), Z, pal.hull2, 90, pylon, sys);
      for (int i = 0; i < 4; ++i) {
        float ox = (i & 1) ? 0.23f : -0.23f, oy = (i & 2) ? 0.17f : -0.17f;
        b.add(n + " TUBE", Role::CannonBarrel, M_CYL, at(ox, oy, -1.38f), Vec3(0.32f, 0.3f, 0.3f), Z, pal.dark, 20, body, sys);
        if (i == 0 || i == 3)
          b.add(n + " WARHEAD", Role::CannonAux, M_CONE, at(ox, oy, -1.7f), Vec3(0.22f, 0.22f, 0.5f), Z, pal.hull2, 8, body, sys);
      }
      b.add(n + " EXHAUST VENT", Role::CannonAux, M_RBOX, at(0, 0, 1.35f), Vec3(0.7f, 0.5f, 0.12f), Z, pal.dark, 15, body, sys);
      muzzleZ = -1.9f;
    } break;
  }
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

  // ---------------------------------------------------------------- fuselage: one continuous body in five sections
  int keel = S("KEEL", R::Hull, M_S_MID, pal.hull, 320, -1);
  parts[keel].critical = true;
  int fwd = S("FORWARD FUSELAGE", R::Hull, M_S_FWD, pal.hull, 200, keel);
  int nose = S("NOSE CONE", R::Hull, M_S_NOSE, pal.hull2, 90, fwd);
  b.add("SENSOR PROBE", R::Sensor, M_CYL, Vec3(0, -0.08f, -11.2f), Vec3(0.05f, 0.05f, 1.4f), Z, pal.dark, 14, nose);
  int aft = S("AFT FUSELAGE", R::Hull, M_S_AFT, pal.hull, 240, keel);
  int tail = S("TAIL BOOM", R::Hull, M_S_TAIL, pal.hull2, 90, aft);
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

  // ---------------------------------------------------------------- armour: plates that follow the hull's surface
  int midArmor = S("PORT MID ARMOR", R::Armor, M_S_ARMOR0 + 1, pal.hull2, 75, keel, 0);
  S("STBD MID ARMOR", R::Armor, M_S_ARMOR0 + 0, pal.hull2, 75, keel, 1);
  S("VENTRAL ARMOR", R::Armor, M_S_ARMOR0 + 2, pal.hull2, 90, keel);
  S("PORT FORWARD ARMOR", R::Armor, M_S_ARMOR0 + 4, pal.hull2, 60, fwd, 0);
  S("STBD FORWARD ARMOR", R::Armor, M_S_ARMOR0 + 3, pal.hull2, 60, fwd, 1);
  S("CHIN ARMOR", R::Armor, M_S_ARMOR0 + 5, pal.hull2, 70, fwd);
  S("PORT AFT ARMOR", R::Armor, M_S_ARMOR0 + 7, pal.hull2, 80, aft, 0);
  S("STBD AFT ARMOR", R::Armor, M_S_ARMOR0 + 6, pal.hull2, 80, aft, 1);
  S("AFT VENTRAL ARMOR", R::Armor, M_S_ARMOR0 + 8, pal.hull2, 80, aft);
  S("PORT DORSAL ARMOR", R::Armor, M_S_ARMOR0 + 10, pal.hull2, 60, aft, 0);
  S("STBD DORSAL ARMOR", R::Armor, M_S_ARMOR0 + 9, pal.hull2, 60, aft, 1);
  S("PORT SHOULDER ARMOR", R::Armor, M_S_ARMOR0 + 12, pal.hull2, 60, keel, 0);
  S("STBD SHOULDER ARMOR", R::Armor, M_S_ARMOR0 + 11, pal.hull2, 60, keel, 1);

  // ---------------------------------------------------------------- fittings
  b.pair("LANDING SKID", R::Detail, M_SQPRISM, Vec3(0.7f, -1.03f, 1.0f), Vec3(0.18f, 0.4f, 2.6f), Z, pal.dark, 40, keel, keel);
  b.add("VENTRAL SENSOR POD", R::Sensor, M_SPHERE, Vec3(0, -0.72f, -3.6f), Vec3(0.55f, 0.36f, 1.1f), Z, pal.dark, 25, fwd);
  b.pair("PITOT TUBE", R::Detail, M_CYL, Vec3(0.36f, 0.08f, -8.9f), Vec3(0.03f, 0.03f, 0.9f), Z, pal.dark, 6, nose, nose);
  b.pair("FORMATION LIGHT", R::Light, M_RBOX, Vec3(1.25f, 0.42f, -1.2f), Vec3(0.03f, 0.05f, 0.9f), Z, pal.glow, 6, midArmor, midArmor + 1, MAT_EMISSIVE, 1.5f);
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
    if (p.role != Role::Light && p.role != Role::Detail) { hpMax += p.maxHp; hp += p.alive ? p.hp : 0; }
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
