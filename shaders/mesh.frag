// Surface shading for every solid object. Lit materials share one physically based model:
// GGX specular with Schlick Fresnel for two lights (the star and a coloured fill), plus a
// reflection of the surroundings. The surroundings are evaluated analytically in envColor():
// nebula glow, the star's halo and the lit discs of nearby planets in space, or the light
// bars, deck and open bay door inside the hangar.
layout(location = 0) in vec3 vWorld;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec3 vLocal;
layout(location = 3) in vec3 vObjN;
layout(location = 4) in vec4 vColor;
layout(location = 5) in vec4 vParams;
layout(location = 6) in vec3 vObj;
layout(location = 7) flat in vec3 vAxX;
layout(location = 8) flat in vec3 vAxY;
layout(location = 9) flat in vec3 vAxZ;
layout(location = 0) out vec4 outColor;

const float PI = 3.14159265;

layout(set = 0, binding = 1) uniform sampler2DArray texA; // rgb colour multiplier / 1.3, a roughness
layout(set = 0, binding = 2) uniform sampler2DArray texB; // rg normal, b occlusion, a wear

struct Surf { vec3 albedo; float rough; vec3 n; float ao; float wear; };
// Triplanar mapping: the texture is projected along the three object axes and blended by the
// surface normal, so no mesh needs texture coordinates and the pattern sticks to the object.
// `n` is the object-space normal; the returned normal is object-space too.
Surf triplanar(vec3 p, vec3 n, float layer, float bump) {
  vec3 w = pow(abs(n), vec3(4.0));
  w /= w.x + w.y + w.z;
  vec4 a = texture(texA, vec3(p.zy, layer)) * w.x + texture(texA, vec3(p.xz, layer)) * w.y + texture(texA, vec3(p.xy, layer)) * w.z;
  vec4 bx = texture(texB, vec3(p.zy, layer)), by = texture(texB, vec3(p.xz, layer)), bz = texture(texB, vec3(p.xy, layer));
  vec4 b = bx * w.x + by * w.y + bz * w.z;
  vec2 tx = (bx.rg * 2.0 - 1.0) * bump, ty = (by.rg * 2.0 - 1.0) * bump, tz = (bz.rg * 2.0 - 1.0) * bump;
  Surf s;
  s.albedo = a.rgb * 1.3;
  s.rough = a.a;
  s.ao = b.b;
  s.wear = b.a;
  s.n = normalize(vec3(n.x, tx.y + n.y, tx.x + n.z) * w.x + vec3(ty.x + n.x, n.y, ty.y + n.z) * w.y + vec3(tz.x + n.x, tz.y + n.y, n.z) * w.z);
  return s;
}
#define TO_WORLD(v) normalize(vAxX * (v).x + vAxY * (v).y + vAxZ * (v).z)

