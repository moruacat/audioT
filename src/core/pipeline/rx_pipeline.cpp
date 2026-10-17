// rx_pipeline.cpp - Receive pipeline implementation
#include "pipeline/rx_pipeline.h"
#include "codec/text_codec.h"
#include "codec/file_codec.h"
#include "fec/hamming.h"
#include "fec/reed_solomon.h"
#include "modem/afsk_modem.h"
#include "modem/mftsk_modem.h"
#include "modem/dpsk_modem.h"
#include "modem/qam16_modem.h"
#include "audio/wav_reader.h"

#include <algorithm>
#include <limits>

namespace audiot {

RxPipeline::RxPipeline(const PipelineConfig& config)
    : config_(config)
{
}

DecodeResult RxPipeline::decode(const std::vector<float>& samples, bool autoFec) {
    DecodeResult result;

    // autoDetectModem (via config): try the configured modulation first,
    // then the others, returning whichever yields a valid frame. This
    // lets a receiver cope with a sender that uses a different modulation
    // without manual re-configuration. Cost is one extra demodulation per
    // failed attempt; demodulations are cheap.
    std::vector<std::pair<ModemType, std::vector<float>>> attempts;
    attempts.push_back(std::make_pair(config_.modemType, samples));
    if (config_.autoDetectModem) {
        const ModemType all[] = { ModemType::AFSK, ModemType::MFSK,
                                  ModemType::PSK, ModemType::QAM };
        for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); ++i) {
            if (all[i] != config_.modemType) {
                attempts.push_back(std::make_pair(all[i], samples));
            }
        }
    }

    for (size_t a = 0; a < attempts.size(); ++a) {
        const ModemType mt = attempts[a].first;
        const std::vector<float>& buf = attempts[a].second;

        if (mt == ModemType::MFSK) {
            MFSKModem::Config mcfg;
            mcfg.sampleRate = config_.sampleRate;
            mcfg.baseFreq = config_.mftskBaseFreq;
            const MftskProfile prof = mftskProfileForTier(config_.mftskTier);
            mcfg.slotSpacing = prof.slotSpacing;
            mcfg.order = prof.bitsPerSymbol;
            mcfg.symbolRate = prof.symbolRate;
            MFSKModem modem(mcfg);
            std::vector<uint8_t> fecBits = modem.demodulate(buf);
            if (!fecBits.empty()) {
                DecodeResult r = continueFec(fecBits, autoFec);
                if (r.valid()) {
                    return r;
                }
            }
        } else if (mt == ModemType::PSK) {
            // 差分 PSK：同样自带前导同步、容忍前置静音/杂音，直接解原始
            // buffer（无需像 AFSK 那样裁剪 prep tone）。
            DPSKModem::Config pcfg;
            pcfg.sampleRate = config_.sampleRate;
            const PskQamProfile prof = pskQamProfileFor(config_.pskQamRate);
            pcfg.symbolRate = prof.symbolRate;
            pcfg.carrierFreq = prof.carrierFreq;
            pcfg.order = config_.pskOrder;
            DPSKModem modem(pcfg);

            std::vector<uint8_t> fecBits = modem.demodulate(buf);
            if (!fecBits.empty()) {
                DecodeResult r = continueFec(fecBits, autoFec);
                if (r.valid()) {
                    return r;
                }
            }
        } else if (mt == ModemType::QAM) {
            // 16-QAM：前导兼作导频做信道估计，同样直接解原始 buffer。
            QAM16Modem::Config qcfg;
            qcfg.sampleRate = config_.sampleRate;
            const PskQamProfile qprof = pskQamProfileFor(config_.pskQamRate);
            qcfg.symbolRate = qprof.symbolRate;
            qcfg.carrierFreq = qprof.carrierFreq;
            QAM16Modem modem(qcfg);

            std::vector<uint8_t> fecBits = modem.demodulate(buf);
            if (!fecBits.empty()) {
                DecodeResult r = continueFec(fecBits, autoFec);
                if (r.valid()) {
                    return r;
                }
            }
        } else {  // AFSK
            AFSKModem::Config modemCfg;
            modemCfg.sampleRate = config_.sampleRate;
            modemCfg.markFreq = config_.markFreq;
            modemCfg.spaceFreq = config_.spaceFreq;
            modemCfg.baudRate = config_.baudRate;
            AFSKModem modem(modemCfg);

            // The transmitter plays a mandatory continuous prep tone before
            // the data; its end is the deterministic start of the signal
            // burst (the preamble begins there). Crop the buffer at that
            // point, keeping a small symbol margin for window-level
            // detection error, so the demodulator never sees the long
            // silence preceding the signal: silence windows decide
            // randomly and can fake a preamble (verified: 31 s of silence +
            // signal produced a perfect fake preamble at the wrong
            // position). Streams without a prep tone (legacy/test signals)
            // are decoded as-is.
            std::vector<float> work = buf;
            const size_t prepEnd = modem.locatePrepEnd(buf);
            if (prepEnd != std::numeric_limits<size_t>::max()) {
                const size_t kMargin = static_cast<size_t>(config_.sampleRate /
                                                           config_.baudRate) * 2;
                const size_t cropStart = prepEnd >= kMargin ? prepEnd - kMargin : 0;
                work.assign(buf.begin() + cropStart, buf.end());
            }

            std::vector<uint8_t> fecBits = modem.demodulate(work);
            if (!fecBits.empty()) {
                DecodeResult r = continueFec(fecBits, autoFec);
                if (r.valid()) {
                    return r;
                }
            }
        }
    }

    return result;
}

