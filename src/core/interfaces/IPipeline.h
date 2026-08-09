#ifndef AUDIOT_IPIPELINE_H
#define AUDIOT_IPIPELINE_H

#include <vector>
#include <cstdint>
#include <memory>

namespace audiot {

// Forward declarations
class ICodec;
class IModem;

// FEC (Forward Error Correction) abstract interface
class IFEC {
public:
    virtual ~IFEC() = default;

    // Encode data with FEC redundancy
    virtual std::vector<uint8_t> encode(const std::vector<uint8_t>& data) = 0;

    // Decode data and correct errors using FEC
    virtual std::vector<uint8_t> decode(const std::vector<uint8_t>& data) = 0;
};

// Pipeline abstract interface: combines ICodec + IFEC + IModem into a complete
// encode/modulate or demodulate/decode chain
class IPipeline {
public:
    virtual ~IPipeline() = default;

    // Transmit path: raw data -> codec encode -> FEC encode -> modem modulate -> audio
    virtual std::vector<float> process(const std::vector<uint8_t>& data) = 0;

    // Receive path: audio -> modem demodulate -> FEC decode -> codec decode -> raw data
    virtual std::vector<uint8_t> processReverse(const std::vector<float>& samples) = 0;

    // Set the codec used in the pipeline
    virtual void setCodec(std::shared_ptr<ICodec> codec) = 0;

    // Set the FEC used in the pipeline
    virtual void setFEC(std::shared_ptr<IFEC> fec) = 0;

    // Set the modem used in the pipeline
    virtual void setModem(std::shared_ptr<IModem> modem) = 0;
};

} // namespace audiot
#endif
