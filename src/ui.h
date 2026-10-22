#pragma once
#include "blocks.h"
#include "gfx.h"
#include "world.h"

struct Inv {
  std::vector<std::pair<int, int>> st;
  int sel = 0;
  int heldId = 0, heldN = 0;  // стак «в руке курсора»
  Inv() { st.assign(36, {0, 0}); }
  void add(int id, int n) {
    if (!id || n <= 0) return;
    for (auto& s : st)
      if (s.first == id && s.second < 64) {
        int m = std::min(64 - s.second, n);
        s.second += m;
        n -= m;
        if (!n) return;
      }
    for (auto& s : st)
      if (s.first == 0) {
        int m = std::min(64, n);
        s = {id, m};
        n -= m;
        if (!n) return;
      }
  }
  int count(int id) const {
    int c = 0;
    for (auto& s : st)
      if (s.first == id) c += s.second;
    return c;
  }
  bool consume(int id, int n) {
    if (count(id) < n) return false;
    for (auto& s : st) {
      if (s.first != id) continue;
      int m = std::min(s.second, n);
      s.second -= m;
      n -= m;
      if (s.second == 0) s.first = 0;
      if (!n) break;
    }
    return true;
  }
  int selected() const { return st[sel].first; }
  void consumeSelected() {
    if (st[sel].second > 0 && --st[sel].second == 0) st[sel].first = 0;
  }
  // ЛКМ по слоту: взять / положить / поменять / дослить
  void clickSlot(int i) {
    auto& s = st[i];
    if (heldN <= 0) {
      heldId = s.first;
      heldN = s.second;
      s = {0, 0};
    } else if (s.first == 0) {
      s = {heldId, heldN};
      heldId = 0;
      heldN = 0;
    } else if (s.first == heldId && s.second < 64) {
      int m = std::min(64 - s.second, heldN);
      s.second += m;
      heldN -= m;
      if (heldN == 0) heldId = 0;
    } else {
      std::swap(s.first, heldId);
      std::swap(s.second, heldN);
    }
  }
  // ПКМ по слоту: взять половину / положить по одному
  void clickSlotHalf(int i) {
    auto& s = st[i];
    if (heldN <= 0) {
      if (s.second > 1) {
        heldId = s.first;
        heldN = s.second / 2;
        s.second -= heldN;
      } else if (s.second == 1) {
        heldId = s.first;
        heldN = 1;
        s = {0, 0};
      }
    } else if (s.first == 0) {
      s = {heldId, 1};
      if (--heldN == 0) heldId = 0;
    } else if (s.first == heldId && s.second < 64) {
      s.second++;
      if (--heldN == 0) heldId = 0;
    }
  }
  void returnHeld() {
    if (heldN > 0) {
      add(heldId, heldN);
      heldId = 0;
      heldN = 0;
    }
  }
};

inline void drawBlockIcon(UiBatch& b, int id, float x, float y, float s) {
  if (!isBlockId(id)) return;
  const BlockDef& D = bdef(id);
  auto face = [&](int tile, float px0, float py0, float px1, float py1, float px2, float py2, float px3, float py3, float br) {
    int col = tile % ATLAS_TILES, row = tile / ATLAS_TILES;
    float u0 = col * TILE_UV, v0 = row * TILE_UV, u1 = (col + 1) * TILE_UV, v1 = (row + 1) * TILE_UV;
    b.quad4(x + px0 * s, y + py0 * s, x + px1 * s, y + py1 * s, x + px2 * s, y + py2 * s, x + px3 * s, y + py3 * s, u0, v0, u1, v1,
            br, br, br, 1.0f);
  };
  face(D.tTop, 0.5f, 0.0f, 1.0f, 0.25f, 0.5f, 0.5f, 0.0f, 0.25f, 1.0f);
  face(D.tSide, 0.0f, 0.25f, 0.5f, 0.5f, 0.5f, 1.0f, 0.0f, 0.75f, 0.68f);
  face(D.tSide, 0.5f, 0.5f, 1.0f, 0.25f, 1.0f, 0.75f, 0.5f, 1.0f, 0.85f);
}