DecodeResult RxPipeline::continueFec(const std::vector<uint8_t>& fecBits,
                                     bool autoFec) const {
    DecodeResult result;

    // Step 2: FEC decode. With autoFec, try the configured scheme first
    // and then every other one until a frame validates (demodulation
    // already ran once; these stages are cheap).
    if (autoFec) {
        std::vector<FecType> order;
        order.push_back(config_.fecType);
        if (config_.fecType != FecType::NONE) order.push_back(FecType::NONE);
        if (config_.fecType != FecType::HAMMING) order.push_back(FecType::HAMMING);
        if (config_.fecType != FecType::REED_SOLOMON) order.push_back(FecType::REED_SOLOMON);
        for (size_t i = 0; i < order.size(); ++i) {
            DecodeResult r = decodeWithFec(fecBits, order[i]);
            if (r.valid()) {
                return r;
            }
        }
        return result;
    }

    return decodeWithFec(fecBits, config_.fecType);
}

DecodeResult RxPipeline::decodeWithFec(const std::vector<uint8_t>& fecBits,
                                       FecType fecType) const {
    DecodeResult result;

    // Step 2: FEC decode (optional)
    std::vector<uint8_t> framed;
    if (fecType == FecType::HAMMING) {
        std::vector<uint8_t> bits = HammingCodec::decode(fecBits).data;
        framed.reserve(bits.size() / 8);
        for (size_t i = 0; i + 7 < bits.size(); i += 8) {
            uint8_t byte = 0;
            for (int b = 0; b < 8; ++b) {
                byte = (byte << 1) | (bits[i + b] & 1);
            }
            framed.push_back(byte);
        }
    } else if (fecType == FecType::REED_SOLOMON) {
        // Convert bits to bytes first; RS works on the byte stream
        std::vector<uint8_t> bytes;
        bytes.reserve(fecBits.size() / 8);
        for (size_t i = 0; i + 7 < fecBits.size(); i += 8) {
            uint8_t byte = 0;
            for (int b = 0; b < 8; ++b) {
                byte = (byte << 1) | (fecBits[i + b] & 1);
            }
            bytes.push_back(byte);
        }
        framed = rsDecode(bytes);
        if (framed.empty()) {
            return result;
        }
    } else {
        // NONE: bits straight to bytes
        framed.reserve(fecBits.size() / 8);
        for (size_t i = 0; i + 7 < fecBits.size(); i += 8) {
            uint8_t byte = 0;
            for (int b = 0; b < 8; ++b) {
                byte = (byte << 1) | (fecBits[i + b] & 1);
            }
            framed.push_back(byte);
        }
    }

    // Step 4: Read the first valid frame. If it is a 1-byte type marker
    // ('T' text / 'F' file), decode the data frame that follows it;
    // otherwise treat the stream as legacy text-only format.
    const TextCodec::FrameInfo first = TextCodec::decodeAt(framed);
    if (!first.valid) {
        return result;
    }

    if (first.payload.size() == 1 &&
        (first.payload[0] == 'T' || first.payload[0] == 'F' ||
         first.payload[0] == 'I' || first.payload[0] == 'V')) {
        if (first.payload[0] == 'T') {
            // Text data frame (same framing/CRC as the type marker)
            const TextCodec::FrameInfo data =
                TextCodec::decodeAt(framed, first.frameEnd);
            if (!data.valid) {
                return result;
            }
            result.type = DecodeResult::TEXT;
            result.text = data.payload;
        } else {
            // File data frame (FileCodec framing: name + type + data + CRC-32).
            // 'F' = 通用文件，'I' = 图片，'V' = 视频——后两者与通用文件走
            // 完全相同的数字链路，只是载荷类别不同，供接收端按类别呈现。
            const std::vector<uint8_t> rest(framed.begin() + first.frameEnd,
                                            framed.end());
            const FileCodec::DecodedFile file = FileCodec::decodeFrame(rest);
            if (!file.valid) {
                return result;
            }
            result.filename = file.filename;
            result.fileData = file.data;
            const char marker = static_cast<char>(first.payload[0]);
            if (marker == 'I' && file.type == FileCodec::FileType::IMAGE) {
                result.type = DecodeResult::IMAGE;
            } else if (marker == 'V' && file.type == FileCodec::FileType::VIDEO) {
                result.type = DecodeResult::VIDEO;
            } else {
                result.type = DecodeResult::FILE;
            }
        }
        return result;
    }

    // Legacy format: no type marker, the first frame is the text itself
    result.type = DecodeResult::TEXT;
    result.text = first.payload;
    return result;
}

