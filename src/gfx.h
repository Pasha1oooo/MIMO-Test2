#pragma once
#include "blocks.h"
#include "common.h"
#include "noise.h"
#include "shaders.h"

struct Program {
  GLuint id = 0;
  Program() = default;
  Program(const char* vs, const char* fs) { compile(vs, fs); }
  void compile(const char* vs, const char* fs) {
    auto sh = [&](GLenum type, const char* src) -> GLuint {
      GLuint s = glCreateShader(type);
      glShaderSource(s, 1, &src, nullptr);
      glCompileShader(s);
      GLint ok = 0;
      glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
      if (!ok) {
        char log[4096];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        fprintf(stderr, "shader error:\n%s\n", log);
      }
      return s;
    };
    GLuint v = sh(GL_VERTEX_SHADER, vs), f = sh(GL_FRAGMENT_SHADER, fs);
    id = glCreateProgram();
    glAttachShader(id, v);
    glAttachShader(id, f);
    glLinkProgram(id);
    GLint ok = 0;
    glGetProgramiv(id, GL_LINK_STATUS, &ok);
    if (!ok) {
      char log[4096];
      glGetProgramInfoLog(id, sizeof(log), nullptr, log);
      fprintf(stderr, "link error:\n%s\n", log);
    }
    glDeleteShader(v);
    glDeleteShader(f);
  }
  void use() const { glUseProgram(id); }
  GLint L(const char* n) const { return glGetUniformLocation(id, n); }
  void m4(const char* n, const glm::mat4& m) const { glUniformMatrix4fv(L(n), 1, GL_FALSE, value_ptr(m)); }
  void v3(const char* n, const glm::vec3& v) const { glUniform3f(L(n), v.x, v.y, v.z); }
  void v2(const char* n, float x, float y) const { glUniform2f(L(n), x, y); }
  void v4(const char* n, const glm::vec4& v) const { glUniform4f(L(n), v.x, v.y, v.z, v.w); }
  void f1(const char* n, float f) const { glUniform1f(L(n), f); }
  void i1(const char* n, int i) const { glUniform1i(L(n), i); }
};

