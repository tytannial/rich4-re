#pragma once

namespace rich4 {

class Application;

// [RE 0x403D74] loadDialog
// 依据: 0x403D74 反编译; sub_450441(data.mkf, 520) 界面资源 + (data.mkf, 2) 头像资源;
//       扫描 SAVE0.DAT..SAVE5.DAT 绘制槽位; runModal(sub_40363A) 事件处理
// 返回选中槽索引（0-5）；取消（右键）或无效返回 -1
int loadDialog(Application& app);

} // namespace rich4