inline void drawItemIcon(UiBatch& b, int id, float x, float y, float s) {
  if (isBlockId(id)) {
    drawBlockIcon(b, id, x, y, s);
    return;
  }
  if (id == IT_STICK) {
    for (int i = 0; i < 6; i++) b.rect(x + s * (0.28f + i * 0.08f), y + s * (0.72f - i * 0.09f), x + s * (0.28f + i * 0.08f + 0.1f), y + s * (0.85f - i * 0.09f), 0.62f, 0.46f, 0.28f, 1);
  } else if (id == IT_COAL) {
    b.rect(x + s * 0.22f, y + s * 0.3f, x + s * 0.78f, y + s * 0.75f, 0.12f, 0.12f, 0.13f, 1);
    b.rect(x + s * 0.3f, y + s * 0.22f, x + s * 0.7f, y + s * 0.82f, 0.16f, 0.16f, 0.17f, 1);
    b.rect(x + s * 0.35f, y + s * 0.35f, x + s * 0.5f, y + s * 0.5f, 0.25f, 0.25f, 0.26f, 1);
  } else if (id == IT_WOODPICK || id == IT_STONEPICK) {
    float hc = id == IT_WOODPICK ? 0.62f : 0.55f;
    for (int i = 0; i < 6; i++)
      b.rect(x + s * (0.3f + i * 0.07f), y + s * (0.75f - i * 0.08f), x + s * (0.3f + i * 0.07f + 0.09f), y + s * (0.88f - i * 0.08f), 0.55f, 0.4f, 0.24f, 1);
    b.rect(x + s * 0.12f, y + s * 0.18f, x + s * 0.88f, y + s * 0.3f, hc, hc, hc + 0.05f, 1);
    b.rect(x + s * 0.12f, y + s * 0.18f, x + s * 0.26f, y + s * 0.42f, hc, hc, hc + 0.05f, 1);
    b.rect(x + s * 0.74f, y + s * 0.18f, x + s * 0.88f, y + s * 0.42f, hc, hc, hc + 0.05f, 1);
  }
}

inline bool nearCraftingTable(World& w, const glm::vec3& p) {
  int px = w2g(p.x), py = w2g(p.y), pz = w2g(p.z);
  for (int y = py - 5; y <= py + 5; y++)
    for (int z = pz - 10; z <= pz + 10; z++)
      for (int x = px - 10; x <= px + 10; x++)
        if (w.getBlock(x, y, z) == B_TABLE) return true;
  return false;
}

// ---- стилизованная тема интерфейса ----
namespace UIT {
constexpr float AR = 1.00f, AG = 0.77f, AB = 0.28f;       // золотой акцент
constexpr float BR = 0.055f, BG = 0.062f, BB = 0.085f;    // корпус панели
constexpr float HR = 0.115f, HG = 0.125f, HB = 0.165f;    // шапка / слоты
constexpr float SR = 0.26f, SG = 0.28f, SB = 0.35f;       // стальная рамка
}  // namespace UIT

// скруглённый (со срезанными углами) прямоугольник
inline void uiRRect(UiBatch& b, float x0, float y0, float x1, float y1, float r, float cr, float cg, float cb, float ca) {
  r = std::min(r, std::min(x1 - x0, y1 - y0) * 0.5f);
  if (r < 1.0f) {
    b.rect(x0, y0, x1, y1, cr, cg, cb, ca);
    return;
  }
  b.rect(x0 + r, y0, x1 - r, y1, cr, cg, cb, ca);
  b.rect(x0, y0 + r, x1, y1 - r, cr, cg, cb, ca);
  auto tri = [&](float ax, float ay, float bx, float by, float cx2, float cy2) {
    b.quad4(ax, ay, bx, by, cx2, cy2, ax, ay, 0, 0, 1, 1, cr, cg, cb, ca);
  };
  tri(x0, y0 + r, x0 + r, y0 + r, x0 + r, y0);
  tri(x1 - r, y0, x1 - r, y0 + r, x1, y0 + r);
  tri(x1 - r, y1 - r, x1, y1 - r, x1 - r, y1);
  tri(x0, y1 - r, x0 + r, y1 - r, x0 + r, y1);
}

