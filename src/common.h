#pragma once
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

using namespace std::chrono;
using namespace glm;

constexpr int CH = 40;         // вокселей в чанке: 40^3 = 64000 кубов (было 32768)
constexpr float VX = 0.8f;     // мировой размер ребра вокселя: чанк CH*VX = 32 мира, куб чуть меньше => герой крупнее
constexpr int WH = 256;        // высота мира в вокселях
constexpr int WATER_L = 64;    // уровень воды
constexpr int RENDER_DIST = 4; // чанков вокруг игрока (~128 мировых юнитов обзора)
constexpr int ATLAS_TILES = 8;
constexpr float TILE_UV = 1.0f / ATLAS_TILES;

// мир (float, юниты = старые блоки) <-> воксельная сетка (int)
inline int w2g(float w) { return (int)floorf(w / VX); }
inline float g2w(int g) { return (float)g * VX; }

inline double nowSec() {
  return duration<double>(steady_clock::now().time_since_epoch()).count();
}

inline int floorDiv(int a, int b) {
  int q = a / b;
  if ((a % b != 0) && ((a < 0) != (b < 0))) q--;
  return q;
}
inline int mod(int a, int b) {
  int r = a % b;
  return r < 0 ? r + b : r;
}

inline int chunkIdx(int lx, int ly, int lz) { return (ly * CH + lz) * CH + lx; }

struct Hit {
  int x = 0, y = 0, z = 0;
  int nx = 0, ny = 0, nz = 0;
  uint16_t id = 0;
  float dist = 0;
  bool hit = false;
};
