// test_qam16.cpp - 16-QAM 调制解调测试
#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>
#include <modem/qam16_modem.h>
#include <pipeline/pipeline_config.h>
#include <pipeline/tx_pipeline.h>
#include <pipeline/rx_pipeline.h>

using namespace audiot;

namespace {
std::vector<uint8_t> makeBits(int n) {
    std::vector<uint8_t> b;
    b.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        b.push_back(static_cast<uint8_t>(((i * 11 + 3) % 5) >= 2 ? 1 : 0));
    }
    return b;
}
} // namespace

TEST_CASE("QAM16 modem type / rates / symbol length", "[qam16]") {
    QAM16Modem modem;
    REQUIRE(modem.type() == ModemType::QAM);
    REQUIRE(modem.sampleRate() == 44100);
    REQUIRE(modem.samplesPerSymbol() == 42);   // 44100 / 1050
    REQUIRE(modem.bitsPerSymbol() == 4);
    REQUIRE(modem.bitsPerSecond() == 4200);    // 1050 x 4
}

TEST_CASE("QAM16 self-loop round trip", "[qam16]") {
    QAM16Modem::Config cfg;
    QAM16Modem modem(cfg);

    std::vector<uint8_t> bits = makeBits(160);
    bits.resize(bits.size() - bits.size() % 4);

    const std::vector<float> samples = modem.modulate(bits);
    REQUIRE(!samples.empty());

    QAM16Modem rx(cfg);
    const std::vector<uint8_t> out = rx.demodulate(samples);
    REQUIRE(out.size() == bits.size());
    for (size_t i = 0; i < bits.size(); ++i) {
        REQUIRE(out[i] == bits[i]);
    }
}

TEST_CASE("QAM16 tolerates leading silence and phase offset", "[qam16]") {
    // 前导兼作导频估计复数信道增益，因此不依赖载波相位锁定；
    // 前置静音由能量门限跳过。
    QAM16Modem::Config cfg;
    QAM16Modem modem(cfg);

    std::vector<uint8_t> bits = makeBits(128);
    bits.resize(bits.size() - bits.size() % 4);

    const std::vector<float> samples = modem.modulate(bits);

    const int offset = 23;  // 任意相位偏移
    std::vector<float> shifted(static_cast<size_t>(offset), 0.0f);
    shifted.insert(shifted.end(), samples.begin(), samples.end());

    QAM16Modem rx(cfg);
    const std::vector<uint8_t> out = rx.demodulate(shifted);
    REQUIRE(out.size() == bits.size());
    for (size_t i = 0; i < bits.size(); ++i) {
        REQUIRE(out[i] == bits[i]);
    }
}

TEST_CASE("QAM16 empty input yields no bits", "[qam16]") {
    QAM16Modem modem;
    REQUIRE(modem.demodulate(std::vector<float>()).empty());
}

TEST_CASE("QAM16 end-to-end through TX/RX pipeline (text + Hamming)", "[qam16][pipeline]") {
    PipelineConfig cfg;
    cfg.modemType = ModemType::QAM;
    cfg.fecType = FecType::HAMMING;
    cfg.autoDetectModem = false;

    const std::string text = "16QAM pipeline round trip 0123456789";

    TxPipeline tx(cfg);
    const std::vector<float> samples = tx.encodeTypedText(text);
    REQUIRE(!samples.empty());

    RxPipeline rx(cfg);
    const DecodeResult r = rx.decode(samples, true);
    REQUIRE(r.valid());
    REQUIRE(r.type == DecodeResult::TEXT);
    REQUIRE(r.text == text);
}
