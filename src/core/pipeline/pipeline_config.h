// pipeline_config.h - Configuration for TX/RX pipelines
#ifndef AUDIOT_PIPELINE_CONFIG_H
#define AUDIOT_PIPELINE_CONFIG_H

#include "interfaces/IModem.h"

namespace audiot {

// Forward error correction scheme applied between framing and
// modulation. NONE keeps raw bits, HAMMING corrects 1 bit per 7-bit
// code word, REED_SOLOMON corrects up to 8 bytes per 239-byte block
// (strongest; best suited to noisy acoustic channels).
enum class FecType {
    NONE = 0,
    HAMMING = 1,
    REED_SOLOMON = 2
};

struct PipelineConfig {
    int sampleRate = 44100;
    ModemType modemType = ModemType::AFSK;

    // FEC scheme used by the TX/RX pipelines. This is the value shown
    // in the settings dialog FEC combo (0 = None, 1 = Hamming,
    // 2 = Reed-Solomon).
    FecType fecType = FecType::HAMMING;

    // AFSK configuration
    float markFreq = 1200.0f;
    float spaceFreq = 2200.0f;
    int baudRate = 1200;

    // Duration (seconds) of the continuous prep tone played before the
    // modulated data, giving the receiving device time to prepare
    // (modem-handshake style). Configurable in code; the settings dialog
    // intentionally has no UI control for it and keeps this default.
    // 1 秒即可满足接收端定位门限（0.5s 连续 mark run），长鸣太久影响
    // 使用体验。
    float prepToneSeconds = 1.0f;

    // Duration (seconds) of the silent interval appended after the
    // modulated data (end-of-frame control marker). The trailing silence
    // lets the receiving side unambiguously detect the end of a frame.
    float endControlSeconds = 1.0f;
};

} // namespace audiot

#endif // AUDIOT_PIPELINE_CONFIG_H
