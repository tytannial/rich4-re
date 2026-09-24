#include "game/platform/audio.h"

#include "game/core/log.h"
#include "game/resource/mkf.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <unordered_map>

// stb_vorbis：OGG Vorbis 解码（公共领域单文件库；实现见 stb_vorbis_impl.cpp）
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

namespace rich4 {

// [RE 0x47E773] off_47E773 可选曲目表（8 首；对应 CD 音轨 track02-09）
const char* const kMusicTracks[8] = {
    "RICH08.MID", "RICH16.MID", "RICH17.MID", "RICH18.MID",
    "RICH19.MID", "RICH20.MID", "RICH21.MID", "RICH22.MID",
};

// [RE 0x47E793] off_47E793 场景音乐表（17 项；对应 CD 音轨 track10-26）
const char* const kSceneMusicTracks[17] = {
    "MIDI01.MID", "MIDI02.MID", "MIDI03.MID", "MIDI04.MID", "MIDI05.MID", "MIDI06.MID",
    "MIDI07.MID", "MIDI08.MID", "MIDI09.MID", "MIDI10.MID", "MIDI11.MID", "MIDI12.MID",
    "MIDI13.MID", "MIDI14-1.MID", "MIDI14-2.MID", "MIDI15.MID", "MIDI16.MID",
};

// [RE 0x48234A] dword_48234A 游戏音效槽→Effect.mkf 索引（原版 {idx,buf} 对数组，-1 结束）
const int kEffectSlotIndex[24] = {7,  9,  10, 32, 33, 34, 35, 36, 37, 38, 43, 44,
                                  45, 46, 53, 47, 48, 49, 50, 54, 55, 56, 15, 62};

namespace {

// 音量增益（原版 DirectSound 音量 0/-2000/-1000/-316/0 百分之一分贝 → 16 分制线性）
constexpr int kVolumeGain[5] = {0, 2, 5, 11, 16};

// 语音专用"槽"标记（-2；不参与 24 个游戏音效槽的同槽替换，voicePlaying 据此查询）
constexpr int kVoiceSlot = -2;

// 输出流目标缓冲（字节；8bit mono）。
// 延迟权衡: 音效响应延迟 ≈ 水位/采样率（1024B @22050Hz ≈ 46ms，接近原版 DirectSound）；
// 每帧 update 补充，帧率 60fps 时每帧消耗约 370B，水位可稳定维持。
constexpr int kTargetQueued = 1024;

// 8bit 无符号 PCM 混音（静音 = 128）
inline void mixSample(uint8_t& dst, uint8_t src, int gain) {
    const int v = (static_cast<int>(dst) - 128) + ((static_cast<int>(src) - 128) * gain) / 16;
    dst = static_cast<uint8_t>(v < -128 ? 0 : (v > 127 ? 255 : v + 128));
}

struct WavInfo {
    const uint8_t* pcm = nullptr;
    size_t size = 0;
    uint16_t channels = 0;
    uint16_t bits = 0;
    uint32_t rate = 0;
};

// 解析 RIFF/WAVE 的 fmt + data 子块（Effect.mkf 音效为 22050Hz/8bit/mono）
bool parseWav(const std::vector<uint8_t>& wav, WavInfo& info) {
    if (wav.size() < 44 || std::memcmp(wav.data(), "RIFF", 4) != 0 ||
        std::memcmp(wav.data() + 8, "WAVE", 4) != 0) {
        return false;
    }
    size_t pos = 12;
    while (pos + 8 <= wav.size()) {
        const uint8_t* tag = wav.data() + pos;
        uint32_t size = 0;
        std::memcpy(&size, wav.data() + pos + 4, 4);
        const size_t body = pos + 8;
        if (std::memcmp(tag, "fmt ", 4) == 0 && body + 16 <= wav.size()) {
            std::memcpy(&info.channels, wav.data() + body + 2, 2);
            std::memcpy(&info.rate, wav.data() + body + 4, 4);
            std::memcpy(&info.bits, wav.data() + body + 14, 2);
        } else if (std::memcmp(tag, "data", 4) == 0) {
            const size_t end = std::min(body + size, wav.size());
            info.pcm = wav.data() + body;
            info.size = end - body;
        }
        pos = body + size + (size & 1);
    }
    return info.pcm != nullptr && info.size > 0;
}

// ---- 音乐解码器（stb_vorbis；44100 stereo → 22050 mono）----
class MusicDecoder {
public:
    ~MusicDecoder() { close(); }

