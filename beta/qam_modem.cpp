// qam_modem.cpp - QAM modem implementation
#include "qam_modem.h"
#include <cmath>
#include <algorithm>
#include <limits>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

namespace audiot {

static const float TWO_PI = 2.0f * static_cast<float>(M_PI);

QAMModem::QAMModem(const Config& config)
    : config_(config), phase_(0.0f)
{
}

QAMModem::~QAMModem() {}

int QAMModem::sampleRate() const {
    return config_.sampleRate;
}

ModemType QAMModem::type() const {
    return ModemType::QAM;
}

int QAMModem::bitsPerSymbol() const {
    return static_cast<int>(config_.order);
}

int QAMModem::samplesPerSymbol() const {
    return config_.sampleRate / config_.symbolRate;
}

int QAMModem::bitsPerSecond() const {
    return config_.symbolRate * bitsPerSymbol();
}

std::vector<std::pair<float, float>> QAMModem::constellationMap(QAMOrder order) {
    std::vector<std::pair<float, float>> map;
    int bitsPerSym = static_cast<int>(order);
    int M = 1 << bitsPerSym;  // Number of constellation points

    if (order == QAMOrder::QAM16) {
        // 4x4 square QAM constellation
        // Gray coding for minimum bit errors
        float levels[] = {-3.0f, -1.0f, 1.0f, 3.0f};
        // Gray code mapping for I and Q
        int grayI[] = {0, 1, 3, 2};  // Gray code order
        int grayQ[] = {0, 1, 3, 2};

        for (int q = 0; q < 4; ++q) {
            for (int i = 0; i < 4; ++i) {
                map.push_back(std::make_pair(levels[grayI[i]], levels[grayQ[q]]));
            }
        }
    } else if (order == QAMOrder::QAM32) {
        // 32-QAM: non-standard but usable (4x8 or cross constellation)
        // Using 4x8 rectangular constellation for simplicity
        float levelsI[] = {-3.0f, -1.0f, 1.0f, 3.0f};
        float levelsQ[] = {-7.0f, -5.0f, -3.0f, -1.0f, 1.0f, 3.0f, 5.0f, 7.0f};

        for (int q = 0; q < 8; ++q) {
            for (int i = 0; i < 4; ++i) {
                map.push_back(std::make_pair(levelsI[i], levelsQ[q]));
            }
        }
    } else if (order == QAMOrder::QAM64) {
        // 8x8 square QAM constellation
        float levels[] = {-7.0f, -5.0f, -3.0f, -1.0f, 1.0f, 3.0f, 5.0f, 7.0f};
        int gray[] = {0, 1, 3, 2, 6, 7, 5, 4};  // Gray code order

        for (int q = 0; q < 8; ++q) {
            for (int i = 0; i < 8; ++i) {
                map.push_back(std::make_pair(levels[gray[i]], levels[gray[q]]));
            }
        }
    }

    // Normalize constellation to unit power
    if (!map.empty()) {
        float sumPower = 0.0f;
        for (size_t i = 0; i < map.size(); ++i) {
            sumPower += map[i].first * map[i].first + map[i].second * map[i].second;
        }
        float norm = std::sqrt(sumPower / static_cast<float>(map.size()));
        if (norm > 1e-6f) {
            for (size_t i = 0; i < map.size(); ++i) {
                map[i].first /= norm;
                map[i].second /= norm;
            }
        }
    }

    return map;
}

std::vector<float> QAMModem::raiseCosineFilter(int span, float beta, int samplesPerSymbol) {
    int filterLen = span * samplesPerSymbol + 1;
    std::vector<float> filter(filterLen);

    float T = static_cast<float>(samplesPerSymbol);
    int center = filterLen / 2;

    for (int i = 0; i < filterLen; ++i) {
        float t = static_cast<float>(i - center) / T;
        float val;

        if (std::abs(t) < 1e-6f) {
            val = 1.0f;
        } else {
            float sinc_t = std::sin(static_cast<float>(M_PI) * t) / (static_cast<float>(M_PI) * t);
            float denom = 1.0f - (2.0f * beta * t) * (2.0f * beta * t);
            if (std::abs(denom) < 1e-6f) {
                val = sinc_t * static_cast<float>(M_PI) / 4.0f;
            } else {
                val = sinc_t * std::cos(static_cast<float>(M_PI) * beta * t) / denom;
            }
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

int QAMModem::nearestConstellationPoint(float I, float Q) const {
    auto constellation = constellationMap(config_.order);
    int nearest = 0;
    float minDist = std::numeric_limits<float>::max();

    for (size_t c = 0; c < constellation.size(); ++c) {
        float dI = I - constellation[c].first;
        float dQ = Q - constellation[c].second;
        float dist = dI * dI + dQ * dQ;
        if (dist < minDist) {
            minDist = dist;
            nearest = static_cast<int>(c);
        }
    }

    return nearest;
}

std::vector<uint8_t> QAMModem::generatePreamble() const {
    int totalBits = config_.preambleSymbols * bitsPerSymbol();
    std::vector<uint8_t> preamble(totalBits);
    for (int i = 0; i < totalBits; ++i) {
        preamble[i] = static_cast<uint8_t>(i & 1);
    }
    return preamble;
}

std::vector<float> QAMModem::modulate(const std::vector<uint8_t>& bits) {
    phase_ = 0.0f;

    int bps = bitsPerSymbol();
    int sps = samplesPerSymbol();
    auto constellation = constellationMap(config_.order);
    int M = static_cast<int>(constellation.size());

    // Generate pulse shaping filter
    std::vector<float> pulseFilter = raiseCosineFilter(4, 0.35f, sps);

    // Build symbol sequence (with preamble)
    std::vector<uint8_t> preamble = generatePreamble();
    std::vector<uint8_t> allBits;
    allBits.reserve(preamble.size() + bits.size());
    allBits.insert(allBits.end(), preamble.begin(), preamble.end());
    allBits.insert(allBits.end(), bits.begin(), bits.end());

    // Map bits to symbols
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
        for (int i = 0; i < static_cast<int>(pulseFilter.size()); ++i) {
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

std::vector<uint8_t> QAMModem::demodulate(const std::vector<float>& samples) {
    if (samples.empty()) return std::vector<uint8_t>();

    int bps = bitsPerSymbol();
    int sps = samplesPerSymbol();
    auto constellation = constellationMap(config_.order);
    int M = static_cast<int>(constellation.size());

    float phaseInc = TWO_PI * config_.carrierFreq / static_cast<float>(config_.sampleRate);
    float rxPhase = 0.0f;

    // Coherent demodulation (assume carrier sync for now)
    int totalSymbols = static_cast<int>(samples.size()) / sps;
    std::vector<std::pair<float, float>> receivedSymbols;
    receivedSymbols.reserve(totalSymbols);

    for (int s = 0; s < totalSymbols; ++s) {
        int start = s * sps;
        float sumI = 0.0f, sumQ = 0.0f;

        // Integrate over symbol period with carrier recovery
        for (int i = 0; i < sps && (start + i) < static_cast<int>(samples.size()); ++i) {
            float sample = samples[start + i];
            sumI += sample * 2.0f * std::cos(rxPhase);
            sumQ -= sample * 2.0f * std::sin(rxPhase);
            rxPhase += phaseInc;
            if (rxPhase >= TWO_PI) rxPhase -= TWO_PI;
        }

        // Average
        sumI /= static_cast<float>(sps);
        sumQ /= static_cast<float>(sps);

        receivedSymbols.push_back(std::make_pair(sumI, sumQ));
    }

    // Find preamble sync
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
            int nearest = nearestConstellationPoint(
                receivedSymbols[start + i].first,
                receivedSymbols[start + i].second);
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

    // Extract data symbols
    int dataStart = bestOffset + preambleSymbols;
    std::vector<uint8_t> dataBits;

    for (int s = dataStart; s < static_cast<int>(receivedSymbols.size()); ++s) {
        int nearest = nearestConstellationPoint(
            receivedSymbols[s].first,
            receivedSymbols[s].second);

        // Convert symbol index to bits
        for (int b = bps - 1; b >= 0; --b) {
            dataBits.push_back(static_cast<uint8_t>((nearest >> b) & 1));
        }
    }

    return dataBits;
}

} // namespace audiot
