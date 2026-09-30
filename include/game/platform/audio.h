#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
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
    // [RE 0x454D91] 切换可选曲目（0-7 → CD 音轨 track02-09；原版 "play cdtrack from N"）；
    //   同步游标 m_gameTrack（原版 byte_47E771 与播放一体）
    // [RE 0x454BCC] seekFrames>0 = 原版 "play mid from <位置>" 续播（面板弹栈恢复用）
    bool playMusic(int trackIndex, uint32_t seekFrames = 0);
    // [RE 0x454D91 a1==0] 切下一首可选曲目（**持久游标** m_gameTrack=(游标+1)&7；
    //   进游戏(续局)/过天归零/读档/时光机/曲终 notify 共用——游标不随 stopMusic/场景音乐丢失）
    bool playNextMusic();
    // [RE 0x47E771] byte_47E771 持久曲目游标（0-7；gameInit 置 0，此后仅 +1&7 演进）
    int gameTrack() const { return m_gameTrack; }
    // [RE word_46CB06] g_musicTimer 切歌天数计数（低4位=剩余天数；**非 0 期间
    //   musicPlayScene/musicStackPopRestore 整体被禁**，advanceDay 每天 −1，归零切歌并置 0）
    int switchDays() const { return m_switchDays; }
    void setSwitchDays(int v) { m_switchDays = v; }
    // [RE 0x41CF67] advanceDay 每日：低4位非0 → −−；((v-1)&0xF)==0 → 置0 + stopMusic + 下一首
    void musicTickDay();
    // [RE 0x447387] 时光机恢复成功后的音乐计时处理：++，若 (低4 > BYTE>>4) → 置0+stop+下一首
    void musicTimeMachineRestore();
    // [RE 0x4549CF] 播放场景音乐（sceneIndex → track10-26；原版 off_47E793 MIDI 表）。
    //   g_musicTimer（switchDays）非 0 → **整体不切**（原版开头 if(!g_musicTimer)）。
    //   saveCurrent=true（默认）时若当前为可选曲目模式先 musicOnTrackEnd 压栈（原版
    //   0x454B1A：playing→同曲+当前 seek 位置；未 playing→游标+1 位置 0 **并推进游标**）；
    //   saveCurrent=false 对应原版 bit15（0x8000|scene 不压栈）
    bool playSceneMusic(int sceneIndex, bool saveCurrent = true);
    // [RE 0x4549CF/0x454B1A] 面板进入：等义于 playSceneMusic（原版同一函数内部自动压栈）；
    //   保留独立入口供面板语义表达
    bool pushSceneMusic(int sceneIndex);
    // [RE 0x454BCC] 面板退出：弹栈恢复之前播放的可选曲目——原版 "play mid from <位置> notify"
    //   **从保存位置续播**（switchDays 非 0 期间同被禁）
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
    // [RE 0x4019DD WM_ACTIVATEAPP] 窗口失焦暂停 / 回焦恢复音乐（暂停=冻结解码与混音推进，
    //   恢复从原位置续播；原版 pause mid / resume cdtrack）——仅管"焦点"源
    void setMusicPaused(bool paused);
    bool musicPaused() const;
    // [NEW] 演出段暂停音乐（FLC 阻塞动画；引用计数与焦点源并存，恢复即原位续播。
    //   原版 MCI 独立线程动画期间照常播放，此为重写观感增强）
    void pushMusicPause();
    void popMusicPause();

    // 兼容旧接口：播放一段 WAV（RIFF）数据
    bool playWav(const std::vector<uint8_t>& wav);

private:
    struct Impl;
    // [RE 0x454B1A] musicOnTrackEnd：压栈当前可选曲目+播放位置（未播放则推进游标、位置0）
    void musicOnTrackEnd();

    std::unique_ptr<Impl> m_impl;
    const MkfArchive* m_effectMkf = nullptr;
    const MkfArchive* m_speakingMkf = nullptr;
    // [RE 0x48CB70+0x48CB50/dword_47E7D7] 面板音乐栈：{曲目 0-7, 原版 status mid position
    //   → 重写=已解码源帧}；弹栈 "play mid from 位置" 续播
    std::vector<std::pair<int, uint32_t>> m_musicStack;
    int m_gameTrack = 0;    // [RE 0x47E771] byte_47E771 持久曲目游标
    int m_switchDays = 0;   // [RE word_46CB06] g_musicTimer
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
