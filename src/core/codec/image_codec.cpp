// image_codec.cpp - Image frame codec implementation
#include "image_codec.h"
#include "file_codec.h"
#include "util/file_utils.h"

#include <cstring>

namespace audiot {

const uint8_t ImageCodec::MAGIC[4] = { 'A', 'I', 'M', 'G' };

std::vector<uint8_t> ImageCodec::encodeImage(const std::string& filename,
                                             const uint8_t* pixels,
                                             int width, int height,
                                             PixelFormat format) {
    std::vector<uint8_t> payload;
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return payload;
    }

    // Header: magic + width(u16 BE) + height(u16 BE) + format(u8)
    payload.insert(payload.end(), MAGIC, MAGIC + 4);
    payload.push_back(static_cast<uint8_t>((width >> 8) & 0xFF));
    payload.push_back(static_cast<uint8_t>(width & 0xFF));
    payload.push_back(static_cast<uint8_t>((height >> 8) & 0xFF));
    payload.push_back(static_cast<uint8_t>(height & 0xFF));
    payload.push_back(static_cast<uint8_t>(format));

    // Pixel data
    const int bpp = (format == PixelFormat::RGB24) ? 3 : 3;
    const size_t dataSize =
        static_cast<size_t>(width) * static_cast<size_t>(height) * bpp;
    payload.insert(payload.end(), pixels, pixels + dataSize);

    return FileCodec::encodeData(filename, payload, FileCodec::FileType::IMAGE);
}

std::vector<uint8_t> ImageCodec::encodeFile(const std::string& filePath) {
    // Read the raw file bytes and wrap them as an IMAGE-typed frame.
    // Unicode-safe: Chinese/non-ASCII paths work.
    FILE* f = openFile(filePath, "rb");
    if (!f) {
        return std::vector<uint8_t>();
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

    std::string filename = filePath;
    const size_t lastSlash = filePath.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        filename = filePath.substr(lastSlash + 1);
    }

    return FileCodec::encodeData(filename, data, FileCodec::FileType::IMAGE);
}

ImageCodec::DecodedImage ImageCodec::decodeFrame(
    const std::vector<uint8_t>& frame) {
    DecodedImage result;

    const FileCodec::DecodedFile file = FileCodec::decodeFrame(frame);
    if (!file.valid || file.type != FileCodec::FileType::IMAGE) {
        return result;
    }
    result.filename = file.filename;

    const std::vector<uint8_t>& d = file.data;
    // Minimum: magic(4) + width(2) + height(2) + format(1) = 9
    if (d.size() < 9 || std::memcmp(d.data(), MAGIC, 4) != 0) {
        return result;
    }

    const int width = (static_cast<int>(d[4]) << 8) | d[5];
    const int height = (static_cast<int>(d[6]) << 8) | d[7];
    const uint8_t fmt = d[8];
    if (width <= 0 || height <= 0 || fmt != 0) {
        return result;
    }

    const size_t expected =
        static_cast<size_t>(width) * static_cast<size_t>(height) * 3;
    if (d.size() < 9 + expected) {
        return result;
    }

    result.valid = true;
    result.width = width;
    result.height = height;
    result.format = PixelFormat::RGB24;
    result.pixels.assign(d.begin() + 9, d.begin() + 9 + expected);
    return result;
}

} // namespace audiot