    bool open(const std::string& path) {
        close();
        int error = 0;
        m_v = stb_vorbis_open_filename(path.c_str(), &error, nullptr);
        if (!m_v) {
            RICH4_LOGW("MusicDecoder: open failed (error %d): %s", error, path.c_str());
            return false;
        }
        m_info = stb_vorbis_get_info(m_v);
        RICH4_LOGI("MusicDecoder: %dHz %dch (%s)", m_info.sample_rate, m_info.channels,
                   path.c_str());
        return true;
    }

    // 读取 16bit mono 22050Hz 采样；返回采样数（0 = EOF/错误）
    int read(int16_t* out, int maxSamples) {
        if (!m_v) {
            return 0;
        }
        const int ch = m_info.channels > 0 ? m_info.channels : 1;
        const bool half = m_info.sample_rate > 22050; // 44100 → 22050 降采样
        int produced = 0;
        int16_t buf[4096];
        while (produced < maxSamples) {
            const int frames = stb_vorbis_get_samples_short_interleaved(m_v, ch, buf, 4096);
            if (frames <= 0) {
                break;
            }
            for (int i = 0; i < frames && produced < maxSamples; ++i) {
                if (half) {
                    m_downsamplePhase ^= 1;
                    if (m_downsamplePhase != 0) {
                        continue;
                    }
                }
                int32_t sum = 0;
                for (int c = 0; c < ch; ++c) {
                    sum += buf[i * ch + c];
                }
                out[produced++] = static_cast<int16_t>(sum / ch);
            }
        }
        return produced;
    }

    void close() {
        if (m_v) {
            stb_vorbis_close(m_v);
            m_v = nullptr;
        }
        m_downsamplePhase = 0;
    }
    bool valid() const { return m_v != nullptr; }

private:
    stb_vorbis* m_v = nullptr;
    stb_vorbis_info m_info{};
    int m_downsamplePhase = 0;
};

} // namespace

struct Audio::Impl {
    SDL_AudioStream* stream = nullptr;
    int sampleRate = 22050;

    // 音效：Effect.mkf 解码缓存（8bit mono PCM）+ 活动声部
    using PcmBuffer = std::shared_ptr<const std::vector<uint8_t>>;
    std::unordered_map<int, PcmBuffer> effectCache;
    std::unordered_map<int, PcmBuffer> speakingCache; // 角色语音（Speaking.mkf）
    struct Voice {
        PcmBuffer pcm;
        size_t pos = 0;
        int slot = -1; // 游戏音效槽（-1 = UI/独立音效，不参与同槽替换）
        int id = -1;   // Effect.mkf 索引（stopEffect 按 id 移除）
        bool looping = false;  // [RE 0x4542CE a2=1] DSBPLAY_LOOPING（老虎机滚动音）
    };
    std::vector<Voice> voices;

    // 音乐
    MusicDecoder decoder;
    std::vector<uint8_t> musicBuf; // 8bit mono 解码缓冲
    size_t musicPos = 0;
    bool musicEof = false;
    int track = 0;       // 1-8 = 可选曲目；-1 = 场景音乐；0 = 无
    int sceneIndex = -1; // 场景音乐索引

    std::vector<uint8_t> mixBuf;
};

Audio::Audio() : m_impl(new Impl()) {}

Audio::~Audio() { close(); }

bool Audio::open(int sampleRate) {
    if (m_impl->stream) {
        return true;
    }
    // [RE 0x453B55] 原版 DirectSound 主缓冲 22050Hz / 8bit / mono
    // 低延迟: 期望设备周期 512 帧（约 23ms），配合 update 水位 1024B（约 46ms）
    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "512");
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_U8;
    spec.channels = 1;
    spec.freq = sampleRate;

    SDL_AudioStream* stream =
        SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!stream) {
        RICH4_LOGE("SDL_OpenAudioDeviceStream failed: %s", SDL_GetError());
        return false;
    }
    SDL_ResumeAudioStreamDevice(stream);
    m_impl->stream = stream;
    m_impl->sampleRate = sampleRate;
    return true;
}

void Audio::close() {
    if (!m_impl) {
        return;
    }
    stopMusic();
    m_impl->voices.clear();
    m_impl->effectCache.clear();
    m_impl->speakingCache.clear();
    m_musicStack.clear();
    if (m_impl->stream) {
        SDL_DestroyAudioStream(m_impl->stream);
        m_impl->stream = nullptr;
    }
}

