// test_placeholder.cpp - 占位测试，验证构建系统正确集成 Catch2
// 后续 Phase 将替换为实际单元测试

#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>

#include <string>
#include <core/audiot_core.h>
#include <audio/audiot_audio.h>

TEST_CASE("Core version is reported", "[core]")
{
    const char* ver = audiot::core::coreVersion();
    REQUIRE(ver != nullptr);
    REQUIRE(std::string(ver) == "0.1.0");
}

TEST_CASE("Audio version is reported", "[audio]")
{
    const char* ver = audiot::audio::audioVersion();
    REQUIRE(ver != nullptr);
    REQUIRE(std::string(ver) == "0.1.0");
}