vec3 envColor(vec3 d, float rough) {
  vec3 L = normalize(G.sunDir.xyz);
  if (G.ambient.w > 0.5) { // ---- station hangar
    vec3 col;
    if (d.y > 0.0) { // ceiling with rows of light bars
      vec2 p = d.xz / max(d.y, 0.04);
      float stripe = abs(fract(p.x * 0.5) - 0.5);
      float bar = smoothstep(0.09 + 0.3 * rough, 0.03, stripe) * (1.0 - 0.55 * rough);
      float seg = smoothstep(0.47, 0.40, abs(fract(p.y * 0.22) - 0.5));
      float fade = 1.0 / (1.0 + 0.05 * dot(p, p));
      col = vec3(0.07, 0.08, 0.095) + vec3(1.0, 0.96, 0.9) * 2.4 * bar * seg * fade;
    } else { // deck
      col = vec3(0.11, 0.115, 0.125) * (0.5 + 0.8 * -d.y) + vec3(0.25, 0.14, 0.03) * 0.25 * smoothstep(0.2, 0.9, -d.y);
    }
    float horizon = pow(1.0 - abs(d.y), 5.0);
    float az = atan(d.z, d.x);
    float strips = smoothstep(0.10 + 0.25 * rough, 0.02, abs(fract(az * 9.0 / 6.2832) - 0.5) - 0.03);
    col += vec3(0.2, 0.6, 0.9) * 0.9 * horizon * strips * (1.0 - 0.5 * rough);
    col += vec3(0.16, 0.18, 0.21) * horizon;
    // the open bay door (towards -Z) lets in cold light from space
    float door = smoothstep(0.55, 0.92, -d.z) * smoothstep(-0.25, 0.05, d.y) * smoothstep(0.75, 0.3, d.y);
    col += vec3(0.2, 0.5, 1.0) * 0.9 * door;
    return col;
  }
  // ---- open space
  float n = fbm3(d * G.nebParams.y * 0.7 + G.camFwd.w * 13.0);
  vec3 col = mix(G.nebA.rgb, G.nebB.rgb, smoothstep(0.3, 0.7, n)) * (0.2 + 1.3 * n) * (0.5 + G.nebParams.x);
  col += G.fog.rgb * 2.0 + G.ambient.rgb * 0.8;
  float cs = max(dot(d, L), 0.0);
  col += G.sunColor.rgb * (pow(cs, 40.0) * 1.2 + pow(cs, 4.0) * 0.12); // halo; the disc itself is the analytic highlight
  int count = int(G.misc.x + 0.5);
  for (int i = 0; i < count; ++i) {
    vec3 pd = G.planetPos[i].xyz - vWorld;
    float R = G.planetPos[i].w;
    float b = dot(pd, d);
    float h = R * R - (dot(pd, pd) - b * b);
    if (b > 0.0 && h > 0.0) {
      vec3 pn = (d * (b - sqrt(h)) - pd) / R;
      float lit = smoothstep(-0.1, 0.45, dot(pn, L));
      vec3 pc = mix(G.planetColA[i].rgb, G.planetColB[i].rgb, 0.5) * G.sunColor.rgb * lit * 1.1;
      pc += G.planetColB[i].rgb * step(2.5, G.planetColA[i].w) * step(G.planetColA[i].w, 3.5) * 0.5; // lava glow
      float edge = smoothstep(0.0, R * R * (0.02 + 0.3 * rough), h);
      col = mix(col, pc, edge);
    }
  }
  return col;
}

// 1 while a pattern of the given spatial frequency (cycles per metre) is resolvable, 0 once it is finer than a pixel
float resolvable(float freq) { return clamp(0.35 / (length(fwidth(vLocal)) * freq + 1e-5), 0.0, 1.0); }

float ggxD(float ndh, float a) {
  float a2 = a * a;
  float d = ndh * ndh * (a2 - 1.0) + 1.0;
  return a2 / (PI * d * d);
}
float smithV(float ndl, float ndv, float a) {
  float k = a * 0.5;
  return 0.25 / ((ndl * (1.0 - k) + k) * (ndv * (1.0 - k) + k));
}
vec3 shadeLight(vec3 N, vec3 V, vec3 Ld, vec3 radiance, vec3 diffuse, vec3 F0, float rough) {
  float ndl = dot(N, Ld);
  if (ndl <= 0.0) return vec3(0.0);
  vec3 H = normalize(Ld + V);
  float ndv = max(dot(N, V), 1e-3), ndh = max(dot(N, H), 0.0), vdh = max(dot(V, H), 0.0);
  float a = max(rough * rough, 0.004);
  vec3 F = F0 + (1.0 - F0) * pow(1.0 - vdh, 5.0);
  vec3 spec = F * min(ggxD(ndh, a) * smithV(ndl, ndv, a), 40.0);
  return (diffuse + spec) * radiance * ndl;
}

