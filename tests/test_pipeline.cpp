// test_pipeline.cpp - End-to-end pipeline tests
#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>
#include <pipeline/tx_pipeline.h>
#include <pipeline/rx_pipeline.h>
#include <pipeline/pipeline_config.h>
#include <codec/file_codec.h>
#include <codec/text_codec.h>
#include <modem/afsk_modem.h>
#include <audio/wav_reader.h>
#include <cstdio>

using namespace audiot;

TEST_CASE("Pipeline end-to-end: text to audio to text", "[pipeline]") {
    PipelineConfig cfg;
    cfg.sampleRate = 44100;
    cfg.fecType = FecType::HAMMING;
    cfg.markFreq = 1200.0f;
    cfg.spaceFreq = 2200.0f;
    cfg.baudRate = 1200;

    std::string originalText = "Hello, AudioT!";

    // TX: type marker 'T' + encoded text as one burst
    TxPipeline tx(cfg);
    std::vector<float> samples = tx.encodeTypedText(originalText);
    REQUIRE(!samples.empty());

    // RX: decode audio to text
    RxPipeline rx(cfg);
    DecodeResult decoded = rx.decode(samples);

    REQUIRE(decoded.valid());
    REQUIRE(decoded.type == DecodeResult::TEXT);
    REQUIRE(decoded.text == originalText);
}

TEST_CASE("Pipeline type marker: file frame decodes as file", "[pipeline]") {
    PipelineConfig cfg;
    cfg.sampleRate = 44100;
    cfg.fecType = FecType::HAMMING;

    std::string filename = "hello.txt";
    std::vector<uint8_t> fileData = {0x48, 0x65, 0x6C, 0x6C, 0x6F, 0x00, 0xFF}; // "Hello\0\xFF"

    // TX: type marker 'F' + FileCodec frame as one burst
    TxPipeline tx(cfg);
    std::vector<float> samples = tx.encodeFrames(
        {TextCodec::encode(std::string(1, 'F')),
         FileCodec::encodeData(filename, fileData)});

    // RX: must detect FILE type and recover filename + data
    RxPipeline rx(cfg);
    DecodeResult decoded = rx.decode(samples);

    REQUIRE(decoded.valid());
    REQUIRE(decoded.type == DecodeResult::FILE);
    REQUIRE(decoded.filename == filename);
    REQUIRE(decoded.fileData == fileData);
}

TEST_CASE("Pipeline without type marker falls back to legacy text", "[pipeline]") {
    PipelineConfig cfg;
    cfg.sampleRate = 44100;
    cfg.fecType = FecType::HAMMING;

    std::string legacyText = "legacy no marker";

    TxPipeline tx(cfg);
    std::vector<float> samples = tx.encode(legacyText);

    RxPipeline rx(cfg);
    DecodeResult decoded = rx.decode(samples);

    REQUIRE(decoded.valid());
    REQUIRE(decoded.type == DecodeResult::TEXT);
    REQUIRE(decoded.text == legacyText);
}

TEST_CASE("Pipeline WAV file roundtrip", "[pipeline]") {
    const char* tempWav = "test_pipeline_roundtrip.wav";

    PipelineConfig cfg;
    cfg.sampleRate = 44100;
    cfg.fecType = FecType::HAMMING;

    std::string originalText = "Test message 123";

    // TX: type marker 'T' + encoded text to WAV file
    TxPipeline tx(cfg);
    REQUIRE(tx.writeSamplesToWav(tx.encodeTypedText(originalText), tempWav));

    // RX: decode from WAV file
    RxPipeline rx(cfg);
    DecodeResult decoded = rx.decodeFromFile(tempWav);

    REQUIRE(decoded.valid());
    REQUIRE(decoded.type == DecodeResult::TEXT);
    REQUIRE(decoded.text == originalText);

    // Cleanup
    std::remove(tempWav);
}

TEST_CASE("Pipeline without FEC", "[pipeline]") {
    PipelineConfig cfg;
    cfg.sampleRate = 44100;
    cfg.fecType = FecType::NONE;

    std::string originalText = "No FEC test";

    TxPipeline tx(cfg);
    std::vector<float> samples = tx.encodeTypedText(originalText);
    REQUIRE(!samples.empty());

    RxPipeline rx(cfg);
    DecodeResult decoded = rx.decode(samples);

    REQUIRE(decoded.valid());
    REQUIRE(decoded.type == DecodeResult::TEXT);
    REQUIRE(decoded.text == originalText);
}

TEST_CASE("Pipeline with longer text", "[pipeline]") {
    PipelineConfig cfg;
    cfg.sampleRate = 44100;
    cfg.fecType = FecType::HAMMING;

    std::string originalText = "The quick brown fox jumps over the lazy dog. 0123456789";

    TxPipeline tx(cfg);
    std::vector<float> samples = tx.encodeTypedText(originalText);
    REQUIRE(!samples.empty());

    RxPipeline rx(cfg);
    DecodeResult decoded = rx.decode(samples);

    REQUIRE(decoded.valid());
    REQUIRE(decoded.type == DecodeResult::TEXT);
    REQUIRE(decoded.text == originalText);
}