// вертикальный градиент с учётом срезанных углов
inline void uiGrad(UiBatch& b, float x0, float y0, float x1, float y1, float r, float tr, float tg, float tb, float br, float bg,
                   float bb, float ca, int steps = 8) {
  r = std::min(r, std::min(x1 - x0, y1 - y0) * 0.5f);
  if (r < 1.0f || y1 <= y0) {
    for (int i = 0; i < steps; i++) {
      float f0 = (float)i / steps, f1 = (float)(i + 1) / steps;
      float sy0 = y0 + (y1 - y0) * f0, sy1 = y0 + (y1 - y0) * f1;
      float f = (f0 + f1) * 0.5f;
      b.rect(x0, sy0, x1, sy1, tr + (br - tr) * f, tg + (bg - tg) * f, tb + (bb - tb) * f, ca);
    }
    return;
  }
  for (int i = 0; i < steps; i++) {
    float f0 = (float)i / steps, f1 = (float)(i + 1) / steps;
    float sy0 = y0 + (y1 - y0) * f0, sy1 = y0 + (y1 - y0) * f1;
    float inset = std::min(r, std::max(std::max(0.0f, y0 + r - sy0), std::max(0.0f, sy1 - (y1 - r))));
    float f = (f0 + f1) * 0.5f;
    b.rect(x0 + inset, sy0, x1 - inset, sy1, tr + (br - tr) * f, tg + (bg - tg) * f, tb + (bb - tb) * f, ca);
  }
}

inline void uiShadow(UiBatch& b, float x0, float y0, float x1, float y1, float off, float a) {
  b.rect(x0 + off, y0 + off, x1 + off, y1 + off, 0, 0, 0, a);
}

// панель: тень + рамка + корпус + шапка-градиент
inline void uiPanel(UiBatch& b, float x0, float y0, float x1, float y1, float s, float headH = 0.0f, float r = -1.0f,
                    float brdR = UIT::SR, float brdG = UIT::SG, float brdB = UIT::SB, float alpha = 0.97f) {
  if (r < 0) r = 10.0f * s;
  uiShadow(b, x0, y0, x1, y1, 4.0f * s, 0.5f);
  uiRRect(b, x0 - 1.5f * s, y0 - 1.5f * s, x1 + 1.5f * s, y1 + 1.5f * s, r + 1.5f * s, brdR, brdG, brdB, 1.0f);
  uiRRect(b, x0, y0, x1, y1, r, UIT::BR, UIT::BG, UIT::BB, alpha);
  if (headH > 0)
    uiGrad(b, x0, y0, x1, std::min(y0 + headH, y1), r, UIT::HR, UIT::HG, UIT::HB, UIT::BR * 1.35f, UIT::BG * 1.35f, UIT::BB * 1.35f,
           alpha, 6);
}

