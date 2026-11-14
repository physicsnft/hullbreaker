// A ship is a tree of parts. Every part is a primitive with its own health, mass and
// role. Flight performance is never stored: it is recomputed from whichever parts are
// still attached, so damage changes how the ship flies as a direct consequence.
#pragma once
#include "math.h"
#include "meshes.h"
#include <string>
#include <vector>

enum class Role : uint8_t {
  Hull, Armor, Cockpit, Glass, Reactor, ShieldGen, Sensor,
  Wing, Control, Fin, RCS,
  EnginePylon, EngineCore, EngineNozzle, EngineAux,
  CannonMount, CannonBody, CannonBarrel, CannonAux,
  Light, Detail,
  Skin // outer plating: takes hits first, passes what it cannot absorb to the structure beneath
};

struct Part {
  std::string name;
  Role role = Role::Detail;
  int sys = -1;       // 0 = port, 1 = starboard (engines, wings, cannons)
  int mesh = M_BOX;
  Vec3 pos;           // ship-local
  Quat rot, invRot;
  Vec3 scale;         // also the oriented bounding box used for hit tests
  Vec3 color;
  float emissive = 0;
  int material = MAT_HULL;
  float maxHp = 1, hp = 1;
  float mass = 1;
  int parent = -1;    // structural attachment: if the parent goes, this goes with it
  bool alive = true;
  bool internal = false; // inside the hull: not hit directly, damaged by bleed-through
  bool critical = false; // losing it destroys the ship
  float wPitch = 0, wYaw = 0, wRoll = 0; // contribution to steering authority
  float seed = 0;
  float flash = 0;    // hit flash timer
  float frac() const { return alive ? clampf(hp / maxHp, 0.0f, 1.0f) : 0.0f; }
};

struct Palette {
  Vec3 hull, hull2, accent, dark, glass, glow;
};
extern const Palette PAL_PLAYER;
extern const Palette PAL_ENEMY[3];

constexpr float kDeathIntegrity = 0.5f; // the ship breaks up once half of its structure is gone

struct ShipStats {
  float mass = 1;
  Vec3 com;
  Vec3 inertia{1, 1, 1};
  float engine[2] = {1, 1};   // thrust efficiency per engine
  Vec3 enginePos[2];
  float authority[3] = {1, 1, 1}; // pitch, yaw, roll torque authority
  float asym = 0;             // -1..1, which side has lost roll surfaces (+ = port lost)
  float rcs = 1;              // lateral / retro thruster capability
  float weapon[2] = {1, 1};   // condition; 0 = offline
  float reactor = 1, shieldGen = 1, sensors = 1;
  bool canopy = true;
  float hullIntegrity = 1;
  bool dead = false;
};

struct Ship {
  std::vector<Part> parts;
  ShipStats stats;
  Vec3 muzzle[2];
  int weapon[2] = {0, 3};
  float baseMass = 1;
  Vec3 baseInertia{1, 1, 1};
  Vec3 baseCom; // centre of mass of the undamaged ship; the engines are trimmed to thrust through it
  Vec3 glow;

  void build(const Palette& pal, int w0, int w1, uint32_t seed);
  void recompute();
  // ray in ship-local space; returns the nearest part hit with 0 <= t <= tMax, or -1
  int raycast(const Vec3& o, const Vec3& d, float tMax, float& tHit, const int* ignore = nullptr, int nIgnore = 0) const;
  int nearestPart(const Vec3& p) const;
  void subtree(int idx, std::vector<int>& out) const;
  int aliveCount() const;
};

const char* sideName(int sys);
