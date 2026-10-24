// RTP receiver (RFC 3550) for live audio: UDP on all interfaces, payload L16 (RFC 3551),
// 48 kHz, 2 channels, big-endian. Feeds the audio module's stream input from its own task.
#pragma once

#include <IPAddress.h>
#include <stdint.h>

constexpr uint16_t RTP_DEFAULT_PORT = 5004;

void rtpBegin(uint16_t port);

struct RtpStats {
  IPAddress sender;     // last sender, 0.0.0.0 before the first packet
  uint32_t packets;
  uint32_t lost;        // sequence gaps, filled with silence
  uint32_t late;        // out-of-order or duplicate, dropped
  float jitterMs;       // RFC 3550 interarrival jitter
  uint16_t frames;      // audio frames per packet of the current stream
};
RtpStats rtpStats();
uint16_t rtpPort();
