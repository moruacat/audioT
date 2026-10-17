// test_dpsk.cpp - 差分 PSK 调制解调测试
#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>
#include <modem/dpsk_modem.h>
#include <pipeline/pipeline_config.h>
#include <pipeline/tx_pipeline.h>
#include <pipeline/rx_pipeline.h>

using namespace audiot;

namespace {
// 伪随机但非交替的比特序列（交替序列会被错误相位的前导匹配迷惑）
std::vector<uint8_t> makeBits(int n) {
    std::vector<uint8_t> b;
    b.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        b.push_back(static_cast<uint8_t>(((i * 13 + 5) % 7) >= 3 ? 1 : 0));
    }
    return b;
}
} // namespace

TEST_CASE("DPSK modem type / rates / symbol length", "[dpsk]") {
    DPSKModem modem;  // 默认 DQPSK
    REQUIRE(modem.type() == ModemType::PSK);
    REQUIRE(modem.sampleRate() == 44100);
    REQUIRE(modem.samplesPerSymbol() == 42);          // 44100 / 1050
    REQUIRE(modem.bitsPerSymbol() == 2);
    REQUIRE(modem.slotCount() == 4);
    REQUIRE(modem.bitsPerSecond() == 2100);           // 1050 x 2
}

TEST_CASE("DPSK self-loop round trip: DBPSK / DQPSK / D8PSK", "[dpsk]") {
    const int orders[] = { 1, 2, 3 };
    for (size_t o = 0; o < sizeof(orders) / sizeof(orders[0]); ++o) {
        DPSKModem::Config cfg;
        cfg.order = orders[o];
        DPSKModem modem(cfg);

        std::vector<uint8_t> bits = makeBits(120);
        bits.resize(bits.size() - bits.size() % static_cast<size_t>(cfg.order));

        const std::vector<float> samples = modem.modulate(bits);
        REQUIRE(!samples.empty());

        DPSKModem rx(cfg);
        const std::vector<uint8_t> out = rx.demodulate(samples);
        REQUIRE(out.size() == bits.size());
        for (size_t i = 0; i < bits.size(); ++i) {
            REQUIRE(out[i] == bits[i]);
        }
    }
}

TEST_CASE("DPSK tolerates arbitrary symbol phase and leading silence", "[dpsk]") {
    // 回归：真实采集与信号到达之间没有样本级对齐，符号相位任意。
    // 接收端必须靠相位扫描 + 差分（非相干）恢复位流，而不是依赖载波
    // 相位锁定（旧 beta/psk_modem 的 Costas 环就在这里失败）。
    DPSKModem::Config cfg;  // DQPSK
    DPSKModem modem(cfg);

    std::vector<uint8_t> bits = makeBits(96);
    bits.resize(bits.size() - bits.size() % 2);

    const std::vector<float> samples = modem.modulate(bits);

    const int offset = 17;  // 非 sps 整数倍的任意相位偏移
    std::vector<float> shifted(static_cast<size_t>(offset), 0.0f);
    shifted.insert(shifted.end(), samples.begin(), samples.end());

    DPSKModem rx(cfg);
    const std::vector<uint8_t> out = rx.demodulate(shifted);
    REQUIRE(out.size() == bits.size());
    for (size_t i = 0; i < bits.size(); ++i) {
        REQUIRE(out[i] == bits[i]);
    }
}

TEST_CASE("DPSK empty input yields no bits", "[dpsk]") {
    DPSKModem modem;
    REQUIRE(modem.demodulate(std::vector<float>()).empty());
}

TEST_CASE("DPSK end-to-end through TX/RX pipeline (text + Hamming)", "[dpsk][pipeline]") {
    // 验证 PSK 已真正接入收发管线：文本帧 -> FEC -> PSK 调制 -> PSK 解调
    // -> FEC 解码 -> 文本帧解析，全链路往返。
    PipelineConfig cfg;
    cfg.modemType = ModemType::PSK;
    cfg.pskOrder = 2;                 // DQPSK
    cfg.fecType = FecType::HAMMING;
    cfg.autoDetectModem = false;

    const std::string text = "PSK-DQPSK pipeline round trip 0123456789";

    TxPipeline tx(cfg);
    const std::vector<float> samples = tx.encodeTypedText(text);
    REQUIRE(!samples.empty());

    RxPipeline rx(cfg);
    const DecodeResult r = rx.decode(samples, true);
    REQUIRE(r.valid());
    REQUIRE(r.type == DecodeResult::TEXT);
    REQUIRE(r.text == text);
}
