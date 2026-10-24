#include "audio.h"

#include <Arduino.h>
#include <atomic>
#include <driver/i2s.h>
#include <math.h>
#include <string.h>

constexpr i2s_port_t I2S_PORT = I2S_NUM_0;
constexpr uint32_t SAMPLE_RATE = AUDIO_SAMPLE_RATE;
constexpr uint32_t FRAMES_PER_MS = SAMPLE_RATE / 1000;
// 2 ms per I2S write, 3 DMA buffers: ~8 ms between mixer and DAC
constexpr size_t BLOCK_FRAMES = 2 * FRAMES_PER_MS;
constexpr int DMA_BUFFERS = 3;
constexpr float TWO_PI_F = 6.2831853f;
// 32-bit slots: digital volume scaling keeps full resolution even at low levels
// (16-bit at 1 % leaves only a few hundred steps per sine and audible quantization distortion)
typedef int32_t sample_t;

static uint8_t ampEnPin;
static bool mono;
static volatile AudioClip requested = AudioClip::None;
static volatile AudioClip playing = AudioClip::None;
static volatile bool stopRequested = false;
static volatile uint8_t volumeLimit = 15;
static volatile uint8_t volumePct = 10;
static volatile float toneHz = 440;

// ---------------------------------------------------------------------------------------------
// Test sounds

// Sine oscillator with exponential amplitude decay (per-sample multiplier)
struct Partial {
  float phase = 0, inc = 0, amp = 0, decay = 1;
  void set(float freq, float amplitude, float tauSeconds) {
    phase = 0;
    inc = TWO_PI_F * freq / SAMPLE_RATE;
    amp = amplitude;
    decay = expf(-1.0f / (tauSeconds * SAMPLE_RATE));
  }
  float next() {
    float s = amp * sinf(phase);
    phase += inc;
    if (phase > TWO_PI_F) phase -= TWO_PI_F;
    amp *= decay;
    return s;
  }
};

// Synthesizes one clip sample by sample; returns false when the clip is over
class ClipSynth {
 public:
  explicit ClipSynth(AudioClip clip) : clip_(clip) {
    switch (clip) {
      case AudioClip::Tone:  length_ = UINT32_MAX; break;  // until stopped
      case AudioClip::Sweep: length_ = 4.0f * SAMPLE_RATE; break;
      case AudioClip::Gong:  length_ = 3.0f * SAMPLE_RATE; break;
      default:               length_ = 0;
    }
  }

  bool next(float &out) {
    if (n_ >= length_) return false;
    float t = static_cast<float>(n_) / SAMPLE_RATE;
    switch (clip_) {
      case AudioClip::Tone:
        // phase accumulator: frequency changes keep the phase continuous, so no clicks
        out = sinf(osc_) * fade(0.005f, 0.02f);
        osc_ += TWO_PI_F * toneHz / SAMPLE_RATE;
        break;
      case AudioClip::Sweep: {
        // logarithmic 100 Hz .. 8 kHz
        float freq = 100.0f * powf(80.0f, t / 4.0f);
        out = sinf(osc_) * fade(0.01f, 0.05f);
        osc_ += TWO_PI_F * freq / SAMPLE_RATE;
        break;
      }
      case AudioClip::Gong:
        // two-tone door chime (E5, then C5), bell timbre from inharmonic overtones
        if (n_ == 0) strike(0, 659.25f);
        if (n_ == static_cast<uint32_t>(0.65f * SAMPLE_RATE)) strike(3, 523.25f);
        out = 0;
        for (Partial &p : partials_) out += p.next();
        out *= 0.55f * fade(0.002f, 0.1f);
        break;
      default:
        return false;
    }
    if (osc_ > TWO_PI_F) osc_ -= TWO_PI_F;
    n_++;
    return true;
  }

 private:
  // linear fade-in/out (seconds) against clicks
  float fade(float in, float out) const {
    float t = static_cast<float>(n_) / SAMPLE_RATE;
    float rest = static_cast<float>(length_ - n_) / SAMPLE_RATE;
    return min(1.0f, min(t / in, rest / out));
  }

  void strike(uint8_t first, float freq) {
    partials_[first].set(freq, 0.6f, 0.9f);
    partials_[first + 1].set(freq * 2.76f, 0.25f, 0.35f);
    partials_[first + 2].set(freq * 5.40f, 0.12f, 0.15f);
  }

