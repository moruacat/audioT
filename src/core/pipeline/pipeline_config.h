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

// M-FSK speed/mass tiers. A single 4-FSK modulator is reused; only the
// symbol rate differs. Faster tiers carry more bits per second but are
// more fragile on noisy acoustic links.
enum class MftskTier {
    ROBUST = 0,   // 50 sym/s  -> ~100 bps,  most resilient
    BALANCED = 1, // 125 sym/s -> ~250 bps, default
    SPORT = 2     // 250 sym/s -> ~500 bps, fastest, use with care
};

struct PipelineConfig {
    int sampleRate = 44100;
    ModemType modemType = ModemType::AFSK;

    // FEC scheme used by the TX/RX pipelines. This is the value shown
    // in the settings dialog FEC combo (0 = None, 1 = Hamming,
    // 2 = Reed-Solomon).
    FecType fecType = FecType::HAMMING;

    // M-FSK tier (used when modemType == MFSK). Overrides the effective
    // symbol rate; see MftskTier. When two stations talk, both should
    // pick the same tier.
    MftskTier mftskTier = MftskTier::BALANCED;

    // Receive-side auto-detection: if true, RxPipeline tries both AFSK
    // and M-FSK demodulation and returns whichever yields a valid frame.
    // Lets a receiver cope with a sender on a different modulation
    // without manual re-configuration. Only affects reception.
    bool autoDetectModem = true;

    // AFSK configuration
    float markFreq = 1200.0f;
    float spaceFreq = 2200.0f;
    int baudRate = 1200;

    // M-FSK configuration (used when modemType == MFSK). Each symbol
    // carries `mftskOrder` bits on one of 1<<mftskOrder orthogonal
    // frequency slots starting at mftskBaseFreq, spaced mftskSlotSpacing
    // apart (orthogonal: slotSpacing == sampleRate / samplesPerSymbol).
    // mftskSymbolRate is the effective symbol rate; the UI picks it via
    // the MftskTier controls (robust/balanced/sport).
    float mftskBaseFreq = 1500.0f;
    float mftskSlotSpacing = 50.0f;
    int mftskOrder = 2;        // bits per symbol (4-FSK)
    int mftskSymbolRate = 125; // symbols per second (balanced baseline)

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

// M-FSK profile for a tier: symbol rate AND orthogonal slot spacing.
// Both must keep the symbol period an exact integer number of samples at
// the 44100 Hz default and keep each slot's carrier on an exact FFT bin
// (slotSpacing == sampleRate / samplesPerSymbol). Faster tiers use a
// larger slot spacing, so fewer slots can fit in the speech band and
// they are more fragile on noisy links.
struct MftskProfile {
    int symbolRate;      // symbols/sec
    float slotSpacing;   // Hz between orthogonal slots
    int bitsPerSymbol;   // log2(slotCount) (kept at 2 for 4-FSK here)
};

// Profile for an M-FSK tier. Shared by TX/RX pipelines and the UI so the
// radio mode and the classic dialogs agree.
inline MftskProfile mftskProfileForTier(MftskTier tier, int sampleRate = 44100) {
    (void)sampleRate;  // profiles below are derived for the default rate
    switch (tier) {
        case MftskTier::ROBUST:   return MftskProfile{50, 50.0f, 2};
        case MftskTier::BALANCED: return MftskProfile{100, 100.0f, 2};
        case MftskTier::SPORT:    return MftskProfile{150, 150.0f, 2};
        default:                  return MftskProfile{100, 100.0f, 2};
    }
}

// Backwards-compatible helper: symbol rate only.
inline int mftskSymbolRateForTier(MftskTier tier) {
    return mftskProfileForTier(tier).symbolRate;
}

} // namespace audiot

#endif // AUDIOT_PIPELINE_CONFIG_H
