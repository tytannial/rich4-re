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

// [RE 0x4502FE] mkfOpen
// 依据: 0x4015D6 中 sub_4502FE("data.mkf"/"speaking.mkf"/"panel.mkf"/"effect.mkf")，
//       返回句柄存入 dword_48A0E4/0x48A054/0x48A05C/0x48A058；0x450441 以句柄取资源
// 差异: 原版打开文件后按需 seek+解压（带缓存），当前实现全量读入 + 索引预解析
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
    // 索引表每一项都是资源头偏移（index[N-1] 也是有效资源）。
    // 依据: Data.mkf 索引表 602 项, index[601] 指向 614400 字节资源（原版 0x401543
    //       sub_450441(handle, 601) 读取）；7 个 MKF 全部验证通过
    const size_t n = (m_data.size() - indexOffset) / 4;
    m_index.resize(n);
    for (size_t i = 0; i < n; ++i) {
        m_index[i] = readU32(m_data.data() + indexOffset + i * 4);
    }

    m_entries.clear();
    for (size_t i = 0; i < n; ++i) {
        const uint32_t off = m_index[i];
        // 第一个资源头位于偏移 4（index[0] = 4）
        if (off < 4 || off + kHeaderSize > indexOffset) {
            break;
        }
        MkfEntry e;
        e.offset = off;
        e.decompressedSize = readU32(m_data.data() + off);
        e.compressedSize = readU32(m_data.data() + off + 4);
        e.dataOffset = readU32(m_data.data() + off + 8);
        e.dataLength = readU32(m_data.data() + off + 12);
        e.dataFileOffset = off + static_cast<uint32_t>(kHeaderSize);
        if (e.dataFileOffset + e.compressedSize > indexOffset) {
            break;
        }
        m_entries.push_back(e);
    }
    return true;
}

// [RE 0x450441] mkfRead
// 依据: 0x4029FD 中 sub_450441(dword_48A0E4, 1, 0, 0) 取 data.mkf 索引 1（菜单资源）；
//       0x406DE7 等多处按 (句柄, 索引, 输出, ...) 调用
// 差异: 原版返回内部缓存指针，当前返回独立 vector；关闭对应 sub_450404
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
