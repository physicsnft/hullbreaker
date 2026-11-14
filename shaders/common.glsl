#version 450
// Shared by every shader stage (prepended at build time).
layout(set = 0, binding = 0) uniform Global {
  mat4 viewProj;
  vec4 camPos;    // xyz, w = time
  vec4 camRight;  // xyz, w = tan(fovX/2)
  vec4 camUp;     // xyz, w = tan(fovY/2)
  vec4 camFwd;    // xyz, w = sky seed
  vec4 sunDir;    // xyz towards the sun, w = angular radius
  vec4 sunColor;  // rgb, w = intensity
  vec4 ambient;   // rgb, w = sky mode (0 space, 1 hangar)
  vec4 fog;       // rgb, w = density
  vec4 nebA;
  vec4 nebB;
  vec4 nebC;
  vec4 nebParams; // x density, y scale
  vec4 planetPos[4];    // xyz, w radius
  vec4 planetColA[4];   // rgb, w type
  vec4 planetColB[4];   // rgb, w atmosphere
  vec4 planetParams[4]; // x ring, y/z ring tilt, w seed
  vec4 screen;    // w, h, 1/w, 1/h
  vec4 misc;      // x planet count, y exposure, z unused, w manual gamma
  vec4 skySun;    // xyz direction of the star as drawn in the sky
} G;

// Lattice hashes work on integers. A float hash of the fract(p * k) kind gives different
// results for the same lattice point depending on how the compiler orders the arithmetic,
// which breaks the noise into visible shards on some GPUs.
uint hashU(ivec3 i) {
  uint h = uint(i.x) * 0x8da6b343u ^ uint(i.y) * 0xd8163841u ^ uint(i.z) * 0xcb1ab31fu;
  h ^= h >> 15; h *= 0x2c1b3c6du; h ^= h >> 12; h *= 0x297a2d39u; h ^= h >> 15;
  return h;
}
float hash31(vec3 p) { return float(hashU(ivec3(floor(p))) >> 8) * (1.0 / 16777216.0); }
float hash21(vec2 p) { return hash31(vec3(p, 17.0)); }
float hash11(float p) { return hash31(vec3(p, 3.0, 29.0)); }

float noise3(vec3 p) {
  vec3 i = floor(p), f = p - i;
  f = f * f * (3.0 - 2.0 * f);
  float a = mix(mix(hash31(i), hash31(i + vec3(1, 0, 0)), f.x), mix(hash31(i + vec3(0, 1, 0)), hash31(i + vec3(1, 1, 0)), f.x), f.y);
  float b = mix(mix(hash31(i + vec3(0, 0, 1)), hash31(i + vec3(1, 0, 1)), f.x), mix(hash31(i + vec3(0, 1, 1)), hash31(i + vec3(1, 1, 1)), f.x), f.y);
  return mix(a, b, f.z);
}
float fbm(vec3 p) {
  float a = 0.5, s = 0.0;
  for (int i = 0; i < 5; ++i) { s += a * noise3(p); p = p * 2.02 + vec3(11.3, 5.7, 3.1); a *= 0.5; }
  return s / 0.96875;
}
float fbm3(vec3 p) {
  float a = 0.5, s = 0.0;
  for (int i = 0; i < 3; ++i) { s += a * noise3(p); p = p * 2.02 + vec3(11.3, 5.7, 3.1); a *= 0.5; }
  return s / 0.875;
}
vec3 tonemap(vec3 x) { // ACES approximation
  x *= G.misc.y;
  return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}
vec3 finalize(vec3 c) { return G.misc.w > 0.5 ? pow(c, vec3(1.0 / 2.2)) : c; }
