#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace rich4 {

class MkfArchive;

// [PORT WINMM:DirectSoundCreate/mciSendStringA] 音频输出
// 替换依据: 0x4015D6 中 sub_453B55 初始化 DirectSound（22050Hz/8bit/mono 主缓冲）;
//           0x4540D8 音效播放（Effect.mkf WAV → DirectSound buffer）;
//           0x454D91/0x4549CF 音乐（MCI MIDI / CD 音轨）;
//           0x454F5B 当前曲目; byte_49715A=音乐音量, byte_49715B=音效音量
// 迁移: DirectSound 多缓冲 → SDL_AudioStream 软件混音（每帧 update 喂流）;
//       MCI MIDI/CD → Media Foundation 解码 Media/Music/trackNN.ogg（Windows 先行）
class Audio {
public:
    Audio();
    ~Audio();

    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;

    // 打开 22050Hz / 8bit / mono 输出流（原版 DirectSound 主缓冲格式）
    bool open(int sampleRate = 22050);
    void close();

    // 每帧调用：混合活动音效/音乐并喂入输出流（替换原版 DirectSound 混音）
    void update();

    // ---- 音效（Effect.mkf）----
    void setEffectArchive(const MkfArchive* mkf) { m_effectMkf = mkf; }
    // [RE 0x4540D8] 播放音效（effectId = Effect.mkf 资源索引；原版 DirectSound buffer 播放）
    //   UI 音效走此接口（原版独立结构 dword_48231A/482322/48232A/482332/48233A/unk_46CCD0
    //   → Effect.mkf 索引 0/1/2/4/3/5）
    bool playEffect(int effectId);
    // [RE 0x4542CE a2=1] 循环播放专用单 buffer（老虎机滚动音 dword_475D3C；重播先停同 id）
    bool playEffectLooping(int effectId);
    // [RE 0x4542E9] 按 Effect.mkf 索引停止声部
    void stopEffect(int effectId);
    // [RE 0x48234A] 播放游戏音效槽（0..23；原版 dword_48234A[2*slot] 映射到 Effect.mkf 索引）
    //   同槽替换（原版单 DirectSound buffer：sub_4542E9 停止 + sub_4542CE 播放）
    bool playEffectSlot(int slot);
    // [RE 0x4542CE(handle,1)] 槽循环播放（移动音/機器娃娃；同槽替换 + DSBPLAY_LOOPING）——
    //   stopEffectSlot(slot) 可停（原版 `audioPlayEffect(&g_effectSlots[2*slot], 1)` 语义）
    bool playEffectSlotLooping(int slot);
    // [RE 0x4542E9] 停止指定音效槽（移动结束等）
    void stopEffectSlot(int slot);
    // [RE byte_49715B] 音效音量 0-4（0 = 静音）
    void setEffectVolume(int volume) { m_effectVolume = volume; }
    int effectVolume() const { return m_effectVolume; }

    // ---- 角色语音（Speaking.mkf；'#NNNN' 前缀经 drawText 0x45441A 触发）----
    void setSpeakingArchive(const MkfArchive* mkf) { m_speakingMkf = mkf; }
    // [RE 0x45441A/0x4544F6] 播放语音：**单通道**（新语音打断旧；原版 dword_47E750 单
    //   DirectSound buffer，先 sub_454493 停旧再播新）；effectVolume=0 不播（原版同检查）
    bool playVoice(int voiceId);
    // [RE 0x4544B9] 语音是否仍在播放（false = 已播完/无，并清理）
    bool voicePlaying();
    // [RE 0x454493] 停止语音
    void stopVoice();

    // ---- 音乐（Media/Music/trackNN.ogg；回退游戏目录 *.MID）----
    void setMusicDir(const std::string& dir) { m_musicDir = dir; }
    // [RE 0x454D91] 切换可选曲目（0-7 → CD 音轨 track02-09；原版 "play cdtrack from N"）
    bool playMusic(int trackIndex);
    // [RE 0x454D91 a1==0] 切下一首可选曲目（byte_47E771=(byte_47E771+1)&7；进游戏/过天轮换）
    bool playNextMusic();
    // [RE 0x4549CF] 播放场景音乐（sceneIndex → track10-26；原版 off_47E793 MIDI 表）
    bool playSceneMusic(int sceneIndex);
    // [RE 0x4549CF/0x454B1A] 面板进入：当前为可选曲目（track 1-8）时压栈（原版 musicOnTrackEnd
    //   记曲目+播放位置 dword_48CB50/byte_48CB70[dword_47E7D7]）再切场景音乐
    bool pushSceneMusic(int sceneIndex);
    // [RE 0x454BCC] 面板退出：弹栈恢复之前播放的可选曲目（原版 "play mid from 位置" 续播；
    //   重写 OGG 从头播，位置恢复省略）
    void resumeSceneMusic();
    void stopMusic();
    // [RE 0x454F5B] 当前曲目（1-8；0 = 未播放）
    int currentTrack() const;
    // 当前场景音乐索引（0-16；-1 = 非场景音乐）
    int currentSceneIndex() const;
    // [RE byte_49715A] 音乐音量 0-4（0 = 静音并停止）
    void setMusicVolume(int volume);
    int musicVolume() const { return m_musicVolume; }
    bool musicPlaying() const;

    // 兼容旧接口：播放一段 WAV（RIFF）数据
    bool playWav(const std::vector<uint8_t>& wav);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    const MkfArchive* m_effectMkf = nullptr;
    const MkfArchive* m_speakingMkf = nullptr;
    std::vector<int> m_musicStack; // [RE 0x48CB70/dword_47E7D7] 面板音乐栈（可选曲目索引 0-7）
    int m_effectVolume = 4;
    int m_musicVolume = 4;
    std::string m_musicDir;
};

// [RE 0x48234A] dword_48234A 游戏音效槽→Effect.mkf 索引（24 项；原版 {idx,buf} 对数组，-1 结束）
extern const int kEffectSlotIndex[24];
// [RE 0x47E773] off_47E773 可选曲目表（8 首，设置界面音乐列表）
extern const char* const kMusicTracks[8];
// [RE 0x47E793] off_47E793 场景音乐表（MIDI01-16 + MIDI14-1，对应 CD 音轨 10-26）
extern const char* const kSceneMusicTracks[17];

} // namespace rich4
