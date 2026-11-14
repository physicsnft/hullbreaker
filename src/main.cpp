#include "audio.h"
#include "game.h"
#include "weapons.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static void usage() {
  std::printf(
      "HULLBREAKER - space combat\n\n"
      "  --fullscreen           start in fullscreen\n"
      "  --width N --height N   window size (default 1600x900)\n"
      "  --msaa N               1, 2, 4 (default) or 8\n"
      "  --novsync              uncapped frame rate\n"
      "  --seed N               fixed sector/enemy seed (default: random each game)\n"
      "  --sector N             0 belt, 1 nebula, 2 planet, 3 star, 4 ice, 5 graveyard\n"
      "  --weapons A,B          starting loadout, 0..5 (pulse, gatling, ion, rail, plasma, missile)\n"
      "  --start menu|hangar|play\n"
      "  --difficulty N         0 rookie, 1 pilot (default), 2 veteran, 3 ace\n"
      "  --nosound              run without audio\n"
      "  --render-audio F [S]   write S seconds of the generated music (for --seed) plus all effects to WAV file F\n"
      "  --validate             enable Vulkan validation layers\n"
      "  --autopilot --frames N --fixed-dt S --warp S --log   (demo / testing)\n"
      "  --selftest             headless checks of the damage / flight model (no window needed)\n");
}

int main(int argc, char** argv) {
  GameOptions o;
  for (int i = 1; i < argc; ++i) {
    auto is = [&](const char* s) { return std::strcmp(argv[i], s) == 0; };
    auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : "0"; };
    if (is("--fullscreen")) o.fullscreen = true;
    else if (is("--width")) o.width = std::atoi(next());
    else if (is("--height")) o.height = std::atoi(next());
    else if (is("--msaa")) o.msaa = std::atoi(next());
    else if (is("--novsync")) o.vsync = false;
    else if (is("--seed")) o.seed = (uint32_t)std::strtoul(next(), nullptr, 10);
    else if (is("--sector")) o.sector = std::atoi(next());
    else if (is("--start")) o.start = next();
    else if (is("--validate")) o.validation = true;
    else if (is("--autopilot")) o.autopilot = true;
    else if (is("--frames")) o.frames = std::atoi(next());
    else if (is("--fixed-dt")) o.fixedDt = (float)std::atof(next());
    else if (is("--warp")) o.warp = (float)std::atof(next());
    else if (is("--log")) o.log = true;
    else if (is("--selftest")) return runSelfTest();
    else if (is("--difficulty")) o.difficulty = std::atoi(next());
    else if (is("--nosound")) o.sound = false;
    else if (is("--orbit")) o.orbit = (float)std::atof(next());
    else if (is("--cam-yaw")) o.camYaw = (float)std::atof(next());
    else if (is("--render-audio")) {
      const char* path = next();
      float secs = i + 1 < argc ? (float)std::atof(argv[i + 1]) : 60.0f;
      return renderAudioToWav(path, secs > 1 ? secs : 60.0f, o.seed ? o.seed : 1);
    }
    else if (is("--weapons")) {
      int a = 0, b = 3;
      if (std::sscanf(next(), "%d,%d", &a, &b) == 2) {
        o.weapon[0] = a < 0 || a >= W_COUNT ? 0 : a;
        o.weapon[1] = b < 0 || b >= W_COUNT ? 3 : b;
      }
    } else { usage(); return is("--help") || is("-h") ? 0 : 1; }
  }
  return runGame(o);
}
