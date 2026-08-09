// test_fec.cpp - Hamming FEC tests
#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>
#include <fec/hamming.h>

using namespace audiot;

TEST_CASE("Hamming encode/decode roundtrip", "[fec]") {
    // 8 data bits (2 groups of 4)
    std::vector<uint8_t> data = {1, 0, 1, 1, 0, 1, 0, 1};

    std::vector<uint8_t> encoded = HammingCodec::encode(data);

    // Each 4 bits -> 7 bits, so 8 bits -> 14 bits
    REQUIRE(encoded.size() == 14);

    HammingCodec::DecodeResult result = HammingCodec::decode(encoded);

    REQUIRE(result.data.size() == data.size());
    REQUIRE(result.corrected_errors == 0);
    REQUIRE(result.uncorrectable == 0);

    for (size_t i = 0; i < data.size(); ++i) {
        REQUIRE(result.data[i] == data[i]);
    }
}

TEST_CASE("Hamming corrects single-bit error", "[fec]") {
    std::vector<uint8_t> data = {1, 1, 0, 0};

    std::vector<uint8_t> encoded = HammingCodec::encode(data);
    REQUIRE(encoded.size() == 7);

    // Introduce a single-bit error at position 2
    std::vector<uint8_t> corrupted = encoded;
    corrupted[2] ^= 1;

    HammingCodec::DecodeResult result = HammingCodec::decode(corrupted);

    // Should correct the error
    REQUIRE(result.corrected_errors == 1);
    REQUIRE(result.data.size() == 4);

    for (size_t i = 0; i < data.size(); ++i) {
        REQUIRE(result.data[i] == data[i]);
    }
}

TEST_CASE("Hamming encode all zeros", "[fec]") {
    std::vector<uint8_t> data = {0, 0, 0, 0};
    std::vector<uint8_t> encoded = HammingCodec::encode(data);

    REQUIRE(encoded.size() == 7);
    // All zeros should encode to all zeros
    for (size_t i = 0; i < 7; ++i) {
        REQUIRE(encoded[i] == 0);
    }
}

TEST_CASE("Hamming encode all ones", "[fec]") {
    std::vector<uint8_t> data = {1, 1, 1, 1};
    std::vector<uint8_t> encoded = HammingCodec::encode(data);

    REQUIRE(encoded.size() == 7);

    HammingCodec::DecodeResult result = HammingCodec::decode(encoded);
    REQUIRE(result.data.size() == 4);
    for (size_t i = 0; i < 4; ++i) {
        REQUIRE(result.data[i] == 1);
    }
}

TEST_CASE("Hamming handles multiple groups", "[fec]") {
    // 12 bits = 3 groups of 4
    std::vector<uint8_t> data = {1, 0, 1, 0, 0, 1, 0, 1, 1, 1, 0, 0};

    std::vector<uint8_t> encoded = HammingCodec::encode(data);
    REQUIRE(encoded.size() == 21); // 3 * 7

    HammingCodec::DecodeResult result = HammingCodec::decode(encoded);
    REQUIRE(result.data.size() == 12);

    for (size_t i = 0; i < data.size(); ++i) {
        REQUIRE(result.data[i] == data[i]);
    }
}
