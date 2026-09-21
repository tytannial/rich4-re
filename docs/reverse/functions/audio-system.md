# 音频系统（DirectSound/MCI → SDL 混音 + stb_vorbis）

## 原版架构

| 子系统 | 原版实现 | 关键函数 |
|--------|----------|----------|
| 初始化 | DirectSound 主缓冲 22050Hz/8bit/mono | `0x453B55` sub_453B55 |
| 音效库 | 按界面分组注册 Effect.mkf WAV（{id, buffer} 表，-1 结束，16 组） | `0x454176` sub_454176（注册）/ `0x454240` sub_454240（注销） |
| 音效播放 | DirectSound buffer Play + 音量表 `dword_47E758` | `0x4540D8` sub_4540D8 / `0x4542CE` sub_4542CE（结构指针入口） |
| 音乐 | MCI `open sequencer!<file> alias mid` + `play mid from 0 notify`（MIDI）或 `play cdtrack from N notify`（CD 音轨） | `0x454D91`（8 首曲目切换）/ `0x4549CF`（场景音乐）/ `0x454ACB`（停止）/ `0x454F5B`（当前曲目） |
| 音量 | 音效：播放时查 `dword_47E758[byte_49715B]`；音乐：`midiOutSetVolume`/`auxSetVolume` | `0x4548EF` sub_4548EF |

## 关键数据

| 符号 | 地址 | 语义 |
|------|------|------|
| `byte_49715A` | 0x49715A | 音乐音量 0-4（settings[2]，0 = 静音） |
| `byte_49715B` | 0x49715B | 音效音量 0-4（settings[3]，0 = 静音） |
| `off_47E773` | 0x47E773 | 8 首可选曲目表：RICH08.MID / RICH16-22.MID |
| `off_47E793` | 0x47E793 | 场景音乐表（17 项）：MIDI01-13 / MIDI14-1 / MIDI14-2 / MIDI15 / MIDI16 |
| `byte_47E770/771/772` | 0x47E770 | CD 模式标志 / 当前曲目索引 / 播放状态 |
| `dword_47E758` | 0x47E758 | 音效音量表（DirectSound 百分之一分贝：-10000/-2000/-1000/-316/0） |
| `unk_48231A` | 0x48231A | UI 音效结构组：`{id, buffer}` 对，id = {0,1,2,4,3}（悬停/点击/確定/取消/音乐提示） |
| `unk_48234A` | 0x48234A | 游戏音效**槽**数组：`dword_48234A[2*slot]` = Effect.mkf 索引，slot 0..23 = {7,9,10,32,33,34,35,36,37,38,43,44,45,46,53,47,48,49,50,54,55,56,15,62} |
| `unk_46CCD0` | 0x46CCD0 | 单音效组：id {5}（选人进入阶段） |
| `dword_48235A` | 0x48235A | = `dword_48234A[2*2]` → 游戏音效槽 2（骰子落地） |
| `dword_482362` | 0x482362 | = `dword_48234A[2*3]` → 游戏音效槽 3（掷骰） |
| `dword_4823FA` | 0x4823FA | = `dword_48234A[2*22]` → 游戏音效槽 22（格子事件动画） |

## 音效 id（Effect.mkf 索引）UI 映射

| id | 用途 | 调用点示例 |
|----|------|-----------|
| 0 | 悬停 | 主菜单 0x402892、读档 0x403722、选人 0x4051F3、设置 0x4106D3 |
| 1 | 点击/选中/切换 | 主菜单 0x40262B、选人 0x405384/0x4055CF/0x405D53、设置多处 |
| 2 | **確定** / 列表选择 | 设置確定 0x4106D9、选人確定 0x404E44、读档 0x403909、热键 0x4116DB、地图选择 0x406830 |
| 3 | 音乐列表点击（静音提示） | 设置 0x4106B2 |
| 4 | **取消** / 分类 | 设置取消 0x4106D9、选人取消 0x404E44、读档 0x403936、热键 0x411659、帮助 0x44E548 |
| 5 | 选人进入阶段（补齐完成） | 选人 0x405D03（unk_46CCD0） |

## 媒体资源（resources/Media）

