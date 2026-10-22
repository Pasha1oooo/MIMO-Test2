#pragma once
#include "blocks.h"
#include "world.h"

struct MVtx {
  float x, y, z, u, v, sky, blk, nx, ny, nz;
};

struct MesherAccess {
  Chunk* nb[3][3] = {{nullptr}};
  MesherAccess(World& w, int cx, int cz) {
    for (int j = -1; j <= 1; j++)
      for (int i = -1; i <= 1; i++) nb[j + 1][i + 1] = w.chunkAt(cx + i, cz + j);
  }
  inline uint16_t gb(int lx, int ly, int lz) const {
    if (ly < 0 || ly >= WH) return B_AIR;
    int ci = lx < 0 ? -1 : (lx >= CH ? 1 : 0);
    int cj = lz < 0 ? -1 : (lz >= CH ? 1 : 0);
    Chunk* c = nb[cj + 1][ci + 1];
    if (!c || !c->gen) return B_AIR;
    return c->blocks[chunkIdx(lx - ci * CH, ly, lz - cj * CH)];
  }
  inline uint8_t gl(int lx, int ly, int lz) const {
    if (ly < 0 || ly >= WH) return 0;
    int ci = lx < 0 ? -1 : (lx >= CH ? 1 : 0);
    int cj = lz < 0 ? -1 : (lz >= CH ? 1 : 0);
    Chunk* c = nb[cj + 1][ci + 1];
    if (!c || !c->gen) return 0;
    return c->light[chunkIdx(lx - ci * CH, ly, lz - cj * CH)];
  }
  inline bool op(int lx, int ly, int lz) const { return isOpaqueId(gb(lx, ly, lz)); }
};

struct MeshOut {
  std::vector<MVtx> solid, water, cross;
};

static const float AO_LEVELS[4] = {0.45f, 0.65f, 0.82f, 1.0f};

inline void pushQuad(std::vector<MVtx>& out, MVtx v[4], float tile, const float fuv[4][2]) {
  int col = (int)tile % ATLAS_TILES, row = (int)tile / ATLAS_TILES;
  auto fix = [&](MVtx& v, const float* uv) {
    v.u = (col + uv[0]) * TILE_UV;
    v.v = (row + uv[1]) * TILE_UV;
    v.x *= VX;  // воксельная сетка -> мировые юниты (куб чуть меньше мира)
    v.y *= VX;
    v.z *= VX;
  };
  fix(v[0], fuv[0]);
  fix(v[1], fuv[1]);
  fix(v[2], fuv[2]);
  fix(v[3], fuv[3]);
  out.push_back(v[0]);
  out.push_back(v[1]);
  out.push_back(v[2]);
  out.push_back(v[0]);
  out.push_back(v[2]);
  out.push_back(v[3]);
}

