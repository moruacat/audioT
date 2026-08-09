// afsk_modem.h - AFSK (Audio Frequency Shift Keying) modem, Bell 202 style
#ifndef AUDIOT_AFSK_MODEM_H
#define AUDIOT_AFSK_MODEM_H

#include <vector>
#include <cstdint>
#include <cmath>
#include "interfaces/IModem.h"

namespace audiot {

// AFSK modem implementing Bell 202 style modulation/demodulation
// Uses phase-continuous sinusoids and Goertzel-based demodulation
class AFSKModem : public IModem {
public:
    struct Config {
        int sampleRate;       // Sample rate, default 44100
        float markFreq;       // Mark frequency (bit=1), default 1200 Hz
        float spaceFreq;      // Space frequency (bit=0), default 2200 Hz
        int baudRate;         // Baud rate, default 1200 bps
        int preambleBits;     // Preamble bit count, default 32
        int postambleBits;    // Postamble bit count, default 16

        Config()
            : sampleRate(44100)
            , markFreq(1200.0f)
            , spaceFreq(2200.0f)
            , baudRate(1200)
            , preambleBits(32)
            , postambleBits(16)
        {}
    };

    explicit AFSKModem(const Config& config = Config());
    virtual ~AFSKModem();

    // Modulate bits into audio samples
    std::vector<float> modulate(const std::vector<uint8_t>& bits) override;

    // Demodulate audio samples into bits
    std::vector<uint8_t> demodulate(const std::vector<float>& samples) override;

    // Locate the end of the mandatory prep tone. The transmitter plays a
    // continuous mark-frequency tone (its length is a whole multiple of
    // the symbol period) before the data burst, so the data preamble
    // starts exactly where the tone ends. Returns the sample index of
    // that end, or npos if no sufficiently long mark-tone run is found.
    // The receiver uses this as a deterministic "signal started" marker
    // instead of scanning the whole buffer: silence windows decide
    // randomly and can fake a preamble pattern.
    size_t locatePrepEnd(const std::vector<float>& samples) const;

    int sampleRate() const override;
    ModemType type() const override;
    int bitsPerSecond() const override;

private:
    Config config_;

    // Modulation state: phase-continuous oscillator
    float phase_;

    // Samples per symbol (bit)
    int samplesPerBit() const;

    // Generate preamble pattern (alternating 0/1)
    std::vector<uint8_t> generatePreamble() const;

    // Generate postamble pattern (all zeros)
    std::vector<uint8_t> generatePostamble() const;

    // Generate tone for a single bit, appending to output
    void generateTone(std::vector<float>& output, uint8_t bit);

    // Goertzel algorithm: compute energy at target frequency
    // over samples[start .. start+len-1]
    float goertzel(const std::vector<float>& samples,
                   size_t start, size_t len, float targetFreq) const;

    // Find preamble sync point in demodulated bit stream.
    // Returns the bit index where data starts, or -1 if not found.
    // If energies is non-null, only windows whose (mark+space) energy
    // exceeds a small threshold count towards the match score: silence
    // and weak noise can randomly reproduce the alternating pattern, but
    // their Goertzel energy is ~0, while a real preamble sits on strong
    // signal. If outScore is non-null, receives the raw preamble match
    // score of the winning offset (used by demodulate's symbol phase
    // search to pick the best sample alignment).
    int findSync(const std::vector<uint8_t>& rawBits,
                 const std::vector<float>* energies = nullptr,
                 int* outScore = nullptr) const;
};

} // namespace audiot
#endif // AUDIOT_AFSK_MODEM_H
