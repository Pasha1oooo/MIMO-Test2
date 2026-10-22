#pragma once

static const char* CHUNK_VS = R"(#version 430 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec2 aUV;
layout(location=2) in vec2 aLight;
layout(location=3) in vec3 aN;
uniform mat4 uVP;
uniform vec3 uOrigin;
uniform float uTime;
uniform int uWater;
out vec2 vUV;
out vec2 vLight;
out vec3 vN;
out vec3 vWorld;
out float vDist;
void main(){
  vec3 wp = aPos + uOrigin;
  if (uWater == 1) wp.y += sin(uTime*1.7 + wp.x*0.6 + wp.z*0.45)*0.045;
  vUV = aUV; vLight = aLight; vN = aN; vWorld = wp;
  vec4 p = uVP * vec4(wp, 1.0);
  vDist = length(p.xyz)/max(p.w, 0.0001);
  vDist = p.w;
  gl_Position = p;
}
)";

static const char* CHUNK_FS = R"(#version 430 core
in vec2 vUV;
in vec2 vLight;
in vec3 vN;
in vec3 vWorld;
in float vDist;
layout(location=0) out vec4 frag;
layout(location=1) out vec4 gN;
uniform sampler2D uAtlas;
uniform vec3 uSunDir, uSunColor, uAmbSky, uAmbGround, uFogColor;
uniform vec3 uCamPos;
uniform float uFogDensity;
uniform int uWater;
void main(){
  vec4 tex = texture(uAtlas, vUV);
  if (uWater == 0 && tex.a < 0.5) discard;
  float sky = vLight.x, blk = vLight.y;
  vec3 n = normalize(vN);
  float dif = max(dot(n, uSunDir), 0.0);
  vec3 light = uAmbSky * sky + uAmbGround * (1.0 - sky) * 0.25;
  light += uSunColor * sky * (0.28 + 0.72 * dif);
  light += vec3(1.0, 0.82, 0.55) * blk * 0.95;
  vec3 col = tex.rgb * light;
  float d = distance(vWorld, uCamPos);
  float fog = 1.0 - exp(-d * d * uFogDensity);
  col = mix(col, uFogColor, clamp(fog, 0.0, 1.0));
  float alpha = (uWater == 1) ? 0.72 : 1.0;
  frag = vec4(col, alpha);
  gN = vec4(n * 0.5 + 0.5, 1.0);
}
)";

static const char* SKY_VS = R"(#version 430 core
out vec2 vNdc;
void main(){
  vec2 p = vec2((gl_VertexID == 1) ? 3.0 : -1.0, (gl_VertexID == 2) ? 3.0 : -1.0);
  vNdc = p;
  gl_Position = vec4(p, 0.999999, 1.0);
}
)";

static const char* SKY_FS = R"(#version 430 core
in vec2 vNdc;
out vec4 frag;
uniform mat4 uInvVP;
uniform vec3 uCamPos;
uniform vec3 uSunDir;
uniform vec3 uTop, uHorizon, uSunCol;
uniform float uNight;
uniform float uTime;
float hash13(vec3 p){
  p = fract(p * 0.1031);
  p += dot(p, p.yzx + 33.33);
  return fract((p.x + p.y) * p.z);
}
float hash12(vec2 p){
  vec3 p3 = fract(vec3(p.xyx) * 0.1031);
  p3 += dot(p3, p3.yzx + 33.33);
  return fract((p3.x + p3.y) * p3.z);
}
// сглаженное шумовое поле (без жёстких клеток — облака не дёргаются при движении)
float vnoise(vec2 p){
  vec2 i = floor(p), f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  float a = hash12(i);
  float b = hash12(i + vec2(1.0, 0.0));
  float c = hash12(i + vec2(0.0, 1.0));
  float d = hash12(i + vec2(1.0, 1.0));
  return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}
