// Fully procedural backdrop: star field, nebula clouds, the local star, and up to four
// ray-traced planets with atmospheres and rings. Nothing here uses textures.
layout(location = 0) in vec2 vNdc;
layout(location = 0) out vec4 outColor;

vec3 starLayer(vec3 d, float scale, float thresh) {
  vec3 p = d * scale;
  vec3 id = floor(p);
  vec3 f = fract(p) - 0.5;
  float h = hash31(id + G.camFwd.w);
  if (h < thresh) return vec3(0.0);
  vec3 off = (vec3(hash31(id + 11.1), hash31(id + 23.7), hash31(id + 37.3)) - 0.5) * 0.5;
  float b = (h - thresh) / (1.0 - thresh);
  float dist = length(f - off);
  float s = smoothstep(0.10 + 0.10 * b, 0.0, dist);
  vec3 tint = mix(vec3(1.0, 0.78, 0.55), vec3(0.65, 0.8, 1.0), hash31(id + 5.5));
  return tint * s * s * (0.35 + 2.5 * b * b);
}

vec3 nebula(vec3 d) {
  vec3 p = d * G.nebParams.y + vec3(G.camFwd.w * 13.0);
  float n1 = fbm(p);
  float n2 = fbm(p * 2.1 + n1 * 1.7 + 7.0);
  float dens = smoothstep(0.62 - 0.22 * G.nebParams.x, 1.0, n1 * 0.65 + n2 * 0.6);
  vec3 col = mix(G.nebA.rgb, G.nebB.rgb, smoothstep(0.3, 0.7, n2));
  col = mix(col, G.nebC.rgb, smoothstep(0.5, 0.85, fbm3(p * 0.6 + 31.0)));
  float dust = smoothstep(0.45, 0.75, fbm3(p * 3.1 + 53.0));
  return col * dens * (1.0 - 0.6 * dust) * G.nebParams.x * 1.6;
}

vec3 planetSurface(int i, vec3 n, vec3 L, out vec3 emissive) {
  int type = int(G.planetColA[i].w + 0.5);
  float seed = G.planetParams[i].w;
  vec3 A = G.planetColA[i].rgb, B = G.planetColB[i].rgb;
  vec3 q = n * 3.0 + seed;
  vec3 col;
  emissive = vec3(0.0);
  if (type == 1) { // gas giant
    float turb = fbm(q * vec3(1.0, 4.0, 1.0)) * 0.35;
    float band = sin((n.y + turb) * 22.0 + seed) * 0.5 + 0.5;
    float band2 = sin((n.y + turb * 0.6) * 51.0) * 0.5 + 0.5;
    col = mix(A, B, band) * (0.8 + 0.25 * band2);
    float storm = smoothstep(0.75, 0.85, fbm3(q * 2.0 + 9.0));
    col = mix(col, B * 1.3, storm * 0.5);
  } else if (type == 2) { // terran
    float h = fbm(q * 1.4);
    float land = smoothstep(0.5, 0.54, h);
    vec3 ocean = A * (0.6 + 0.4 * smoothstep(0.3, 0.5, h));
    vec3 ground = mix(B, B * vec3(0.75, 0.6, 0.45), smoothstep(0.58, 0.75, h));
    col = mix(ocean, ground, land);
    col = mix(col, vec3(0.95), smoothstep(0.78, 0.9, abs(n.y) + 0.15 * h));
    float clouds = smoothstep(0.52, 0.72, fbm(q * 2.6 + 40.0 + G.camPos.w * 0.002));
    col = mix(col, vec3(1.0), clouds * 0.8);
  } else if (type == 3) { // lava
    float h = fbm(q * 2.2);
    float crack = 1.0 - smoothstep(0.0, 0.06, abs(h - 0.5));
    col = A * (0.35 + 0.8 * h);
    emissive = B * crack * 2.5 + B * smoothstep(0.62, 0.8, h) * 0.6;
  } else if (type == 4) { // ice
    float h = fbm(q * 2.0);
    float crack = 1.0 - smoothstep(0.0, 0.03, abs(fbm3(q * 4.0 + 5.0) - 0.5));
    col = mix(A, B, h);
    col = mix(col, A * 0.45, crack * 0.6);
  } else { // rocky / moon
    float h = fbm(q * 2.5);
    float cr = smoothstep(0.55, 0.7, fbm3(q * 7.0 + 3.0));
    col = mix(A, B, h) * (1.0 - 0.35 * cr);
  }
  return col;
}