| 文件 | 内容 |
|------|------|
| `Music/track02-26.ogg` | CD 音轨 2-26（44100Hz stereo）；track02-09 = 8 首可选曲目，track10-26 = 17 首场景音乐 |
| `tracklen.nfo` | CD 音轨布局（track 1 = 数据轨） |
| `*.avi` | 开场/结局视频（Start/End/Over/Thanks/END01-12/Fly*/airplane，播放待接入） |

## 重写实现

| 原版 | 重写 | 说明 |
|------|------|------|
| DirectSound 主缓冲 + 多 buffer | `Audio`（`src/platform/audio.cpp`） | SDL_AudioStream 22050Hz/8bit/mono；`update()` 每帧软件混音活动声部 + 音乐后喂流 |
| Effect.mkf WAV 分组注册 | `Audio::playEffect(id)` | 懒加载 + 缓存（RIFF 解析后 8bit PCM） |
| 音量表 | `Audio::setEffectVolume/setMusicVolume`（0-4） | 增益 {0, 2, 5, 11, 16}/16（近似原版分贝表） |
| MCI MIDI/CD | `Audio::playMusic/playSceneMusic` | **stb_vorbis**（`third_party/stb/stb_vorbis.c`，公共领域）解码 OGG → 22050 mono；播完自动切下一首（8 曲目循环） |
| `sub_454F5B` 当前曲目 | `Audio::currentTrack()` | 设置界面音乐列表红色高亮 |
| `sub_4549CF` 场景音乐 | `Audio::playSceneMusic(index)` | 主菜单 index 0（track10）、选人 index 1（track11） |
| `sub_454D91(0)` 切下一首 | `Audio::playNextMusic()` | 进游戏 0x4019C8、过天计时器 `byte_46CB06`（`advanceDay`） |
| `sub_454B1A` 播完处理 | `Audio::update()` 播完自动下一首 | 可选曲目 `(cur+1)&7` 循环 |
| `sub_410991` 確定音乐处理 | `settings_dialog` case 8 | 仅音量 0↔非0 时停止/重播；非0 之间只调音量（**取消不改动音乐**） |
| `sub_4542CE/4542E9` 游戏音效槽 | `Audio::playEffectSlot/stopEffectSlot` | 同槽替换（原版单 buffer：先停后播） |
| `sub_451387` FLC 帧触发音效 | `turn_system` 骰子阶段1 | 骰子 FLC 第 30 帧（`dword_48C850=0x1E`）播槽 2 |

### 界面接入

- 主菜单：悬停 id=0 / 点击 id=1；进入播放场景音乐 0，退出停止（`0x4029FD`/`0x454ACB`）
- 读档：悬停 id=0 / 选槽 id=2 / 取消 id=4
- 选人：悬停 id=0 / 选中·切换·补齐 id=1 / **確定 id=2、取消 id=4**（`&dword_48231A+8*控件+8`）/ 选项(3-8) id=0 / 进入阶段 id=5；进入播放场景音乐 1
- 设置：悬停 id=0 / 滑块·页按钮·复选 id=1 / 音乐列表 id=3（静音时）+ `playMusic(index)` / **確定 id=2、取消 id=4**；確定仅静音 0↔非0 时停/重播，取消不动音乐
- 日期/热键/帮助：点击 id=1（热键列表 id=2、帮助分类 id=4）
- 游戏内：悬停 id=0 / 点击 id=1（前进面板/小地图箭头/工具条/日历切换）
- 掷骰（`case2`）：角色扔骰动画**无音效** → 骰子 FLC 第30帧播槽2（落地①）→ FLC 结束播槽2（落地②）→ 转移动前播载具槽(11-15)+槽3（玩家<4 且 cellNo）
- 移动：每步组切换播载具槽（**走路/载具 = `travel&3`+11；水上/渡水（cellEnt+39 bit31=1）= 槽 15**，
  同槽替换）；移动结束 `stopEffectSlot` 停止

### 依赖

`third_party/stb/stb_vorbis.c`（v1.22，公共领域；实现编译单元 `src/platform/stb_vorbis_impl.cpp`，
声明以 `STB_VORBIS_HEADER_ONLY` 方式包含于 `audio.cpp`）。CMake 已加入 `third_party/stb` include 路径。
