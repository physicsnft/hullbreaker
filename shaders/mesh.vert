layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
// per instance
layout(location = 2) in vec4 iM0;
layout(location = 3) in vec4 iM1;
layout(location = 4) in vec4 iM2;
layout(location = 5) in vec4 iM3;
layout(location = 6) in vec4 iColor;  // rgb, a = emissive strength
layout(location = 7) in vec4 iParams; // x material, y damage, z seed, w intensity / metalness

layout(location = 0) out vec3 vWorld;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec3 vLocal;  // object space in metres
layout(location = 3) out vec3 vObjN;
layout(location = 4) out vec4 vColor;
layout(location = 5) out vec4 vParams;
layout(location = 6) out vec3 vObj;    // unit object space
layout(location = 7) flat out vec3 vAxX; // world-space directions of the object's axes,
layout(location = 8) flat out vec3 vAxY; // used as the tangent frame for surface detail
layout(location = 9) flat out vec3 vAxZ;

void main() {
  mat4 model = mat4(iM0, iM1, iM2, iM3);
  vec3 scale = vec3(length(iM0.xyz), length(iM1.xyz), length(iM2.xyz));
  vec4 w = model * vec4(inPos, 1.0);
  vWorld = w.xyz;
  vNormal = mat3(model) * (inNormal / (scale * scale));
  vLocal = inPos * scale;
  vObjN = inNormal;
  vObj = inPos;
  vColor = iColor;
  vParams = iParams;
  vAxX = iM0.xyz / scale.x;
  vAxY = iM1.xyz / scale.y;
  vAxZ = iM2.xyz / scale.z;
  gl_Position = G.viewProj * w;
}
