// test_reed_solomon.cpp - Reed-Solomon FEC tests
#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>
#include <fec/reed_solomon.h>

using namespace audiot;

TEST_CASE("ReedSolomon clean codeword decodes without correction", "[fec][rs]") {
    std::vector<uint8_t> data(239);
    for (int i = 0; i < 239; ++i) data[i] = static_cast<uint8_t>(i * 7 + 3);

    ReedSolomon rs(8);
    std::vector<uint8_t> enc = rs.encode(data);

    ReedSolomon::DecodeResult r = rs.decode(enc);
    REQUIRE(r.success);
    REQUIRE(r.corrected_errors == 0);
    REQUIRE(r.data == data);
}

TEST_CASE("ReedSolomon corrects a single symbol error", "[fec][rs]") {
    std::vector<uint8_t> data(239);
    for (int i = 0; i < 239; ++i) data[i] = static_cast<uint8_t>(i * 7 + 3);

    ReedSolomon rs(8);
    std::vector<uint8_t> enc = rs.encode(data);

    // Corrupt one byte in the data region
    std::vector<uint8_t> corrupted = enc;
    corrupted[100] ^= 0x5A;

    ReedSolomon::DecodeResult r = rs.decode(corrupted);
    REQUIRE(r.success);
    REQUIRE(r.corrected_errors == 1);
    REQUIRE(r.data == data);
}

TEST_CASE("ReedSolomon corrects errors up to t", "[fec][rs]") {
    std::vector<uint8_t> data(239);
    for (int i = 0; i < 239; ++i) data[i] = static_cast<uint8_t>(i * 11 + 7);

    ReedSolomon rs(8);  // t = 8, corrects up to 8 symbol errors
    std::vector<uint8_t> enc = rs.encode(data);

    // Corrupt 8 bytes spread across data and parity regions
    std::vector<uint8_t> corrupted = enc;
    const int positions[] = {0, 1, 100, 200, 254, 128, 64, 32};
    const uint8_t values[] = {0xFF, 0x01, 0x5A, 0xA5, 0x0F, 0x33, 0xCC, 0x77};
    for (int k = 0; k < 8; ++k) {
        corrupted[positions[k]] ^= values[k];
    }

    ReedSolomon::DecodeResult r = rs.decode(corrupted);
    REQUIRE(r.success);
    REQUIRE(r.corrected_errors == 8);
    REQUIRE(r.data == data);
}

TEST_CASE("ReedSolomon rejects more errors than t can correct", "[fec][rs]") {
    std::vector<uint8_t> data(239);
    for (int i = 0; i < 239; ++i) data[i] = static_cast<uint8_t>(i * 3 + 1);

    ReedSolomon rs(8);
    std::vector<uint8_t> enc = rs.encode(data);

    // Corrupt 12 bytes: far beyond t = 8
    std::vector<uint8_t> corrupted = enc;
    for (int k = 0; k < 12; ++k) {
        corrupted[k * 21 % 255] ^= static_cast<uint8_t>(0x40 + k);
    }

    ReedSolomon::DecodeResult r = rs.decode(corrupted);
    REQUIRE(!r.success);
    REQUIRE(r.uncorrectable > 0);
}

TEST_CASE("ReedSolomon basic encode produces output", "[fec][rs]") {
    ReedSolomon rs(4); // t=4, can correct up to 4 symbol errors

    std::vector<uint8_t> data = {0x48, 0x65, 0x6C, 0x6C, 0x6F}; // "Hello"

    std::vector<uint8_t> encoded = rs.encode(data);

    // Encoded should be data + 2*t parity bytes
    REQUIRE(encoded.size() == data.size() + 2 * 4);
}

TEST_CASE("ReedSolomon parity bytes count", "[fec][rs]") {
    ReedSolomon rs(8);
    REQUIRE(rs.parityBytes() == 16); // 2*t = 2*8 = 16

    ReedSolomon rs2(4);
    REQUIRE(rs2.parityBytes() == 8); // 2*t = 2*4 = 8
}

TEST_CASE("ReedSolomon empty data encoding", "[fec][rs]") {
    ReedSolomon rs(4);

    std::vector<uint8_t> data;
    std::vector<uint8_t> encoded = rs.encode(data);

    // Should just have parity bytes
    REQUIRE(encoded.size() == 2 * 4);
}

TEST_CASE("ReedSolomon encode does not modify input", "[fec][rs]") {
    ReedSolomon rs(4);

    std::vector<uint8_t> data = {0x01, 0x02, 0x03, 0x04};
    std::vector<uint8_t> original = data;

    std::vector<uint8_t> encoded = rs.encode(data);

    // Original data should be unchanged
    REQUIRE(data == original);
    
    // Encoded should start with original data (systematic encoding)
    REQUIRE(encoded.size() >= data.size());
    for (size_t i = 0; i < data.size(); ++i) {
        REQUIRE(encoded[i] == data[i]);
    }
}

TEST_CASE("ReedSolomon different t values produce different output sizes", "[fec][rs]") {
    std::vector<uint8_t> data = {0x01, 0x02, 0x03};

    ReedSolomon rs2(2);
    ReedSolomon rs4(4);
    ReedSolomon rs8(8);

    auto enc2 = rs2.encode(data);
    auto enc4 = rs4.encode(data);
    auto enc8 = rs8.encode(data);

    REQUIRE(enc2.size() == data.size() + 4);  // 2*2 = 4 parity bytes
    REQUIRE(enc4.size() == data.size() + 8);  // 2*4 = 8 parity bytes
    REQUIRE(enc8.size() == data.size() + 16); // 2*8 = 16 parity bytes
}
