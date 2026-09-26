#pragma once

#include <cstdint>

namespace rich4 {

struct GameState;
class Application;

// 地图物件（神明/禮物/寶箱/惡犬/道具）生命周期，见
// docs/reverse/functions/map-object-refresh.md。

// [RE 0x40EA62] canAttach：可附身物件 = type≤12 且 ≠11(惡犬)，或 15(死神)
bool canAttachObject(const GameState& st, int objId);

// [RE 0x40EAD7] attachObject：玩家停留踩中可附身物件（旧神飘走→新神挂身
//   cellTableIdx、寿命 7/13、luckA/B/C += 表、case 即时效果）
void attachObject(Application& app, int player, int cellEntId, int objId);

// [RE 0x40E32C] attachEnd：附身结束（24 帧上升飘走动画）→ deleteMapObject（轮替）。
//   stateFlags≠0（挂起态）时跳过动画静默删除
void attachEnd(Application& app, int player);

// [RE 0x440706] slotMachineDialog：財神/窮神数字老虎机（panel.mkf[67]）。
//   a1=0/1 小/大財神、4/5 小/大窮神；a1&1=0 → 4 轮 0..9999，否则 3 轮 0..999
int slotMachineDialog(Application& app, int a1);

// [RE 0x40CD07] damagePlayer：载具损毁 + 受伤标记（恶犬咬/地雷/炸弹爆炸），
//   已住院/出国（stateFlags≠0）时不生效
void damagePlayer(Application& app, int player);

// [RE 0x40AB4A] demolishAtObjId：住宅/商業用地（2000+/4000+）拆除：
//   mode 0 = 降一级（type≠0 直接清设施）、1 = 没收重置、2 = 拆建筑留地
bool demolishAtObjId(Application& app, int objId, int mode);

// [RE 0x445ADA] drawGiftCard：礼物卡池（misc8A[8] 按库存展开）随机抽 1 张，返回卡 id（0=池空）
int drawGiftCard(Application& app, int player);

// [RE 0x441E77] discardRandomCard：随机丢弃 1 张卡（返回卡 id，0=空）
int discardRandomCard(Application& app, int player);

// [RE 0x441ECE] discardHalfCards：丢弃卡包前 数量/2 张（有丢弃返回 1）
int discardHalfCards(Application& app, int player);

// [RE 0x445B3F] confiscateItems：没收玩家全部道具（前 8 类回礼物池 misc8A、载具归还
//   槽 4/5/11），返回没收道具价格合计（点券）
int confiscateItems(Application& app, int player);

// [RE 0x441F21] confiscateCards：没收卡包全部卡（赠卡池 propStock 归还），返回价合计
int confiscateCards(Application& app, int player);

// [RE 0x41D3F4] addMoney：入账（cash=true 现金否则银行）+ 本月意外之财统计
// 尾：player==当前玩家 → refreshPlayerPanelFor（0x41D433，无模态时立即重绘面板）
void addMoney(Application& app, int player, int32_t amount, bool cash);

// [RE 0x40E2A2] showGodNarration：神明旁白（屏幕下方 28px 白字黑影，2400ms）
void showGodNarration(Application& app, const char* text);

// [RE 0x40FAFD] bounceObject：物件弹飞（设 f32 位置/速度 + flyCount=-1，由 renderMap 驱动）
void bounceObject(GameState& st, int objId, int fromCell, int toCell);

// [RE 0x40E669] flyObjectSprite：物件精灵 A→B 插值动画（世界坐标，阻塞）
void flyObjectSprite(Application& app, int objId, int fromX, int fromY, int toX, int toY,
                     int holdMs);

// [RE 0x40FC00] 附身/挂身物件 cellEnt 同步为玩家当前格（moveOneStep/入狱/住院后调用）
void updatePlayerCarriedObjects(GameState& st, int p);

// [RE 0x40AF12] getObjectPosition：objId → 世界像素坐标（<2000 cellEnt / estate / corp /
//   specPt / evtCell / 0x8000|玩家位掩码 / 事件槽 / 挂身物件 cellTable 槽）
void getObjectPosition(const GameState& st, int objId, int& outX, int& outY);

// [RE 0x40CC56] relocatePlayer：清本格占用 → randomCellEnt(参照本格) 随机换位 +
//   朝向重算 + 占新格（乞丐施捨后打发；淘汰/传送类复用）
void relocatePlayer(GameState& st, int p);

// 神明旁白文本（索引 = cellTable 类型 1..15；[RE 0x463250..0x463495] BIG5→UTF-8）
const char* godNarrText(int type);

// [RE 0x444D1A] pickNearestAttachable：视野内（mapHitRegions 的 cellTable 物件）
//   可附身+无主、距当前玩家世界坐标最近者，返回物件 ID（0=无）
int pickNearestAttachable(Application& app);

// [RE 0x444E1A] useInviteGodCard：请神符(卡23)——选中最近神明 → 丢卡 →
//   飞行动画（神明→玩家）→ attachObject 附身。返回是否成功（无候选失败不丢卡）
bool useInviteGodCard(Application& app);

// [RE 0x444C45] useBanishGodCard：送神符(卡22)——挂身道具(炸弹)直接清除；
//   附身负面神明（窮/衰/惡魔/死神 5/6/7/8/10/15）送走（→轮替）；
//   正面神无可送 → 失败不丢卡。返回是否成功
bool useBanishGodCard(Application& app);

// [RE 0x445A4D] givePlayerItem：发放道具（itemStock[15p+id-1]++，上限 9；
//   id≤8 受礼物池 misc8A[id-1] 库存限制）
void givePlayerItem(GameState& st, int player, int itemId);

// [RE 0x445AA2] takePlayerItem：扣除道具（对称；id≤8 归还礼物池），成功返回 true
bool takePlayerItem(GameState& st, int player, int itemId);

} // namespace rich4
