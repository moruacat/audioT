#ifndef AUDIOT_SSTV_PROTOCOLS_H
#define AUDIOT_SSTV_PROTOCOLS_H
#include <cstdint>
#include <string>

namespace audiot {
enum class SSTVMode { Robot36, Robot72, ScottieS1, ScottieS2, ScottieDX, MartinM1, MartinM2, PD50, PD120 };

struct SSTVProtocolInfo {
    SSTVMode mode; uint8_t visCode; std::string name;
    int width; int height; float lineTimeMs; float syncDurMs;
    float syncFreq; float blackFreq; float whiteFreq;
    bool usesYCrCb; int colorComponents;
};

const SSTVProtocolInfo& getProtocolInfo(SSTVMode mode);
SSTVMode modeFromVISCode(uint8_t code);
} // namespace audiot
#endif
