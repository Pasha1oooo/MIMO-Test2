#pragma once
#include "world.h"

struct Player {
  glm::vec3 pos{0.5f, 140.0f, 0.5f};
  glm::vec3 vel{0.0f};
  float yaw = 0.0f, pitch = 0.0f;
  bool onGround = false, flying = false, sprint = false, sneak = false;
  bool inWater = false;
  int hp = 20, maxHp = 20;
  float hurtFx = 0;   // красная вспышка урона 0..1
  float regenT = 0;   // таймер регенерации
  float airPeak = 0;  // максимальная высота в полёте (для урона падением)
  bool wasAir = false;

  glm::vec3 eye() const { return pos + glm::vec3(0, sneak ? 2.9f : 3.24f, 0); }
  glm::vec3 forward() const {
    return glm::vec3(cosf(pitch) * sinf(yaw), -sinf(pitch), cosf(pitch) * cosf(yaw));
  }
  glm::vec3 right() const { return glm::vec3(-cosf(yaw), 0, sinf(yaw)); }

  bool collides(World& w, const glm::vec3& p) const {
    float hw = 0.6f, h = sneak ? 3.1f : 3.6f;  // миры (мир героя не меняется, воксели мельче)
    int x0 = w2g(p.x - hw), x1 = w2g(p.x + hw);
    int y0 = w2g(p.y + 0.001f), y1 = w2g(p.y + h);
    int z0 = w2g(p.z - hw), z1 = w2g(p.z + hw);
    for (int y = y0; y <= y1; y++)
      for (int z = z0; z <= z1; z++)
        for (int x = x0; x <= x1; x++)
          if (isSolidId(w.getBlock(x, y, z))) return true;
    return false;
  }

  void damage(int dmg, float kbX = 0.0f, float kbZ = 0.0f) {
    if (dmg <= 0) return;
    hp -= dmg;
    hurtFx = 1.0f;
    regenT = 0;
    vel.x += kbX;
    vel.z += kbZ;
    vel.y = std::max(vel.y, 6.0f);
  }

  void update(World& w, const glm::vec3& wish, bool jump, float dt) {
    uint16_t feet = w.getBlock(w2g(pos.x), w2g(pos.y + 0.4f), w2g(pos.z));
    inWater = (feet == B_WATER);
    float speed = flying ? (sprint ? 44.0f : 24.0f) : (sneak ? 3.6f : (sprint ? 13.6f : 8.8f));
    if (inWater && !flying) speed *= 0.65f;
    glm::vec3 fwd = glm::vec3(sinf(yaw), 0, cosf(yaw));
    glm::vec3 rgt = glm::vec3(-cosf(yaw), 0, sinf(yaw));
    glm::vec3 wishDir = fwd * wish.z + rgt * wish.x;
    if (glm::length(wishDir) > 1e-4f) wishDir = normalize(wishDir);
    float accel = onGround || flying ? 14.0f : 4.0f;
    vel.x += (wishDir.x * speed - vel.x) * std::min(1.0f, accel * dt);
    vel.z += (wishDir.z * speed - vel.z) * std::min(1.0f, accel * dt);
    if (flying) {
      float vy = (jump ? speed : 0.0f) - (sneak ? speed : 0.0f);
      vel.y += (vy - vel.y) * std::min(1.0f, 14.0f * dt);
    } else if (inWater) {
      // плавание: слабая гравитация, сильный ход вверх — легко вылезти на берег
      vel.y -= 10.0f * dt;
      if (jump) vel.y += 34.0f * dt;
      vel.y = std::max(-9.0f, std::min(9.0f, vel.y));
      vel.y *= 0.94f;
    } else {
      vel.y -= 54.0f * dt;
      if (jump && onGround) {
        vel.y = 25.46f;  // подрыв на 6 вокселей (≈3 старых блока)
        onGround = false;
      }
      vel.y = std::max(-110.0f, vel.y);
    }
    // урон падением
    if (!inWater && !flying && !onGround) {
      if (!wasAir) {
        airPeak = pos.y;
        wasAir = true;
      }
      airPeak = std::max(airPeak, pos.y);
    }
    glm::vec3 d = vel * dt;
    int steps = std::max(1, (int)ceilf(glm::length(d) / 0.1f));
    steps = std::min(steps, 24);
    glm::vec3 s = d / (float)steps;
    for (int i = 0; i < steps; i++) {
      pos.x += s.x;
      if (collides(w, pos)) {
        pos.x -= s.x;
        vel.x = 0;
      }
      pos.z += s.z;
      if (collides(w, pos)) {
        pos.z -= s.z;
        vel.z = 0;
      }
      pos.y += s.y;
      if (collides(w, pos)) {
        pos.y -= s.y;
        if (s.y < 0) {
          if (wasAir && !flying && !inWater) {
            float fall = airPeak - pos.y;
            if (fall > 7.0f) damage((int)((fall - 7.0f) / 3.0f) + 1);
          }
          onGround = true;
          wasAir = false;
        }
        vel.y = 0;
      } else if (s.y < 0) {
        onGround = false;
      }
    }
    if (inWater || onGround) wasAir = false;
    // регенерация и затухание вспышки
    hurtFx = std::max(0.0f, hurtFx - dt * 2.0f);
    if (hp > 0 && hurtFx <= 0.0f) {
      regenT += dt;
      if (regenT >= 4.0f && hp < maxHp) {
        hp++;
        regenT = 0;
      }
    }
    if (pos.y < -20) {
      pos.y = g2w(WATER_L + 80);
      vel = glm::vec3(0);
      damage(4);
    }
  }
};
