#pragma once
#include "common.h"

enum : uint16_t {
  B_AIR = 0,
  B_GRASS,
  B_DIRT,
  B_STONE,
  B_SAND,
  B_WATER,
  B_LOG,
  B_LEAVES,
  B_PLANKS,
  B_COBBLE,
  B_TABLE,
  B_LAMP,
  B_COALORE,
  B_IRONORE,
  B_SNOW,
  B_BEDROCK,
  B_GLASS,
  B_TALL,
  B_FRED,
  B_FYELLOW,
  B_FBLUE,
  B_FWHITE,
  B_FVIOLET,
  B_COUNT_
};

enum : int {
  IT_NONE = 0,
  IT_STICK = 1001,
  IT_COAL = 1002,
  IT_WOODPICK = 1003,
  IT_STONEPICK = 1004,
};

enum : uint8_t {
  T_GRASS_TOP = 0, T_GRASS_SIDE, T_DIRT, T_STONE, T_SAND, T_WATER, T_LOG_SIDE, T_LOG_TOP,
  T_LEAVES, T_PLANKS, T_COBBLE, T_TABLE_TOP, T_TABLE_SIDE, T_LAMP, T_COALORE, T_IRONORE,
  T_SNOW, T_BEDROCK, T_GLASS, T_TALL, T_FRED, T_FYELLOW, T_FBLUE, T_FWHITE, T_FVIOLET, T_COUNT_
};

struct BlockDef {
  const char* name;
  bool solid, opaque, liquid, cross, breakable;
  float hardness;
  int pick;
  int drop, dropN;
  uint8_t emit;
  uint8_t tTop, tSide, tBot;
};

static const BlockDef DEFS[] = {
    {"Воздух", false, false, false, false, false, 0, 0, 0, 0, 0, 0, 0, 0},
    {"Трава", true, true, false, false, true, 0.6f, 0, B_DIRT, 1, 0, T_GRASS_TOP, T_GRASS_SIDE, T_DIRT},
    {"Земля", true, true, false, false, true, 0.5f, 0, B_DIRT, 1, 0, T_DIRT, T_DIRT, T_DIRT},
    {"Камень", true, true, false, false, true, 3.0f, 1, B_COBBLE, 1, 0, T_STONE, T_STONE, T_STONE},
    {"Песок", true, true, false, false, true, 0.5f, 0, B_SAND, 1, 0, T_SAND, T_SAND, T_SAND},
    {"Вода", false, false, true, false, false, 0, 0, 0, 0, 0, T_WATER, T_WATER, T_WATER},
    {"Бревно", true, true, false, false, true, 2.0f, 0, B_LOG, 1, 0, T_LOG_TOP, T_LOG_SIDE, T_LOG_TOP},
    {"Листва", true, false, false, false, true, 0.2f, 0, 0, 0, 0, T_LEAVES, T_LEAVES, T_LEAVES},
    {"Доски", true, true, false, false, true, 2.0f, 0, B_PLANKS, 1, 0, T_PLANKS, T_PLANKS, T_PLANKS},
    {"Булыжник", true, true, false, false, true, 3.5f, 1, B_COBBLE, 1, 0, T_COBBLE, T_COBBLE, T_COBBLE},
    {"Верстак", true, true, false, false, true, 2.5f, 0, B_TABLE, 1, 0, T_TABLE_TOP, T_TABLE_SIDE, T_PLANKS},
    {"Светильник", true, true, false, false, true, 0.4f, 0, B_LAMP, 1, 15, T_LAMP, T_LAMP, T_LAMP},
    {"Угольная руда", true, true, false, false, true, 4.0f, 1, IT_COAL, 1, 0, T_COALORE, T_COALORE, T_COALORE},
    {"Железная руда", true, true, false, false, true, 5.0f, 2, B_IRONORE, 1, 0, T_IRONORE, T_IRONORE, T_IRONORE},
    {"Снег", true, true, false, false, true, 0.3f, 0, B_SNOW, 1, 0, T_SNOW, T_SNOW, T_SNOW},
    {"Коренная порода", true, true, false, false, false, 0, 0, 0, 0, 0, T_BEDROCK, T_BEDROCK, T_BEDROCK},
    {"Стекло", true, false, false, false, true, 0.3f, 0, B_GLASS, 1, 0, T_GLASS, T_GLASS, T_GLASS},
    {"Травинка", false, false, false, true, true, 0.05f, 0, 0, 0, 0, T_TALL, T_TALL, T_TALL},
    {"Красный цветок", false, false, false, true, true, 0.05f, 0, B_FRED, 1, 0, T_FRED, T_FRED, T_FRED},
    {"Жёлтый цветок", false, false, false, true, true, 0.05f, 0, B_FYELLOW, 1, 0, T_FYELLOW, T_FYELLOW, T_FYELLOW},
    {"Синий цветок", false, false, false, true, true, 0.05f, 0, B_FBLUE, 1, 0, T_FBLUE, T_FBLUE, T_FBLUE},
    {"Белый цветок", false, false, false, true, true, 0.05f, 0, B_FWHITE, 1, 0, T_FWHITE, T_FWHITE, T_FWHITE},
    {"Сиреневый цветок", false, false, false, true, true, 0.05f, 0, B_FVIOLET, 1, 0, T_FVIOLET, T_FVIOLET, T_FVIOLET},
};

inline const BlockDef& bdef(int id) { return DEFS[id]; }
inline bool isBlockId(int id) { return id > 0 && id < B_COUNT_; }
inline bool isOpaqueId(int id) { return id > 0 && id < B_COUNT_ && DEFS[id].opaque; }
inline bool isSolidId(int id) { return id > 0 && id < B_COUNT_ && DEFS[id].solid; }

inline int itemPickLevel(int item) {
  if (item == IT_WOODPICK) return 1;
  if (item == IT_STONEPICK) return 2;
  return 0;
}

inline const char* itemName(int id) {
  if (isBlockId(id)) return DEFS[id].name;
  switch (id) {
    case IT_STICK: return "Палка";
    case IT_COAL: return "Уголь";
    case IT_WOODPICK: return "Деревянная кирка";
    case IT_STONEPICK: return "Каменная кирка";
  }
  return "?";
}

struct Recipe {
  const char* name;
  int out, outN;
  int in[3][2];
  int nIn;
  bool table;
};

static const Recipe RECIPES[] = {
    {"Доски", B_PLANKS, 4, {{B_LOG, 1}}, 1, false},
    {"Палки", IT_STICK, 4, {{B_PLANKS, 2}}, 1, false},
    {"Верстак", B_TABLE, 1, {{B_PLANKS, 4}}, 1, false},
    {"Светильник", B_LAMP, 4, {{IT_COAL, 1}, {IT_STICK, 1}}, 2, false},
    {"Деревянная кирка", IT_WOODPICK, 1, {{B_PLANKS, 3}, {IT_STICK, 2}}, 2, true},
    {"Каменная кирка", IT_STONEPICK, 1, {{B_COBBLE, 3}, {IT_STICK, 2}}, 2, true},
};
static const int RECIPE_COUNT = sizeof(RECIPES) / sizeof(RECIPES[0]);
