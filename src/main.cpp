#include "common.h"

#include "blocks.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb/stb_truetype.h>

#include "gfx.h"
#include "mesher.h"
#include "mobs.h"
#include "net.h"
#include "player.h"
#include "ui.h"
#include "world.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>

static GLFWwindow* gWin = nullptr;
static int gW = 1280, gH = 720;
static bool gInvOpen = false;
static bool gDebug = false;
static bool gRT = false;
static float gTod = 0.15f;
static float gBreakProg = 0;
static int gBreakX = 0, gBreakY = 0, gBreakZ = 0;
static float gCraftScroll = 0;
static std::string gShotPath;
static int gShotFrames = 90;
static float gShotTime = -1;
static glm::vec3 gShotPos(NAN);
static float gShotYaw = 0, gShotPitch = 0;
static int gShotRT = -1;
static bool gShotInv = false;
static float gShotMobD = -1;
static bool gShotDead = false;
static uint32_t gSeedV = 0;
static bool gSeedSet = false;
static bool gNetListen = false;
static int gNetPort = 35617;
static std::string gNetHost;
static std::string gNetName;
static bool gDead = false;
static float gRespawnFlash = 0;
static bool gOutline = false;  // чёрная обводка контуров (как в мультиках)

struct InputState {
  bool down[512] = {0};
  bool prev[512] = {0};
  bool mdown[3] = {0};
  bool mprev[3] = {0};
  double mx = 0, my = 0;
  double dx = 0, dy = 0;
  double lastMx = 0, lastMy = 0;
  float wheel = 0;
  bool firstMouse = true;

  bool key(int k) const { return down[k]; }
  bool pressed(int k) const { return down[k] && !prev[k]; }
  bool mouse(int b) const { return mdown[b]; }
  bool clicked(int b) const { return mdown[b] && !mprev[b]; }
  void endFrame() {
    memcpy(prev, down, sizeof(down));
    memcpy(mprev, mdown, sizeof(mdown));
    wheel = 0;
    dx = dy = 0;
  }
};
static InputState gIn;

static void cursorCb(GLFWwindow*, double x, double y) {
  gIn.mx = x;
  gIn.my = y;
  if (!gIn.firstMouse) {
    gIn.dx += x - gIn.lastMx;
    gIn.dy += y - gIn.lastMy;
  }
  gIn.lastMx = x;
  gIn.lastMy = y;
  gIn.firstMouse = false;
}
static void mouseBtnCb(GLFWwindow*, int b, int act, int) {
  if (b >= 0 && b < 3) gIn.mdown[b] = (act == GLFW_PRESS);
}
static void scrollCb(GLFWwindow*, double, double y) { gIn.wheel += (float)y; }
static void keyCb(GLFWwindow*, int key, int, int act, int) {
  if (key >= 0 && key < 512) gIn.down[key] = (act != GLFW_RELEASE);
}

static const char* FONT_PATHS[] = {
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/noto/NotoSans-Regular.ttf",
    "/usr/share/fonts/Adwaita/AdwaitaSans-Regular.ttf",
};

// RT volume
constexpr int VOL_S = 200;  // кратно CH (200/40 = 5 чанков)
constexpr int VOL_Y = 256;
static GLuint gVolTex = 0, gTileTex = 0;
static std::vector<uint8_t> gVolData;
static int gVolOx = 1 << 30, gVolOz = 1 << 30;
static bool gVolDirty = true;

static void initVolumeTextures() {
  glGenTextures(1, &gVolTex);
  glBindTexture(GL_TEXTURE_3D, gVolTex);
  glTexImage3D(GL_TEXTURE_3D, 0, GL_RGB8, VOL_S, VOL_Y, VOL_S, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
  gVolData.assign((size_t)VOL_S * VOL_Y * VOL_S * 3, 0);
  std::vector<uint8_t> tm(64 * 4, 0);
  for (int id = 0; id < B_COUNT_; id++) {
    tm[id * 4 + 0] = (uint8_t)(DEFS[id].tTop * 255 / 63);
    tm[id * 4 + 1] = (uint8_t)(DEFS[id].tSide * 255 / 63);
    tm[id * 4 + 2] = (uint8_t)(DEFS[id].tBot * 255 / 63);
    tm[id * 4 + 3] = 255;
  }
  gTileTex = makeTex2D(64, 1, tm.data());
}

static void updateVolume(World& w, const glm::vec3& pos, bool force) {
  int cx = w2g(pos.x), cz = w2g(pos.z);
  int ox = ((cx - VOL_S / 2) / CH) * CH;
  int oz = ((cz - VOL_S / 2) / CH) * CH;
  float distCenter = glm::length(glm::vec2((float)(cx - (ox + VOL_S / 2)), (float)(cz - (oz + VOL_S / 2))));
  if (!force && !gVolDirty && ox == gVolOx && oz == gVolOz && distCenter < 20.0f) return;
  gVolOx = ox;
  gVolOz = oz;
  w.fillVolume(ox, 0, oz, VOL_S, VOL_Y, VOL_S, gVolData.data());
  glBindTexture(GL_TEXTURE_3D, gVolTex);
  glTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 0, VOL_S, VOL_Y, VOL_S, GL_RGB, GL_UNSIGNED_BYTE, gVolData.data());
  gVolDirty = false;
}

static void parseArgs(int argc, char** argv) {
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    auto next = [&]() -> const char* { return (i + 1 < argc) ? argv[++i] : ""; };
    if (a == "--shot") gShotPath = next();
    else if (a == "--frames") gShotFrames = atoi(next());
    else if (a == "--size") sscanf(next(), "%dx%d", &gW, &gH);
    else if (a == "--time") gShotTime = (float)atof(next());
    else if (a == "--pos") sscanf(next(), "%f,%f,%f", &gShotPos.x, &gShotPos.y, &gShotPos.z);
    else if (a == "--yaw") gShotYaw = (float)atof(next());
    else if (a == "--pitch") gShotPitch = (float)atof(next());
    else if (a == "--rt") gShotRT = atoi(next());
    else if (a == "--inv") gShotInv = true;
    else if (a == "--outline") gOutline = atoi(next()) != 0;
    else if (a == "--mobb") gShotMobD = (float)atof(next());
    else if (a == "--dead") gShotDead = true;
    else if (a == "--seed") {
      gSeedV = (uint32_t)strtoul(next(), nullptr, 10);
      gSeedSet = true;
    }
    else if (a == "--listen") {
      gNetListen = true;
      if (i + 1 < argc && argv[i + 1][0] != '-') gNetPort = atoi(next());
    } else if (a == "--connect") {
      std::string hp = next();
      size_t c = hp.rfind(':');
      if (c != std::string::npos) {
        gNetPort = atoi(hp.c_str() + c + 1);
        gNetHost = hp.substr(0, c);
      } else {
        gNetHost = hp;
      }
    } else if (a == "--name") gNetName = next();
    else if (a == "--dist") { /* handled after world create */ }
  }
}

