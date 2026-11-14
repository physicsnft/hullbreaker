layout(location = 0) in vec2 inPos; // pixels
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inColor;
layout(location = 3) in uvec2 inGlyph;

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec4 vColor;
layout(location = 2) flat out uvec2 vGlyph;

void main() {
  vUV = inUV;
  vColor = vec4(pow(inColor.rgb, vec3(G.misc.w > 0.5 ? 1.0 : 2.2)), inColor.a);
  vGlyph = inGlyph;
  gl_Position = vec4(inPos * G.screen.zw * 2.0 - 1.0, 0.0, 1.0);
}
