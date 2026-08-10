// psk_modem.cpp - PSK modem implementation
#include "psk_modem.h"
#include <cmath>
#include <algorithm>
#include <numeric>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

namespace audiot {

static const float TWO_PI = 2.0f * static_cast<float>(M_PI);

PSKModem::PSKModem(const Config& config)
    : config_(config), phase_(0.0f), costasLoop_()
{
}

PSKModem::~PSKModem() {}

int PSKModem::sampleRate() const {
    return config_.sampleRate;
}

ModemType PSKModem::type() const {
    return ModemType::PSK;
}

int PSKModem::bitsPerSymbol() const {
    return static_cast<int>(config_.order);
}

int PSKModem::samplesPerSymbol() const {
    return config_.sampleRate / config_.symbolRate;
}

int PSKModem::bitsPerSecond() const {
    return config_.symbolRate * bitsPerSymbol();
}

std::vector<std::pair<float, float>> PSKModem::constellationMap(PSKOrder order) {
    std::vector<std::pair<float, float>> map;
    int M = 1 << static_cast<int>(order);  // 2^order points

    for (int i = 0; i < M; ++i) {
        // Phase for each symbol: evenly spaced around unit circle
        float phase = TWO_PI * static_cast<float>(i) / static_cast<float>(M);
        // For QPSK, offset by pi/4 for better noise margin
        if (order == PSKOrder::QPSK) {
            phase += static_cast<float>(M_PI) / 4.0f;
        }
        map.push_back(std::make_pair(std::cos(phase), std::sin(phase)));
    }
    return map;
}

std::vector<float> PSKModem::raiseCosineFilter(int span, float beta, int samplesPerSymbol) {
    // Raised cosine filter for pulse shaping
    // span: filter span in symbols
    // beta: roll-off factor (0 to 1)
    int filterLen = span * samplesPerSymbol + 1;
    std::vector<float> filter(filterLen);

    float T = static_cast<float>(samplesPerSymbol);
    int center = filterLen / 2;

    for (int i = 0; i < filterLen; ++i) {
        float t = static_cast<float>(i - center) / T;
        float val;

        if (std::abs(t) < 1e-6f) {
            val = 1.0f;
        } else if (beta > 0 && std::abs(t) > (1.0f - 1e-6f) / (2.0f * beta)) {
            // Beyond roll-off region
            float sinc_t = std::sin(static_cast<float>(M_PI) * t) / (static_cast<float>(M_PI) * t);
            float denom = 1.0f - (2.0f * beta * t) * (2.0f * beta * t);
            val = sinc_t * std::cos(static_cast<float>(M_PI) * beta * t) / denom;
        } else {
            // Normal sinc * cos / (1 - (2*beta*t)^2)
            float sinc_t = std::sin(static_cast<float>(M_PI) * t) / (static_cast<float>(M_PI) * t);
            float denom = 1.0f - (2.0f * beta * t) * (2.0f * beta * t);
            val = sinc_t * std::cos(static_cast<float>(M_PI) * beta * t) / denom;
        }
        filter[i] = val;
    }

    // Normalize
    float sum = 0.0f;
    for (size_t i = 0; i < filter.size(); ++i) {
        sum += filter[i] * filter[i];
    }
    sum = std::sqrt(sum);
    if (sum > 1e-6f) {
        for (size_t i = 0; i < filter.size(); ++i) {
            filter[i] /= sum;
        }
    }

    return filter;
}

std::vector<uint8_t> PSKModem::generatePreamble() const {
    // Preamble: known pattern for synchronization
    int totalBits = config_.preambleSymbols * bitsPerSymbol();
    std::vector<uint8_t> preamble(totalBits);
    for (int i = 0; i < totalBits; ++i) {
        preamble[i] = static_cast<uint8_t>(i & 1);
    }
    return preamble;
}

float PSKModem::CostasLoop::update(float input, float& I, float& Q) {
    // Mix with local oscillator
    I = input * 2.0f * std::cos(phase);
    Q = input * 2.0f * std::sin(phase);

    // Phase error detector (for BPSK/QPSK)
    // error = sign(I) * Q - sign(Q) * I (simplified)
    float error = 0.0f;
    if (std::abs(I) > 1e-6f && std::abs(Q) > 1e-6f) {
        // QPSK phase error detector
        error = (I > 0 ? 1.0f : -1.0f) * Q - (Q > 0 ? 1.0f : -1.0f) * I;
    }

    // Loop filter update
    freq += beta * error;
    phase += freq + alpha * error;

    // Keep phase in [0, 2*PI)
    while (phase >= TWO_PI) phase -= TWO_PI;
    while (phase < 0) phase += TWO_PI;

    return phase;
}

std::vector<float> PSKModem::modulate(const std::vector<uint8_t>& bits) {
    phase_ = 0.0f;

    int bps = bitsPerSymbol();
    int sps = samplesPerSymbol();
    auto constellation = constellationMap(config_.order);
    int M = static_cast<int>(constellation.size());

    // Generate pulse shaping filter
    std::vector<float> pulseFilter = raiseCosineFilter(4, 0.35f, sps);

    // Build symbol sequence from bits (with preamble)
    std::vector<uint8_t> preamble = generatePreamble();
    std::vector<uint8_t> allBits;
    allBits.reserve(preamble.size() + bits.size());
    allBits.insert(allBits.end(), preamble.begin(), preamble.end());
    allBits.insert(allBits.end(), bits.begin(), bits.end());

    // Map bits to symbols (I, Q pairs)
    std::vector<std::pair<float, float>> symbols;
    symbols.reserve(allBits.size() / bps);

    for (size_t i = 0; i + bps <= allBits.size(); i += bps) {
        int symbolIdx = 0;
        for (int b = 0; b < bps; ++b) {
            symbolIdx = (symbolIdx << 1) | (allBits[i + b] & 1);
        }
        symbolIdx = symbolIdx % M;
        symbols.push_back(constellation[symbolIdx]);
    }

    // Generate baseband I/Q waveforms with pulse shaping
    int totalSamples = static_cast<int>(symbols.size()) * sps;
    std::vector<float> basebandI(totalSamples, 0.0f);
    std::vector<float> basebandQ(totalSamples, 0.0f);

    for (size_t s = 0; s < symbols.size(); ++s) {
        int center = static_cast<int>(s) * sps;
        for (int i = 0; i < static_cast<int>(pulseFilter.size()) && (center + i) < totalSamples; ++i) {
            int idx = center + i - static_cast<int>(pulseFilter.size()) / 2;
            if (idx >= 0 && idx < totalSamples) {
                basebandI[idx] += symbols[s].first * pulseFilter[i];
                basebandQ[idx] += symbols[s].second * pulseFilter[i];
            }
        }
    }

    // Modulate onto carrier
    std::vector<float> output;
    output.reserve(totalSamples);

    float phaseInc = TWO_PI * config_.carrierFreq / static_cast<float>(config_.sampleRate);

    for (int i = 0; i < totalSamples; ++i) {
        float sample = basebandI[i] * std::cos(phase_) - basebandQ[i] * std::sin(phase_);
        output.push_back(sample);
        phase_ += phaseInc;
        if (phase_ >= TWO_PI) phase_ -= TWO_PI;
    }

    return output;
}

std::vector<uint8_t> PSKModem::demodulate(const std::vector<float>& samples) {
    if (samples.empty()) return std::vector<uint8_t>();

    int bps = bitsPerSymbol();
    int sps = samplesPerSymbol();
    auto constellation = constellationMap(config_.order);
    int M = static_cast<int>(constellation.size());

    // Reset Costas loop
    costasLoop_.phase = 0.0f;
    costasLoop_.freq = 0.0f;

    // Demodulate using Costas loop
    int totalSymbols = static_cast<int>(samples.size()) / sps;
    std::vector<std::pair<float, float>> receivedSymbols;
    receivedSymbols.reserve(totalSymbols);

    for (int s = 0; s < totalSymbols; ++s) {
        int start = s * sps;
        float sumI = 0.0f, sumQ = 0.0f;

        // Integrate over symbol period
        for (int i = 0; i < sps && (start + i) < static_cast<int>(samples.size()); ++i) {
            float I, Q;
            costasLoop_.update(samples[start + i], I, Q);
            sumI += I;
            sumQ += Q;
        }

        // Average
        sumI /= static_cast<float>(sps);
        sumQ /= static_cast<float>(sps);

        receivedSymbols.push_back(std::make_pair(sumI, sumQ));
    }

    // Find preamble to determine sync point
    int preambleSymbols = config_.preambleSymbols;
    int bestOffset = -1;
    int bestScore = 0;

    // Generate expected preamble symbols
    std::vector<uint8_t> preambleBits = generatePreamble();
    std::vector<int> expectedPreambleSyms;
    for (size_t i = 0; i + bps <= preambleBits.size(); i += bps) {
        int idx = 0;
        for (int b = 0; b < bps; ++b) {
            idx = (idx << 1) | (preambleBits[i + b] & 1);
        }
        expectedPreambleSyms.push_back(idx % M);
    }

    // Correlate with expected preamble
    int maxStart = static_cast<int>(receivedSymbols.size()) - preambleSymbols;
    for (int start = 0; start <= maxStart; ++start) {
        int score = 0;
        for (int i = 0; i < preambleSymbols && (start + i) < static_cast<int>(receivedSymbols.size()); ++i) {
            // Check if received symbol is closest to expected symbol
            float rxI = receivedSymbols[start + i].first;
            float rxQ = receivedSymbols[start + i].second;

            // Find nearest constellation point
            int nearest = 0;
            float minDist = 1e9f;
            for (int c = 0; c < M; ++c) {
                float dI = rxI - constellation[c].first;
                float dQ = rxQ - constellation[c].second;
                float dist = dI * dI + dQ * dQ;
                if (dist < minDist) {
                    minDist = dist;
                    nearest = c;
                }
            }

            if (nearest == expectedPreambleSyms[i]) {
                ++score;
            }
        }
        if (score > bestScore) {
            bestScore = score;
            bestOffset = start;
        }
    }

    // Require at least 60% match
    if (bestScore < preambleSymbols * 6 / 10) {
        return std::vector<uint8_t>();
    }

    // Extract data symbols (after preamble)
    int dataStart = bestOffset + preambleSymbols;
    std::vector<uint8_t> dataBits;

    for (int s = dataStart; s < static_cast<int>(receivedSymbols.size()); ++s) {
        float rxI = receivedSymbols[s].first;
        float rxQ = receivedSymbols[s].second;

        // Find nearest constellation point
        int nearest = 0;
        float minDist = 1e9f;
        for (int c = 0; c < M; ++c) {
            float dI = rxI - constellation[c].first;
            float dQ = rxQ - constellation[c].second;
            float dist = dI * dI + dQ * dQ;
            if (dist < minDist) {
                minDist = dist;
                nearest = c;
            }
        }

        // Convert symbol index to bits
        for (int b = bps - 1; b >= 0; --b) {
            dataBits.push_back(static_cast<uint8_t>((nearest >> b) & 1));
        }
    }

    return dataBits;
}

} // namespace audiot
