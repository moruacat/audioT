#ifndef AUDIOT_IMODEM_H
#define AUDIOT_IMODEM_H

#include <vector>
#include <cstdint>

namespace audiot {

// Modulation mode enumeration
enum class ModemType {
    AFSK,   // Audio Frequency Shift Keying
    PSK,    // Phase Shift Keying
    QAM     // Quadrature Amplitude Modulation
};

// Modem abstract interface: converts between bits and audio samples
class IModem {
public:
    virtual ~IModem() = default;

    // Modulate: convert bit data to audio samples (float, range -1.0~1.0)
    virtual std::vector<float> modulate(const std::vector<uint8_t>& bits) = 0;

    // Demodulate: convert audio samples to bit data
    virtual std::vector<uint8_t> demodulate(const std::vector<float>& samples) = 0;

    // Get current sample rate
    virtual int sampleRate() const = 0;

    // Get modulation mode
    virtual ModemType type() const = 0;

    // Get bits per second
    virtual int bitsPerSecond() const = 0;
};

} // namespace audiot
#endif
