# 资料、代码和素材来源

## 经典版素材

地图阶段改用用户提供的 `assets/mpq2` 五个经典完整版／资料片 MPQ；文件摘要见 [MPQ 资源](MPQ_RESOURCES.md)。地形及原表均从这些档案读取，没有引入第三方 JSON 地图。以下试玩来源保留用于旧版本适配。

- [Blizzard 历史下载导航](https://classic.battle.net/diablo-universe.shtml) 曾提供 Diablo II Playable Demo。
- [ModDB 的 Diablo II Demo 页面](https://www.moddb.com/games/diablo-2/downloads/diablo-ii-demo) 标注官方 Windows 试玩版、文件大小 138,309,685 字节和 MD5 `9ae5033551a078937cd5d1f388cd8438`。
- 实际下载来源：[Internet Archive 的 Diablo II Demo 存档](https://archive.org/details/DiabloIiDemo)。完整下载在本次开发中与上述大小、MD5 一致；随后直接读取包内 MPQ，没有执行安装器。
- 供后续导入的脚本只请求源文件对应 MPQ 的字节范围。每个 MPQ 的 SHA-256 和偏移固定在 `scripts/fetch-demo.ps1`，用于识别下载损坏与服务器忽略 Range 的情况。

素材属于 Blizzard Entertainment。试玩包的公开下载与本项目代码的开源许可是两件事；仓库忽略所有 MPQ、下载缓存和导出的原版图片。请保留代码与素材各自的权利说明。

物品定义由 `src/content/classic_data.cpp` 与 `lod_data.cpp` 从当前 MPQ 的 `misc.txt`、`weapons.txt`、`armor.txt` 运行时导入；正式包读取命名类型和 `TreasureClassEx`，旧试玩适配保留。原表属于游戏资源。包裹／储物箱尺寸依据 `inventory.txt` Barbarian / Bank Page 1；当前加载 34 种物品美术，自定掉落权重已删除，原版生成尚未执行。详见 [原版物品数据](ITEM_DATA.md)。

包裹面板读取当前 MPQ 的 `data/global/ui/panel/invchar.dc6`，按右侧面板分块拼接原版石框和格子，保留上方装备区域，物品详情以悬停提示呈现。该 DC6 与物品图标保留暴雪素材权利说明。

## 实际使用的开源项目

| 项目 | 用途 | 固定版本/来源 | 许可 |
| --- | --- | --- | --- |
| [raylib](https://github.com/raysan5/raylib) | 窗口、输入、OpenGL、音效、图片导出 | 5.5 / `c1ab645ca298a2801097931d1079b10ff7eb9df8` | zlib |
| [StormLib](https://github.com/ladislav-zezula/StormLib) | MPQ 挂载、读取、压缩打包 | v9.30 / `86f9b99ffe4d3417dad16d00541cf6f2e3d7bf79` | MIT，附带库保留各自许可 |
| [nlohmann/json](https://github.com/nlohmann/json) | 应用层调试命令 JSON 编解码 | 3.11.3，发布归档 SHA256 固定于 CMake | MIT，见 [许可](licenses/nlohmann-json.txt) |
| [DGEngine](https://github.com/dgcor/DGEngine) | DCC 解码器的直接改造来源 | `ae6dcabf4f824d617dc4b15ead1f0ef206c9a106`，`src/Resources/ImageContainers/DCCImageContainer.cpp` | Diablo 格式代码使用 GPL-3.0 |
| [Worldstone](https://github.com/Lectem/Worldstone) | DGEngine DCC 解码器的上游算法 | 由 DGEngine 说明和代码引用 | GPL-3.0 |
| [OpenDiablo2](https://github.com/OpenDiablo2/OpenDiablo2) | 第一幕对象预设数据、经典 HUD 布局参考 | `d2core/d2records/object_lookup_record_data.go` | GPL-3.0 |
| [D2MOO](https://github.com/ThePhrozenKeep/D2MOO) | Cave／Crypt 迷宫、矩形户外布局／边界及人口规则适配 | `5596f5cb6c5251a0a07c6637d26458b06099d516` | MIT，见 [许可](licenses/D2MOO.txt) |

`src/resources/dcc.cpp` 已标注改动：C++ 索引帧接口、一次性解码所有方向、输入范围检查。`src/resources/presets.hpp` 从 OpenDiablo2 的第一幕数据筛选生成。项目整体使用根目录 `LICENSE` 的 GPL-3.0 文本；相关许可保存在 `docs/licenses`。

## 格式研究参考

本地 `reference/` 下有四份忽略提交的参考仓库，优先在本机核对，再按需要查其他来源：

| 本地目录 | 固定提交 | 适用范围 |
| --- | --- | --- |
| `reference/d2moo/` | `5596f5c` | D2Common／D2Game 的规则、地图生成、掉落与 NPC；此快照没有 D2Client 角色属性／技能面板布局实现 |
| `reference/opend2/` | `0578244` | DT1、DS1、COF、字体结构及旧客户端菜单；不是 OpenDiablo2，游戏内角色面板尚无可用布局 |
| `reference/dgengine/` | `ae6dcab` | DCC、DS1、DT1 解码实现；项目目标为 Diablo I，引擎 UI 不可直接用于本作布局 |
| `reference/dgengine-core/` | `dd600ab` | DGEngine 的通用 2D 引擎组件；不提供 Diablo II 规则或面板布局 |

角色面板布局本轮另与 [OpenDiablo2 的 hero_stats_panel.go](https://github.com/OpenDiablo2/OpenDiablo2/blob/master/d2game/d2player/hero_stats_panel.go) 核对；该仓库不在当前 `reference/` 目录。坐标仍以用户截图及当前 MPQ 图框核对，运行时图像只从 MPQ 加载。引用、改编和权利按下方各项目说明保留。

本轮世界生成代码依据 D2MOO 的 `DrlgMaze`、`DrlgOutPlace`、`DrlgOutWild`、`DrlgOutdoors`、`DrlgRoomTile` 和 `DrlgDrlgVer` 适配；附近房间策略参考 `DrlgActivate`。这是实际代码适配来源，不是完整 DRLG 或逐种子等价实现。当前入口见 [地图](ACT1_MAPS.md) 与 [数据生命周期](baseline/DATA.md)。

- [D2MOO](https://github.com/ThePhrozenKeep/D2MOO/tree/5596f5cb6c5251a0a07c6637d26458b06099d516)，固定提交 `5596f5cb6c5251a0a07c6637d26458b06099d516`，MIT，Copyright 2020–2025 The Phrozen Keep community。共有 DT1、隐藏空白瓦片和变体权重解释依据 DrlgRoomTile.cpp；生成边界参考 DrlgDrlg.cpp、DrlgPreset.cpp、DrlgMaze.cpp。许可见 [D2MOO.txt](licenses/D2MOO.txt)。没有移植完整 DRLG，也未执行参考仓库的游戏代码。

  女巫技能还核对了该快照的 `D2Common/src/D2Skills.cpp`（等级伤害分段与定点法力）、`D2Game/src/SKILLS/SkillSor.cpp`（传送的 Levels 许可与静电力场生命下限）。本项目从用户 MPQ 在运行时读取每个技能和难度的实际数值；现有战斗系统尚不具备原版完整施法帧、抗性与逐弹命中规则。

  怪物生成阶段还依据该固定提交的 MonsterRegion、MonsterChoose、MonsterSpawn、MonsterUnique、MonsterTbls 和 D2Common Monsters，适配区域选择／密度、随从、精英、固定首领和家族链规则。当前使用本项目的 DS1 空间与随机流适配，不声称逐种子或逐帧等价。代码入口和执行／暂缓字段见 [怪物生成](MONSTER_POPULATION.md)；原始 MonStats2、MonPreset、SuperUniques、MonPlace、MonUMod 表来自用户 MPQ。

- [OpenD2](https://github.com/eezstreet/OpenD2)，参考提交 `057824439ca145aa8411f9b024bc3fe6aa8fa450` 的 DT1、DS1、COF 和 Font TBL 结构说明。
- [DGEngine 的 DS1/DT1 实现](https://github.com/dgcor/DGEngine/tree/master/src/Resources)，其说明注明参考 Riiablo、Collin Smith 与 Grant Ramsay。
- [OpenDiablo2 MPQ Viewer](https://github.com/OpenDiablo2/MpqViewer)，其 `listfile.go` 引用 Zezula 的 Diablo II LOD 文件名表。开发时用于发现实际资源路径；运行时使用明确路径和本项目 MPQ 的内置文件名表。


腰带阶段还使用 `ctrlpnl_popbelt.dc6`、`inv_belt.dc6`、`hlthmana.dc6`、`mediumbuttonblank.dc6`、`baskillicon.dc6` 以及 `potiondrink.wav`、`belt.wav`。腰带容量、自动入带及起始消耗品根据原 MPQ 的 `belts.txt`、`misc.txt`、`charstats.txt` 提取；技能图标帧取自 `skills.txt`。物品、自动入带标志和腰带容量已使用运行时导入；起始消耗品仍是按原表提取的配置；技能图标已改为运行时读取 Skills / SkillDesc，并非支持任意 MOD。

药剂恢复量与基本行为参考 [暴雪 Arreat Summit 药剂资料](https://classic.battle.net/diablo2exp/items/potions.shtml)。该说明包含资料片年代的规则，不是经典试玩 1.04 的逐帧规范；项目使用的持续时长、混用队列和耐力增强详见 [腰带与物品使用](BELT_AND_CONSUMABLES.md)，不可据此宣称完整复刻。


私人储物箱读取当前 MPQ 的原 DS1 私人箱实体及 b6 COF/DCC；资料片面板为 `data/global/ui/panel/tradestash.dc6`，格子取自 `inventory.txt` Big Bank Page 1；经典模式面板为 `bank.dc6`，格子取自 Bank Page 1。操作距离取自 `objects.txt` bank 记录。文件清单与版本边界见 [MPQ 资源](MPQ_RESOURCES.md)。

经典 HUD 的面板分块、球体偏移和 Sky 调色板用法参考 OpenDiablo2 的 [hud.go](https://github.com/OpenDiablo2/OpenDiablo2/blob/master/d2game/d2player/hud.go) 与 [globeWidget.go](https://github.com/OpenDiablo2/OpenDiablo2/blob/master/d2game/d2player/globeWidget.go)（2026-09-22 查阅，GPL-3.0）。本项目以 C++ 实现独立的布局、绘制与输入控制，素材来自用户完整 MPQ；路径和映射见 [CLASSIC_HUD.md](CLASSIC_HUD.md)。
