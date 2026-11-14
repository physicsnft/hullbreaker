#pragma once
#include <cstdint>
#include <string>

struct GameOptions {
  int width = 1600, height = 900;
  bool fullscreen = false, vsync = true, validation = false;
  int msaa = 4;
  std::string start = "menu"; // menu | hangar | play
  uint32_t seed = 0;          // 0 = random
  int sector = -1;            // -1 = random
  int weapon[2] = {0, 3};
  // testing / demo
  int frames = 0;             // quit after N frames (0 = run forever)
  bool autopilot = false;     // AI flies the player ship
  float fixedDt = 0;          // fixed simulation step per frame (0 = real time)
  float warp = 0;             // simulate this many seconds before the first frame
  bool log = false;           // print match status to stdout
  int difficulty = 1;         // 0 rookie, 1 pilot, 2 veteran, 3 ace
  bool sound = true;
  float camYaw = 0;           // testing: chase camera yaw offset in degrees
  float orbit = -1000;        // testing: fixed hangar camera angle in radians
};

int runGame(const GameOptions& opt);
int runSelfTest(); // headless checks of the damage and flight model