inline void buildBlockAtlas(std::vector<uint8_t>& out, int S) {
  const int T = S / ATLAS_TILES;
  out.assign((size_t)S * S * 4, 0);
  auto put = [&](int tile, int x, int y, int r, int g, int b, int a = 255) {
    if (x < 0 || y < 0 || x >= T || y >= T) return;
    int col = tile % ATLAS_TILES, row = tile / ATLAS_TILES;
    uint8_t* p = &out[((row * T + y) * S + col * T + x) * 4];
    p[0] = (uint8_t)std::max(0, std::min(255, r));
    p[1] = (uint8_t)std::max(0, std::min(255, g));
    p[2] = (uint8_t)std::max(0, std::min(255, b));
    p[3] = (uint8_t)a;
  };
  auto n2 = [&](float x, float y) { return nz::vn2(x, y); };
  // grass top
  for (int y = 0; y < T; y++)
    for (int x = 0; x < T; x++) {
      float n = n2(x * 0.22f + 3.1f, y * 0.22f) * 26.0f;
      int sp = nz::h2(x, y) % 17 == 0 ? -22 : 0;
      put(T_GRASS_TOP, x, y, 86 + n + sp, 150 + n * 0.8f + sp, 52 + n * 0.6f + sp);
    }
  // dirt
  for (int y = 0; y < T; y++)
    for (int x = 0; x < T; x++) {
      float n = n2(x * 0.3f + 9.7f, y * 0.3f) * 20.0f;
      int sp = nz::h2(x + 40, y) % 13 == 0 ? -25 : 0;
      put(T_DIRT, x, y, 133 + n + sp, 96 + n + sp, 67 + n * 0.8f + sp);
    }
  // grass side
  for (int x = 0; x < T; x++) {
    int h = 6 + (int)(nz::srandf(x, 5) * 4.0f);
    for (int y = 0; y < T; y++) {
      float n = n2(x * 0.3f + 9.7f, y * 0.3f) * 18.0f;
      if (y < h) {
        float n2v = n2(x * 0.25f + 3.1f, y * 0.25f) * 22.0f;
        put(T_GRASS_SIDE, x, y, 86 + n2v, 150 + n2v * 0.8f, 52 + n2v * 0.6f);
      } else if (y == h) {
        put(T_GRASS_SIDE, x, y, 70 + n, 120 + n, 44 + n);
      } else {
        int sp = nz::h2(x + 40, y) % 13 == 0 ? -25 : 0;
        put(T_GRASS_SIDE, x, y, 133 + n + sp, 96 + n + sp, 67 + n * 0.8f + sp);
      }
    }
  }
  // stone
  for (int y = 0; y < T; y++)
    for (int x = 0; x < T; x++) {
      float n = n2(x * 0.25f + 20.5f, y * 0.25f) * 16.0f;
      float c = n2(x * 0.09f + 50.5f, y * 0.09f) * 14.0f;
      put(T_STONE, x, y, 124 + n + c, 124 + n + c, 126 + n + c);
    }
  // sand
  for (int y = 0; y < T; y++)
    for (int x = 0; x < T; x++) {
      float n = n2(x * 0.4f + 70.5f, y * 0.4f) * 12.0f;
      put(T_SAND, x, y, 219 + n, 205 + n, 158 + n);
    }
  // water
  for (int y = 0; y < T; y++)
    for (int x = 0; x < T; x++) {
      float n = n2(x * 0.2f + 90.5f, y * 0.2f) * 14.0f + sinf(x * 0.5f + y * 0.3f) * 4.0f;
      put(T_WATER, x, y, 44 + n, 92 + n, 200 + n * 0.5f);
    }
  // log side
  for (int x = 0; x < T; x++) {
    float s = nz::srandf(x, 11) * 26.0f - 13.0f;
    for (int y = 0; y < T; y++) {
      float n = n2(x * 0.5f + 110.5f, y * 0.15f) * 12.0f;
      int dark = (x % 7 == 0) ? -22 : 0;
      put(T_LOG_SIDE, x, y, 104 + s + n + dark, 82 + s + n + dark, 50 + s * 0.7f + n + dark);
    }
  }
  // log top
  for (int y = 0; y < T; y++)
    for (int x = 0; x < T; x++) {
      float dx = x - T / 2.0f + 0.5f, dy = y - T / 2.0f + 0.5f;
      float d = sqrtf(dx * dx + dy * dy);
      float ring = sinf(d * 1.4f) * 12.0f;
      float n = n2(x * 0.4f + 130.5f, y * 0.4f) * 6.0f;
      int dark = d > T * 0.42f ? -30 : 0;
      put(T_LOG_TOP, x, y, 160 + ring + n + dark, 128 + ring + n + dark, 82 + ring * 0.6f + n + dark);
    }
  // leaves (alpha-tested)
  for (int y = 0; y < T; y++)
    for (int x = 0; x < T; x++) {
      float n = n2(x * 0.35f + 150.5f, y * 0.35f) * 30.0f;
      float hole = n2(x * 0.5f + 170.5f, y * 0.5f);
      if (hole > 0.42f) {
        put(T_LEAVES, x, y, 0, 0, 0, 0);
      } else {
        int sp = nz::h2(x + 3, y + 7) % 11 == 0 ? -20 : 0;
        put(T_LEAVES, x, y, 52 + n + sp, 118 + n + sp, 38 + n * 0.7f + sp);
      }
    }
  // planks
  for (int y = 0; y < T; y++)
    for (int x = 0; x < T; x++) {
      float n = n2(x * 0.6f + 190.5f, y * 0.12f) * 14.0f;
      int sep = (y % 8 == 0) ? -35 : 0;
      int nail = (y % 8 == 4 && (x % 16 == 3 || x % 16 == 12)) ? -30 : 0;
      put(T_PLANKS, x, y, 174 + n + sep + nail, 138 + n + sep + nail, 90 + n * 0.8f + sep + nail);
    }
  // cobble
  for (int y = 0; y < T; y++)
    for (int x = 0; x < T; x++) {
      float n = n2(x * 0.2f + 210.5f, y * 0.2f);
      float v = n2(x * 0.08f + 230.5f, y * 0.08f) * 20.0f;
      if (n < -0.18f) {
        put(T_COBBLE, x, y, 78 + v, 78 + v, 80 + v);
      } else {
        int sp = nz::h2(x, y + 99) % 9 == 0 ? -18 : 0;
        put(T_COBBLE, x, y, 132 + v + sp, 132 + v + sp, 134 + v + sp);
      }
    }
  // table top
  for (int y = 0; y < T; y++)
    for (int x = 0; x < T; x++) {
      float n = n2(x * 0.6f + 250.5f, y * 0.12f) * 12.0f;
      int border = (x < 2 || y < 2 || x >= T - 2 || y >= T - 2) ? -40 : 0;
      int grid = (x == T / 2 || y == T / 2) ? -45 : 0;
      put(T_TABLE_TOP, x, y, 168 + n + border + grid, 132 + n + border + grid, 86 + n + border + grid);
    }
  // table side
  for (int y = 0; y < T; y++)
    for (int x = 0; x < T; x++) {
      float n = n2(x * 0.6f + 270.5f, y * 0.12f) * 12.0f;
      int tool = (y > 8 && y < 12 && x > 6 && x < 12) ? -50 : 0;
      int tool2 = (y > 8 && y < 11 && x > 18 && x < 26) ? -50 : 0;
      int sep = (y % 8 == 0) ? -30 : 0;
      put(T_TABLE_SIDE, x, y, 160 + n + tool + tool2 + sep, 126 + n + tool + tool2 + sep, 82 + n + tool + tool2 + sep);
    }
  // lamp
  for (int y = 0; y < T; y++)
    for (int x = 0; x < T; x++) {
      float dx = x - T / 2.0f + 0.5f, dy = y - T / 2.0f + 0.5f;
      float d = sqrtf(dx * dx + dy * dy);
      bool frame = x < 3 || y < 3 || x >= T - 3 || y >= T - 3;
      if (frame) {
        int n = (int)(n2(x * 0.5f + 290.5f, y * 0.5f) * 10.0f);
        put(T_LAMP, x, y, 118 + n, 92 + n, 56 + n);
      } else {
        float g = 1.0f - d / (T * 0.5f);
        int r = 200 + (int)(g * 55.0f), gg = 150 + (int)(g * 90.0f), b = 60 + (int)(g * 90.0f);
        put(T_LAMP, x, y, r, gg, b);
      }
    }
  // ores
  for (int ore = 0; ore < 2; ore++) {
    int tile = ore == 0 ? T_COALORE : T_IRONORE;
    for (int y = 0; y < T; y++)
      for (int x = 0; x < T; x++) {
        float n = n2(x * 0.25f + 20.5f, y * 0.25f) * 14.0f;
        float c = n2(x * 0.09f + 50.5f, y * 0.09f) * 12.0f;
        int r = 124 + n + c, g = 124 + n + c, b = 126 + n + c;
        int cx = x / 8, cy = y / 8;
        float rr = nz::srandf(cx + ore * 31, cy + 5);
        if (rr < 0.42f) {
          float ox = x - cx * 8 - 4.0f, oy = y - cy * 8 - 4.0f;
          float d = sqrtf(ox * ox + oy * oy) + n2(x * 0.6f + 300.5f, y * 0.6f) * 1.8f;
          if (d < 2.6f) {
            if (ore == 0) { r = 34; g = 34; b = 36; }
            else { r = 206; g = 160; b = 118; }
          } else if (d < 3.4f) {
            r = (r + (ore == 0 ? 34 : 206)) / 2;
            g = (g + (ore == 0 ? 34 : 160)) / 2;
            b = (b + (ore == 0 ? 36 : 118)) / 2;
          }
        }
        put(tile, x, y, r, g, b);
      }
  }
  // snow
  for (int y = 0; y < T; y++)
    for (int x = 0; x < T; x++) {
      float n = n2(x * 0.3f + 320.5f, y * 0.3f) * 8.0f;
      put(T_SNOW, x, y, 240 + n, 246 + n, 252 + n * 0.5f);
    }
  // bedrock
  for (int y = 0; y < T; y++)
    for (int x = 0; x < T; x++) {
      float n = n2(x * 0.5f + 340.5f, y * 0.5f) * 30.0f;
      float c = n2(x * 0.15f + 360.5f, y * 0.15f) * 15.0f;
      put(T_BEDROCK, x, y, 62 + n + c, 62 + n + c, 64 + n + c);
    }
  // glass
  for (int y = 0; y < T; y++)
    for (int x = 0; x < T; x++) {
      bool border = x == 0 || y == 0 || x == T - 1 || y == T - 1;
      bool diag = (x + y == 8) || (x + y == 9) || (x + y == 20);
      if (border) put(T_GLASS, x, y, 198, 224, 232, 220);
      else if (diag) put(T_GLASS, x, y, 230, 245, 250, 140);
      else put(T_GLASS, x, y, 0, 0, 0, 0);
    }
  // tall grass
  for (int x = 0; x < T; x++) {
    float ph = nz::srandf(x, 41);
    int h = 10 + (int)(ph * 18.0f);
    int sway = (int)(sinf(ph * 12.0f) * 3.0f);
    for (int y = 0; y < h; y++) {
      int xx = x + (int)(sway * (float)y / std::max(1, h) * 2.0f);
      float n = n2(x * 0.5f + 380.5f, y * 0.5f) * 16.0f;
      put(T_TALL, xx, T - 1 - y, 64 + n, 132 + n, 42 + n * 0.7f);
    }
  }
  // цветы: стебель с листиком, 6 лепестков, светлая серединка
  auto flower = [&](int tile, int pr, int pg, int pb, int dr, int dg, int db, int cr, int cg, int cb, int seed) {
    float jx = nz::srandf(seed, 3) * 1.6f - 0.8f;   // лёгкий сдвиг соцветия
    float jh = nz::srandf(seed, 7) * 4.0f;          // высота стебля
    int cxp = T / 2 + (int)jx;
    int stemTop = 12 + (int)jh;
    // стебель
    for (int yy = 0; yy < stemTop; yy++) {
      float wob = sinf(yy * 0.35f + seed) * 0.6f;
      int xx = cxp + (int)wob;
      float n = n2(xx * 0.6f + seed * 3.0f, yy * 0.6f) * 14.0f;
      put(tile, xx, T - 1 - yy, 52 + n, 118 + n, 40 + n * 0.7f);
      if (yy < stemTop - 2 && nz::srandf(seed, yy + 20) > 0.55f) put(tile, xx + 1, T - 1 - yy, 44 + n, 100 + n, 34 + n * 0.7f);
    }
    // листик
    int ly = 4 + (int)(nz::srandf(seed, 11) * 4.0f);
    int ldir = nz::srandf(seed, 13) > 0.5f ? 1 : -1;
    for (int i = 0; i < 5; i++) {
      int xx = cxp + ldir * (1 + i);
      int yy = ly + i / 2;
      float n = n2(xx * 0.5f + 9.0f, yy * 0.5f) * 12.0f;
      put(tile, xx, T - 1 - yy, 58 + n, 128 + n, 42 + n * 0.7f);
      if (i > 0 && i < 4) put(tile, xx, T - 2 - yy, 50 + n, 112 + n, 36 + n * 0.7f);
    }
    // соцветие: 6 лепестков по кругу + тёмный контур + серединка
    float fcx = (float)cxp, fcy = (float)(T - 1 - stemTop);
    for (int y = 0; y < T; y++)
      for (int x = 0; x < T; x++) {
        float dx = x + 0.5f - fcx, dy = y + 0.5f - fcy;
        float dist = sqrtf(dx * dx + dy * dy);
        if (dist > 8.5f) continue;
        float ang = atan2f(dy, dx);
        // радиус лепестка с 6-лучевой модуляцией
        float pet = 3.4f + 2.7f * powf(0.5f + 0.5f * cosf(ang * 6.0f + seed), 0.7f);
        float n = n2(x * 0.55f + seed * 5.0f, y * 0.55f) * 16.0f;
        if (dist < 1.9f) {
          // серединка
          int s = nz::h2(x + seed, y) % 5 == 0 ? -25 : 0;
          put(tile, x, y, cr + n * 0.5f + s, cg + n * 0.5f + s, cb + n * 0.4f + s);
        } else if (dist < pet - 1.1f) {
          put(tile, x, y, pr + n, pg + n, pb + n * 0.8f);
        } else if (dist < pet) {
          put(tile, x, y, dr + n * 0.6f, dg + n * 0.6f, db + n * 0.5f);  // тень на краю лепестка
        } else if (dist < pet + 0.9f && cosf(ang * 6.0f + seed) > 0.15f) {
          put(tile, x, y, dr * 0.75f + n * 0.4f, dg * 0.75f + n * 0.4f, db * 0.75f + n * 0.4f);  // контур
        }
      }
  };
  flower(T_FRED, 214, 48, 56, 145, 24, 34, 255, 212, 88, 3);
  flower(T_FYELLOW, 246, 208, 58, 188, 142, 28, 170, 112, 32, 17);
  flower(T_FBLUE, 78, 124, 228, 38, 72, 165, 252, 232, 118, 29);
  flower(T_FWHITE, 246, 246, 236, 196, 202, 192, 252, 208, 72, 41);
  flower(T_FVIOLET, 178, 96, 216, 118, 52, 156, 250, 226, 104, 53);
}

