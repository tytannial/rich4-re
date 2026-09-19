#include "game/platform/audio.h"

#include "game/core/log.h"

#include <SDL3/SDL.h>

#include <cstring>

namespace rich4 {

struct Audio::Impl {
    SDL_AudioStream* stream = nullptr;
};

Audio::~Audio() { close(); }

bool Audio::open(int sampleRate) {
    if (m_impl) {
        return true;
    }
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_U8;
    spec.channels = 1;
    spec.freq = sampleRate;

    SDL_AudioStream* stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!stream) {
        RICH4_LOGE("SDL_OpenAudioDeviceStream failed: %s", SDL_GetError());
        return false;
    }
    SDL_ResumeAudioStreamDevice(stream);
    m_impl = new Impl{stream};
    return true;
}

void Audio::close() {
    if (m_impl) {
        SDL_DestroyAudioStream(m_impl->stream);
        delete m_impl;
        m_impl = nullptr;
    }
}

bool Audio::playWav(const std::vector<uint8_t>& wav) {
    if (!m_impl) {
        return false;
    }
    if (wav.size() < 44 || std::memcmp(wav.data(), "RIFF", 4) != 0 ||
        std::memcmp(wav.data() + 8, "WAVE", 4) != 0) {
        RICH4_LOGW("playWav: not a RIFF/WAVE buffer");
        return false;
    }
    // 在 RIFF 块中定位 "data" 子块。
    size_t pos = 12;
    while (pos + 8 <= wav.size()) {
        const uint8_t* tag = wav.data() + pos;
        uint32_t size = 0;
        std::memcpy(&size, wav.data() + pos + 4, 4);
        if (std::memcmp(tag, "data", 4) == 0) {
            size_t end = pos + 8 + size;
            if (end > wav.size()) {
                end = wav.size();
            }
            SDL_PutAudioStreamData(m_impl->stream, wav.data() + pos + 8,
                                   static_cast<int>(end - (pos + 8)));
            return true;
        }
        pos += 8 + size + (size & 1);
    }
    return false;
}

} // namespace rich4
