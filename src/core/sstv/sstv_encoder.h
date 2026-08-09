// sstv_encoder.h - SSTV image-to-audio encoder
#ifndef AUDIOT_SSTV_ENCODER_H
#define AUDIOT_SSTV_ENCODER_H

#include <vector>
#include <cstdint>
#include "sstv_protocols.h"

namespace audiot {

// Encodes an RGB image into an SSTV audio signal (mono, float [-1,1]).
//
// Signal structure:
//   Leader (1900 Hz, 300 ms)
//   VIS start (1200 Hz, 300 ms)
//   VIS data (7 bits LSB-first + even parity, 30 ms each,
//             bit=1 -> 1100 Hz, bit=0 -> 1300 Hz)
//   VIS stop (1200 Hz, 30 ms)
//   Per line: sync pulse (1200 Hz) + pixel scan (1500..2300 Hz FM)
class SSTVEncoder {
public:
    SSTVEncoder();

    // Set SSTV mode (default: Martin M1)
    void setMode(SSTVMode mode);
    SSTVMode mode() const;

    // Output signal amplitude (0..1), default 0.8
    void setAmplitude(float amp);

    // Encode an RGB image buffer (row-major, 3 bytes per pixel)
    // into a mono audio signal at 'sampleRate' Hz.
    // Returns an empty vector if pixels/w/h are invalid.
    std::vector<float> encode(const uint8_t* pixels, int w, int h,
                              int sampleRate) const;

    // Total expected duration of an encoded frame in seconds
    // (header + all scan lines) for the given mode.
    static double frameDuration(SSTVMode mode);

private:
    // Append a constant-frequency tone with phase continuity
    void appendTone(std::vector<float>& out, float freqHz, float durationMs,
                    int sampleRate, double& phase) const;

    // Append the VIS header for the current mode
    void appendVIS(std::vector<float>& out, int sampleRate, double& phase) const;

    SSTVMode mode_;
    float amplitude_;
};

} // namespace audiot
#endif // AUDIOT_SSTV_ENCODER_H
