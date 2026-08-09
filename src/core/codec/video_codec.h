// video_codec.h - Basic video frame packaging for audio transmission
#ifndef AUDIOT_VIDEO_CODEC_H
#define AUDIOT_VIDEO_CODEC_H

#include <vector>
#include <cstdint>
#include <string>

namespace audiot {

// Basic frame extraction / packaging interface for video-over-audio.
// Each video frame (e.g. one still image) is wrapped into a transmission
// frame with sequence information so the receiver can reassemble the clip.
//
// Frame payload layout (inside the FileCodec data field):
//   [VIDEO_MAGIC "AVID"(4)] [FRAME_INDEX u32] [TOTAL_FRAMES u32]
//   [FRAME_DATA ...]
class VideoCodec {
public:
    // Decoded video frame
    struct DecodedFrame {
        bool valid;
        uint32_t frameIndex;   // 0-based position in the clip
        uint32_t totalFrames;  // Total number of frames in the clip
        std::vector<uint8_t> data;
        std::string filename;
        DecodedFrame() : valid(false), frameIndex(0), totalFrames(0) {}
    };

    // Wrap one video frame (raw bytes of a still image) into a
    // transmission frame.
    static std::vector<uint8_t> encodeFrame(const std::string& filename,
                                            const std::vector<uint8_t>& frameData,
                                            uint32_t frameIndex,
                                            uint32_t totalFrames);

    // Extract a video frame from a transmission frame
    static DecodedFrame decodeFrame(const std::vector<uint8_t>& frame);

    // Assemble ordered frame payloads into a single byte stream.
    // Missing frames are skipped; output order follows frameIndex.
    static std::vector<uint8_t> assembleClip(
        const std::vector<DecodedFrame>& frames);

private:
    static const uint8_t MAGIC[4]; // "AVID"
};

} // namespace audiot
#endif // AUDIOT_VIDEO_CODEC_H
