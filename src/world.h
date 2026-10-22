#pragma once
#include "blocks.h"
#include "common.h"
#include "noise.h"

struct Chunk {
  int cx = 0, cz = 0;
  uint16_t blocks[CH * CH * WH];
  uint8_t light[CH * CH * WH];
  int hmax = 0;  // самый высокий непустой блок (для ускорения света/меша)
  bool gen = false;
  bool meshDirty = true;
  bool meshed = false;
  GLuint vao[3] = {0, 0, 0};
  GLuint vbo[3] = {0, 0, 0};
  int nv[3] = {0, 0, 0};
  Chunk() {
    memset(blocks, 0, sizeof(blocks));
    memset(light, 0, sizeof(light));
  }
  ~Chunk() {
    for (int i = 0; i < 3; i++)
      if (vao[i]) {
        glDeleteVertexArrays(1, &vao[i]);
        glDeleteBuffers(1, &vbo[i]);
      }
  }
};

inline uint64_t chunkKey(int cx, int cz) { return ((uint64_t)(uint32_t)cx << 32) | (uint32_t)cz; }

class World {
 public:
  std::unordered_map<uint64_t, Chunk*> chunks;
  int renderDist = RENDER_DIST;

  Chunk* chunkAt(int cx, int cz) {
    auto it = chunks.find(chunkKey(cx, cz));
    return it == chunks.end() ? nullptr : it->second;
  }

  uint16_t getBlock(int x, int y, int z) const {
    if (y < 0 || y >= WH) return B_AIR;
    auto it = chunks.find(chunkKey(floorDiv(x, CH), floorDiv(z, CH)));
    if (it == chunks.end() || !it->second->gen) return B_AIR;
    return it->second->blocks[chunkIdx(mod(x, CH), y, mod(z, CH))];
  }

  uint8_t getLight(int x, int y, int z) const {
    if (y < 0 || y >= WH) return 0;
    auto it = chunks.find(chunkKey(floorDiv(x, CH), floorDiv(z, CH)));
    if (it == chunks.end() || !it->second->gen) return 0;
    return it->second->light[chunkIdx(mod(x, CH), y, mod(z, CH))];
  }

  // Высота в вокселях. Мир стал вдвое «плотнее» (воксель в 2 раза меньше),
  // поэтому частоты шума вдвое меньше, а итоговая высота вдвое больше.
  float terrainHeight(int wx, int wz) const {
    float cont = nz::fbm2(wx * 0.00155f, wz * 0.00155f, 4);
    float hills = nz::fbm2(wx * 0.0065f + 100.0f, wz * 0.0065f + 100.0f, 4);
    float detail = nz::fbm2(wx * 0.0275f + 210.0f, wz * 0.0275f + 210.0f, 3);
    float mRaw = nz::fbm2(wx * 0.0011f + 555.0f, wz * 0.0011f + 555.0f, 3);
    float mMask = smoothstep(0.10f, 0.52f, mRaw);
    float rid = nz::ridged2(wx * 0.00425f + 77.0f, wz * 0.00425f + 77.0f, 5);
    float h = 30.0f + cont * 18.0f + hills * 7.0f + detail * 1.6f + mMask * rid * rid * 78.0f;
    // террасирование склонов для ступенчатых холмов
    float terr = smoothstep(0.35f, 0.75f, 1.0f - mMask) * smoothstep(0.15f, 0.6f, fabsf(hills));
    float step = 2.0f;
    h = mix(h, floorf(h / step) * step + step * 0.5f, terr * 0.6f);
    float rv = nz::fbm2(wx * 0.0021f + 999.0f, wz * 0.0021f + 999.0f, 3);
    float river = 1.0f - smoothstep(0.0f, 0.030f, fabsf(rv));
    river *= (1.0f - mMask * 0.85f);
    h = mix(h, 29.5f, river * 0.95f);
    // озёра в низинах
    float lake = nz::fbm2(wx * 0.00375f + 31337.0f, wz * 0.00375f + 31337.0f, 3);
    float lakeMask = smoothstep(0.34f, 0.55f, lake) * (1.0f - mMask);
    h = mix(h, 29.0f, lakeMask * 0.92f);
    return h * 2.0f;  // воксель вдвое мельче
  }