inline void buildChunkMesh(World& w, Chunk* c, MeshOut& out) {
  MesherAccess A(w, c->cx, c->cz);
  static const int dirs[6][3] = {{0, 1, 0}, {0, -1, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}};
  int ytop = std::min(WH - 1, c->hmax + 1);
  for (int y = 0; y <= ytop; y++)
    for (int lz = 0; lz < CH; lz++)
      for (int lx = 0; lx < CH; lx++) {
        uint16_t id = c->blocks[chunkIdx(lx, y, lz)];
        if (id == B_AIR) continue;
        const BlockDef& D = bdef(id);
        if (D.cross) {
          uint8_t L = A.gl(lx, y, lz);
          float sk = (L >> 4) / 15.0f, bl = (L & 15) / 15.0f;
          const float uv[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
          for (int k = 0; k < 2; k++) {
            float a = k == 0 ? 0.15f : 0.85f, b = k == 0 ? 0.85f : 0.15f;
            MVtx v[4] = {
                {(float)lx + a, (float)y, (float)lz + a, 0, 0, sk, bl, 0, 1, 0},
                {(float)lx + b, (float)y, (float)lz + b, 0, 0, sk, bl, 0, 1, 0},
                {(float)lx + b, (float)y + 1.0f, (float)lz + b, 0, 0, sk, bl, 0, 1, 0},
                {(float)lx + a, (float)y + 1.0f, (float)lz + a, 0, 0, sk, bl, 0, 1, 0},
            };
            pushQuad(out.cross, v, (float)D.tTop, uv);
          }
          continue;
        }
        if (D.liquid) {
          uint8_t L = A.gl(lx, y, lz);
          float sk = (L >> 4) / 15.0f, bl = (L & 15) / 15.0f;
          float topY = (A.gb(lx, y + 1, lz) == B_AIR) ? 0.88f : 1.0f;
          const float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
          for (int f = 0; f < 6; f++) {
            int dx = dirs[f][0], dy = dirs[f][1], dz = dirs[f][2];
            uint16_t nb = A.gb(lx + dx, y + dy, lz + dz);
            if (isOpaqueId(nb) || nb == B_WATER) continue;
            if (dy == 1 && nb != B_AIR && !isSolidId(nb)) {
            }
            float sh = dy == 1 ? 1.0f : (dy == -1 ? 0.6f : 0.85f);
            MVtx v[4];
            glm::vec3 pos[4];
            int ax = dx != 0 ? 0 : (dy != 0 ? 1 : 2);
            for (int k = 0; k < 4; k++) {
              float fu = k == 1 || k == 2 ? 1.0f : 0.0f;
              float fv = k == 2 || k == 3 ? 1.0f : 0.0f;
              glm::vec3 p((float)lx, (float)y, (float)lz);
              if (ax == 0) {
                p.x = dx > 0 ? 1.0f : 0.0f;
                p.z = fu;
                p.y = fv * topY;
              } else if (ax == 1) {
                p.y = dy > 0 ? topY : 0.0f;
                p.x = fu;
                p.z = fv;
              } else {
                p.z = dz > 0 ? 1.0f : 0.0f;
                p.x = fu;
                p.y = fv * topY;
              }
              pos[k] = p;
            }
            if (ax == 1 && dy > 0) std::swap(pos[1], pos[3]);
            if (ax == 0 && dx > 0) std::swap(pos[1], pos[3]);
            if (ax == 2 && dz < 0) std::swap(pos[1], pos[3]);
            glm::vec3 nrm((float)dx, (float)dy, (float)dz);
            for (int k = 0; k < 4; k++)
              v[k] = {(float)lx + pos[k].x, y + pos[k].y, (float)lz + pos[k].z, 0, 0, sk * sh, bl, nrm.x, nrm.y, nrm.z};
            pushQuad(out.water, v, (float)D.tTop, uv);
          }
          continue;
        }
        for (int f = 0; f < 6; f++) {
          int dx = dirs[f][0], dy = dirs[f][1], dz = dirs[f][2];
          uint16_t nb = A.gb(lx + dx, y + dy, lz + dz);
          bool draw = !isOpaqueId(nb);
          if (id == B_LEAVES && nb == B_LEAVES) draw = false;
          if (id == B_GLASS && nb == B_GLASS) draw = false;
          if (!draw) continue;
          int ax = dx != 0 ? 0 : (dy != 0 ? 1 : 2);
          int sign = (dx + dy + dz);
          float shade = dy == 1 ? 1.0f : (dy == -1 ? 0.55f : (ax == 0 ? 0.72f : 0.86f));
          uint8_t L = A.gl(lx + dx, y + dy, lz + dz);
          float baseSky = (L >> 4) / 15.0f;
          float baseBlk = (L & 15) / 15.0f;
          int uax = (ax + 1) % 3, vax = (ax + 2) % 3;
          float tile = dy == 1 ? (float)D.tTop : (dy == -1 ? (float)D.tBot : (float)D.tSide);
          glm::vec3 nrm((float)dx, (float)dy, (float)dz);
          MVtx vs[4];
          float uvs[4][2];
          int order[4] = {0, 1, 2, 3};
          if (sign < 0) {
            order[1] = 3;
            order[3] = 1;
          }
          for (int k = 0; k < 4; k++) {
            int ci = order[k];
            float fu = (ci == 1 || ci == 2) ? 1.0f : 0.0f;
            float fv = (ci == 2 || ci == 3) ? 1.0f : 0.0f;
            glm::vec3 p((float)lx, (float)y, (float)lz);
            p[ax] += sign > 0 ? 1.0f : 0.0f;
            p[uax] += fu;
            p[vax] += fv;
            // AO
            int s1u = fu > 0.5f ? 1 : -1, s1v = fv > 0.5f ? 1 : -1;
            glm::ivec3 air((float)lx + dx, (float)y + dy, (float)lz + dz);
            glm::ivec3 p1 = air;
            p1[uax] += s1u;
            glm::ivec3 p2 = air;
            p2[vax] += s1v;
            glm::ivec3 p3 = p1;
            p3[vax] += s1v;
            int a1 = A.op(p1.x, p1.y, p1.z) ? 1 : 0;
            int a2 = A.op(p2.x, p2.y, p2.z) ? 1 : 0;
            int a3 = A.op(p3.x, p3.y, p3.z) ? 1 : 0;
            int ao = (a1 && a2) ? 0 : 3 - (a1 + a2 + a3);
            float tu = fu, tv = fv;
            if (ax == 0) {
              tu = fv;
              tv = 1.0f - fu;
            } else if (ax == 2) {
              tv = 1.0f - fv;
            }
            uvs[k][0] = tu;
            uvs[k][1] = tv;
            float sky = baseSky * shade * AO_LEVELS[ao];
            float blk = baseBlk * (0.6f + 0.4f * AO_LEVELS[ao]);
            vs[k] = {p.x, p.y, p.z, tu, tv, sky, blk, nrm.x, nrm.y, nrm.z};
          }
          pushQuad(out.solid, vs, tile, uvs);
        }
      }
}

inline void uploadChunkMesh(Chunk* c, int pass, std::vector<MVtx>& v) {
  if (!c->vao[pass]) {
    glGenVertexArrays(1, &c->vao[pass]);
    glGenBuffers(1, &c->vbo[pass]);
    glBindVertexArray(c->vao[pass]);
    glBindBuffer(GL_ARRAY_BUFFER, c->vbo[pass]);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(MVtx), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(MVtx), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(MVtx), (void*)(5 * sizeof(float)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(MVtx), (void*)(7 * sizeof(float)));
  }
  glBindVertexArray(c->vao[pass]);
  glBindBuffer(GL_ARRAY_BUFFER, c->vbo[pass]);
  glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(MVtx), v.empty() ? nullptr : v.data(), GL_STATIC_DRAW);
  c->nv[pass] = (int)v.size();
  glBindVertexArray(0);
}

inline void meshAndUpload(World& w, Chunk* c) {
  thread_local MeshOut m;
  m.solid.clear();
  m.water.clear();
  m.cross.clear();
  buildChunkMesh(w, c, m);
  uploadChunkMesh(c, 0, m.solid);
  uploadChunkMesh(c, 1, m.water);
  uploadChunkMesh(c, 2, m.cross);
  c->meshed = true;
  c->meshDirty = false;
}
