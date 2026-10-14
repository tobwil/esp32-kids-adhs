// Verbindungen fürs Portal im Simulator: BSD-Sockets ohne TLS (Mac-Portal oder lokales „wrangler dev“).
#include <fcntl.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>

#include "portal_net.h"

struct dk::NetConn {
  int fd;
};

dk::NetConn* dk::netOpen(const Endpoint& ep, int timeoutMs) {
  if (ep.tls) { static bool told = false; if (!told) fprintf(stderr, "Simulator: HTTPS geht nicht, bitte „wrangler dev“ (http) nehmen\n"); told = true; return nullptr; }
  addrinfo hints{}, *res = nullptr;
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  char ps[8];
  snprintf(ps, sizeof ps, "%d", ep.port);
  if (getaddrinfo(ep.host, ps, &hints, &res) != 0 || !res) return nullptr;
  const int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (fd < 0) { freeaddrinfo(res); return nullptr; }
  const int fl = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, fl | O_NONBLOCK);
  const int rc = connect(fd, res->ai_addr, res->ai_addrlen);
  freeaddrinfo(res);
  if (rc != 0) {
    fd_set w;
    FD_ZERO(&w);
    FD_SET(fd, &w);
    timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
    int err = 0;
    socklen_t len = sizeof err;
    if (select(fd + 1, nullptr, &w, nullptr, &tv) != 1 || getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) != 0 || err) { close(fd); return nullptr; }
  }
  fcntl(fd, F_SETFL, fl);
  int one = 1;
  setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
  return new NetConn{fd};
}

int dk::netWrite(NetConn* c, const void* data, int n) {
  const ssize_t w = send(c->fd, data, size_t(n), 0);
  return w < 0 ? -1 : int(w);
}

int dk::netRead(NetConn* c, void* buf, int n, int timeoutMs) {
  fd_set r;
  FD_ZERO(&r);
  FD_SET(c->fd, &r);
  timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
  const int s = select(c->fd + 1, &r, nullptr, nullptr, &tv);
  if (s == 0) return -2;
  if (s < 0) return -1;
  const ssize_t k = recv(c->fd, buf, size_t(n), 0);
  return k < 0 ? -1 : int(k);
}

void dk::netClose(NetConn* c) {
  if (!c) return;
  close(c->fd);
  delete c;
}