  // 0=пляж/низина, 1=равнина, 2=лес, 3=пустыня, 4=снежные горы
  int biomeAt(int wx, int wz) const {
    float t = nz::fbm2(wx * 0.0013f + 4242.0f, wz * 0.0013f + 4242.0f, 3);
    float h = terrainHeight(wx, wz);
    if (h >= 124.0f) return 4;
    if (h <= (float)WATER_L + 2) return 0;
    if (t > 0.24f) return 3;
    if (t < -0.20f) return 2;
    return 1;
  }

  float forestDensity(int wx, int wz) const {
    float f = nz::fbm2(wx * 0.0031f + 7000.0f, wz * 0.0031f + 7000.0f, 3);
    float dens = smoothstep(-0.12f, 0.42f, f);
    float clearing = nz::fbm2(wx * 0.0135f + 300.0f, wz * 0.0135f + 300.0f, 2);
    if (clearing < -0.15f) dens *= 0.05f;
    return dens;
  }

  void genTerrain(Chunk* c) {
    int ox = c->cx * CH, oz = c->cz * CH;
    for (int lz = 0; lz < CH; lz++)
      for (int lx = 0; lx < CH; lx++) fillColumn(c, ox + lx, oz + lz, lx, lz);
    decorate(c);
    c->gen = true;
    recomputeLight(c, 0);
  }

  void setBlock(int x, int y, int z, uint16_t id) {
    if (y < 0 || y >= WH) return;
    int cx = floorDiv(x, CH), cz = floorDiv(z, CH);
    Chunk* c = chunkAt(cx, cz);
    if (!c || !c->gen) return;
    int lx = mod(x, CH), lz = mod(z, CH);
    int i = chunkIdx(lx, y, lz);
    if (c->blocks[i] == id) return;
    c->blocks[i] = id;
    if (id != B_AIR && y > c->hmax) c->hmax = y;
    queueLight(c, 0);
    markDirtyAround(x, y, z, cx, cz, lx, lz);
  }

  bool raycast(const glm::vec3& ro, const glm::vec3& rdIn, float maxD, Hit& out);

  int surfaceY(int x, int z) {
    Chunk* c = chunkAt(floorDiv(x, CH), floorDiv(z, CH));
    if (!c) return -1;
    int lx = mod(x, CH), lz = mod(z, CH);
    for (int y = WH - 1; y >= 0; y--) {
      uint16_t b = c->blocks[chunkIdx(lx, y, lz)];
      if (b != B_AIR && b != B_WATER) return y;
    }
    return -1;
  }

  struct LoadOpt {
    int cx, cz;
    float d2;
  };
  std::vector<LoadOpt> offsets;
  int lastOCx = 1 << 30, lastOCz = 1 << 30;

  void rebuildOffsets(int pcx, int pcz) {
    offsets.clear();
    int R = renderDist + 1;
    for (int dz = -R; dz <= R; dz++)
      for (int dx = -R; dx <= R; dx++) {
        float d = (float)(dx * dx + dz * dz);
        offsets.push_back({pcx + dx, pcz + dz, d});
      }
    std::sort(offsets.begin(), offsets.end(), [](const LoadOpt& a, const LoadOpt& b) { return a.d2 < b.d2; });
    lastOCx = pcx;
    lastOCz = pcz;
  }

