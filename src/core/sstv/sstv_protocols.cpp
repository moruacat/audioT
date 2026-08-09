// sstv_protocols.cpp - SSTV protocol table and VIS code lookup
#include "sstv_protocols.h"

namespace audiot {

// Protocol parameter table (standard SSTV values)
// Fields: mode, visCode, name, width, height,
//         lineTimeMs, syncDurMs, syncFreq, blackFreq, whiteFreq,
//         usesYCrCb, colorComponents
static const SSTVProtocolInfo kProtocolTable[] = {
    { SSTVMode::Robot36,   8,  "Robot 36",    320, 240,  88.000f,  9.000f, 1200.0f, 1500.0f, 2300.0f, true,  3 },
    { SSTVMode::Robot72,   12, "Robot 72",    320, 240, 138.000f,  9.000f, 1200.0f, 1500.0f, 2300.0f, true,  3 },
    { SSTVMode::ScottieS1, 60, "Scottie S1",  320, 256, 138.240f,  9.000f, 1200.0f, 1500.0f, 2300.0f, false, 3 },
    { SSTVMode::ScottieS2, 56, "Scottie S2",  320, 256,  88.064f,  9.000f, 1200.0f, 1500.0f, 2300.0f, false, 3 },
    { SSTVMode::ScottieDX, 76, "Scottie DX",  320, 256, 263.816f,  9.000f, 1200.0f, 1500.0f, 2300.0f, false, 3 },
    { SSTVMode::MartinM1,  44, "Martin M1",   320, 256, 146.432f,  4.862f, 1200.0f, 1500.0f, 2300.0f, false, 3 },
    { SSTVMode::MartinM2,  40, "Martin M2",   320, 256,  73.216f,  4.862f, 1200.0f, 1500.0f, 2300.0f, false, 3 },
    { SSTVMode::PD50,      82, "PD50",        640, 496,  63.500f, 20.800f, 1200.0f, 1500.0f, 2300.0f, true,  3 },
    { SSTVMode::PD120,     97, "PD120",       640, 496, 121.600f, 20.800f, 1200.0f, 1500.0f, 2300.0f, true,  3 },
};

static const size_t kProtocolCount = sizeof(kProtocolTable) / sizeof(kProtocolTable[0]);

const SSTVProtocolInfo& getProtocolInfo(SSTVMode mode) {
    for (size_t i = 0; i < kProtocolCount; ++i) {
        if (kProtocolTable[i].mode == mode) {
            return kProtocolTable[i];
        }
    }
    // Fallback: Martin M1
    return kProtocolTable[5];
}

SSTVMode modeFromVISCode(uint8_t code) {
    for (size_t i = 0; i < kProtocolCount; ++i) {
        if (kProtocolTable[i].visCode == code) {
            return kProtocolTable[i].mode;
        }
    }
    // Unknown VIS code: default to Martin M1
    return SSTVMode::MartinM1;
}

} // namespace audiot