void main(){
  vec4 a = uInvVP * vec4(vNdc, -1.0, 1.0);
  vec4 b = uInvVP * vec4(vNdc, 1.0, 1.0);
  vec3 dir = normalize(b.xyz / b.w - a.xyz / a.w);
  float h = dir.y;
  vec3 col = mix(uHorizon, uTop, pow(clamp(h, 0.0, 1.0), 0.55));
  if (h < 0.0) col = mix(uHorizon, uHorizon * 0.45, clamp(-h * 3.0, 0.0, 1.0));
  float sunvis = smoothstep(-0.12, 0.03, uSunDir.y);

  // ---- стилизованное солнце: плоский диск с чётким краем и ореолом ----
  float sd = dot(dir, uSunDir);
  float ang = acos(clamp(sd, -1.0, 1.0));
  float R = 0.065;
  if (ang < R * 2.4) {
    float halo = smoothstep(R * 2.4, R, ang);
    col += uSunCol * halo * halo * 0.38 * sunvis;
  }
  if (ang < R) {
    float edge = smoothstep(R, R * 0.90, ang);
    vec3 core = vec3(1.0, 0.97, 0.80);
    vec3 rim  = vec3(1.0, 0.74, 0.28);
    col = mix(col, mix(rim, core, smoothstep(R, R * 0.35, ang)), edge);
  }

  // ---- луна: диск с кратерами ----
  vec3 md = -uSunDir;
  float mang = acos(clamp(dot(dir, md), -1.0, 1.0));
  float mR = 0.052;
  float moonvis = smoothstep(-0.10, 0.04, md.y) * uNight;
  if (mang < mR * 1.7) {
    float halo = smoothstep(mR * 1.7, mR, mang);
    col += vec3(0.55, 0.62, 0.90) * halo * halo * 0.22 * moonvis;
  }
  if (mang < mR) {
    vec3 up = abs(md.y) < 0.99 ? vec3(0, 1, 0) : vec3(1, 0, 0);
    vec3 tx = normalize(cross(up, md));
    vec3 ty = cross(md, tx);
    vec2 mp = vec2(dot(dir, tx), dot(dir, ty));
    float rr = length(mp) / mR;
    float edge = smoothstep(mR, mR * 0.92, mang);
    vec3 moonCol = mix(vec3(0.78, 0.81, 0.92), vec3(0.96, 0.97, 1.0), smoothstep(1.0, 0.15, rr));
    vec2 cell = floor(mp / mR * 4.0);
    float cr = hash12(cell + 3.7);
    if (cr > 0.60 && rr < 0.88) moonCol *= 0.80;
    if (cr > 0.84 && rr < 0.82) moonCol *= 0.84;
    col = mix(col, moonCol, edge * moonvis);
  }

  // ---- звёзды ----
  if (uNight > 0.02 && dir.y > 0.0) {
    vec3 sp = dir * 220.0;
    vec3 ip = floor(sp);
    vec3 fp = fract(sp) - 0.5;
    float r = hash13(ip);
    if (r > 0.9955) {
      float b = smoothstep(0.32, 0.02, length(fp));
      float tw = 0.55 + 0.45 * hash13(ip + 7.0);
      col += vec3(0.9, 0.93, 1.0) * b * uNight * tw;
    }
  }

  // ---- блочные облака (сглаженный шум + авто-сглаживание краёв) ----
  float cloudY = 210.0;
  if (dir.y > 0.015) {
    float cty = (cloudY - uCamPos.y) / dir.y;
    if (cty > 0.0 && cty < 3000.0) {
      vec2 cp = (uCamPos.xz + dir.xz * cty) * 0.0024 + vec2(uTime * 0.006, uTime * 0.002);
      float m = vnoise(cp) * 0.62 + vnoise(cp * 2.1 + 31.7) * 0.38;
      // ширина перехода растёт там, где узор меняется быстрее пикселя —
      // края не «шагают» пикселями при движении камеры
      float aa = max(fwidth(m) * 1.3, 0.015);
      float cov = smoothstep(0.575 - aa, 0.575 + aa, m);
      float fade = exp(-cty * 0.0026) * smoothstep(0.015, 0.10, dir.y);
      float day = 1.0 - uNight;
      vec3 cloudCol = mix(vec3(0.10, 0.11, 0.17), vec3(1.04, 1.05, 1.08), day);
      cloudCol += uSunCol * 0.10 * day;
      col = mix(col, cloudCol, cov * fade * 0.92);
    }
  }

  frag = vec4(col, 1.0);
}
)";