TEST_CASE("Pipeline decodes prep tone + type marker + data", "[pipeline]") {
    PipelineConfig cfg;
    cfg.sampleRate = 44100;
    cfg.fecType = FecType::HAMMING;
    cfg.prepToneSeconds = 0.5f;   // short prep tone for the test

    // Same signal structure as an exported WAV: prep tone -> type marker
    // frame -> data frame
    std::vector<float> audio = audiot::TxPipeline::makePrepAligned(
        cfg.markFreq, cfg.prepToneSeconds, cfg.sampleRate, cfg.baudRate);
    TxPipeline tx(cfg);
    std::vector<float> burst = tx.encodeTypedText("hello prep tone");
    audio.insert(audio.end(), burst.begin(), burst.end());

    RxPipeline rx(cfg);
    DecodeResult decoded = rx.decode(audio);

    REQUIRE(decoded.valid());
    REQUIRE(decoded.type == DecodeResult::TEXT);
    REQUIRE(decoded.text == "hello prep tone");
}

TEST_CASE("Pipeline with Reed-Solomon FEC", "[pipeline]") {
    PipelineConfig cfg;
    cfg.sampleRate = 44100;
    cfg.fecType = FecType::REED_SOLOMON;

    std::string originalText = "Reed-Solomon end-to-end 123";

    TxPipeline tx(cfg);
    std::vector<float> samples = tx.encodeTypedText(originalText);
    REQUIRE(!samples.empty());

    RxPipeline rx(cfg);
    DecodeResult decoded = rx.decode(samples);

    REQUIRE(decoded.valid());
    REQUIRE(decoded.type == DecodeResult::TEXT);
    REQUIRE(decoded.text == originalText);
}

TEST_CASE("Pipeline with Reed-Solomon FEC spans multiple blocks", "[pipeline]") {
    PipelineConfig cfg;
    cfg.sampleRate = 44100;
    cfg.fecType = FecType::REED_SOLOMON;

    // 500 chars: type frame + text frame exceed one 239-byte RS block,
    // exercising block framing, padding and length-based truncation.
    std::string originalText(500, 'A');
    originalText += "end";

    TxPipeline tx(cfg);
    std::vector<float> samples = tx.encodeTypedText(originalText);
    REQUIRE(!samples.empty());

    RxPipeline rx(cfg);
    DecodeResult decoded = rx.decode(samples);

    REQUIRE(decoded.valid());
    REQUIRE(decoded.type == DecodeResult::TEXT);
    REQUIRE(decoded.text == originalText);
}

TEST_CASE("Pipeline autoFec decodes RS stream with Hamming config", "[pipeline]") {
    // A stream encoded with Reed-Solomon must still decode when the
    // receiver is configured for Hamming: autoFec falls back to the
    // other schemes after the configured one fails.
    PipelineConfig txCfg;
    txCfg.sampleRate = 44100;
    txCfg.fecType = FecType::REED_SOLOMON;

    PipelineConfig rxCfg = txCfg;
    rxCfg.fecType = FecType::HAMMING;

    std::string text = "cross-fec compatibility";
    TxPipeline tx(txCfg);
    std::vector<float> samples = tx.encodeTypedText(text);

    RxPipeline rx(rxCfg);
    DecodeResult decoded = rx.decode(samples, true);

    REQUIRE(decoded.valid());
    REQUIRE(decoded.type == DecodeResult::TEXT);
    REQUIRE(decoded.text == text);
}

TEST_CASE("Realtime buffer trim preserves AFSK symbol alignment", "[pipeline]") {
    // 回归：实时接收把缓冲截断到上限时，砍掉的长度必须是符号周期的
    // 整数倍。旧实现任意截断——音频回调按 1024 样本碎块累积，截断
    // 长度几乎总不是 36 的整数倍——导致 AFSK 位网格错位、解调全面
    // 失败（用 rec-un.wav 实测复现：62127 % 36 = 27 样本错位）。
    PipelineConfig cfg;
    cfg.sampleRate = 44100;
    cfg.fecType = FecType::HAMMING;

    TxPipeline tx(cfg);
    const std::string text = "Hello, AudioT!";
    const std::vector<float> sig = tx.encodeTypedText(text);

    // 模拟实时缓冲：前置静音（整秒 + 500 样本非整除偏移，模拟回调
    // 碎块），总长超过 30s 上限从而触发截断；信号位于保留窗口内。
    const int spb = cfg.sampleRate / cfg.baudRate;  // 36
    std::vector<float> buf(static_cast<size_t>(cfg.sampleRate) * 31 + 500, 0.0f);
    buf.insert(buf.end(), sig.begin(), sig.end());

    const size_t maxSamples = static_cast<size_t>(cfg.sampleRate) * 30;
    REQUIRE(buf.size() > maxSamples);
    size_t eraseCount = buf.size() - maxSamples;
    REQUIRE(eraseCount % spb != 0);   // 原始截断长度非对齐（复现 bug 场景）

    // 修复逻辑：截断数量向下取整到符号周期整数倍
    eraseCount -= eraseCount % static_cast<size_t>(spb);
    buf.erase(buf.begin(), buf.begin() + eraseCount);

    RxPipeline rx(cfg);
    const DecodeResult r = rx.decode(buf, true);
    REQUIRE(r.valid());
    REQUIRE(r.type == DecodeResult::TEXT);
    REQUIRE(r.text == text);
}