void main() {
  int mat = int(vParams.x + 0.5);
  float dmg = vParams.y;
  float seed = vParams.z;
  float time = G.camPos.w;
  vec3 N = normalize(vNormal);
  if (!gl_FrontFacing) N = -N;
  vec3 toCam = G.camPos.xyz - vWorld;
  float dist = length(toCam);
  vec3 V = toCam / max(dist, 1e-4);
  vec3 L = normalize(G.sunDir.xyz);

  // ---- additive materials
  if (mat == 6) { // shield bubble
    float fr = pow(1.0 - abs(dot(N, V)), 2.2);
    vec3 p = vObj * 22.0;
    float pat = 0.55 + 0.45 * sin(p.x + time * 5.0) * sin(p.y - time * 3.0) * sin(p.z);
    outColor = vec4(vColor.rgb * (fr * fr * 0.55 + 0.008) * pat * vParams.w, 1.0);
    return;
  }
  if (mat == 7) { // ---- ion-plasma exhaust. Unit cone: base (+Z) sits on the nozzle, tip (-Z) is the tail.
    float a = 0.5 - vObj.z;                       // 0 at the nozzle, 1 at the tail
    float ang = atan(vObj.y, vObj.x);
    float along = abs(dot(V, normalize(vAxZ)));   // 1 when looking straight down the jet
    float sinv = max(sqrt(1.0 - along * along), 0.12);
    float facing = clamp(abs(dot(N, V)) / sinv, 0.0, 1.0); // 1 on the jet's centre line from any angle
    float depth = mix(1.0, 2.0, along * along);   // looking down the jet you see through its whole length
    float throttle = vParams.w;
    bool sheath = dmg > 0.5;                      // the outer, cooler envelope is drawn as a second cone
    // turbulence carried down the jet
    float flow = noise3(vec3(ang * 2.5 + seed * 7.0, a * 11.0 - time * 16.0, seed * 3.0)) * 0.6
               + noise3(vec3(ang * 6.0, a * 30.0 - time * 34.0, seed * 5.0)) * 0.4;
    float flick = 0.9 + 0.1 * sin(time * 63.0 + seed * 40.0) * sin(time * 17.0 + seed * 11.0);
    vec3 hot = vec3(1.0, 0.98, 0.95);
    vec3 ion = vColor.rgb;
    vec3 tail = vColor.rgb * vec3(0.6, 0.35, 1.25) + vec3(0.08, 0.0, 0.16); // recombining plasma shifts to violet
    vec3 c;
    if (sheath) {
      float body = exp(-a * 3.2) * (1.0 - a);
      float inten = body * pow(facing, 0.6) * (0.35 + 0.9 * flow) * 0.5;
      c = mix(ion, tail, smoothstep(0.1, 0.8, a)) * inten;
    } else {
      float core = pow(facing, 2.2);
      float body = exp(-a * 2.6);
      // standing shock diamonds: bright knots where the jet re-compresses, fading downstream
      float knots = pow(0.5 + 0.5 * cos(a * 6.2832 * 5.5 - 0.5), 4.0) * exp(-a * 4.2) * smoothstep(0.0, 0.05, a);
      float heat = clamp(core * body * 1.5 + knots * core * 1.6, 0.0, 1.0);
      c = mix(ion, hot, heat);
      c = mix(c, tail, smoothstep(0.3, 0.95, a));
      float inten = body * (0.22 + 0.9 * core) + knots * core * 1.4;
      inten *= 0.72 + 0.4 * flow;
      c *= inten * 1.25;
    }
    c *= smoothstep(1.0, 0.8, a) * flick * throttle * depth;
    outColor = vec4(c, 1.0);
    return;
  }
  if (mat == 8) { // atmosphere-retention field across the bay door
    vec2 q = vObj.xy;
    float edge = pow(max(abs(q.x), abs(q.y)) * 2.0, 6.0);
    float scan = 0.5 + 0.5 * sin(q.y * 260.0 - time * 2.0);
    float sweep = pow(0.5 + 0.5 * sin(q.y * 9.0 + time * 0.9), 14.0);
    float hex = 0.6 + 0.4 * noise3(vec3(q * 34.0, time * 0.4));
    outColor = vec4(vColor.rgb * (0.006 + 0.012 * scan * hex + 0.22 * edge + 0.05 * sweep) * vParams.w, 1.0);
    return;
  }

  vec3 albedo = vColor.rgb;
  vec3 emissive = vec3(0.0);
  float metal = 0.0, rough = 0.6, F0d = 0.04, envOcc = 1.0;

  // planar texture coordinates and the matching tangent frame
  vec3 an = abs(normalize(vObjN));
  vec3 lp = vLocal;
  vec2 tuv; vec3 U, W;
  if (an.x > an.y && an.x > an.z) { tuv = lp.yz; U = vAxY; W = vAxZ; }
  else if (an.y > an.z) { tuv = lp.xz; U = vAxX; W = vAxZ; }
  else { tuv = lp.xy; U = vAxX; W = vAxY; }
  float lod = clamp(1.0 - dist / 260.0, 0.0, 1.0);
  float fine = clamp(1.0 - dist / 55.0, 0.0, 1.0); // the finest detail fades out before it can shimmer

  vec3 nObj = normalize(vec3(dot(N, vAxX), dot(N, vAxY), dot(N, vAxZ)));

  if (mat == 0) { // ---- hull: textured plating (panels, seams, rivets, scratches) over steel, with painted areas
    Surf s = triplanar(vLocal / 6.0 + floor(seed * 8.0) * 0.37, nObj, 0.0, 1.0);
    // paint: saturated colours are a coat over the metal, gone wherever the wear map says the surface is worn through
    float mx = max(albedo.r, max(albedo.g, albedo.b)), mn = min(albedo.r, min(albedo.g, albedo.b));
    float paint = smoothstep(0.22, 0.55, (mx - mn) / max(mx, 1e-3));
    vec3 ao = abs(vObj);
    float e2 = max(min(ao.x, ao.y), max(min(ao.y, ao.z), min(ao.x, ao.z)));
    float edge = smoothstep(0.42, 0.5, e2) * lod; // exposed edges polish themselves bright
    float chips = smoothstep(0.35, 0.6, s.wear + edge * 0.4);
    paint *= 1.0 - chips;
    vec3 steel = mix(vec3(dot(albedo, vec3(0.333))), albedo, 0.35) * 0.9 + 0.12;
    vec3 base = mix(steel, albedo, paint) * s.albedo;
    base = mix(base, base * 1.4 + 0.06, edge * 0.5);
    metal = (1.0 - 0.85 * paint) * vParams.w;
    rough = s.rough + 0.14 * paint - 0.1 * edge;
    albedo = base;
    N = TO_WORLD(s.n);
    envOcc = s.ao;
  } else if (mat == 10) { // ---- station structure: plated, bolted, scuffed
    Surf s = triplanar(vLocal / 4.0 + floor(seed * 16.0) * 0.21, nObj, 3.0, 1.0);
    albedo *= s.albedo;
    albedo *= 0.75 + 0.25 * smoothstep(0.0, 2.5, vWorld.y); // dirtier near the deck
    metal = vParams.w;
    rough = s.rough + 0.2 - 0.35 * metal;
    N = TO_WORLD(s.n);
    envOcc = s.ao;
  } else if (mat == 9) { // ---- corrugated, painted cargo container
    bool zFace = !(an.x > an.y && an.x > an.z) && !(an.y > an.z);
    float ribs = sin((zFace ? tuv.x : tuv.y) * 9.0);
    N = normalize(N + (zFace ? U : W) * ribs * 0.45 * lod);
    float wear = fbm3(vLocal * 1.3 + seed * 7.0);
    float chips = smoothstep(0.6, 0.75, fbm3(vLocal * 4.0 + seed * 3.0));
    albedo *= 0.75 + 0.35 * wear;
    albedo = mix(albedo, vec3(0.30, 0.16, 0.07), chips * 0.7);
    albedo *= 0.8 + 0.2 * ribs;
    float band = smoothstep(0.03, 0.0, abs(vObj.y - 0.18) - 0.07); // id stripe
    albedo = mix(albedo, vec3(0.85), band * 0.6 * (1.0 - chips));
    Surf s = triplanar(vLocal / 4.0 + seed * 3.0, nObj, 3.0, 0.0);
    albedo *= s.albedo;
    metal = 0.15 + 0.6 * chips;
    rough = 0.3 + 0.2 * wear + 0.4 * s.rough;
  } else if (mat == 1) { // ---- rock: the texture at two scales, so both mountains and pebbles have detail
    vec3 q = vObj * 2.3 + seed * 31.0;
    Surf a = triplanar(q, nObj, 1.0, 1.0);
    Surf b = triplanar(q * 5.3 + 0.37, nObj, 1.0, 0.8);
    float tone = fbm3(vObj * 2.0 + seed * 9.0);
    albedo *= a.albedo * (0.5 + 0.75 * dot(b.albedo, vec3(0.333))) * (0.6 + 0.7 * tone);
    albedo = mix(albedo, albedo * vec3(1.18, 0.96, 0.8), smoothstep(0.4, 0.7, tone));
    N = TO_WORLD(normalize(a.n + b.n - nObj));
    float crack = (1.0 - smoothstep(0.0, 0.03 + 0.03 * dmg, abs(fbm3(vObj * 4.5 + seed * 17.0) - 0.5))) * clamp(dmg * 2.2, 0.0, 1.0);
    albedo *= 1.0 - 0.85 * crack;
    rough = a.rough; F0d = 0.03; envOcc = 0.4 * a.ao * b.ao * (1.0 - crack);
  } else if (mat == 2) { // ---- canopy glass
    albedo = vColor.rgb * 0.12;
    rough = 0.06; F0d = 0.09;
  } else if (mat == 4) { // ---- ice: clear blue where bare, white where frosted, split by fracture lines
    Surf s = triplanar(vObj * 2.6 + seed * 31.0, nObj, 2.0, 1.0);
    albedo = mix(albedo * 0.5, vec3(0.92, 0.97, 1.0), s.wear) * s.albedo;
    N = TO_WORLD(s.n);
    float crack = (1.0 - smoothstep(0.0, 0.03 + 0.03 * dmg, abs(fbm3(vObj * 4.5 + seed * 17.0) - 0.5))) * clamp(dmg * 2.2, 0.0, 1.0);
    albedo *= 1.0 - 0.6 * crack;
    rough = s.rough; F0d = 0.06; envOcc = s.ao;
  } else if (mat == 5) { // ---- hangar deck plating
    vec2 w = vWorld.xz;
    vec2 uv = w * 0.25;
    vec2 g = abs(fract(uv) - 0.5);
    float line = smoothstep(0.48, 0.497, max(g.x, g.y)) * clamp(1.0 - dist / 120.0, 0.0, 1.0);
    float pv = hash21(floor(uv));
    float wear = fbm3(vec3(w * 0.35, 3.0));
    albedo *= 0.75 + 0.3 * pv;
    float r = length(w);
    // hazard ring around the service pad
    float ring = smoothstep(0.0, 0.15, r - 15.2) * smoothstep(0.0, 0.15, 16.6 - r);
    float stripes = step(0.5, fract(atan(w.y, w.x) * 28.0 / 6.2832));
    vec3 hazard = mix(vec3(0.03), vec3(0.95, 0.68, 0.05), stripes);
    float worn = smoothstep(0.35, 0.6, wear);
    albedo = mix(albedo, hazard * (0.7 + 0.3 * worn), ring * 0.92);
    // taxi lanes
    float lane = smoothstep(0.22, 0.14, abs(abs(w.x) - 27.0)) * step(0.25, fract(w.y * 0.08));
    albedo = mix(albedo, vec3(0.8), lane * 0.7 * worn);
    float lamp = smoothstep(0.32, 0.2, length(vec2(abs(w.x) - 29.0, (fract(w.y * 0.125) - 0.5) * 8.0)));
    emissive += vec3(0.1, 0.7, 1.0) * lamp * 2.5;
    // soft contact shadow under the ship
    albedo *= mix(0.3, 1.0, smoothstep(0.25, 1.15, length(w * vec2(0.085, 0.07))));
    vec4 fa = texture(texA, vec3(w * 0.25, 3.0)), fb = texture(texB, vec3(w * 0.25, 3.0));
    albedo *= fa.rgb * 1.3;
    N = normalize(N + vec3(fb.r * 2.0 - 1.0, 0.0, fb.g * 2.0 - 1.0) * step(0.9, N.y));
    metal = 0.45;
    rough = fa.a + 0.08 + 0.2 * wear;
    envOcc = mix(0.35, 1.0, smoothstep(0.3, 1.3, length(w * vec2(0.085, 0.07)))) * fb.b;
  }

  if (mat == 3) { // self-lit
    vec3 c = albedo * (0.35 + vColor.a);
    c = mix(G.fog.rgb, c, exp(-dist * G.fog.w));
    outColor = vec4(finalize(tonemap(c)), 1.0);
    return;
  }

  // ---- battle damage: scorch marks eat across the surface as damage rises
  if (dmg > 0.01 && mat != 1 && mat != 4) {
    float bres = resolvable(6.4);
    float n = mix(0.5, fbm3(vLocal * 1.6 + seed * 13.0), bres);
    float burn = smoothstep(n - 0.12, n + 0.06, dmg * 1.15);
    albedo = mix(albedo, vec3(0.025, 0.022, 0.02), burn * 0.92);
    metal *= 1.0 - 0.9 * burn;
    rough = mix(rough, 0.9, burn);
    envOcc *= 1.0 - 0.8 * burn;
    float edge = smoothstep(0.10, 0.0, abs(n - dmg * 1.15 + 0.03));
    float flick = 0.6 + 0.4 * sin(time * 6.0 + seed * 50.0 + n * 30.0);
    emissive += vec3(1.0, 0.33, 0.05) * edge * smoothstep(0.3, 0.9, dmg) * flick * 1.6 * bres;
  }

  rough = clamp(rough, 0.05, 1.0);
  vec3 F0 = mix(vec3(F0d), albedo, metal);
  vec3 diffuse = albedo * (1.0 - metal);
  float ndv = max(dot(N, V), 1e-3);

  // key light: the local star (or the bay floodlights)
  vec3 sun = G.sunColor.rgb * G.sunColor.w * 0.5;
  vec3 col = shadeLight(N, V, L, sun, diffuse, F0, rough);
  // fill light: nebula glow from the far side
  vec3 fillDir = normalize(-L + vec3(0.0, 0.45, 0.0) + G.camRight.xyz * 0.35);
  vec3 fill = (G.nebA.rgb + G.nebB.rgb) * 0.55 + G.ambient.rgb * 1.5;
  col += shadeLight(N, V, fillDir, fill, diffuse, F0, min(rough + 0.1, 1.0));

  // surroundings: diffuse ambient plus the mirrored environment
  vec3 amb = G.ambient.rgb * (0.65 + 0.35 * N.y);
  col += diffuse * amb;
  vec3 Rv = reflect(-V, N);
  vec4 r4 = rough * vec4(-1.0, -0.0275, -0.572, 0.022) + vec4(1.0, 0.0425, 1.04, -0.04);
  float a004 = min(r4.x * r4.x, exp2(-9.28 * ndv)) * r4.x + r4.y;
  vec2 AB = vec2(-1.04, 1.04) * a004 + r4.zw;
  col += envColor(Rv, rough) * (F0 * AB.x + AB.y) * envOcc;

  col += emissive + albedo * vColor.a;
  col = mix(G.fog.rgb, col, exp(-dist * G.fog.w));
  outColor = vec4(finalize(tonemap(col)), 1.0);
}
