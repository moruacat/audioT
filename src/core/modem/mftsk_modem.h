// mftsk_modem.h - M-FSK (Multi-Tone Frequency Shift Keying) modem
//
// A conservative, interference-robust modulation built on the project's
// FFT engine (KissFFT via FFTWrapper). Each symbol maps to ONE of M
// mutually orthogonal frequency slots (default M=4, i.e. 2 bits/symbol).
// Because the slot frequencies are chosen so that an integer number of
// carrier cycles fits exactly in one symbol period, the FFT bins align
// to the slot frequencies and the demodulator picks the bin with the
// largest magnitude. This is intrinsically resistant to single-frequency
// noise / interference: a narrowband tone only affects one bin, whose
// slot then simply won't win the per-symbol peak decision.
//
//   - Orthogonal slot spacing:  df = 1 / symbolPeriod
//   - Slots live in the speech band (~1500-3500 Hz) to survive acoustic
//     channels (speaker->mic, tape).
//
// Design notes (conservative, robustness first):
//   * Phase-continuous sinusoid synthesis (no clicks between symbols),
//     plus a Hann window per symbol so the on-channel sidelobes of every
//     active carrier do not leak into neighbouring FFT bins.
//   * Preamble of fixed known symbols for frame sync + correlator.
//   * Demodulation: per-symbol Hann window -> FFT -> peak-bin decision.

#ifndef AUDIOT_MFSK_MODEM_H
#define AUDIOT_MFSK_MODEM_H

#include <vector>
#include <cstdint>
#include <cmath>
#include "interfaces/IModem.h"

namespace audiot {

// M-FSK modem. Each symbol conveys log2(M) bits by transmitting one of
// M orthogonal frequencies. Default: M=4 -> 2 bits/symbol.
class MFSKModem : public IModem {
public:
    struct Config {
        int sampleRate;       // Sample rate, default 44100
        float baseFreq;       // Lowest slot frequency, default 1500 Hz
        float slotSpacing;    // Orthogonal slot spacing (df), default 50 Hz
        int order;            // Bits per symbol (M = 1<<order), default 2 (4-FSK)
        int symbolRate;       // Symbols per second, default 50 (20 ms/symbol)
        int preambleSymbols;  // Preamble symbol count, default 8

        Config()
            : sampleRate(44100)
            , baseFreq(1500.0f)
            , slotSpacing(50.0f)
            , order(2)
            , symbolRate(50)
            , preambleSymbols(8)
        {}
    };

    explicit MFSKModem(const Config& config = Config());
    virtual ~MFSKModem();

    // --- IModem interface ---
    std::vector<float> modulate(const std::vector<uint8_t>& bits) override;
    std::vector<uint8_t> demodulate(const std::vector<float>& samples) override;
    int sampleRate() const override;
    ModemType type() const override;
    int bitsPerSecond() const override;

private:
    Config config_;

    // Per-symbol sample count (must equal an integer so that each slot's
    // carrier has an exact integer number of cycles -> FFT bin alignment).
    int samplesPerSymbol() const;

    // Number of slots M = 1 << order
    int slotCount() const;

    // Frequency of slot i (0-based)
    float slotFreq(int i) const;

    // Precomputed Hann window for a symbol period (size samplesPerSymbol()).
    std::vector<float> window_;

    // Preamble: fixed sequence of slot indices (for sync / known data).
    std::vector<uint8_t> generatePreamble() const;
};

} // namespace audiot
#endif // AUDIOT_MFSK_MODEM_H