static const char* LINE_VS = R"(#version 430 core
layout(location=0) in vec3 aPos;
uniform mat4 uVP;
void main(){ gl_Position = uVP * vec4(aPos, 1.0); }
)";

static const char* LINE_FS = R"(#version 430 core
out vec4 frag;
uniform vec4 uColor;
void main(){ frag = uColor; }
)";

static const char* UI_VS = R"(#version 430 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
layout(location=2) in vec4 aCol;
uniform vec2 uScreen;
out vec2 vUV; out vec4 vCol;
void main(){
  vec2 ndc = vec2(aPos.x / uScreen.x * 2.0 - 1.0, 1.0 - aPos.y / uScreen.y * 2.0);
  vUV = aUV; vCol = aCol;
  gl_Position = vec4(ndc, 0.0, 1.0);
}
)";

static const char* UI_FS = R"(#version 430 core
in vec2 vUV; in vec4 vCol;
out vec4 frag;
uniform sampler2D uTex;
void main(){ frag = texture(uTex, vUV) * vCol; }
)";

static const char* RT_VS = R"(#version 430 core
out vec2 vNdc;
void main(){
  vec2 p = vec2((gl_VertexID == 1) ? 3.0 : -1.0, (gl_VertexID == 2) ? 3.0 : -1.0);
  vNdc = p;
  gl_Position = vec4(p, 0.0, 1.0);
}
)";

static const char* RT_FS = R"(#version 430 core
in vec2 vNdc;
out vec4 frag;
uniform sampler3D uVol;
uniform sampler2D uAtlas;
uniform sampler2D uTileMap;
uniform mat4 uInvVP;
uniform vec3 uCamPos;
uniform vec3 uSunDir, uSunColor, uAmbSky, uAmbGround, uFogColor, uBlockCol;
uniform vec3 uVolOrigin;
uniform vec3 uVolSize;
uniform float uFogDensity, uTime;
uniform int uMaxSteps;

struct HitInfo { bool hit; float t; vec3 n; uint id; vec3 air; };

vec4 sampleVol(ivec3 p){
  ivec3 q = p - ivec3(uVolOrigin);
  if (any(lessThan(q, ivec3(0))) || any(greaterThanEqual(q, ivec3(uVolSize)))) return vec4(0.0);
  return texelFetch(uVol, q, 0);
}
HitInfo trace(vec3 ro, vec3 rd, float maxT){
  HitInfo h; h.hit = false; h.t = maxT; h.n = vec3(0.0); h.id = 0u; h.air = ro;
  ivec3 mapPos = ivec3(floor(ro));
  vec3 sd = sign(rd);
  vec3 inv = 1.0 / rd;
  vec3 tMax = (vec3(mapPos) + max(sd, vec3(0.0)) - ro) * inv;
  vec3 tDelta = abs(inv);
  vec3 n = vec3(0.0);
  vec3 air = ro;
  float t = 0.0;
  for (int i = 0; i < uMaxSteps; i++){
    vec4 s = sampleVol(mapPos);
    uint id = uint(s.r * 255.0 + 0.5);
    if (id == 5u) {
      air = vec3(mapPos);
      if (tMax.x < tMax.y) { if (tMax.x < tMax.z) { mapPos.x += int(sd.x); t = tMax.x; tMax.x += tDelta.x; n = vec3(-sd.x,0,0);} else { mapPos.z += int(sd.z); t = tMax.z; tMax.z += tDelta.z; n = vec3(0,0,-sd.z);} }
      else { if (tMax.y < tMax.z) { mapPos.y += int(sd.y); t = tMax.y; tMax.y += tDelta.y; n = vec3(0,-sd.y,0);} else { mapPos.z += int(sd.z); t = tMax.z; tMax.z += tDelta.z; n = vec3(0,0,-sd.z);} }
      if (t > maxT) break;
      continue;
    }
    if (id != 0u) {
      h.hit = true; h.t = t; h.n = n; h.id = id; h.air = air;
      return h;
    }
    air = vec3(mapPos);
    if (tMax.x < tMax.y) { if (tMax.x < tMax.z) { mapPos.x += int(sd.x); t = tMax.x; tMax.x += tDelta.x; n = vec3(-sd.x,0,0);} else { mapPos.z += int(sd.z); t = tMax.z; tMax.z += tDelta.z; n = vec3(0,0,-sd.z);} }
    else { if (tMax.y < tMax.z) { mapPos.y += int(sd.y); t = tMax.y; tMax.y += tDelta.y; n = vec3(0,-sd.y,0);} else { mapPos.z += int(sd.z); t = tMax.z; tMax.z += tDelta.z; n = vec3(0,0,-sd.z);} }
    if (t > maxT) break;
  }
  return h;
}