  void stream(const glm::vec3& pos, double budgetMs, const std::function<void(Chunk*)>& onMesh) {
    double t0 = nowSec();
    double budget = budgetMs / 1000.0;
    int pcx = floorDiv(w2g(pos.x), CH), pcz = floorDiv(w2g(pos.z), CH);
    if (pcx != lastOCx || pcz != lastOCz) rebuildOffsets(pcx, pcz);
    // отложенная очередь света: разбираем до меша, чтобы чанк сразу пересобрался с новым светом
    while (!lightJobs.empty()) {
      if (nowSec() - t0 >= budget) break;
      LightJob j = lightJobs.front();
      lightJobs.pop_front();
      Chunk* c = chunkAt(j.cx, j.cz);
      if (!c || !c->gen) continue;
      recomputeLight(c, j.depth);
      c->meshDirty = true;
    }
    // меш раньше генерации: сломанный блок/свет пересобираются в том же кадре
    int meshN = 0, genN = 0;
    for (auto& o : offsets) {
      if (std::max(std::abs(o.cx - pcx), std::abs(o.cz - pcz)) > renderDist) continue;
      Chunk* c = chunkAt(o.cx, o.cz);
      if (!c || !c->gen) continue;
      if (c->meshed && !c->meshDirty) continue;
      if (meshN > 0 && nowSec() - t0 >= budget) break;
      if (meshN > 0 && c->meshed && nowSec() - t0 + meshEst > budget) break;  // первый меш за вызов всегда можно
      bool ok = true;
      for (int dz = -1; dz <= 1 && ok; dz++)
        for (int dx = -1; dx <= 1 && ok; dx++) {
          if (!dx && !dz) continue;
          Chunk* n = chunkAt(o.cx + dx, o.cz + dz);
          if (!n || !n->gen) ok = false;
        }
      if (!ok) continue;
      double m0 = nowSec();
      onMesh(c);
      double m = nowSec() - m0;
      meshEst += (m - meshEst) * 0.3;
      meshN++;
      if (nowSec() - t0 > budget) break;
    }
    for (auto& o : offsets) {
      if (chunkAt(o.cx, o.cz)) continue;
      if (genN > 0 && nowSec() - t0 >= budget) break;
      if (genN > 0 && nowSec() - t0 + genEst > budget) break;  // первая генерация за вызов всегда может стартовать
      double g0 = nowSec();
      Chunk* c = new Chunk();
      c->cx = o.cx;
      c->cz = o.cz;
      chunks[chunkKey(o.cx, o.cz)] = c;
      genTerrain(c);
      double g = nowSec() - g0;
      genEst += (g - genEst) * 0.3;
      genN++;
      if (nowSec() - t0 > budget) break;
    }
    int lim = renderDist + 3;
    for (auto it = chunks.begin(); it != chunks.end();) {
      Chunk* c = it->second;
      if (std::abs(c->cx - pcx) > lim || std::abs(c->cz - pcz) > lim) {
        if (!lightJobs.empty()) {
          // не выгружаем чанки с неразобранной очередью света
          bool pending = false;
          for (auto& j : lightJobs)
            if (j.cx == c->cx && j.cz == c->cz) pending = true;
          if (pending) {
            ++it;
            continue;
          }
        }
        delete c;
        it = chunks.erase(it);
      } else
        ++it;
    }
  }

  void fillVolume(int ox, int oy, int oz, int sx, int sy, int sz, uint8_t* data) {
    memset(data, 0, (size_t)sx * sy * sz * 3);
    int cx0 = floorDiv(ox, CH), cz0 = floorDiv(oz, CH);
    int ncx = sx / CH, ncz = sz / CH;
    for (int cz = 0; cz < ncz; cz++)
      for (int cx = 0; cx < ncx; cx++) {
        auto it = chunks.find(chunkKey(cx0 + cx, cz0 + cz));
        if (it == chunks.end() || !it->second->gen) continue;
        Chunk* ch = it->second;
        for (int y = 0; y < sy && y < WH; y++)
          for (int z = 0; z < CH; z++)
            for (int x = 0; x < CH; x++) {
              size_t i = ((size_t)(cz * CH + z) * sy + y) * sx + (cx * CH + x);
              int li = chunkIdx(x, y, z);
              data[i * 3 + 0] = (uint8_t)ch->blocks[li];
              uint8_t l = ch->light[li];
              data[i * 3 + 1] = (uint8_t)((l >> 4) * 17);
              data[i * 3 + 2] = (uint8_t)((l & 15) * 17);
            }
      }
  }

