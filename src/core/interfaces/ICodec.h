#ifndef AUDIOT_ICODEC_H
#define AUDIOT_ICODEC_H

#include <vector>
#include <cstdint>
#include <string>

namespace audiot {

// Codec type enumeration
enum class CodecType {
    TEXT,       // Plain text encoding
    BINARY,     // Raw binary passthrough
    COMPRESSED  // Compressed encoding (e.g. zlib)
};

// Codec abstract interface: encodes raw data for transmission and decodes received data
class ICodec {
public:
    virtual ~ICodec() = default;

    // Encode: convert raw data into transmission bytes (adds framing, checksum, etc.)
    virtual std::vector<uint8_t> encode(const std::vector<uint8_t>& data) = 0;

    // Decode: convert received transmission bytes back to original data
    virtual std::vector<uint8_t> decode(const std::vector<uint8_t>& data) = 0;

    // Get codec type
    virtual CodecType codecType() const = 0;

    // Get codec name for display/logging
    virtual std::string name() const = 0;
};

} // namespace audiot
#endif
