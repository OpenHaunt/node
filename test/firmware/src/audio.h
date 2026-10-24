// Audio output: test sounds synthesized on the fly plus a live stream input (fed by the RTP receiver),
// mixed and written to I2S by one real-time task. The amp is always enabled (SD_MODE high) and gets
// digital silence when idle, so its outputs never float.
#pragma once

#include <stddef.h>
#include <stdint.h>

constexpr uint32_t AUDIO_SAMPLE_RATE = 48000;

enum class AudioClip : uint8_t { None, Tone, Gong, Sweep };

// monoOut: sum L+R onto both slots (MAX98357A plays only one slot); false for a stereo DAC
void audioBegin(uint8_t pinBclk, uint8_t pinLrclk, uint8_t pinDin, uint8_t pinAmpEn, bool monoOut);

// Starts a clip (stops the current one). volume 0..100, perceptual (squared) scale.
// The test tone plays until audioStop(); the other clips end by themselves.
void audioPlay(AudioClip clip, uint8_t volume);
void audioStop();
void audioSetVolume(uint8_t volume);       // clamped to the limit, takes effect immediately
void audioSetVolumeLimit(uint8_t limit);   // upper bound for every source, 1..100
void audioSetFrequency(float hz);          // test tone frequency, 20..20000 Hz, glides without clicks

uint8_t audioVolume();
uint8_t audioVolumeLimit();
float audioFrequency();
AudioClip audioPlaying();
const char *audioClipName(AudioClip clip);  // "tone" / "gong" / "sweep" / ""
AudioClip audioClipFromName(const char *name);

// Live stream input: interleaved stereo int16 frames at AUDIO_SAMPLE_RATE, host byte order.
// Single producer (the receiver task); playback starts once enough audio is buffered.
void audioStreamRestart();                         // new stream: drop buffered audio, buffer up again
void audioStreamWrite(const int16_t *frames, size_t count);
void audioStreamWriteSilence(size_t count);        // fills the gap of lost packets
void audioStreamSetJitter(float ms);               // network jitter estimate, sizes the buffer

struct AudioStreamStats {
  bool active;
  float bufferMs;   // average audio waiting in the jitter buffer
  float targetMs;   // what the buffer is steered to
  float outputMs;   // mixer block + I2S DMA queue after the buffer
  uint32_t underruns;
  uint32_t overflows;
};
AudioStreamStats audioStreamStats();