void Audio::update() {
    Impl& impl = *m_impl;
    if (!impl.stream) {
        return;
    }
    const int queued = SDL_GetAudioStreamQueued(impl.stream);
    // [NEW] 一次性诊断：确认输出设备在消耗缓冲（queued 应低于目标水位）
    static int updateCount = 0;
    if (++updateCount == 300) {
        RICH4_LOGI("audio: queued=%d voices=%zu track=%d playing=%d", queued,
                   impl.voices.size(), impl.track, impl.decoder.valid() ? 1 : 0);
    }
    if (queued >= kTargetQueued) {
        return;
    }
    const int need = kTargetQueued - (queued > 0 ? queued : 0);

    // 混音基线：8bit 无符号静音 = 128
    if (static_cast<int>(impl.mixBuf.size()) < need) {
        impl.mixBuf.resize(static_cast<size_t>(need));
    }
    std::memset(impl.mixBuf.data(), 128, static_cast<size_t>(need));

    // 音乐混入
    if (impl.decoder.valid() && m_musicVolume > 0) {
        const int gain = kVolumeGain[std::clamp(m_musicVolume, 0, 4)];
        while (!impl.musicEof &&
               impl.musicBuf.size() - impl.musicPos < static_cast<size_t>(need)) {
            int16_t pcm[2048];
            const int got = impl.decoder.read(pcm, 2048);
            if (got <= 0) {
                impl.musicEof = true;
                break;
            }
            for (int i = 0; i < got; ++i) {
                // 16bit 有符号 → 8bit 无符号
                impl.musicBuf.push_back(static_cast<uint8_t>((pcm[i] >> 8) + 128));
            }
        }
        const size_t avail = impl.musicBuf.size() - impl.musicPos;
        const int n = static_cast<int>(std::min(static_cast<size_t>(need), avail));
        for (int i = 0; i < n; ++i) {
            mixSample(impl.mixBuf[i], impl.musicBuf[impl.musicPos + i], gain);
        }
        impl.musicPos += static_cast<size_t>(n);
        if (impl.musicPos > 65536) {
            impl.musicBuf.erase(impl.musicBuf.begin(),
                                impl.musicBuf.begin() + static_cast<ptrdiff_t>(impl.musicPos));
            impl.musicPos = 0;
        }
        // 播完：8 首可选曲目自动切下一首（原版 sub_454B1A 播完 +1 & 7）
        if (impl.musicEof && impl.musicBuf.size() == impl.musicPos) {
            if (impl.track >= 1 && impl.track <= 8) {
                const int next = impl.track % 8; // 0-7
                playMusic(next);
            } else if (impl.sceneIndex >= 0) {
                playSceneMusic(impl.sceneIndex);
            }
        }
    }

    // 音效混入
    if (m_effectVolume > 0) {
        const int gain = kVolumeGain[std::clamp(m_effectVolume, 0, 4)];
        for (auto it = impl.voices.begin(); it != impl.voices.end();) {
            if (!it->pcm) {
                it = impl.voices.erase(it);
                continue;
            }
            const std::vector<uint8_t>& pcm = *it->pcm;
            const size_t avail = pcm.size() - it->pos;
            const int n = static_cast<int>(std::min(static_cast<size_t>(need), avail));
            for (int i = 0; i < n; ++i) {
                mixSample(impl.mixBuf[i], pcm[it->pos + i], gain);
            }
            it->pos += static_cast<size_t>(n);
            if (it->pos >= pcm.size()) {
                if (it->looping) {
                    it->pos = 0;  // [RE 0x4542CE a2=1] 循环回卷（单 buffer 持续播）
                    ++it;
                } else {
                    it = impl.voices.erase(it);
                }
            } else {
                ++it;
            }
        }
    }

    SDL_PutAudioStreamData(impl.stream, impl.mixBuf.data(), need);
}

bool Audio::playEffect(int effectId) {
    if (!m_impl->stream || !m_effectMkf || m_effectVolume <= 0) {
        return false;
    }
    // [RE 0x454176] 音效懒加载（原版进界面时 sub_454176 预加载 Effect.mkf WAV）
    auto it = m_impl->effectCache.find(effectId);
    if (it == m_impl->effectCache.end()) {
        auto blob = m_effectMkf->read(static_cast<size_t>(effectId));
        if (!blob) {
            RICH4_LOGW("playEffect: Effect.mkf[%d] unavailable", effectId);
            return false;
        }
        WavInfo info;
        if (!parseWav(*blob, info)) {
            RICH4_LOGW("playEffect: Effect.mkf[%d] not RIFF/WAVE", effectId);
            return false;
        }
        if (info.channels != 1 || info.bits != 8 ||
            info.rate != static_cast<uint32_t>(m_impl->sampleRate)) {
            RICH4_LOGW("playEffect: Effect.mkf[%d] format %uch/%ubit/%uHz unsupported", effectId,
                       info.channels, info.bits, info.rate);
            return false;
        }
        auto pcm = std::make_shared<std::vector<uint8_t>>(info.pcm, info.pcm + info.size);
        it = m_impl->effectCache.emplace(effectId, Impl::PcmBuffer(std::move(pcm))).first;
    }
    Impl::Voice voice;
    voice.pcm = it->second;
    voice.pos = 0;
    voice.id = effectId;
    m_impl->voices.push_back(std::move(voice));
    return true;
}

