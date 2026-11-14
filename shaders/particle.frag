layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vColor;
layout(location = 2) flat in float vKind;
layout(location = 0) out vec4 outColor;

void main() {
  float r = length(vUV);
  float a;
  if (vKind < 0.5) {        // soft glow
    a = exp(-r * r * 4.5) * smoothstep(1.0, 0.6, r);
  } else if (vKind < 1.5) { // shock ring
    a = smoothstep(0.16, 0.0, abs(r - 0.82)) * 0.8;
  } else if (vKind < 2.5) { // streak / bolt
    float across = 1.0 - abs(vUV.x);
    a = across * across * smoothstep(1.0, 0.55, abs(vUV.y));
    a += pow(across, 6.0) * 0.8 * smoothstep(1.0, 0.7, abs(vUV.y));
  } else {                  // smoke puff
    float n = noise3(vec3(vUV * 2.5, vColor.r * 40.0 + vColor.a * 13.0));
    a = smoothstep(1.0, 0.15, r) * (0.55 + 0.45 * n);
  }
  outColor = vec4(vColor.rgb * a, vColor.a * a);
}