 private:
  void setLocal(Chunk* c, int lx, int y, int lz, uint16_t id) {
    if (lx < 0 || lx >= CH || lz < 0 || lz >= CH || y < 0 || y >= WH) return;
    c->blocks[chunkIdx(lx, y, lz)] = id;
    if (id != B_AIR && y > c->hmax) c->hmax = y;
  }
  uint16_t getLocal(const Chunk* c, int lx, int y, int lz) const {
    if (lx < 0 || lx >= CH || lz < 0 || lz >= CH || y < 0 || y >= WH) return B_AIR;
    return c->blocks[chunkIdx(lx, y, lz)];
  }
  bool isOpaqueLocal(const Chunk* c, int lx, int y, int lz) const {
    return isOpaqueId(getLocal(c, lx, y, lz));
  }
  void fillColumn(Chunk* c, int wx, int wz, int lx, int lz);
  void decorate(Chunk* c);
  void placeTree(Chunk* c, int wx, int wz, int groundY);
  void recomputeLight(Chunk* c, int depth);
  void markDirtyAround(int x, int y, int z, int cx, int cz, int lx, int lz);
  struct LightJob {
    int cx, cz, depth;
  };
  std::deque<LightJob> lightJobs;
  double genEst = 0.004;
  double meshEst = 0.0;
  void queueLight(Chunk* c, int depth);
};

inline void World::markDirtyAround(int x, int y, int z, int cx, int cz, int lx, int lz) {
  (void)y;
  auto dirty = [&](int ncx, int ncz) {
    Chunk* n = chunkAt(ncx, ncz);
    if (n) n->meshDirty = true;
  };
  dirty(cx, cz);
  if (lx == 0) dirty(cx - 1, cz);
  if (lx == CH - 1) dirty(cx + 1, cz);
  if (lz == 0) dirty(cx, cz - 1);
  if (lz == CH - 1) dirty(cx, cz + 1);
  if (lx == 0 && lz == 0) dirty(cx - 1, cz - 1);
  if (lx == CH - 1 && lz == 0) dirty(cx + 1, cz - 1);
  if (lx == 0 && lz == CH - 1) dirty(cx - 1, cz + 1);
  if (lx == CH - 1 && lz == CH - 1) dirty(cx + 1, cz + 1);
}

inline void World::fillColumn(Chunk* c, int wx, int wz, int lx, int lz) {
  float hf = terrainHeight(wx, wz);
  int h = (int)floorf(hf);
  h = std::max(4, std::min(h, WH - 24));
  int biome = biomeAt(wx, wz);
  bool beach = h <= WATER_L + 2;
  bool alpine = h >= 120;
  bool desert = biome == 3;
  bool snowy = biome == 4;
  for (int y = 0; y <= h; y++) {
    uint16_t id;
    if (y == 0)
      id = B_BEDROCK;
    else if (y == h) {
      if (beach)
        id = B_SAND;
      else if (desert)
        id = B_SAND;
      else if (snowy || alpine)
        id = (h >= 128 || snowy) ? B_SNOW : B_STONE;
      else
        id = B_GRASS;
    } else if (y > h - 8) {
      id = (beach || desert) ? B_SAND : ((snowy || alpine) ? B_STONE : B_DIRT);
    } else {
      id = B_STONE;
    }
    if (y > 2 && y < h) {
      float worm = fabsf(nz::fbm3(wx * 0.0085f, y * 0.015f, wz * 0.0085f, 3));
      float cheese = nz::fbm3(wx * 0.00525f + 400.0f, y * 0.01f + 400.0f, wz * 0.00525f + 400.0f, 3);
      bool cave = false;
      if (worm < 0.05f && y < h - 4) cave = true;
      if (cheese > 0.60f && y > 6 && y < h - 12) cave = true;
      if (cave && !(hf < WATER_L + 4.0f && y > h - 10)) id = B_AIR;
    }
    if (id == B_STONE) {
      float on = nz::fbm3(wx * 0.0425f, y * 0.0425f, wz * 0.0425f, 2);
      if (on > 0.55f && y < 104 && y > 4) id = B_COALORE;
      float in = nz::fbm3(wx * 0.0475f + 80.0f, y * 0.0475f + 80.0f, wz * 0.0475f + 80.0f, 2);
      if (in > 0.62f && y < 52 && y > 2) id = B_IRONORE;
    }
    if (id != B_AIR) setLocal(c, lx, y, lz, id);
  }
  for (int y = h + 1; y <= WATER_L; y++) setLocal(c, lx, y, lz, B_WATER);
}

