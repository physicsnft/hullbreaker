// Turns simulation state into draw lists for the renderer.
#pragma once
#include "renderer.h"
#include "world.h"

struct Camera {
  Vec3 pos;
  Quat rot;
  float fovY = radians(62.0f);
  float aspect = 16.0f / 9.0f;
  Mat4 viewProj;
  Vec3 right, up, fwd;
  void update() {
    right = rotate(rot, Vec3(1, 0, 0));
    up = rotate(rot, Vec3(0, 1, 0));
    fwd = rotate(rot, Vec3(0, 0, -1));
    viewProj = perspectiveRevZ(fovY, aspect, 0.5f, 400000.0f) * viewFrom(pos, rot);
  }
  // world -> screen (in the given width/height units). Returns false when behind the camera.
  bool project(const Vec3& p, float W, float H, float& sx, float& sy) const {
    const float* m = viewProj.m;
    float x = m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12];
    float y = m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13];
    float w = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
    if (w < 0.05f) { sx = x; sy = y; return false; }
    sx = (x / w * 0.5f + 0.5f) * W;
    sy = (y / w * 0.5f + 0.5f) * H;
    return true;
  }
};

void renderEnvironment(FrameData& fd, const Sector& s, const Camera& cam, float time, bool hangar);
void renderHangar(FrameData& fd, const Ship& ship, const Ship* parked, float time);
void renderShipModel(FrameData& fd, const Ship& ship, const Vec3& pos, const Quat& rot, float time);
void renderWorld(FrameData& fd, const World& w, const Camera& cam);
void addInstance(FrameData& fd, bool additive, int mesh, const Mat4& m, Vec3 color, float emissive, int material, float damage, float seed, float intensity);
