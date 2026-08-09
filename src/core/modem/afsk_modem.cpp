// afsk_modem.cpp - AFSK modem implementation
#include "afsk_modem.h"
#include <cmath>
#include <algorithm>
#include <limits>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

namespace audiot {

static const float TWO_PI = 2.0f * static_cast<float>(M_PI);

AFSKModem::AFSKModem(const Config& config)
    : config_(config), phase_(0.0f)
{
}

AFSKModem::~AFSKModem() {}

int AFSKModem::sampleRate() const {
    return config_.sampleRate;
}

ModemType AFSKModem::type() const {
    return ModemType::AFSK;
}

int AFSKModem::bitsPerSecond() const {
    return config_.baudRate;
}

int AFSKModem::samplesPerBit() const {
    return config_.sampleRate / config_.baudRate;
}

std::vector<uint8_t> AFSKModem::generatePreamble() const {
    // Alternating 1,0 pattern for clock recovery
    std::vector<uint8_t> preamble(config_.preambleBits);
    for (int i = 0; i < config_.preambleBits; ++i) {
        preamble[i] = static_cast<uint8_t>(i & 1);
    }
    return preamble;
}

std::vector<uint8_t> AFSKModem::generatePostamble() const {
    // All-zero tail
    return std::vector<uint8_t>(config_.postambleBits, 0);
}

void AFSKModem::generateTone(std::vector<float>& output, uint8_t bit) {
    float freq = (bit != 0) ? config_.markFreq : config_.spaceFreq;
    int n = samplesPerBit();
    float phaseInc = TWO_PI * freq / static_cast<float>(config_.sampleRate);

    output.reserve(output.size() + n);
    for (int i = 0; i < n; ++i) {
        output.push_back(std::sin(phase_));
        phase_ += phaseInc;
        // Keep phase in [0, 2*PI) to avoid precision loss
        if (phase_ >= TWO_PI) {
            phase_ -= TWO_PI;
        }
    }
}

std::vector<float> AFSKModem::modulate(const std::vector<uint8_t>& bits) {
    // Reset phase for clean start
    phase_ = 0.0f;

    std::vector<float> output;

    // Preamble
    std::vector<uint8_t> preamble = generatePreamble();
    for (size_t i = 0; i < preamble.size(); ++i) {
        generateTone(output, preamble[i]);
    }

    // Data bits
    for (size_t i = 0; i < bits.size(); ++i) {
        generateTone(output, bits[i]);
    }

    // Postamble
    std::vector<uint8_t> postamble = generatePostamble();
    for (size_t i = 0; i < postamble.size(); ++i) {
        generateTone(output, postamble[i]);
    }

    return output;
}

float AFSKModem::goertzel(const std::vector<float>& samples,
                           size_t start, size_t len, float targetFreq) const {
    // Goertzel algorithm for single-frequency energy detection
    // More efficient than FFT when only checking specific frequencies
    float k = targetFreq * static_cast<float>(len) / static_cast<float>(config_.sampleRate);
    float w = TWO_PI * k / static_cast<float>(len);
    float coeff = 2.0f * std::cos(w);

    float s0 = 0.0f;
    float s1 = 0.0f;
    float s2 = 0.0f;

    size_t end = start + len;
    if (end > samples.size()) end = samples.size();

    for (size_t i = start; i < end; ++i) {
        s0 = samples[i] + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }

    // Energy = s1^2 + s2^2 - coeff*s1*s2
    float energy = s1 * s1 + s2 * s2 - coeff * s1 * s2;
    return energy;
}

int AFSKModem::findSync(const std::vector<uint8_t>& rawBits,
                        const std::vector<float>* energies,
                        int* outScore) const {
    // Look for the alternating preamble pattern
    // Expected: 1,0,1,0,... or 0,1,0,1,...
    int preambleLen = config_.preambleBits;
    if (static_cast<int>(rawBits.size()) < preambleLen + 4) return -1;

    int bestOffset = -1;
    int bestScore = 0;

    // Energy gate: only windows whose combined tone energy is meaningful
    // count towards the match. Silence and weak noise decide randomly
    // (Goertzel energy ~0), and a long enough random run can still
    // reproduce the alternating preamble pattern; a real preamble sits on
    // signal, so gating the score on energy kills such ghost matches
    // (verified: 31 s of silence before the signal produced a perfect
    // 32/32 fake preamble at the wrong position). The threshold must only
    // exclude exact silence: analog chains (e.g. a tape deck into the
    // mic input) can deliver the signal at amplitude ~0.001, whose window
    // energy is ~1e-4; a 1e-3 gate rejected it and killed all decoding
    // (reported: "no reaction at all" on a tape-deck source).
    static const float kMinWindowEnergy = 1e-6f;

    // Try each possible start position
    int maxStart = static_cast<int>(rawBits.size()) - preambleLen;
    for (int start = 0; start <= maxStart; ++start) {
        int score = 0;
        for (int i = 0; i < preambleLen; ++i) {
            if (rawBits[start + i] != static_cast<uint8_t>(i & 1)) continue;
            if (energies && (*energies)[start + i] <= kMinWindowEnergy) continue;
            ++score;
        }
        if (score > bestScore) {
            bestScore = score;
            bestOffset = start;
        }
    }

    // Require at least 75% match
    if (bestScore < preambleLen * 3 / 4) return -1;
    if (outScore) *outScore = bestScore;

    // Data starts after preamble
    return bestOffset + preambleLen;
}

size_t AFSKModem::locatePrepEnd(const std::vector<float>& samples) const {
    // The transmitter always plays a continuous mark-frequency prep tone
    // (its length is a whole multiple of the symbol period) before the
    // data burst, so the data preamble starts exactly where the tone
    // ends. Locating that end gives the receiver a deterministic signal
    // start: the demodulator then works on the signal only, and the long
    // silence/noise run before it can never fake a preamble.
    //
    // Window-level scan: a window is "mark" when its mark energy clearly
    // dominates. The prep tone is a long run of consecutive mark windows
    // (>= 0.5 s); data bits can produce short runs (e.g. a 0xFF byte = 8
    // consecutive marks) but never one that long, so the longest run is
    // the prep tone. The first preamble bit is itself a mark and extends
    // the run by up to one extra window, which the caller's margin
    // absorbs.
    const size_t npos = std::numeric_limits<size_t>::max();
    const int spb = samplesPerBit();
    if (spb <= 1 || samples.size() < static_cast<size_t>(spb) * 4) {
        return npos;
    }

    static const float kMinMarkEnergy = 1e-6f;   // same gate as findSync
    const size_t windows = samples.size() / static_cast<size_t>(spb);

    size_t bestStart = 0;
    size_t bestLen = 0;
    size_t curStart = 0;
    size_t curLen = 0;
    for (size_t w = 0; w < windows; ++w) {
        const size_t start = w * static_cast<size_t>(spb);
        const float markE = goertzel(samples, start,
                                     static_cast<size_t>(spb), config_.markFreq);
        const float spaceE = goertzel(samples, start,
                                      static_cast<size_t>(spb), config_.spaceFreq);
        const bool isMark = markE > spaceE && markE > kMinMarkEnergy;
        if (isMark) {
            if (curLen == 0) curStart = w;
            ++curLen;
        } else {
            if (curLen > bestLen) { bestLen = curLen; bestStart = curStart; }
            curLen = 0;
        }
    }
    if (curLen > bestLen) { bestLen = curLen; bestStart = curStart; }

    // Require a run of at least 0.5 s of mark windows
    const size_t minPrepWindows = static_cast<size_t>(config_.baudRate) / 2;
    if (bestLen < minPrepWindows) {
        return npos;
    }

    // Prep tone ends at the boundary after its last mark window
    return (bestStart + bestLen) * static_cast<size_t>(spb);
}

std::vector<uint8_t> AFSKModem::demodulate(const std::vector<float>& samples) {
    if (samples.empty()) return std::vector<uint8_t>();

    const int spb = samplesPerBit();
    const size_t totalBits = samples.size() / spb;
    if (totalBits == 0) return std::vector<uint8_t>();

    // Step 1: Demodulate each symbol period using Goertzel, on a coarse
    // grid of symbol phase offsets. AFSK has no sample-level sync between
    // the transmitter's playback start and the receiver's capture start:
    // the signal can begin at ANY sample offset inside the buffer. Slicing
    // the stream into fixed spb-sample windows from offset 0 (the
    // historical behaviour) corrupts the Goertzel decisions when the
    // phase is off by a large fraction of a symbol (verified: realtime
    // capture, 27-32 samples off => decode fails). We keep the phase
    // whose preamble matches best: the preamble is a strong, clean
    // alternating pattern, so its match score is a reliable phase
    // quality measure. The winning phase's bit stream is then used for
    // data extraction.
    const int phaseStep = std::max(1, spb / 6);   // 6 candidate phases
    int bestScore = -1;
    int bestSync = -1;
    std::vector<uint8_t> rawBits;     // bit stream of the best phase
    std::vector<float> rawEnergies;   // per-window tone energy of that stream

    for (int phase = 0; phase < spb; phase += phaseStep) {
        std::vector<uint8_t> cand;
        std::vector<float> energies;
        cand.reserve(totalBits);
        energies.reserve(totalBits);
        for (size_t i = 0; i < totalBits; ++i) {
            const size_t start = i * static_cast<size_t>(spb) + static_cast<size_t>(phase);
            if (start >= samples.size()) break;   // nothing left
            // The last window may be shorter than a full symbol when the
            // buffer ends mid-symbol (e.g. after realtime trimming); use
            // the remaining samples instead of dropping the symbol, so
            // every phase keeps the same bit count.
            const size_t len = std::min(static_cast<size_t>(spb), samples.size() - start);
            const float markEnergy = goertzel(samples, start, len, config_.markFreq);
            const float spaceEnergy = goertzel(samples, start, len, config_.spaceFreq);
            energies.push_back(markEnergy + spaceEnergy);
            cand.push_back(markEnergy > spaceEnergy ? 1 : 0);
        }
        int score = 0;
        const int sync = findSync(cand, &energies, &score);
        // Pick the phase with the best preamble match; on ties prefer the
        // EARLIER sync point: a wrong phase can still reproduce an
        // alternating pattern (e.g. a bit stream shifted by one symbol),
        // but its preamble position is then off by that shift, while the
        // correctly aligned phase reports the true preamble start.
        if (sync >= 0 &&
            (score > bestScore || (score == bestScore && sync < bestSync))) {
            bestScore = score;
            bestSync = sync;
            rawBits.swap(cand);
            rawEnergies.swap(energies);
        }
    }

    if (bestScore < 0) {
        // No phase produced a valid preamble, give up
        return std::vector<uint8_t>();
    }

    // Step 2: Find preamble sync (re-run on the winning phase's stream;
    // cheap and keeps the extraction logic below self-contained)
    int dataStart = findSync(rawBits, &rawEnergies);
    if (dataStart < 0) {
        return std::vector<uint8_t>();
    }

    // Step 3: Extract data bits (after preamble, before postamble)
    int totalPreamble = config_.preambleBits;
    int totalPostamble = config_.postambleBits;
    int rawTotal = static_cast<int>(rawBits.size());

    int dataEnd = rawTotal - totalPostamble;
    if (dataEnd <= dataStart) {
        // Try to find where postamble starts (run of zeros)
        dataEnd = rawTotal;
        for (int i = dataStart; i < rawTotal; ++i) {
            // Check for a run of zeros indicating postamble
            int zeroRun = 0;
            for (int j = i; j < rawTotal && rawBits[j] == 0; ++j) {
                ++zeroRun;
            }
            if (zeroRun >= totalPostamble / 2) {
                dataEnd = i;
                break;
            }
        }
    }

    std::vector<uint8_t> dataBits;
    for (int i = dataStart; i < dataEnd; ++i) {
        dataBits.push_back(rawBits[i]);
    }

    return dataBits;
}

} // namespace audiot
