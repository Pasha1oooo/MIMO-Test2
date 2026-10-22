#pragma once
#include "player.h"
#include "world.h"
#include <vector>

struct Mob {
  float x = 0, y = 0, z = 0;
  float yaw = 0, vy = 0;
  float timer = 0, walkPhase = 0, moveAmt = 0;
  float kbx = 0, kbz = 0;  // отброс при ударе
  float hurt = 0;          // 0..1 — красная вспышка
  float atkCd = 0;         // кулдаун атаки (зомби)
  int hp = 5;
  int type = 0;  // 0 - свинья, 1 - зомби
  bool onGround = false;
};

struct MobSystem {
  std::vector<Mob> mobs;
  float spawnTimer = 1.5f;

  static constexpr float HW = 0.64f;
  static constexpr float H0 = 1.8f;   // свинья (воксель вдвое мельче)
  static constexpr float H1 = 3.7f;   // зомби

  float height(const Mob& m) const { return m.type == 0 ? H0 : H1; }

  static float rayAABB(const glm::vec3& ro, const glm::vec3& rd, const glm::vec3& mn, const glm::vec3& mx) {
    float tmin = 0.001f, tmax = 1e9f;
    for (int i = 0; i < 3; i++) {
      float d = rd[i];
      if (fabsf(d) < 1e-8f) d = d >= 0 ? 1e-8f : -1e-8f;
      float inv = 1.0f / d;
      float t0 = (mn[i] - ro[i]) * inv, t1 = (mx[i] - ro[i]) * inv;
      if (t0 > t1) std::swap(t0, t1);
      tmin = std::max(tmin, t0);
      tmax = std::min(tmax, t1);
    }
    return tmax >= tmin ? tmin : -1.0f;
  }

  // ближайший моб под лучом; возвращает дистанцию (или -1)
  float hitTest(const glm::vec3& ro, const glm::vec3& rd, float maxDist, Mob** out) {
    float best = maxDist;
    Mob* bm = nullptr;
    for (auto& m : mobs) {
      float hw = 0.76f;
      glm::vec3 mn(m.x - hw, m.y, m.z - hw), mx(m.x + hw, m.y + height(m), m.z + hw);
      float t = rayAABB(ro, rd, mn, mx);
      if (t >= 0.0f && t < best) {
        best = t;
        bm = &m;
      }
    }
    if (out) *out = bm;
    return bm ? best : -1.0f;
  }

  // урон; dirX/dirZ — направление отбраса (от ударившего)
  bool damage(Mob& m, int dmg, float dirX, float dirZ) {
    m.hp -= dmg;
    m.hurt = 1.0f;
    float len = sqrtf(dirX * dirX + dirZ * dirZ);
    if (len > 1e-4f) {
      m.kbx += dirX / len * 14.0f;
      m.kbz += dirZ / len * 14.0f;
    }
    m.vy = std::max(m.vy, 9.0f);
    return m.hp <= 0;
  }

  void kill(Mob& m) { m.hp = 0; }

  bool collides(World& w, float x, float y, float z, float h) const {
    int x0 = w2g(x - HW), x1 = w2g(x + HW);
    int y0 = w2g(y + 0.001f), y1 = w2g(y + h);
    int z0 = w2g(z - HW), z1 = w2g(z + HW);
    for (int yy = y0; yy <= y1; yy++)
      for (int zz = z0; zz <= z1; zz++)
        for (int xx = x0; xx <= x1; xx++)
          if (isSolidId(w.getBlock(xx, yy, zz))) return true;
    return false;
  }

  void trySpawn(World& w, const glm::vec3& pp, float night) {
    if ((int)mobs.size() >= 14) return;
    for (int attempt = 0; attempt < 6; attempt++) {
      float ang = nz::srandf((int)(pp.x * 3.1f) + attempt * 17, (int)(pp.z * 2.7f) + attempt * 31, 44) * 6.2831853f;
      float rad = 36.0f + nz::srandf(attempt, (int)pp.x, 91) * 52.0f;
      float wxw = pp.x + sinf(ang) * rad;
      float wzw = pp.z + cosf(ang) * rad;
      int x = w2g(wxw);
      int z = w2g(wzw);
      int h = w.surfaceY(x, z);
      if (h < WATER_L + 2 || h + 4 >= WH) continue;
      if (w.getBlock(x, h + 1, z) != B_AIR || w.getBlock(x, h + 2, z) != B_AIR || w.getBlock(x, h + 3, z) != B_AIR)
        continue;
      if (w.getBlock(x, h, z) != B_GRASS && w.getBlock(x, h, z) != B_SNOW) continue;
      bool tooClose = false;
      for (auto& m : mobs)
        if ((m.x - wxw) * (m.x - wxw) + (m.z - wzw) * (m.z - wzw) < 144.0f) tooClose = true;
      if (tooClose) continue;
      Mob m;
      m.x = g2w(x) + VX * 0.5f;
      m.y = g2w(h + 1);
      m.z = g2w(z) + VX * 0.5f;
      m.yaw = nz::srandf(x, z, 5) * 6.2831853f;
      // зомби — только ночью
      bool zombie = night > 0.5f && nz::srandf(x + 7, z - 3, 61) < 0.55f;
      m.type = zombie ? 1 : 0;
      m.hp = m.type == 0 ? 5 : 10;
      m.timer = 1.0f + nz::srandf(x, z + 9, 7) * 4.0f;
      mobs.push_back(m);
      return;
    }
  }

