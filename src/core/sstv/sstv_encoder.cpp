// sstv_encoder.cpp - SSTV image-to-audio encoder implementation
#include "sstv_encoder.h"
#include "sstv_timing.h"

#include <cmath>

namespace audiot {

SSTVEncoder::SSTVEncoder()
    : mode_(SSTVMode::MartinM1)
    , amplitude_(0.8f)
{
}

void SSTVEncoder::setMode(SSTVMode mode) { mode_ = mode; }
SSTVMode SSTVEncoder::mode() const { return mode_; }

void SSTVEncoder::setAmplitude(float amp) {
    if (amp < 0.0f) amp = 0.0f;
    if (amp > 1.0f) amp = 1.0f;
    amplitude_ = amp;
}

double SSTVEncoder::frameDuration(SSTVMode mode) {
    const SSTVProtocolInfo& info = getProtocolInfo(mode);
    double headerMs = sstv::LEADER_TIME_MS + sstv::BREAK_TIME_MS +
                      sstv::VIS_START_TIME_MS +
                      (sstv::VIS_DATA_BITS + 1) * sstv::VIS_BIT_TIME_MS +
                      sstv::VIS_STOP_TIME_MS;
    double imageMs = static_cast<double>(info.lineTimeMs) * info.height;
    return (headerMs + imageMs) / 1000.0;
}

void SSTVEncoder::appendTone(std::vector<float>& out, float freqHz,
                             float durationMs, int sampleRate,
                             double& phase) const {
    const size_t count =
        static_cast<size_t>(durationMs * 0.001 * sampleRate + 0.5);
    const double phaseStep = 2.0 * 3.14159265358979323846 * freqHz / sampleRate;
    out.reserve(out.size() + count);
    for (size_t i = 0; i < count; ++i) {
        out.push_back(static_cast<float>(std::sin(phase) * amplitude_));
        phase += phaseStep;
        if (phase > 2.0 * 3.14159265358979323846) {
            phase -= 2.0 * 3.14159265358979323846;
        }
    }
}

void SSTVEncoder::appendVIS(std::vector<float>& out, int sampleRate,
                            double& phase) const {
    const SSTVProtocolInfo& info = getProtocolInfo(mode_);

    // VIS start bit: 1200 Hz, 300 ms
    appendTone(out, sstv::VIS_START_FREQ_HZ, sstv::VIS_START_TIME_MS,
               sampleRate, phase);

    // 7 data bits, LSB first; bit=1 -> 1100 Hz, bit=0 -> 1300 Hz
    int ones = 0;
    for (int i = 0; i < sstv::VIS_DATA_BITS; ++i) {
        const bool bit = ((info.visCode >> i) & 0x01) != 0;
        if (bit) ++ones;
        appendTone(out, bit ? sstv::VIS_BIT_ONE_HZ : sstv::VIS_BIT_ZERO_HZ,
                   sstv::VIS_BIT_TIME_MS, sampleRate, phase);
    }

    // Even parity bit
    const bool parity = (ones & 1) != 0;
    appendTone(out, parity ? sstv::VIS_BIT_ONE_HZ : sstv::VIS_BIT_ZERO_HZ,
               sstv::VIS_BIT_TIME_MS, sampleRate, phase);

    // VIS stop bit: 1200 Hz, 30 ms
    appendTone(out, sstv::VIS_STOP_FREQ_HZ, sstv::VIS_STOP_TIME_MS,
               sampleRate, phase);
}

std::vector<float> SSTVEncoder::encode(const uint8_t* pixels, int w, int h,
                                       int sampleRate) const {
    std::vector<float> out;
    if (pixels == nullptr || w <= 0 || h <= 0 || sampleRate <= 0) {
        return out;
    }

    const SSTVProtocolInfo& info = getProtocolInfo(mode_);
    double phase = 0.0;

    // ---- Header ----
    appendTone(out, sstv::LEADER_FREQ_HZ, sstv::LEADER_TIME_MS, sampleRate,
               phase);
    appendTone(out, sstv::BREAK_FREQ_HZ, sstv::BREAK_TIME_MS, sampleRate,
               phase);
    appendVIS(out, sampleRate, phase);

    // ---- Scan lines ----
    // Per line: sync pulse, then one full-width FM scan of the row.
    // Scan frequency maps pixel value 0 -> 1500 Hz, 255 -> 2300 Hz.
    const double syncMs = static_cast<double>(info.syncDurMs);
    const double scanMs =
        static_cast<double>(info.lineTimeMs) - syncMs;

    for (int y = 0; y < h; ++y) {
        appendTone(out, info.syncFreq, static_cast<float>(syncMs), sampleRate,
                   phase);

        // Per-pixel duration within the scan interval
        const double pixelMs = scanMs / w;
        const size_t pixelSamples =
            static_cast<size_t>(pixelMs * 0.001 * sampleRate + 0.5);
        const double span =
            static_cast<double>(info.whiteFreq) - info.blackFreq;

        for (int x = 0; x < w; ++x) {
            const uint8_t* px = pixels + (static_cast<size_t>(y) * w + x) * 3;
            // Luminance from RGB (Rec.601-ish weights, integer math)
            const int lum = (77 * px[0] + 150 * px[1] + 29 * px[2]) >> 8;
            const double freq =
                info.blackFreq + span * (static_cast<double>(lum) / 255.0);
            const double phaseStep =
                2.0 * 3.14159265358979323846 * freq / sampleRate;
            for (size_t i = 0; i < pixelSamples; ++i) {
                out.push_back(
                    static_cast<float>(std::sin(phase) * amplitude_));
                phase += phaseStep;
                if (phase > 2.0 * 3.14159265358979323846) {
                    phase -= 2.0 * 3.14159265358979323846;
                }
            }
        }
    }

    return out;
}

} // namespace audiot
