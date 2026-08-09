// video_codec.cpp - Basic video frame packaging implementation
#include "video_codec.h"
#include "file_codec.h"

#include <algorithm>
#include <cstring>

namespace audiot {

const uint8_t VideoCodec::MAGIC[4] = { 'A', 'V', 'I', 'D' };

// Helper: append a big-endian uint32_t
static void appendU32(std::vector<uint8_t>& buf, uint32_t val) {
    buf.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>(val & 0xFF));
}

// Helper: read a big-endian uint32_t
static uint32_t readU32BE(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}

std::vector<uint8_t> VideoCodec::encodeFrame(const std::string& filename,
                                             const std::vector<uint8_t>& frameData,
                                             uint32_t frameIndex,
                                             uint32_t totalFrames) {
    std::vector<uint8_t> payload;
    if (totalFrames == 0 || frameIndex >= totalFrames) {
        return payload;
    }

    payload.insert(payload.end(), MAGIC, MAGIC + 4);
    appendU32(payload, frameIndex);
    appendU32(payload, totalFrames);
    payload.insert(payload.end(), frameData.begin(), frameData.end());

    return FileCodec::encodeData(filename, payload, FileCodec::FileType::VIDEO);
}

VideoCodec::DecodedFrame VideoCodec::decodeFrame(
    const std::vector<uint8_t>& frame) {
    DecodedFrame result;

    const FileCodec::DecodedFile file = FileCodec::decodeFrame(frame);
    if (!file.valid || file.type != FileCodec::FileType::VIDEO) {
        return result;
    }
    result.filename = file.filename;

    const std::vector<uint8_t>& d = file.data;
    // Minimum: magic(4) + index(4) + total(4) = 12
    if (d.size() < 12 || std::memcmp(d.data(), MAGIC, 4) != 0) {
        return result;
    }

    const uint32_t index = readU32BE(&d[4]);
    const uint32_t total = readU32BE(&d[8]);
    if (total == 0 || index >= total) {
        return result;
    }

    result.valid = true;
    result.frameIndex = index;
    result.totalFrames = total;
    result.data.assign(d.begin() + 12, d.end());
    return result;
}

std::vector<uint8_t> VideoCodec::assembleClip(
    const std::vector<DecodedFrame>& frames) {
    // Order by frame index, skipping invalid/duplicate entries
    std::vector<const DecodedFrame*> ordered;
    for (size_t i = 0; i < frames.size(); ++i) {
        if (!frames[i].valid) continue;
        bool duplicate = false;
        for (size_t j = 0; j < ordered.size(); ++j) {
            if (ordered[j]->frameIndex == frames[i].frameIndex) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) ordered.push_back(&frames[i]);
    }

    std::sort(ordered.begin(), ordered.end(),
              [](const DecodedFrame* a, const DecodedFrame* b) {
                  return a->frameIndex < b->frameIndex;
              });

    std::vector<uint8_t> clip;
    size_t totalBytes = 0;
    for (size_t i = 0; i < ordered.size(); ++i) {
        totalBytes += ordered[i]->data.size();
    }
    clip.reserve(totalBytes);
    for (size_t i = 0; i < ordered.size(); ++i) {
        clip.insert(clip.end(), ordered[i]->data.begin(),
                    ordered[i]->data.end());
    }
    return clip;
}

} // namespace audiot