inline GLuint makeTex2D(int w, int h, const void* data, GLenum fmt = GL_RGBA) {
  GLuint t;
  glGenTextures(1, &t);
  glBindTexture(GL_TEXTURE_2D, t);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, fmt, GL_UNSIGNED_BYTE, data);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  return t;
}

struct UiBatch {
  struct V {
    float x, y, u, v, r, g, b, a;
  };
  GLuint vao = 0, vbo = 0;
  std::vector<V> verts;
  void init() {
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(V), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(V), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(V), (void*)(4 * sizeof(float)));
    glBindVertexArray(0);
  }
  void clear() { verts.clear(); }
  void quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, float r, float g, float b, float a) {
    V v[6] = {
        {x0, y0, u0, v0, r, g, b, a}, {x1, y0, u1, v0, r, g, b, a}, {x1, y1, u1, v1, r, g, b, a},
        {x0, y0, u0, v0, r, g, b, a}, {x1, y1, u1, v1, r, g, b, a}, {x0, y1, u0, v1, r, g, b, a},
    };
    verts.insert(verts.end(), v, v + 6);
  }
  void rect(float x0, float y0, float x1, float y1, float r, float g, float b, float a) {
    quad(x0, y0, x1, y1, 0, 0, 1, 1, r, g, b, a);
  }
  void quad4(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, float u0, float v0, float u1,
             float v1, float r, float g, float b, float a) {
    V v[6] = {
        {x0, y0, u0, v0, r, g, b, a}, {x1, y1, u1, v0, r, g, b, a}, {x2, y2, u1, v1, r, g, b, a},
        {x0, y0, u0, v0, r, g, b, a}, {x2, y2, u1, v1, r, g, b, a}, {x3, y3, u0, v1, r, g, b, a},
    };
    verts.insert(verts.end(), v, v + 6);
  }
  void flush(const Program& p, GLuint tex, int sw, int sh) {
    if (verts.empty()) return;
    p.use();
    p.i1("uTex", 0);
    p.v2("uScreen", (float)sw, (float)sh);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(V), verts.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)verts.size());
    glBindVertexArray(0);
    verts.clear();
  }
};

