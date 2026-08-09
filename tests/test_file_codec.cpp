// test_file_codec.cpp - FileCodec tests
#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>
#include <codec/file_codec.h>

using namespace audiot;

TEST_CASE("FileCodec encodeData/decodeFrame roundtrip", "[codec][file]") {
    std::string filename = "test.txt";
    std::vector<uint8_t> data = {0x48, 0x65, 0x6C, 0x6C, 0x6F}; // "Hello"

    std::vector<uint8_t> frame = FileCodec::encodeData(filename, data, FileCodec::FileType::TEXT);

    FileCodec::DecodedFile result = FileCodec::decodeFrame(frame);

    REQUIRE(result.valid == true);
    REQUIRE(result.filename == filename);
    REQUIRE(result.type == FileCodec::FileType::TEXT);
    REQUIRE(result.data == data);
}

TEST_CASE("FileCodec frame structure has sync bytes", "[codec][file]") {
    std::string filename = "data.bin";
    std::vector<uint8_t> data = {0x01, 0x02, 0x03};

    std::vector<uint8_t> frame = FileCodec::encodeData(filename, data);

    // Check sync bytes (0xAA, 0x55)
    REQUIRE(frame.size() >= 2);
    REQUIRE(frame[0] == 0xAA);
    REQUIRE(frame[1] == 0x55);
}

TEST_CASE("FileCodec frame has valid CRC", "[codec][file]") {
    std::string filename = "crc_test.dat";
    std::vector<uint8_t> data = {0xAA, 0xBB, 0xCC, 0xDD};

    std::vector<uint8_t> frame = FileCodec::encodeData(filename, data);

    // Decode should succeed (which verifies CRC)
    FileCodec::DecodedFile result = FileCodec::decodeFrame(frame);
    REQUIRE(result.valid == true);

    // Corrupt a byte and verify CRC fails
    std::vector<uint8_t> corrupted = frame;
    corrupted[5] ^= 0xFF; // Corrupt data area

    FileCodec::DecodedFile corruptResult = FileCodec::decodeFrame(corrupted);
    REQUIRE(corruptResult.valid == false);
}

TEST_CASE("FileCodec empty data", "[codec][file]") {
    std::string filename = "empty.txt";
    std::vector<uint8_t> data;

    std::vector<uint8_t> frame = FileCodec::encodeData(filename, data, FileCodec::FileType::TEXT);

    FileCodec::DecodedFile result = FileCodec::decodeFrame(frame);

    REQUIRE(result.valid == true);
    REQUIRE(result.filename == filename);
    REQUIRE(result.data.empty());
}

TEST_CASE("FileCodec invalid sync rejected", "[codec][file]") {
    std::vector<uint8_t> badFrame = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                      0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    FileCodec::DecodedFile result = FileCodec::decodeFrame(badFrame);
    REQUIRE(result.valid == false);
}

TEST_CASE("FileCodec too small frame rejected", "[codec][file]") {
    std::vector<uint8_t> tinyFrame = {0xAA, 0x55};

    FileCodec::DecodedFile result = FileCodec::decodeFrame(tinyFrame);
    REQUIRE(result.valid == false);
}

TEST_CASE("FileCodec file types preserved", "[codec][file]") {
    std::string filename = "test";
    std::vector<uint8_t> data = {0x01};

    SECTION("GENERIC type") {
        auto frame = FileCodec::encodeData(filename, data, FileCodec::FileType::GENERIC);
        auto result = FileCodec::decodeFrame(frame);
        REQUIRE(result.valid == true);
        REQUIRE(result.type == FileCodec::FileType::GENERIC);
    }

    SECTION("TEXT type") {
        auto frame = FileCodec::encodeData(filename, data, FileCodec::FileType::TEXT);
        auto result = FileCodec::decodeFrame(frame);
        REQUIRE(result.valid == true);
        REQUIRE(result.type == FileCodec::FileType::TEXT);
    }

    SECTION("IMAGE type") {
        auto frame = FileCodec::encodeData(filename, data, FileCodec::FileType::IMAGE);
        auto result = FileCodec::decodeFrame(frame);
        REQUIRE(result.valid == true);
        REQUIRE(result.type == FileCodec::FileType::IMAGE);
    }
}
