// text_codec.cpp - Text codec implementation with CRC-16 and sync header
#include "text_codec.h"

namespace audiot {

// Sync header bytes
static const uint8_t SYNC_HI = 0xAA;
static const uint8_t SYNC_LO = 0x55;

// Frame header size: sync(2) + length(2) = 4 bytes
static const size_t HEADER_SIZE = 4;
// Frame footer size: CRC16(2) = 2 bytes
static const size_t FOOTER_SIZE = 2;

uint16_t TextCodec::crc16(const uint8_t* data, size_t len) {
    // CRC-16/CCITT (polynomial 0x1021, init 0xFFFF)
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

std::vector<uint8_t> TextCodec::encode(const std::string& text) {
    std::vector<uint8_t> frame;

    uint16_t dataLen = static_cast<uint16_t>(text.size());

    // Reserve space: header(4) + data + footer(2)
    frame.reserve(HEADER_SIZE + dataLen + FOOTER_SIZE);

    // Sync header
    frame.push_back(SYNC_HI);
    frame.push_back(SYNC_LO);

    // Data length (big-endian)
    frame.push_back(static_cast<uint8_t>((dataLen >> 8) & 0xFF));
    frame.push_back(static_cast<uint8_t>(dataLen & 0xFF));

    // Data payload
    for (size_t i = 0; i < text.size(); ++i) {
        frame.push_back(static_cast<uint8_t>(text[i]));
    }

    // CRC-16 over data payload
    uint16_t crc = crc16(reinterpret_cast<const uint8_t*>(text.data()), text.size());
    frame.push_back(static_cast<uint8_t>((crc >> 8) & 0xFF));
    frame.push_back(static_cast<uint8_t>(crc & 0xFF));

    return frame;
}

std::string TextCodec::decode(const std::vector<uint8_t>& data) {
    return decodeAt(data).payload;
}

TextCodec::FrameInfo TextCodec::decodeAt(const std::vector<uint8_t>& data,
                                         size_t offset) {
    FrameInfo result;
    if (data.size() < HEADER_SIZE + FOOTER_SIZE) {
        return result;
    }

    // Find sync header starting at offset, skipping noise bytes
    size_t syncPos = data.size();
    for (size_t i = offset; i + 1 < data.size(); ++i) {
        if (data[i] == SYNC_HI && data[i + 1] == SYNC_LO) {
            syncPos = i;
            break;
        }
    }

    if (syncPos + HEADER_SIZE > data.size()) return result;

    // Read data length
    uint16_t dataLen = (static_cast<uint16_t>(data[syncPos + 2]) << 8)
                     | static_cast<uint16_t>(data[syncPos + 3]);

    // Verify total frame size
    size_t frameEnd = syncPos + HEADER_SIZE + dataLen + FOOTER_SIZE;
    if (frameEnd > data.size()) return result;

    // Verify CRC over the payload
    const uint8_t* payload = &data[syncPos + HEADER_SIZE];
    uint16_t computedCrc = crc16(payload, dataLen);
    uint16_t receivedCrc = (static_cast<uint16_t>(data[frameEnd - 2]) << 8)
                         | static_cast<uint16_t>(data[frameEnd - 1]);
    if (computedCrc != receivedCrc) {
        return result; // CRC mismatch
    }

    result.valid = true;
    result.payload.assign(reinterpret_cast<const char*>(payload), dataLen);
    result.frameEnd = frameEnd;
    return result;
}

} // namespace audiot
