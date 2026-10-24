#include "sse.h"

#include <Arduino.h>
#include <errno.h>
#include <lwip/sockets.h>

constexpr uint32_t SEND_TIMEOUT_MS = 50;  // a client whose socket stays full this long is dropped

static WiFiClient clients[SSE_MAX_CLIENTS];
static uint32_t connectedAt[SSE_MAX_CLIENTS];

static bool active(int i) { return clients[i].fd() >= 0; }

static void drop(int i) {
  clients[i].stop();
  clients[i] = WiFiClient();
}

// Writes the whole buffer without blocking on a stuck peer
static bool sendAll(int i, const char *buf, size_t len) {
  int fd = clients[i].fd();
  uint32_t start = millis();
  size_t off = 0;
  while (off < len) {
    int n = send(fd, buf + off, len - off, MSG_DONTWAIT);
    if (n > 0) {
      off += n;
    } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) && millis() - start < SEND_TIMEOUT_MS) {
      delay(1);
    } else {
      return false;  // closed, reset or too slow; a partly sent event would corrupt the stream anyway
    }
  }
  return true;
}

void sseAdd(WiFiClient &client) {
  int slot = -1;
  for (int i = 0; i < SSE_MAX_CLIENTS && slot < 0; i++) {
    if (!active(i)) slot = i;
  }
  if (slot < 0) {
    slot = 0;
    for (int i = 1; i < SSE_MAX_CLIENTS; i++) {
      if (connectedAt[i] < connectedAt[slot]) slot = i;
    }
    drop(slot);
  }
  clients[slot] = client;
  connectedAt[slot] = millis();
  clients[slot].setNoDelay(true);  // events are small and latency matters

  static const char header[] =
      "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nCache-Control: no-store\r\n"
      "Connection: keep-alive\r\nAccess-Control-Allow-Origin: *\r\n\r\n"
      "retry: 2000\n\n";  // browser reconnect delay
  if (!sendAll(slot, header, sizeof(header) - 1)) drop(slot);
}

void sseBroadcast(const char *data) {
  static char event[3200];  // one buffer, so each event leaves as a single TCP segment
  int len = snprintf(event, sizeof(event), "data: %s\n\n", data);
  if (len <= 0 || len >= static_cast<int>(sizeof(event))) return;
  for (int i = 0; i < SSE_MAX_CLIENTS; i++) {
    if (active(i) && !sendAll(i, event, len)) drop(i);
  }
}

void sseCloseAll() {
  for (int i = 0; i < SSE_MAX_CLIENTS; i++) {
    if (active(i)) drop(i);
  }
}

int sseClients() {
  int n = 0;
  for (int i = 0; i < SSE_MAX_CLIENTS; i++) n += active(i);
  return n;
}
