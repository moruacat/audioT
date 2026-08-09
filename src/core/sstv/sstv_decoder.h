// sstv_decoder.h - SSTV audio-to-image decoder
#ifndef AUDIOT_SSTV_DECODER_H
#define AUDIOT_SSTV_DECODER_H

#include <vector>
#include <cstdint>
#include <cstddef>
#include "sstv_protocols.h"

namespace audiot {

// Decodes an SSTV audio signal (mono float) back into an image.
//
// Pipeline:
//   detect()  - locate leader/VIS header, identify the SSTV mode
//   decode()  - run detect() then demodulate scan lines into RGB pixels
class SSTVDecoder {
public:
    // Header detection result
    struct DetectionResult {
        bool found;          // Valid SSTV header located
        SSTVMode mode;       // Mode decoded from VIS code
        uint8_t visCode;     // Raw VIS code (7 bits)
        size_t leaderStart;  // Sample index of leader tone start
        size_t imageStart;   // Sample index of first scan line
        DetectionResult()
            : found(false), mode(SSTVMode::MartinM1), visCode(0),
              leaderStart(0), imageStart(0) {}
    };

    // Image decode result
    struct DecodeResult {
        bool success;
        SSTVMode mode;
        int width;
        int height;
        std::vector<uint8_t> pixels;  // RGB, row-major, 3 bytes per pixel
        DecodeResult() : success(false), mode(SSTVMode::MartinM1),
                         width(0), height(0) {}
    };

    SSTVDecoder();

    // Detect SSTV header in the signal and identify the mode.
    // sampleRate: audio sample rate in Hz.
    DetectionResult detect(const std::vector<float>& samples,
                           int sampleRate) const;

    // Full decode: detect header and demodulate the image.
    DecodeResult decode(const std::vector<float>& samples,
                        int sampleRate) const;

private:
    // Estimate tone frequency (Hz) of samples[start..start+count)
    // using interpolated zero-crossing counting. Returns 0 on failure.
    static float estimateFrequency(const std::vector<float>& samples,
                                   size_t start, size_t count, int sampleRate);

    // Check whether freq is within relative tolerance of target
    static bool freqMatches(float freq, float target);

    // Read one VIS bit (30 ms) at position; returns true if bit == 1
    bool readVISBit(const std::vector<float>& samples, size_t start,
                    size_t count, int sampleRate) const;
};

} // namespace audiot
#endif // AUDIOT_SSTV_DECODER_H
