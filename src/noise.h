#pragma once
#include "common.h"

namespace nz {

inline uint32_t uhash(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  x ^= x >> 16;
  return x;
}
inline uint32_t h2(int x, int y) {
  return uhash((uint32_t)x * 374761393u + (uint32_t)y * 668265263u);
}
inline uint32_t h3(int x, int y, int z) {
  return uhash((uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)z * 2147483647u);
}

inline float srandf(int x, int y) { return h2(x, y) / (float)4294967295.0f; }
inline float srandf(int x, int y, int z) { return h3(x, y, z) / (float)4294967295.0f; }
inline float fh2(int x, int y) { return srandf(x, y) * 2.0f - 1.0f; }
inline float fh3(int x, int y, int z) { return srandf(x, y, z) * 2.0f - 1.0f; }

inline float fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

inline float vn2(float x, float y) {
  int x0 = (int)floorf(x), y0 = (int)floorf(y);
  float fx = fade(x - x0), fy = fade(y - y0);
  float a = fh2(x0, y0), b = fh2(x0 + 1, y0), c = fh2(x0, y0 + 1), d = fh2(x0 + 1, y0 + 1);
  return lerp(lerp(a, b, fx), lerp(c, d, fx), fy);
}

inline float vn3(float x, float y, float z) {
  int x0 = (int)floorf(x), y0 = (int)floorf(y), z0 = (int)floorf(z);
  float fx = fade(x - x0), fy = fade(y - y0), fz = fade(z - z0);
  float c000 = fh3(x0, y0, z0), c100 = fh3(x0 + 1, y0, z0);
  float c010 = fh3(x0, y0 + 1, z0), c110 = fh3(x0 + 1, y0 + 1, z0);
  float c001 = fh3(x0, y0, z0 + 1), c101 = fh3(x0 + 1, y0, z0 + 1);
  float c011 = fh3(x0, y0 + 1, z0 + 1), c111 = fh3(x0 + 1, y0 + 1, z0 + 1);
  float x00 = lerp(c000, c100, fx), x10 = lerp(c010, c110, fx);
  float x01 = lerp(c001, c101, fx), x11 = lerp(c011, c111, fx);
  return lerp(lerp(x00, x10, fy), lerp(x01, x11, fy), fz);
}

inline float fbm2(float x, float y, int oct, float lac = 2.0f, float gain = 0.5f) {
  float a = 1.0f, s = 0.0f, n = 0.0f;
  for (int i = 0; i < oct; i++) {
    s += a * vn2(x, y);
    n += a;
    x *= lac;
    y *= lac;
    a *= gain;
  }
  return s / n;
}

inline float fbm3(float x, float y, float z, int oct, float lac = 2.0f, float gain = 0.5f) {
  float a = 1.0f, s = 0.0f, n = 0.0f;
  for (int i = 0; i < oct; i++) {
    s += a * vn3(x, y, z);
    n += a;
    x *= lac;
    y *= lac;
    z *= lac;
    a *= gain;
  }
  return s / n;
}

inline float ridged2(float x, float y, int oct, float lac = 2.0f, float gain = 0.5f) {
  float a = 1.0f, s = 0.0f, n = 0.0f;
  for (int i = 0; i < oct; i++) {
    float v = 1.0f - fabsf(vn2(x, y));
    v *= v;
    s += a * v;
    n += a;
    x *= lac;
    y *= lac;
    a *= gain;
  }
  return s / n;
}

}  // namespace nz
