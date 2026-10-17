// file_codec.h - Generic file encoder/decoder for audio transmission
#ifndef AUDIOT_FILE_CODEC_H
#define AUDIOT_FILE_CODEC_H

#include <vector>
#include <cstdint>
#include <string>

namespace audiot {

class FileCodec {
public:
    // Frame structure:
    // [SYNC_H(0xAA)] [SYNC_L(0x55)]
    // [TYPE(1)] [FLAGS(1)] [NAME_LEN(2)] [NAME(N)]
    // [DATA_LEN(4)] [DATA(N)]
    // [CRC32(4)]

    static const uint8_t SYNC_HIGH = 0xAA;
    static const uint8_t SYNC_LOW = 0x55;

    // File type classification
    enum class FileType : uint8_t {
        GENERIC = 0,
        TEXT = 1,
        IMAGE = 2,
        VIDEO = 3
    };

    // Encode: read file and wrap into a transmission frame.
    // type 标记载荷类别（GENERIC / IMAGE / VIDEO ...），接收端据此决定
    // 如何呈现；默认 GENERIC。
    static std::vector<uint8_t> encodeFile(const std::string& filePath,
                                           FileType type = FileType::GENERIC);

    // Encode: wrap in-memory data into a transmission frame
    static std::vector<uint8_t> encodeData(const std::string& filename,
                                            const std::vector<uint8_t>& data,
                                            FileType type = FileType::GENERIC);

    // Decoded file result
    struct DecodedFile {
        std::string filename;
        std::vector<uint8_t> data;
        FileType type;
        bool valid;
    };

    // Decode: extract file data from a transmission frame
    static DecodedFile decodeFrame(const std::vector<uint8_t>& frame);

    // Split large file into multiple chunked frames
    static std::vector<std::vector<uint8_t>> encodeFileChunked(
        const std::string& filePath, size_t chunkSize = 4096);

private:
    // Compute CRC-32 checksum
    static uint32_t crc32(const uint8_t* data, size_t len);
};

} // namespace audiot
#endif // AUDIOT_FILE_CODEC_H