  void update(World& w, const glm::vec3& playerPos, float dt, float night, Player& player) {
    spawnTimer -= dt;
    if (spawnTimer <= 0.0f) {
      spawnTimer = 1.8f;
      trySpawn(w, playerPos, night);
    }
    for (auto& m : mobs) {
      float h = height(m);
      bool inWater = w.getBlock(w2g(m.x), w2g(m.y + 0.4f), w2g(m.z)) == B_WATER;
      // зомби днём рассеиваются
      if (m.type == 1 && night < 0.4f) m.hp = 0;
      float speed = m.type == 0 ? 3.0f : 3.8f;
      if (inWater) speed *= 0.6f;
      m.hurt = std::max(0.0f, m.hurt - dt * 3.0f);
      m.atkCd = std::max(0.0f, m.atkCd - dt);
      m.timer -= dt;
      float pdx = playerPos.x - m.x, pdz = playerPos.z - m.z;
      float pd2 = pdx * pdx + pdz * pdz;
      bool chase = m.type == 1 && night > 0.5f && pd2 < 24.0f * 24.0f && player.hp > 0;
      if (chase) {
        // идти к игроку
        m.yaw = atan2f(pdx, pdz);
        m.moveAmt = 1.0f;
        m.timer = 1.0f;
      } else if (m.timer <= 0.0f) {
        m.timer = 1.5f + nz::srandf((int)(m.x * 10.0f), (int)(m.z * 10.0f), (int)(m.y * 10.0f)) * 5.0f;
        m.yaw += (nz::srandf((int)m.x, (int)m.z, (int)(m.timer * 100)) - 0.5f) * 3.5f;
        m.moveAmt = nz::srandf((int)(m.z * 7.0f), (int)m.timer, 3) > 0.3f ? 1.0f : 0.0f;
      }
      // атака игрока
      if (m.type == 1 && pd2 < 3.2f * 3.2f && std::fabs(playerPos.y - m.y) < 4.5f && m.atkCd <= 0.0f && player.hp > 0) {
        m.atkCd = 1.1f;
        float len = std::max(0.001f, sqrtf(pdx * pdx + pdz * pdz));
        player.damage(3, pdx / len * 10.0f, pdz / len * 10.0f);
      }
      float wishX = sinf(m.yaw) * speed * m.moveAmt;
      float wishZ = cosf(m.yaw) * speed * m.moveAmt;
      // гравитация
      if (inWater) {
        m.vy -= 18.0f * dt;
        m.vy += 24.0f * dt;  // всплытие
        m.vy = std::max(-6.0f, std::min(6.0f, m.vy));
        m.vy *= 0.9f;
      } else {
        m.vy -= 54.0f * dt;
        m.vy = std::max(-100.0f, m.vy);
      }
      // горизонтальное движение с коллизией + отброс; при упоре — прыжок
      float dx = (wishX + m.kbx) * dt, dz = (wishZ + m.kbz) * dt;
      float nx = m.x + dx;
      if (!collides(w, nx, m.y, m.z, h))
        m.x = nx;
      else if (m.onGround && m.moveAmt > 0.1f)
        m.vy = 17.6f;
      float nz2 = m.z + dz;
      if (!collides(w, m.x, m.y, nz2, h))
        m.z = nz2;
      else if (m.onGround && m.moveAmt > 0.1f)
        m.vy = 17.6f;
      float kbf = expf(-8.0f * dt);
      m.kbx *= kbf;
      m.kbz *= kbf;
      // вертикаль
      float dy = m.vy * dt;
      m.y += dy;
      if (collides(w, m.x, m.y, m.z, h)) {
        m.y -= dy;
        if (m.vy < 0) m.onGround = true;
        m.vy = 0;
      } else if (m.vy < 0) {
        m.onGround = false;
      }
      if (m.y < -10.0f) m.hp = 0;
      float spd = fabsf(wishX) + fabsf(wishZ);
      m.walkPhase += dt * (2.5f + spd * 3.5f);
    }
    // удаление мёртвых и деспавн
    for (auto it = mobs.begin(); it != mobs.end();) {
      float d2 = (it->x - playerPos.x) * (it->x - playerPos.x) + (it->z - playerPos.z) * (it->z - playerPos.z);
      if (it->hp <= 0 || d2 > 140.0f * 140.0f || it->y < -8.0f)
        it = mobs.erase(it);
      else
        ++it;
    }
  }
};
