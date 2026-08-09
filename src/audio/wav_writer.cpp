// wav_writer.cpp - WAV file writer implementation
#include "wav_writer.h"
#include <cmath>
#include <cstring>

namespace audiot {

WavWriter::WavWriter()
    : dataBytesWritten_(0), isOpen_(false)
{
}

WavWriter::~WavWriter() {
    close();
}

void WavWriter::writeU16LE(uint16_t val) {
    uint8_t buf[2] = {
        static_cast<uint8_t>(val & 0xFF),
        static_cast<uint8_t>((val >> 8) & 0xFF)
    };
    file_.write(reinterpret_cast<const char*>(buf), 2);
}

void WavWriter::writeU32LE(uint32_t val) {
    uint8_t buf[4] = {
        static_cast<uint8_t>(val & 0xFF),
        static_cast<uint8_t>((val >> 8) & 0xFF),
        static_cast<uint8_t>((val >> 16) & 0xFF),
        static_cast<uint8_t>((val >> 24) & 0xFF)
    };
    file_.write(reinterpret_cast<const char*>(buf), 4);
}

bool WavWriter::open(const std::string& filename, const AudioFormat& format) {
    close();

    file_.open(filename, std::ios::binary);
    if (!file_.is_open()) return false;

    format_ = format;
    dataBytesWritten_ = 0;
    isOpen_ = true;

    // Write placeholder header (will be overwritten at close)
    writePlaceholderHeader();
    return true;
}

void WavWriter::writePlaceholderHeader() {
    // Write 44-byte WAV header with zeroed sizes
    // RIFF header
    file_.write("RIFF", 4);
    writeU32LE(0);  // Placeholder for file size - 8
    file_.write("WAVE", 4);

    // fmt chunk
    file_.write("fmt ", 4);
    writeU32LE(16);  // Chunk size
    writeU16LE(1);   // PCM format
    writeU16LE(static_cast<uint16_t>(format_.channels));
    writeU32LE(static_cast<uint32_t>(format_.sampleRate));

    uint32_t byteRate = static_cast<uint32_t>(format_.sampleRate) *
                        static_cast<uint32_t>(format_.bytesPerFrame());
    writeU32LE(byteRate);
    writeU16LE(static_cast<uint16_t>(format_.bytesPerFrame()));
    writeU16LE(static_cast<uint16_t>(format_.bitsPerSample));

    // data chunk header
    file_.write("data", 4);
    writeU32LE(0);  // Placeholder for data size
}

void WavWriter::close() {
    if (!isOpen_) return;

    // Seek back to beginning and rewrite header with correct sizes
    file_.seekp(0);
    writeHeader();

    file_.close();
    isOpen_ = false;
    dataBytesWritten_ = 0;
}

void WavWriter::writeHeader() {
    uint32_t dataSize = static_cast<uint32_t>(dataBytesWritten_);
    uint32_t riffSize = dataSize + 36;  // 36 = 4(WAVE) + 24(fmt chunk) + 8(data header)

    // RIFF header
    file_.write("RIFF", 4);
    writeU32LE(riffSize);
    file_.write("WAVE", 4);

    // fmt chunk
    file_.write("fmt ", 4);
    writeU32LE(16);
    writeU16LE(1);   // PCM
    writeU16LE(static_cast<uint16_t>(format_.channels));
    writeU32LE(static_cast<uint32_t>(format_.sampleRate));

    uint32_t byteRate = static_cast<uint32_t>(format_.sampleRate) *
                        static_cast<uint32_t>(format_.bytesPerFrame());
    writeU32LE(byteRate);
    writeU16LE(static_cast<uint16_t>(format_.bytesPerFrame()));
    writeU16LE(static_cast<uint16_t>(format_.bitsPerSample));

    // data chunk
    file_.write("data", 4);
    writeU32LE(dataSize);
}

void WavWriter::write(const std::vector<float>& samples) {
    write(samples.data(), samples.size());
}

void WavWriter::write(const float* data, size_t count) {
    if (!isOpen_ || count == 0) return;

    int bps = format_.bitsPerSample;
    int ch = format_.channels;

    if (bps == 16) {
        std::vector<uint8_t> buf(count * ch * 2);
        for (size_t i = 0; i < count; ++i) {
            // Clamp sample
            float s = data[i];
            if (s > 1.0f) s = 1.0f;
            if (s < -1.0f) s = -1.0f;

            int16_t pcm = static_cast<int16_t>(s * 32767.0f);
            size_t offset = i * ch * 2;
            for (int c = 0; c < ch; ++c) {
                size_t pos = offset + static_cast<size_t>(c) * 2;
                buf[pos]     = static_cast<uint8_t>(pcm & 0xFF);
                buf[pos + 1] = static_cast<uint8_t>((pcm >> 8) & 0xFF);
            }
        }
        file_.write(reinterpret_cast<const char*>(buf.data()),
                    static_cast<std::streamsize>(buf.size()));
        dataBytesWritten_ += buf.size();

    } else if (bps == 24) {
        std::vector<uint8_t> buf(count * ch * 3);
        for (size_t i = 0; i < count; ++i) {
            float s = data[i];
            if (s > 1.0f) s = 1.0f;
            if (s < -1.0f) s = -1.0f;

            int32_t pcm = static_cast<int32_t>(s * 8388607.0f);
            size_t offset = i * ch * 3;
            for (int c = 0; c < ch; ++c) {
                size_t pos = offset + static_cast<size_t>(c) * 3;
                buf[pos]     = static_cast<uint8_t>(pcm & 0xFF);
                buf[pos + 1] = static_cast<uint8_t>((pcm >> 8) & 0xFF);
                buf[pos + 2] = static_cast<uint8_t>((pcm >> 16) & 0xFF);
            }
        }
        file_.write(reinterpret_cast<const char*>(buf.data()),
                    static_cast<std::streamsize>(buf.size()));
        dataBytesWritten_ += buf.size();
    }
}

bool WavWriter::isOpen() const {
    return isOpen_;
}

} // namespace audiot