  AudioClip clip_;
  uint32_t length_ = 0;
  uint32_t n_ = 0;
  float osc_ = 0;
  Partial partials_[6];
};

// ---------------------------------------------------------------------------------------------
// Live stream: single-producer/single-consumer ring of stereo frames plus playout control

constexpr uint32_t RING_FRAMES = 8192;  // power of two, ~170 ms
constexpr uint32_t STREAM_TIMEOUT_MS = 500;
constexpr float MIN_TARGET_MS = 8;
constexpr float MAX_TARGET_MS = 120;
constexpr float DRIFT_BAND_MS = 2;   // tolerated deviation before frames are dropped/repeated
constexpr float EXCESS_MS = 12;      // average above target + this: jump straight back to the target
constexpr uint32_t ADJUST_EVERY_MS = 20;

static int16_t ring[RING_FRAMES * 2];
static std::atomic<uint32_t> ringWr{0};  // free-running frame counters; producer owns wr,
static std::atomic<uint32_t> ringRd{0};  // consumer owns rd
static std::atomic<uint32_t> restartAt{0};
static std::atomic<bool> restartPending{false};
static std::atomic<uint32_t> lastWriteMs{0};
static std::atomic<bool> everWritten{false};
static volatile float jitterMs = 0;
static volatile uint32_t underruns = 0;
static volatile uint32_t overflows = 0;

// consumer-side state, only touched by the audio task
static bool streamActive = false;
static bool buffering = true;
static float fillAvg = 0;
static float underrunHeadroomMs = 0;  // grows on underruns, given back slowly
static uint32_t headroomTimer = 0;
static uint32_t lastAdjustMs = 0;
static volatile float statBufferMs = 0;
static volatile float statTargetMs = 0;

void audioStreamRestart() {
  restartAt.store(ringWr.load(std::memory_order_relaxed));
  restartPending.store(true, std::memory_order_release);
}

static bool ringReserve(size_t count, uint32_t &wr) {
  wr = ringWr.load(std::memory_order_relaxed);
  if (wr - ringRd.load(std::memory_order_acquire) + count > RING_FRAMES) {
    overflows++;
    return false;
  }
  return true;
}

static void ringCommit(uint32_t wr, size_t count) {
  ringWr.store(wr + count, std::memory_order_release);
  lastWriteMs.store(millis(), std::memory_order_relaxed);
  everWritten.store(true, std::memory_order_relaxed);
}

void audioStreamWrite(const int16_t *frames, size_t count) {
  uint32_t wr;
  if (!ringReserve(count, wr)) return;
  for (size_t i = 0; i < count; i++) {
    uint32_t idx = (wr + i) & (RING_FRAMES - 1);
    ring[idx * 2] = frames[i * 2];
    ring[idx * 2 + 1] = frames[i * 2 + 1];
  }
  ringCommit(wr, count);
}

void audioStreamWriteSilence(size_t count) {
  uint32_t wr;
  if (!ringReserve(count, wr)) return;
  for (size_t i = 0; i < count; i++) {
    uint32_t idx = (wr + i) & (RING_FRAMES - 1);
    ring[idx * 2] = ring[idx * 2 + 1] = 0;
  }
  ringCommit(wr, count);
}

void audioStreamSetJitter(float ms) { jitterMs = ms; }

static void ringCopy(int16_t *dst, uint32_t rd, size_t count) {
  for (size_t i = 0; i < count; i++) {
    uint32_t idx = (rd + i) & (RING_FRAMES - 1);
    dst[i * 2] = ring[idx * 2];
    dst[i * 2 + 1] = ring[idx * 2 + 1];
  }
}

static float streamTargetMs() {
  return min(MAX_TARGET_MS, max(MIN_TARGET_MS, jitterMs * 3.0f + 4.0f) + underrunHeadroomMs);
}

