// Immediate-mode 2D drawing in a virtual canvas that is always 900 units tall.
#pragma once
#include "renderer.h"
#include <string>

struct Color {
  float r, g, b, a;
  Color withA(float na) const { return {r, g, b, na}; }
  Color operator*(float s) const { return {r * s, g * s, b * s, a}; }
};

class UI {
 public:
  float W = 1600, H = 900;
  void begin(std::vector<UIVertex>* out, int pxW, int pxH);
  void rect(float x, float y, float w, float h, Color c);
  void frame(float x, float y, float w, float h, float t, Color c);
  void line(float x0, float y0, float x1, float y1, float t, Color c);
  void disc(float cx, float cy, float r, Color c);
  void ring(float cx, float cy, float r, float thick, Color c);
  void bar(float x, float y, float w, float h, float frac, Color fg, Color bg);
  void text(float x, float y, float size, Color c, const std::string& s);
  void textC(float cx, float y, float size, Color c, const std::string& s);
  void textR(float xr, float y, float size, Color c, const std::string& s);
  // word-wrapped text; returns the y below the last line
  float textWrap(float x, float y, float maxW, float size, Color c, const std::string& s);
  static float textW(const std::string& s, float size) { return (float)s.size() * 6.0f * size; }
  // corner brackets
  void brackets(float cx, float cy, float half, float len, float t, Color c);

 private:
  void quad(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, Color c, uint32_t g0, uint32_t g1);
  std::vector<UIVertex>* out_ = nullptr;
  float scale_ = 1;
};
