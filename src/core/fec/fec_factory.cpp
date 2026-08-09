// fec_factory.cpp - FEC factory and codec adapters
#include "fec/fec_factory.h"

#include "fec/hamming.h"
#include "fec/reed_solomon.h"
#include "fec/ldpc.h"

namespace audiot {

namespace {

// ---------- NONE (passthrough) ----------

class NoneEncoder : public IFECEncoder {
public:
    std::vector<uint8_t> encode(const std::vector<uint8_t>& data) const override
    {
        return data;
    }
    FECType type() const override { return FECType::NONE; }
    std::string typeName() const override { return FECFactory::typeName(FECType::NONE); }
};

class NoneDecoder : public IFECDecoder {
public:
    DecodeResult decode(const std::vector<uint8_t>& encoded) const override
    {
        DecodeResult r;
        r.data = encoded;
        r.correctedErrors = 0;
        r.success = true;
        return r;
    }
    FECType type() const override { return FECType::NONE; }
};

// ---------- Hamming(7,4) ----------

class HammingEncoder : public IFECEncoder {
public:
    std::vector<uint8_t> encode(const std::vector<uint8_t>& data) const override
    {
        return HammingCodec::encode(data);
    }
    FECType type() const override { return FECType::HAMMING; }
    std::string typeName() const override { return FECFactory::typeName(FECType::HAMMING); }
};

class HammingDecoder : public IFECDecoder {
public:
    DecodeResult decode(const std::vector<uint8_t>& encoded) const override
    {
        HammingCodec::DecodeResult hr = HammingCodec::decode(encoded);
        DecodeResult r;
        r.data = hr.data;
        r.correctedErrors = hr.corrected_errors;
        r.success = (hr.uncorrectable == 0);
        return r;
    }
    FECType type() const override { return FECType::HAMMING; }
};

// ---------- Reed-Solomon ----------

class ReedSolomonEncoder : public IFECEncoder {
public:
    ReedSolomonEncoder() : rs_(8) {}
    std::vector<uint8_t> encode(const std::vector<uint8_t>& data) const override
    {
        return rs_.encode(data);
    }
    FECType type() const override { return FECType::REED_SOLOMON; }
    std::string typeName() const override { return FECFactory::typeName(FECType::REED_SOLOMON); }

private:
    ReedSolomon rs_;
};

class ReedSolomonDecoder : public IFECDecoder {
public:
    ReedSolomonDecoder() : rs_(8) {}
    DecodeResult decode(const std::vector<uint8_t>& encoded) const override
    {
        ReedSolomon::DecodeResult rr = rs_.decode(encoded);
        DecodeResult r;
        r.data = rr.data;
        r.correctedErrors = rr.corrected_errors;
        r.success = rr.success;
        return r;
    }
    FECType type() const override { return FECType::REED_SOLOMON; }

private:
    ReedSolomon rs_;
};

// ---------- LDPC ----------

class LdpcEncoder : public IFECEncoder {
public:
    LdpcEncoder() : codec_(256, 128) {}
    std::vector<uint8_t> encode(const std::vector<uint8_t>& data) const override
    {
        return codec_.encode(data);
    }
    FECType type() const override { return FECType::LDPC; }
    std::string typeName() const override { return FECFactory::typeName(FECType::LDPC); }

private:
    LDPCCodec codec_;
};

class LdpcDecoder : public IFECDecoder {
public:
    LdpcDecoder() : codec_(256, 128) {}
    DecodeResult decode(const std::vector<uint8_t>& encoded) const override
    {
        LDPCCodec::DecodeResult lr = codec_.decode(encoded);
        DecodeResult r;
        r.data = lr.data;
        r.success = lr.success;
        // LDPC does not report corrected errors directly; count differing
        // bits between the received word and the re-encoded decoded data.
        r.correctedErrors = 0;
        if (lr.success) {
            const std::vector<uint8_t> reenc = codec_.encode(lr.data);
            if (reenc.size() == encoded.size()) {
                for (size_t i = 0; i < reenc.size(); ++i) {
                    if (reenc[i] != encoded[i]) {
                        ++r.correctedErrors;
                    }
                }
            }
        }
        return r;
    }
    FECType type() const override { return FECType::LDPC; }

private:
    LDPCCodec codec_;
};

} // namespace

std::unique_ptr<IFECEncoder> FECFactory::createEncoder(FECType type)
{
    switch (type) {
    case FECType::NONE:         return std::unique_ptr<IFECEncoder>(new NoneEncoder());
    case FECType::HAMMING:      return std::unique_ptr<IFECEncoder>(new HammingEncoder());
    case FECType::REED_SOLOMON: return std::unique_ptr<IFECEncoder>(new ReedSolomonEncoder());
    case FECType::LDPC:         return std::unique_ptr<IFECEncoder>(new LdpcEncoder());
    }
    return std::unique_ptr<IFECEncoder>(new NoneEncoder());
}

std::unique_ptr<IFECDecoder> FECFactory::createDecoder(FECType type)
{
    switch (type) {
    case FECType::NONE:         return std::unique_ptr<IFECDecoder>(new NoneDecoder());
    case FECType::HAMMING:      return std::unique_ptr<IFECDecoder>(new HammingDecoder());
    case FECType::REED_SOLOMON: return std::unique_ptr<IFECDecoder>(new ReedSolomonDecoder());
    case FECType::LDPC:         return std::unique_ptr<IFECDecoder>(new LdpcDecoder());
    }
    return std::unique_ptr<IFECDecoder>(new NoneDecoder());
}

std::vector<FECType> FECFactory::supportedTypes()
{
    return std::vector<FECType>{
        FECType::NONE,
        FECType::HAMMING,
        FECType::REED_SOLOMON,
        FECType::LDPC
    };
}

std::string FECFactory::typeName(FECType type)
{
    switch (type) {
    case FECType::NONE:         return "None";
    case FECType::HAMMING:      return "Hamming(7,4)";
    case FECType::REED_SOLOMON: return "Reed-Solomon";
    case FECType::LDPC:         return "LDPC";
    }
    return "Unknown";
}

} // namespace audiot