bool shadowRay(vec3 ro, vec3 rd, float maxT){
  ivec3 mapPos = ivec3(floor(ro));
  vec3 sd = sign(rd);
  vec3 inv = 1.0 / rd;
  vec3 tMax = (vec3(mapPos) + max(sd, vec3(0.0)) - ro) * inv;
  vec3 tDelta = abs(inv);
  float t = 0.0;
  for (int i = 0; i < 48; i++){
    vec4 s = sampleVol(mapPos);
    uint id = uint(s.r * 255.0 + 0.5);
    if (id != 0u && id != 5u && id != 7u && id != 17u && id != 18u) return true;
    if (tMax.x < tMax.y) { if (tMax.x < tMax.z) { mapPos.x += int(sd.x); t = tMax.x; tMax.x += tDelta.x; } else { mapPos.z += int(sd.z); t = tMax.z; tMax.z += tDelta.z; } }
    else { if (tMax.y < tMax.z) { mapPos.y += int(sd.y); t = tMax.y; tMax.y += tDelta.y; } else { mapPos.z += int(sd.z); t = tMax.z; tMax.z += tDelta.z; } }
    if (t > maxT) break;
  }
  return false;
}

void main(){
  vec4 a = uInvVP * vec4(vNdc, -1.0, 1.0);
  vec4 b = uInvVP * vec4(vNdc, 1.0, 1.0);
  vec3 rd = normalize(b.xyz / b.w - a.xyz / a.w);
  vec3 ro = uCamPos;
  float maxT = 160.0;
  HitInfo h = trace(ro, rd, maxT);
  vec3 col;
  if (!h.hit) {
    float hh = rd.y;
    col = mix(uFogColor, uAmbSky * 2.2, pow(clamp(hh, 0.0, 1.0), 0.55));
    float sd = dot(rd, uSunDir);
    col += uSunColor * (pow(max(sd, 0.0), 900.0) * 2.0 + pow(max(sd, 0.0), 12.0) * 0.12);
    col += vec3(0.9, 0.93, 1.0) * pow(max(-sd, 0.0), 900.0) * 0.6;
  } else {
    vec3 hitPos = ro + rd * h.t;
    vec3 lt = texelFetch(uTileMap, ivec2(int(h.id), 0), 0).rgb * 63.0;
    float tile;
    vec2 fuv;
    if (abs(h.n.y) > 0.5) { tile = lt.x; fuv = fract(hitPos.xz); }
    else { tile = lt.y; fuv = fract(hitPos.zy); }
    if (h.n.y > 0.5) fuv = fract(hitPos.xz);
    if (h.n.y < -0.5) fuv = fract(hitPos.xz);
    if (h.n.x > 0.5) fuv = vec2(1.0 - fract(hitPos.z), fract(hitPos.y));
    if (h.n.x < -0.5) fuv = vec2(fract(hitPos.z), fract(hitPos.y));
    if (h.n.z > 0.5) fuv = vec2(fract(hitPos.x), fract(hitPos.y));
    if (h.n.z < -0.5) fuv = vec2(1.0 - fract(hitPos.x), fract(hitPos.y));
    float ti = floor(tile + 0.5);
    vec2 auv = (vec2(mod(ti, 8.0), floor(ti / 8.0)) + fuv) * (1.0 / 8.0);
    vec3 albedo = texture(uAtlas, auv).rgb;
    vec4 la = sampleVol(ivec3(floor(h.air)));
    float sky = la.g, blk = la.b;
    float dif = max(dot(h.n, uSunDir), 0.0);
    float sh = 1.0;
    if (dif > 0.01 && sky > 0.05) {
      if (shadowRay(hitPos + h.n * 0.02, uSunDir, 40.0)) sh = 0.0;
    }
    vec3 light = uAmbSky * sky * (0.45 + 0.55 * dif * mix(0.35, 1.0, sh));
    light += uSunColor * sky * dif * sh * 0.85;
    light += uBlockCol * blk * 0.95;
    light += uAmbGround * 0.12;
    col = albedo * light;
    float d = h.t;
    float fog = 1.0 - exp(-d * d * uFogDensity);
    col = mix(col, uFogColor, clamp(fog, 0.0, 1.0));
  }
  frag = vec4(col, 1.0);
}
)";

