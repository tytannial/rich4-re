#pragma once

#include <cstdint>
#include <vector>

namespace rich4 {

// 简单音频层：打开 22050Hz / 8bit / mono 输出流，播放 WAV(MKF 中 RIFF 资源)。
class Audio {
public:
    Audio() = default;
    ~Audio();

    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;

    bool open(int sampleRate = 22050);
    void close();

    // 播放一段 WAV 数据（解析 RIFF 后入队）。
    bool playWav(const std::vector<uint8_t>& wav);

private:
    struct Impl;
    Impl* m_impl = nullptr;
};

} // namespace rich4