inline void World::placeTree(Chunk* c, int wx, int wz, int groundY) {
  bool big = nz::srandf(wx, wz, 31) > 0.82f;
  int th = big ? (14 + (int)(nz::srandf(wx, wz, 55) * 6.0f)) : (8 + (int)(nz::srandf(wx, wz, 55) * 6.0f));
  int tlx = wx - c->cx * CH, tlz = wz - c->cz * CH;
  for (int i = 0; i < th; i++) setLocal(c, tlx, groundY + 1 + i, tlz, B_LOG);
  int top = groundY + th;
  int baseR = big ? 5 : 4;
  for (int dy = -4; dy <= 2; dy++) {
    int r = (dy >= 0) ? 2 : baseR;
    if (big && dy == -4) r = baseR - 1;
    for (int dx = -r; dx <= r; dx++)
      for (int dz = -r; dz <= r; dz++) {
        if (dx * dx + dz * dz > r * r + 1) continue;
        if (dy == 2 && std::abs(dx) + std::abs(dz) > 2) continue;
        int lx = tlx + dx, lz = tlz + dz, y = top + dy + 1;
        if (lx < 0 || lx >= CH || lz < 0 || lz >= CH || y < 0 || y >= WH) continue;
        if (getLocal(c, lx, y, lz) == B_AIR) setLocal(c, lx, y, lz, B_LEAVES);
      }
  }
}

inline void World::decorate(Chunk* c) {
  const int CELL = 10;  // воксель вдвое мельче — ячейка деревьев вдвое шире
  int ox = c->cx * CH, oz = c->cz * CH;
  int gx0 = floorDiv(ox - 6, CELL), gx1 = floorDiv(ox + CH + 6, CELL);
  int gz0 = floorDiv(oz - 6, CELL), gz1 = floorDiv(oz + CH + 6, CELL);
  for (int gz = gz0; gz <= gz1; gz++)
    for (int gx = gx0; gx <= gx1; gx++) {
      float r1 = nz::srandf(gx, gz);
      float r2 = nz::srandf(gx + 1013, gz - 77);
      float r3 = nz::srandf(gx - 311, gz + 991);
      int wx = gx * CELL + (int)(r1 * CELL);
      int wz = gz * CELL + (int)(r2 * CELL);
      float dens = forestDensity(wx, wz);
      if (r3 > dens * 0.62f) continue;
      int h = (int)floorf(terrainHeight(wx, wz));
      if (h < WATER_L + 4 || h >= 132) continue;
      int lx = wx - ox, lz = wz - oz;
      if (lx >= 0 && lx < CH && lz >= 0 && lz < CH && getLocal(c, lx, h, lz) != B_GRASS) continue;
      placeTree(c, wx, wz, h);
    }
  // цветы и трава: 5 видов цветков, в небольшом смещении палитры
  for (int lz = 0; lz < CH; lz++)
    for (int lx = 0; lx < CH; lx++) {
      int wx = ox + lx, wz = oz + lz;
      int h = (int)floorf(terrainHeight(wx, wz));
      if (h < 4 || h + 1 >= WH) continue;
      if (getLocal(c, lx, h, lz) != B_GRASS) continue;
      if (getLocal(c, lx, h + 1, lz) != B_AIR) continue;
      float dens = forestDensity(wx, wz);
      float rr = nz::srandf(wx, wz, 777);
      int biome = biomeAt(wx, wz);
      bool flowering = biome == 1 || biome == 2;
      if (flowering && rr < 0.09f + dens * 0.15f) {
        float f2 = nz::srandf(wx, wz, 999);
        uint16_t id;
        if (f2 < 0.24f)
          id = B_FRED;
        else if (f2 < 0.44f)
          id = B_FYELLOW;
        else if (f2 < 0.64f)
          id = B_FBLUE;
        else if (f2 < 0.84f)
          id = B_FWHITE;
        else
          id = B_FVIOLET;
        setLocal(c, lx, h + 1, lz, id);
      } else if (rr < 0.20f + dens * 0.30f) {
        setLocal(c, lx, h + 1, lz, B_TALL);
      }
    }
}

