// tx_pipeline.cpp - Transmit pipeline implementation
#include "pipeline/tx_pipeline.h"
#include "codec/text_codec.h"
#include "codec/file_codec.h"
#include "fec/hamming.h"
#include "fec/reed_solomon.h"
#include "modem/afsk_modem.h"
#include "modem/mftsk_modem.h"
#include "audio/wav_writer.h"
#include "audio/audio_format.h"

#include <cmath>

namespace audiot {

TxPipeline::TxPipeline(const PipelineConfig& config)
    : config_(config)
{
}

std::vector<float> TxPipeline::encode(const std::string& text) {
    // Step 1: Text codec encode (adds frame header + CRC)
    std::vector<uint8_t> framed = TextCodec::encode(text);

    // Step 2-4: FEC + AFSK modulate
    return encodeFrame(framed);
}

std::vector<float> TxPipeline::encodeFrame(const std::vector<uint8_t>& framed) {
    // Reed-Solomon is applied at byte level, before the bit conversion
    // (it corrects whole bytes, which is what a noisy acoustic channel
    // corrupts). Hamming is applied later on the bit stream instead.
    std::vector<uint8_t> fecBytes = framed;
    if (config_.fecType == FecType::REED_SOLOMON) {
        fecBytes = rsEncode(framed);
    }

    // Bytes to bits, then FEC + modulate
    std::vector<uint8_t> bits;
    bits.reserve(fecBytes.size() * 8);
    for (size_t i = 0; i < fecBytes.size(); ++i) {
        for (int b = 7; b >= 0; --b) {
            bits.push_back((fecBytes[i] >> b) & 1);
        }
    }
    return modulateBits(bits);
}

std::vector<float> TxPipeline::encodeFrames(
    const std::vector<std::vector<uint8_t>>& frames) {
    // Concatenate all frames into one bit stream, then FEC + modulate
    // once: the output carries a single preamble/postamble pair, so the
    // demodulator sees one continuous burst and does not cut the second
    // frame at the mid-stream postamble.
    size_t totalBytes = 0;
    for (size_t f = 0; f < frames.size(); ++f) {
        totalBytes += frames[f].size();
    }

    // Reed-Solomon at byte level over the concatenated frame stream
    // (one RS stream for the whole burst, blocks back-to-back).
    std::vector<uint8_t> allBytes;
    allBytes.reserve(totalBytes);
    for (size_t f = 0; f < frames.size(); ++f) {
        allBytes.insert(allBytes.end(), frames[f].begin(), frames[f].end());
    }
    std::vector<uint8_t> fecBytes = allBytes;
    if (config_.fecType == FecType::REED_SOLOMON) {
        fecBytes = rsEncode(allBytes);
    }

    std::vector<uint8_t> bits;
    bits.reserve(fecBytes.size() * 8);
    for (size_t i = 0; i < fecBytes.size(); ++i) {
        for (int b = 7; b >= 0; --b) {
            bits.push_back((fecBytes[i] >> b) & 1);
        }
    }
    return modulateBits(bits);
}

std::vector<float> TxPipeline::encodeTypedText(const std::string& text) {
    return encodeFrames({TextCodec::encode(std::string(1, 'T')),
                         TextCodec::encode(text)});
}

std::vector<float> TxPipeline::encodeTypedFile(const std::string& filePath) {
    return encodeFrames({TextCodec::encode(std::string(1, 'F')),
                         FileCodec::encodeFile(filePath)});
}

std::vector<float> TxPipeline::modulateBits(const std::vector<uint8_t>& bits) {
    // Step 3: Hamming FEC encode (optional)
    std::vector<uint8_t> fecBits;
    if (config_.fecType == FecType::HAMMING) {
        // Pad to multiple of 4
        std::vector<uint8_t> padded = bits;
        while (padded.size() % 4 != 0) {
            padded.push_back(0);
        }
        fecBits = HammingCodec::encode(padded);
    } else {
        fecBits = bits;
    }

    // Step 4: Modulate (AFSK or M-FSK, chosen by config).
    if (config_.modemType == ModemType::MFSK) {
        MFSKModem::Config mcfg;
        mcfg.sampleRate = config_.sampleRate;
        mcfg.baseFreq = config_.mftskBaseFreq;
        const MftskProfile prof = mftskProfileForTier(config_.mftskTier);
        mcfg.slotSpacing = prof.slotSpacing;
        mcfg.order = prof.bitsPerSymbol;
        mcfg.symbolRate = prof.symbolRate;
        MFSKModem modem(mcfg);
        return modem.modulate(fecBits);
    }

    // Default: AFSK (Bell 202 style)
    AFSKModem::Config modemCfg;
    modemCfg.sampleRate = config_.sampleRate;
    modemCfg.markFreq = config_.markFreq;
    modemCfg.spaceFreq = config_.spaceFreq;
    modemCfg.baudRate = config_.baudRate;

    AFSKModem modem(modemCfg);
    return modem.modulate(fecBits);
}

std::vector<uint8_t> TxPipeline::rsEncode(const std::vector<uint8_t>& data) {
    // Stream layout: [0xAA 0x55][len 4B big-endian][RS blocks]
    // Each block: 239 data bytes (last block zero-padded) + 16 parity
    // bytes. The receiver re-reads the length from the header to strip
    // the padding.
    static const size_t kDataPerBlock = 239;
    static const size_t kParity = 16;

    if (data.empty()) {
        return std::vector<uint8_t>();
    }

    const size_t blockCount = (data.size() + kDataPerBlock - 1) / kDataPerBlock;
    std::vector<uint8_t> out;
    out.reserve(6 + blockCount * (kDataPerBlock + kParity));
    out.push_back(0xAA);
    out.push_back(0x55);
    out.push_back(static_cast<uint8_t>((data.size() >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((data.size() >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((data.size() >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(data.size() & 0xFF));

    ReedSolomon rs(8);
    std::vector<uint8_t> block;
    block.reserve(kDataPerBlock);
    for (size_t off = 0; off < data.size(); off += kDataPerBlock) {
        const size_t end = std::min(off + kDataPerBlock, data.size());
        block.assign(data.begin() + off, data.begin() + end);
        block.resize(kDataPerBlock, 0);  // pad last block
        const std::vector<uint8_t> enc = rs.encode(block);
        out.insert(out.end(), enc.begin(), enc.end());
    }
    return out;
}

bool TxPipeline::encodeToFile(const std::string& text, const std::string& wavPath) {
    std::vector<float> samples = encode(text);
    return writeSamplesToWav(samples, wavPath);
}

bool TxPipeline::writeSamplesToWav(const std::vector<float>& samples,
                                   const std::string& wavPath) {
    if (samples.empty()) {
        return false;
    }

    AudioFormat fmt;
    fmt.sampleRate = config_.sampleRate;
    fmt.channels = 1;
    fmt.bitsPerSample = 16;

    WavWriter writer;
    if (!writer.open(wavPath, fmt)) {
        return false;
    }
    writer.write(samples);
    writer.close();
    return true;
}

std::vector<float> TxPipeline::makeTone(float freqHz, float seconds,
                                        int sampleRate, float amplitude) {
    std::vector<float> out;
    if (freqHz <= 0.0f || seconds <= 0.0f || sampleRate <= 0 || amplitude <= 0.0f) {
        return out;
    }

    const long long count =
        static_cast<long long>(static_cast<double>(seconds) * sampleRate + 0.5);
    if (count <= 0) {
        return out;
    }
    out.reserve(static_cast<size_t>(count));

    // Phase-accumulator synthesis (same idea as sstv_encoder appendTone).
    // Double precision keeps long tones (10 s+) free of audible drift.
    static const double kTwoPi = 6.283185307179586476925286766559;
    const double phaseStep =
        kTwoPi * static_cast<double>(freqHz) / static_cast<double>(sampleRate);
    double phase = 0.0;
    for (long long i = 0; i < count; ++i) {
        out.push_back(static_cast<float>(std::sin(phase)) * amplitude);
        phase += phaseStep;
    }
    return out;
}

std::vector<float> TxPipeline::makePrepAligned(float freqHz, float seconds,
                                               int sampleRate, int baudRate,
                                               float amplitude) {
    std::vector<float> out = makeTone(freqHz, seconds, sampleRate, amplitude);
    if (out.empty() || baudRate <= 0) {
        return out;
    }
    const int spb = sampleRate / baudRate;
    if (spb > 1) {
        out.resize((out.size() / spb) * spb);
    }
    return out;
}

std::vector<float> TxPipeline::makeSilence(float seconds, int sampleRate) {
    std::vector<float> out;
    if (seconds <= 0.0f || sampleRate <= 0) {
        return out;
    }

    const long long count =
        static_cast<long long>(static_cast<double>(seconds) * sampleRate + 0.5);
    if (count <= 0) {
        return out;
    }
    out.assign(static_cast<size_t>(count), 0.0f);
    return out;
}

} // namespace audiot
