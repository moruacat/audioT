// test_wav.cpp - WAV reader/writer tests
#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>
#include <audio/wav_writer.h>
#include <audio/wav_reader.h>
#include <audio/audio_format.h>
#include <cmath>
#include <cstdio>

using namespace audiot;

TEST_CASE("WAV write and read roundtrip", "[wav]") {
    const char* tempFile = "test_wav_roundtrip.wav";

    // Write
    {
        AudioFormat fmt(44100, 1, 16);
        WavWriter writer;
        REQUIRE(writer.open(tempFile, fmt));

        // Generate a simple sine wave
        std::vector<float> samples(4410); // 0.1 second
        for (size_t i = 0; i < samples.size(); ++i) {
            samples[i] = 0.5f * std::sin(2.0f * 3.14159f * 440.0f * static_cast<float>(i) / 44100.0f);
        }

        writer.write(samples);
        writer.close();
    }

    // Read back
    {
        WavReader reader;
        REQUIRE(reader.open(tempFile));

        AudioFormat fmt = reader.format();
        REQUIRE(fmt.sampleRate == 44100);
        REQUIRE(fmt.channels == 1);
        REQUIRE(fmt.bitsPerSample == 16);

        REQUIRE(reader.totalSamples() == 4410);

        std::vector<float> samples = reader.readAll();
        REQUIRE(samples.size() == 4410);

        // Verify samples are approximately correct (16-bit quantization)
        for (size_t i = 0; i < samples.size(); ++i) {
            float expected = 0.5f * std::sin(2.0f * 3.14159f * 440.0f * static_cast<float>(i) / 44100.0f);
            REQUIRE(samples[i] == Approx(expected).margin(0.001f));
        }

        reader.close();
    }

    // Cleanup
    std::remove(tempFile);
}

TEST_CASE("WAV format parameters preserved", "[wav]") {
    const char* tempFile = "test_wav_format.wav";

    AudioFormat fmt(48000, 1, 16);
    WavWriter writer;
    REQUIRE(writer.open(tempFile, fmt));

    std::vector<float> samples(4800);
    for (size_t i = 0; i < samples.size(); ++i) {
        samples[i] = 0.0f;
    }
    writer.write(samples);
    writer.close();

    WavReader reader;
    REQUIRE(reader.open(tempFile));

    AudioFormat readFmt = reader.format();
    REQUIRE(readFmt.sampleRate == 48000);
    REQUIRE(readFmt.channels == 1);
    REQUIRE(readFmt.bitsPerSample == 16);
    REQUIRE(reader.totalSamples() == 4800);

    reader.close();
    std::remove(tempFile);
}

TEST_CASE("WAV reader fails on non-existent file", "[wav]") {
    WavReader reader;
    REQUIRE(!reader.open("nonexistent_file.wav"));
}

TEST_CASE("WAV writer and reader handle silence", "[wav]") {
    const char* tempFile = "test_wav_silence.wav";

    {
        AudioFormat fmt(44100, 1, 16);
        WavWriter writer;
        REQUIRE(writer.open(tempFile, fmt));

        std::vector<float> silence(4410, 0.0f);
        writer.write(silence);
        writer.close();
    }

    {
        WavReader reader;
        REQUIRE(reader.open(tempFile));

        std::vector<float> samples = reader.readAll();
        REQUIRE(samples.size() == 4410);

        for (size_t i = 0; i < samples.size(); ++i) {
            REQUIRE(samples[i] == Approx(0.0f).margin(0.0001f));
        }

        reader.close();
    }

    std::remove(tempFile);
}
