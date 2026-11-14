// Surface textures, generated at start-up (nothing is loaded from disk).
// Every layer tiles seamlessly and comes as two RGBA8 images:
//   A: rgb = colour multiplier (stored / 1.3), a = roughness
//   B: rg  = tangent-space normal xy, b = ambient occlusion, a = wear / frost mask
#pragma once
#include <cstdint>
#include <vector>

enum TexLayer {
  TEX_HULL = 0,   // ship plating: panels, seams, rivets, hatches, scratches, grime. One tile = 6 m
  TEX_ROCK,       // asteroid surface: ridges, craters, grain
  TEX_ICE,        // fractured ice with frost
  TEX_INDUSTRIAL, // station plating: seams, bolts, scuffs, stains. One tile = 4 m
  TEX_LAYERS
};

struct TextureSet {
  int size = 0, layers = TEX_LAYERS, mipCount = 0;
  // mips[m] holds all layers of level m back to back: layers * (size >> m)^2 * 4 bytes
  std::vector<std::vector<uint8_t>> mipsA, mipsB;
};

void buildTextures(int size, TextureSet& out);
