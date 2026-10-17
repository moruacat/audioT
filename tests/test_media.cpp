// test_media.cpp - 图片/视频数字帧传输测试
#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>
#include <pipeline/pipeline_config.h>
#include <pipeline/tx_pipeline.h>
#include <pipeline/rx_pipeline.h>

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace audiot;

namespace {
// 写一个临时文件（二进制），返回文件路径（相对名，不含目录分隔符）
std::string writeTempFile(const std::string& name, const std::vector<uint8_t>& bytes) {
    std::ofstream f(name.c_str(), std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    f.close();
    return name;
}

// 伪 PNG 头 + 若干字节，够验证"按字节无损还原"即可
std::vector<uint8_t> fakeImageBytes() {
    std::vector<uint8_t> b = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    for (int i = 0; i < 40; ++i) b.push_back(static_cast<uint8_t>((i * 37 + 11) & 0xFF));
    return b;
}

std::vector<uint8_t> fakeVideoBytes() {
    std::vector<uint8_t> b;
    for (int i = 0; i < 60; ++i) b.push_back(static_cast<uint8_t>((i * 19 + 7) & 0xFF));
    return b;
}
} // namespace

TEST_CASE("image payload round trip through pipeline (AFSK)", "[media][pipeline]") {
    const std::vector<uint8_t> png = fakeImageBytes();
    const std::string path = writeTempFile("audiot_tmp_img.png", png);

    PipelineConfig cfg;              // AFSK 默认
    cfg.autoDetectModem = false;

    TxPipeline tx(cfg);
    const std::vector<float> samples = tx.encodeTypedImage(path);
    REQUIRE(!samples.empty());

    RxPipeline rx(cfg);
    const DecodeResult r = rx.decode(samples, true);
    REQUIRE(r.valid());
    REQUIRE(r.type == DecodeResult::IMAGE);
    REQUIRE(r.filename == "audiot_tmp_img.png");
    REQUIRE(r.fileData == png);

    std::remove(path.c_str());
}

TEST_CASE("video payload round trip through pipeline (differential PSK)", "[media][pipeline]") {
    const std::vector<uint8_t> mp4 = fakeVideoBytes();
    const std::string path = writeTempFile("audiot_tmp_clip.mp4", mp4);

    PipelineConfig cfg;
    cfg.modemType = ModemType::PSK;   // 验证图片/视频链路同样走新调制
    cfg.pskOrder = 2;
    cfg.autoDetectModem = false;

    TxPipeline tx(cfg);
    const std::vector<float> samples = tx.encodeTypedVideo(path);
    REQUIRE(!samples.empty());

    RxPipeline rx(cfg);
    const DecodeResult r = rx.decode(samples, true);
    REQUIRE(r.valid());
    REQUIRE(r.type == DecodeResult::VIDEO);
    REQUIRE(r.filename == "audiot_tmp_clip.mp4");
    REQUIRE(r.fileData == mp4);

    std::remove(path.c_str());
}

TEST_CASE("generic file keeps FILE type (not image/video)", "[media][pipeline]") {
    const std::vector<uint8_t> data = { 1, 2, 3, 4, 5, 6, 7, 8, 9 };
    const std::string path = writeTempFile("audiot_tmp_blob.bin", data);

    PipelineConfig cfg;
    cfg.autoDetectModem = false;

    TxPipeline tx(cfg);
    const std::vector<float> samples = tx.encodeTypedFile(path);
    REQUIRE(!samples.empty());

    RxPipeline rx(cfg);
    const DecodeResult r = rx.decode(samples, true);
    REQUIRE(r.valid());
    REQUIRE(r.type == DecodeResult::FILE);
    REQUIRE(r.fileData == data);

    std::remove(path.c_str());
}
