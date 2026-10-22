#include "world.h"
#include "mesher.h"

#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

static uint64_t lightHash(World& w) {
  uint64_t h = 1469598103934665603ull;
  std::vector<uint64_t> keys;
  for (auto& kv : w.chunks) keys.push_back(kv.first);
  std::sort(keys.begin(), keys.end());
  for (uint64_t k : keys) {
    Chunk* c = w.chunks[k];
    int yb = std::min(WH - 1, c->hmax + 2);
    for (int y = 0; y <= yb; y++)
      for (int i = y * CH * CH; i < (y + 1) * CH * CH; i++) {
        h ^= c->light[i];
        h *= 1099511628211ull;
      }
  }
  return h;
}

int main(int argc, char** argv) {
  int K = argc > 1 ? atoi(argv[1]) : 40;
  World w;
  glm::vec3 pos(g2w(0), g2w(120), g2w(0));
  auto cb = [&](Chunk* c) {
    MeshOut m;
    buildChunkMesh(w, c, m);
    c->meshed = true;
    c->meshDirty = false;
  };

  double tSetup = nowSec();
  int want = (2 * w.renderDist + 1) * (2 * w.renderDist + 1);
  for (int it = 0; it < 80; it++) {
    w.stream(pos, 3000.0, cb);
    int meshed = 0;
    for (auto& kv : w.chunks)
      if (kv.second->meshed) meshed++;
    if (meshed >= want) break;
  }
  int meshed0 = 0;
  for (auto& kv : w.chunks)
    if (kv.second->meshed) meshed0++;
  printf("setup %.1fs chunks %d meshed %d/%d\n", nowSec() - tSetup, (int)w.chunks.size(), meshed0, want);

  double tMesh = 0;
  int nm = 0;
  for (auto& kv : w.chunks) {
    double t0 = nowSec();
    MeshOut m;
    buildChunkMesh(w, kv.second, m);
    tMesh += nowSec() - t0;
    nm++;
  }
  printf("buildChunkMesh avg %.2f ms (%d chunks)\n", tMesh / nm * 1000.0, nm);

  std::mt19937 rng(42);
  double setBlockMs = 0, frameSum = 0, worstFrame = 0;
  int frames = 0, edits = 0;
  for (int e = 0; e < K; e++) {
    int gx = (int)(rng() % (uint32_t)(7 * CH)) - 3 * CH + CH / 2;
    int gz = (int)(rng() % (uint32_t)(7 * CH)) - 3 * CH + CH / 2;
    int gy = w.surfaceY(gx, gz);
    if (gy < 2) continue;
    bool breaking = (e % 2 == 0);
    double t0 = nowSec();
    if (breaking)
      w.setBlock(gx, gy, gz, B_AIR);
    else
      w.setBlock(gx, std::min(WH - 1, gy + 1), gz, B_STONE);
    setBlockMs += nowSec() - t0;
    edits++;
    double worst = 0;
    int fr = 0;
    bool any = true;
    while (any && fr < 12) {
      double f0 = nowSec();
      w.stream(pos, 10.0, cb);
      worst = std::max(worst, nowSec() - f0);
      fr++;
      any = false;
      for (auto& kv : w.chunks)
        if (kv.second->meshDirty) any = true;
      if (!any) {
        double l0 = nowSec();
        w.stream(pos, 100000.0, cb);
        if (nowSec() - l0 > 0.0005) any = true;
      }
    }
    frameSum += worst;
    worstFrame = std::max(worstFrame, worst);
    frames += fr;
  }
  printf("setBlock avg %.2f ms (%d edits)\n", setBlockMs / std::max(1, edits) * 1000.0, edits);
  printf("break->settle avg %.1f frames, worst frame avg %.2f ms, max %.2f ms\n", frames / (double)std::max(1, edits),
         frameSum / std::max(1, edits) * 1000.0, worstFrame * 1000.0);
  printf("lightHash %016llx\n", (unsigned long long)lightHash(w));
  return 0;
}
