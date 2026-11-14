// UI quads. Text needs no font texture: each glyph is a 5x7 bitmap packed into 35 bits
// that travel with the vertex and are sampled here.
layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vColor;
layout(location = 2) flat in uvec2 vGlyph;
layout(location = 0) out vec4 outColor;

void main() {
  float a = 1.0;
  if (vGlyph.y == 0xFFFFFFFFu) {
    uint mode = vGlyph.x & 0xFFu;
    float r = length(vUV * 2.0 - 1.0);
    float aa = fwidth(r) * 1.5;
    if (mode == 1u) {
      a = smoothstep(1.0, 1.0 - aa, r);
    } else if (mode == 2u) {
      float th = float((vGlyph.x >> 8) & 0xFFu) / 255.0;
      a = smoothstep(1.0, 1.0 - aa, r) * smoothstep(1.0 - th - aa, 1.0 - th, r);
    }
  } else {
    int cx = clamp(int(vUV.x * 5.0), 0, 4);
    int cy = clamp(int(vUV.y * 7.0), 0, 6);
    int bit = cx * 7 + cy;
    uint v = bit < 32 ? (vGlyph.x >> uint(bit)) & 1u : (vGlyph.y >> uint(bit - 32)) & 1u;
    if (v == 0u) discard;
  }
  if (a <= 0.001) discard;
  outColor = vec4(vColor.rgb, vColor.a * a);
}
