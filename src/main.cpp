#include "audio.h"
#include "game.h"
#include "textures.h"
#include <string>
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
      "  --texsize N            surface texture resolution: 512, 1024, 2048 (default) or 4096\n"
      "  --dump-textures DIR    write the generated textures to DIR as PPM images\n"
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
    else if (is("--texsize")) o.texSize = std::atoi(next());
    else if (is("--dump-textures")) { // write every generated texture layer as PPM images into a directory
      std::string dir = next();
      TextureSet ts;
      buildTextures(o.texSize, ts);
      static const char* names[TEX_LAYERS] = {"hull", "rock", "ice", "industrial"};
      for (int l = 0; l < ts.layers; ++l)
        for (int k = 0; k < 4; ++k) { // colour, roughness, normal, occlusion+wear
          static const char* kind[4] = {"colour", "roughness", "normal", "occlusion_wear"};
          std::string path = dir + "/" + names[l] + "_" + kind[k] + ".ppm";
          FILE* f = std::fopen(path.c_str(), "wb");
          if (!f) { std::fprintf(stderr, "cannot write %s\n", path.c_str()); return 1; }
          std::fprintf(f, "P6\n%d %d\n255\n", ts.size, ts.size);
          const uint8_t* A = ts.mipsA[0].data() + (size_t)l * ts.size * ts.size * 4;
          const uint8_t* B = ts.mipsB[0].data() + (size_t)l * ts.size * ts.size * 4;
          for (size_t i = 0; i < (size_t)ts.size * ts.size; ++i) {
            uint8_t px[3];
            if (k == 0) { px[0] = A[i * 4]; px[1] = A[i * 4 + 1]; px[2] = A[i * 4 + 2]; }
            else if (k == 1) px[0] = px[1] = px[2] = A[i * 4 + 3];
            else if (k == 2) { px[0] = B[i * 4]; px[1] = B[i * 4 + 1]; px[2] = 255; }
            else { px[0] = px[1] = B[i * 4 + 2]; px[2] = B[i * 4 + 3]; }
            std::fwrite(px, 1, 3, f);
          }
          std::fclose(f);
        }
      std::printf("wrote %d texture images (%dx%d) to %s\n", ts.layers * 4, ts.size, ts.size, dir.c_str());
      return 0;
    }
    else if (is("--orbit")) o.orbit = (float)std::atof(next());
    else if (is("--cam-yaw")) o.camYaw = (float)std::atof(next());
    else if (is("--focus")) std::sscanf(next(), "%f,%f,%f", &o.focus[0], &o.focus[1], &o.focus[2]);
    else if (is("--zoom")) o.zoom = (float)std::atof(next());
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
