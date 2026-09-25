#pragma once

namespace rich4 {

class Application;

// [RE 0x42E931] shopDialog：百貨公司（特殊地點 case 15，卡片店/道具店）。
//   objId ∈ (6000,8000)（cellEnt+32 special）→ specPt 索引 = objId-6000：
//   - 自己拥有（owner == cur+1）→ rand 50% 道具 / 50% 卡 + showMessage("歡迎董事長光臨…")
//     + playValueLine 价值语音（0x42E93F..0x42EA2B）
//   - 人类（alive==1）→ 商店 UI（sub_42D37F，50ms tick）：panel.mkf[10] 38 帧
//     （卡片店 0..15 / 道具店 16..31 / 共享 32..37）+ panel.mkf[11] 卡包/道具包；
//     首次进入每页：欢迎消息 → 抽屉缓动滑入（x -222→5 / y 640→227）→ 提示消息 → 可交互；
//     切页按钮 (542,13)、离开按钮 (556,246)、卖出区 (232,298) 5×3 格、商品列表点击购买
//   - AI（alive!=1）→ 自动买卖（0x42ED8D..0x42F30C）：性格匹配卖卡/道具、点券<100 时
//     卖最低价卡/多余道具/下车、按库存×价格降序买卡、[7,1,6,0,3,2] 顺序买礼物道具
//   买卖营业额（买 10×价 / 卖 1×价，0x42D237/42D272/42D145/42D1B2）累加 specPt.fund/+44
//   详见 docs/reverse/functions/42e931-department-store.md
void shopDialog(Application& app, int objId);

} // namespace rich4
