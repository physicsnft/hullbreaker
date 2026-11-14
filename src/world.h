// Simulation: sector generation, rigid-body flight, weapons, damage, AI.
#pragma once
#include "math.h"
#include "sfx.h"
#include "ship.h"
#include "weapons.h"
#include <string>
#include <vector>

// ---- flight model constants (SI units: metres, seconds, kilograms)
constexpr float kMainAccel = 58.0f;    // main-engine acceleration at full health and design mass
constexpr float kRetroAccel = 30.0f;   // braking thrusters
constexpr float kRcsAccel = 26.0f;     // lateral thrusters
constexpr float kMaxSpeed = 130.0f;    // flight-assist cruise limit
constexpr float kBoostSpeed = 240.0f;
constexpr float kStrafeSpeed = 55.0f;
constexpr float kReverseSpeed = 60.0f;
constexpr float kBoostThrust = 1.9f;
constexpr float kTurnRate[3] = {1.55f, 1.15f, 2.5f};  // commanded pitch / yaw / roll rate, rad/s
constexpr float kAngAccel[3] = {5.5f, 4.2f, 9.0f};    // torque authority, rad/s^2 at design inertia
constexpr float kShipRadius = 11.0f;   // bounding sphere for broad-phase
constexpr float kShipCollide = 6.0f;   // collision sphere
constexpr float kConvergence = 450.0f; // gun convergence distance

struct Controls {
  float thrust = 0, strafeX = 0, strafeY = 0;
  float pitch = 0, yaw = 0, roll = 0; // +pitch = nose up, +yaw = nose right, +roll = clockwise
  bool boost = false, brake = false, assist = true;
  bool fire[2] = {false, false};
};

struct Planet {
  Vec3 pos;
  float radius = 1;
  Vec3 colA, colB;
  int type = 0; // 0 rocky, 1 gas giant, 2 terran, 3 lava, 4 ice
  bool ring = false;
  float atmo = 0, seed = 0;
  float tiltX = 0, tiltZ = 0;
};

enum SectorType { SEC_BELT = 0, SEC_NEBULA, SEC_PLANET, SEC_STAR, SEC_ICE, SEC_GRAVEYARD, SEC_COUNT };
extern const char* const SECTOR_NAMES[SEC_COUNT];

struct Sector {
  uint32_t seed = 0;
  int type = 0;
  std::string name;
  Vec3 sunDir{0, 1, 0}, sunColor{1, 1, 1};
  float sunSize = 0.03f, sunIntensity = 3;
  Vec3 ambient{0.05f, 0.05f, 0.06f};
  Vec3 fogColor;
  float fogDensity = 0;
  Vec3 nebA, nebB, nebC;
  float nebDensity = 0.5f, nebScale = 2.0f;
  float skySeed = 0, exposure = 1;
  std::vector<Planet> planets;
};

struct Asteroid {
  Vec3 pos, vel;
  Quat rot;
  Vec3 angVel;
  int mesh = M_AST0, meshLo = M_ASTLO0;
  Vec3 scale{1, 1, 1};
  float radius = 1, mass = 1, hp = 1, maxHp = 1;
  Vec3 color;
  int material = MAT_ROCK;
  float seed = 0, damage = 0;
  bool alive = true;
};

struct Projectile {
  Vec3 pos, prev, vel;
  float life = 1;
  int weapon = 0, owner = -1, team = 0, target = -1;
  float dmgScale = 1;
  float trail = 0;
  bool alive = true;
};

struct DebrisPiece {
  int mesh; Vec3 pos; Quat rot; Vec3 scale; Vec3 color; int material; float seed, damage, emissive;
};
struct Debris {
  Vec3 pos, vel;
  Quat rot;
  Vec3 angVel;
  std::vector<DebrisPiece> pieces;
  float life = 1, maxLife = 1;
  float smoke = 0;
};

struct Particle {
  Vec3 pos, vel;
  float life, maxLife;
  float size0, size1;
  float c0[4], c1[4];
  int kind;      // 0 glow, 1 ring, 2 streak, 3 smoke
  float stretch; // streak length in seconds of travel
  float drag;
};

struct Message {
  std::string text;
  float ttl;
  Vec3 color;
};

struct SoundEvent {
  int sfx;
  Vec3 pos;
  float gain;
  int ship; // id of the ship it happened to / came from, or -1
};

