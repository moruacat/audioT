// rx_pipeline.h - Receive pipeline: audio -> demodulate -> FEC -> decode -> text
#ifndef AUDIOT_RX_PIPELINE_H
#define AUDIOT_RX_PIPELINE_H

#include <vector>
#include <string>
#include <cstdint>
#include "pipeline/pipeline_config.h"

namespace audiot {

// Result of decoding received audio: the type marker frame ('T'/'F')
// transmitted right after the prep tone decides whether the payload is
// text or a file.
struct DecodeResult {
    enum Type {
        NONE = 0,   // nothing decodable found
        TEXT,       // payload is text
        FILE        // payload is a file (filename + raw data)
    };

    Type type = NONE;
    std::string text;               // valid for TEXT
    std::string filename;           // valid for FILE
    std::vector<uint8_t> fileData;  // valid for FILE

    bool valid() const { return type != NONE; }
};

class RxPipeline {
public:
    explicit RxPipeline(const PipelineConfig& config = PipelineConfig());

    // Decode audio samples. First frame in the stream is expected to be
    // the type marker ('T' text / 'F' file); a stream without a type
    // marker is treated as legacy text-only format.
    //
    // With autoFec=true the demodulated bits are tried against the
    // configured FEC first, then against every other scheme (NONE /
    // HAMMING / REED_SOLOMON) until one validates. This keeps old
    // files (e.g. exported with a different FEC setting) decodable
    // regardless of the current configuration. Demodulation runs once,
    // only the cheap FEC/frame stages are retried.
    DecodeResult decode(const std::vector<float>& samples, bool autoFec = false);

    // Read WAV file and decode to text/file (autoFec enabled).
    DecodeResult decodeFromFile(const std::string& wavPath);

    // Get the configuration
    const PipelineConfig& config() const { return config_; }

private:
    // FEC-decode demodulated bits and parse the framed byte stream with
    // the given scheme; returns invalid result on any failure.
    DecodeResult decodeWithFec(const std::vector<uint8_t>& fecBits,
                               FecType fecType) const;

    // Shared FEC stage used by both AFSK and M-FSK demodulation: decode
    // the demodulated bit stream under autoFec (try configured scheme,
    // then the others) and return the parsed result.
    DecodeResult continueFec(const std::vector<uint8_t>& fecBits,
                             bool autoFec) const;

    // Reed-Solomon byte-level decoding of an RS stream produced by
    // TxPipeline::rsEncode: locate the AA 55 sync header, read the
    // 4-byte length, decode each 255-byte block and strip the zero
    // padding. Returns empty on any failure.
    static std::vector<uint8_t> rsDecode(const std::vector<uint8_t>& bytes);

    PipelineConfig config_;
};

} // namespace audiot

#endif // AUDIOT_RX_PIPELINE_H