TEST_CASE("Realtime receive: prep tone locates the signal burst", "[pipeline]") {
    // 回归：发送端在数据前强制播放连续 mark 频率的准备音（长度对齐
    // 符号网格），接收端以准备音结束位置作为信号起点裁剪缓冲，从源
    // 头上排除信号之前的静音区——静音窗口的随机 Goertzel 判决能拼出
    // 完美交替的伪 preamble，把 findSync 锁到错误位置（实测复现：
    // 31s 静音 + 信号 => 伪 sync=36355，真实 36357）。
    //
    // 本用例模拟真实链路：静音 + 准备音 + 数据突发 + 帧尾静音，且
    // 整体偏移任意样本数（麦克风捕获与信号到达之间无样本级对齐）。
    PipelineConfig cfg;
    cfg.sampleRate = 44100;
    cfg.fecType = FecType::HAMMING;

    TxPipeline tx(cfg);
    const std::string text = "Hello, AudioT!";
    const std::vector<float> sig = tx.encodeTypedText(text);

    // 3s 前置静音 + 2s 准备音（与 beginTxPlayback 相同的生成方式）
    // + 数据突发 + 1s 帧尾静音
    std::vector<float> buf;
    const std::vector<float> leadSilence =
        TxPipeline::makeSilence(3.0f, cfg.sampleRate);
    const std::vector<float> prep = TxPipeline::makePrepAligned(
        cfg.markFreq, 2.0f, cfg.sampleRate, cfg.baudRate);
    const std::vector<float> tailSilence =
        TxPipeline::makeSilence(1.0f, cfg.sampleRate);
    buf.insert(buf.end(), leadSilence.begin(), leadSilence.end());
    buf.insert(buf.end(), prep.begin(), prep.end());
    buf.insert(buf.end(), sig.begin(), sig.end());
    buf.insert(buf.end(), tailSilence.begin(), tailSilence.end());

    // 整体偏移 500 个样本（非符号周期整数倍）=> 符号相位任意
    std::vector<float> shifted(500, 0.0f);
    shifted.insert(shifted.end(), buf.begin(), buf.end());

    RxPipeline rx(cfg);
    const DecodeResult r = rx.decode(shifted, true);
    REQUIRE(r.valid());
    REQUIRE(r.type == DecodeResult::TEXT);
    REQUIRE(r.text == text);
}

TEST_CASE("Pipeline decodes weak analog-chain signals", "[pipeline]") {
    // 回归：磁带机等模拟链路输出电平低，采集后幅度可小至 ~0.001。
    // 能量门控必须只排除精确静音（能量 ~0），不能设过高的绝对阈值：
    // A=0.001 时窗口能量 ≈ (A*spb/2)^2 ≈ 3e-4，1e-3 门限会把它全部
    // 拒绝，findSync 计分不足 => 解码完全失败（用户实测磁带机链路
    // 报"一点反应都没有"）。本用例用带准备音 + 幅度 0.001 的信号
    // 复现该场景。
    PipelineConfig cfg;
    cfg.sampleRate = 44100;
    cfg.fecType = FecType::HAMMING;

    TxPipeline tx(cfg);
    const std::string text = "Hello, AudioT!";
    const std::vector<float> sig = tx.encodeTypedText(text);

    // 磁带链路结构：1s 前置静音 + 2s 准备音 + 数据突发
    std::vector<float> buf;
    const std::vector<float> lead =
        TxPipeline::makeSilence(1.0f, cfg.sampleRate);
    const std::vector<float> prep = TxPipeline::makePrepAligned(
        cfg.markFreq, 2.0f, cfg.sampleRate, cfg.baudRate);
    buf.insert(buf.end(), lead.begin(), lead.end());
    buf.insert(buf.end(), prep.begin(), prep.end());
    buf.insert(buf.end(), sig.begin(), sig.end());

    const float kAmp = 0.001f;   // 弱电平（磁带链路）
    for (size_t i = 0; i < buf.size(); ++i) {
        buf[i] *= kAmp;
    }

    RxPipeline rx(cfg);
    const DecodeResult r = rx.decode(buf, true);
    REQUIRE(r.valid());
    REQUIRE(r.type == DecodeResult::TEXT);
    REQUIRE(r.text == text);
}

TEST_CASE("Pipeline config defaults", "[pipeline]") {
    PipelineConfig cfg;
    REQUIRE(cfg.sampleRate == 44100);
    REQUIRE(cfg.modemType == ModemType::AFSK);
    REQUIRE(cfg.fecType == FecType::HAMMING);
    REQUIRE(cfg.markFreq == 1200.0f);
    REQUIRE(cfg.spaceFreq == 2200.0f);
    REQUIRE(cfg.baudRate == 1200);
}
