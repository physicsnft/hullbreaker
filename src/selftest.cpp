// Headless checks that damage really changes how the ship behaves.
// Run with: hullbreaker --selftest
#include "game.h"
#include "world.h"
#include <cstdio>

namespace {
int g_fail = 0;
void check(bool ok, const char* what) {
  std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) ++g_fail;
}
void fresh(World& w, int w0, int w1) {
  w.generate(42, SEC_BELT, w0, w1, false);
  w.asteroids.clear();
}
void run(World& w, float secs, const Controls& c) {
  for (float t = 0; t < secs; t += 1.0f / 120.0f) {
    w.player().in = c;
    w.update(1.0f / 120.0f);
  }
}
float headingDrift(const World& w) { // degrees between the nose and its starting direction (-Z)
  Vec3 f = rotate(w.player().rot, Vec3(0, 0, -1));
  return std::acos(clampf(-f.z, -1.0f, 1.0f)) * 180.0f / PI;
}
} // namespace

int runSelfTest() {
  std::vector<MeshData> meshes;
  buildMeshes(meshes); // sets the asteroid radii the world generator uses
  World w;
  Controls burn;
  burn.assist = false;
  burn.thrust = 1;

  std::printf("Ship structure\n");
  fresh(w, W_GATLING, W_RAIL);
  const Ship& ship = w.player().ship;
  std::printf("    %d parts, %.1f t, inertia (%.0f, %.0f, %.0f) kg m2\n", (int)ship.parts.size(), ship.stats.mass / 1000,
              ship.stats.inertia.x, ship.stats.inertia.y, ship.stats.inertia.z);
  check(ship.parts.size() >= 120, "ship is built from 120+ individual parts");
  float t;
  int hit = ship.raycast(Vec3(5, 10, 2), Vec3(0, -1, 0), 20, t);
  check(hit >= 0 && ship.parts[hit].name.find("WING") != std::string::npos, "a shot from above the starboard wing hits a wing part");
  hit = ship.raycast(Vec3(0, -0.08f, -30), Vec3(0, 0, 1), 40, t);
  check(hit >= 0 && ship.parts[hit].name == "SENSOR PROBE", "a head-on shot hits the nose probe first");

  std::printf("Engines\n");
  run(w, 3, burn);
  float vFull = length(w.player().vel), driftFull = headingDrift(w);
  fresh(w, W_GATLING, W_RAIL);
  w.debugDestroy("PORT ENGINE NOZZLE");
  float massAfter = w.player().ship.stats.mass;
  run(w, 3, burn);
  float vOne = length(w.player().vel), driftOne = headingDrift(w);
  w.debugDestroy("STBD ENGINE NACELLE");
  Vec3 v0 = w.player().vel;
  run(w, 2, burn);
  float dvNone = length(w.player().vel - v0);
  std::printf("    3 s burn: both engines %.0f m/s (nose drift %.1f deg), one engine %.0f m/s (drift %.1f deg), none: dv %.1f m/s\n", vFull,
              driftFull, vOne, driftOne, dvNone);
  check(vFull > 150, "two healthy engines accelerate the ship hard");
  check(vOne < vFull * 0.62f && vOne > vFull * 0.3f, "losing one engine roughly halves acceleration");
  check(driftOne > driftFull + 1.0f, "thrust from a single engine twists the ship off course");
  check(dvNone < 1.0f, "with both engines gone the ship cannot accelerate");
  check(massAfter < ship.baseMass, "a destroyed part no longer counts toward the ship's mass");
  check(!w.debris.empty(), "destroyed parts become drifting debris");

  std::printf("Wings / steering\n");
  Controls roll;
  roll.roll = 1;
  fresh(w, W_GATLING, W_RAIL);
  run(w, 0.25f, roll);
  float rollFull = std::fabs(w.player().angVel.z);
  Controls pitch;
  pitch.pitch = 1;
  fresh(w, W_GATLING, W_RAIL);
  run(w, 2, pitch);
  float coupleFull = std::fabs(w.player().angVel.z);
  fresh(w, W_GATLING, W_RAIL);
  w.debugDestroy("PORT WING ROOT");
  float auth = w.player().ship.stats.authority[2];
  int lost = w.player().partsLost;
  run(w, 0.25f, roll);
  float rollHurt = std::fabs(w.player().angVel.z);
  fresh(w, W_GATLING, W_RAIL);
  w.debugDestroy("PORT WING ROOT");
  run(w, 2, pitch);
  float coupleHurt = std::fabs(w.player().angVel.z);
  std::printf("    roll rate after 0.25 s: intact %.2f rad/s, port wing gone %.2f rad/s (authority %.0f%%, %d parts torn off)\n", rollFull,
              rollHurt, auth * 100, lost);
  std::printf("    uncommanded roll while pitching: intact %.3f rad/s, port wing gone %.3f rad/s\n", coupleFull, coupleHurt);
  check(lost > 10, "shooting off the wing root takes the whole wing (and its weapon) with it");
  check(rollHurt < rollFull * 0.75f, "a missing wing makes the ship slower to roll");
  check(coupleHurt > coupleFull + 0.02f, "a missing wing makes the ship roll by itself in turns");
  check(w.player().ship.stats.weapon[0] <= 0, "the weapon hanging from the lost wing is offline");

  std::printf("Cannons\n");
  Controls fire;
  fire.fire[0] = true;
  fresh(w, W_GATLING, W_RAIL);
  run(w, 4, fire);
  int shotsFull = w.player().shotsFired;
  fresh(w, W_GATLING, W_RAIL);
  w.debugDestroy("VULCAN GATLING BARREL");
  w.debugDestroy("VULCAN GATLING BARREL");
  float cond = w.player().ship.stats.weapon[0];
  run(w, 4, fire);
  int shotsHalf = w.player().shotsFired;
  w.debugDestroy("VULCAN GATLING HOUSING");
  int before = w.player().shotsFired;
  run(w, 2, fire);
  int shotsDead = w.player().shotsFired - before;
  std::printf("    4 s of fire: intact %d shots, two of four barrels gone %d shots (condition %.0f%%), housing gone %d shots\n", shotsFull,
              shotsHalf, cond * 100, shotsDead);
  check(shotsFull > 50, "an intact gatling fires rapidly");
  check(shotsHalf < shotsFull * 0.85f && shotsHalf > 5, "a damaged cannon still works, but fires slower and misfires");
  check(shotsDead == 0, "a destroyed cannon cannot fire");

  std::printf("Ship loss\n");
  fresh(w, W_PULSE, W_RAIL);
  w.debugDestroy("COCKPIT");
  check(!w.player().alive, "losing a critical part destroys the ship");

  std::printf("Procedural sectors\n");
  bool varied = true;
  for (int type = 0; type < SEC_COUNT; ++type) {
    World a, b;
    a.generate(100 + type, type, 0, 3, true);
    b.generate(200 + type, type, 0, 3, true);
    std::printf("    %-16s %-12s %3d bodies, %d hostiles, %d planets   |   %-12s %3d bodies, %d hostiles\n", SECTOR_NAMES[type],
                a.sector.name.c_str(), (int)a.asteroids.size(), a.enemiesTotal, (int)a.sector.planets.size(), b.sector.name.c_str(),
                (int)b.asteroids.size(), b.enemiesTotal);
    varied &= !a.asteroids.empty() && a.enemiesTotal >= 3 && a.enemiesTotal <= 6;
    varied &= length(a.ships[1].pos - b.ships[1].pos) > 1.0f;
  }
  check(varied, "every sector type generates, and different seeds give different enemies");

  std::printf("Difficulty (10 simulated matches each, the computer flying the player's ship)\n");
  int wins[DIFFICULTY_COUNT] = {0};
  for (int d = 0; d < DIFFICULTY_COUNT; ++d) {
    float hullSum = 0, timeSum = 0, longest = 0;
    int enemies = 0;
    for (int m = 0; m < 10; ++m) {
      World g;
      g.difficulty = d;
      g.autopilot = true;
      g.generate(1000 + m * 17, m % SEC_COUNT, W_PULSE, W_RAIL, true);
      enemies += g.enemiesTotal;
      while (g.result == 0 && g.time < 600) { g.update(1.0f / 60.0f); g.sounds.clear(); }
      if (g.result == 1) { ++wins[d]; hullSum += (g.player().ship.stats.hullIntegrity - kDeathIntegrity) / (1 - kDeathIntegrity); }
      timeSum += g.time;
      longest = std::max(longest, g.time);
    }
    std::printf("    %-8s %.1f enemies on average: %2d/10 won, hull left after a win %3.0f%%, match length mean %3.0f s, longest %3.0f s\n", DIFFICULTY[d].name,
                enemies / 10.0f, wins[d], wins[d] ? 100 * hullSum / wins[d] : 0.0f, timeSum / 10, longest);
  }
  check(wins[0] >= 8, "ROOKIE is comfortably winnable");
  check(wins[0] >= wins[1] && wins[1] >= wins[2] && wins[2] >= wins[3] && wins[0] > wins[3], "each level is at least as hard as the one below it");

  std::printf(g_fail ? "\n%d CHECK(S) FAILED\n" : "\nALL CHECKS PASSED\n", g_fail);
  return g_fail ? 1 : 0;
}