// Fills dst with the next `frames` stream frames. Returns false if no stream is active.
static bool streamRead(int16_t *dst, size_t frames) {
  uint32_t now = millis();
  if (restartPending.exchange(false, std::memory_order_acquire)) {
    ringRd.store(restartAt.load(), std::memory_order_release);
    buffering = true;
    underrunHeadroomMs = 0;
  }

  bool active = everWritten.load(std::memory_order_relaxed) &&
                now - lastWriteMs.load(std::memory_order_relaxed) < STREAM_TIMEOUT_MS;
  if (!active) {
    if (streamActive) {
      streamActive = false;
      ringRd.store(ringWr.load(std::memory_order_acquire), std::memory_order_release);
      buffering = true;
    }
    memset(dst, 0, frames * 2 * sizeof(int16_t));
    return false;
  }
  streamActive = true;

  if (underrunHeadroomMs > 0 && now - headroomTimer > 10000) {
    underrunHeadroomMs = max(0.0f, underrunHeadroomMs - 1.0f);
    headroomTimer = now;
  }

  uint32_t rd = ringRd.load(std::memory_order_relaxed);
  uint32_t fill = ringWr.load(std::memory_order_acquire) - rd;
  float targetMs = streamTargetMs();
  uint32_t target = static_cast<uint32_t>(targetMs * FRAMES_PER_MS);
  statTargetMs = targetMs;

  if (buffering) {
    if (fill < target) {
      memset(dst, 0, frames * 2 * sizeof(int16_t));
      statBufferMs = static_cast<float>(fill) / FRAMES_PER_MS;
      return true;
    }
    buffering = false;
    fillAvg = fill;
  }

  fillAvg += (static_cast<float>(fill) - fillAvg) * 0.02f;  // ~100 ms time constant at 2 ms blocks
  // far too much queued on average (burst at start, sender catching up): one jump back to the target
  // instead of draining it slowly with the drift correction. Uses the average, so the sawtooth of a
  // bursty sender doesn't trigger it.
  if (fillAvg > target + EXCESS_MS * FRAMES_PER_MS) {
    uint32_t skip = min(static_cast<uint32_t>(fillAvg - target), fill > frames ? fill - frames : 0);
    rd += skip;
    fill -= skip;
    fillAvg -= skip;
  }
  statBufferMs = fillAvg / FRAMES_PER_MS;

  // sender and DAC clocks differ slightly: drop or repeat a single frame now and then
  int adjust = 0;
  if (now - lastAdjustMs >= ADJUST_EVERY_MS) {
    if (fillAvg > target + DRIFT_BAND_MS * FRAMES_PER_MS) adjust = 1;
    else if (fillAvg < target - DRIFT_BAND_MS * FRAMES_PER_MS) adjust = -1;
    if (adjust) lastAdjustMs = now;
  }

  size_t need = frames + adjust;
  if (fill < need) {
    ringCopy(dst, rd, fill);
    memset(dst + fill * 2, 0, (frames - fill) * 2 * sizeof(int16_t));
    ringRd.store(rd + fill, std::memory_order_release);
    buffering = true;
    underruns++;
    underrunHeadroomMs = min(60.0f, underrunHeadroomMs + 5.0f);
    headroomTimer = now;
    return true;
  }

  if (adjust < 0) {
    ringCopy(dst, rd, frames - 1);
    dst[(frames - 1) * 2] = dst[(frames - 2) * 2];
    dst[(frames - 1) * 2 + 1] = dst[(frames - 2) * 2 + 1];
  } else {
    ringCopy(dst, rd, frames);  // adjust > 0: the frame after this block is skipped
  }
  ringRd.store(rd + need, std::memory_order_release);
  return true;
}

AudioStreamStats audioStreamStats() {
  AudioStreamStats s;
  s.active = everWritten.load() && millis() - lastWriteMs.load() < STREAM_TIMEOUT_MS;
  s.bufferMs = statBufferMs;
  s.targetMs = statTargetMs;
  s.outputMs = static_cast<float>(BLOCK_FRAMES * (DMA_BUFFERS + 1)) / FRAMES_PER_MS;
  s.underruns = underruns;
  s.overflows = overflows;
  return s;
}

// ---------------------------------------------------------------------------------------------
// Mixer / output task

static inline sample_t toSample(float x) {
  return static_cast<sample_t>(constrain(x, -2147483648.0f, 2147483520.0f));
}

