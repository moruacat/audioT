// test_tone.cpp - Unit tests for TxPipeline::makeTone (prep tone generator)
#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>
#include <pipeline/tx_pipeline.h>
#include <pipeline/pipeline_config.h>
#include <cmath>
#include <algorithm>

using namespace audiot;

TEST_CASE("makeTone: sample count matches duration", "[tone]") {
    std::vector<float> tone = TxPipeline::makeTone(1200.0f, 0.5f, 44100);
    REQUIRE(tone.size() == 22050);

    // 5-second prep tone at default config sample rate
    PipelineConfig cfg;
    std::vector<float> prep = TxPipeline::makeTone(cfg.markFreq, cfg.prepToneSeconds, cfg.sampleRate);
    REQUIRE(prep.size() == static_cast<size_t>(cfg.prepToneSeconds * cfg.sampleRate));
}

TEST_CASE("makeTone: default amplitude is 0.5", "[tone]") {
    std::vector<float> tone = TxPipeline::makeTone(1200.0f, 0.1f, 44100);
    REQUIRE(!tone.empty());

    float maxAbs = 0.0f;
    for (float s : tone) {
        maxAbs = std::max(maxAbs, std::fabs(s));
    }
    // Peak should be very close to the default amplitude 0.5
    REQUIRE(maxAbs == Approx(0.5f).epsilon(0.01));
    // No clipping / NaN
    for (float s : tone) {
        REQUIRE(std::fabs(s) <= 0.501f);
        REQUIRE(!std::isnan(s));
    }
}

TEST_CASE("makeTone: custom amplitude is respected", "[tone]") {
    std::vector<float> tone = TxPipeline::makeTone(1200.0f, 0.1f, 44100, 0.9f);
    float maxAbs = 0.0f;
    for (float s : tone) {
        maxAbs = std::max(maxAbs, std::fabs(s));
    }
    REQUIRE(maxAbs == Approx(0.9f).epsilon(0.01));
}

TEST_CASE("makeTone: frequency via sign-change count", "[tone]") {
    // A pure sine of f Hz crosses zero 2*f times per second.
    const float freq = 1200.0f;
    const int sampleRate = 44100;
    std::vector<float> tone = TxPipeline::makeTone(freq, 1.0f, sampleRate);
    REQUIRE(tone.size() == static_cast<size_t>(sampleRate));

    int signChanges = 0;
    for (size_t i = 1; i < tone.size(); ++i) {
        if ((tone[i - 1] < 0.0f && tone[i] >= 0.0f) ||
            (tone[i - 1] > 0.0f && tone[i] <= 0.0f)) {
            ++signChanges;
        }
    }
    // Expect ~2400 zero crossings for 1200 Hz; allow +-2 for edge effects
    REQUIRE(signChanges >= 2398);
    REQUIRE(signChanges <= 2402);
}

TEST_CASE("makeTone: starts at phase zero and is phase-continuous", "[tone]") {
    std::vector<float> tone = TxPipeline::makeTone(1200.0f, 0.05f, 44100);
    REQUIRE(tone.size() > 2);
    // sin(0) == 0
    REQUIRE(std::fabs(tone[0]) < 1e-6f);
    // First samples must be rising (positive derivative at phase 0)
    REQUIRE(tone[1] > tone[0]);

    // Phase continuity: adjacent-sample difference bounded by the step size
    const float maxStep = 2.0f * 3.14159265f * 1200.0f / 44100.0f * 0.5f * 1.01f;
    for (size_t i = 1; i < tone.size(); ++i) {
        REQUIRE(std::fabs(tone[i] - tone[i - 1]) <= maxStep + 1e-6f);
    }
}

TEST_CASE("makeTone: invalid parameters yield empty output", "[tone]") {
    REQUIRE(TxPipeline::makeTone(0.0f, 1.0f, 44100).empty());
    REQUIRE(TxPipeline::makeTone(-1200.0f, 1.0f, 44100).empty());
    REQUIRE(TxPipeline::makeTone(1200.0f, 0.0f, 44100).empty());
    REQUIRE(TxPipeline::makeTone(1200.0f, -1.0f, 44100).empty());
    REQUIRE(TxPipeline::makeTone(1200.0f, 1.0f, 0).empty());
    REQUIRE(TxPipeline::makeTone(1200.0f, 1.0f, -1).empty());
}

TEST_CASE("makeTone: works at all supported pipeline sample rates", "[tone]") {
    const int rates[] = { 22050, 44100, 48000 };
    for (int rate : rates) {
        std::vector<float> tone = TxPipeline::makeTone(1200.0f, 1.0f, rate);
        REQUIRE(tone.size() == static_cast<size_t>(rate));
    }
}

TEST_CASE("PipelineConfig: prepToneSeconds default", "[pipeline]") {
    PipelineConfig cfg;
    REQUIRE(cfg.prepToneSeconds == 5.0f);
}

TEST_CASE("PipelineConfig: endControlSeconds default", "[pipeline]") {
    PipelineConfig cfg;
    REQUIRE(cfg.endControlSeconds == 1.0f);
}

TEST_CASE("makeSilence: sample count matches duration", "[tone]") {
    std::vector<float> sil = TxPipeline::makeSilence(1.0f, 44100);
    REQUIRE(sil.size() == 44100);

    // Default end-of-frame control marker length at default sample rate
    PipelineConfig cfg;
    std::vector<float> endMarker =
        TxPipeline::makeSilence(cfg.endControlSeconds, cfg.sampleRate);
    REQUIRE(endMarker.size() ==
            static_cast<size_t>(cfg.endControlSeconds * cfg.sampleRate));
}

TEST_CASE("makeSilence: all samples are exactly zero", "[tone]") {
    std::vector<float> sil = TxPipeline::makeSilence(0.25f, 48000);
    REQUIRE(!sil.empty());
    for (float s : sil) {
        REQUIRE(s == 0.0f);
    }
}

TEST_CASE("makeSilence: invalid parameters yield empty output", "[tone]") {
    REQUIRE(TxPipeline::makeSilence(0.0f, 44100).empty());
    REQUIRE(TxPipeline::makeSilence(-1.0f, 44100).empty());
    REQUIRE(TxPipeline::makeSilence(1.0f, 0).empty());
    REQUIRE(TxPipeline::makeSilence(1.0f, -1).empty());
}

TEST_CASE("makeSilence: works at all supported pipeline sample rates", "[tone]") {
    const int rates[] = { 22050, 44100, 48000 };
    for (int rate : rates) {
        std::vector<float> sil = TxPipeline::makeSilence(1.0f, rate);
        REQUIRE(sil.size() == static_cast<size_t>(rate));
    }
}
