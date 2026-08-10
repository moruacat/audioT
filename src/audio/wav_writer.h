// wav_writer.h - WAV file writer (PCM 16/24-bit)
// Uses the Unicode-safe openFile() helper so Chinese/non-ASCII paths
// work on Windows (std::ofstream would fail on UTF-8 paths there).
#ifndef AUDIOT_WAV_WRITER_H
#define AUDIOT_WAV_WRITER_H

#include <string>
#include <vector>
#include <cstdint>
#include <cstdio>
#include "audio_format.h"

namespace audiot {

class WavWriter {
public:
    WavWriter();
    ~WavWriter();

    // Open a WAV file for writing with given format (UTF-8 path accepted)
    bool open(const std::string& filename, const AudioFormat& format);

    // Close the file (writes final header)
    void close();

    // Write float samples [-1.0, 1.0] as PCM
    void write(const std::vector<float>& samples);

    // Write float samples from raw pointer
    void write(const float* data, size_t count);

    // Check if file is open
    bool isOpen() const;

private:
    // Write little-endian values
    void writeU16LE(uint16_t val);
    void writeU32LE(uint32_t val);

    // Write WAV header (called at close time with final sizes)
    void writeHeader();

    // Write placeholder header at open time
    void writePlaceholderHeader();

    FILE* file_;
    AudioFormat format_;
    size_t dataBytesWritten_;
    bool isOpen_;
};

} // namespace audiot
#endif // AUDIOT_WAV_WRITER_H