std::vector<uint8_t> RxPipeline::rsDecode(const std::vector<uint8_t>& bytes) {
    // Stream layout (mirror of TxPipeline::rsEncode):
    // [0xAA 0x55][len 4B big-endian][blocks of 255 bytes]
    static const size_t kDataPerBlock = 239;
    static const size_t kParity = 16;

    // Locate the sync header, skipping any leading noise bytes
    size_t syncPos = bytes.size();
    for (size_t i = 0; i + 1 < bytes.size(); ++i) {
        if (bytes[i] == 0xAA && bytes[i + 1] == 0x55) {
            syncPos = i;
            break;
        }
    }
    if (syncPos + 6 > bytes.size()) {
        return std::vector<uint8_t>();
    }

    const size_t len = (static_cast<size_t>(bytes[syncPos + 2]) << 24)
                     | (static_cast<size_t>(bytes[syncPos + 3]) << 16)
                     | (static_cast<size_t>(bytes[syncPos + 4]) << 8)
                     | static_cast<size_t>(bytes[syncPos + 5]);
    if (len == 0) {
        return std::vector<uint8_t>();
    }

    const size_t blocks = (len + kDataPerBlock - 1) / kDataPerBlock;
    const size_t streamEnd = syncPos + 6 + blocks * (kDataPerBlock + kParity);
    if (streamEnd > bytes.size()) {
        return std::vector<uint8_t>();  // truncated stream
    }

    ReedSolomon rs(8);
    std::vector<uint8_t> out;
    out.reserve(len);
    for (size_t b = 0; b < blocks; ++b) {
        const uint8_t* blk = &bytes[syncPos + 6 + b * (kDataPerBlock + kParity)];
        const std::vector<uint8_t> enc(blk, blk + kDataPerBlock + kParity);
        const ReedSolomon::DecodeResult r = rs.decode(enc);
        if (!r.success) {
            return std::vector<uint8_t>();  // uncorrectable errors
        }
        out.insert(out.end(), r.data.begin(), r.data.end());
    }
    out.resize(len);
    return out;
}

DecodeResult RxPipeline::decodeFromFile(const std::string& wavPath) {
    WavReader reader;
    if (!reader.open(wavPath)) {
        return DecodeResult();
    }
    std::vector<float> samples = reader.readAll();
    reader.close();
    return decode(samples, true);
}

} // namespace audiot
