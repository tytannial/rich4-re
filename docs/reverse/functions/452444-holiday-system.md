# 节日系统（`findHoliday 0x4521F0` + 每日节日处理 `sub_452444`）

> [NEW] M3-B 专项研究文档。目的：补齐**原版有、重写缺/简化**的完整节日系统——
> 浮动节日（type1）、节日效果属性（全屏插画 / 节日音乐 / 人人发卡）、`sub_452444` 演出链。
> 权威依据：`findHoliday 0x4521F0`、`sub_452444`（`advanceDay 0x41CF67` 内 0x41D07B 每日调用）。

## 0. 重写现状与缺口

| 项 | 现状 | 缺口 |
|----|------|------|
| `findHoliday` | 两副本（`game_panel.cpp:396` / `bank_stay_dialog.cpp:140`）✅ type 0/1/2（type1 查 `kFloatHoliday` + 越界 clamp；1998 春节 1/28 冒烟命中 idx17 音乐14） | 两副本重复（可选后续统一） |
| 节日数据表 | `kHoliday[8][24][5]`（`map_tables.cpp:717`）`{flag,type,month,day,weekday}` | **丢原版 +5..+11**：效果 flags / 插画FLC / 音乐idx（`sub_452444` 必需） |
| 浮动节日表 | 无 | 需 `dword_47639C`（按 `daysSince1998` 索引） |
| `sub_452444` 每日节日处理 | ✅ 阶段1+2a：`holidayDaily`（`advanceDay` 0x41D07B 接线）插画 FLC + 节日音乐 + 人人发卡（type0/2 命中即生效；圣诞 12/25 flags=0xf 冒烟全触发） | **type1 农历节日待**（需 `kFloatHoliday` 表 + `findHoliday` type1 + 超界 clamp） |
| 日历红字 / 节日背景 / 休市 / 停业 | 已实现（type0/2 命中即生效） | type1 命中的节日这些链自动补齐 |

## 1. 数据表：`g_holidayTable 0x47FF4A` = `[8][24][12]`

`base = 4*gameMode + mapIndex`（8 组）；每组 24 条，每条 **12 字节**：

| 偏移 | 原符号 | 含义 |
|------|--------|------|
| +0 | `g_holidayTable`(0x47FF4A) | flag：日历红字/有效（`>=0`；`0x80`=128 结束/占位哨兵，`findHoliday` 判 `h[0]<0x80`） |
| +1 | `byte_47FF4B` | type：0=固定月日 / 1=浮动(查 dword_47639C) / 2=第 N 个星期 X |
| +2 | `byte_47FF4C` | month（type0/1）或 目标月 |
| +3 | `byte_47FF4D` | day（type0）／ 目标月日低位（type1 比较）／ N-th（type2） |
| +4 | `byte_47FF4E` | weekday（type2 用） |
| +5 | `byte_47FF4F` | **效果 flags**：`&1` 全屏插画、`&4` 节日音乐、`&8` 人人发卡（`&2` 含义待核） |
| +6/+7 | `word_47FF50` | 插画 `data.mkf[idx]`（FLC，`sub_450441(dword_48A0E4,...)`） |
| +8/+9 | `word_47FF52` | 插画 FLC 参数（`sub_45144F` 末参，如声音/停留，元旦=0x5a=90） |
| +10/+11 | `word_47FF54` | 节日音乐场景索引（`musicPlayScene(idx \| 0x8000)`，bit15=不压栈） |

样例（base0）：`{01 00 01 01 00 | 03 | 0b 02 | 5a 00 | 00 00}` = 元旦 1/1，flags=0x03，
插画 `data.mkf[523]`，FLC参数 90，无音乐、无发卡。

> 提取任务：脚本从 exe 0x47FF4A 起读 8×24×12=2304 字节生成 `kHoliday[8][24][12]`
> （替换现 5 字节版）；从 `dword_47639C` 读浮动表（见 §3）。仿 `tools/gen_lzhuf_tables.py`
> 落到 `map_tables.cpp`（勿手改），VA→文件偏移 = `VA - 0x401A00`。

## 2. `findHoliday 0x4521F0`（三 type）

`v2 = 当前日期的 月<<8|日`（type0 用）；遍历 24 条命中返回索引否则 -1。
- **type 0 固定**：`a1 = 目标月<<8|日`，`v2==a1` 即命中。
- **type 1 浮动**：`v2 = (u16) dword_47639C[ daysSince1998(date) ]`（把绝对日序映射为浮动月日，
  如农历/复活节类随年移动者）；`a1 = 记录月<<8|日`；`v2==a1` 命中。
- **type 2 第 N 星期 X**：`calcCalendar` 求该年月首日星期 + 当月天数，
  `wd = weekday; if(wd<firstWd) wd=7; d = 7*(N-1)+wd-firstWd+1`；`d<=当月天数` 且 `月<<8|d` 命中。
- 末尾统一：`v2==a1 && g_holidayTable[base][i] >= 0` → 返回 i。

