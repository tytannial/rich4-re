// 特殊地点三种小游戏（landingEvent case 6/7/8）：当前为点数兜底分支；交互版逐游戏接入。
// 依据: sub_415215/0x4154DC/0x4155FC + 共享 else 0x415457 反编译；
//       详见 docs/reverse/functions/41982d-p2-events.md §6。
//
// 交互小游戏逆向框架结论（供逐游戏接入，2026-09-25 会话沉淀）：
//   公共流程（三游戏同构）：
//     门控（人类 && settings[1]）→ audioRegisterEffects(音效表) → 载 Panel.mkf 资源
//     → 初始化布局 → musicPlayScene(场景) 压栈 → runModal(WndProc) →
//     musicStackPopRestore → 卸音效 → 释放资源 → 返回得分 dword_48BCEC（→点券）。
//     开场 WM_USER(0x400)：倒计时 dword_48BD2C=150/150/360（tick 100/100/50ms =
//     15/15/18 秒）+ 开场计数 99/99/99 + SetTimer；WM_USER+4(0x404) 开场 FLC
//     panel.mkf[78] 播放（sub_45144F）→ cursorSelect(挖=42/气=(9,3,5)/接=41) +
//     setPauseDraw(1)；0x405 = 开场结束。结束：byte_48BD58 1→2 结算 20 tick →
//     大字显示得分（sub_414789："%d" 数字=panel[79] 子帧 (ch-'0')，x=353-66*len/2,
//     y=150，字宽 66）→ postModalExit。
//     HUD y=421（panel[79] 数字字形，sub_41461B/sub_413A4A/sub_413F07 绘制）：
//     倒计时 3 位 @(49,69,94)+单位 @(114)；四计数 @(185,205)/(276,296)/(367,387)/
//     (458,478)；总分 3 位 @(549,569,589)。
//   case 6 企鵝挖寶（sub_415215→runModal sub_414858，100ms；音乐 12）：
//     9×9 网格 word_474D7C(可埋图)/word_474D80（步距 4 word：+0 有宝, +4 低 nibble
//     翻开态/高 nibble 宝物类型 1..5）；sub_412014 埋宝：5 类数量表
//     g_digTreasureCounts(0x411FC8)={3,12,3,9,1}（Σ28），v=(64递减)*rand()>>15 随机选格。
//     点击：帧 81 像素掩码 (px%9, px/9) → sub_41211C 设企鹅目标（16.16 定点
//     dword_48BD04/48BD08 + 步长 dword_48BCFC/48BD00）→ sub_412287 行走/挖取
//     （音效表 dword_475057=Effect.mkf{11..14,16..18,15} 8 槽）。
//     计数 dword_48BBB4/BBB8/BBBC/BBC0；得分 = 5*BBB4 + 20*BBC0 + 12*BBB8 + 8*BBBC
//     （sub_413A4A）；档位：分>55=4 / 40..55=6 / <40=5（音效 0x475067=13/0x47506F=14）。
//     资源 panel: 78 开场FLC 79 数字 80 背景 81 网格掩码 82-85 企鹅帧
//     86-90 五宝物（[0]=5分,[1]=20,[2]=12,[3]=8,[4]=?1个）待精读 sub_412287/4124C8。
//   case 7 七彩氣球（sub_4154DC→runModal sub_414BBC，100ms；音乐 11）：
//     16 气球 word_48BC44[4i]：+0 x, +2 y, +4 类型（0..8=分值 type+1；9=分×2;
//     10=分÷2；11=特殊 rand%6 {倒计时=1 即终 / byte_48BD59=20 减速 / 风力
//     dword_48BCC8=∓1 / 分=0 / 分×2}；60=已破）。点击命中盒：type>=6 ±18/±26、
//     否则 ±22/±30；未命中音效槽 0x4750A7，命中 0x4750AF（表 dword_47509F=
//     Effect.mkf{19,20,21}）。得分 cap 999。开场 sub_4146EE 生成、每 tick
//     sub_413F07(2) 上升 + sub_412F6F 补充。资源 panel[91]（气球/背景）。
//   case 8 喜從天降（sub_4155FC→runModal sub_414FCD，50ms；音乐 10）：
//     开场 data.mkf[526] FLC（byte_48BD5A + flcDecodeFrame 逐帧，播完直接进结算）；
//     娃娃 = Panel.mkf[100+charIndex]，半宽 word_48BD52=(宽-5)>>1，x 初值 320
//     （word_48BD4E）；钱袋数组 word_48BBC4[4i]（sub_413248 生成/下落/接取，
//     出生 y=word_48BD4C=110）；四类计数 dword_48BBB4/BBB8/BBBC/BBC0（sub_41417E
//     得分 = 10*BBB4 + 5*BBB8 + 3*BBBC + 1*BBC0；时间显示 tick>>1）；
//     钱袋图 panel[95..99]（BD14[0..4]），场景 92/93/94（92=dword_47504B 背景）；
//     结束评级 word_48BD56：>=60→3 / >=50→2 / >=40→0 / <40→1。音效表空(-1)。
//   待精读：sub_412287/4124C8（企鹅移动挖取+第5宝物）、sub_4146EE/413F07/412F6F
//   （气球生成/移动/补充）、sub_413248（钱袋）、sub_41461B/sub_41473B（绘制辅助）。

#include "game/app/minigame.h"

#include <cstdio>
#include <cstdlib>

#include "game/app/item_lines.h"
#include "game/app/message_dialog.h"
#include "game/application.h"
#include "game/core/log.h"
#include "game/game_state.h"

namespace rich4 {

void miniGameVisit(Application& app, int cellType) {
    GameState& st = app.gameState();
    const int p = st.currentPlayer;
    if (p < 0 || p >= 4) {
        // 差异: 四大恶人（事件槽 4..7）忽略（原版亦随机得分）
        RICH4_LOGI("miniGame: cellType %d skip NPC p%d (差异: 四大恶人忽略)", cellType, p);
        return;
    }
    Player& pl = st.players[p];
    if (pl.alive == 1 && st.settings[1] != 0) {
        // [RE 0x41523A/0x4154FA/0x41561A] 原版此处进交互小游戏；未接入前同走下方兜底
    }
    // [RE 0x415457] 共享 else：score = rand()%20+50（50..69）
    const int score = std::rand() % 20 + 50;
    char text[64];
    std::snprintf(text, sizeof(text), "得點券%d點", score);  // [RE 0x463797 cp950]
    showMessage(app, text, 2000);                            // [RE 0x41548E]
    // [RE 0x4154B4..0x4154CF] 台词 sub_44EF41(p, 0, off_48084A[char*27 + (rand&1)])
    const int ci = pl.charIndex;
    if (ci >= 0 && ci < 12) {
        playLine(app, p, kValueLines[ci][std::rand() & 1]);
    }
    pl.points = static_cast<uint16_t>(pl.points + score);  // [RE 0x41B152] g_playerPoints += ax
    RICH4_LOGI("miniGame: cellType %d p%d score %d -> points %u (RE 0x415457)", cellType, p,
               score, pl.points);
}

} // namespace rich4
