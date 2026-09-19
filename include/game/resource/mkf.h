#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace rich4 {

struct MkfEntry {
    uint32_t offset = 0;            // 资源头文件偏移
    uint32_t decompressedSize = 0;
    uint32_t compressedSize = 0;
    uint32_t dataOffset = 0;        // v10
    uint32_t dataLength = 0;        // v11
    uint32_t dataFileOffset = 0;    // 数据区文件偏移

    bool compressed() const { return compressedSize != decompressedSize; }
};

// MKF 资源包，详见 docs/formats/mkf.md。
class MkfArchive {
public:
    bool load(const std::string& path);
    size_t count() const { return m_entries.size(); }
    const MkfEntry& entry(size_t index) const { return m_entries[index]; }
    std::optional<std::vector<uint8_t>> read(size_t index) const;
    const std::string& path() const { return m_path; }

private:
    std::string m_path;
    std::vector<uint8_t> m_data;
    std::vector<uint32_t> m_index;
    std::vector<MkfEntry> m_entries;
};

} // namespace rich4
