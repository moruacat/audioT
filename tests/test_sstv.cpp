// test_sstv.cpp - SSTV protocol and codec roundtrip tests
#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>
#include <sstv/sstv_protocols.h>
#include <sstv/sstv_timing.h>
#include <sstv/sstv_encoder.h>
#include <sstv/sstv_decoder.h>

#include <vector>
#include <cstdint>
#include <cmath>

using namespace audiot;

// ----------------------------------------------------------------
// Protocol table
// ----------------------------------------------------------------

TEST_CASE("SSTV VIS codes match specification", "[sstv][protocol]") {
    REQUIRE(getProtocolInfo(SSTVMode::Robot36).visCode == 8);
    REQUIRE(getProtocolInfo(SSTVMode::Robot72).visCode == 12);
    REQUIRE(getProtocolInfo(SSTVMode::ScottieS1).visCode == 60);
    REQUIRE(getProtocolInfo(SSTVMode::ScottieS2).visCode == 56);
    REQUIRE(getProtocolInfo(SSTVMode::ScottieDX).visCode == 76);
    REQUIRE(getProtocolInfo(SSTVMode::MartinM1).visCode == 44);
    REQUIRE(getProtocolInfo(SSTVMode::MartinM2).visCode == 40);
    REQUIRE(getProtocolInfo(SSTVMode::PD50).visCode == 82);
    REQUIRE(getProtocolInfo(SSTVMode::PD120).visCode == 97);
}

TEST_CASE("SSTV modeFromVISCode roundtrip", "[sstv][protocol]") {
    const SSTVMode modes[] = {
        SSTVMode::Robot36, SSTVMode::Robot72, SSTVMode::ScottieS1,
        SSTVMode::ScottieS2, SSTVMode::ScottieDX, SSTVMode::MartinM1,
        SSTVMode::MartinM2, SSTVMode::PD50, SSTVMode::PD120 };

    for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i) {
        const SSTVProtocolInfo& info = getProtocolInfo(modes[i]);
        REQUIRE(info.mode == modes[i]);
        REQUIRE(modeFromVISCode(info.visCode) == modes[i]);
        REQUIRE_FALSE(info.name.empty());
        REQUIRE(info.width > 0);
        REQUIRE(info.height > 0);
        REQUIRE(info.syncFreq == Approx(1200.0));
        REQUIRE(info.blackFreq == Approx(1500.0));
        REQUIRE(info.whiteFreq == Approx(2300.0));
        REQUIRE(info.colorComponents == 3);
    }
}

TEST_CASE("SSTV timing constants", "[sstv][protocol]") {
    REQUIRE(sstv::LEADER_FREQ_HZ == Approx(1900.0));
    REQUIRE(sstv::VIS_BIT_ONE_HZ == Approx(1100.0));
    REQUIRE(sstv::VIS_BIT_ZERO_HZ == Approx(1300.0));
    REQUIRE(sstv::BLACK_FREQ_HZ == Approx(1500.0));
    REQUIRE(sstv::WHITE_FREQ_HZ == Approx(2300.0));
}

// ----------------------------------------------------------------
// Encoder
// ----------------------------------------------------------------

TEST_CASE("SSTV encoder rejects invalid input", "[sstv][encoder]") {
    SSTVEncoder enc;
    REQUIRE(enc.encode(nullptr, 320, 256, 44100).empty());

    std::vector<uint8_t> pixels(32 * 32 * 3, 128);
    REQUIRE(enc.encode(pixels.data(), 0, 32, 44100).empty());
    REQUIRE(enc.encode(pixels.data(), 32, 32, 0).empty());
}

