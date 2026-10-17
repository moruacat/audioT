#ifndef AUDIOT_IMODEM_H
#define AUDIOT_IMODEM_H

#include <vector>
#include <cstdint>

namespace audiot {

// Modulation mode enumeration
// 旧的 beta/psk_modem 与 beta/qam_modem 因相干载波同步失败（自环即
// 解不出数据）被移除。这里接入的是重新实现的版本：
//   - PSK：差分相移键控（DBPSK/DQPSK/D8PSK），接收端用非相干差分解调，
//          不需要载波相位恢复，从根源上规避了 Costas 环锁定的问题。
//   - QAM：16-QAM，前导符号兼作导频做信道估计 + 判决导向跟踪。
// 枚举值持久化到 INI（modemType），故新成员追加在末尾，保持旧值不变。
enum class ModemType {
    AFSK,   // Audio Frequency Shift Keying
    MFSK,   // Multi-Tone Frequency Shift Keying (FFT-based, 4-FSK default)
    PSK,    // Differential PSK (DBPSK / DQPSK / D8PSK), non-coherent
    QAM     // 16-QAM, preamble-assisted coherent demodulation
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