inline void World::recomputeLight(Chunk* c, int depth) {
  static thread_local std::vector<int> queue;
  queue.clear();
  int top = std::min(WH - 1, c->hmax + 1);
  for (int lz = 0; lz < CH; lz++)
    for (int lx = 0; lx < CH; lx++)
      for (int y = top; y >= 0; y--) {
        int i = chunkIdx(lx, y, lz);
        if (isOpaqueLocal(c, lx, y, lz)) break;
        c->light[i] = 0xF0;
        queue.push_back(i);
      }
  for (int y = 0; y <= top; y++)
    for (int lz = 0; lz < CH; lz++)
      for (int lx = 0; lx < CH; lx++) {
        int i = chunkIdx(lx, y, lz);
        uint16_t b = c->blocks[i];
        if (b != B_AIR && bdef(b).emit > 0) {
          c->light[i] = (uint8_t)((c->light[i] & 0xF0) | bdef(b).emit);
          queue.push_back(i);
        }
      }
  auto seedBorder = [&](int lx, int lz, int nx, int nz) {
    Chunk* nc = chunkAt(c->cx + nx, c->cz + nz);
    if (!nc || !nc->gen) return;
    int nlx = mod(lx + nx, CH), nlz = mod(lz + nz, CH);
    int yb = std::min(WH - 1, std::max(top, nc->hmax + 1) + 1);
    for (int y = 0; y <= yb; y++) {
      uint8_t nl = nc->light[chunkIdx(nlx, y, nlz)];
      if (nl == 0) continue;
      int i = chunkIdx(lx, y, lz);
      if (isOpaqueLocal(c, lx, y, lz)) continue;
      uint8_t cur = c->light[i];
      uint8_t merged = (uint8_t)(std::max((uint8_t)(cur & 0xF0), (uint8_t)(nl & 0xF0)) |
                                 std::max((uint8_t)(cur & 0x0F), (uint8_t)(nl & 0x0F)));
      if (merged != cur) {
        c->light[i] = merged;
        queue.push_back(i);
      }
    }
  };
  for (int l = 0; l < CH; l++) {
    seedBorder(0, l, -1, 0);
    seedBorder(CH - 1, l, 1, 0);
    seedBorder(l, 0, 0, -1);
    seedBorder(l, CH - 1, 0, 1);
  }
  size_t qh = 0;
  static const int dirs[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
  while (qh < queue.size()) {
    int i = queue[qh++];
    int y = i / (CH * CH);
    int rem = i % (CH * CH);
    int lz = rem / CH, lx = rem % CH;
    uint8_t l = c->light[i];
    uint8_t sk = (uint8_t)(l >> 4), bl = (uint8_t)(l & 15);
    for (auto& d : dirs) {
      int nx = lx + d[0], ny = y + d[1], nz = lz + d[2];
      if (nx < 0 || nx >= CH || nz < 0 || nz >= CH || ny < 0 || ny >= WH) continue;
      int j = chunkIdx(nx, ny, nz);
      if (isOpaqueLocal(c, nx, ny, nz)) continue;
      if (sk > 0) {
        uint8_t ns = (d[1] == -1) ? sk : (uint8_t)(sk - 1);
        uint8_t cur = (uint8_t)(c->light[j] >> 4);
        if (ns > cur) {
          uint8_t nv = (uint8_t)((ns << 4) | (c->light[j] & 0x0F));
          if (nv > c->light[j]) {
            c->light[j] = nv;
            queue.push_back(j);
          }
        }
      }
      if (bl > 1) {
        uint8_t nb = (uint8_t)(bl - 1);
        uint8_t cur = (uint8_t)(c->light[j] & 0x0F);
        if (nb > cur) {
          uint8_t nv = (uint8_t)((c->light[j] & 0xF0) | nb);
          if (nv > c->light[j]) {
            c->light[j] = nv;
            queue.push_back(j);
          }
        }
      }
    }
  }
  if (depth >= 4) return;
  auto mismatch = [&](Chunk* n, int dx, int dz) {
    int ytop = std::min(top, n->hmax + 1);
    for (int l = 0; l < CH; l++)
      for (int y = 0; y <= ytop; y++) {
        int ax = dx > 0 ? CH - 1 : 0, az = dz > 0 ? CH - 1 : 0;
        int bx = dx > 0 ? 0 : CH - 1, bz = dz > 0 ? 0 : CH - 1;
        uint8_t la, lb;
        if (dx != 0) {
          la = c->light[chunkIdx(ax, y, l)];
          lb = n->light[chunkIdx(bx, y, l)];
        } else {
          la = c->light[chunkIdx(l, y, az)];
          lb = n->light[chunkIdx(l, y, bz)];
        }
        int dsk = std::abs((int)(la >> 4) - (int)(lb >> 4));
        int dbl = std::abs((int)(la & 15) - (int)(lb & 15));
        if (dsk > 1 || dbl > 1) return true;
      }
    return false;
  };
  for (auto& d : dirs) {
    if (d[1] != 0) continue;
    Chunk* n = chunkAt(c->cx + d[0], c->cz + d[2]);
    if (!n || !n->gen) continue;
    if (mismatch(n, d[0], d[2])) {
      n->meshDirty = true;  // свет соседа изменился — его меш надо пересобрать
      queueLight(n, depth + 1);
    }
  }
}

inline void World::queueLight(Chunk* c, int depth) {
  for (auto& j : lightJobs) {
    if (j.cx == c->cx && j.cz == c->cz) {
      if (j.depth <= depth) return;
      j.depth = depth;
      return;
    }
  }
  lightJobs.push_back({c->cx, c->cz, depth});
}

inline bool World::raycast(const glm::vec3& roW, const glm::vec3& rdIn, float maxDW, Hit& out) {
  glm::vec3 rd = normalize(rdIn);
  // трассировка идёт в воксельном пространстве, наружу — мировые юниты
  glm::vec3 ro = roW * (1.0f / VX);
  float maxD = maxDW / VX;
  int x = (int)floorf(ro.x), y = (int)floorf(ro.y), z = (int)floorf(ro.z);
  int sx = rd.x > 0 ? 1 : -1, sy = rd.y > 0 ? 1 : -1, sz = rd.z > 0 ? 1 : -1;
  auto safeInv = [](float v) { return 1.0f / (fabsf(v) < 1e-9f ? (v < 0 ? -1e-9f : 1e-9f) : v); };
  float ix = safeInv(rd.x), iy = safeInv(rd.y), iz = safeInv(rd.z);
  float tMaxX = ((rd.x > 0 ? x + 1.0f : (float)x) - ro.x) * ix;
  float tMaxY = ((rd.y > 0 ? y + 1.0f : (float)y) - ro.y) * iy;
  float tMaxZ = ((rd.z > 0 ? z + 1.0f : (float)z) - ro.z) * iz;
  float tDX = fabsf(ix), tDY = fabsf(iy), tDZ = fabsf(iz);
  int nx = 0, ny = 0, nz = 0;
  float t = 0;
  for (int i = 0; i < 256; i++) {
    uint16_t b = getBlock(x, y, z);
    if (b != B_AIR) {
      out.hit = true;
      out.x = x;
      out.y = y;
      out.z = z;
      out.nx = nx;
      out.ny = ny;
      out.nz = nz;
      out.id = b;
      out.dist = t * VX;
      return true;
    }
    if (tMaxX < tMaxY) {
      if (tMaxX < tMaxZ) {
        x += sx;
        t = tMaxX;
        tMaxX += tDX;
        nx = -sx;
        ny = 0;
        nz = 0;
      } else {
        z += sz;
        t = tMaxZ;
        tMaxZ += tDZ;
        nx = 0;
        ny = 0;
        nz = -sz;
      }
    } else {
      if (tMaxY < tMaxZ) {
        y += sy;
        t = tMaxY;
        tMaxY += tDY;
        nx = 0;
        ny = -sy;
        nz = 0;
      } else {
        z += sz;
        t = tMaxZ;
        tMaxZ += tDZ;
        nx = 0;
        ny = 0;
        nz = -sz;
      }
    }
    if (t > maxD) break;
    if (y < -8 || y > WH + 8) break;
  }
  out.hit = false;
  return false;
}