TEST_CASE("SSTV encoder output duration matches expectation", "[sstv][encoder]") {
    SSTVEncoder enc;
    enc.setMode(SSTVMode::Robot36);

    const SSTVProtocolInfo& info = getProtocolInfo(SSTVMode::Robot36);
    std::vector<uint8_t> pixels(
        static_cast<size_t>(info.width) * info.height * 3, 128);

    const int sampleRate = 44100;
    const std::vector<float> audio =
        enc.encode(pixels.data(), info.width, info.height, sampleRate);

    const double expected = SSTVEncoder::frameDuration(SSTVMode::Robot36);
    const double actual = static_cast<double>(audio.size()) / sampleRate;
    REQUIRE(actual == Approx(expected).epsilon(0.01));
}

// ----------------------------------------------------------------
// Detection + roundtrip
// ----------------------------------------------------------------

TEST_CASE("SSTV header detection finds mode and position", "[sstv][decoder]") {
    SSTVEncoder enc;
    enc.setMode(SSTVMode::MartinM2);

    const SSTVProtocolInfo& info = getProtocolInfo(SSTVMode::MartinM2);
    std::vector<uint8_t> pixels(
        static_cast<size_t>(info.width) * info.height * 3, 64);

    const int sampleRate = 44100;
    const std::vector<float> audio =
        enc.encode(pixels.data(), info.width, info.height, sampleRate);

    // Prepend silence to exercise header search
    std::vector<float> shifted(4000, 0.0f);
    shifted.insert(shifted.end(), audio.begin(), audio.end());

    SSTVDecoder dec;
    SSTVDecoder::DetectionResult det = dec.detect(shifted, sampleRate);

    REQUIRE(det.found);
    REQUIRE(det.mode == SSTVMode::MartinM2);
    REQUIRE(det.visCode == getProtocolInfo(SSTVMode::MartinM2).visCode);
    REQUIRE(det.leaderStart >= 4000);
    REQUIRE(det.leaderStart <= 4100);
    REQUIRE(det.imageStart > det.leaderStart);
}

TEST_CASE("SSTV detection fails on silence", "[sstv][decoder]") {
    SSTVDecoder dec;
    std::vector<float> silence(44100, 0.0f);
    SSTVDecoder::DetectionResult det = dec.detect(silence, 44100);
    REQUIRE_FALSE(det.found);
}

TEST_CASE("SSTV encode/decode roundtrip: horizontal gradient",
          "[sstv][roundtrip]") {
    SSTVEncoder enc;
    enc.setMode(SSTVMode::Robot36);

    const SSTVProtocolInfo& info = getProtocolInfo(SSTVMode::Robot36);
    const int w = info.width;
    const int h = info.height;

    // Smooth horizontal gray ramp (identical rows)
    std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * 3);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const uint8_t lum =
                static_cast<uint8_t>(x * 255 / (w - 1));
            const size_t idx = (static_cast<size_t>(y) * w + x) * 3;
            pixels[idx + 0] = lum;
            pixels[idx + 1] = lum;
            pixels[idx + 2] = lum;
        }
    }

    const int sampleRate = 44100;
    const std::vector<float> audio = enc.encode(pixels.data(), w, h, sampleRate);
    REQUIRE_FALSE(audio.empty());

    SSTVDecoder dec;
    SSTVDecoder::DecodeResult res = dec.decode(audio, sampleRate);

    REQUIRE(res.success);
    REQUIRE(res.mode == SSTVMode::Robot36);
    REQUIRE(res.width == w);
    REQUIRE(res.height == h);
    REQUIRE(res.pixels.size() == pixels.size());

    // Compare center region (avoid ramp edges affected by filter kernel)
    double maxErr = 0.0;
    double sumErr = 0.0;
    size_t count = 0;
    for (int y = 2; y < h - 2; ++y) {
        for (int x = 8; x < w - 8; ++x) {
            const size_t idx = (static_cast<size_t>(y) * w + x) * 3;
            const double e = std::fabs(
                static_cast<double>(res.pixels[idx]) - pixels[idx]);
            if (e > maxErr) maxErr = e;
            sumErr += e;
            ++count;
        }
    }
    const double meanErr = sumErr / count;
    REQUIRE(meanErr < 3.0);
    REQUIRE(maxErr < 8.0);
}

