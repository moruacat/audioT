// wav_reader.cpp - WAV file reader implementation
#include "wav_reader.h"
#include "util/file_utils.h"
#include <cstring>
#include <cmath>
#include <algorithm>

namespace audiot {

WavReader::WavReader()
    : file_(nullptr), dataOffset_(0), dataSize_(0), totalFrames_(0), framesRead_(0)
{
}

WavReader::~WavReader() {
    close();
}

bool WavReader::readU16LE(FILE* f, uint16_t& val) {
    uint8_t buf[2];
    if (fread(buf, 1, 2, f) != 2) return false;
    val = static_cast<uint16_t>(buf[0]) | (static_cast<uint16_t>(buf[1]) << 8);
    return true;
}

bool WavReader::readU32LE(FILE* f, uint32_t& val) {
    uint8_t buf[4];
    if (fread(buf, 1, 4, f) != 4) return false;
    val = static_cast<uint32_t>(buf[0])
        | (static_cast<uint32_t>(buf[1]) << 8)
        | (static_cast<uint32_t>(buf[2]) << 16)
        | (static_cast<uint32_t>(buf[3]) << 24);
    return true;
}

bool WavReader::open(const std::string& filename) {
    close();

    file_ = openFile(filename, "rb");
    if (!file_) return false;

    // --- RIFF header ---
    char riffId[4];
    if (fread(riffId, 1, 4, file_) != 4) { close(); return false; }
    if (std::memcmp(riffId, "RIFF", 4) != 0) { close(); return false; }

    uint32_t fileSize;
    if (!readU32LE(file_, fileSize)) { close(); return false; }

    char waveId[4];
    if (fread(waveId, 1, 4, file_) != 4) { close(); return false; }
    if (std::memcmp(waveId, "WAVE", 4) != 0) { close(); return false; }

    // --- Parse chunks ---
    // Get file size for bounds checking
    if (fseek(file_, 0, SEEK_END) != 0) { close(); return false; }
    long fileEndPos = ftell(file_);
    if (fileEndPos < 0) { close(); return false; }
    if (fseek(file_, 12, SEEK_SET) != 0) { close(); return false; } // after RIFF header

    bool foundFmt = false;
    bool foundData = false;

    while (!foundData) {
        char chunkId[4];
        uint32_t chunkSize;

        if (fread(chunkId, 1, 4, file_) != 4) break;
        if (!readU32LE(file_, chunkSize)) break;

        // Safety: verify chunk size doesn't exceed remaining file
        long currentPos = ftell(file_);
        if (currentPos < 0) { close(); return false; }
        long remaining = fileEndPos - currentPos;
        if (static_cast<uint64_t>(chunkSize) > static_cast<uint64_t>(remaining)) {
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
                if (fseek(file_, static_cast<long>(chunkSize - 16), SEEK_CUR) != 0) {
                    close(); return false;
                }
            }
            foundFmt = true;

        } else if (std::memcmp(chunkId, "data", 4) == 0) {
            if (!foundFmt) { close(); return false; }
            dataOffset_ = static_cast<size_t>(ftell(file_));
            dataSize_ = chunkSize;
            size_t bytesPerFrame = format_.bytesPerFrame();
            if (bytesPerFrame > 0) {
                totalFrames_ = dataSize_ / bytesPerFrame;
            }
            foundData = true;

        } else {
            // Skip unknown chunk (WAV chunks are word-aligned)
            uint32_t skipBytes = chunkSize + (chunkSize & 1);
            if (static_cast<uint64_t>(skipBytes) > static_cast<uint64_t>(remaining)) {
                // Not enough data to skip, stop parsing
                break;
            }
            if (fseek(file_, static_cast<long>(skipBytes), SEEK_CUR) != 0) {
                break;
            }
        }
    }

    if (!foundData) { close(); return false; }

    framesRead_ = 0;
    return true;
}

void WavReader::close() {
    if (file_) {
        fclose(file_);
        file_ = nullptr;
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
    return file_ != nullptr;
}

std::vector<float> WavReader::readAll() {
    // Seek to data start
    if (fseek(file_, static_cast<long>(dataOffset_), SEEK_SET) != 0) {
        return std::vector<float>();
    }
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
    if (maxFrames == 0 || !file_) return result;

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
    size_t gcount = fread(raw.data(), 1, bytesToRead, file_);

    if (gcount <= 0) return result;
    size_t actuallyRead = gcount;

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
