// tx_pipeline.h - Transmit pipeline: text -> encode -> FEC -> modulate -> audio
#ifndef AUDIOT_TX_PIPELINE_H
#define AUDIOT_TX_PIPELINE_H

#include <vector>
#include <string>
#include <cstdint>
#include "pipeline/pipeline_config.h"

namespace audiot {

class TxPipeline {
public:
    explicit TxPipeline(const PipelineConfig& config = PipelineConfig());

    // Encode text to audio samples
    std::vector<float> encode(const std::string& text);

    // Encode an already-framed byte sequence (FEC + AFSK modulate),
    // used e.g. for a single FileCodec frame.
    std::vector<float> encodeFrame(const std::vector<uint8_t>& framed);

    // Encode multiple framed byte sequences as ONE continuous audio
    // burst (single preamble/postamble, frames back-to-back). The type
    // marker frame must be transmitted this way, directly followed by
    // the data frame; separate modulate() calls would insert an extra
    // preamble/postamble between them which the demodulator would treat
    // as the end of the stream.
    std::vector<float> encodeFrames(
        const std::vector<std::vector<uint8_t>>& frames);

    // Encode text with the 'T' type marker frame in front.
    std::vector<float> encodeTypedText(const std::string& text);

    // Encode a file (FileCodec frame: name + type + CRC-32) with the
    // 'F' type marker frame in front.
    std::vector<float> encodeTypedFile(const std::string& filePath);

    // Encode text and write directly to WAV file
    bool encodeToFile(const std::string& text, const std::string& wavPath);

    // Write already-modulated samples to a 16-bit mono WAV file
    // (avoids re-encoding when the samples are also played in real time).
    bool writeSamplesToWav(const std::vector<float>& samples,
                           const std::string& wavPath);

    // Generate a continuous sine tone (pure DSP, no Qt dependency).
    // Used e.g. for the pre-transmission prep tone. Returns empty vector
    // for non-positive frequency/duration/sampleRate.
    static std::vector<float> makeTone(float freqHz, float seconds,
                                       int sampleRate, float amplitude = 0.5f);

    // Generate the prep tone truncated to a whole number of AFSK symbol
    // periods (sampleRate/baudRate samples each). Without this, the
    // modulated burst that follows the prep tone starts mid-symbol on
    // the demodulator's fixed sample grid (e.g. 44100/1200 = 36.75 -> 36
    // samples), shifting every subsequent bit and breaking preamble
    // sync. Duration shrinks by at most one symbol (~0.03 s at 1200 bd).
    static std::vector<float> makePrepAligned(float freqHz, float seconds,
                                              int sampleRate, int baudRate,
                                              float amplitude = 0.5f);

    // Generate a block of digital silence (all-zero samples, pure DSP,
    // no Qt dependency). Used as the end-of-frame control marker
    // appended after the modulated data. Returns empty vector for
    // non-positive duration/sampleRate.
    static std::vector<float> makeSilence(float seconds, int sampleRate);

    // Get the configuration
    const PipelineConfig& config() const { return config_; }

private:
    // Convert bytes to bits, apply optional FEC, then AFSK modulate
    std::vector<float> modulateBits(const std::vector<uint8_t>& bits);

    // Reed-Solomon byte-level encoding of a framed byte stream:
    // [sync AA 55][len 4B big-endian][blocks], each block = 239 data
    // bytes (last one zero-padded) + 16 parity bytes (t=8). The sync
    // header lets the receiver locate the stream after noise, the
    // 4-byte length lets it drop the zero padding. Returns empty on
    // empty input.
    static std::vector<uint8_t> rsEncode(const std::vector<uint8_t>& data);

    PipelineConfig config_;
};

} // namespace audiot

#endif // AUDIOT_TX_PIPELINE_H
