#include "microtest.h"

#include <string>

#include "game/resource/mkf.h"

// [NEW] L0 单测：MKF 读取 + LZHUF 解压往返（docs/formats/lzhuf.md）
MT_TEST(mkf_data_read_stable) {
    const std::string path = mt::g_gameDir + "/Data.mkf";
    rich4::MkfArchive mkf;
    if (!mkf.load(path)) {
        std::printf("  SKIP (no %s)\n", path.c_str());
        return;
    }
    auto first = mkf.read(0);
    MT_CHECK(first.has_value());
    MT_CHECK(!first->empty());
    // 解压确定性：同一项两次读取字节一致（LZHUF 工作区重置约束）
    auto again = mkf.read(0);
    MT_CHECK(again.has_value());
    MT_CHECK(*first == *again);
    auto other = mkf.read(537); // 事件格 FLC（得點券）
    MT_CHECK(other.has_value());
    MT_CHECK(other->size() > 16);
}