struct Font {
  GLuint tex = 0;
  float px = 20.0f;
  stbtt_fontinfo info;
  float scale = 0, ascent = 0;
  std::vector<float> cw, ch;
  std::vector<float> xo, yo;
  std::vector<int> aw;
  float cellW = 0, cellH = 0;

  bool load(const char* path, float pixel) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> buf(sz);
    if (fread(buf.data(), 1, sz, f) != (size_t)sz) { fclose(f); return false; }
    fclose(f);
    if (!stbtt_InitFont(&info, buf.data(), stbtt_GetFontOffsetForIndex(buf.data(), 0))) return false;
    px = pixel;
    scale = stbtt_ScaleForPixelHeight(&info, px);
    int a, d;
    stbtt_GetFontVMetrics(&info, &a, &d, 0);
    ascent = a * scale;
    const int W = 1024, H = 1024;
    std::vector<uint8_t> bit((size_t)W * H, 0);
    int first = 32, count = 0x460 - 32;
    std::vector<stbtt_bakedchar> cdata(count);
    stbtt_BakeFontBitmap(buf.data(), 0, px, bit.data(), W, H, first, count, cdata.data());
    cellW = 0;
    cellH = px + 6;
    cw.assign(count, 0);
    ch.assign(count, 0);
    xo.assign(count, 0);
    yo.assign(count, 0);
    aw.assign(count, 0);
    for (int i = 0; i < count; i++) {
      stbtt_bakedchar* c = &cdata[i];
      cw[i] = (float)(c->x1 - c->x0);
      ch[i] = (float)(c->y1 - c->y0);
      xo[i] = c->xoff;
      yo[i] = c->yoff;
      aw[i] = c->xadvance;
      cellW = std::max(cellW, cw[i]);
    }
    std::vector<uint8_t> rgba((size_t)W * H * 4, 0);
    for (int i = 0; i < W * H; i++) {
      rgba[i * 4 + 0] = 255;
      rgba[i * 4 + 1] = 255;
      rgba[i * 4 + 2] = 255;
      rgba[i * 4 + 3] = bit[i];
    }
    tex = makeTex2D(W, H, rgba.data());
    storeCpu(cdata, first);
    return true;
  }
  std::vector<float> u0, v0, u1, v1, adv;
  int firstCh = 32;
  void storeCpu(std::vector<stbtt_bakedchar>& cd, int first) {
    firstCh = first;
    int n = (int)cd.size();
    u0.resize(n); v0.resize(n); u1.resize(n); v1.resize(n); adv.resize(n);
    for (int i = 0; i < n; i++) {
      u0[i] = cd[i].x0 / 1024.0f;
      v0[i] = cd[i].y0 / 1024.0f;
      u1[i] = cd[i].x1 / 1024.0f;
      v1[i] = cd[i].y1 / 1024.0f;
      adv[i] = (float)cd[i].xadvance;
    }
  }
  static const char* nextCp(const char* p, unsigned& cp) {
    unsigned char c = (unsigned char)*p;
    if (c < 0x80) {
      cp = c;
      return p + 1;
    }
    if ((c >> 5) == 6 && (p[1] & 0xC0) == 0x80) {
      cp = ((unsigned)(c & 31) << 6) | (unsigned)(p[1] & 63);
      return p + 2;
    }
    if ((c >> 4) == 14 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
      cp = ((unsigned)(c & 15) << 12) | ((unsigned)(p[1] & 63) << 6) | (unsigned)(p[2] & 63);
      return p + 3;
    }
    if ((c >> 3) == 30 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) {
      cp = ((unsigned)(c & 7) << 18) | ((unsigned)(p[1] & 63) << 12) | ((unsigned)(p[2] & 63) << 6) | (unsigned)(p[3] & 63);
      return p + 4;
    }
    cp = c;
    return p + 1;
  }
  float width(const char* s, float size) const {
    float k = size / px, w = 0;
    for (const char* p = s; *p;) {
      unsigned cp;
      p = nextCp(p, cp);
      if (cp < (unsigned)firstCh || cp - (unsigned)firstCh >= adv.size()) {
        w += size * 0.5f;
        continue;
      }
      w += adv[cp - (unsigned)firstCh] * k;
    }
    return w;
  }
  void draw(UiBatch& b, const char* s, float x, float y, float size, float r, float g, float bl, float a = 1.0f) const {
    float k = size / px, cx = x;
    for (const char* p = s; *p;) {
      unsigned cp;
      p = nextCp(p, cp);
      if (cp < (unsigned)firstCh || cp - (unsigned)firstCh >= adv.size()) {
        cx += size * 0.5f;
        continue;
      }
      int i = (int)(cp - (unsigned)firstCh);
      if (cw[i] > 0) {
        float gx = cx + xo[i] * k;
        float gy = y + ascent * k + yo[i] * k;
        b.quad(gx, gy, gx + cw[i] * k, gy + ch[i] * k, u0[i], v0[i], u1[i], v1[i], r, g, bl, a);
      }
      cx += adv[i] * k;
    }
  }
};

