// test_afsk.cpp - AFSK modem tests
#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>
#include <modem/afsk_modem.h>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

using namespace audiot;

TEST_CASE("AFSK modem type and sample rate", "[afsk]") {
    AFSKModem::Config cfg;
    cfg.sampleRate = 44100;
    cfg.baudRate = 1200;
    AFSKModem modem(cfg);

    REQUIRE(modem.sampleRate() == 44100);
    REQUIRE(modem.type() == ModemType::AFSK);
    REQUIRE(modem.bitsPerSecond() == 1200);
}

TEST_CASE("AFSK modulation produces audio samples", "[afsk]") {
    AFSKModem::Config cfg;
    cfg.sampleRate = 44100;
    cfg.baudRate = 1200;
    AFSKModem modem(cfg);

    std::vector<uint8_t> bits = {1, 0, 1, 1, 0, 0, 1, 0};
    std::vector<float> samples = modem.modulate(bits);

    // Should produce samples (preamble + data + postamble)
    REQUIRE(!samples.empty());

    // Check that samples are in valid range
    for (size_t i = 0; i < samples.size(); ++i) {
        REQUIRE(samples[i] >= -1.0f);
        REQUIRE(samples[i] <= 1.0f);
    }
}

TEST_CASE("AFSK self-loop: modulate then demodulate", "[afsk]") {
    AFSKModem::Config cfg;
    cfg.sampleRate = 44100;
    cfg.markFreq = 1200.0f;
    cfg.spaceFreq = 2200.0f;
    cfg.baudRate = 1200;
    cfg.preambleBits = 32;
    cfg.postambleBits = 16;
    AFSKModem modem(cfg);

    // Create test data bits
    std::vector<uint8_t> originalBits;
    for (int i = 0; i < 64; ++i) {
        originalBits.push_back(static_cast<uint8_t>((i * 7 + 3) % 2));
    }

    // Modulate
    std::vector<float> samples = modem.modulate(originalBits);
    REQUIRE(!samples.empty());

    // Demodulate
    std::vector<uint8_t> recoveredBits = modem.demodulate(samples);

    // The recovered bits should match the original
    REQUIRE(recoveredBits.size() == originalBits.size());
    for (size_t i = 0; i < originalBits.size(); ++i) {
        REQUIRE(recoveredBits[i] == originalBits[i]);
    }
}

TEST_CASE("AFSK demodulate recovers bits with arbitrary symbol phase", "[afsk]") {
    // 回归：实时麦克风采集与信号到达之间没有样本级对齐，符号相位
    // 任意。解调器必须通过相位搜索恢复位流（旧实现固定从样本 0 切
    // 窗口，相位偏移大时 Goertzel 判决全面劣化、解调失败）。
    AFSKModem::Config cfg;
    cfg.sampleRate = 44100;
    cfg.markFreq = 1200.0f;
    cfg.spaceFreq = 2200.0f;
    cfg.baudRate = 1200;
    cfg.preambleBits = 32;
    cfg.postambleBits = 16;
    AFSKModem modem(cfg);

    std::vector<uint8_t> originalBits;
    // 非交替序列（真实数据帧不会像 preamble 那样严格交替，否则
    // 相位搜索会被错误相位的伪交替窗口迷惑）。
    for (int i = 0; i < 64; ++i) {
        originalBits.push_back(static_cast<uint8_t>(((i * 13 + 5) % 7) >= 3 ? 1 : 0));
    }
    const std::vector<float> samples = modem.modulate(originalBits);

    // 相位偏移：信号从样本 offset 处开始（非 spb 整数倍，模拟真实
    // 麦克风采集到任意相位）。
    const int offset = 27;
    std::vector<float> shifted(offset, 0.0f);
    shifted.insert(shifted.end(), samples.begin(), samples.end());

    AFSKModem rx(cfg);
    const std::vector<uint8_t> recoveredBits = rx.demodulate(shifted);
    REQUIRE(recoveredBits.size() == originalBits.size());
    for (size_t i = 0; i < originalBits.size(); ++i) {
        REQUIRE(recoveredBits[i] == originalBits[i]);
    }
}

TEST_CASE("AFSK demodulate empty input", "[afsk]") {
    AFSKModem::Config cfg;
    AFSKModem modem(cfg);

    std::vector<float> empty;
    std::vector<uint8_t> result = modem.demodulate(empty);
    REQUIRE(result.empty());
}
