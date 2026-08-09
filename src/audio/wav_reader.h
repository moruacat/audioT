// wav_reader.h - WAV file reader (PCM 16/24-bit, standard C++ I/O)
#ifndef AUDIOT_WAV_READER_H
#define AUDIOT_WAV_READER_H

#include <string>
#include <vector>
#include <fstream>
#include <cstdint>
#include "audio_format.h"

namespace audiot {

class WavReader {
public:
    WavReader();
    ~WavReader();

    // Open a WAV file and parse its header
    bool open(const std::string& filename);

    // Close the file
    void close();

    // Get audio format info
    AudioFormat format() const;

    // Total number of samples (per channel, i.e. frames)
    size_t totalSamples() const;

    // Read all samples, converted to mono float [-1.0, 1.0]
    std::vector<float> readAll();

    // Read up to 'count' samples (mono float)
    std::vector<float> read(size_t count);

    // Check if file is open
    bool isOpen() const;

private:
    // Read a little-endian value from stream
    bool readU16LE(std::ifstream& f, uint16_t& val);
    bool readU32LE(std::ifstream& f, uint32_t& val);

    // Read raw PCM samples and convert to float
    std::vector<float> readSamples(size_t maxFrames);

    std::ifstream file_;
    AudioFormat format_;
    size_t dataOffset_;     // Byte offset of data chunk
    size_t dataSize_;       // Size of data chunk in bytes
    size_t totalFrames_;    // Total frames (samples per channel)
    size_t framesRead_;     // Frames already read
};

} // namespace audiot
#endif // AUDIOT_WAV_READER_H
