layout(location = 0) in vec4 iPosSize;  // xyz, size
layout(location = 1) in vec4 iColor;    // premultiplied rgb, a = opacity (0 = additive)
layout(location = 2) in vec4 iAxisKind; // xyz streak axis, w kind

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec4 vColor;
layout(location = 2) flat out float vKind;

void main() {
  const vec2 corners[6] = vec2[6](vec2(-1, -1), vec2(1, -1), vec2(1, 1), vec2(-1, -1), vec2(1, 1), vec2(-1, 1));
  vec2 c = corners[gl_VertexIndex];
  vec3 pos = iPosSize.xyz;
  float size = iPosSize.w;
  vec3 world;
  float alen = length(iAxisKind.xyz);
  if (alen > 1e-4) { // streak: stretched along the axis, facing the camera
    vec3 ax = iAxisKind.xyz / alen;
    vec3 toCam = normalize(G.camPos.xyz - pos);
    vec3 side = cross(ax, toCam);
    float sl = length(side);
    side = sl > 1e-4 ? side / sl : G.camRight.xyz;
    world = pos + side * c.x * size + ax * c.y * (alen * 0.5 + size);
  } else {
    world = pos + (G.camRight.xyz * c.x + G.camUp.xyz * c.y) * size;
  }
  float dist = length(G.camPos.xyz - world);
  vUV = c;
  vColor = iColor * exp(-dist * G.fog.w);
  vKind = iAxisKind.w;
  gl_Position = G.viewProj * vec4(world, 1.0);
}
