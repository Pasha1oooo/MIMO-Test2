#pragma once
#include "common.h"
#include "world.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <string>
#include <vector>

struct NetEdit {
  int x = 0, y = 0, z = 0;
  uint16_t id = 0;
  bool applied = false;
};

struct NetPeer {
  int fd = -1;
  int id = 0;
  std::string in, out;
  float px = 0, py = 0, pz = 0, yaw = 0;
  bool helloDone = false;
};

struct RemotePlayer {
  int id = 0;
  float x = 0, y = 0, z = 0, yaw = 0;
  bool valid = false;
};

class Net {
 public:
  bool isServer = false;
  bool connected = false;  // клиент подключён / сервер слушает
  int myId = 0;
  std::string name = "player";
  std::vector<RemotePlayer> remotes;
  std::vector<NetEdit> edits;
  bool worldDirty = false;

  ~Net() { shutdownAll(); }

  bool startServer(int port) {
    listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd < 0) return false;
    int one = 1;
    setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = INADDR_ANY;
    a.sin_port = (uint16_t)htons(port);
    if (bind(listenFd, (sockaddr*)&a, sizeof(a)) < 0 || listen(listenFd, 4) < 0) {
      ::close(listenFd);
      listenFd = -1;
      return false;
    }
    fcntl(listenFd, F_SETFL, O_NONBLOCK);
    isServer = true;
    connected = true;
    myId = 0;
    fprintf(stderr, "net: сервер слушает порт %d (до 4 игроков)\n", port);
    return true;
  }

  bool connectTo(const std::string& host, int port) {
    clientFd = socket(AF_INET, SOCK_STREAM, 0);
    if (clientFd < 0) return false;
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = (uint16_t)htons(port);
    if (inet_pton(AF_INET, host.c_str(), &a.sin_addr) != 1) {
      fprintf(stderr, "net: плохой адрес %s\n", host.c_str());
      ::close(clientFd);
      clientFd = -1;
      return false;
    }
    if (connect(clientFd, (sockaddr*)&a, sizeof(a)) < 0) {
      fprintf(stderr, "net: не удалось подключиться к %s:%d\n", host.c_str(), port);
      ::close(clientFd);
      clientFd = -1;
      return false;
    }
    fcntl(clientFd, F_SETFL, O_NONBLOCK);
    int one = 1;
    setsockopt(clientFd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    connected = true;
    queue(clientFd, clientOut, "HELLO " + name + "\n");
    fprintf(stderr, "net: подключено к %s:%d как '%s'\n", host.c_str(), port, name.c_str());
    return true;
  }

  void sendEdit(int x, int y, int z, uint16_t id) {
    if (!connected) return;
    char buf[96];
    snprintf(buf, sizeof(buf), "SET %d %d %d %u\n", x, y, z, (unsigned)id);
    if (isServer) {
      edits.push_back({x, y, z, id, false});
      worldDirty = true;
      broadcast(buf, -1);
    } else if (clientFd >= 0) {
      queue(clientFd, clientOut, buf);
    }
  }

  void setOwnPos(float x, float y, float z, float yaw) {
    ownX = x;
    ownY = y;
    ownZ = z;
    ownYaw = yaw;
    if (!connected) return;
    double now = nowSec();
    if (now - lastPosSend < 1.0 / 15.0) return;
    lastPosSend = now;
    char buf[128];
    snprintf(buf, sizeof(buf), "POS %.3f %.3f %.3f %.3f\n", x, y, z, yaw);
    if (isServer)
      broadcast(buf, -1);  // сервер виден всем как игрок id=0
    else if (clientFd >= 0)
      queue(clientFd, clientOut, buf);
  }

  void pump(World& w) {
    if (!connected) return;
    if (isServer) {
      acceptPeers();
      for (auto& p : peers) {
        if (p.fd < 0) continue;
        if (!recvInto(p.fd, p.in)) {
          fprintf(stderr, "net: игрок %d отключился\n", p.id);
          char buf[32];
          snprintf(buf, sizeof(buf), "LEAVE %d\n", p.id);
          broadcast(buf, p.id);
          removeRemote(p.id);  // закрывает сокет и убирает из remotes
          continue;
        }
        size_t pos;
        while ((pos = p.in.find('\n')) != std::string::npos) {
          std::string line = p.in.substr(0, pos);
          p.in.erase(0, pos + 1);
          serverLine(w, p, line);
        }
        flushOut(p.fd, p.out);
      }
    } else if (clientFd >= 0) {
      if (!recvInto(clientFd, clientIn)) clientDead = true;
      size_t pos;
      while ((pos = clientIn.find('\n')) != std::string::npos) {
        std::string line = clientIn.substr(0, pos);
        clientIn.erase(0, pos + 1);
        clientLine(w, line);
      }
      flushOut(clientFd, clientOut);
      if (clientDead) {
        fprintf(stderr, "net: соединение с сервером потеряно\n");
        ::close(clientFd);
        clientFd = -1;
        connected = false;
      }
    }
    applyPending(w);
  }

  int playerCount() const {
    if (isServer) {
      int n = 1;
      for (auto& p : peers)
        if (p.fd >= 0 && p.helloDone) n++;
      return n;
    }
    return connected ? 2 : 1;
  }

 private:
  int listenFd = -1, clientFd = -1;
  int nextId = 1;
  std::vector<NetPeer> peers;
  std::string clientIn, clientOut;
  bool clientDead = false;
  float ownX = 0, ownY = 0, ownZ = 0, ownYaw = 0;
  double lastPosSend = 0;

  void shutdownAll() {
    for (auto& p : peers)
      if (p.fd >= 0) ::close(p.fd);
    peers.clear();
    if (clientFd >= 0) ::close(clientFd);
    if (listenFd >= 0) ::close(listenFd);
    clientFd = listenFd = -1;
  }

  static void setNonblock(int fd) { fcntl(fd, F_SETFL, O_NONBLOCK); }

  static void queue(int fd, std::string& out, const std::string& s) {
    if (fd < 0) return;
    out += s;
    flushOut(fd, out);
  }

  static void flushOut(int fd, std::string& out) {
    while (!out.empty()) {
      ssize_t n = send(fd, out.data(), out.size(), MSG_NOSIGNAL);
      if (n > 0) {
        out.erase(0, (size_t)n);
      } else {
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
        out.clear();
        return;
      }
    }
  }

  static bool recvInto(int fd, std::string& in) {
    char buf[1024];
    for (;;) {
      ssize_t n = recv(fd, buf, sizeof(buf), 0);
      if (n > 0) {
        in.append(buf, (size_t)n);
        if (in.size() > 1 << 20) return false;  // защита от мусора
      } else if (n == 0) {
        return false;
      } else {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return true;
        return false;
      }
    }
  }

  void acceptPeers() {
    for (;;) {
      int fd = accept(listenFd, nullptr, nullptr);
      if (fd < 0) break;
      int active = 0;
      for (auto& p : peers)
        if (p.fd >= 0) active++;
      if (active >= 4) {
        fprintf(stderr, "net: отклонён вход (лимит 4 игрока)\n");
        ::close(fd);
        continue;
      }
      setNonblock(fd);
      int one = 1;
      setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
      NetPeer np;
      np.fd = fd;
      np.id = nextId++;
      peers.push_back(np);
      fprintf(stderr, "net: подключение, id=%d\n", peers.back().id);
    }
  }

  void broadcast(const std::string& s, int exceptId) {
    for (auto& p : peers) {
      if (p.fd < 0 || !p.helloDone || p.id == exceptId) continue;
      queue(p.fd, p.out, s);
    }
  }

  void serverLine(World& w, NetPeer& p, const std::string& line) {
    char nameBuf[64];
    float x, y, z, yaw;
    int ix, iy, iz, iid;
    if (sscanf(line.c_str(), "HELLO %63s", nameBuf) == 1) {
      p.helloDone = true;
      char buf[96];
      snprintf(buf, sizeof(buf), "WELCOME %d\n", p.id);
      queue(p.fd, p.out, buf);
      for (auto& e : edits) {
        snprintf(buf, sizeof(buf), "EDIT %d %d %d %u\n", e.x, e.y, e.z, (unsigned)e.id);
        queue(p.fd, p.out, buf);
      }
      // сервер как игрок id=0
      snprintf(buf, sizeof(buf), "PEER 0 %.3f %.3f %.3f %.3f\n", ownX, ownY, ownZ, ownYaw);
      queue(p.fd, p.out, buf);
      for (auto& q : peers) {
        if (q.fd < 0 || !q.helloDone || q.id == p.id) continue;
        snprintf(buf, sizeof(buf), "PEER %d %.3f %.3f %.3f %.3f\n", q.id, q.px, q.py, q.pz, q.yaw);
        queue(p.fd, p.out, buf);
      }
      snprintf(buf, sizeof(buf), "JOIN %d %.3f %.3f %.3f %.3f\n", p.id, p.px, p.py, p.pz, p.yaw);
      broadcast(buf, p.id);
      upsertRemote(p.id, p.px, p.py, p.pz, p.yaw);
      fprintf(stderr, "net: игрок %d '%s' вошёл (всего %d)\n", p.id, nameBuf, playerCount());
      (void)w;
      return;
    }
    if (sscanf(line.c_str(), "POS %f %f %f %f", &x, &y, &z, &yaw) == 4) {
      p.px = x;
      p.py = y;
      p.pz = z;
      p.yaw = yaw;
      if (p.helloDone) {
        char buf[128];
        snprintf(buf, sizeof(buf), "POS %d %.3f %.3f %.3f %.3f\n", p.id, x, y, z, yaw);
        broadcast(buf, p.id);
        upsertRemote(p.id, x, y, z, yaw);
      }
      return;
    }
    if (sscanf(line.c_str(), "SET %d %d %d %d", &ix, &iy, &iz, &iid) == 4) {
      if (p.helloDone) {
        edits.push_back({ix, iy, iz, (uint16_t)iid, false});
        worldDirty = true;
        char buf[96];
        snprintf(buf, sizeof(buf), "SET %d %d %d %d\n", ix, iy, iz, iid);
        broadcast(buf, p.id);
      }
      return;
    }
  }

  void clientLine(World& w, const std::string& line) {
    int id, ix, iy, iz, iid;
    float x, y, z, yaw;
    if (sscanf(line.c_str(), "WELCOME %d", &id) == 1) {
      myId = id;
      fprintf(stderr, "net: сервер принял, мой id=%d\n", myId);
      return;
    }
    if (sscanf(line.c_str(), "EDIT %d %d %d %d", &ix, &iy, &iz, &iid) == 4) {
      edits.push_back({ix, iy, iz, (uint16_t)iid, false});
      worldDirty = true;
      return;
    }
    if (sscanf(line.c_str(), "SET %d %d %d %d", &ix, &iy, &iz, &iid) == 4) {
      edits.push_back({ix, iy, iz, (uint16_t)iid, false});
      worldDirty = true;
      return;
    }
    if (sscanf(line.c_str(), "JOIN %d %f %f %f %f", &id, &x, &y, &z, &yaw) == 5) {
      if (id != myId) upsertRemote(id, x, y, z, yaw);
      fprintf(stderr, "net: игрок %d вошёл в игру\n", id);
      return;
    }
    if (sscanf(line.c_str(), "PEER %d %f %f %f %f", &id, &x, &y, &z, &yaw) == 5) {
      if (id != myId) upsertRemote(id, x, y, z, yaw);
      return;
    }
    if (sscanf(line.c_str(), "POS %d %f %f %f %f", &id, &x, &y, &z, &yaw) == 5) {
      if (id != myId) upsertRemote(id, x, y, z, yaw);
      return;
    }
    if (sscanf(line.c_str(), "LEAVE %d", &id) == 1) {
      removeRemote(id);
      fprintf(stderr, "net: игрок %d вышел\n", id);
      return;
    }
    (void)w;
  }

  // применяет накопленные правки, когда нужный чанк уже сгенерирован
  void applyPending(World& w) {
    for (auto& e : edits) {
      if (e.applied) continue;
      Chunk* c = w.chunkAt(floorDiv(e.x, CH), floorDiv(e.z, CH));
      if (!c || !c->gen) continue;
      w.setBlock(e.x, e.y, e.z, e.id);
      e.applied = true;
      worldDirty = true;
    }
  }

  void upsertRemote(int id, float x, float y, float z, float yaw) {
    for (auto& r : remotes) {
      if (r.id == id) {
        r.x = x;
        r.y = y;
        r.z = z;
        r.yaw = yaw;
        r.valid = true;
        return;
      }
    }
    remotes.push_back({id, x, y, z, yaw, true});
  }

  void removeRemote(int id) {
    for (size_t i = 0; i < remotes.size(); i++) {
      if (remotes[i].id == id) {
        remotes.erase(remotes.begin() + (long)i);
        break;
      }
    }
    for (auto& p : peers)
      if (p.id == id && p.fd >= 0) {
        ::close(p.fd);
        p.fd = -1;
      }
  }
};
