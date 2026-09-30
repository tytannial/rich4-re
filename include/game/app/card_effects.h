#pragma once

#include <cstdint>

namespace rich4 {

class Application;

// [RE 0x475D5C] g_cardEffectFuncs 卡片效果（P4）。
// 契约（useCardFlow 0x441BAA）：返回非 0 = 结束流程（已消耗）；返回 0 = 重弹面板（不消耗）。
// 消耗由效果函数内部 cardBagRemove 完成。
// AI 分支：原版 sub_41E6F2(0) = dword_48BE58 预选目标（由 AI 用卡链 sub_41E69E 填写，P5）；
//   useCardFlow 仅 alive==1 进入，故当前只实现人类路径。
// 逐卡规格见 docs/reverse/functions/441baa-card-effects.md。

// id1 均富卡 [RE 0x4420D8]：全员现金取均值（超出均值部分 1/100 记债给使用者）
int cardEffectEqualRich(Application& app);
// id2 均貧卡 [RE 0x4421B4]：选一名玩家，两人现金取均（差额 1/100 记债给使用者）
int cardEffectEqualPoor(Application& app);
// id3 購地卡 [RE 0x442325]：强制收购当前所站他人地块（价 = (level*priceBase+priceAdd)*M）
int cardEffectBuyLand(Application& app);
// id4 換地卡 [RE 0x442622]：脚下地块与所选同类地块交换 owner（地权互换）
int cardEffectSwapLand(Application& app);
// id5 換屋卡 [RE 0x442B02]：脚下地块与所选同类地块交换房屋（level/sub + type，地权不变）
int cardEffectSwapHouse(Application& app);
// id6 轉向卡 [RE 0x442F4D]：选一名玩家令其掉头（turnToAdjacentCell 0x40C78C）
int cardEffectTurn(Application& app);
// id8 拍賣卡 [RE 0x443225]：脚下地强制拍卖（卖方=使用者，补偿原主 M*base*(level+2)/5；流标归公）
int cardEffectAuction(Application& app);
// id11 怪獸卡 [RE 0x443917]：选一块地彻底夷平（demolishAtObjId mode2）+ FLC 557
int cardEffectMonster(Application& app);
// id12 拆除卡 [RE 0x443B0F]：选目标降一级 / 拆连锁 / 拆路面道具 + FLC 529
int cardEffectDemolish(Application& app);
// id13 搶奪卡 [RE 0x443E3D]：选一名玩家夺取其一张卡或道具（记债卡价/道具价）
int cardEffectSteal(Application& app);

// ===== 档B =====
// id9 天使卡 [RE 0x4434C0]：选一条路段整段加盖（estate 同名批量 level++ / corp angelUpgrade）
int cardEffectAngel(Application& app);
// id10 惡魔卡 [RE 0x4436E0]：选一条路段整段夷平（level/type 清零 + 原主记债 30×M×level）
int cardEffectDevil(Application& app);
// id14 停留卡 [RE 0x443F80]：选一名玩家令其下次前进停留（skipMove）
int cardEffectStay(Application& app);
// id15 冬眠卡 [RE 0x4440EA]：全体对手冬眠 5 天（byte54=5）并各记债 150×M 给使用者
int cardEffectHibernate(Application& app);
// id16 夢遊卡 [RE 0x4441DC]：选一名玩家梦游（state37=4/5 + 清载具），经免罪/嫁祸/復仇被动链
int cardEffectSleepwalk(Application& app);
// id17 陷害卡 [RE 0x4444BF]：选一名玩家入狱 5 天（反弹自己 4 天），经免罪/嫁祸/復仇被动链
int cardEffectFrame(Application& app);
// id26 查稅卡 [RE 0x4451F0]：选一名玩家收取其现金 20% 税，经免費抵用/嫁祸链
int cardEffectTaxAudit(Application& app);
// id27 漲價卡 [RE 0x44542D]：选一条路段 flag 高半字节=5（加倍收费 5 天）
int cardEffectPriceUp(Application& app);
// id28 查封卡 [RE 0x445593]：选一条路段 flag=0x51（免收租 5 天；研究所停研发）
int cardEffectSeal(Application& app);
// id29 同盟卡 [RE 0x445710]：与目标结盟 7 天（先解双方旧盟）
int cardEffectAlly(Application& app);
// id30 烏龜卡 [RE 0x4458DF]：选一名玩家令其下次前进只走 1 步（fixedStep=2 自己/3 他人）
int cardEffectTurtle(Application& app);
// id24 紅卡 [RE 0x444F25]：选股票涨停 3 天（stockNews 高 4 位 = 3）
int cardEffectRedStock(Application& app);
// id25 黑卡 [RE 0x44503F]：选股票跌停 3 天 + 持股者损失记债（股数×跌幅/200）
int cardEffectBlackStock(Application& app);

// ===== 被动卡（不经 g_cardEffectFuncs 主动表，由惩罚/收费链调用）=====
// id18 復仇卡 [RE 0x444691]：受害者显示/消耗復仇卡(18) + 台词；实际反伤由调用方执行
void triggerRevengeCard(Application& app, int victim);
// id20 免費卡 [RE 0x444A60]：受罚者 target 费用 fee 时询问/判定用卡抵用；返回 1 = 已抵用
int applyFreeCard(Application& app, int target, int32_t fee, int feeOwner = -1);

} // namespace rich4