struct DayColors {
  glm::vec3 top, horizon, sun, ambSky, ambGround, fog, sunDir, blockCol;
  float night = 0, dayF = 1;
};

inline DayColors computeDay(float tod) {
  // tod in [0,1); 0 = sunrise
  float ang = tod * 6.2831853f;
  DayColors d;
  d.sunDir = normalize(glm::vec3(cosf(ang), sinf(ang), 0.35f));
  float sh = d.sunDir.y;
  float dayF = smoothstep(-0.06f, 0.22f, sh);
  float dusk = expf(-fabsf(sh) * 12.0f) * (sh > -0.25f ? 1.0f : 0.0f);
  d.dayF = dayF;
  d.night = 1.0f - dayF;
  glm::vec3 dayTop(0.28f, 0.52f, 0.95f), dayHor(0.62f, 0.80f, 0.98f);
  glm::vec3 nightTop(0.015f, 0.02f, 0.06f), nightHor(0.03f, 0.045f, 0.09f);
  glm::vec3 duskHor(0.95f, 0.48f, 0.25f), duskTop(0.25f, 0.22f, 0.4f);
  d.top = mix(nightTop, dayTop, dayF);
  d.horizon = mix(nightHor, dayHor, dayF);
  d.top = mix(d.top, duskTop, dusk * 0.7f);
  d.horizon = mix(d.horizon, duskHor, dusk * 0.85f);
  float sunI = smoothstep(-0.05f, 0.15f, sh);
  glm::vec3 sunLow(1.0f, 0.55f, 0.28f), sunHigh(1.0f, 0.96f, 0.88f);
  d.sun = mix(glm::vec3(0.12f, 0.14f, 0.2f), mix(sunLow, sunHigh, smoothstep(0.05f, 0.4f, sh)), sunI);
  d.ambSky = mix(glm::vec3(0.07f, 0.09f, 0.16f), glm::vec3(0.42f, 0.52f, 0.68f), dayF);
  d.ambGround = mix(glm::vec3(0.04f, 0.05f, 0.08f), glm::vec3(0.22f, 0.24f, 0.26f), dayF);
  d.fog = d.horizon;
  d.blockCol = glm::vec3(1.0f, 0.82f, 0.55f);
  return d;
}

