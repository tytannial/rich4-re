#pragma once

#include <cstdint>
#include <vector>

namespace rich4 {

// [PORT WINMM:DirectSoundCreate/mciSendStringA] 音频输出
// 替换依据: 0x4015D6 中 sub_453B55 初始化 DirectSound；0x4019DD 用 mciSendStringA
//           控制 vfw/mid/cdtrack 音轨（"pause vfw"/"resume mid"/"play cdtrack notify"）
//           → SDL3 SDL_AudioStream 播放 WAV；MCI/MIDI/CD 音轨迁移待办
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
