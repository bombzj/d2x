# 资料、代码和素材来源

## 经典版素材

地图阶段改用用户提供的 `assets/mpq2` 完整版／资料片 MPQ；文件摘要见 [MPQ 资源](MPQ_RESOURCES.md)。地形及原表均从这些档案读取，没有引入第三方 JSON 地图。当前只运行资料片；以下试玩来源仅保留历史素材出处，不再作为游戏运行入口。

- [Blizzard 历史下载导航](https://classic.battle.net/diablo-universe.shtml) 曾提供 Diablo II Playable Demo。
- [ModDB 的 Diablo II Demo 页面](https://www.moddb.com/games/diablo-2/downloads/diablo-ii-demo) 标注官方 Windows 试玩版、文件大小 138,309,685 字节和 MD5 `9ae5033551a078937cd5d1f388cd8438`。
- 实际下载来源：[Internet Archive 的 Diablo II Demo 存档](https://archive.org/details/DiabloIiDemo)。完整下载在本次开发中与上述大小、MD5 一致；随后直接读取包内 MPQ，没有执行安装器。
- 供后续导入的脚本只请求源文件对应 MPQ 的字节范围。每个 MPQ 的 SHA-256 和偏移固定在 `scripts/fetch-demo.ps1`，用于识别下载损坏与服务器忽略 Range 的情况。

素材属于 Blizzard Entertainment。试玩包的公开下载与本项目代码的开源许可是两件事；仓库忽略所有 MPQ、下载缓存和导出的原版图片。请保留代码与素材各自的权利说明。

物品定义由 `src/content/classic_data.cpp` 与 `lod_data.cpp` 从当前 MPQ 的 `misc.txt`、`weapons.txt`、`armor.txt` 运行时导入；读取资料片命名类型和 `TreasureClassEx`，旧试玩适配已移除。原表属于游戏资源。储物箱尺寸依据 `inventory.txt` Big Bank Page 1；原掉落生成与物品效果的覆盖以 [物品完成度](ITEM_COMPLETION.md) 为准。

包裹面板读取当前 MPQ 的 `data/global/ui/panel/invchar6.dc6`，按右侧面板分块拼接原版石框和格子，保留上方装备区域，物品详情以悬停提示呈现。该 DC6 与物品图标保留暴雪素材权利说明。

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

运行时 TXT 字段查找还核对了 D2MOO `D2Common/src/DataTbls/ItemsTbls.cpp` 的小写字段绑定与当前 MPQ 的 `StrBonus`／`DexBonus` 列名，采用通用 ASCII 忽略大小写查找；伤害比例参照 `D2Game/src/UNIT/SUnitDmg.cpp::SUNITDMG_ApplyDamageBonuses`。Talk 的多任务条目参照 `QUESTS_InitScrollTextChain` 逐任务追加消息的规则，文本和标题仍来自当前 MPQ。资料片背包资源路径与 OpenDiablo2 `resource_paths.go` 核对，实际加载当前 MPQ 的 `invchar6.dc6`／`invchar6Tab.dc6`。

鉴定光标、手持拾取、自动合并和随机机制的本地参考入口见 [背包](INVENTORY_UI.md)、[经典 HUD](CLASSIC_HUD.md) 与 [随机机制](RANDOMNESS.md)。使用当前 MPQ 的 Books／ItemTypes／DT1 权重，D2MOO 的 D2Seed、SUnit、ItemMode、Items、D2Inventory、DrlgRoomTile，以及 OpenDiablo2 AutoStack 字段说明；参考仓库和导出图不纳入源码提交。

宝箱生成、上锁、背包钥匙扣减和普通／特殊箱掉落适配本地 D2MOO 固定 `5596f5c` 的 `OBJECTS/Objects.cpp`、`ObjMode.cpp`、`ITEMS/ItemMode.cpp`、`Items.cpp` 与 `D2Common/DataTbls/MonsterTbls.cpp`；实际 Lockable、MonLvl1、难度等级、TC、ItemRatio、品质、钥匙堆叠、原图／文本／声音仍读取当前 MPQ。`world/chest.cpp`、`content/chest_loot.cpp` 的规则来源适用上述 MIT 许可；覆盖、随机流适配和陷阱等限制见 [交互物体](INTERACTIVE_OBJECTS.md#上锁宝箱2026-09-27)。

祭坛效果适配本地 D2MOO 固定 `5596f5c` 的 `ObjMode.cpp` 各 SHRINES 处理器、`SUnitDmg.cpp` 状态及经验规则、`D2Common/Skills.cpp` 的 shrine_skill；怪物强化适配 `MonsterUnique.cpp` 的选取、初始化、光环和事件，以及 `MonsterSpawn.cpp` 的 setboss 直属随从关系。新增 C++ 类型及执行器使用当前 MPQ 的 Shrines、MonUMod、MonStats/2、MonType、MonLvl、DifficultyLevels、Skills、Missiles 与 States/Overlay；MIT 归属仍为上述 D2MOO 许可。限时祭坛／诅咒／光环图形播放速度仍沿 Diablerie `Overlay.Create` 的参考适配，未声称完整 D2Client 等价。覆盖与限制见 [交互物体](INTERACTIVE_OBJECTS.md) 和 [怪物](MONSTERS.md)。

战斗关系、公共伤害与死灵法师召唤适配同一 D2MOO 固定快照的 SUnit／SUnitDmg、SkillNec、D2Skills、Monster、AiThink、PlayerPets 及 ObjEval；具体函数见 [阵营](COMBAT_FACTIONS.md) 和 [召唤](NECROMANCER_SKILLS.md)。所有技能参数、骷髅数值、组件和动画仍取当前 MPQ，不纳入参考表或资源。规则适配保留上述 D2MOO MIT 归属。

角色手持武器组件选择核对本地 D2MOO 固定 `5596f5c` 的 `D2Common/src/D2Inventory.cpp::INVENTORY_GetCompositItem`；世界方向到 DCC 方向帧核对 Diablerie 固定 `9e42ef2` 的 `Engine/Iso.cs::Direction`、`Engine/IO/D2Formats/DirectionMapping.cs` 和 `Engine/Entities/Missile.cs::Create`。两者沿用上文 MIT 归属；弓的组件、女巫 COF／DCC 与 Arrow 的 32 方向仍取用户当前 MPQ，不引入参考资源。源码入口与未验收范围见 [通用攻击](COMMON_ATTACKS.md#武器组件与朝向)。

场景坐标／绘制／阻挡核对的本地入口汇总见 [地图](ACT1_MAPS.md#坐标绘制与阻挡)。D2MOO 固定 `5596f5c` 的 D2Dungeon、Units、Path、D2Collision、DrlgRoomTile 提供投影、静态／动态坐标、路径形状与掩码、房间初始化和门／出口标志；MonsterSpawn 核对出生掩码与 spawnCol，沿用上述 MIT 归属。OpenD2 固定 `0578244` 的 Engine/DT1.cpp 的 indexTable 交叉核对子格倒行；DGEngine 固定 `ae6dcab` 的 DT1/DC6ImageContainer 和 DGEngine.core 固定 `dd600ab` 的 Sprite2/CompositeSprite 用于帧偏移与纹理原点核对，沿用上述许可。OpenDiablo2 固定 `7f92c57` 的 d2maprenderer/renderer.go、d2mapentity/object.go 与对象字段记录用于低墙／地板、上墙／单位、屋顶及 OrderFlag/DrawUnder 层级证据，GPL-3.0；Diablerie 固定 `9e42ef2` 的 Iso、WorldRenderer、LevelBuilder、COFRenderer、Overlay 提供独立落点排序、阴影层和 PreDraw 交叉证据，MIT。C++ 入口独立实现；原图、Objects/MonStats/Overlay 参数及 DT1 原标记仍来自当前 MPQ，参考表、仓库、导出文件不纳入源码。

## 格式研究参考

投掷药瓶客户端特效另核对 [D2R Data Guide（Corrected）的 Missiles.txt](https://locbones.github.io/D2R_DataGuide/#missilestxt)：`CltHit03/HitOilPotion` 为主爆炸加 `CltHitSubMissile2/3` 随机二选一，`CltDo03/04` 分别说明尾迹和区域烟雾子效果。这里只借用函数／字段含义，所有 ID、图像和参数仍读取当前 1.13c MPQ，不复制第三方数据或代码。该说明没有给出旧客户端烟雾精确节拍和随机采样算法；不能据此宣称与原版逐帧一致。RandStart 与旧版 Phrozen Keep 指南的描述存在冲突，暂缓该字段；具体范围见 [通用攻击](COMMON_ATTACKS.md)。

连续关卡边界的实体可见性核对本地 D2MOO 固定 `5596f5c` 的 `D2Common/src/Drlg/DrlgActivate.cpp::DRLGACTIVATE_ChangeClientRoom`、`DRLGACTIVATE_RoomSetAndPropagateStatus` 及 `DRLGACTIVATE_RoomExPropagateSetStatus`：新旧观察房间通过 `ppRoomsNear` 传播状态，不把当前 LevelId 当作整片单位的绘制开关。本项目用现有房间矩形及地图世界偏移实现相邻可见性，沿用 MIT 归属；未移植完整激活和 AI 调度，边界见 [地图](ACT1_MAPS.md)。

物件标签核对本地 OpenDiablo2 固定 `7f92c57` 的 `d2mapentity/object.go::Label`、`d2player/hud.go` 与 `d2ui/label.go::processColorTokens`：普通物件名走白色 Font16。地面物品标题核对 Diablerie 固定 `9e42ef2` 的 `Engine/Item.cs::GetTitle`，仅金币包含数量。地面动画核对该项目 `Entities/Loot.cs`、`SpriteAnimator.cs` 与 `IO/D2Formats/DC6.cs`，以及 DGEngine 固定 `ae6dcab` 的 `DC6ImageContainer.cpp` 底边偏移；素材／名称路径仍在运行时读取用户 MPQ，没有写入箭矢专用图形或帧数。OpenDiablo2 的 `factory.go::NewItem` 使用 Units 调色板，Diablerie Loot 使用 Act1；参考实现不能单独证明原版客户端逐像素结果，地面原图缓存修复及未验收范围见 [物品完成度](ITEM_COMPLETION.md)。

D2S 读写依据本地 D2MOO 的 `PlrSave2.h/.cpp`、`Items.cpp`、`ItemMods.cpp`、`PlrIntro.cpp` 与第一幕任务代码，原位宽与属性值仍来自当前 MPQ。免费重置字节另核对 [D2CE ActsInfo](https://github.com/WalterCouto/D2CE/blob/c246509f385790462979004aeeaf7a47696ed605/source/d2ce/ActsInfo.h) 的字段布局及同目录 `.cpp`，只作格式证据，未移植实现。`@dschu012/d2s` 2.0.36 仅安装在忽略的 `reference/d2s-validation` 中作独立读写验证，不是运行依赖，不随游戏分发；其附带定义不替代运行时 MPQ。

本地 `reference/` 下的参考仓库均忽略提交，优先在本机核对，再按需要查其他来源：

| 本地目录 | 固定提交 | 适用范围 |
| --- | --- | --- |
| `reference/d2moo/` | `5596f5c` | D2Common／D2Game 的规则、地图生成、掉落与 NPC；此快照没有 D2Client 角色属性／技能面板布局实现 |
| `reference/opend2/` | `0578244` | DT1、DS1、COF、字体结构及旧客户端菜单；不是 OpenDiablo2，游戏内角色面板尚无可用布局 |
| `reference/dgengine/` | `ae6dcab` | DCC、DS1、DT1 解码实现；项目目标为 Diablo I，引擎 UI 不可直接用于本作布局 |
| `reference/dgengine-core/` | `dd600ab` | DGEngine 的通用 2D 引擎组件；不提供 Diablo II 规则或面板布局 |
| `reference/opendiablo2/` | `7f92c57` | 怪物 `TransLvl + 2`、COF 逐层轮廓阴影、默认 `ohand.dc6` 鼠标及 Units 调色板；另有角色／任务面板参考 |
| `reference/diablerie/` | `9e42ef2` | COF Shadow／透明字段与半高斜投影交叉参考；MIT；参考附带表不替代当前 MPQ |

角色面板布局另与 [OpenDiablo2 的 hero_stats_panel.go](https://github.com/OpenDiablo2/OpenDiablo2/blob/master/d2game/d2player/hero_stats_panel.go) 核对；当前已保存该项目参考快照。坐标仍以用户截图及当前 MPQ 图框核对，运行时图像只从 MPQ 加载。引用、改编和权利按下方各项目说明保留。

2026-09-25 场景显示修正依据 OpenDiablo2 固定提交 `7f92c571bf04057a7fbdfb5d25a486f7d775e3c3` 的 [monster_stats_record.go](https://github.com/OpenDiablo2/OpenDiablo2/blob/7f92c571bf04057a7fbdfb5d25a486f7d775e3c3/d2core/d2records/monster_stats_record.go)（`PaletteId` 明确为 `TransLvl + 2`）、[animation.go](https://github.com/OpenDiablo2/OpenDiablo2/blob/7f92c571bf04057a7fbdfb5d25a486f7d775e3c3/d2core/d2asset/animation.go)／`composite.go`（按 COF 层标志绘制斜向半高阴影）、[gui_manager.go](https://github.com/OpenDiablo2/OpenDiablo2/blob/7f92c571bf04057a7fbdfb5d25a486f7d775e3c3/d2core/d2gui/gui_manager.go)／`resource_paths.go`（`ohand.dc6`、Units 调色板和鼠标热点偏移），GPL-3.0。另与 [Diablerie COFRenderer.cs](https://github.com/mofr/Diablerie/blob/9e42ef257228257825ebbbeb905d4b1d894b6e98/Assets/Scripts/Diablerie/Engine/Entities/COFRenderer.cs) 及 `COF.cs` 交叉核对阴影组件字段；该项目 MIT，Copyright 2019 Alexander Egorov。本项目独立实现 C++ 索引轮廓投影，不纳入参考仓库或其附带游戏表。阴影比例／颜色为参考实现适配，天气时序及完整客户端效果仍待核实。

本轮世界生成代码依据 D2MOO 的 `DrlgMaze`、`DrlgOutPlace`、`DrlgOutWild`、`DrlgOutdoors`、`DrlgRoomTile` 和 `DrlgDrlgVer` 适配；附近房间策略参考 `DrlgActivate`。这是实际代码适配来源，不是完整 DRLG 或逐种子等价实现。当前入口见 [地图](ACT1_MAPS.md) 与 [数据生命周期](baseline/DATA.md)。

鼠标按住行为参考同一固定 Diablerie 快照的 `Engine/PlayerController.cs::FixedSelection/ControlPlayerUnit`、`Engine/MouseSelection.cs::Update`：按住时不重新选中光标下对象，技能请求区分单位目标与地面坐标。左右键按单位持续请求及近战追击核对同一固定 D2MOO 的 `D2Game/src/PLAYER/PlrMsg.cpp`，尤其 `Rcv0x09_LeftSkillOnUnitHold`、`Rcv0x10_RightSkillOnUnitHold` 与 `sub_6FC836D0`。Diablerie 的右键没有完整单位锁定分支，D2MOO 也不包含完整客户端输入循环；用户原版操作说明补充了左右键按住后不随光标换目标的依据。本项目独立适配到现有命令和角色状态，不引入参考代码或资源到分发包；本轮没有运行验证。

墙角补片规则交叉核对 [D2MOO `DrlgRoomTile.cpp`](https://github.com/ThePhrozenKeep/D2MOO/blob/5596f5cb6c5251a0a07c6637d26458b06099d516/source/D2Common/src/Drlg/DrlgRoomTile.cpp) 与 [Diablerie `LevelBuilder.cs`](https://github.com/mofr/Diablerie/blob/9e42ef257228257825ebbbeb905d4b1d894b6e98/Assets/Scripts/Diablerie/Engine/World/LevelBuilder.cs)：DS1 方向 3 墙角同格绘制方向 4 补片；图像仍只取当前 MPQ。

通用碰撞与障碍目标靠近依据同一固定 D2MOO 的 `D2Collision.cpp`、`DrlgRoomTile.cpp`、`Path/Step.cpp`、`Path/AStar.cpp`、`Units/Units.cpp`、`MISSILES/MissMode.cpp` 和 `OBJECTS/ObjMode.cpp`。第一幕 DS1→Objects 身份索引适配 `DrlgPreset.cpp::DRLGPRESET_GetObjectIndexFromObjPreset`，这是引擎索引映射；对象尺寸／各模式碰撞／是否阻弹及弹体模式／尺寸仍动态读取当前 MPQ，不按石头名或示例记录覆写参数。沿用 MIT 归属和 [D2MOO 许可](licenses/D2MOO.txt)。

- [D2MOO](https://github.com/ThePhrozenKeep/D2MOO/tree/5596f5cb6c5251a0a07c6637d26458b06099d516)，固定提交 `5596f5cb6c5251a0a07c6637d26458b06099d516`，MIT，Copyright 2020–2025 The Phrozen Keep community。共有 DT1、隐藏空白瓦片和变体权重解释依据 DrlgRoomTile.cpp；生成边界参考 DrlgDrlg.cpp、DrlgPreset.cpp、DrlgMaze.cpp。许可见 [D2MOO.txt](licenses/D2MOO.txt)。没有移植完整 DRLG，也未执行参考仓库的游戏代码。

  通用技能状态结构还核对同一快照的 `Skills.cpp::sub_6FD11C90`（按目标原状态组互斥）、`SkillSor.cpp::SKILLS_SrvDo018_DefensiveBuff`／`SKILLS_CurseStateCallback_DefensiveBuff`（状态属性列表、事件注册及移除）和 `D2States.cpp`（死亡保留标志）。当前状态 ID、组、标志、叠层仍读取用户 MPQ 的 States.txt，玩法容器为本项目独立 C++ 实现；不纳入参考表或声称已复刻所有 Buff。

  女巫技能还核对了该快照的 `D2Common/src/D2Skills.cpp`（等级伤害分段、基础等级协同、装备冰伤加成与定点法力）、`D2Game/src/SKILLS/SkillSor.cpp`（传送的 Levels 许可与静电力场生命下限）、`D2Game/src/MISSILES/MissMode.cpp`（冰封球 SrvDo15／16、SrvHit29、寿命与命中顺序）、`D2Common/src/Units/Missile.cpp`（父子弹体伤害来源）及 `SUnitDmg.cpp`（冷抗、冰冷穿透与 MonsterColdDivisor）。冰封球原图、方向、创建／到期行为核对本地 Diablerie `9e42ef2` 的 `Engine/Entities/Missile.cs`、`Game/MissileFunctions.cs`；音效分组、Compound 与经典版淡出单位参照其 `Engine/Datasheets/SoundInfo.cs`、`Engine/AudioManager.cs`，MIT。PL2 格式与模式核对 OpenD2 `0578244` 的 `Engine/Palette.hpp/Renderer_GL.cpp`、OpenDiablo2 `7f92c57` 的 `d2pl2` 和 `d2enum/draw_effect.go`，GPL-3.0；Units 弹体调色板核对其 `d2mapentity/factory.go`。客户端字段／函数含义另核对 [D2R Data Guide](https://locbones.github.io/D2R_DataGuide/#missilestxt)，不移植其数据，也不把 D2R 音频 tick 单位直接视为经典版客户端规范。PL2 混色表、光源半径／色值、Sounds 分组和原 WAV 采样循环元数据均从当前用户 MPQ 读取；未纳入参考源码或导出资源。原定点路径、完整客户端动画／彩光及音频空间规则仍有缺口，范围见 [技能](SKILLS.md#冰封球依据与当前实现)，不声明全面复刻。

  怪物生成阶段还依据该固定提交的 MonsterRegion、MonsterChoose、MonsterSpawn、MonsterUnique、MonsterTbls 和 D2Common Monsters，适配区域选择／密度、随从、精英、固定首领和家族链规则。当前使用本项目的 DS1 空间与随机流适配，不声称逐种子或逐帧等价。代码入口和执行／暂缓字段见 [怪物生成](MONSTER_POPULATION.md)；原始 MonStats2、MonPreset、SuperUniques、MonPlace、MonUMod 表来自用户 MPQ。

- [OpenD2](https://github.com/eezstreet/OpenD2)，参考提交 `057824439ca145aa8411f9b024bc3fe6aa8fa450` 的 DT1、DS1、COF 和 Font TBL 结构说明。
- [DGEngine 的 DS1/DT1 实现](https://github.com/dgcor/DGEngine/tree/master/src/Resources)，其说明注明参考 Riiablo、Collin Smith 与 Grant Ramsay。
- [OpenDiablo2 MPQ Viewer](https://github.com/OpenDiablo2/MpqViewer)，其 `listfile.go` 引用 Zezula 的 Diablo II LOD 文件名表。开发时用于发现实际资源路径；运行时使用明确路径和本项目 MPQ 的内置文件名表。


腰带阶段还使用 `ctrlpnl_popbelt.dc6`、`inv_belt.dc6`、`hlthmana.dc6`、`mediumbuttonblank.dc6`、`baskillicon.dc6` 以及 `potiondrink.wav`、`belt.wav`。腰带容量、自动入带及起始消耗品根据原 MPQ 的 `belts.txt`、`misc.txt`、`charstats.txt` 提取；技能图标帧取自 `skills.txt`。物品、自动入带标志和腰带容量已使用运行时导入；起始消耗品仍是按原表提取的配置；技能图标已改为运行时读取 Skills / SkillDesc，并非支持任意 MOD。

药剂恢复量与基本行为参考 [暴雪 Arreat Summit 药剂资料](https://classic.battle.net/diablo2exp/items/potions.shtml)。该说明包含资料片年代的规则，不是经典试玩 1.04 的逐帧规范；项目使用的持续时长、混用队列和耐力增强详见 [腰带与物品使用](BELT_AND_CONSUMABLES.md)，不可据此宣称完整复刻。


私人储物箱读取当前 MPQ 的原 DS1 私人箱实体及 b6 COF/DCC；资料片面板为 `data/global/ui/panel/tradestash.dc6`，格子取自 `inventory.txt` Big Bank Page 1；不再提供经典模式回退。操作距离取自 `objects.txt` bank 记录。文件清单与版本边界见 [MPQ 资源](MPQ_RESOURCES.md)。

经典 HUD 的分块、球体偏移和 Sky 调色板参考 OpenDiablo2 [hud.go](https://github.com/OpenDiablo2/OpenDiablo2/blob/master/d2game/d2player/hud.go)／`globeWidget.go`。任务日志三列两行、选中时保留全部任务、图像状态帧和只显示已到达幕页签参考 [quest_log.go](https://github.com/OpenDiablo2/OpenDiablo2/blob/master/d2game/d2player/quest_log.go)；外框五块拼接和包裹金币／关闭按钮位置另核对 `d2core/d2ui/frame.go`、`d2game/d2player/inventory.go`，固定提交 `7f92c57`，GPL-3.0。本项目用 C++ 实现布局、绘制与输入，运行时素材来自用户 MPQ；见 [CLASSIC_HUD.md](CLASSIC_HUD.md) 与 [ACT1_QUESTS.md](ACT1_QUESTS.md)。

顶部怪物生命条的布局／透明度／字体核对本地 Diablerie `Assets/Prefabs/EnemyBar.prefab` 与 `Game/UI/EnemyBar.cs`；普通白／勇士蓝／暗金金色分类补充核对[暴雪怪物说明](https://classic.battle.net/diablo2exp/monsters/basics.shtml)，RGB 使用 OpenDiablo2 `color_tokens.go`。地面悬停参照 Diablerie `Engine/Entities/Loot.cs`、`Engine/Materials.cs`、`Resources/Shaders/Sprite.shader`；背包非模态及场景鼠标过滤核对其 `InventoryPanel.cs`、`PlayerController.cs`、`MouseSelection.cs`。这些客户端参考不等同原版逐像素规范。地面单格占位／放置掩码／空位搜索另核对 D2MOO `D2Game/src/ITEMS/Items.cpp`、`ItemMode.cpp`、`D2Common/src/D2Collision.cpp`、`Path/Path.cpp`、`Units/Units.cpp`；运行时物体与单位尺寸仍来自用户 MPQ，参考仓库和抽取图像不纳入提交。

NPC 提示和初见依据 D2MOO 固定提交 `5596f5c` 的 `A1Intro.cpp`、`A1Q0.cpp`、第一幕各任务 `ActiveFilterCallback` 与 `PLAYER/PlrIntro.cpp`；区分按难度保存的介绍和本局 GUID 反应列表。原图来自当前 `Overlay.txt/npcalert` 和 `NPCSpeechBalloon.dcc`，高度取 `MonStats2.OverlayHeight`；字段含义交叉核对 [Diablo II Data File Guide](https://wolfieeiflow.github.io/diabloiidatafileguide/#overlaytxt)。用户本批八张原版图用于确认接任务、剩余数、清空待领奖、外框和角落地图。本地 `reference/` 中固定版本的开源项目没有原版 D2Client 完整字幕时序／重播间隔实现；这与用户 MPQ 的完整性无关，当前未声称逐帧一致。

感叹号横向定位曾引用本地 OpenDiablo2 `d2core/d2map/d2mapentity/factory.go::NewCastOverlay`。审查确认其将 `Overlay.XOffset/YOffset` 加入 `NewAnimatedEntity` 的实体位置，随后经 `AnimatedEntity.Render` 的等角坐标变换绘制，并非直接加到屏幕 X；不能仅凭该函数证明本项目的屏幕像素偏移符号和单位。当前保留 NPC 提示直接使用原表 `Xoffset=-5` 的待验收适配，未改其他法术叠层，不宣称已与原版定位等价。阿卡拉首次介绍与邪恶洞窟任务文本的独立来源核对本地 D2MOO `A1Intro.cpp::ACT1Intro_Callback00_NpcActivate` 和 `A1Q1.cpp::ACT1Q1_Callback00_NpcActivate`，两段仍读取当前 MPQ 文本；显示先后顺序依据用户原版观察，客户端音频及自动翻页时序未核实。

NPC 初次接触自动播开场白、后续出现交互菜单参考[暴雪《Diablo II》手册](https://ftp.blizzard.com/pub/misc/Diablo%20II%20Manual.pdf)。资料片 I／II 武器标签、W 切换及备用组属性不生效参考[暴雪《毁灭之王》手册](https://ftp.blizzard.com/pub/misc/Diablo%20II%20-%20Lord%20of%20Destruction.pdf)和[Arreat Summit 操作说明](https://classic.battle.net/diablo2exp/basics/controls.shtml)。NPC 菜单、任务、交易和背包的位置、图像状态及文案还对照用户原版截图；程序只读取当前 MPQ 素材。任务与侧栏布局已有参考源码，滚动节奏和字幕自然结束时序仍未核实。

物品 v88 阶段继续使用上述本地固定快照：D2MOO `SUnitNpc.cpp`／`SUnitProxy.cpp` 核对满堆叠售出、普通／魔法库存、赌博／修理商和单人城镇刷新；`Items.cpp` 核对交易费用、赌博价格和折扣（该反编译函数本身有舍入 TODO，未声称所有金币结果逐值等价）；`ItemsMagic.cpp`／`ItemMods.cpp` 核对稀有词缀、物品职业限制、属性层和劣质数值；`Skills.cpp`、`PlrModes.cpp::sub_6FC80B90`、`Items.cpp::ITEMS_IsMagSetRarUniCrfOrTmp` 核对扣数量后的品质分支（普通／优质／劣质移除，魔法及以上保留），`ItemMode.cpp` 核对自动补充；`SkillItem.cpp`／`SUnitDmg.cpp` 核对压碎、撕裂与吸取。OpenDiablo2 `d2core/d2ui/button.go` 提供修理按钮帧索引、`d2core/d2stats/diablo2stats/stat.go` 提供说明函数交叉依据；Diablerie 原文字键辅助核对，但运行时只使用用户 MPQ TBL。参考仓库和抽取资源不纳入提交。