// Оверлей ломания блока (провал/трещины по UV)
static const char* BREAK_VS = R"(#version 430 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec2 aUV;
layout(location=2) in vec3 aN;
uniform mat4 uVP;
uniform vec3 uOrigin;
uniform float uBlk; // мировой размер вокселя
out vec2 vUV; out vec3 vN;
void main(){
  vec3 wp = aPos * uBlk + uOrigin + aN * 0.0025;
  vUV = aUV; vN = aN;
  gl_Position = uVP * vec4(wp, 1.0);
}
)";

static const char* BREAK_FS = R"(#version 430 core
in vec2 vUV; in vec3 vN;
layout(location=0) out vec4 frag;
layout(location=1) out vec4 gN;
uniform float uProg;
uniform vec3 uCamPos;
uniform vec3 uFogColor;
uniform float uFogDensity;
float h21(vec2 p){ p = fract(p*vec2(123.34,456.21)); p += dot(p,p+45.32); return fract(p.x*p.y); }
void main(){
  // разбиваем грань на ячейки 4x4, каждая трескается в свой момент
  vec2 cell = floor(vUV * 4.0);
  float thr = h21(cell + vec2(vN.x * 3.0 + vN.y * 7.0, vN.z * 5.0 + vN.y * 11.0)) * 0.85;
  float crack = smoothstep(thr, thr + 0.12, uProg);
  // мелкий шум внутри ячейки — неровный край трещины
  float n = h21(vUV * 9.0 + cell);
  crack *= step(0.18, n + 0.35);
  if (crack < 0.01) discard;
  vec3 col = vec3(0.02, 0.015, 0.012) * (0.6 + 0.4 * h21(cell * 1.7));
  frag = vec4(col, crack * 0.92);
  gN = vec4(normalize(vN) * 0.5 + 0.5, 1.0);
}
)";

// Цветной рендер кубов (мобы, модельки)
static const char* COLOR_VS = R"(#version 430 core
layout(location=0) in vec3 aPos;
layout(location=2) in vec3 aN;
uniform mat4 uVP;
uniform mat4 uModel;
out vec3 vN; out vec3 vWorld;
void main(){
  vec4 wp4 = uModel * vec4(aPos, 1.0);
  vWorld = wp4.xyz;
  vN = mat3(uModel) * aN;
  gl_Position = uVP * wp4;
}
)";

static const char* COLOR_FS = R"(#version 430 core
in vec3 vN; in vec3 vWorld;
layout(location=0) out vec4 frag;
layout(location=1) out vec4 gN;
uniform vec3 uColor;
uniform vec3 uSunDir, uSunColor, uAmbSky, uAmbGround, uFogColor;
uniform vec3 uCamPos;
uniform float uFogDensity;
uniform float uHurt;
float h21(vec2 p){ p = fract(p*vec2(123.34,456.21)); p += dot(p,p+45.32); return fract(p.x*p.y); }
void main(){
  vec3 n = normalize(vN);
  float sky = clamp(n.y * 0.5 + 0.5, 0.0, 1.0);
  float sun = clamp(dot(n, normalize(uSunDir)), 0.0, 1.0);
  vec3 light = mix(uAmbGround, uAmbSky, sky) + uSunColor * sun * 0.75;
  // пиксельная "текстура" как у блоков
  vec3 cell = floor(vWorld * 16.0);
  float pix = 0.88 + 0.24 * h21(cell.xy + cell.z * 7.0);
  vec3 col = uColor * light * pix;
  col = mix(col, vec3(1.0, 0.25, 0.2), clamp(uHurt, 0.0, 1.0) * 0.65);
  float dist = length(vWorld - uCamPos);
  float fog = 1.0 - exp(-pow(dist * uFogDensity, 2.0));
  col = mix(col, uFogColor, clamp(fog, 0.0, 1.0));
  frag = vec4(col, 1.0);
  gN = vec4(n * 0.5 + 0.5, 1.0);
}
)";