// [RE 0x4542CE(handle, 1)] 循环播放专用 buffer（第二参 = DSBPLAY_LOOPING；老虎机滚动音
//   dword_475D3C）：同 id 旧循环先停（单 buffer 重播语义），直至 stopEffect
bool Audio::playEffectLooping(int effectId) {
    stopEffect(effectId);
    if (!playEffect(effectId)) {
        return false;
    }
    m_impl->voices.back().looping = true;
    return true;
}

// [RE 0x4542E9] 按 Effect.mkf 索引停止声部（老虎机 case8 停滚动音）
void Audio::stopEffect(int effectId) {
    auto& voices = m_impl->voices;
    voices.erase(std::remove_if(voices.begin(), voices.end(),
                                [effectId](const Impl::Voice& v) { return v.id == effectId; }),
                 voices.end());
}

bool Audio::playEffectSlot(int slot) {
    if (slot < 0 || slot >= 24) {
        return false;
    }
    // [RE 0x4542E9/0x4542CE] 原版单 buffer：同槽先停止旧音效再播放（替换式，避免叠加）
    stopEffectSlot(slot);
    if (!playEffect(kEffectSlotIndex[slot])) {
        return false;
    }
    m_impl->voices.back().slot = slot;
    return true;
}

void Audio::stopEffectSlot(int slot) {
    auto& voices = m_impl->voices;
    voices.erase(std::remove_if(voices.begin(), voices.end(),
                                [slot](const Impl::Voice& v) { return v.slot == slot; }),
                 voices.end());
}

// ---- 角色语音（Speaking.mkf；0x45441A/0x4544B9/0x454493）----
// 原版 dword_47E750 单 DirectSound buffer：sub_45441A 播新语音前先 sub_454493 停旧；
// 迁移: 单槽声部（slot = kVoiceSlot），update 混音与其他音效同路。

// [RE 0x45441A] 播放语音（voiceId = Speaking.mkf 索引；'#NNNN' 前缀数字）
bool Audio::playVoice(int voiceId) {
    if (!m_impl->stream || !m_speakingMkf || m_effectVolume <= 0) {
        return false; // 原版 g_effectVolume 为 0 时 sub_45441A 不播
    }
    stopVoice(); // [RE 0x454493] 单通道：打断旧语音
    auto& cache = m_impl->speakingCache;
    auto it = cache.find(voiceId);
    if (it == cache.end()) {
        auto blob = m_speakingMkf->read(static_cast<size_t>(voiceId));
        if (!blob) {
            RICH4_LOGW("playVoice: Speaking.mkf[%d] unavailable", voiceId);
            return false;
        }
        WavInfo info;
        if (!parseWav(*blob, info) || info.channels != 1 || info.bits != 8 ||
            info.rate != static_cast<uint32_t>(m_impl->sampleRate)) {
            RICH4_LOGW("playVoice: Speaking.mkf[%d] not 22050/8bit/mono RIFF", voiceId);
            return false;
        }
        auto pcm = std::make_shared<std::vector<uint8_t>>(info.pcm, info.pcm + info.size);
        it = cache.emplace(voiceId, Impl::PcmBuffer(std::move(pcm))).first;
    }
    Impl::Voice voice;
    voice.pcm = it->second;
    voice.pos = 0;
    voice.slot = kVoiceSlot;
    m_impl->voices.push_back(std::move(voice));
    return true;
}

// [RE 0x4544B9] 语音是否仍在播放（原版 Status&DSBSTATUS_PLAYING；播完声部由 update 清理）
bool Audio::voicePlaying() {
    for (const auto& v : m_impl->voices) {
        if (v.slot == kVoiceSlot) {
            return true;
        }
    }
    return false;
}

// [RE 0x454493] 停止语音（Release）
void Audio::stopVoice() {
    stopEffectSlot(kVoiceSlot);
}

