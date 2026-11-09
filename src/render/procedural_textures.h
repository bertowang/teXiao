#pragma once
// 程序化资源：LUT / 贴纸 / 无相机时的测试图。避免外部图片依赖，保证 demo 开箱即跑。
#include <cstdint>
#include <string>
#include <vector>

namespace fx {

std::vector<uint8_t> MakeLutStrip(const std::string& generator, uint32_t& width, uint32_t& height);
std::vector<uint8_t> MakeRgbaImage(const std::string& generator, uint32_t& width,
                                   uint32_t& height);
std::vector<uint8_t> MakeTestPattern(uint32_t width, uint32_t height, uint64_t frameIndex);

}  // namespace fx