int main(int argc, char** argv) {
  parseArgs(argc, argv);
  bool shotMode = !gShotPath.empty();

  if (!glfwInit()) {
    fprintf(stderr, "glfw init failed\n");
    return 1;
  }
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_SAMPLES, 0);
  if (!shotMode) glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);
  gWin = glfwCreateWindow(gW, gH, "MCGL - OpenGL voxel game", nullptr, nullptr);
  if (!gWin) {
    fprintf(stderr, "window failed\n");
    return 1;
  }
  if (!shotMode) {
    // настоящий fullscreen — иначе тайловые WM (Hyprland) режут окно пополам
    GLFWmonitor* mon = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = mon ? glfwGetVideoMode(mon) : nullptr;
    if (mode)
      glfwSetWindowMonitor(gWin, mon, 0, 0, mode->width, mode->height, mode->refreshRate);
    else
      glfwMaximizeWindow(gWin);
  }
  glfwMakeContextCurrent(gWin);
  if (!gladLoadGL((GLADloadfunc)glfwGetProcAddress)) {
    fprintf(stderr, "glad: не удалось загрузить OpenGL\n");
    return 1;
  }
  glfwSwapInterval(shotMode ? 0 : 1);
  glfwSetInputMode(gWin, GLFW_CURSOR, shotMode ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
  glfwSetCursorPosCallback(gWin, cursorCb);
  glfwSetMouseButtonCallback(gWin, mouseBtnCb);
  glfwSetScrollCallback(gWin, scrollCb);
  glfwSetKeyCallback(gWin, keyCb);
  printf("GL: %s %s\n", (const char*)glGetString(GL_VERSION), (const char*)glGetString(GL_RENDERER));

  Program chunkProg(CHUNK_VS, CHUNK_FS);
  Program skyProg(SKY_VS, SKY_FS);
  Program lineProg(LINE_VS, LINE_FS);
  Program uiProg(UI_VS, UI_FS);
  Program rtProg(RT_VS, RT_FS);
  Program breakProg(BREAK_VS, BREAK_FS);
  Program colorProg(COLOR_VS, COLOR_FS);
  Program shadowProg(SHADOW_VS, SHADOW_FS);
  Program outlineProg(OUTLINE_VS, OUTLINE_FS);
  SceneTarget sceneTgt;
  GLuint shadowVao = 0, shadowVbo = 0;
  glGenVertexArrays(1, &shadowVao);
  glGenBuffers(1, &shadowVbo);
  glBindVertexArray(shadowVao);
  glBindBuffer(GL_ARRAY_BUFFER, shadowVbo);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(5 * sizeof(float)));
  glBindVertexArray(0);
  GLuint outlineVao;
  glGenVertexArrays(1, &outlineVao);

  std::vector<uint8_t> atlasPx;
  buildBlockAtlas(atlasPx, 256);
  GLuint atlasTex = makeTex2D(256, 256, atlasPx.data());
  GLuint whiteTex;
  {
    uint8_t wh[4] = {255, 255, 255, 255};
    whiteTex = makeTex2D(1, 1, wh);
  }
  Font font;
  bool haveFont = false;
  for (auto p : FONT_PATHS)
    if (font.load(p, 22.0f)) {
      haveFont = true;
      printf("font: %s\n", p);
      break;
    }
  fprintf(stderr, "init: programs+atlas ok\n");
  UiBatch ubR, ubI, ubT;
  ubR.init();
  ubI.init();
  ubT.init();
  initVolumeTextures();
  fprintf(stderr, "init: volume ok\n");

  GLuint lineVao, lineVbo;
  glGenVertexArrays(1, &lineVao);
  glGenBuffers(1, &lineVbo);
  glBindVertexArray(lineVao);
  glBindBuffer(GL_ARRAY_BUFFER, lineVbo);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
  glBindVertexArray(0);
  // юнит-куб: pos(3) uv(2) normal(3), 36 вершин — для оверлея ломания и мобов
  GLuint cubeVao, cubeVbo;
  {
    std::vector<float> cv;
    const float face[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (int f = 0; f < 6; f++) {
      float nx = face[f][0], ny = face[f][1], nz = face[f][2];
      int ax = nx != 0 ? 0 : (ny != 0 ? 1 : 2);
      int uax = (ax + 1) % 3, vax = (ax + 2) % 3;
      float c[4][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
      for (int k = 0; k < 4; k++) {
        float fu = (k == 1 || k == 2) ? 1.0f : 0.0f, fv = (k == 2 || k == 3) ? 1.0f : 0.0f;
        c[k][ax] = (nx + ny + nz) > 0 ? 1.0f : 0.0f;
        c[k][uax] = fu;
        c[k][vax] = fv;
      }
      float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
      int idx[6];
      // у отрицательных граней об winding — иначе они вырезаются снаружи (буаг «текстуры с одной стороны»)
      if ((nx + ny + nz) < 0) {
        idx[0] = 0; idx[1] = 2; idx[2] = 1; idx[3] = 0; idx[4] = 3; idx[5] = 2;
      } else {
        idx[0] = 0; idx[1] = 1; idx[2] = 2; idx[3] = 0; idx[4] = 2; idx[5] = 3;
      }
      for (int i = 0; i < 6; i++) {
        int k = idx[i];
        cv.push_back(c[k][0]); cv.push_back(c[k][1]); cv.push_back(c[k][2]);
        cv.push_back(uv[k][0]); cv.push_back(uv[k][1]);
        cv.push_back(nx); cv.push_back(ny); cv.push_back(nz);
      }
    }
    glGenVertexArrays(1, &cubeVao);
    glGenBuffers(1, &cubeVbo);
    glBindVertexArray(cubeVao);
    glBindBuffer(GL_ARRAY_BUFFER, cubeVbo);
    glBufferData(GL_ARRAY_BUFFER, cv.size() * sizeof(float), cv.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(5 * sizeof(float)));
    glBindVertexArray(0);
  }
  GLuint skyVao;
  glGenVertexArrays(1, &skyVao);
  GLuint rtVao;
  glGenVertexArrays(1, &rtVao);
  RTTarget rtTarget;

  World world;
  Inv inv;
  Player player;
  auto meshCb = [&](Chunk* c) { meshAndUpload(world, c); };

  // spawn — рандомная точка мира, поиск суши по спирали
  fprintf(stderr, "init: before spawn\n");
  glm::vec3 spawnPos(0.5f, 140.0f, 0.5f);
  {
    double ts = nowSec();
    uint32_t seed = gSeedSet ? gSeedV
                              : ((uint32_t)time(nullptr) * 2654435761u ^
                                 (uint32_t)std::chrono::steady_clock::now().time_since_epoch().count());
    int bx = (int)(nz::h2((int)seed, 1) % 40001) - 20000;
    int bz = (int)(nz::h2((int)seed, 2) % 40001) - 20000;
    bool found = false;
    int fsx = bx, fsz = bz;
    for (int r = 0; r <= 20 && !found; r++) {
      for (int dz = -r; dz <= r && !found; dz++)
        for (int dx = -r; dx <= r && !found; dx++) {
          if (std::max(std::abs(dx), std::abs(dz)) != r) continue;
          int x = bx + dx * 32, z = bz + dz * 32;
          float h = world.terrainHeight(x, z);
          if (h > WATER_L + 4 && h < 140) {
            fsx = x;
            fsz = z;
            found = true;
          }
        }
    }
    bx = fsx;
    bz = fsz;
    world.stream(glm::vec3(g2w(bx), g2w(120), g2w(bz)), 3000.0, meshCb);
    int sy = world.surfaceY(bx, bz);
    player.pos = glm::vec3(g2w(bx) + VX * 0.5f, g2w(sy + 1) + 0.01f, g2w(bz) + VX * 0.5f);
    player.yaw = 0.7f;
    spawnPos = player.pos;
    fprintf(stderr, "spawn %d %d (h=%d) gen %.2fs\n", bx, bz, sy, nowSec() - ts);
  }
  // initial load around player
  {
    int meshed = 0, target = 0;
    int R = world.renderDist;
    for (int dz = -R; dz <= R; dz++)
      for (int dx = -R; dx <= R; dx++) target++;
    double t0 = nowSec();
    while (nowSec() - t0 < 45.0) {
      world.stream(player.pos, 12.0, meshCb);
      meshed = 0;
      int pcx = floorDiv(w2g(player.pos.x), CH), pcz = floorDiv(w2g(player.pos.z), CH);
      for (int dz = -R; dz <= R; dz++)
        for (int dx = -R; dx <= R; dx++) {
          Chunk* c = world.chunkAt(pcx + dx, pcz + dz);
          if (c && c->meshed) meshed++;
        }
      if (meshed >= target) break;
      glfwPollEvents();
    }
    fprintf(stderr, "init: initial load %.2fs (%d/%d)\n", nowSec() - t0, meshed, target);
    world.stream(player.pos, 5000.0, meshCb);  // дожать отложенную очередь света
  }
  if (!std::isnan(gShotPos.x)) player.pos = gShotPos;
  if (shotMode) {
    player.yaw = gShotYaw;
    player.pitch = gShotPitch;
    if (gShotTime >= 0) gTod = gShotTime;
    if (gShotRT >= 0) gRT = (gShotRT != 0);
    if (gShotInv) {
      int w, h;
      glfwGetWindowSize(gWin, &w, &h);
      glfwSetCursorPos(gWin, w * 0.5, h * 0.5);
      gIn.mx = w * 0.5;
      gIn.my = h * 0.5;
      gIn.firstMouse = true;
    }
  }
  inv.add(B_PLANKS, 32);
  inv.add(IT_STICK, 16);
  inv.add(B_LAMP, 8);
  inv.add(IT_WOODPICK, 1);
  if (gShotInv) {
    gInvOpen = true;
    inv.heldId = B_STONE;
    inv.heldN = 7;
  }
  if (gShotDead) gDead = true;

  MobSystem mobs;
  if (shotMode && gShotMobD > 0) {
    for (int a = 0; a < 24; a++) {
      float ang = a * (6.2831853f / 24.0f);
      float xx = player.pos.x + sinf(ang) * gShotMobD, zz = player.pos.z + cosf(ang) * gShotMobD;
      int gx = w2g(xx), gz = w2g(zz);
      int gy = world.surfaceY(gx, gz);
      uint16_t gsb = world.getBlock(gx, gy, gz);
      if (gsb != B_GRASS && gsb != B_SAND && gsb != B_DIRT && gsb != B_SNOW) continue;
      bool clear = true;
      for (int dy = 1; dy <= 5; dy++)
        if (world.getBlock(gx, gy + dy, gz) != B_AIR) clear = false;
      if (!clear) continue;
      Mob m;
      m.x = g2w(gx) + VX * 0.5f;
      m.z = g2w(gz) + VX * 0.5f;
      m.y = g2w(gy + 1);
      m.yaw = ang + 1.7f;
      m.type = 0;
      m.hp = 5;
      m.timer = 1e9f;
      mobs.mobs.push_back(m);
      glm::vec3 to = glm::vec3(m.x, m.y + 1.0f, m.z) - player.eye();
      player.yaw = atan2f(to.x, to.z);
      player.pitch = -asinf(to.y / std::max(0.01f, glm::length(to)));
      break;
    }
  }
  Net net;
  {
    if (gNetListen) {
      if (!net.startServer(gNetPort)) fprintf(stderr, "net: не удалось слушать порт %d\n", gNetPort);
    }
    if (!gNetHost.empty()) {
      net.name = gNetName.empty() ? ("player" + std::to_string((int)getpid())) : gNetName;
      if (!net.connectTo(gNetHost, gNetPort)) fprintf(stderr, "net: подключение не удалось\n");
    }
  }
  auto setBlockNet = [&](int x, int y, int z, uint16_t id) {
    world.setBlock(x, y, z, id);
    gVolDirty = true;
    net.sendEdit(x, y, z, id);
  };

  double last = nowSec();
  double tStart = last;  // относительное время для шейдеров (точность float)
  double fpsT = last;
  int fpsN = 0;
  float fps = 0;
  int frame = 0;
  glm::vec3 camPos = player.eye();
  Hit look;
  bool running = true;

  while (running && !glfwWindowShouldClose(gWin)) {
    double now = nowSec();
    float dt = shotMode ? (1.0f / 60.0f) : (float)std::min(0.05, now - last);
    last = now;
    gTod = fmodf(gTod + dt / 480.0f, 1.0f);
    DayColors day = computeDay(gTod);

    glfwPollEvents();
    if (!shotMode) {
      if (gIn.pressed(GLFW_KEY_ESCAPE)) {
        if (gInvOpen) {
          gInvOpen = false;
          inv.returnHeld();
          glfwSetInputMode(gWin, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
          gIn.firstMouse = true;
        } else {
          glfwSetInputMode(gWin, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }
      }
      if (gIn.pressed(GLFW_KEY_E)) {
        gInvOpen = !gInvOpen;
        if (!gInvOpen) inv.returnHeld();
        glfwSetInputMode(gWin, GLFW_CURSOR, gInvOpen ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
        gIn.firstMouse = true;
      }
      if (gIn.pressed(GLFW_KEY_F3)) gDebug = !gDebug;
      if (gIn.pressed(GLFW_KEY_F)) player.flying = !player.flying;
      if (gIn.pressed(GLFW_KEY_F5)) {
        gRT = !gRT;
        gVolDirty = true;
        updateVolume(world, player.pos, true);
      }
      if (gIn.pressed(GLFW_KEY_O)) gOutline = !gOutline;  // обводка контуров
      for (int i = 0; i < 9; i++)
        if (gIn.pressed(GLFW_KEY_1 + i)) inv.sel = i;
      if (gIn.wheel != 0 && !gInvOpen) inv.sel = mod(inv.sel - (int)gIn.wheel, 9);

      bool locked = glfwGetInputMode(gWin, GLFW_CURSOR) == GLFW_CURSOR_DISABLED;
      if (locked && !gDead) {
        const float sens = 0.0022f;
        player.yaw -= (float)gIn.dx * sens;
        player.pitch += (float)gIn.dy * sens;
        player.pitch = std::max(-1.55f, std::min(1.55f, player.pitch));
        glm::vec3 wish(0);
        if (gIn.key(GLFW_KEY_W)) wish.z += 1;
        if (gIn.key(GLFW_KEY_S)) wish.z -= 1;
        if (gIn.key(GLFW_KEY_D)) wish.x += 1;
        if (gIn.key(GLFW_KEY_A)) wish.x -= 1;
        player.sneak = gIn.key(GLFW_KEY_LEFT_SHIFT);
        player.sprint = gIn.key(GLFW_KEY_LEFT_CONTROL);
        bool jump = gIn.key(GLFW_KEY_SPACE);
        if (!gInvOpen) player.update(world, wish, jump, dt);
      } else if (!gInvOpen && shotMode) {
        // shot mode: no physics, camera fixed at given pos
      }
      if (!locked && !gInvOpen && !shotMode) {
        // keep idle
      }
      // mouse actions
      if (locked && !gInvOpen && !gDead) {
        camPos = player.eye();
        bool hasBlock = world.raycast(camPos, player.forward(), 12.0f, look) && look.hit;
        // моб под прицелом (ближе блока)
        Mob* aimMob = nullptr;
        float aimDist = mobs.hitTest(camPos, player.forward(), 9.0f, &aimMob);
        if (aimMob && hasBlock && aimDist > look.dist) aimMob = nullptr;
        static float atkCd = 0;
        atkCd -= dt;
        if (aimMob && gIn.clicked(0) && atkCd <= 0.0f) {
          atkCd = 0.4f;
          int tool = itemPickLevel(inv.selected());
          int dmg = 1 + (tool > 0 ? 1 : 0);
          bool dead = mobs.damage(*aimMob, dmg, aimMob->x - player.pos.x, aimMob->z - player.pos.z);
          if (dead) { /* удалится в update */ }
        }
        if (!aimMob && hasBlock) {
          if (gIn.mouse(0)) {
            if (look.x != gBreakX || look.y != gBreakY || look.z != gBreakZ) {
              gBreakX = look.x;
              gBreakY = look.y;
              gBreakZ = look.z;
              gBreakProg = 0;
            }
            const BlockDef& D = bdef(look.id);
            if (D.breakable) {
              int tool = itemPickLevel(inv.selected());
              float spd = 1.0f;
              if (D.pick > 0) spd = (tool >= D.pick) ? (tool >= 2 ? 3.5f : 2.2f) : 0.25f;
              else spd = 1.0f + (tool > 0 ? 0.5f : 0.0f);
              gBreakProg += dt * spd / std::max(0.05f, D.hardness);
              if (gBreakProg >= 1.0f) {
                gBreakProg = 0;
                bool canDrop = (D.pick == 0) || (tool >= D.pick);
                setBlockNet(look.x, look.y, look.z, B_AIR);
                if (canDrop && D.drop > 0) inv.add(D.drop, D.dropN);
              }
            }
          } else {
            gBreakProg = 0;
          }
          if (gIn.clicked(2) && isBlockId(look.id)) {
            for (int i = 0; i < 9; i++)
              if (inv.st[i].first == look.id) inv.sel = i;
          }
          if (gIn.clicked(1)) {
            int sel = inv.selected();
            if (isBlockId(sel) && !bdef(sel).liquid) {
              int px = look.x + look.nx, py = look.y + look.ny, pz = look.z + look.nz;
              uint16_t cur = world.getBlock(px, py, pz);
              if (cur == B_AIR || cur == B_WATER || bdef(cur).cross) {
                // player intersection (сетка в вокселях, игрок в мирах)
                float hw = 0.6f;
                bool inter = (g2w(px + 1) > player.pos.x - hw && g2w(px) < player.pos.x + hw &&
                              g2w(pz + 1) > player.pos.z - hw && g2w(pz) < player.pos.z + hw &&
                              g2w(py + 1) > player.pos.y && g2w(py) < player.pos.y + 3.6f);
                if (!inter) {
                  setBlockNet(px, py, pz, (uint16_t)sel);
                  inv.consumeSelected();
                }
              }
            }
          }
        } else {
          gBreakProg = 0;
        }
      }
      if (gInvOpen) {
        // crafting clicks handled below in UI section (need layout) -- store click
      }
    }

    // streaming
    if (shotMode) {
      world.stream(player.pos, 2.0, meshCb);
    } else {
      world.stream(player.pos, gInvOpen ? 16.0 : 10.0, meshCb);
    }
    if (gRT) updateVolume(world, player.pos, false);

    // смерть героя
    if (!gDead && player.hp <= 0) {
      gDead = true;
      player.hp = 0;
      player.vel = glm::vec3(0);
      gInvOpen = false;
      inv.returnHeld();
      glfwSetInputMode(gWin, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
    if (gDead && (gIn.pressed(GLFW_KEY_R) || gIn.pressed(GLFW_KEY_ENTER) || gIn.clicked(0))) {
      gDead = false;
      player.pos = spawnPos;
      player.vel = glm::vec3(0);
      player.hp = player.maxHp;
      player.hurtFx = 0;
      player.flying = false;
      player.airPeak = player.pos.y;
      player.wasAir = false;
      gRespawnFlash = 1.0f;
      // убрать зомби вокруг точки возрождения
      for (auto& m : mobs.mobs) {
        float dx = m.x - player.pos.x, dz = m.z - player.pos.z;
        if (dx * dx + dz * dz < 40.0f * 40.0f) m.hp = 0;
      }
      glfwSetInputMode(gWin, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
      gIn.firstMouse = true;
    }

    // сеть и мобы
    net.setOwnPos(player.pos.x, player.pos.y, player.pos.z, player.yaw);
    net.pump(world);
    if (net.worldDirty) {
      gVolDirty = true;
      net.worldDirty = false;
    }
    mobs.update(world, player.pos, dt, day.night, player);

    // camera
    camPos = shotMode ? (std::isnan(gShotPos.x) ? player.eye() : player.eye()) : player.eye();
    if (shotMode && !std::isnan(gShotPos.x)) camPos = player.pos + glm::vec3(0, 1.62f, 0);
    glm::mat4 view = lookAt(camPos, camPos + player.forward(), glm::vec3(0, 1, 0));
    int fbw, fbh;
    glfwGetFramebufferSize(gWin, &fbw, &fbh);
    glm::mat4 proj = perspective(radians(70.0f), (float)fbw / (float)fbh, 0.05f, 700.0f);
    glm::mat4 vp = proj * view;
    glm::mat4 invVP = inverse(vp);

    // ---- render ----
    float tRel = (float)(now - tStart);
    bool usePost = !gRT;  // сцена всегда в текстуру: пикселизация дальних объектов + опциональная обводка
    if (usePost) {
      sceneTgt.ensure(std::max(64, fbw), std::max(64, fbh));
      sceneTgt.bind();
    } else {
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    glViewport(0, 0, fbw, fbh);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glClearColor(day.fog.r, day.fog.g, day.fog.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (usePost) {
      const float nz4[4] = {0.5f, 0.5f, 0.5f, 1.0f};  // «нулевая» нормаль для неба
      glClearBufferfv(GL_COLOR, 1, nz4);
    }

    if (gRT) {
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      glViewport(0, 0, fbw, fbh);
      glDisable(GL_DEPTH_TEST);
      rtProg.use();
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_3D, gVolTex);
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, atlasTex);
      glActiveTexture(GL_TEXTURE2);
      glBindTexture(GL_TEXTURE_2D, gTileTex);
      rtProg.i1("uVol", 0);
      rtProg.i1("uAtlas", 1);
      rtProg.i1("uTileMap", 2);
      rtProg.m4("uInvVP", invVP);
      rtProg.v3("uCamPos", glm::vec3(camPos.x / VX, camPos.y / VX, camPos.z / VX));  // объём в единой воксельной сетке (все оси /VX)
      rtProg.v3("uSunDir", day.sunDir);
      rtProg.v3("uSunColor", day.sun);
      rtProg.v3("uAmbSky", day.ambSky);
      rtProg.v3("uAmbGround", day.ambGround);
      rtProg.v3("uFogColor", day.fog);
      rtProg.v3("uBlockCol", day.blockCol);
      rtProg.v3("uVolOrigin", glm::vec3((float)gVolOx, 0.0f, (float)gVolOz));
      rtProg.v3("uVolSize", glm::vec3((float)VOL_S, (float)VOL_Y, (float)VOL_S));
      rtProg.f1("uFogDensity", 0.00003f * VX);
      rtProg.f1("uTime", tRel);
      rtProg.i1("uMaxSteps", 320);
      glBindVertexArray(rtVao);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      glEnable(GL_DEPTH_TEST);
    } else {
      // sky
      glDepthMask(GL_FALSE);
      glDisable(GL_CULL_FACE);
      skyProg.use();
      skyProg.m4("uInvVP", invVP);
      skyProg.v3("uCamPos", camPos);
      skyProg.v3("uSunDir", day.sunDir);
      skyProg.v3("uTop", day.top);
      skyProg.v3("uHorizon", day.horizon);
      skyProg.v3("uSunCol", day.sun);
      skyProg.f1("uNight", day.night);
      skyProg.f1("uTime", tRel);
      glBindVertexArray(skyVao);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      glEnable(GL_CULL_FACE);
      glDepthMask(GL_TRUE);

      // chunks
      chunkProg.use();
      chunkProg.m4("uVP", vp);
      chunkProg.f1("uTime", tRel);
      chunkProg.i1("uAtlas", 0);
      chunkProg.v3("uSunDir", day.sunDir);
      chunkProg.v3("uSunColor", day.sun);
      chunkProg.v3("uAmbSky", day.ambSky);
      chunkProg.v3("uAmbGround", day.ambGround);
      chunkProg.v3("uFogColor", day.fog);
      chunkProg.v3("uCamPos", camPos);
      chunkProg.f1("uFogDensity", 0.00003f);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, atlasTex);
      int pcx = floorDiv(w2g(player.pos.x), CH), pcz = floorDiv(w2g(player.pos.z), CH);
      chunkProg.i1("uWater", 0);
      // видимые чанки: отсечение по фрустуму + сортировка (solid — ближние первыми для early-Z)
      auto row4 = [&](int r) { return glm::vec4(vp[0][r], vp[1][r], vp[2][r], vp[3][r]); };
      glm::vec4 r3 = row4(3);
      glm::vec4 fpl[6] = {r3 + row4(0), r3 - row4(0), r3 + row4(1), r3 - row4(1), r3 + row4(2), r3 - row4(2)};
      for (auto& p : fpl) {
        float len = glm::length(glm::vec3(p));
        if (len > 0) p /= len;
      }
      struct Vis {
        Chunk* c;
        float d2;
      };
      std::vector<Vis> vis;
      vis.reserve(world.chunks.size());
      for (auto& kv : world.chunks) {
        Chunk* c = kv.second;
        if (!c->meshed) continue;
        if (std::abs(c->cx - pcx) > world.renderDist || std::abs(c->cz - pcz) > world.renderDist) continue;
        float x0 = g2w(c->cx * CH), z0 = g2w(c->cz * CH);
        float x1 = x0 + CH * VX, z1 = z0 + CH * VX;
        float y1 = g2w(std::min(WH, c->hmax + 2));
        bool out = false;
        for (int i = 0; i < 6 && !out; i++) {
          const glm::vec4& p = fpl[i];
          float px = p.x >= 0 ? x1 : x0, py = p.y >= 0 ? y1 : 0.0f, pz = p.z >= 0 ? z1 : z0;
          if (p.x * px + p.y * py + p.z * pz + p.w < 0) out = true;
        }
        if (out) continue;
        float dxc = x0 + CH * VX * 0.5f - player.pos.x, dzc = z0 + CH * VX * 0.5f - player.pos.z;
        vis.push_back({c, dxc * dxc + dzc * dzc});
      }
      std::sort(vis.begin(), vis.end(), [](const Vis& a, const Vis& b) { return a.d2 < b.d2; });
      for (auto& v : vis) {
        Chunk* c = v.c;
        chunkProg.v3("uOrigin", glm::vec3(g2w(c->cx * CH), 0.0f, g2w(c->cz * CH)));
        if (c->nv[0]) {
          glBindVertexArray(c->vao[0]);
          glDrawArrays(GL_TRIANGLES, 0, c->nv[0]);
        }
        if (c->nv[2]) {
          glBindVertexArray(c->vao[2]);
          glDrawArrays(GL_TRIANGLES, 0, c->nv[2]);
        }
      }
      // ---- тени мобов: мягкое пятно, спроецированное на землю по солнцу ----
      {
        std::vector<float> sv;
        auto groundAt = [&](int x, int z, float fromY) {
          for (int y = w2g(fromY); y >= 0; y--)
            if (isSolidId(world.getBlock(x, y, z))) return g2w(y + 1);
          return -1e9f;
        };
        auto addShadow = [&](float mx, float my, float mz, float yaw, float hw, float hl) {
          int gx = w2g(mx), gz = w2g(mz);
          if (world.getBlock(gx, w2g(my + 0.4f), gz) == B_WATER) return;
          float se = day.sunDir.y;
          if (se < 0.06f) return;
          float gy = groundAt(gx, gz, my + 1.0f);
          if (gy < -1e8f) return;
          float h = my - gy + 1.2f;
          float ox = -day.sunDir.x / se * h, oz = -day.sunDir.z / se * h;
          float ol = sqrtf(ox * ox + oz * oz);
          if (ol > 4.0f) {
            ox *= 4.0f / ol;
            oz *= 4.0f / ol;
          }
          float sx = mx + ox, sz = mz + oz;
          float sy = groundAt(w2g(sx), w2g(sz), my + 2.5f);
          if (sy < -1e8f) return;
          if (world.getBlock(w2g(sx), w2g(sy + 0.05f), w2g(sz)) == B_WATER) return;
          sy += 0.03f;
          float fadeH = 1.0f - std::max(0.0f, (my - gy - 5.0f) / 4.0f);
          float a = 0.40f * smoothstep(0.06f, 0.30f, se) * fadeH;
          if (a < 0.02f) return;
          float c = cosf(yaw), sn = sinf(yaw);
          float lx[4] = {-hw, hw, hw, -hw}, lz[4] = {-hl, -hl, hl, hl};
          float uu[4] = {0, 1, 1, 0}, vv[4] = {0, 0, 1, 1};
          int idx[6] = {0, 2, 1, 0, 3, 2};
          for (int i = 0; i < 6; i++) {
            int k = idx[i];
            sv.push_back(sx + lx[k] * c + lz[k] * sn);
            sv.push_back(sy);
            sv.push_back(sz - lx[k] * sn + lz[k] * c);
            sv.push_back(uu[k]);
            sv.push_back(vv[k]);
            sv.push_back(a);
          }
        };
        for (auto& m : mobs.mobs) {
          if (m.type == 0)
            addShadow(m.x, m.y, m.z, m.yaw, 0.95f, 1.35f);
          else
            addShadow(m.x, m.y, m.z, m.yaw, 0.85f, 0.85f);
        }
        for (auto& r : net.remotes)
          if (r.valid) addShadow(r.x, r.y, r.z, r.yaw, 0.85f, 0.85f);
        if (!sv.empty()) {
          shadowProg.use();
          shadowProg.m4("uVP", vp);
          shadowProg.v3("uCamPos", camPos);
          shadowProg.v3("uFogCol", day.fog);
          shadowProg.f1("uFogD", 0.00003f);
          glEnable(GL_BLEND);
          glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
          glDepthMask(GL_FALSE);
          glEnable(GL_POLYGON_OFFSET_FILL);
          glPolygonOffset(-1.0f, -1.0f);
          glBindVertexArray(shadowVao);
          glBindBuffer(GL_ARRAY_BUFFER, shadowVbo);
          glBufferData(GL_ARRAY_BUFFER, sv.size() * sizeof(float), sv.data(), GL_STREAM_DRAW);
          glDrawArrays(GL_TRIANGLES, 0, (GLsizei)sv.size() / 6);
          glDisable(GL_POLYGON_OFFSET_FILL);
          glDepthMask(GL_TRUE);
          glDisable(GL_BLEND);
          glBindVertexArray(0);
        }
      }
      // water — самодостаточный пасс: после теней мобов (shadowProg) явно
      // восстанавливаем chunkProg и все uniform'ы, не полагаясь на сплошной пасс
      chunkProg.use();
      chunkProg.m4("uVP", vp);
      chunkProg.f1("uTime", tRel);
      chunkProg.i1("uAtlas", 0);
      chunkProg.v3("uSunDir", day.sunDir);
      chunkProg.v3("uSunColor", day.sun);
      chunkProg.v3("uAmbSky", day.ambSky);
      chunkProg.v3("uAmbGround", day.ambGround);
      chunkProg.v3("uFogColor", day.fog);
      chunkProg.v3("uCamPos", camPos);
      chunkProg.f1("uFogDensity", 0.00003f);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, atlasTex);
      chunkProg.i1("uWater", 1);
      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      glDepthMask(GL_FALSE);
      glDisable(GL_CULL_FACE);
      for (int vi = (int)vis.size() - 1; vi >= 0; vi--) {  // дальние первыми — корректное смешивание воды
        Chunk* c = vis[vi].c;
        if (!c->nv[1]) continue;
        chunkProg.v3("uOrigin", glm::vec3(g2w(c->cx * CH), 0.0f, g2w(c->cz * CH)));
        glBindVertexArray(c->vao[1]);
        glDrawArrays(GL_TRIANGLES, 0, c->nv[1]);
      }
      glBindVertexArray(0);
      glEnable(GL_CULL_FACE);
      glDepthMask(GL_TRUE);
      glDisable(GL_BLEND);
      chunkProg.i1("uWater", 0);  // вернуть состояние для следующего кадра

      // мобы и удалённые игроки — цветные кубы (uModel: сдвиг + поворот yaw + масштаб)
      colorProg.use();
      colorProg.m4("uVP", vp);
      colorProg.v3("uSunDir", day.sunDir);
      colorProg.v3("uSunColor", day.sun);
      colorProg.v3("uAmbSky", day.ambSky);
      colorProg.v3("uAmbGround", day.ambGround);
      colorProg.v3("uFogColor", day.fog);
      colorProg.v3("uCamPos", camPos);
      colorProg.f1("uFogDensity", 0.00003f);
      glBindVertexArray(cubeVao);
      // lx,ly,lz — минимальный угол куба в локальных координатах моба (+Z — лицо)
      auto box = [&](float wx, float wy, float wz, float yaw, float lx, float ly, float lz, float sx, float sy, float sz, float r,
                     float g, float b) {
        glm::mat4 mo(1.0f);
        mo = glm::translate(mo, glm::vec3(wx, wy, wz));
        mo = glm::rotate(mo, yaw, glm::vec3(0, 1, 0));
        mo = glm::translate(mo, glm::vec3(lx, ly, lz));
        mo = glm::scale(mo, glm::vec3(sx, sy, sz));
        colorProg.m4("uModel", mo);
        colorProg.v3("uColor", glm::vec3(r, g, b));
        glDrawArrays(GL_TRIANGLES, 0, 36);
      };
      for (auto& m : mobs.mobs) {
        colorProg.f1("uHurt", m.hurt);
        float leg = m.moveAmt > 0.1f ? sinf(m.walkPhase) * 0.18f : 0.0f;
        if (m.type == 0) {  // свинья (воксель вдвое мельче — модель вдвое больше)
          box(m.x, m.y, m.z, m.yaw, -0.64f, 0.56f, -1.0f, 1.28f, 0.96f, 1.9f, 0.96f, 0.63f, 0.66f);
          box(m.x, m.y, m.z, m.yaw, -0.4f, 0.64f, 0.84f, 0.8f, 0.8f, 0.76f, 0.94f, 0.6f, 0.63f);
          box(m.x, m.y, m.z, m.yaw, -0.18f, 0.8f, 1.52f, 0.36f, 0.28f, 0.2f, 0.9f, 0.48f, 0.55f);
          box(m.x, m.y, m.z, m.yaw, -0.56f, 0.0f, -0.84f + leg, 0.36f, 0.6f, 0.36f, 0.88f, 0.55f, 0.58f);
          box(m.x, m.y, m.z, m.yaw, 0.2f, 0.0f, -0.84f - leg, 0.36f, 0.6f, 0.36f, 0.88f, 0.55f, 0.58f);
          box(m.x, m.y, m.z, m.yaw, -0.56f, 0.0f, 0.48f - leg, 0.36f, 0.6f, 0.36f, 0.88f, 0.55f, 0.58f);
          box(m.x, m.y, m.z, m.yaw, 0.2f, 0.0f, 0.48f + leg, 0.36f, 0.6f, 0.36f, 0.88f, 0.55f, 0.58f);
        } else {  // зомби
          box(m.x, m.y, m.z, m.yaw, -0.5f, 1.5f, -0.28f, 1.0f, 1.24f, 0.56f, 0.18f, 0.48f, 0.52f);
          box(m.x, m.y, m.z, m.yaw, -0.48f, 2.74f, -0.48f, 0.96f, 0.96f, 0.96f, 0.38f, 0.74f, 0.44f);
          box(m.x, m.y, m.z, m.yaw, -0.42f, 0.0f, -0.22f + leg, 0.4f, 1.5f, 0.44f, 0.28f, 0.42f, 0.62f);
          box(m.x, m.y, m.z, m.yaw, 0.02f, 0.0f, -0.22f - leg, 0.4f, 1.5f, 0.44f, 0.28f, 0.42f, 0.62f);
          box(m.x, m.y, m.z, m.yaw, -0.92f, 1.7f, 0.16f - leg, 0.4f, 1.1f, 0.4f, 0.38f, 0.74f, 0.44f);
          box(m.x, m.y, m.z, m.yaw, 0.52f, 1.7f, 0.16f + leg, 0.4f, 1.1f, 0.4f, 0.38f, 0.74f, 0.44f);
        }
      }
      colorProg.f1("uHurt", 0.0f);
      static const float PCOL[4][3] = {{0.9f, 0.3f, 0.25f}, {0.3f, 0.5f, 0.95f}, {0.95f, 0.8f, 0.25f}, {0.6f, 0.35f, 0.85f}};
      for (auto& r : net.remotes) {
        if (!r.valid) continue;
        const float* c = PCOL[r.id % 4];
        box(r.x, r.y, r.z, r.yaw, -0.5f, 1.5f, -0.28f, 1.0f, 1.24f, 0.56f, c[0] * 0.7f, c[1] * 0.7f, c[2] * 0.75f);
        box(r.x, r.y, r.z, r.yaw, -0.48f, 2.74f, -0.48f, 0.96f, 0.96f, 0.96f, 0.95f, 0.78f, 0.6f);
        box(r.x, r.y, r.z, r.yaw, -0.42f, 0.0f, -0.22f, 0.4f, 1.5f, 0.44f, c[0] * 0.45f, c[1] * 0.45f, c[2] * 0.5f);
        box(r.x, r.y, r.z, r.yaw, 0.02f, 0.0f, -0.22f, 0.4f, 1.5f, 0.44f, c[0] * 0.45f, c[1] * 0.45f, c[2] * 0.5f);
        box(r.x, r.y, r.z, r.yaw, -0.92f, 1.7f, 0.16f, 0.4f, 1.1f, 0.4f, 0.95f, 0.78f, 0.6f);
        box(r.x, r.y, r.z, r.yaw, 0.52f, 1.7f, 0.16f, 0.4f, 1.1f, 0.4f, 0.95f, 0.78f, 0.6f);
      }
      glBindVertexArray(0);

      // selection
      if (world.raycast(camPos, player.forward(), 12.0f, look) && look.hit && !gInvOpen) {
        float e = 0.002f;
        float x0 = g2w(look.x) - e, y0 = g2w(look.y) - e, z0 = g2w(look.z) - e;
        float x1 = g2w(look.x + 1) + e, y1 = g2w(look.y + 1) + e, z1 = g2w(look.z + 1) + e;
        float verts[] = {
            x0, y0, z0, x1, y0, z0, x1, y0, z0, x1, y0, z1, x1, y0, z1, x0, y0, z1, x0, y0, z1, x0, y0, z0,
            x0, y1, z0, x1, y1, z0, x1, y1, z0, x1, y1, z1, x1, y1, z1, x0, y1, z1, x0, y1, z1, x0, y1, z0,
            x0, y0, z0, x0, y1, z0, x1, y0, z0, x1, y1, z0, x0, y0, z1, x0, y1, z1, x1, y0, z1, x1, y1, z1,
        };
        lineProg.use();
        lineProg.m4("uVP", vp);
        lineProg.v4("uColor", glm::vec4(UIT::AR, UIT::AG, UIT::AB, 0.95f));
        glBindVertexArray(lineVao);
        glBindBuffer(GL_ARRAY_BUFFER, lineVbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STREAM_DRAW);
        // нормаль-буфер обводки не трогаем (иначе обводка линий сама обведётся)
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
        glLineWidth(2.0f);
        glDrawArrays(GL_LINES, 0, 24);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
      }
      // оверлей трещин на ломаемом блоке
      if (gBreakProg > 0.001f && look.hit) {
        breakProg.use();
        breakProg.m4("uVP", vp);
        breakProg.v3("uOrigin", glm::vec3(g2w(look.x), g2w(look.y), g2w(look.z)));
        breakProg.f1("uBlk", VX);
        breakProg.f1("uProg", gBreakProg);
        breakProg.v3("uCamPos", camPos);
        breakProg.v3("uFogColor", day.fog);
        breakProg.f1("uFogDensity", 0.00003f);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        glBindVertexArray(cubeVao);
        glDrawArrays(GL_TRIANGLES, 0, 36);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
      }
    }

    // ---- пост-эффект: чёрная обводка контуров (как в мультиках) ----
    if (usePost) {
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      glViewport(0, 0, fbw, fbh);
      glDisable(GL_DEPTH_TEST);
      glDisable(GL_BLEND);
      outlineProg.use();
      outlineProg.v2("uRes", (float)fbw, (float)fbh);
      outlineProg.f1("uNear", 0.05f);
      outlineProg.f1("uFar", 700.0f);
      outlineProg.f1("uThick", std::max(1.0f, fbh / 800.0f));
      outlineProg.f1("uStrength", 0.95f);
      outlineProg.f1("uOutline", gOutline ? 1.0f : 0.0f);
      outlineProg.v3("uPix", glm::vec3(28.0f, 130.0f, 3.0f));
      outlineProg.i1("uCol", 0);
      outlineProg.i1("uNrm", 1);
      outlineProg.i1("uDepth", 2);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, sceneTgt.col);
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, sceneTgt.nrm);
      glActiveTexture(GL_TEXTURE2);
      glBindTexture(GL_TEXTURE_2D, sceneTgt.dep);
      glBindVertexArray(outlineVao);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      glBindVertexArray(0);
      glActiveTexture(GL_TEXTURE0);
    }

    // ---- UI ----
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);
    ubR.clear();
    ubI.clear();
    ubT.clear();
    // масштаб HUD от размера окна + поправка на HiDPI (курсор в окне, рендер в фреймбуфере)
    float uiScale = std::min(fbw / 1280.0f, fbh / 720.0f);
    uiScale = std::max(0.7f, std::min(4.0f, uiScale));
    int winW = 1, winH = 1;
    glfwGetWindowSize(gWin, &winW, &winH);
    float mX = (float)gIn.mx * (float)fbw / (float)std::max(1, winW);
    float mY = (float)gIn.my * (float)fbh / (float)std::max(1, winH);
    auto text = [&](const char* s, float x, float y, float size, float r, float g, float b, float a = 1) {
      if (haveFont) font.draw(ubT, s, x, y, size, r, g, b, a);
    };
    // текст с мягкой тенью — заголовки/подписи
    auto textSh = [&](const char* s, float x, float y, float size, float r, float g, float b, float a = 1, float sh = 0.6f) {
      float o = std::max(1.0f, size * 0.05f);
      text(s, x + o, y + o, size, 0, 0, 0, a * sh);
      text(s, x, y, size, r, g, b, a);
    };
    if (!gRT || !shotMode) {
      drawCrosshair(ubR, fbw, fbh, uiScale);
      drawHotbar(ubR, ubI, inv, fbw, fbh, uiScale);
      // сердечки над хотбаром (на своей плашке)
      float hbSlot = 46.0f * uiScale, hbGap = 4.0f * uiScale;
      float hbTotal = 9 * hbSlot + 8 * hbGap;
      drawHearts(ubR, player.hp, player.maxHp, fbw * 0.5f - hbTotal * 0.5f, fbh - hbSlot - 12.0f * uiScale - 40.0f * uiScale,
                 uiScale);
    }
    // вспышка урона + подводная дымка
    if (player.hurtFx > 0.001f)
      ubR.rect(0, 0, (float)fbw, (float)fbh, 0.75f, 0.05f, 0.05f, 0.28f * player.hurtFx);
    {
      uint16_t eyeB = world.getBlock(w2g(camPos.x), w2g(camPos.y), w2g(camPos.z));
      if (eyeB == B_WATER) ubR.rect(0, 0, (float)fbw, (float)fbh, 0.08f, 0.25f, 0.55f, 0.38f);
    }
    if (gRespawnFlash > 0.001f) {
      ubR.rect(0, 0, (float)fbw, (float)fbh, 1, 1, 1, 0.5f * gRespawnFlash);
      gRespawnFlash = std::max(0.0f, gRespawnFlash - dt * 2.0f);
    }
    if (!gInvOpen && !shotMode && !gDead) {
      int sel = inv.selected();
      if (sel) {
        float slotPx = 46.0f * uiScale;
        float nameY = fbh - slotPx - 12.0f * uiScale - 40.0f * uiScale - 24.0f * uiScale;
        float wpx = font.width(itemName(sel), 18.0f * uiScale);
        float pad = 8.0f * uiScale;
        ubR.rect(fbw * 0.5f - wpx * 0.5f - pad, nameY - 4 * uiScale, fbw * 0.5f + wpx * 0.5f + pad,
                 nameY + 24.0f * uiScale, 0, 0, 0, 0.45f);
        textSh(itemName(sel), fbw * 0.5f - wpx * 0.5f, nameY, 18.0f * uiScale, 1, 1, 1, 0.95f);
      }
      if (gBreakProg > 0.01f) {
        float bw = 60 * uiScale, by0 = fbh * 0.5f + 20 * uiScale;
        ubR.rect(fbw * 0.5f - bw - 2, by0 - 2, fbw * 0.5f + bw + 2, by0 + 8 * uiScale + 2, 0, 0, 0, 0.6f);
        ubR.rect(fbw * 0.5f - bw, by0, fbw * 0.5f + bw, by0 + 8 * uiScale, 0.10f, 0.10f, 0.12f, 0.9f);
        ubR.rect(fbw * 0.5f - bw, by0, fbw * 0.5f - bw + bw * 2 * std::min(1.0f, gBreakProg), by0 + 8 * uiScale,
                 UIT::AR, UIT::AG, UIT::AB, 0.95f);
      }
    }
    if (gDebug && !shotMode) {
      char buf[512];
      snprintf(buf, sizeof(buf),
               "FPS %.0f | XYZ %.1f %.1f %.1f | chunk %d,%d | tod %.2f %s | chunks %d | HP %d | RT %s | обводка %s",
               fps, player.pos.x, player.pos.y, player.pos.z, floorDiv(w2g(player.pos.x), CH), floorDiv(w2g(player.pos.z), CH),
               gTod, day.dayF > 0.5f ? "день" : "ночь", (int)world.chunks.size(), player.hp, gRT ? "вкл" : "выкл",
               gOutline ? "вкл" : "выкл");
      uiPanel(ubR, 6, 6, 16 + font.width(buf, 15), 30, uiScale);
      textSh(buf, 14, 10, 15, 1, 1, 1, 1);
      snprintf(buf, sizeof(buf), "Лес %.2f | поверхность %d | режим: %s", world.forestDensity(w2g(player.pos.x), w2g(player.pos.z)),
               world.surfaceY(w2g(player.pos.x), w2g(player.pos.z)), player.flying ? "полёт" : "ходьба");
      uiPanel(ubR, 6, 34, 16 + font.width(buf, 15), 30, uiScale);
      textSh(buf, 14, 38, 15, 1, 1, 1, 1);
    }
    if (gRT && !shotMode) {
      const char* msg = "РЕЖИМ ТРАССИРОВКИ ЛУЧЕЙ (F5)";
      float wpx = font.width(msg, 16);
      uiRRect(ubR, fbw * 0.5f - wpx * 0.5f - 12, 8, fbw * 0.5f + wpx * 0.5f + 12, 36, 6 * uiScale, UIT::BR, UIT::BG, UIT::BB, 0.9f);
      ubR.rect(fbw * 0.5f - wpx * 0.5f - 12, 8, fbw * 0.5f + wpx * 0.5f + 12, 11, UIT::AR, UIT::AG, UIT::AB, 0.9f);
      textSh(msg, fbw * 0.5f - wpx * 0.5f, 13, 16, UIT::AR, UIT::AG, UIT::AB, 1);
    }
    if (gOutline && !gRT && !shotMode) {
      const char* msg = "ОБВОДКА КОНТУРОВ: ВКЛЮЧЕНА (O)";
      float wpx = font.width(msg, 16);
      float by0 = 44.0f;
      uiRRect(ubR, fbw * 0.5f - wpx * 0.5f - 12, by0, fbw * 0.5f + wpx * 0.5f + 12, by0 + 28, 6 * uiScale, UIT::BR, UIT::BG, UIT::BB, 0.9f);
      ubR.rect(fbw * 0.5f - wpx * 0.5f - 12, by0, fbw * 0.5f + wpx * 0.5f + 12, by0 + 3, UIT::AR, UIT::AG, UIT::AB, 0.9f);
      textSh(msg, fbw * 0.5f - wpx * 0.5f, by0 + 5, 16, UIT::AR, UIT::AG, UIT::AB, 1);
    }
    if (gDead && (!shotMode || gShotDead)) {
      ubR.rect(0, 0, (float)fbw, (float)fbh, 0.35f, 0.02f, 0.02f, 0.55f);
      const char* t1 = "ВЫ ПОГИБЛИ";
      float w1 = font.width(t1, 52.0f * uiScale);
      float px1 = fbw * 0.5f - w1 * 0.5f - 30.0f * uiScale, py1 = fbh * 0.32f - 16.0f * uiScale;
      uiShadow(ubR, px1, py1, fbw * 0.5f + w1 * 0.5f + 30.0f * uiScale, py1 + 68.0f * uiScale, 6.0f * uiScale, 0.5f);
      uiRRect(ubR, px1 - 1.5f * uiScale, py1 - 1.5f * uiScale, fbw * 0.5f + w1 * 0.5f + 31.5f * uiScale,
              py1 + 69.5f * uiScale, 10.0f * uiScale, 0.55f, 0.16f, 0.14f, 0.95f);
      uiRRect(ubR, px1, py1, fbw * 0.5f + w1 * 0.5f + 30.0f * uiScale, py1 + 68.0f * uiScale, 9.0f * uiScale, 0.12f, 0.03f, 0.04f, 0.95f);
      textSh(t1, fbw * 0.5f - w1 * 0.5f, fbh * 0.32f, 52.0f * uiScale, 1.0f, 0.25f, 0.2f, 1, 0.7f);
      const char* t2 = "R / ENTER / ЛКМ - возродиться";
      float w2 = font.width(t2, 22.0f * uiScale);
      textSh(t2, fbw * 0.5f - w2 * 0.5f, fbh * 0.32f + 88.0f * uiScale, 22.0f * uiScale, 1, 1, 1, 0.9f);
    }
    if (gInvOpen && (!shotMode || gShotInv)) {
      float s = uiScale;
      float pw = 720 * s, ph = 500 * s;
      float px = fbw * 0.5f - pw * 0.5f, py = fbh * 0.5f - ph * 0.5f;
      ubR.rect(0, 0, (float)fbw, (float)fbh, 0, 0, 0, 0.5f);
      uiPanel(ubR, px, py, px + pw, py + ph, s, 40 * s);
      ubR.rect(px, py + 40 * s, px + pw, py + 43 * s, UIT::AR, UIT::AG, UIT::AB, 0.85f);
      textSh("Инвентарь и крафт", px + 14 * s, py + 9 * s, 20 * s, 1, 1, 1, 1);
      bool table = nearCraftingTable(world, player.pos);
      const char* tmsg = table ? "Верстак рядом: да" : "Верстак рядом: нет";
      float tw = font.width(tmsg, 16 * s);
      uiRRect(ubR, px + pw - tw - 30 * s, py + 7 * s, px + pw - 14 * s, py + 31 * s, 6 * s, UIT::HR, UIT::HG, UIT::HB, 0.95f);
      text(tmsg, px + pw - tw - 22 * s, py + 10 * s, 16 * s, table ? 0.45f : 1.0f, table ? 1.0f : 0.72f, 0.42f, 1);
      float ix = px + 16 * s, iy = py + 56 * s;
      text("Предметы (ЛКМ - взять/положить, ПКМ - половина/по одному)", ix, iy, 14 * s, 0.72f, 0.75f, 0.82f, 1);
      iy += 26 * s;
      float slot = 44 * s;
      for (int i = 0; i < 36; i++) {
        float sx = ix + (i % 9) * (slot + 4 * s), sy = iy + (i / 9) * (slot + 4 * s);
        bool hov = mX >= sx && mX <= sx + slot && mY >= sy && mY <= sy + slot;
        if (i < 9) {
          ubR.rect(sx - 3 * s, sy - 3 * s, sx + slot + 3 * s, sy + slot + 3 * s, UIT::AR, UIT::AG, UIT::AB, 0.16f);
          uiRRect(ubR, sx - 3 * s, sy - 3 * s, sx + slot + 3 * s, sy - 1.4f * s, 4 * s, UIT::AR, UIT::AG, UIT::AB, 0.75f);
        }
        uiSlot(ubR, sx, sy, slot, s, hov, false);
        if (inv.st[i].first) {
          drawItemIcon(ubI, inv.st[i].first, sx + 6 * s, sy + 6 * s, slot - 12 * s);
          if (inv.st[i].second > 1) {
            char nb[16];
            snprintf(nb, sizeof(nb), "%d", inv.st[i].second);
            textSh(nb, sx + slot - font.width(nb, 13 * s) - 3 * s, sy + slot - 16 * s, 13 * s, 1, 1, 1, 1, 0.85f);
          }
        }
        if (hov && gIn.clicked(0)) {
          inv.clickSlot(i);
          if (i < 9) inv.sel = i;
        }
        if (hov && gIn.clicked(2)) inv.clickSlotHalf(i);
      }
      float rx = px + 460 * s, ry = py + 56 * s, rw = pw - 460 * s - 16 * s;
      text("Рецепты", rx, ry, 16 * s, UIT::AR, UIT::AG, UIT::AB, 1);
      ry += 26 * s;
      for (int i = 0; i < RECIPE_COUNT; i++) {
        const Recipe& r = RECIPES[i];
        float rh = 58 * s;
        float sy = ry + i * (rh + 6 * s) - gCraftScroll * s;
        if (sy + rh < py + 40 * s || sy > py + ph - 40 * s) continue;
        bool can = true;
        for (int j = 0; j < r.nIn; j++)
          if (inv.count(r.in[j][0]) < r.in[j][1]) can = false;
        if (r.table && !table) can = false;
        bool hov = mX >= rx && mX <= rx + rw && mY >= sy && mY <= sy + rh;
        float ca = can ? 1.0f : 0.55f;
        uiShadow(ubR, rx, sy, rx + rw, sy + rh, 2.5f * s, 0.4f);
        uiRRect(ubR, rx - 1.5f * s, sy - 1.5f * s, rx + rw + 1.5f * s, sy + rh + 1.5f * s, 7 * s,
                can ? UIT::AR : UIT::SR, can ? UIT::AG : UIT::SG, can ? UIT::AB : UIT::SB, can ? (hov ? 0.95f : 0.7f) : 0.5f);
        uiRRect(ubR, rx, sy, rx + rw, sy + rh, 6 * s, can ? (hov ? 0.13f : 0.10f) : 0.075f,
                can ? (hov ? 0.14f : 0.115f) : 0.078f, can ? (hov ? 0.18f : 0.15f) : 0.10f, 0.97f);
        ubR.rect(rx, sy, rx + 3.5f * s, sy + rh, UIT::AR, UIT::AG, UIT::AB, can ? (hov ? 1.0f : 0.85f) : 0.3f);
        drawItemIcon(ubI, r.out, rx + 12 * s, sy + 11 * s, 34 * s);
        float nfsz = 17 * s;
        while (nfsz > 12 * s && font.width(r.name, nfsz) > rw - 64 * s) nfsz -= 0.5f * s;
        textSh(r.name, rx + 56 * s, sy + 7 * s, nfsz, 1, 1, 1, ca);
        char ing[256];
        ing[0] = 0;
        for (int j = 0; j < r.nIn; j++) {
          char one[64];
          snprintf(one, sizeof(one), "%s%d %s", j ? ", " : "", r.in[j][1], itemName(r.in[j][0]));
          strncat(ing, one, sizeof(ing) - strlen(ing) - 1);
        }
        char line[320];
        snprintf(line, sizeof(line), "нужно: %s  ->  %dx %s%s", ing, r.outN, itemName(r.out), r.table ? " (верстак)" : "");
        float fsz = 13 * s;
        float maxW = rw - 64 * s;
        while (fsz > 9 * s && font.width(line, fsz) > maxW) fsz -= 0.5f * s;
        text(line, rx + 56 * s, sy + 31 * s, fsz, 0.68f, 0.71f, 0.76f, ca);
        if (hov && gIn.clicked(0) && can) {
          for (int j = 0; j < r.nIn; j++) inv.consume(r.in[j][0], r.in[j][1]);
          inv.add(r.out, r.outN);
        }
      }
      if (gIn.wheel != 0) gCraftScroll = std::max(0.0f, gCraftScroll - gIn.wheel * 40);
      ubR.rect(px, py + ph - 32 * s, px + pw, py + ph, UIT::HR, UIT::HG, UIT::HB, 0.8f);
      ubR.rect(px, py + ph - 32 * s, px + pw, py + ph - 30.5f * s, 0, 0, 0, 0.45f);
      textSh("E / ESC - закрыть   |   ЛКМ - скрафтить", px + 16 * s, py + ph - 25 * s, 15 * s, 0.82f, 0.84f, 0.88f, 1);
      if (inv.heldN > 0) {
        drawItemIcon(ubI, inv.heldId, mX - 18 * s, mY - 18 * s, 36 * s);
        if (inv.heldN > 1) {
          char nb[16];
          snprintf(nb, sizeof(nb), "%d", inv.heldN);
          textSh(nb, mX + 10 * s, mY + 10 * s, 14 * s, 1, 1, 1, 1, 0.85f);
        }
      }
    }
    ubR.flush(uiProg, whiteTex, fbw, fbh);
    ubI.flush(uiProg, atlasTex, fbw, fbh);
    ubT.flush(uiProg, font.tex, fbw, fbh);

    glfwSwapBuffers(gWin);
    gIn.endFrame();
    frame++;
    fpsN++;
    if (now - fpsT >= 1.0) {
      fps = (float)(fpsN / (now - fpsT));
      fpsT = now;
      fpsN = 0;
      printf("fps %.0f pos %.1f %.1f %.1f chunks %d mobs %d tod %.2f %s\n", fps, player.pos.x, player.pos.y, player.pos.z,
             (int)world.chunks.size(), (int)mobs.mobs.size(), gTod, day.dayF > 0.5f ? "day" : "night");
    }
    if (shotMode && frame >= gShotFrames) running = false;
  }

  if (shotMode) {
    std::vector<uint8_t> px((size_t)gW * gH * 4);
    int fbw, fbh;
    glfwGetFramebufferSize(gWin, &fbw, &fbh);
    px.assign((size_t)fbw * fbh * 4, 0);
    glReadPixels(0, 0, fbw, fbh, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    std::vector<uint8_t> flip((size_t)fbw * fbh * 4);
    for (int y = 0; y < fbh; y++) memcpy(&flip[(size_t)y * fbw * 4], &px[(size_t)(fbh - 1 - y) * fbw * 4], (size_t)fbw * 4);
    stbi_write_png(gShotPath.c_str(), fbw, fbh, 4, flip.data(), fbw * 4);
    printf("saved %s\n", gShotPath.c_str());
  }
  glfwTerminate();
  return 0;
}