struct Difficulty {
  const char* name;
  const char* blurb;
  int minEnemies, maxEnemies;
  float damage;     // multiplier on enemy weapon damage
  float skillLo, skillHi;
  int attackers;    // how many enemies press the attack at once; the rest hold back
  float shield;     // enemy shield strength
  float reload;     // multiplier on enemy reload time
  float playerDamage; // multiplier on the player's weapon damage against ships
};
const int DIFFICULTY_COUNT = 4;
extern const Difficulty DIFFICULTY[DIFFICULTY_COUNT];

enum AIState { AI_ATTACK = 0, AI_BREAK, AI_EVADE };

struct ShipEntity {
  int id = 0, team = 0;
  bool player = false, alive = true;
  Ship ship;
  Vec3 pos, vel;
  Quat rot;
  Vec3 angVel; // body frame
  Controls in;
  float shield = 0, shieldMax = 100, shieldDelay = 0, shieldFlash = 0;
  float energy = 100;
  float reload[2] = {0, 0}, reloadMax[2] = {1, 1};
  float misfire[2] = {0, 0};
  float mainOut = 0;        // main engine output 0..1 (for exhaust visuals)
  float engineOut[2] = {0, 0};
  bool boosting = false;
  float hitFlash = 0, smokeT = 0, trailT = 0, abandonT = 0;
  int target = -1;
  std::string callsign;
  int shotsFired = 0, shotsHit = 0, partsLost = 0;
  // AI
  int aiState = AI_ATTACK;
  float aiTimer = 0, skill = 0.7f, burst = 0, noiseT = 0;
  bool aiHold = false; // waiting for an attack slot
  Vec3 aiVec, aimNoise;
};

class World {
 public:
  Sector sector;
  std::vector<ShipEntity> ships; // ships[0] is the player
  std::vector<Asteroid> asteroids;
  std::vector<Projectile> projectiles;
  std::vector<Debris> debris;
  std::vector<Particle> particles;
  std::vector<Message> messages;
  Rng rng{1};
  float time = 0;
  int enemiesTotal = 0, enemiesAlive = 0;
  int result = 0; // 0 running, 1 victory, 2 defeat
  float endTimer = 0;
  float shake = 0;
  int difficulty = 1;
  std::vector<SoundEvent> sounds; // drained by the game every frame
  void sound(int sfx, Vec3 pos, float gain = 1.0f, int ship = -1) {
    if (sounds.size() < 96) sounds.push_back({sfx, pos, gain, ship});
  }
  bool autopilot = false;
  bool logMessages = false;
  float enemyDamageMul = 0.32f;
  Vec3 lastPlayerPos;

  void generate(uint32_t seed, int sectorType, int w0, int w1, bool withEnemies = true);
  void update(float dt);
  ShipEntity& player() { return ships[0]; }
  const ShipEntity& player() const { return ships[0]; }
  void cycleTarget(int dir);
  void targetNearest();
  bool debugHitAsteroid(size_t idx, float dmg, Vec3 dir); // testing: shoot an asteroid
  bool debugDestroy(const std::string& partName); // testing: blow a named part off the player's ship
  void say(const std::string& text, Vec3 color = Vec3(0.5f, 0.9f, 1.0f), float ttl = 5.0f);

 private:
  void generateSector(uint32_t seed, int type);
  void populate(Rng& r);
  void addAsteroid(Rng& r, Vec3 pos, float radius, int kind);
  void spawnEnemies(Rng& r);
  void stepShip(ShipEntity& s, float dt);
  void fireWeapons(ShipEntity& s, float dt);
  void updateAI(ShipEntity& s, ShipEntity* target, float dt);
  void stepProjectiles(float dt);
  void collisions(float dt);
  void applyHit(ShipEntity& s, int part, Vec3 hitLocal, const WeaponDef& w, float dmgScale, Vec3 dirWorld);
  void damagePart(ShipEntity& s, int idx, float dmg, Vec3 dirWorld, bool bleed = true);
  void destroyPart(ShipEntity& s, int idx, Vec3 dirWorld);
  void explodeShip(ShipEntity& s);
  void damageAsteroid(size_t idx, float dmg, Vec3 hitPos, Vec3 dir);
  void fractureAsteroid(size_t idx, Vec3 dir, float overkill);
  void spawnDebris(const ShipEntity& s, const std::vector<int>& idx, Vec3 impulseDir, float speed);
  // effects
  void emit(Vec3 pos, Vec3 vel, float life, float s0, float s1, Vec3 c0, float a0, Vec3 c1, float a1, int kind, float stretch = 0, float drag = 0);
  void fxSparks(Vec3 pos, Vec3 baseVel, Vec3 dir, Vec3 color, int n, float speed);
  void fxExplosion(Vec3 pos, Vec3 vel, float size);
  void fxSmoke(Vec3 pos, Vec3 vel, float size);
};
