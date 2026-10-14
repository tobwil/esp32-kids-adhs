#include "portal_net.h"

#include <fcntl.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

namespace dk {

static void sleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

// Verbinden mit Zeitlimit; -1 bei Fehler
static int connectTo(const char* host, int port, int timeoutMs) {
  if (!host || !host[0]) return -1;
  addrinfo hints{}, *res = nullptr;
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  char ps[8];
  snprintf(ps, sizeof ps, "%d", port);
  if (getaddrinfo(host, ps, &hints, &res) != 0 || !res) return -1;
  const int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (fd < 0) { freeaddrinfo(res); return -1; }
  const int fl = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, fl | O_NONBLOCK);
  int rc = connect(fd, res->ai_addr, res->ai_addrlen);
  freeaddrinfo(res);
  if (rc != 0) {
    fd_set w;
    FD_ZERO(&w);
    FD_SET(fd, &w);
    timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
    int err = 0;
    socklen_t len = sizeof err;
    if (select(fd + 1, nullptr, &w, nullptr, &tv) != 1 || getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) != 0 || err) { close(fd); return -1; }
  }
  fcntl(fd, F_SETFL, fl);
  timeval rt{1, 0};
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &rt, sizeof rt);
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &rt, sizeof rt);
  return fd;
}

// GET senden und Kopfzeilen überspringen; gibt den Socket zurück (Körper folgt), -1 bei Fehler.
// Bereits mitgelesene Bytes des Körpers landen in extra/nExtra.
static int httpGet(const char* host, int port, const char* path, char* extra, int& nExtra) {
  nExtra = 0;
  const int fd = connectTo(host, port, 1500);
  if (fd < 0) return -1;
  char req[256];
  const int n = snprintf(req, sizeof req, "GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n", path, host);
  if (send(fd, req, n, 0) != n) { close(fd); return -1; }
  char buf[1024];
  int have = 0;
  for (int tries = 0; tries < 6; tries++) {
    const int r = int(recv(fd, buf + have, sizeof buf - 1 - have, 0));
    if (r <= 0) break;
    have += r;
    buf[have] = 0;
    char* end = std::strstr(buf, "\r\n\r\n");
    if (end) {
      if (std::strncmp(buf + 9, "200", 3) != 0) { close(fd); return -1; }   // „HTTP/1.x 200“
      end += 4;
      nExtra = int(buf + have - end);
      std::memcpy(extra, end, nExtra);
      return fd;
    }
    if (have >= int(sizeof buf) - 1) break;
  }
  close(fd);
  return -1;
}

static bool fetch(const char* host, int port, const char* path, char* out, int cap) {
  int n = 0;
  const int fd = httpGet(host, port, path, out, n);
  if (fd < 0) return false;
  for (;;) {
    if (n >= cap - 1) break;
    const int r = int(recv(fd, out + n, cap - 1 - n, 0));
    if (r <= 0) break;
    n += r;
  }
  out[n] = 0;
  close(fd);
  return true;
}

bool portalPoll(RadioPlayer& p, const char* host, int port) {
  char path[256], body[4096];
  bool ok = true;
  while (ok && p.takeAction(path, sizeof path)) ok = fetch(host, port, path, body, sizeof body);
  p.stateQuery(path, sizeof path);
  ok = ok && fetch(host, port, path, body, sizeof body);
  if (ok) p.parse(body);
  p.setOnline(ok);
  return ok;
}

void portalStreamLoop(RadioPlayer& p, const char* (*host)(), int port) {
  static char extra[1024];
  int16_t pcm[512];
  for (;;) {
    char path[64];
    const uint32_t gen = p.wantedStream(path, sizeof path);
    if (!path[0]) { sleepMs(50); continue; }
    int n = 0;
    const int fd = httpGet(host(), port, path, extra, n);
    if (fd < 0) { sleepMs(1000); continue; }
    // Bytes zu Samples; ein ungerades Byte wartet auf das nächste Stück
    uint8_t carry = 0;
    bool hasCarry = false;
    auto push = [&](const uint8_t* b, int len) {
      int k = 0;
      while (k < len) {
        int m = 0;
        if (hasCarry) { pcm[m++] = int16_t(carry | (b[k++] << 8)); hasCarry = false; }
        while (m < 512 && k + 1 < len) { pcm[m++] = int16_t(b[k] | (b[k + 1] << 8)); k += 2; }
        if (k + 1 == len) { carry = b[k++]; hasCarry = true; }
        while (p.ring.space() < m) {
          if (p.wantedStream(path, sizeof path) != gen) return;
          sleepMs(20);
        }
        p.ring.write(pcm, m);
      }
    };
    push(reinterpret_cast<uint8_t*>(extra), n);
    bool ended = false;
    uint8_t buf[1024];
    while (p.wantedStream(path, sizeof path) == gen) {
      if (p.ring.space() < 512) { sleepMs(20); continue; }   // Puffer voll: das Portal wartet (TCP bremst)
      const int r = int(recv(fd, buf, sizeof buf, 0));
      if (r == 0) { ended = true; break; }
      if (r < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) continue;   // Zeitlimit, z. B. Spotify pausiert
        break;
      }
      push(buf, r);
    }
    close(fd);
    if (ended) {
      // Aufnahme zu Ende: abspielen lassen, was noch im Puffer ist, dann aufhören
      while (p.ring.fill() > 0 && p.wantedStream(path, sizeof path) == gen) sleepMs(50);
      p.streamEnded(gen);
    }
  }
}

}  // namespace dk
