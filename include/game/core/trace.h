#pragma once

#include <string>

namespace rich4 {
namespace trace {

// [NEW] 演出/判定调用 Trace（headless 代码级断言支柱之一，无原版对应）
// 记录"呈现请求与关键判定按规格发生"的结构化行：kind=… k=v …，写入内存环形缓冲，
// 供脚本 `assert trace <pattern>` 断言与 --trace-out JSON 导出。
// 画面观感（像素/时序/听感）不在本通道职责内 → 人工验证（docs/testing.md §0）。
// 正常非 debug 运行不记录（setEnabled(false) 零开销）。
void setEnabled(bool on);
bool enabled();

void logf(const char* fmt, ...);
bool contains(const char* substr);
int count(const char* substr);
// 最近一条匹配的行（供失败上下文展示）
bool lastMatch(const char* substr, std::string& out);
void clear();
bool exportJson(const std::string& path);

} // namespace trace
} // namespace rich4
