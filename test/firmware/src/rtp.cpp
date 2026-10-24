#include "rtp.h"

#include <Arduino.h>
#include <esp_timer.h>
#include <lwip/sockets.h>

#include "audio.h"

constexpr size_t RTP_HEADER = 12;
constexpr int16_t MAX_GAP_FILL = 8;         // lost packets in a row that are bridged with silence
constexpr uint32_t NEW_STREAM_PAUSE_MS = 500;  // a pause this long starts a fresh stream

static uint16_t listenPort;
static volatile uint32_t packets, lost, late;
static volatile uint32_t senderAddr;
static volatile float jitter;  // in timestamp units (samples)
static volatile uint16_t framesPerPacket;

static void rtpTask(void *) {
  int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(listenPort);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  bind(sock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));

  static uint8_t pkt[1500];
  static int16_t frames[(sizeof(pkt) - RTP_HEADER) / 2];
  uint32_t ssrc = 0;
  uint16_t expectedSeq = 0;
  uint32_t lastRxMs = 0;
  uint32_t prevTransit = 0;
  bool haveStream = false;

  for (;;) {
    sockaddr_in from;
    socklen_t fromLen = sizeof(from);
    int n = recvfrom(sock, pkt, sizeof(pkt), 0, reinterpret_cast<sockaddr *>(&from), &fromLen);
    if (n < static_cast<int>(RTP_HEADER) || (pkt[0] >> 6) != 2) continue;

    // header: V/P/X/CC, M/PT, sequence, timestamp, SSRC, CSRC list, optional extension
    bool padding = pkt[0] & 0x20;
    bool extension = pkt[0] & 0x10;
    size_t offset = RTP_HEADER + (pkt[0] & 0x0f) * 4;
    uint16_t seq = pkt[2] << 8 | pkt[3];
    uint32_t ts = static_cast<uint32_t>(pkt[4]) << 24 | pkt[5] << 16 | pkt[6] << 8 | pkt[7];
    uint32_t pktSsrc = static_cast<uint32_t>(pkt[8]) << 24 | pkt[9] << 16 | pkt[10] << 8 | pkt[11];
    if (extension) {
      if (n < static_cast<int>(offset + 4)) continue;
      offset += 4 + (pkt[offset + 2] << 8 | pkt[offset + 3]) * 4;
    }
    int len = n - static_cast<int>(offset);
    if (padding && len > 0) len -= pkt[n - 1];
    size_t count = len > 0 ? len / 4 : 0;  // L16 stereo: 4 bytes per frame
    if (!count) continue;

    uint32_t now = millis();
    if (!haveStream || pktSsrc != ssrc || now - lastRxMs > NEW_STREAM_PAUSE_MS) {
      haveStream = true;
      ssrc = pktSsrc;
      expectedSeq = seq;
      jitter = 0;
      prevTransit = 0;
      audioStreamRestart();
    }
    lastRxMs = now;
    senderAddr = from.sin_addr.s_addr;

    int16_t gap = static_cast<int16_t>(seq - expectedSeq);
    if (gap < 0) {  // already played or bridged
      late++;
      continue;
    }
    if (gap > 0) {
      lost += gap;
      if (gap <= MAX_GAP_FILL) audioStreamWriteSilence(gap * count);
    }
    expectedSeq = seq + 1;

    // RFC 3550 A.8 interarrival jitter, in sample units
    uint32_t arrival = static_cast<uint32_t>(esp_timer_get_time() * (AUDIO_SAMPLE_RATE / 1000) / 1000);
    uint32_t transit = arrival - ts;
    if (prevTransit) {
      int32_t d = static_cast<int32_t>(transit - prevTransit);
      jitter += (abs(d) - jitter) / 16.0f;
      audioStreamSetJitter(jitter * 1000.0f / AUDIO_SAMPLE_RATE);
    }
    prevTransit = transit;

    const uint8_t *payload = pkt + offset;
    for (size_t i = 0; i < count * 2; i++) {
      frames[i] = static_cast<int16_t>(payload[i * 2] << 8 | payload[i * 2 + 1]);  // network byte order
    }
    audioStreamWrite(frames, count);
    framesPerPacket = count;
    packets++;
  }
}

void rtpBegin(uint16_t port) {
  listenPort = port;
  // above the HTTP loop, below the audio output task
  xTaskCreatePinnedToCore(rtpTask, "rtp", 4096, nullptr, 19, nullptr, 1);
}

RtpStats rtpStats() {
  RtpStats s;
  s.sender = IPAddress(senderAddr);
  s.packets = packets;
  s.lost = lost;
  s.late = late;
  s.jitterMs = jitter * 1000.0f / AUDIO_SAMPLE_RATE;
  s.frames = framesPerPacket;
  return s;
}

uint16_t rtpPort() { return listenPort; }
