// qam_modem.h - QAM (Quadrature Amplitude Modulation) modem implementation
#ifndef AUDIOT_QAM_MODEM_H
#define AUDIOT_QAM_MODEM_H

#include <vector>
#include <cstdint>
#include <cmath>
#include <utility>
#include "interfaces/IModem.h"

namespace audiot {

// QAM modem supporting 16-QAM, 32-QAM, and 64-QAM modulation
// Uses raised cosine pulse shaping and coherent demodulation
class QAMModem : public IModem {
public:
    enum class QAMOrder { QAM16 = 4, QAM32 = 5, QAM64 = 6 };  // Value = bits per symbol

    struct Config {
        int sampleRate;
        float carrierFreq;
        QAMOrder order;
        int symbolRate;
        int preambleSymbols;

        Config()
            : sampleRate(44100)
            , carrierFreq(1800.0f)
            , order(QAMOrder::QAM16)
            , symbolRate(1200)
            , preambleSymbols(32)
        {}
    };

    explicit QAMModem(const Config& config = Config());
    virtual ~QAMModem();

    std::vector<float> modulate(const std::vector<uint8_t>& bits) override;
    std::vector<uint8_t> demodulate(const std::vector<float>& samples) override;
    int sampleRate() const override;
    ModemType type() const override;
    int bitsPerSecond() const override;

private:
    Config config_;
    float phase_;

    // Get QAM constellation map
    static std::vector<std::pair<float, float>> constellationMap(QAMOrder order);

    // Generate raised cosine pulse shaping filter
    std::vector<float> raiseCosineFilter(int span, float beta, int samplesPerSymbol);

    // Find nearest constellation point (hard decision)
    int nearestConstellationPoint(float I, float Q) const;

    // Bits per symbol
    int bitsPerSymbol() const;

    // Samples per symbol
    int samplesPerSymbol() const;

    // Generate preamble
    std::vector<uint8_t> generatePreamble() const;
};

} // namespace audiot
#endif // AUDIOT_QAM_MODEM_H