static void audioTaskFn(void *) {
  constexpr uint32_t RELEASE_SAMPLES = SAMPLE_RATE / 50;  // 20 ms fade-out when a clip is stopped
  static sample_t out[BLOCK_FRAMES * 2];
  static int16_t stream[BLOCK_FRAMES * 2];
  ClipSynth synth(AudioClip::None);
  bool clipOn = false;
  uint32_t release = 0;  // samples left in the fade-out, 0 = not stopping

  for (;;) {
    AudioClip req = requested;
    if (req != AudioClip::None) {
      requested = AudioClip::None;
      synth = ClipSynth(req);
      clipOn = true;
      release = 0;
      stopRequested = false;
      playing = req;
    }
    if (clipOn && stopRequested && !release) release = RELEASE_SAMPLES;

    float v = volumePct / 100.0f;
    float clipGain = v * v * 2147483647.0f * 0.9f;
    float streamGain = v * v * 65536.0f;
    bool haveStream = streamRead(stream, BLOCK_FRAMES);

    for (size_t i = 0; i < BLOCK_FRAMES; i++) {
      float clip = 0;
      if (clipOn) {
        float s;
        if (!synth.next(s)) {
          clipOn = false;
          playing = AudioClip::None;
        } else {
          if (release) {
            s *= static_cast<float>(release) / RELEASE_SAMPLES;
            if (--release == 0) {
              clipOn = false;
              playing = AudioClip::None;
            }
          }
          clip = s * clipGain;
        }
      }
      float l = clip, r = clip;
      if (haveStream) {
        float sl = stream[i * 2] * streamGain, sr = stream[i * 2 + 1] * streamGain;
        if (mono) sl = sr = (sl + sr) * 0.5f;
        l += sl;
        r += sr;
      }
      out[i * 2] = toSample(l);
      out[i * 2 + 1] = toSample(r);
    }
    size_t written;
    i2s_write(I2S_PORT, out, sizeof(out), &written, portMAX_DELAY);  // paces the loop in real time
  }
}

void audioBegin(uint8_t pinBclk, uint8_t pinLrclk, uint8_t pinDin, uint8_t pinAmpEn, bool monoOut) {
  ampEnPin = pinAmpEn;
  mono = monoOut;
  digitalWrite(ampEnPin, LOW);
  pinMode(ampEnPin, OUTPUT);

  i2s_config_t cfg = {};
  cfg.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = SAMPLE_RATE;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.dma_buf_count = DMA_BUFFERS;
  cfg.dma_buf_len = BLOCK_FRAMES;
  cfg.tx_desc_auto_clear = true;  // underrun outputs silence instead of repeating the last buffer
  i2s_driver_install(I2S_PORT, &cfg, 0, nullptr);

  i2s_pin_config_t pins = {};
  pins.mck_io_num = I2S_PIN_NO_CHANGE;
  pins.bck_io_num = pinBclk;
  pins.ws_io_num = pinLrclk;
  pins.data_out_num = pinDin;
  pins.data_in_num = I2S_PIN_NO_CHANGE;
  i2s_set_pin(I2S_PORT, &pins);
  i2s_zero_dma_buffer(I2S_PORT);

  // Amp stays on permanently and gets digital silence between clips. In shutdown its outputs are
  // high-impedance, and the open line to J1 picks up mains hum. Enabled only now, with the I2S
  // clocks already running, so it starts up without a pop.
  delay(10);
  digitalWrite(ampEnPin, HIGH);

  xTaskCreatePinnedToCore(audioTaskFn, "audio", 4096, nullptr, 20, nullptr, 1);
}

void audioPlay(AudioClip clip, uint8_t volume) {
  audioSetVolume(volume);
  stopRequested = true;
  requested = clip;
}

void audioStop() {
  requested = AudioClip::None;
  stopRequested = true;
}

void audioSetVolume(uint8_t volume) {
  uint8_t limit = volumeLimit;
  volumePct = volume < limit ? volume : limit;
}

void audioSetVolumeLimit(uint8_t limit) {
  volumeLimit = constrain(limit, 1, 100);
  if (volumePct > volumeLimit) volumePct = volumeLimit;
}

void audioSetFrequency(float hz) { toneHz = constrain(hz, 20.0f, 20000.0f); }

uint8_t audioVolume() { return volumePct; }

uint8_t audioVolumeLimit() { return volumeLimit; }

float audioFrequency() { return toneHz; }

AudioClip audioPlaying() { return playing; }

const char *audioClipName(AudioClip clip) {
  switch (clip) {
    case AudioClip::Tone:  return "tone";
    case AudioClip::Gong:  return "gong";
    case AudioClip::Sweep: return "sweep";
    default:               return "";
  }
}

AudioClip audioClipFromName(const char *name) {
  if (strncmp(name, "tone", 4) == 0) return AudioClip::Tone;
  if (strncmp(name, "gong", 4) == 0) return AudioClip::Gong;
  if (strncmp(name, "sweep", 5) == 0) return AudioClip::Sweep;
  return AudioClip::None;
}