// слот с фаской, подсветкой и акцентной рамкой выделения
inline void uiSlot(UiBatch& b, float x, float y, float sz, float s, bool hover, bool sel, float alpha = 1.0f, float r = -1.0f) {
  if (r < 0) r = 5.0f * s;
  if (sel) {
    uiRRect(b, x - 4 * s, y - 4 * s, x + sz + 4 * s, y + sz + 4 * s, r + 4 * s, UIT::AR, UIT::AG, UIT::AB, 0.22f * alpha);
    uiRRect(b, x - 1.5f * s, y - 1.5f * s, x + sz + 1.5f * s, y + sz + 1.5f * s, r + 1.5f * s, UIT::AR, UIT::AG, UIT::AB, 0.95f * alpha);
  } else if (hover) {
    uiRRect(b, x - 1.5f * s, y - 1.5f * s, x + sz + 1.5f * s, y + sz + 1.5f * s, r + 1.5f * s, UIT::AR, UIT::AG, UIT::AB, 0.5f * alpha);
  }
  float br = hover ? 0.255f : 0.185f, bg = hover ? 0.265f : 0.195f, bb = hover ? 0.325f : 0.245f;
  uiRRect(b, x, y, x + sz, y + sz, r, br, bg, bb, 0.96f * alpha);
  b.rect(x + r, y, x + sz - r, y + 1.4f * s, 1, 1, 1, 0.16f * alpha);
  b.rect(x + r, y + sz - 1.4f * s, x + sz - r, y + sz, 0, 0, 0, 0.34f * alpha);
  b.rect(x, y + r, x + 1.4f * s, y + sz - r, 1, 1, 1, 0.10f * alpha);
  b.rect(x + sz - 1.4f * s, y + r, x + sz, y + sz - r, 0, 0, 0, 0.28f * alpha);
  if (sel) b.rect(x + r, y, x + sz - r, y + 2.0f * s, UIT::AR, UIT::AG, UIT::AB, 0.9f * alpha);
}

inline void drawHotbar(UiBatch& b, UiBatch& icons, const Inv& inv, int W, int H, float scale = 1.0f, float alpha = 1.0f) {
  float s = scale;
  float slot = 46.0f * s, gap = 4.0f * s, pad = 5.0f * s;
  float total = 9 * slot + 8 * gap;
  float x0 = W * 0.5f - total * 0.5f, y0 = (float)H - slot - 12.0f * s;
  // контейнер под слотами
  float bx0 = x0 - pad, by0 = y0 - pad, bx1 = x0 + total + pad, by1 = y0 + slot + pad;
  uiShadow(b, bx0, by0, bx1, by1, 4.0f * s, 0.5f * alpha);
  uiRRect(b, bx0 - 1.5f * s, by0 - 1.5f * s, bx1 + 1.5f * s, by1 + 1.5f * s, 10.0f * s, UIT::SR, UIT::SG, UIT::SB, alpha);
  uiRRect(b, bx0, by0, bx1, by1, 9.0f * s, UIT::BR, UIT::BG, UIT::BB, 0.88f * alpha);
  uiGrad(b, bx0, by0, bx1, by0 + 7.0f * s, 9.0f * s, 0.16f, 0.17f, 0.22f, 0.07f, 0.08f, 0.10f, 0.75f * alpha, 4);
  for (int i = 0; i < 9; i++) {
    float x = x0 + i * (slot + gap);
    uiSlot(b, x, y0, slot, s, false, i == inv.sel, alpha);
    if (inv.st[i].first) drawItemIcon(icons, inv.st[i].first, x + 5 * s, y0 + 5 * s, slot - 10 * s);
  }
}

