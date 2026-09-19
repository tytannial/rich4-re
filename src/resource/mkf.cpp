#include "game/resource/mkf.h"

#include "game/core/log.h"
#include "game/resource/lzhuf.h"

#include <cstdio>
#include <cstring>

namespace rich4 {

namespace {
constexpr size_t kHeaderSize = 16;

uint32_t readU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
} // namespace

bool MkfArchive::load(const std::string& path) {
    m_path = path;
    std::FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) {
        RICH4_LOGE("MKF open failed: %s", path.c_str());
        return false;
    }
    std::fseek(fp, 0, SEEK_END);
    const long size = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);
    if (size <= 0) {
        std::fclose(fp);
        return false;
    }
    m_data.resize(static_cast<size_t>(size));
    const size_t got = std::fread(m_data.data(), 1, m_data.size(), fp);
    std::fclose(fp);
    if (got != m_data.size()) {
        RICH4_LOGE("MKF short read: %s", path.c_str());
        return false;
    }

    const uint32_t indexOffset = readU32(m_data.data());
    if (indexOffset == 0 || indexOffset >= m_data.size()) {
        RICH4_LOGE("MKF bad index offset 0x%X: %s", indexOffset, path.c_str());
        return false;
    }
    const size_t n = (m_data.size() - indexOffset) / 4;
    m_index.resize(n);
    for (size_t i = 0; i < n; ++i) {
        m_index[i] = readU32(m_data.data() + indexOffset + i * 4);
    }

    m_entries.clear();
    for (size_t i = 0; i + 1 < n; ++i) {
        const uint32_t off = m_index[i];
        if (off + kHeaderSize > m_data.size()) {
            break;
        }
        MkfEntry e;
        e.offset = off;
        e.decompressedSize = readU32(m_data.data() + off);
        e.compressedSize = readU32(m_data.data() + off + 4);
        e.dataOffset = readU32(m_data.data() + off + 8);
        e.dataLength = readU32(m_data.data() + off + 12);
        e.dataFileOffset = off + static_cast<uint32_t>(kHeaderSize);
        m_entries.push_back(e);
    }
    return true;
}

std::optional<std::vector<uint8_t>> MkfArchive::read(size_t index) const {
    if (index >= m_entries.size()) {
        return std::nullopt;
    }
    const MkfEntry& e = m_entries[index];
    const uint8_t* raw = m_data.data() + e.dataFileOffset;
    if (e.dataFileOffset + e.compressedSize > m_data.size()) {
        return std::nullopt;
    }
    if (!e.compressed()) {
        return std::vector<uint8_t>(raw, raw + e.compressedSize);
    }
    return lzhufDecompress(raw, e.compressedSize, e.decompressedSize);
}

} // namespace rich4
