// audio_format.h - Audio format definition for PCM audio
#ifndef AUDIOT_AUDIO_FORMAT_H
#define AUDIOT_AUDIO_FORMAT_H

#include <cstdint>

namespace audiot {

struct AudioFormat {
    int sampleRate;      // Sample rate (e.g. 44100, 48000)
    int channels;        // Channel count (1=mono, 2=stereo)
    int bitsPerSample;   // Bits per sample (16, 24, 32)

    AudioFormat() : sampleRate(44100), channels(1), bitsPerSample(16) {}
    AudioFormat(int sr, int ch, int bps)
        : sampleRate(sr), channels(ch), bitsPerSample(bps) {}

    int bytesPerSample() const { return bitsPerSample / 8; }
    int bytesPerFrame() const { return channels * bytesPerSample(); }
};

} // namespace audiot
#endif // AUDIOT_AUDIO_FORMAT_H
