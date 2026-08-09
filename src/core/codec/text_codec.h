// text_codec.h - Text encoder/decoder with frame structure and CRC
#ifndef AUDIOT_TEXT_CODEC_H
#define AUDIOT_TEXT_CODEC_H

#include <string>
#include <vector>
#include <cstdint>

namespace audiot {

// Text codec with framed transmission format
// Frame structure: [0xAA][0x55][LEN_HI][LEN_LO][DATA...][CRC16_HI][CRC16_LO]
class TextCodec {
public:
    // Encode text string into framed byte sequence for transmission
    static std::vector<uint8_t> encode(const std::string& text);

    // Decode framed byte sequence back to text string
    // Returns empty string if frame is invalid or CRC fails
    static std::string decode(const std::vector<uint8_t>& data);

    // Result of locating/decoding one frame inside a byte stream
    struct FrameInfo {
        bool valid = false;      // frame found and CRC verified
        std::string payload;     // decoded frame payload
        size_t frameEnd = 0;     // index one past the frame's last byte
    };

    // Search for the next valid frame starting at offset (skips noise /
    // garbage bytes before the sync header) and report where it ends.
    // Needed by the receive pipeline to find the type marker frame and
    // then continue decoding the data frame right after it.
    static FrameInfo decodeAt(const std::vector<uint8_t>& data,
                              size_t offset = 0);

private:
    // Compute CRC-16/CCITT over data
    static uint16_t crc16(const uint8_t* data, size_t len);
};

} // namespace audiot
#endif // AUDIOT_TEXT_CODEC_H