// Мягкая тень-пятно под мобом (проецируется на землю по направлению солнца)
static const char* SHADOW_VS = R"(#version 430 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec2 aUV;
layout(location=2) in float aA;
uniform mat4 uVP;
out vec2 vUV; out float vA; out vec3 vW;
void main(){
  vUV = aUV; vA = aA; vW = aPos;
  gl_Position = uVP * vec4(aPos, 1.0);
}
)";

static const char* SHADOW_FS = R"(#version 430 core
in vec2 vUV; in float vA; in vec3 vW;
out vec4 frag;
uniform vec3 uCamPos, uFogCol;
uniform float uFogD;
void main(){
  float d = length(vUV * 2.0 - 1.0);
  float a = smoothstep(1.0, 0.42, d) * vA;
  float dist = length(vW - uCamPos);
  float fog = 1.0 - exp(-dist * dist * uFogD);
  a *= 1.0 - fog;
  if (a < 0.004) discard;
  frag = vec4(mix(vec3(0.0, 0.0, 0.03), uFogCol, fog), a);
}
)";

// Пост-эффект: чёрная обводка контуров (как в мультиках), по глубине и нормалям
static const char* OUTLINE_VS = R"(#version 430 core
out vec2 vNdc;
void main(){
  vec2 p = vec2((gl_VertexID == 1) ? 3.0 : -1.0, (gl_VertexID == 2) ? 3.0 : -1.0);
  vNdc = p;
  gl_Position = vec4(p, 0.0, 1.0);
}
)";

static const char* OUTLINE_FS = R"(#version 430 core
in vec2 vNdc;
out vec4 frag;
uniform sampler2D uCol, uNrm, uDepth;
uniform vec2 uRes;
uniform float uNear, uFar, uThick, uStrength;
uniform float uOutline;   // 1 — рисовать обводку, 0 — только пикселизация
uniform vec3 uPix;        // x — дистанция старта, y — дистанция максимума, z — макс. размер пикселя
float lin(float d){
  float z = d * 2.0 - 1.0;
  return (2.0 * uNear * uFar) / (uFar + uNear - z * (uFar - uNear));
}
void main(){
  vec2 uv = vNdc * 0.5 + 0.5;
  float d0 = lin(texture(uDepth, uv).r);
  // пикселизация дальних объектов: сетка растёт с дистанцией (t*t — ближние почти не тронуты)
  float t = clamp((d0 - uPix.x) / max(uPix.y - uPix.x, 1.0), 0.0, 1.0);
  float px = mix(1.0, uPix.z, t * t);
  vec2 grid = max(uRes / px, vec2(1.0));
  vec2 suv = (floor(uv * grid) + 0.5) / grid;
  vec3 col = texture(uCol, suv).rgb;
  float e = 0.0;
  if (uOutline > 0.5) {
    vec3 n0 = texture(uNrm, uv).rgb * 2.0 - 1.0;
    float de = 0.0, ne = 0.0;
    for (int i = 0; i < 4; i++) {
      vec2 dir = i == 0 ? vec2(1.0, 0.0) : i == 1 ? vec2(-1.0, 0.0) : i == 2 ? vec2(0.0, 1.0) : vec2(0.0, -1.0);
      for (int r = 1; r <= 2; r++) {
        vec2 off = dir * uThick * float(r) / uRes;
        float d1 = lin(texture(uDepth, uv + off).r);
        vec3 n1 = texture(uNrm, uv + off).rgb * 2.0 - 1.0;
        de = max(de, abs(d1 - d0) / max(min(d0, d1), 0.75));
        ne = max(ne, length(n1 - n0));
      }
    }
    e = max(smoothstep(0.035, 0.10, de), smoothstep(0.55, 1.05, ne));
    e *= smoothstep(135.0, 75.0, d0);   // вдали обводка тает (не обводит границу прорисовки)
  }
  frag = vec4(mix(col, vec3(0.01, 0.01, 0.02), e * uStrength), 1.0);
}
)";
