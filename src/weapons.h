// Weapon catalogue. Three light weapons (fast reload, low damage) and three heavy ones
// (slow reload, high damage). Projectiles carry real mass: firing produces recoil and
// hits push the target around.
#pragma once
#include "math.h"

enum WeaponId { W_PULSE = 0, W_GATLING, W_ION, W_RAIL, W_PLASMA, W_MISSILE, W_COUNT };

struct WeaponDef {
  const char* name;
  bool heavy;
  float damage;    // per projectile
  float reload;    // seconds between shots
  float speed;     // muzzle velocity, m/s (added to the ship's own velocity)
  float spread;    // radians
  float energy;    // reactor energy per shot
  float projMass;  // kg
  float splash;    // blast radius in metres (0 = none)
  float shieldMul; // damage multiplier against shields
  float hullMul;   // damage multiplier against parts
  float life;      // seconds of flight
  int penetrate;   // extra parts pierced
  bool homing;
  Vec3 color;
  const char* desc;
};

inline const WeaponDef WEAPONS[W_COUNT] = {
    {"PULSE LASER", false, 9.0f, 0.125f, 1500.0f, 0.004f, 1.6f, 0.02f, 0.0f, 1.0f, 1.0f, 1.5f, 0, false, Vec3(1.0f, 0.22f, 0.16f),
     "Rapid energy bolts. Accurate and fast, but every shot drains the reactor."},
    {"VULCAN GATLING", false, 5.5f, 0.06f, 1150.0f, 0.014f, 0.0f, 0.35f, 0.0f, 0.6f, 1.25f, 1.5f, 0, false, Vec3(1.0f, 0.78f, 0.3f),
     "Four-barrel autocannon. Shreds plating, weak against shields. Loses fire rate with every barrel shot off."},
    {"ION BLASTER", false, 8.0f, 0.2f, 950.0f, 0.006f, 2.2f, 0.01f, 0.0f, 3.0f, 0.5f, 1.9f, 0, false, Vec3(0.3f, 0.65f, 1.0f),
     "Ion bolts collapse shields in moments but barely scratch armour."},
    {"RAILGUN", true, 120.0f, 1.9f, 4200.0f, 0.0008f, 14.0f, 6.0f, 0.0f, 1.0f, 1.0f, 0.9f, 3, false, Vec3(0.75f, 0.88f, 1.0f),
     "Hypervelocity slug that punches through several parts in a line. Heavy recoil."},
    {"PLASMA CANNON", true, 75.0f, 1.25f, 600.0f, 0.003f, 12.0f, 0.5f, 4.0f, 1.2f, 1.0f, 3.4f, 0, false, Vec3(0.3f, 1.0f, 0.42f),
     "Slow plasma orb that bursts on impact and melts everything around it."},
    {"SEEKER MISSILE", true, 95.0f, 2.6f, 140.0f, 0.02f, 0.0f, 40.0f, 5.5f, 0.8f, 1.1f, 9.0f, 0, true, Vec3(1.0f, 0.6f, 0.2f),
     "Guided warhead with a wide blast. Homes on your locked target."},
};

inline float weaponDps(const WeaponDef& w) { return w.damage / w.reload; }