TEST_CASE("SSTV encode/decode roundtrip: uniform gray", "[sstv][roundtrip]") {
    SSTVEncoder enc;
    enc.setMode(SSTVMode::Robot36);

    const SSTVProtocolInfo& info = getProtocolInfo(SSTVMode::Robot36);
    const int w = info.width;
    const int h = info.height;

    std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * 3, 128);

    const int sampleRate = 44100;
    const std::vector<float> audio = enc.encode(pixels.data(), w, h, sampleRate);

    SSTVDecoder dec;
    SSTVDecoder::DecodeResult res = dec.decode(audio, sampleRate);

    REQUIRE(res.success);

    // Compare the interior region (demodulator filter has edge transients
    // on the first/last pixels of each line)
    double maxErr = 0.0;
    double sumErr = 0.0;
    size_t count = 0;
    for (int y = 2; y < h - 2; ++y) {
        for (int x = 8; x < w - 8; ++x) {
            const size_t idx = (static_cast<size_t>(y) * w + x) * 3;
            const double e =
                std::fabs(static_cast<double>(res.pixels[idx]) - 128.0);
            if (e > maxErr) maxErr = e;
            sumErr += e;
            ++count;
        }
    }
    REQUIRE(sumErr / count < 2.0);
    REQUIRE(maxErr < 8.0);
}

TEST_CASE("SSTV encode/decode roundtrip: colour solid primaries", "[sstv][roundtrip][colour]") {
    // Standard colour SSTV: each line carries R-Y, B-Y, Y scan segments.
    // A solid primary must round-trip to (nearly) the same RGB.
    SSTVEncoder enc;
    enc.setMode(SSTVMode::MartinM1);

    const SSTVProtocolInfo& info = getProtocolInfo(SSTVMode::MartinM1);
    const int w = info.width;
    const int h = info.height;
    const int sampleRate = 44100;

    struct { int r, g, b; const char* name; } colours[] = {
        {255, 0, 0,   "red"},
        {0, 255, 0,   "green"},
        {0, 0, 255,   "blue"},
        {255, 255, 0, "yellow"},
    };

    for (const auto& c : colours) {
        std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * 3);
        for (size_t i = 0; i < pixels.size(); i += 3) {
            pixels[i] = static_cast<uint8_t>(c.r);
            pixels[i+1] = static_cast<uint8_t>(c.g);
            pixels[i+2] = static_cast<uint8_t>(c.b);
        }
        const std::vector<float> audio = enc.encode(pixels.data(), w, h, sampleRate);
        SSTVDecoder dec;
        SSTVDecoder::DecodeResult res = dec.decode(audio, sampleRate);
        REQUIRE(res.success);

        // Evaluate a central pixel (filter settles away from line edges).
        const int x = w / 2, y = h / 2;
        const size_t idx = (static_cast<size_t>(y) * w + x) * 3;
        INFO("colour " << c.name);
        REQUIRE(std::abs(static_cast<int>(res.pixels[idx])   - c.r) <= 3);
        REQUIRE(std::abs(static_cast<int>(res.pixels[idx+1]) - c.g) <= 3);
        REQUIRE(std::abs(static_cast<int>(res.pixels[idx+2]) - c.b) <= 3);
    }
}

TEST_CASE("SSTV encode/decode roundtrip: frame duration exact", "[sstv][roundtrip]") {
    // The per-line colour segments must not drift the total frame length.
    SSTVEncoder enc;
    enc.setMode(SSTVMode::Robot36);
    const SSTVProtocolInfo& info = getProtocolInfo(SSTVMode::Robot36);
    const int w = info.width, h = info.height;
    std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * 3, 128);
    const int sampleRate = 44100;
    const std::vector<float> audio = enc.encode(pixels.data(), w, h, sampleRate);
    const double actual = static_cast<double>(audio.size()) / sampleRate;
    REQUIRE(actual == Approx(SSTVEncoder::frameDuration(SSTVMode::Robot36)).epsilon(0.01));
}
