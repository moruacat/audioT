// fec_factory.h - Unified factory for FEC encoders/decoders
#ifndef AUDIOT_FEC_FACTORY_H
#define AUDIOT_FEC_FACTORY_H

#include <memory>
#include <vector>
#include <string>
#include <cstdint>

namespace audiot {

// FEC 类型枚举
enum class FECType {
    NONE,           // 无 FEC
    HAMMING,        // Hamming(7,4)
    REED_SOLOMON,   // Reed-Solomon
    LDPC            // LDPC
};

// FEC 编码器统一接口
class IFECEncoder {
public:
    virtual ~IFECEncoder() = default;
    virtual std::vector<uint8_t> encode(const std::vector<uint8_t>& data) const = 0;
    virtual FECType type() const = 0;
    virtual std::string typeName() const = 0;
};

// FEC 解码器统一接口
class IFECDecoder {
public:
    virtual ~IFECDecoder() = default;
    struct DecodeResult {
        std::vector<uint8_t> data;
        int correctedErrors;
        bool success;
    };
    virtual DecodeResult decode(const std::vector<uint8_t>& encoded) const = 0;
    virtual FECType type() const = 0;
};

// FEC 工厂
class FECFactory {
public:
    // 创建编码器
    static std::unique_ptr<IFECEncoder> createEncoder(FECType type);

    // 创建解码器
    static std::unique_ptr<IFECDecoder> createDecoder(FECType type);

    // 获取支持的 FEC 类型列表
    static std::vector<FECType> supportedTypes();

    // 获取类型名称
    static std::string typeName(FECType type);
};

} // namespace audiot
#endif // AUDIOT_FEC_FACTORY_H
