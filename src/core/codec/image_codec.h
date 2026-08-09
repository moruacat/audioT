// image_codec.h - Image frame codec for audio transmission
#ifndef AUDIOT_IMAGE_CODEC_H
#define AUDIOT_IMAGE_CODEC_H

#include <vector>
#include <cstdint>
#include <string>

namespace audiot {

// Wraps image data into transmission frames (built on FileCodec).
//
// Frame payload layout (inside the FileCodec data field):
//   [IMG_MAGIC "AIMG"(4)] [WIDTH u16] [HEIGHT u16] [FORMAT u8]
//   [PIXEL_DATA ...]
// FORMAT: 0 = RGB24 (3 bytes/pixel)
class ImageCodec {
public:
    enum class PixelFormat : uint8_t {
        RGB24 = 0
    };

    // Decoded image result
    struct DecodedImage {
        bool valid;
        int width;
        int height;
        PixelFormat format;
        std::vector<uint8_t> pixels;  // raw pixel data
        std::string filename;
        DecodedImage() : valid(false), width(0), height(0),
                         format(PixelFormat::RGB24) {}
    };

    // Encode an in-memory RGB image into a transmission frame
    static std::vector<uint8_t> encodeImage(const std::string& filename,
                                            const uint8_t* pixels,
                                            int width, int height,
                                            PixelFormat format = PixelFormat::RGB24);

    // Encode an image file (raw bytes are wrapped as-is, dimensions unknown)
    static std::vector<uint8_t> encodeFile(const std::string& filePath);

    // Decode an image frame produced by encodeImage()
    static DecodedImage decodeFrame(const std::vector<uint8_t>& frame);

private:
    static const uint8_t MAGIC[4]; // "AIMG"
};

} // namespace audiot
#endif // AUDIOT_IMAGE_CODEC_H