现有 `firstWeekday/daysInMonth` 等价 `calcCalendar`；补 type1 需 `daysSince1998`（已有 `date_util.cpp:21`）+ 浮动表。

## 3. `dword_47639C` 浮动节日表

`u32[]`，按 `daysSince1998(date)` 索引，取低 16 位 = `月<<8|日`（样本首项 `0x0c03`=12/3、递增）。
- **已确定（2026-09-29）**：表长 **8401** 项（day0..8400，1997/12/3→约 2021；day8401 起为紧邻垃圾数据），
  生成 `kFloatHoliday[8401]` u16（`holiday_tables.cpp`，`gen_holiday_tables.py --write` 提取，存 low16=农历月<<8|日）。
- **语义确认**：`kFloatHoliday[daysSince1998(公历日)] = 农历月<<8|日`；type1 节日 `h[2]<<8|h[3]` 为农历目标
  月日，相等即命中（春节=农历1/1、端午=5/5、中秋=8/15、除夕=12/大尽）。验证 1998-01-28→float[27]=0x0101→
  base0 idx17（音乐14）命中。**超界保护**：`days<0 || days>=8401`（2021 后系统日期）→ type1 不命中
  （与原版表边界一致；原版越界为未定义读，重写显式 clamp 更安全）。

## 4. `sub_452444 0x452444`（每日节日处理，`advanceDay` 0x41D07B 调用）

```
Holiday = findHoliday(?, dword_497160/*当前日期*/);
if (Holiday>=0 && flags(base,Holiday)!=0) {
  save panelLayout(0x49715D); if(==1)0; sub_41906A(1)/*刷新*/;
  f = byte_47FF4F[base组 + 12*Holiday];
  v2=0; v16=0;
  if (f&1) { v2 = data.mkf[word_47FF50[..]];  v16 = word_47FF52[..]; }       // 插画资源
  if ((f&4) && !g_musicTimer) {                                               // 节日音乐
     music = word_47FF54[..] | 0x8000;  sub_454EDC();  musicPlayScene(music);
     g_musicTimer = (word_47FF54[..+6]? ) ? 51 : 17;                          // 0x33/0x11
  }
  if (v2) { sub_45144F(v2, 0, 40, /*透明*/1, v16); free(v2); }                // 全屏节日插画 FLC
  if (f&8) { for 每存活玩家 i: c=sub_441E12(i)/*抽赠卡*/; if(c){ refreshGameUi(玩家i坐标);
             copyNameNoSpaces; sprintf 按 v13=4*mode+map(0..6) 文案 unk_4661C4/1DD/1F2/203;
             sub_441F73(c, text)/*showCardGet*/; playValueLine(i, byte_47FDEF[c]); } }  // 人人发卡
  if (panelLayout 原==1) 1; dword_48BE18=0; sub_41906A(1);
}
```

三效果各自可独立降级（插画=现有 `playEventFlc`、音乐=`playSceneMusic`、发卡=现有 `drawFreeCard`+
`showCardGet`+`playValueLine`），复用 M2 原语。文案按地图组（v13）6 变体（unk_4661C4/1DD/1F2/203…）。

## 5. 接线点

- `advanceDay`（`turn_system.cpp`）：`stockTick` 之后、15 号分红之前插入 `holidayDaily(app)`
  （原版 0x41D07B `sub_452444()`）。
- 统一 `findHoliday`：抽到单一实现（`map_tables`/`date_util` 或新 `holiday.cpp`），
  `game_panel`/`bank_stay` 改引用；补 type1。

## 6. 实施顺序（建议分步 wip 提交）

1. **数据提取脚本** `tools/gen_holiday_tables.py` → `kHoliday[8][24][12]` + `kFloatHoliday[]`
   落 `map_tables.cpp`（扩展现 5 字节表；VA→文件偏移 `VA-0x401A00`）。
2. `findHoliday` 统一 + 补 type1（浮动表）；两调用点改引用。日历红字/背景/休市/停业随之覆盖 type1。
3. `sub_452444`→`holidayDaily(app)`：插画（`playEventFlc`）+ 节日音乐（`playSceneMusic|0x8000`+
   `musicTimer`）+ 人人发卡（`drawFreeCard`+`showCardGet`+`playValueLine`+文案表）；接 `advanceDay`。
4. 测试：`--exec` 强制日期命中各 type 节日（新增 debug `holiday <date>` 或 `date.set`），
   断言插画/音乐/发卡 trace；`288_holiday_effects.txt`。

## 7. 验收 / 待实机

- type1 浮动节日命中日期与原版一致（对拍农历/固定浮动节）。
- 节日插画 FLC 落点（原版 `sub_45144F(v2,0,40,1,v16)` = @(0,40) 透明=1）；v16 语义（声音/停留）核。
- 节日音乐是否续播背景（bit15=0x8000 不压栈 → 不保存当前曲，符合原版）。
- 人人发卡：`drawFreeCard 0x441E12` 复用赠卡池；卡满时原版行为（舍弃/不发放）核对。
- flags bit1(&2) 含义核实。
