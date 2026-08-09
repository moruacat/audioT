// sstv_decoder.cpp - SSTV audio-to-image decoder implementation
#include "sstv_decoder.h"
#include "sstv_timing.h"

#include <algorithm>
#include <cmath>

namespace audiot {

static const double kPi = 3.14159265358979323846;

SSTVDecoder::SSTVDecoder() {}

bool SSTVDecoder::freqMatches(float freq, float target) {
    if (freq <= 0.0f) return false;
    const float diff = freq - target;
    const float rel = (diff < 0 ? -diff : diff) / target;
    return rel <= sstv::FREQ_TOLERANCE;
}

float SSTVDecoder::estimateFrequency(const std::vector<float>& samples,
                                     size_t start, size_t count,
                                     int sampleRate) {
    if (start + count > samples.size() || count < 4 || sampleRate <= 0) {
        return 0.0f;
    }

    // Count zero crossings with linear interpolation for sub-sample accuracy
    double crossings = 0.0;
    for (size_t i = start + 1; i < start + count; ++i) {
        const float a = samples[i - 1];
        const float b = samples[i];
        if ((a < 0.0f && b >= 0.0f) || (a > 0.0f && b <= 0.0f)) {
            // Fractional crossing position within [i-1, i)
            const float denom = b - a;
            double frac = 0.5;
            if (denom < -1e-9f || denom > 1e-9f) {
                frac = static_cast<double>(-a) / static_cast<double>(denom);
                if (frac < 0.0) frac = 0.0;
                if (frac > 1.0) frac = 1.0;
            }
            crossings += 1.0;
            (void)frac; // crossing at sample boundary is accurate enough
                        // for phase-continuous tones used by the encoder
        }
    }

    if (crossings < 1.0) return 0.0f;
    // Each full cycle crosses zero twice
    const double cycles = crossings / 2.0;
    const double durationSec = static_cast<double>(count) / sampleRate;
    return static_cast<float>(cycles / durationSec);
}

bool SSTVDecoder::readVISBit(const std::vector<float>& samples, size_t start,
                             size_t count, int sampleRate) const {
    const float freq = estimateFrequency(samples, start, count, sampleRate);
    // bit=1 -> 1100 Hz, bit=0 -> 1300 Hz; threshold at 1200 Hz
    return freq > 0.0f && freq < 1200.0f;
}

SSTVDecoder::DetectionResult SSTVDecoder::detect(
    const std::vector<float>& samples, int sampleRate) const {
    DetectionResult result;
    if (samples.empty() || sampleRate <= 0) return result;

    const size_t winSamples = static_cast<size_t>(0.010 * sampleRate); // 10 ms
    const size_t stepSamples = winSamples / 2;                          // 5 ms
    if (samples.size() < winSamples * 4) return result;

    // ---- 1. Find leader tone (1900 Hz) ----
    size_t leaderStart = 0;
    bool foundLeader = false;
    for (size_t pos = 0; pos + winSamples <= samples.size();
         pos += stepSamples) {
        const float f = estimateFrequency(samples, pos, winSamples, sampleRate);
        if (freqMatches(f, sstv::LEADER_FREQ_HZ)) {
            leaderStart = pos;
            foundLeader = true;
            break;
        }
    }
    if (!foundLeader) return result;

    // ---- 2. Walk past the leader tone (coarse) ----
    size_t pos = leaderStart;
    const size_t maxLeaderEnd =
        std::min(samples.size(), leaderStart + static_cast<size_t>(2.0 * sampleRate));
    while (pos + winSamples <= maxLeaderEnd) {
        const float f = estimateFrequency(samples, pos, winSamples, sampleRate);
        if (!freqMatches(f, sstv::LEADER_FREQ_HZ)) break;
        pos += stepSamples;
    }
    // Leader must last at least MIN_LEADER_MS
    const double leaderMs =
        static_cast<double>(pos - leaderStart) * 1000.0 / sampleRate;
    if (leaderMs < sstv::MIN_LEADER_MS) return result;

    // ---- 2b. Refine the leader start position ----
    // The pixel grid must align within half a pixel (a few samples), so a
    // window-frequency scan is not sharp enough. Instead:
    //   a) locate the tone onset via a short-window energy threshold
    //   b) resolve the sub-window offset from the baseband phase, which is
    //      known modulo one 1900 Hz period (23.2 samples at 44.1 kHz)
    const size_t eWin = winSamples; // 10 ms energy window
    const size_t regionLo = (leaderStart > eWin + stepSamples * 2)
                                ? leaderStart - eWin - stepSamples * 2
                                : 0;
    size_t regionHi = leaderStart + stepSamples * 2;
    if (regionHi + eWin > samples.size()) {
        regionHi = (samples.size() > eWin) ? samples.size() - eWin : 0;
    }

    // Find the last window position whose energy is still "silence"-like
    double eMax = 0.0;
    for (size_t i = regionLo; i + eWin <= samples.size() && i <= regionHi + eWin;
         ++i) {
        double e = 0.0;
        for (size_t n = i; n < i + eWin; ++n) {
            e += static_cast<double>(samples[n]) * samples[n];
        }
        if (e > eMax) eMax = e;
    }
    if (eMax <= 0.0) return result;

    size_t lastSilent = 0;
    bool haveSilent = false;
    for (size_t i = regionLo; i <= regionHi; ++i) {
        if (i + eWin > samples.size()) break;
        double e = 0.0;
        for (size_t n = i; n < i + eWin; ++n) {
            e += static_cast<double>(samples[n]) * samples[n];
        }
        if (e < 0.25 * eMax) {
            lastSilent = i;
            haveSilent = true;
        }
    }

    size_t leaderStartFine = leaderStart;
    if (haveSilent) {
        // Energy ramps linearly across the onset; the tone starts ~3/4 of a
        // window after the last fully-silent window position.
        leaderStartFine = lastSilent + 1 + (eWin * 3) / 4;
    }

    // Phase refinement: mix a window well inside the leader against a
    // 1900 Hz reference anchored at leaderStartFine. The residual phase
    // gives the onset offset modulo one tone period.
    const double w0 = 2.0 * kPi * sstv::LEADER_FREQ_HZ / sampleRate;
    const size_t phaseOffset = static_cast<size_t>(0.120 * sampleRate);
    const size_t phaseWin = static_cast<size_t>(0.020 * sampleRate);
    if (leaderStartFine + phaseOffset + phaseWin < samples.size() &&
        haveSilent) {
        double sumI = 0.0, sumQ = 0.0;
        const size_t base = leaderStartFine + phaseOffset;
        for (size_t n = 0; n < phaseWin; ++n) {
            const double ref = w0 * static_cast<double>(base + n -
                                                         leaderStartFine);
            sumI += samples[base + n] * std::cos(ref);
            sumQ += samples[base + n] * std::sin(ref);
        }
        // baseband phase = w0 * (leaderStartFine - trueStart), mod 2*pi
        const double delta = std::atan2(sumI, sumQ);
        const double period = static_cast<double>(sampleRate) /
                              sstv::LEADER_FREQ_HZ;
        double shiftSamples = delta / w0;
        // Wrap into [-period/2, period/2)
        while (shiftSamples > period / 2.0) shiftSamples -= period;
        while (shiftSamples < -period / 2.0) shiftSamples += period;
        const double fine =
            static_cast<double>(leaderStartFine) - shiftSamples;
        if (fine >= 0.0) {
            leaderStartFine = static_cast<size_t>(fine + 0.5);
        }
    }

    // ---- 3. Frame boundaries from nominal durations ----
    // Same rounding as the encoder so sample grids align exactly.
    const size_t leaderLen = static_cast<size_t>(
        sstv::LEADER_TIME_MS * 0.001 * sampleRate + 0.5);
    const size_t breakLen = static_cast<size_t>(
        sstv::BREAK_TIME_MS * 0.001 * sampleRate + 0.5);
    const size_t visStartLen = static_cast<size_t>(
        sstv::VIS_START_TIME_MS * 0.001 * sampleRate + 0.5);
    const size_t bitSamples = static_cast<size_t>(
        sstv::VIS_BIT_TIME_MS * 0.001 * sampleRate + 0.5);

    const size_t leaderEnd = leaderStartFine + leaderLen;
    size_t bitPos = leaderEnd + breakLen + visStartLen;
    const size_t totalBits =
        sstv::VIS_DATA_BITS + 1; // data bits + parity
    if (bitPos + (totalBits + 1) * bitSamples > samples.size()) {
        return result;
    }

    // Sanity-check the 1200 Hz preamble
    const float preFreq = estimateFrequency(samples, leaderEnd + breakLen,
                                            visStartLen / 2, sampleRate);
    if (!freqMatches(preFreq, sstv::VIS_START_FREQ_HZ)) return result;

    // ---- 4. Read 7 VIS data bits + parity (LSB first) ----
    uint8_t visCode = 0;
    int ones = 0;
    for (int i = 0; i < sstv::VIS_DATA_BITS; ++i) {
        if (readVISBit(samples, bitPos, bitSamples, sampleRate)) {
            visCode = static_cast<uint8_t>(visCode | (1 << i));
            ++ones;
        }
        bitPos += bitSamples;
    }
    // Parity bit (even parity)
    if (readVISBit(samples, bitPos, bitSamples, sampleRate)) ++ones;
    bitPos += bitSamples;
    if ((ones & 1) != 0) return result; // parity error

    // ---- 5. VIS stop bit (1200 Hz, 30 ms) ----
    const float stopFreq = estimateFrequency(samples, bitPos, bitSamples,
                                             sampleRate);
    if (!freqMatches(stopFreq, sstv::VIS_STOP_FREQ_HZ)) return result;
    bitPos += bitSamples;

    result.found = true;
    result.visCode = visCode;
    result.mode = modeFromVISCode(visCode);
    result.leaderStart = leaderStartFine;
    result.imageStart = bitPos;
    return result;
}

// Demodulate one scan line (FM 1500..2300 Hz) into per-pixel frequencies
// using quadrature down-conversion + cascaded moving-average filters.
// Returns one frequency estimate per pixel in outFreq.
static bool demodScanLine(const std::vector<float>& samples, size_t scanStart,
                          size_t scanLen, int width, size_t pixelSamples,
                          int sampleRate, float blackFreq, float whiteFreq,
                          std::vector<float>& outFreq) {
    outFreq.assign(static_cast<size_t>(width), blackFreq);
    if (scanStart + scanLen > samples.size() || scanLen < 4 ||
        pixelSamples == 0) {
        return false;
    }

    // Filter length per stage: ~3 pixels for strong 2*LO ripple rejection.
    size_t W = pixelSamples * 3;
    if (W > scanLen) W = scanLen;
    if (W < 4) W = 4;

    // Build padded segment: repeat the first/last pixel's tone samples so
    // the filter stays centered and never sees the 1200 Hz sync pulse.
    // (Time reversal would mirror the frequency, so use repetition.)
    const size_t padBlock = std::min(pixelSamples, scanLen);
    std::vector<float> seg;
    seg.reserve(scanLen + 2 * W);
    for (size_t i = 0; i < W; ++i) {
        seg.push_back(samples[scanStart + (i % padBlock)]);
    }
    for (size_t i = 0; i < scanLen; ++i) {
        seg.push_back(samples[scanStart + i]);
    }
    for (size_t i = 0; i < W; ++i) {
        seg.push_back(samples[scanStart + scanLen - padBlock + (i % padBlock)]);
    }

    const double fc = (static_cast<double>(blackFreq) + whiteFreq) / 2.0;
    const double w0 = 2.0 * kPi * fc / sampleRate;
    const size_t M = seg.size();

    // Quadrature mix
    std::vector<float> mixI(M), mixQ(M);
    for (size_t n = 0; n < M; ++n) {
        const double ph = w0 * static_cast<double>(n);
        mixI[n] = seg[n] * static_cast<float>(std::cos(ph));
        mixQ[n] = seg[n] * static_cast<float>(std::sin(ph));
    }

    // Cascaded moving average (four stages) on I and Q.
    // More stages give much stronger rejection of the 2*LO (~3800 Hz)
    // mixing product, which would otherwise alias into the per-pixel
    // frequency estimates.
    const int numStages = 4;
    std::vector<float> filtI(M, 0.0f), filtQ(M, 0.0f);
    for (int stage = 0; stage < numStages; ++stage) {
        const std::vector<float>& src = (stage == 0) ? mixI : filtI;
        const std::vector<float>& srcQ = (stage == 0) ? mixQ : filtQ;
        std::vector<float>& dstI = filtI;
        std::vector<float>& dstQ = filtQ;
        double sumI = 0.0, sumQ = 0.0;
        std::vector<float> tmpI(M), tmpQ(M);
        for (size_t n = 0; n < M; ++n) {
            sumI += src[n];
            sumQ += srcQ[n];
            if (n >= W) {
                sumI -= src[n - W];
                sumQ -= srcQ[n - W];
            }
            const size_t cnt = std::min(n + 1, W);
            tmpI[n] = static_cast<float>(sumI / cnt);
            tmpQ[n] = static_cast<float>(sumQ / cnt);
        }
        dstI.swap(tmpI);
        dstQ.swap(tmpQ);
    }

    // Instantaneous frequency from unwrapped phase derivative.
    // With I ~ sin(2*pi*(f-fc)*t) and Q ~ cos(2*pi*(f-fc)*t),
    // atan2(I, Q) yields the baseband phase directly.
    const double delay = (numStages / 2) * static_cast<double>(W - 1);
    double prevPhase = 0.0;
    bool havePhase = false;
    std::vector<float> freq(M, 0.0f);
    for (size_t n = 0; n < M; ++n) {
        const double ph = std::atan2(filtI[n], filtQ[n]);
        if (havePhase) {
            double d = ph - prevPhase;
            while (d > kPi) d -= 2.0 * kPi;
            while (d < -kPi) d += 2.0 * kPi;
            freq[n] = static_cast<float>(fc + d / (2.0 * kPi) * sampleRate);
        }
        prevPhase = ph;
        havePhase = true;
    }

    // Sample at each pixel center (accounting for pad + group delay)
    for (int k = 0; k < width; ++k) {
        const double center =
            static_cast<double>(W) + k * pixelSamples + pixelSamples / 2.0;
        size_t n = static_cast<size_t>(center + delay + 0.5);
        if (n >= M) n = M - 1;
        outFreq[k] = freq[n];
    }
    return true;
}

SSTVDecoder::DecodeResult SSTVDecoder::decode(
    const std::vector<float>& samples, int sampleRate) const {
    DecodeResult result;

    const DetectionResult det = detect(samples, sampleRate);
    if (!det.found) return result;

    const SSTVProtocolInfo& info = getProtocolInfo(det.mode);
    const int w = info.width;
    const int h = info.height;

    const size_t syncSamples = static_cast<size_t>(
        info.syncDurMs * 0.001 * sampleRate + 0.5);
    const double scanMs =
        static_cast<double>(info.lineTimeMs) - info.syncDurMs;
    const double pixelMs = scanMs / w;
    const size_t pixelSamples =
        static_cast<size_t>(pixelMs * 0.001 * sampleRate + 0.5);
    if (syncSamples == 0 || pixelSamples == 0) return result;

    const size_t lineSamples = syncSamples + static_cast<size_t>(w) * pixelSamples;
    if (det.imageStart + static_cast<size_t>(h) * lineSamples > samples.size()) {
        return result; // Not enough audio for a full frame
    }

    std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * 3, 0);
    const double span =
        static_cast<double>(info.whiteFreq) - info.blackFreq;

    size_t linePos = det.imageStart;
    std::vector<float> pixelFreqs;
    for (int y = 0; y < h; ++y) {
        const size_t scanStart = linePos + syncSamples;
        const size_t scanLen = static_cast<size_t>(w) * pixelSamples;
        demodScanLine(samples, scanStart, scanLen, w, pixelSamples, sampleRate,
                     info.blackFreq, info.whiteFreq, pixelFreqs);
        for (int x = 0; x < w; ++x) {
            double norm =
                (static_cast<double>(pixelFreqs[x]) - info.blackFreq) / span;
            if (norm < 0.0) norm = 0.0;
            if (norm > 1.0) norm = 1.0;
            const uint8_t lum = static_cast<uint8_t>(norm * 255.0 + 0.5);

            const size_t idx = (static_cast<size_t>(y) * w + x) * 3;
            pixels[idx + 0] = lum;
            pixels[idx + 1] = lum;
            pixels[idx + 2] = lum;
        }
        linePos += lineSamples;
    }

    result.success = true;
    result.mode = det.mode;
    result.width = w;
    result.height = h;
    result.pixels.swap(pixels);
    return result;
}

} // namespace audiot
