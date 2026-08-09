// wav_reader.cpp - WAV file reader implementation
#include "wav_reader.h"
#include <cstring>
#include <cmath>
#include <algorithm>

namespace audiot {

WavReader::WavReader()
    : dataOffset_(0), dataSize_(0), totalFrames_(0), framesRead_(0)
{
}

WavReader::~WavReader() {
    close();
}

bool WavReader::readU16LE(std::ifstream& f, uint16_t& val) {
    uint8_t buf[2];
    if (!f.read(reinterpret_cast<char*>(buf), 2)) return false;
    val = static_cast<uint16_t>(buf[0]) | (static_cast<uint16_t>(buf[1]) << 8);
    return true;
}

bool WavReader::readU32LE(std::ifstream& f, uint32_t& val) {
    uint8_t buf[4];
    if (!f.read(reinterpret_cast<char*>(buf), 4)) return false;
    val = static_cast<uint32_t>(buf[0])
        | (static_cast<uint32_t>(buf[1]) << 8)
        | (static_cast<uint32_t>(buf[2]) << 16)
        | (static_cast<uint32_t>(buf[3]) << 24);
    return true;
}

bool WavReader::open(const std::string& filename) {
    close();

    file_.open(filename, std::ios::binary);
    if (!file_.is_open()) return false;

    // --- RIFF header ---
    char riffId[4];
    if (!file_.read(riffId, 4)) { close(); return false; }
    if (std::memcmp(riffId, "RIFF", 4) != 0) { close(); return false; }

    uint32_t fileSize;
    if (!readU32LE(file_, fileSize)) { close(); return false; }

    char waveId[4];
    if (!file_.read(waveId, 4)) { close(); return false; }
    if (std::memcmp(waveId, "WAVE", 4) != 0) { close(); return false; }

    // --- Parse chunks ---
    // Get file size for bounds checking
    file_.seekg(0, std::ios::end);
    auto fileEndPos = file_.tellg();
    file_.seekg(12, std::ios::beg); // back to start of chunks (after RIFF header)

    bool foundFmt = false;
    bool foundData = false;

    while (!foundData) {
        char chunkId[4];
        uint32_t chunkSize;

        if (!file_.read(chunkId, 4)) break;
        if (!readU32LE(file_, chunkSize)) break;

        // Safety: verify chunk size doesn't exceed remaining file
        auto currentPos = file_.tellg();
        if (currentPos < 0) { close(); return false; }
        auto remaining = static_cast<uint64_t>(fileEndPos) - static_cast<uint64_t>(currentPos);
        if (static_cast<uint64_t>(chunkSize) > remaining) {
            // Chunk truncated - only safe if this is the data chunk
            if (std::memcmp(chunkId, "data", 4) == 0 && foundFmt) {
                chunkSize = static_cast<uint32_t>(remaining);
            } else {
                close();
                return false;
            }
        }

        if (std::memcmp(chunkId, "fmt ", 4) == 0) {
            if (chunkSize < 16) { close(); return false; }

            uint16_t audioFormat;
            if (!readU16LE(file_, audioFormat)) { close(); return false; }
            // Accept PCM (1) and WAVE_FORMAT_EXTENSIBLE (0xFFFE)
            if (audioFormat != 1 && audioFormat != 0xFFFE) {
                close();
                return false;
            }

            uint16_t channels;
            if (!readU16LE(file_, channels)) { close(); return false; }
            format_.channels = channels;

            uint32_t sampleRate;
            if (!readU32LE(file_, sampleRate)) { close(); return false; }
            format_.sampleRate = static_cast<int>(sampleRate);

            uint32_t byteRate;
            if (!readU32LE(file_, byteRate)) { close(); return false; }

            uint16_t blockAlign;
            if (!readU16LE(file_, blockAlign)) { close(); return false; }

            uint16_t bitsPerSample;
            if (!readU16LE(file_, bitsPerSample)) { close(); return false; }
            format_.bitsPerSample = bitsPerSample;

            if (bitsPerSample != 16 && bitsPerSample != 24) {
                close();
                return false;
            }

            // Skip extra fmt bytes
            if (chunkSize > 16) {
                file_.seekg(static_cast<std::streamoff>(chunkSize - 16), std::ios::cur);
            }
            foundFmt = true;

        } else if (std::memcmp(chunkId, "data", 4) == 0) {
            if (!foundFmt) { close(); return false; }
            dataOffset_ = static_cast<size_t>(file_.tellg());
            dataSize_ = chunkSize;
            size_t bytesPerFrame = format_.bytesPerFrame();
            if (bytesPerFrame > 0) {
                totalFrames_ = dataSize_ / bytesPerFrame;
            }
            foundData = true;

        } else {
            // Skip unknown chunk (WAV chunks are word-aligned)
            uint32_t skipBytes = chunkSize + (chunkSize & 1);
            auto pos = file_.tellg();
            if (pos >= 0 && static_cast<uint64_t>(skipBytes) > remaining) {
                // Not enough data to skip, stop parsing
                break;
            }
            file_.seekg(static_cast<std::streamoff>(skipBytes), std::ios::cur);
        }
    }

    if (!foundData) { close(); return false; }

    framesRead_ = 0;
    return true;
}

void WavReader::close() {
    if (file_.is_open()) {
        file_.close();
    }
    dataOffset_ = 0;
    dataSize_ = 0;
    totalFrames_ = 0;
    framesRead_ = 0;
}

AudioFormat WavReader::format() const {
    return format_;
}

size_t WavReader::totalSamples() const {
    return totalFrames_;
}

bool WavReader::isOpen() const {
    return file_.is_open();
}

std::vector<float> WavReader::readAll() {
    // Seek to data start
    file_.seekg(static_cast<std::streamoff>(dataOffset_));
    framesRead_ = 0;
    return readSamples(totalFrames_);
}

std::vector<float> WavReader::read(size_t count) {
    size_t remaining = totalFrames_ - framesRead_;
    size_t toRead = (count < remaining) ? count : remaining;
    return readSamples(toRead);
}

std::vector<float> WavReader::readSamples(size_t maxFrames) {
    std::vector<float> result;
    if (maxFrames == 0 || !file_.is_open()) return result;

    int bps = format_.bitsPerSample;
    int ch = format_.channels;
    if (ch <= 0 || bps <= 0) return result;

    int bytesPerSample = bps / 8;
    size_t bytesPerFrame = static_cast<size_t>(format_.bytesPerFrame());
    if (bytesPerFrame == 0) return result;

    // Clamp maxFrames to what's actually remaining
    size_t remainingFrames = totalFrames_ - framesRead_;
    if (maxFrames > remainingFrames) maxFrames = remainingFrames;

    // Read raw bytes - protect against truncated files
    size_t bytesToRead = maxFrames * bytesPerFrame;
    std::vector<uint8_t> raw(bytesToRead);
    std::streamsize gcount = file_.read(
        reinterpret_cast<char*>(raw.data()),
        static_cast<std::streamsize>(bytesToRead)
    ).gcount();

    if (gcount <= 0) return result;
    size_t actuallyRead = static_cast<size_t>(gcount);

    // Align to frame boundary - partial frames are discarded
    size_t framesRead = actuallyRead / bytesPerFrame;
    if (framesRead == 0) return result;
    result.resize(framesRead);

    float scale16 = 1.0f / 32768.0f;
    float scale24 = 1.0f / 8388608.0f;

    for (size_t i = 0; i < framesRead; ++i) {
        float sum = 0.0f;
        for (int c = 0; c < ch; ++c) {
            size_t offset = i * bytesPerFrame + static_cast<size_t>(c) * bytesPerSample;
            float sample = 0.0f;

            if (bps == 16) {
                int16_t s = static_cast<int16_t>(
                    static_cast<uint16_t>(raw[offset]) |
                    (static_cast<uint16_t>(raw[offset + 1]) << 8)
                );
                sample = static_cast<float>(s) * scale16;
            } else if (bps == 24) {
                // 24-bit signed, little-endian
                int32_t s = static_cast<int32_t>(raw[offset])
                    | (static_cast<int32_t>(raw[offset + 1]) << 8)
                    | (static_cast<int32_t>(raw[offset + 2]) << 16);
                // Sign extend from 24-bit
                if (s & 0x800000) {
                    s |= static_cast<int32_t>(0xFF000000);
                }
                sample = static_cast<float>(s) * scale24;
            }

            // Clamp
            if (sample > 1.0f) sample = 1.0f;
            if (sample < -1.0f) sample = -1.0f;
            sum += sample;
        }
        // Mix down to mono
        result[i] = sum / static_cast<float>(ch);
    }

    framesRead_ += framesRead;
    return result;
}

} // namespace audiot
