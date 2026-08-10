// file_codec.cpp - Generic file encoder/decoder implementation
#include "file_codec.h"
#include "util/file_utils.h"
#include <cstring>

namespace audiot {

// Out-of-class definitions for static const members (required for ODR-use in C++14)
const uint8_t FileCodec::SYNC_HIGH;
const uint8_t FileCodec::SYNC_LOW;

// CRC-32 lookup table (standard polynomial 0xEDB88320)
static uint32_t crc32_table[256];
static bool crc32_table_ready = false;

static void buildCrc32Table() {
    if (crc32_table_ready) return;
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t crc = i;
        for (int j = 0; j < 8; ++j) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320u;
            else
                crc >>= 1;
        }
        crc32_table[i] = crc;
    }
    crc32_table_ready = true;
}

uint32_t FileCodec::crc32(const uint8_t* data, size_t len) {
    buildCrc32Table();
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        uint8_t idx = static_cast<uint8_t>((crc ^ data[i]) & 0xFF);
        crc = (crc >> 8) ^ crc32_table[idx];
    }
    return crc ^ 0xFFFFFFFFu;
}

// Helper: write a big-endian uint16_t
static void writeU16(std::vector<uint8_t>& buf, uint16_t val) {
    buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>(val & 0xFF));
}

// Helper: write a big-endian uint32_t
static void writeU32(std::vector<uint8_t>& buf, uint32_t val) {
    buf.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>(val & 0xFF));
}

// Helper: read a big-endian uint16_t
static uint16_t readU16(const uint8_t* p) {
    return (static_cast<uint16_t>(p[0]) << 8) | static_cast<uint16_t>(p[1]);
}

// Helper: read a big-endian uint32_t
static uint32_t readU32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}

std::vector<uint8_t> FileCodec::encodeData(const std::string& filename,
                                            const std::vector<uint8_t>& data,
                                            FileType type) {
    std::vector<uint8_t> frame;

    // Sync bytes
    frame.push_back(SYNC_HIGH);
    frame.push_back(SYNC_LOW);

    // Type
    frame.push_back(static_cast<uint8_t>(type));

    // Flags (reserved, set to 0)
    frame.push_back(0x00);

    // Filename length + filename
    uint16_t nameLen = static_cast<uint16_t>(filename.size());
    writeU16(frame, nameLen);
    for (size_t i = 0; i < filename.size(); ++i) {
        frame.push_back(static_cast<uint8_t>(filename[i]));
    }

    // Data length + data
    uint32_t dataLen = static_cast<uint32_t>(data.size());
    writeU32(frame, dataLen);
    frame.insert(frame.end(), data.begin(), data.end());

    // CRC-32 over everything before CRC
    uint32_t crc = crc32(frame.data(), frame.size());
    writeU32(frame, crc);

    return frame;
}

std::vector<uint8_t> FileCodec::encodeFile(const std::string& filePath) {
    // Read entire file (Unicode-safe: Chinese/non-ASCII paths work)
    FILE* f = openFile(filePath, "rb");
    if (!f) {
        return std::vector<uint8_t>(); // Empty on failure
    }

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return std::vector<uint8_t>(); }
    const long fileSize = ftell(f);
    if (fileSize < 0) { fclose(f); return std::vector<uint8_t>(); }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return std::vector<uint8_t>(); }

    std::vector<uint8_t> data(static_cast<size_t>(fileSize));
    if (fileSize > 0) {
        const size_t got = fread(data.data(), 1, static_cast<size_t>(fileSize), f);
        data.resize(got);
    }
    fclose(f);

    // Extract filename from path
    std::string filename = filePath;
    size_t lastSlash = filePath.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        filename = filePath.substr(lastSlash + 1);
    }

    return encodeData(filename, data);
}

FileCodec::DecodedFile FileCodec::decodeFrame(const std::vector<uint8_t>& frame) {
    DecodedFile result;
    result.valid = false;
    result.type = FileType::GENERIC;

    // Minimum frame size: sync(2) + type(1) + flags(1) + nameLen(2) + dataLen(4) + crc(4) = 14
    if (frame.size() < 14) return result;

    // Verify sync
    if (frame[0] != SYNC_HIGH || frame[1] != SYNC_LOW) return result;

    size_t pos = 2;

    // Type
    result.type = static_cast<FileType>(frame[pos++]);

    // Flags (skip)
    pos++;

    // Filename
    uint16_t nameLen = readU16(&frame[pos]);
    pos += 2;
    if (pos + nameLen > frame.size()) return result;
    result.filename.assign(reinterpret_cast<const char*>(&frame[pos]), nameLen);
    pos += nameLen;

    // Data
    if (pos + 4 > frame.size()) return result;
    uint32_t dataLen = readU32(&frame[pos]);
    pos += 4;
    if (pos + dataLen > frame.size()) return result;
    result.data.assign(frame.begin() + pos, frame.begin() + pos + dataLen);
    pos += dataLen;

    // CRC verification
    if (pos + 4 > frame.size()) return result;
    uint32_t receivedCrc = readU32(&frame[pos]);
    uint32_t computedCrc = crc32(frame.data(), pos);
    if (receivedCrc != computedCrc) return result;

    result.valid = true;
    return result;
}

std::vector<std::vector<uint8_t>> FileCodec::encodeFileChunked(
    const std::string& filePath, size_t chunkSize) {

    std::vector<std::vector<uint8_t>> frames;

    // Read entire file (Unicode-safe: Chinese/non-ASCII paths work)
    FILE* f = openFile(filePath, "rb");
    if (!f) return frames;

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return frames; }
    const long fileSize = ftell(f);
    if (fileSize < 0) { fclose(f); return frames; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return frames; }

    std::vector<uint8_t> data(static_cast<size_t>(fileSize));
    if (fileSize > 0) {
        const size_t got = fread(data.data(), 1, static_cast<size_t>(fileSize), f);
        data.resize(got);
    }
    fclose(f);

    // Extract filename
    std::string filename = filePath;
    size_t lastSlash = filePath.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        filename = filePath.substr(lastSlash + 1);
    }

    // Split into chunks
    size_t offset = 0;
    int chunkIndex = 0;
    while (offset < data.size()) {
        size_t len = std::min(chunkSize, data.size() - offset);
        std::vector<uint8_t> chunkData(data.begin() + offset, data.begin() + offset + len);

        // Append chunk index to filename
        std::string chunkName = filename + ".part" + std::to_string(chunkIndex);
        frames.push_back(encodeData(chunkName, chunkData));

        offset += len;
        ++chunkIndex;
    }

    // Handle empty file
    if (data.empty()) {
        frames.push_back(encodeData(filename, data));
    }

    return frames;
}

} // namespace audiot
