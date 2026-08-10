// mftsk_modem.cpp - M-FSK (Multi-Tone Frequency Shift Keying) modem
#include "mftsk_modem.h"
#include "dsp/fft_wrapper.h"
#include "dsp/window.h"

#include <algorithm>
#include <complex>
#include <limits>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

namespace audiot {

MFSKModem::MFSKModem(const Config& config)
    : config_(config)
{
    // Precompute the Hann window for one symbol period.
    const int sps = samplesPerSymbol();
    if (sps > 0) {
        window_ = std::vector<float>(static_cast<size_t>(sps), 1.0f);
        apply_hann(window_);
    }
}

MFSKModem::~MFSKModem() {}

int MFSKModem::sampleRate() const {
    return config_.sampleRate;
}

ModemType MFSKModem::type() const {
    return ModemType::MFSK;
}

int MFSKModem::bitsPerSecond() const {
    return config_.symbolRate * config_.order;
}

int MFSKModem::samplesPerSymbol() const {
    if (config_.symbolRate <= 0) return 0;
    return config_.sampleRate / config_.symbolRate;
}

int MFSKModem::slotCount() const {
    return 1 << config_.order;  // M = 2^order
}

float MFSKModem::slotFreq(int i) const {
    return config_.baseFreq + static_cast<float>(i) * config_.slotSpacing;
}

std::vector<uint8_t> MFSKModem::generatePreamble() const {
    // Preamble: a known, fixed slot sequence used by the demodulator to
    // locate the start of the data burst. Use alternating slots.
    std::vector<uint8_t> preamble;
    preamble.reserve(static_cast<size_t>(config_.preambleSymbols));
    const int M = slotCount();
    for (int i = 0; i < config_.preambleSymbols; ++i) {
        preamble.push_back(static_cast<uint8_t>(i % M));
    }
    return preamble;
}

std::vector<float> MFSKModem::modulate(const std::vector<uint8_t>& bits) {
    const int sps = samplesPerSymbol();
    const int M = slotCount();
    if (sps <= 0 || M <= 1 || bits.empty()) {
        return std::vector<float>();
    }

    // Build the sequence of slot indices (preamble + data).
    std::vector<int> slots;
    // Prepend preamble.
    {
        const std::vector<uint8_t> pre = generatePreamble();
        slots.reserve(pre.size() + bits.size() / config_.order + 1);
        for (size_t i = 0; i < pre.size(); ++i) slots.push_back(static_cast<int>(pre[i]));
    }
    // Map bits -> slot index.
    for (size_t i = 0; i + static_cast<size_t>(config_.order) <= bits.size(); i += static_cast<size_t>(config_.order)) {
        int idx = 0;
        for (int b = 0; b < config_.order; ++b) {
            idx = (idx << 1) | (bits[i + static_cast<size_t>(b)] & 1);
        }
        slots.push_back(idx % M);
    }

    // Phase-continuous sine synthesis.
    std::vector<float> out;
    out.reserve(slots.size() * static_cast<size_t>(sps));

    double phase = 0.0;
    for (size_t s = 0; s < slots.size(); ++s) {
        const float freq = slotFreq(slots[s]);
        if (freq <= 0.0f) {
            // Safety: emit a silent symbol.
            for (int i = 0; i < sps; ++i) out.push_back(0.0f);
            continue;
        }
        const double phaseStep = 2.0 * 3.14159265358979323846 * static_cast<double>(freq) /
                                 static_cast<double>(config_.sampleRate);
        for (int i = 0; i < sps; ++i) {
            out.push_back(static_cast<float>(std::sin(phase)) * 0.5f);
            phase += phaseStep;
            if (phase >= 2.0 * 3.14159265358979323846) phase -= 2.0 * 3.14159265358979323846;
        }
    }
    return out;
}

std::vector<uint8_t> MFSKModem::demodulate(const std::vector<float>& samples) {
    const int sps = samplesPerSymbol();
    const int M = slotCount();
    if (sps <= 0 || M <= 1 || samples.empty()) {
        return std::vector<uint8_t>();
    }

    const int order = config_.order;
    const int preambleSymbols = config_.preambleSymbols;
    size_t fftSize = static_cast<size_t>(sps);

    FFTWrapper fft(fftSize);
    std::vector<float> buf(fftSize, 0.0f);
    std::vector<std::complex<float>> spec;

    const std::vector<uint8_t> preamble = generatePreamble();

    // Multi-phase scan. The transmitter's symbol grid may start at any
    // sample phase relative to the receiver's (acoustic path timing, tape
    // speed, etc.). We demodulate the burst for every possible integer
    // sample phase 0..sps-1 (coarse window alignment) and keep the phase
    // whose preamble matches best. Windowed-FFT peak detection is
    // sensitive to window placement, so searching the phase tames
    // fractional-symbol timing offset robustly (conservative choice).
    int bestPhaseScore = -1;
    std::vector<uint8_t> bestBits;               // demodulated bits for best phase
    size_t bestDataStart = static_cast<size_t>(preambleSymbols);

    // Limit phase search: full sps scan is heavy; sample every step but
    // bail out early once a full-preamble match is found (score ==
    // preambleSymbols). We scan the first 2*sps samples' worth of phases.
    const int phaseSearchLimit = sps;  // all phases; preamble must be present
    for (int ph = 0; ph < phaseSearchLimit; ++ph) {
        const size_t symbolCount = samples.size() / static_cast<size_t>(sps);
        if (symbolCount == 0) break;

        std::vector<uint8_t> symbolRx;
        symbolRx.reserve(symbolCount);

        for (size_t s = 0; s < symbolCount; ++s) {
            const size_t start = ph + s * static_cast<size_t>(sps);
            if (start + static_cast<size_t>(sps) > samples.size()) break;
            std::fill(buf.begin(), buf.end(), 0.0f);
            for (int i = 0; i < sps; ++i) {
                const float w = window_.empty() ? 1.0f : window_[static_cast<size_t>(i)];
                buf[static_cast<size_t>(i)] = samples[start + static_cast<size_t>(i)] * w;
            }

            spec.clear();
            fft.forward(buf, spec);

            int bestSlot = 0;
            float bestMag = -1.0f;
            for (int k = 0; k < M; ++k) {
                const float targetFreq = slotFreq(k);
                int bin = static_cast<int>(static_cast<double>(targetFreq) *
                                           static_cast<double>(fftSize) /
                                           static_cast<double>(config_.sampleRate) + 0.5);
                if (bin < 0) bin = 0;
                if (bin >= static_cast<int>(fftSize)) bin = static_cast<int>(fftSize) - 1;
                const float mag = std::abs(spec[static_cast<size_t>(bin)]);
                if (mag > bestMag) {
                    bestMag = mag;
                    bestSlot = k;
                }
            }
            symbolRx.push_back(static_cast<uint8_t>(bestSlot));
        }

        if (symbolRx.size() <= static_cast<size_t>(preambleSymbols)) {
            continue;
        }

        // Preamble match at this phase.
        int score = 0;
        size_t dataStart = static_cast<size_t>(preambleSymbols);
        const size_t searchLen = symbolRx.size() - static_cast<size_t>(preambleSymbols) + 1;
        for (size_t off = 0; off < searchLen; ++off) {
            int sc = 0;
            for (int p = 0; p < preambleSymbols; ++p) {
                if (symbolRx[off + static_cast<size_t>(p)] == preamble[static_cast<size_t>(p)]) {
                    ++sc;
                }
            }
            if (sc > score) {
                score = sc;
                dataStart = off + static_cast<size_t>(preambleSymbols);
            }
        }

        if (score > bestPhaseScore) {
            bestPhaseScore = score;
            bestDataStart = dataStart;
            bestBits.clear();
            bestBits.reserve((symbolRx.size() - bestDataStart) * static_cast<size_t>(order));
            for (size_t s = bestDataStart; s < symbolRx.size(); ++s) {
                int slot = static_cast<int>(symbolRx[s]);
                for (int b = order - 1; b >= 0; --b) {
                    bestBits.push_back(static_cast<uint8_t>((slot >> b) & 1));
                }
            }
        }

        // Early exit: perfect preamble match found.
        if (bestPhaseScore >= preambleSymbols) {
            break;
        }
    }

    if (bestPhaseScore < 0) {
        return std::vector<uint8_t>();
    }
    return bestBits;
}

} // namespace audiot