struct RTTarget {
  GLuint fbo = 0, tex = 0;
  int w = 0, h = 0;
  void ensure(int nw, int nh) {
    if (nw == w && nh == h && fbo) return;
    if (fbo) { glDeleteFramebuffers(1, &fbo); glDeleteTextures(1, &tex); }
    w = nw; h = nh;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }
};

// сцена в текстуру: цвет + нормали + глубина (для пост-эффекта обводки)
struct SceneTarget {
  GLuint fbo = 0, col = 0, nrm = 0, dep = 0;
  int w = 0, h = 0;
  void ensure(int nw, int nh) {
    if (fbo && nw == w && nh == h) return;
    if (fbo) {
      glDeleteFramebuffers(1, &fbo);
      glDeleteTextures(1, &col);
      glDeleteTextures(1, &nrm);
      glDeleteTextures(1, &dep);
    }
    w = nw; h = nh;
    auto mk = [&](GLuint& t) {
      glGenTextures(1, &t);
      glBindTexture(GL_TEXTURE_2D, t);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    };
    mk(col);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    mk(nrm);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    mk(dep);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, col, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, nrm, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, dep, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
      fprintf(stderr, "scene fbo: incomplete\n");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }
  void bind() {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    GLenum bufs[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    glDrawBuffers(2, bufs);
  }
};
