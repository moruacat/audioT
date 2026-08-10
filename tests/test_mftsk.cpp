// test_mftsk.cpp - M-FSK (Multi-Tone FSK) modem & pipeline tests
#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>

#include <modem/mftsk_modem.h>
#include <pipeline/pipeline_config.h>
#include <pipeline/tx_pipeline.h>
#include <pipeline/rx_pipeline.h>

#include <cmath>
#include <random>

using namespace audiot;

static MFSKModem makeModem() {
    MFSKModem::Config cfg;
    cfg.sampleRate = 44100;
    cfg.baseFreq = 1500.0f;
    cfg.slotSpacing = 50.0f;
    cfg.order = 2;        // 4-FSK
    cfg.symbolRate = 50;  // 20 ms/symbol
    cfg.preambleSymbols = 8;
    return MFSKModem(cfg);
}

static std::vector<uint8_t> makeBits(int n) {
    std::vector<uint8_t> b;
    for (int i = 0; i < n; ++i) b.push_back(static_cast<uint8_t>((i * 3 + 1) % 2));
    return b;
}

static void addNoise(std::vector<float>& s, float amp, unsigned seed) {
    std::mt19937 rng(seed);
    std::normal_distribution<float> nd(0.0f, 1.0f);
    for (size_t i = 0; i < s.size(); ++i) s[i] += nd(rng) * amp;
}

static void addTone(std::vector<float>& s, float freq, float amp, int rate) {
    double ph = 0.0;
    for (size_t i = 0; i < s.size(); ++i) {
        s[i] += static_cast<float>(std::sin(ph)) * amp;
        ph += 6.28318530718 * freq / rate;
        if (ph > 6.28318530718) ph -= 6.28318530718;
    }
}

TEST_CASE("MFSK type, sample rate, bits per second", "[mftsk]") {
    MFSKModem modem = makeModem();
    REQUIRE(modem.type() == ModemType::MFSK);
    REQUIRE(modem.sampleRate() == 44100);
    REQUIRE(modem.bitsPerSecond() == 100);  // 50 sym/s * 2 bit
}

TEST_CASE("MFSK clean self-loop recovers all bits", "[mftsk]") {
    MFSKModem modem = makeModem();
    std::vector<uint8_t> orig = makeBits(400);
    std::vector<float> samples = modem.modulate(orig);

    REQUIRE(!samples.empty());

    std::vector<uint8_t> rec = modem.demodulate(samples);
    REQUIRE(rec.size() == orig.size());
    for (size_t i = 0; i < orig.size(); ++i) {
        INFO("bit " << i);
        REQUIRE(rec[i] == orig[i]);
    }
}

TEST_CASE("MFSK survives white noise", "[mftsk]") {
    MFSKModem modem = makeModem();
    std::vector<uint8_t> orig = makeBits(400);
    std::vector<float> samples = modem.modulate(orig);
    addNoise(samples, 0.10f, 42);

    std::vector<uint8_t> rec = modem.demodulate(samples);
    REQUIRE(rec == orig);
}

TEST_CASE("MFSK survives single-frequency interference", "[mftsk]") {
    MFSKModem modem = makeModem();
    std::vector<uint8_t> orig = makeBits(400);
    // A strong narrowband tone exactly on one of the used slots (1550 Hz).
    std::vector<float> samples = modem.modulate(orig);
    addTone(samples, 1550.0f, 0.3f, 44100);

    std::vector<uint8_t> rec = modem.demodulate(samples);
    REQUIRE(rec == orig);
}

TEST_CASE("MFSK pipeline: text TX/RX roundtrip", "[mftsk]") {
    PipelineConfig cfg;
    cfg.modemType = ModemType::MFSK;
    cfg.fecType = FecType::HAMMING;

    TxPipeline tx(cfg);
    const std::string msg = "Hello M-FSK pipeline test 12345!";
    std::vector<float> audio = tx.encodeTypedText(msg);

    RxPipeline rx(cfg);
    DecodeResult res = rx.decode(audio, true);

    REQUIRE(res.valid());
    REQUIRE(res.type == DecodeResult::TEXT);
    REQUIRE(res.text == msg);
}

TEST_CASE("MFSK pipeline: threshold tolerance to leading silence", "[mftsk]") {
    // Leading silence (as a tape/speaker path would have) must not break
    // the M-FSK receiver's own preamble sync.
    PipelineConfig cfg;
    cfg.modemType = ModemType::MFSK;
    cfg.fecType = FecType::NONE;

    TxPipeline tx(cfg);
    const std::string msg = "sync-check";
    std::vector<float> audio = tx.encodeTypedText(msg);

    // Prepend ~0.5 s of silence (44100 * 0.5).
    std::vector<float> padded(44100 / 2, 0.0f);
    padded.insert(padded.end(), audio.begin(), audio.end());

    RxPipeline rx(cfg);
    DecodeResult res = rx.decode(padded, true);

    REQUIRE(res.valid());
    REQUIRE(res.type == DecodeResult::TEXT);
    REQUIRE(res.text == msg);
}
