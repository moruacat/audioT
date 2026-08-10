// psk_modem.h - PSK (Phase Shift Keying) modem implementation
#ifndef AUDIOT_PSK_MODEM_H
#define AUDIOT_PSK_MODEM_H

#include <vector>
#include <cstdint>
#include <cmath>
#include <utility>
#include "interfaces/IModem.h"

namespace audiot {

// PSK modem supporting BPSK, QPSK, and 8PSK modulation
// Uses Costas loop for carrier synchronization in demodulation
class PSKModem : public IModem {
public:
    enum class PSKOrder { BPSK = 1, QPSK = 2, PSK8 = 3 };

    struct Config {
        int sampleRate;
        float carrierFreq;
        PSKOrder order;
        int symbolRate;
        int preambleSymbols;

        Config()
            : sampleRate(44100)
            , carrierFreq(1800.0f)
            , order(PSKOrder::QPSK)
            , symbolRate(1200)
            , preambleSymbols(32)
        {}
    };

    explicit PSKModem(const Config& config = Config());
    virtual ~PSKModem();

    std::vector<float> modulate(const std::vector<uint8_t>& bits) override;
    std::vector<uint8_t> demodulate(const std::vector<float>& samples) override;
    int sampleRate() const override;
    ModemType type() const override;
    int bitsPerSecond() const override;

private:
    Config config_;
    float phase_;

    // Costas loop for carrier recovery
    struct CostasLoop {
        float phase;
        float freq;
        float alpha;  // Loop filter parameter
        float beta;

        CostasLoop() : phase(0.0f), freq(0.0f), alpha(0.05f), beta(0.001f) {}

        // Update loop: input is baseband signal, outputs I/Q components
        // Returns corrected phase
        float update(float input, float& I, float& Q);
    } costasLoop_;

    // Get constellation map for given PSK order
    static std::vector<std::pair<float, float>> constellationMap(PSKOrder order);

    // Generate raised cosine pulse shaping filter
    std::vector<float> raiseCosineFilter(int span, float beta, int samplesPerSymbol);

    // Bits per symbol
    int bitsPerSymbol() const;

    // Samples per symbol
    int samplesPerSymbol() const;

    // Generate preamble symbols
    std::vector<uint8_t> generatePreamble() const;
};

} // namespace audiot
#endif // AUDIOT_PSK_MODEM_H