bool Audio::playMusic(int trackIndex) {
    // [RE 0x454D91] 原版 if (byte_49715A)：音乐音量为 0 时不播放
    if (!m_impl->stream || m_musicDir.empty() || m_musicVolume <= 0) {
        return false;
    }
    if (trackIndex < 0 || trackIndex > 7) {
        return false;
    }
    // [RE 0x454D91] 原版 CD 模式 "play cdtrack from (index + 2)"；OGG = track02-09
    char name[32];
    std::snprintf(name, sizeof(name), "track%02d.ogg", trackIndex + 2);
    const std::string path = m_musicDir + "/" + name;
    stopMusic();
    if (!m_impl->decoder.open(path)) {
        return false;
    }
    m_impl->musicEof = false;
    m_impl->musicBuf.clear();
    m_impl->musicPos = 0;
    m_impl->track = trackIndex + 1;
    m_impl->sceneIndex = -1;
    RICH4_LOGI("playMusic: track %d (%s) (RE 0x454D91)", m_impl->track, name);
    return true;
}

bool Audio::playNextMusic() {
    const int cur = (m_impl->track >= 1 && m_impl->track <= 8) ? (m_impl->track - 1) : -1;
    return playMusic((cur + 1) & 7);
}

bool Audio::playSceneMusic(int sceneIndex) {
    // [RE 0x4549CF] 原版 if (byte_49715A)：音乐音量为 0 时不播放
    if (!m_impl->stream || m_musicDir.empty() || m_musicVolume <= 0) {
        return false;
    }
    if (sceneIndex < 0 || sceneIndex >= 17) {
        return false;
    }
    // [RE 0x4549CF] 场景音乐（off_47E793 表）→ CD 音轨 10-26
    char name[32];
    std::snprintf(name, sizeof(name), "track%02d.ogg", sceneIndex + 10);
    const std::string path = m_musicDir + "/" + name;
    stopMusic();
    if (!m_impl->decoder.open(path)) {
        return false;
    }
    m_impl->musicEof = false;
    m_impl->musicBuf.clear();
    m_impl->musicPos = 0;
    m_impl->track = -1;
    m_impl->sceneIndex = sceneIndex;
    RICH4_LOGI("playSceneMusic: index %d (%s) (RE 0x4549CF)", sceneIndex, name);
    return true;
}

// [RE 0x4549CF] 面板进入：当前为可选曲目（byte_47E772<0 判定）时 musicOnTrackEnd(0x454B1A)
//   压栈（记曲目+位置）再切场景音乐。重写不记播放位置（OGG 从头播，见 resumeSceneMusic）。
bool Audio::pushSceneMusic(int sceneIndex) {
    const int t = m_impl->track;
    if (t >= 1 && t <= 8) {
        m_musicStack.push_back(t - 1);
    }
    return playSceneMusic(sceneIndex);
}

// [RE 0x454BCC] 面板退出：弹栈恢复之前播放的可选曲目（原版 "play mid from 位置" 续播）。
// 差异: 重写 OGG 从头播放；栈空时不动作（原版无条件弹栈）。
void Audio::resumeSceneMusic() {
    if (m_musicStack.empty()) {
        return;
    }
    const int idx = m_musicStack.back();
    m_musicStack.pop_back();
    playMusic(idx);
}

void Audio::stopMusic() {
    m_impl->decoder.close();
    m_impl->musicBuf.clear();
    m_impl->musicPos = 0;
    m_impl->musicEof = false;
    m_impl->track = 0;
    m_impl->sceneIndex = -1;
}

int Audio::currentTrack() const {
    // [RE 0x454F5B] 当前曲目（1-8；0 = 未播放）
    return m_impl->track >= 1 && m_impl->track <= 8 ? m_impl->track : 0;
}

int Audio::currentSceneIndex() const { return m_impl->sceneIndex; }

void Audio::setMusicVolume(int volume) {
    m_musicVolume = volume;
    // [RE byte_49715A] 音量为 0（静音）时停止音乐（原版 MCI 不播放）
    if (m_musicVolume <= 0) {
        stopMusic();
    }
}

bool Audio::musicPlaying() const {
    return m_impl->decoder.valid() && !m_impl->musicEof;
}

bool Audio::playWav(const std::vector<uint8_t>& wav) {
    // 兼容旧接口：解析 RIFF 后作为独立声部播放
    if (!m_impl->stream) {
        return false;
    }
    WavInfo info;
    if (!parseWav(wav, info)) {
        RICH4_LOGW("playWav: not a RIFF/WAVE buffer");
        return false;
    }
    Impl::Voice voice;
    voice.pcm = std::make_shared<std::vector<uint8_t>>(info.pcm, info.pcm + info.size);
    voice.pos = 0;
    m_impl->voices.push_back(std::move(voice));
    return true;
}

} // namespace rich4