// сердечки героя: 10 сердец по 2 хп на тёмной плашке
inline void drawHearts(UiBatch& b, int hp, int maxHp, float x0, float y0, float scale) {
  float s = 18.0f * scale, gap = 3.0f * scale;
  int hearts = (maxHp + 1) / 2;
  float rowW = hearts * (s + gap) - gap;
  float pad = 5.0f * scale;
  float px0 = x0 - pad, py0 = y0 - pad, px1 = x0 + rowW + pad, py1 = y0 + s + pad;
  uiShadow(b, px0, py0, px1, py1, 3.0f * scale, 0.45f);
  uiRRect(b, px0 - 1.5f * scale, py0 - 1.5f * scale, px1 + 1.5f * scale, py1 + 1.5f * scale, 7.0f * scale, UIT::SR, UIT::SG, UIT::SB, 1.0f);
  uiRRect(b, px0, py0, px1, py1, 6.0f * scale, UIT::BR, UIT::BG, UIT::BB, 0.85f);
  auto heart = [&](float x, float y, float u, float r, float g, float bl, float a = 1.0f) {
    b.rect(x, y, x + u * 2.7f, y + u * 2.4f, r, g, bl, a);
    b.rect(x + u * 3.3f, y, x + u * 6.0f, y + u * 2.4f, r, g, bl, a);
    b.rect(x + u * 0.6f, y + u * 1.8f, x + u * 5.4f, y + u * 3.6f, r, g, bl, a);
    b.rect(x + u * 1.5f, y + u * 3.6f, x + u * 4.5f, y + u * 4.8f, r, g, bl, a);
    b.rect(x + u * 2.3f, y + u * 4.8f, x + u * 3.7f, y + u * 5.8f, r, g, bl, a);
  };
  for (int i = 0; i < hearts; i++) {
    float x = x0 + i * (s + gap), y = y0 + (s - 5.8f * (s / 7.0f)) * 0.5f;
    float u = s / 7.0f;
    heart(x - u * 0.55f, y - u * 0.3f, u * 1.15f, 0.045f, 0.02f, 0.035f, 0.9f);
    heart(x, y, u, 0.30f, 0.11f, 0.14f);
    int v = hp - i * 2;
    if (v >= 2) {
      heart(x, y, u, 0.93f, 0.22f, 0.26f);
      b.rect(x + u * 0.6f, y + u * 3.6f, x + u * 5.4f, y + u * 4.4f, 0.55f, 0.07f, 0.11f, 0.4f);
      b.rect(x + u * 1.5f, y + u * 4.4f, x + u * 4.5f, y + u * 5.0f, 0.55f, 0.07f, 0.11f, 0.4f);
      b.rect(x + u * 0.7f, y + u * 0.6f, x + u * 1.7f, y + u * 1.5f, 1.0f, 0.86f, 0.86f, 0.95f);
      b.rect(x + u * 4.2f, y + u * 0.6f, x + u * 4.8f, y + u * 1.1f, 1.0f, 0.86f, 0.86f, 0.6f);
    } else if (v == 1) {
      auto half = [&](float xx, float yy, float ww, float hgt) {
        float x1 = std::min(xx + ww, x + u * 3.0f);
        if (x1 > xx && yy + hgt > y) b.rect(xx, std::max(yy, y), x1, yy + hgt, 0.93f, 0.22f, 0.26f, 1);
      };
      half(x, y, u * 2.7f, u * 2.4f);
      half(x + u * 3.3f, y, u * 2.7f, u * 2.4f);
      half(x + u * 0.6f, y + u * 1.8f, u * 4.8f, u * 1.8f);
      half(x + u * 1.5f, y + u * 3.6f, u * 3.0f, u * 1.2f);
      half(x + u * 2.3f, y + u * 4.8f, u * 1.4f, u);
    }
  }
}

inline void drawCrosshair(UiBatch& b, int W, int H, float scale = 1.0f) {
  float cx = W * 0.5f, cy = H * 0.5f;
  float a = 9.0f * scale, t = 1.2f * scale, g = 3.0f * scale, o = 1.1f * scale;
  auto cross = [&](float dt, float r, float gg, float bb, float al) {
    b.rect(cx - a, cy - dt, cx - g, cy + dt, r, gg, bb, al);
    b.rect(cx + g, cy - dt, cx + a, cy + dt, r, gg, bb, al);
    b.rect(cx - dt, cy - a, cx + dt, cy - g, r, gg, bb, al);
    b.rect(cx - dt, cy + g, cx + dt, cy + a, r, gg, bb, al);
  };
  cross(t + o, 0, 0, 0, 0.55f);
  cross(t, 1, 1, 1, 0.92f);
  float d = 1.4f * scale;
  b.rect(cx - d, cy - d, cx + d, cy + d, UIT::AR, UIT::AG, UIT::AB, 0.95f);
}