void main() {
  vec3 d = normalize(G.camFwd.xyz + vNdc.x * G.camRight.w * G.camRight.xyz - vNdc.y * G.camUp.w * G.camUp.xyz);

  vec3 L = normalize(G.skySun.xyz);
  vec3 col = starLayer(d, 70.0, 0.90) + starLayer(d, 130.0, 0.93) * 0.7;
  col += nebula(d);

  // local star
  float cs = dot(d, L);
  float ang = acos(clamp(cs, -1.0, 1.0));
  float r = G.sunDir.w;
  float disc = smoothstep(r, r * 0.86, ang);
  float corona = exp(-ang / (r * 1.6)) * 1.2 + exp(-ang / (r * 6.0)) * 0.35 + pow(max(cs, 0.0), 12.0) * 0.06;
  float rays = 0.5 + 0.5 * noise3(vec3(atan(d.y - L.y, d.x - L.x) * 9.0, G.camFwd.w, 0.0));
  col += G.sunColor.rgb * (disc * 14.0 + corona * (0.8 + 0.4 * rays));

  // planets
  int count = int(G.misc.x + 0.5);
  vec3 ro = G.camPos.xyz;
  for (int i = 0; i < count; ++i) {
    vec3 C = G.planetPos[i].xyz;
    float R = G.planetPos[i].w;
    vec3 oc = ro - C;
    float b = dot(oc, d);
    float c = dot(oc, oc) - R * R;
    float h = b * b - c;
    float tPlanet = 1e30;
    float dc = sqrt(max(dot(oc, oc) - b * b, 0.0)); // closest approach of the view ray
    float atmo = G.planetColB[i].w;
    if (b < 0.0) {
      // atmosphere halo outside the limb
      vec3 pc = normalize(ro + d * (-b) - C);
      float lit = smoothstep(-0.35, 0.5, dot(pc, L));
      float halo = exp(-max(dc - R, 0.0) / (R * 0.035)) * atmo * lit;
      col += mix(G.planetColA[i].rgb, vec3(0.6, 0.8, 1.0), 0.6) * halo * 0.7 * step(R, dc);
      if (h > 0.0) {
        tPlanet = -b - sqrt(h);
        vec3 P = ro + d * tPlanet;
        vec3 n = normalize(P - C);
        vec3 em;
        vec3 alb = planetSurface(i, n, L, em);
        float ndl = dot(n, L);
        float light = smoothstep(-0.08, 0.35, ndl);
        vec3 pc2 = alb * (G.sunColor.rgb * light * 1.15 + G.nebA.rgb * 0.03 + 0.008) + em * (1.0 - light * 0.7);
        float rim = pow(1.0 - max(dot(n, -d), 0.0), 3.0);
        pc2 += mix(G.planetColA[i].rgb, vec3(0.55, 0.75, 1.0), 0.6) * rim * atmo * light * 1.2;
        float edge = smoothstep(R, R * 0.997, dc);
        col = mix(col, pc2, edge);
      }
    }
    // rings
    if (G.planetParams[i].x > 0.5) {
      vec3 rn = normalize(vec3(G.planetParams[i].y, 1.0, G.planetParams[i].z));
      float dn = dot(d, rn);
      if (abs(dn) > 1e-5) {
        float t = dot(C - ro, rn) / dn;
        if (t > 0.0 && t < tPlanet) {
          vec3 Q = ro + d * t;
          float rho = length(Q - C) / R;
          if (rho > 1.35 && rho < 2.45) {
            float bands = 0.35 + 0.65 * noise3(vec3(rho * 28.0, G.planetParams[i].w, 0.0));
            bands *= 0.6 + 0.4 * noise3(vec3(rho * 90.0, 3.0, G.planetParams[i].w));
            float gap = smoothstep(0.0, 0.03, abs(rho - 1.95));
            float a = bands * gap * smoothstep(1.35, 1.45, rho) * smoothstep(2.45, 2.3, rho) * 0.85;
            // planet shadow on the ring
            vec3 oq = Q - C;
            float b2 = dot(oq, L);
            float h2 = b2 * b2 - (dot(oq, oq) - R * R);
            float shadow = (h2 > 0.0 && b2 < 0.0) ? 0.08 : 1.0;
            vec3 rc = mix(G.planetColB[i].rgb, vec3(0.8, 0.75, 0.7), 0.5) * G.sunColor.rgb * shadow * 0.9;
            col = mix(col, rc, a);
          }
        }
      }
    }
  }

  // in-sector fog swallows the distant backdrop
  float skyFog = G.ambient.w > 0.5 ? 0.0 : 1.0 - exp(-G.fog.w * 3000.0);
  float cl = fbm(d * 2.2 + G.camFwd.w);
  float cloud = 0.15 + 2.2 * cl * cl;
  skyFog *= 0.75 + 0.25 * smoothstep(0.3, 0.7, cl);
  vec3 fogCol = G.fog.rgb * cloud + G.sunColor.rgb * corona * 0.12;
  col = mix(col, fogCol, skyFog);

  outColor = vec4(finalize(tonemap(col)), 1.0);
}
