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
    // Per line: sync pulse, then N color scan segments (standard SSTV:
    // R-Y, B-Y, Y for Martin/Scottie/Robot; "colorComponents" is 3 for
    // colour modes, 1 for mono). Each segment is a full-width FM scan of
    // one colour-difference / luminance component. Pixel value 0 -> 1500
    // Hz, 255 -> 2300 Hz (black..white).
    const int components = info.colorComponents;
    const double syncMs = static_cast<double>(info.syncDurMs);
    const double scanMs =
        static_cast<double>(info.lineTimeMs) - syncMs;  // total per-line scan
    const double pixelMs = scanMs / static_cast<double>(components) / w;
    // Per-pixel sample count in floating point; total per segment is exact
    // (no cumulative drift that would lengthen the frame / shift pixels).
    const double pixelSamplesD = pixelMs * 0.001 * sampleRate;
    const double segSamplesD = pixelSamplesD * w;
    if (segSamplesD < 1.0) {
        return out;
    }
    const double span =
        static_cast<double>(info.whiteFreq) - info.blackFreq;

    const size_t rowStride = static_cast<size_t>(w) * 3;
    for (int y = 0; y < h; ++y) {
        appendTone(out, info.syncFreq, static_cast<float>(syncMs), sampleRate,
                   phase);

        const uint8_t* row = pixels + static_cast<size_t>(y) * rowStride;
        // Each segment must emit exactly segSamples samples so that the
        // decoder's frame-length check (segSamples = round(segSamplesD))
        // stays in sync frame after frame. The floating raster distributes
        // per-pixel counts; the last pixel absorbs any rounding remainder.
        const size_t segSamples = static_cast<size_t>(segSamplesD + 0.5);
        for (int comp = 0; comp < components; ++comp) {
            double posD = 0.0;
            size_t emitted = 0;
            double lastPhaseStep = 0.0;
            for (int x = 0; x < w; ++x) {
                const double nextD = (static_cast<double>(x) + 1.0) * pixelSamplesD;
                const long pixStart = static_cast<long>(posD);
                const long pixEnd = static_cast<long>(nextD);
                size_t nSamp = (pixEnd > pixStart)
                                   ? static_cast<size_t>(pixEnd - pixStart)
                                   : 1;
                posD = nextD;

                const uint8_t* px = row + static_cast<size_t>(x) * 3;
                const double R = px[0], G = px[1], B = px[2];
                const double Y = 0.299 * R + 0.587 * G + 0.114 * B;
                double compVal;  // 0..255
                if (comp == 2) {
                    compVal = Y;                 // luminance
                } else if (comp == 0) {
                    compVal = (R - Y) * 0.5 + 128.0;  // R-Y
                } else {
                    compVal = (B - Y) * 0.5 + 128.0;  // B-Y
                }
                if (compVal < 0.0) compVal = 0.0;
                if (compVal > 255.0) compVal = 255.0;

                const double freq =
                    info.blackFreq + span * (compVal / 255.0);
                lastPhaseStep =
                    2.0 * 3.14159265358979323846 * freq / sampleRate;
                for (size_t i = 0; i < nSamp; ++i) {
                    out.push_back(
                        static_cast<float>(std::sin(phase) * amplitude_));
                    phase += lastPhaseStep;
                    if (phase > 2.0 * 3.14159265358979323846) {
                        phase -= 2.0 * 3.14159265358979323846;
                    }
                }
                emitted += nSamp;
            }
            // Absorb the rounding remainder into the last pixel (same tone,
            // so it does not perturb the picture). lastPhaseStep holds the
            // last pixel's phase increment (kept in scope for the fill).
            for (size_t i = emitted; i < segSamples; ++i) {
                out.push_back(
                    static_cast<float>(std::sin(phase) * amplitude_));
                phase += lastPhaseStep;
                if (phase > 2.0 * 3.14159265358979323846) {
                    phase -= 2.0 * 3.14159265358979323846;
                }
            }
        }
    }

    return out;
}

} // namespace audiot
