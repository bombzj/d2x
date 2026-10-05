# 资料、代码和素材来源

联网地图交互核对D2MOO `5596f5c` 的PlrMsg Rcv0x13／Rcv0x49、ObjMode门／传送台／门户／Waypoint处理器、D2Waypoints以及D2PacketDef Srv0x60／0x63／0x82。对象／Levels／Automap数据及DC6／TBL／PL2仍动态读当前MPQ；原传送点面板与本地共用。鲁高因空帧表参考libd2固定快照的`render/src/lib.zig::lut_town_skip`，并在本机1.13c D2Client文件0xD2DB8核实同一11项加0xFFFFFFFF终止表；运行时只用该规则，不加载原DLL。引用适用既有MIT归属，reference／原资源／开发证据不纳入提交。

纯 C++ 联网地图阶段依据既有 D2MOO `5596f5c` 的 DrlgDrlg、DrlgOutPlace、DrlgOutdoors／OutRoom、DrlgOutWild、DrlgDrlgVer／Grid／Room、DrlgPreset、DrlgTileSub、DrlgRoomTile、DrlgActivate、DrlgDrlgAnim、D2Collision 与 PathMisc，保留 MIT 归属。另查阅 [libd2](https://github.com/jaenster/libd2) 固定 `f92423bfd4df8a1ff162967dd9d894052e1da457` 的 1.14d 布局及 `tilegen.zig` 的文件内反序、40候选和即时阴影随机消耗。两个版本不能单独认证 1.13c。幕布局／道路已收拢到共同模块，五幕现有迷宫图接共同分房；没有导入参考地图、DT1、表或原 DLL。

本机 1.13c 静态证据：`D2Common.dll` MD5 `ee1238806ef6d6d9801d12a09d128fe1`（ImageBase `0x6fd50000`），`D2Client.dll` MD5 `f5860c629d309b8fc1f96174babcf633`（ImageBase `0x6fab0000`）。本批第二至第五幕补查同快照的DrlgOutDesr／OutJung／OutMesa／OutSiege及DrlgMaze，DS1单位按文件自身Act解析。1.13c RVA `0x90080` 确认瓦片重映射表包含type19；本地D2MOO ObjMode的OperateFunction29／61核对黏液门和哈洛加斯大门开门／结束动画。D2Client RVA `0x53aa5` 确认固定怪物变换8–37按减8索引30张RandTransforms，修正新地图中莎莉娜的变换越界。D2Common RVA `0x695d0` 的墙合并、`0x69722` 的真实 next 读取及 `0x68f00` 的双拐角插入确认共享链语义；`0x4ca20`／`0x1330` 确认活动碰撞改选先找拥有房间，再找活动近邻。RVA `0x7c101` 重置房间 low=initialSeed、high=666，区别于分配／延迟单位随机流。D2Client RVA `0xac440`／`0xac3d0` 的 0x07／0x08 处理调用 D2Common ordinal 10401／11099，进入／离开房间视野；ordinal 10401 wrapper RVA `0x3cca0` 调用视野引用与传播。RVA `0xba0c`–`0xbabe` 确认 DS1 替换组按声明数量直接读取四字段，没有 EOF 截断；Trees 第14组因此读取文件范围外的缺失字段，用户最新要求按 D2MOO 保留14组抽签，共同解码采用明确的零尺寸末组兼容，最终瓦片及完整碰撞已动态对照一致，详见[MPQ](MPQ.md#treesds1-原尾部兼容)。静态核对不修改原文件；原DLL另用于开发导出，未分发。D2Common RVA A6A0／A180核对Pops时间与分组，D2Client RVA62AA0／62580／61880核对自动地图已处理标志0x40000；动态Pops逐帧视觉未作原客户端认证。

开发对照使用既有 [d2mapapi_mod](https://github.com/soarqin/d2mapapi_mod) `f61d05244f323409aa48b326033639326d7c285c` 的32位1.13c导出入口；补充实际房间顺序、近邻、单位、Pops、DT1文件／记录和完整16位碰撞，记录进入／采样／离开事件。DT1身份由D2CMP RVA0x15D90的活动记录关联真实父文件，不把主次编号相同的不同记录当同一瓦片。第一幕此前122组、第二至第五幕本批364组原版新进程结果与共同C++核心一致，具体覆盖及屏蔽标志见[原版对照](../architecture/MULTIPLAYER.md#原版-dll-对照方法)。另曾查阅 [d2bs](https://github.com/noah-/d2bs) `f4b99bbe8de6916384991dfdd198ecf234cef1c0`；客户端运行不接入原DLL地图辅助器，reference、导出文件和原DLL均不提交或分发。

联网四步前端依据用户原客户端截图，主菜单布局／Logo 核对本地 OpenD2 `0578244` 的 `Menus/Main.cpp`、`Panels/Main.cpp` 与 OpenDiablo2 `7f92c57` 的 `main_menu.go`／`d2ui/button.go`；服务器选角外框与按钮位置核对 OpenD2 CharSelect。它们没有可直接采用的完整封闭 Realm 前端，当前 C++ 渲染与命令路由独立实现，所有背景／字体／按钮／建局面板来自当前 MPQ。native 1.13c 已保存预览与 flags 核对 D2MOO `5596f5c` 的 `GAME/Clients.cpp`、`D2Inventory.cpp`，同时读本机原版 D2Common.dll（外观槽类别表文件 0x9D888）及既有 AAA charinfo 的 `8D 80` 头；只保留协议兼容类别，item code／继承／武器姿态仍由 MPQ 动态重建。未复制参考源码、原 DLL、角色档或抽取资源到提交；预览组件染色暂不绘制，UI／协议互通尚未实机核对。

## 经典版素材

第一至三幕佣兵技能核对本地 D2MOO 固定 `5596f5c` 的 `AiThink.cpp::Fn061_Hireable/sub_6FCE4610/sub_6FCE4830`、`MonsterAI.cpp::MONSTERAI_UpdateMercStatsAndSkills`、`PlrMsg.cpp` 的佣兵装备规则、`SkillSor.cpp::StartInferno/DoInferno` 和 `D2Common/Units/Units.cpp` 的怪物 SC/FCR。天然技能及光环独立适配到公共单位／技能端口；实际类型、分段成长、技能权重、箭／法术、Jab 两击序列、Inferno 期限、动画／原图均取当前 MPQ。地狱火重建计时疑点和有限验证边界见[佣兵](../gameplay/characters/HIRELINGS.md#原表技能)。沿下述 MIT 归属，不提交 reference 或导出资源。

本批另核对 `Units.cpp::UNITS_GetMeleeRange/UNITS_IsInMeleeRange` 的怪物原距离／门和弹体阻挡掩码、`SkillAma.cpp::SrvDo007_Jab` 的序列事件及 `Missiles.cpp` 的子弹体创建owner要求；低于Hireling基准等级的技能成长沿MonsterAI原有符号右移。GU死亡血层仅从当前MPQ唯一组件文件定位，不复制参考资源。

亚马逊被动与魔法整页核对本地D2MOO固定 `5596f5c` 的 `SkillAma.cpp::SrvDo006/015/016`、`SkillNec.cpp::SetSummonBaseStats/SetSummonPassiveStats`、`SkillAss.cpp::sub_6FCF9580`、`SUnitDmg.cpp` 的暴击／格挡／三项防御被动、`Missiles.cpp` 的CanSlow／Pierce创建和 `MissMode.cpp` 的逐次碰撞／命中函数；宠物跟随参考 `AiThink.cpp::Fn067_NecroPet` 和旅行／上限入口。数值、MonEquip候选行、品质、原States角色伪装、宠物头像／组件／叠层均取当前MPQ。依上述MIT规则独立适配，不提交参考源码或资源；随机种子、路径、AI和原客户端差异见[亚马逊](../gameplay/skills/AMAZON.md#被动页验证与限制)。

亚马逊弓与弩整页继续核对本地 D2MOO 固定 `5596f5c` 的 `SkillAma.cpp::SrvDo008/SrvDo010/SrvSt08/SrvDo012`、`Skills.cpp::sub_6FD107F0/sub_6FD118C0`、`SUnit.cpp::sub_6FCBCFD0`、`MissMode.cpp` 的转换／冻结／引导／牺牲火／范围子弹体函数及 `D2Common/Units/Missile.cpp` 的 SrcDamage／毒源／吸取规则。参数、公式、伤害曲线、图形和声音仍读取当前 MPQ；沿上述 MIT 归属适配规则，不纳入参考源码或导出资源。原路径／客户端未完整移植的边界见[亚马逊](../gameplay/skills/AMAZON.md#本批验证与限制)。

地图阶段改用用户提供的 `assets/mpq2` 完整版／资料片 MPQ；文件摘要见 [MPQ 资源](MPQ.md)。地形及原表均从这些档案读取，没有引入第三方 JSON 地图。当前只运行资料片；以下试玩来源仅保留历史素材出处，不再作为游戏运行入口。

- [Blizzard 历史下载导航](https://classic.battle.net/diablo-universe.shtml) 曾提供 Diablo II Playable Demo。
- [ModDB 的 Diablo II Demo 页面](https://www.moddb.com/games/diablo-2/downloads/diablo-ii-demo) 标注官方 Windows 试玩版、文件大小 138,309,685 字节和 MD5 `9ae5033551a078937cd5d1f388cd8438`。
- 实际下载来源：[Internet Archive 的 Diablo II Demo 存档](https://archive.org/details/DiabloIiDemo)。完整下载在本次开发中与上述大小、MD5 一致；随后直接读取包内 MPQ，没有执行安装器。
- 供后续导入的脚本只请求源文件对应 MPQ 的字节范围。每个 MPQ 的 SHA-256 和偏移固定在 `scripts/fetch-demo.ps1`，用于识别下载损坏与服务器忽略 Range 的情况。

素材属于 Blizzard Entertainment。试玩包的公开下载与本项目代码的开源许可是两件事；仓库忽略所有 MPQ、下载缓存和导出的原版图片。请保留代码与素材各自的权利说明。

物品定义由 `src/content/classic_data.cpp` 与 `lod_data.cpp` 从当前 MPQ 的 `misc.txt`、`weapons.txt`、`armor.txt` 运行时导入；读取资料片命名类型和 `TreasureClassEx`，旧试玩适配已移除。原表属于游戏资源。储物箱尺寸依据 `inventory.txt` Big Bank Page 1；原掉落生成与物品效果的覆盖以 [物品完成度](../gameplay/items/SUPPORT.md) 为准。

2026-10-04 物品复核先读本地参考和当前 MPQ：D2MOO 同下述固定快照的 `ITEMS/ItemMode.cpp::sub_6FC425F0/sub_6FC428F0`、`D2GAME_ITEMSOCKET_PlaceItem_6FC497E0` 核对拾取／限带与镶嵌；`D2Common/Items/Items.cpp` 核对 socket filler／孔／符文之语及两项职业药剂倍率，`ItemMods.cpp` 核对 gem／rune 三组属性；`DataTbls/HoradricCube.cpp` 与 `D2Game/PLAYER/PlrTrade.cpp` 核对配方字符串、类型／数量匹配、op 和输出。`D2Inventory` 核对孔内子物品顺序，`PlrSave`／`Items` 核对根记录计数、mode=6、符文之语 TBL 身份及独立属性位流。限带、药剂与镶嵌规则独立适配为 C++，沿用 D2MOO MIT 归属，实际 quest／carry1／物品数值仍读当前 MPQ。OpenDiablo2 固定快照的 `cubemain_record.go`、`runeword_loader.go` 只交叉核对字段结构：class 语法注释为假设，符文之语属性循环把列名当 code，不能原样复制；Diablerie 的 UniqueItemLoader／Item／Player.Use 交叉核对字段与说明，方块 Use 分支为空且部分说明用固定示例等级，不能作为完整规则依据。[暴雪 jewel 说明](https://classic.battle.net/diablo2exp/items/jewels.shtml)、[方块说明](https://classic.battle.net/diablo2exp/items/cube.shtml)只补充用途；参数覆盖／未实现边界见 [数据](../gameplay/items/DATA.md)、[支持](../gameplay/items/SUPPORT.md) 与 [方块](../gameplay/items/CUBE_AND_GOLD.md)。本批未复制参考源码、资源或导出表到源码目录；Windows Release 构建及有限镶嵌实机冒烟通过，范围见上述支持文档，未打包。

包裹面板读取当前 MPQ 的 `data/global/ui/panel/invchar6.dc6`，按右侧面板分块拼接原版石框和格子，保留上方装备区域，物品详情以悬停提示呈现。该 DC6 与物品图标保留暴雪素材权利说明。

2026-10-05召唤整页沿本地D2MOO 5596f5c：SkillNec的基础／被动／抗性、SrvDo031／056／057／058、EventFunc23／27，SkillMonst的导弹149，AiThink的Fn067_NecroPet／近战分支，PlayerPets上限／旅行，MonsterSpawn／MonsterUnique的CorpseBoomDeath，Items金属判断／ItemsTbls的bitfield1导入，PlrSave2原kf段及SUnitDmg的40%吸收上限。数值／原资源仍动态读取当前MPQ；客户端映射推断、有限冒烟和限制见[死灵法师](../gameplay/skills/NECROMANCER.md#召唤技能整页)。参考源码及导出表不提交。

## 实际使用的开源项目

五幕地图复核沿下述固定本地快照：D2MOO 的 DrlgMaze、DrlgOutPlace／OutDesr／OutJung／OutSiege 核对特殊房、七墓、丛林与条带；DrlgTileSub 核对主题概率、Trials／Max、CheckAll、变体及掩码合并；DrlgPreset 核对 KillEdge、Pops／PopPad 和完整预设；DrlgDrlgAnim 与 D2CMP 标志核对 lava 帧号、Animate 和默认动画速度。OpenD2 DT1 结构和 OpenDiablo2 d2dt1／tile_cache 交叉核对材料位与 RarityFrameIndex。Trees.ds1末组EOF核对OpenD2 Engine/DS1.cpp的点名注释／边界检查，以及Diablerie Engine/IO/D2Formats/DS1.cs的ReadGroups／EndOfStreamException处理；D2MOO DrlgPreset的ReadInt32无EOF校验，D2Hell Archive分配文件长度加800字节，不能据此推造缺少的组字段。数据仍读取当前MPQ；当前源码与包保留声明14组，已知缺失末组字段作零尺寸兼容并保留位置随机消耗，不复现未初始化内存读取。原尾部事实见[MPQ](MPQ.md#treesds1-原尾部兼容)。适配与未消费字段集中列于[地图](../gameplay/world/MAPS.md#数据解码与重建配方)，不提交reference或导出资源。

传送点初始化／恢复核对本地 D2MOO `D2Common/src/D2Waypoints.cpp::WAYPOINTS_AllocWaypointData`／`WAYPOINTS_CopyAndValidateWaypointData` 的第零点必选位，以及 `D2Game/src/OBJECTS/ObjMode.cpp::OBJECTS_OperateFunction23_Waypoint` 的激活与打开阶段。菜单字体变换结构核对 OpenDiablo2 `d2common/d2fileformats/d2pl2/pl2.go` 的 `TextColorShifts`，边框拼接沿 `d2core/d2ui/frame.go`；原图、TBL 标题、字体、调色板和变换表均只从当前 MPQ 读取。参考代码遵循下述固定版本／许可，不提交参考仓库或导出图像；实现与验收限制见 [地图基线](../modules/MAP.md#客户端显示与操作)。

| 项目 | 用途 | 固定版本/来源 | 许可 |
| --- | --- | --- | --- |
| [raylib](https://github.com/raysan5/raylib) | 窗口、输入、OpenGL、音效、图片导出 | 5.5 / `c1ab645ca298a2801097931d1079b10ff7eb9df8` | zlib |
| [StormLib](https://github.com/ladislav-zezula/StormLib) | MPQ 挂载、读取、压缩打包 | v9.30 / `86f9b99ffe4d3417dad16d00541cf6f2e3d7bf79` | MIT，附带库保留各自许可 |
| [nlohmann/json](https://github.com/nlohmann/json) | 应用层调试命令 JSON 编解码 | 3.11.3，发布归档 SHA256 固定于 CMake | MIT，见 [许可](../licenses/nlohmann-json.txt) |
| [Asio](https://github.com/chriskohlhoff/asio) | 独立异步 DNS／TCP，仅实现层使用 | 1.30.2 / `12e0ce9e0500bf0f247dbd1ae894272656456079` | Boost-1.0，见 [许可](../licenses/Asio-Boost-1.0.txt)、[声明](../licenses/Asio-notices.txt) |
| [BNCSutil](https://github.com/BNETDocs/bncsutil) | CheckRevision、CD-key proof、旧式账号哈希；仅构建认证子集，不引入 NLS／GMP | `6334e0bde9cb7d2df73f7f9aa1072b54210a4d21` | LGPL-2.1-or-later，Eric Naeseth，见 [许可](../licenses/BNCSutil-LGPL-2.1.txt)；可替换的独立动态库 |
| [OpenD2](https://github.com/eezstreet/OpenD2) | D2GS Huffman 码字关系证据；保留推导的协议码字，独立实现有限前缀树解码，不复制原查表或解码函数 | `057824439ca145aa8411f9b024bc3fe6aa8fa450`，`Engine/Network.cpp` | GPL-3.0，见 [归属说明](../licenses/OpenD2-notices.txt) 与根目录 LICENSE |
| [DGEngine](https://github.com/dgcor/DGEngine) | DCC 解码器的直接改造来源 | `ae6dcabf4f824d617dc4b15ead1f0ef206c9a106`，`src/Resources/ImageContainers/DCCImageContainer.cpp` | Diablo 格式代码使用 GPL-3.0 |
| [Worldstone](https://github.com/Lectem/Worldstone) | DGEngine DCC 解码器的上游算法 | 由 DGEngine 说明和代码引用 | GPL-3.0 |
| [OpenDiablo2](https://github.com/OpenDiablo2/OpenDiablo2) | 第一幕对象预设数据、经典 HUD 布局参考 | `d2core/d2records/object_lookup_record_data.go` | GPL-3.0 |
| [D2MOO](https://github.com/ThePhrozenKeep/D2MOO) | Cave／Crypt 迷宫、矩形户外布局／边界及人口规则适配 | `5596f5cb6c5251a0a07c6637d26458b06099d516` | MIT，见 [许可](../licenses/D2MOO.txt) |

`src/resources/dcc.cpp` 已标注改动：C++ 索引帧接口、一次性解码所有方向、输入范围检查。`src/resources/presets.hpp` 从 OpenDiablo2 的第一幕数据筛选生成。项目整体使用根目录 `LICENSE` 的 GPL-3.0 文本；相关许可保存在 `docs/licenses`。

联网协议另核对本地 diablo2-protocol `173e55723b90a37aa8baabe913cd8e4dfb3fb9b4`（MIT，Louis Beaumont）及 PvPGN `9cd173f4e02ba3d9f8f15a67ca308b5eb78723e4`（GPL-2.0-or-later）的 SID／MCP 结构、字符 portrait 和服务响应，仅作证据，不复制其运行后端或源码。1.13c 固定包长度及入局常量结合用户原 `D2Net.dll`／`D2Client.dll` 静态读取核对，代码只保留协议常量，不纳入原 DLL、key、角色资料或 MPQ。认证文件只读路径由调用方提供。原表／玩法参数仍由当前 MPQ 决定；实际协议范围见 [联网模块](../modules/NETWORK.md)。未来分发需随包保留上述动态依赖及原始许可，当前未打包。

运行时 TXT 字段查找还核对了 D2MOO `D2Common/src/DataTbls/ItemsTbls.cpp` 的小写字段绑定与当前 MPQ 的 `StrBonus`／`DexBonus` 列名，采用通用 ASCII 忽略大小写查找；伤害比例参照 `D2Game/src/UNIT/SUnitDmg.cpp::SUNITDMG_ApplyDamageBonuses`。Talk 的多任务条目参照 `QUESTS_InitScrollTextChain` 逐任务追加消息的规则，文本和标题仍来自当前 MPQ。资料片背包资源路径与 OpenDiablo2 `resource_paths.go` 核对，实际加载当前 MPQ 的 `invchar6.dc6`／`invchar6Tab.dc6`。

鉴定光标、手持拾取、自动合并和随机机制的本地参考入口见 [背包](../gameplay/items/INVENTORY_UI.md)、[经典 HUD](../gameplay/ui/CLASSIC_HUD.md) 与 [随机机制](../gameplay/combat/RANDOMNESS.md)。使用当前 MPQ 的 Books／ItemTypes／DT1 权重，D2MOO 的 D2Seed、SUnit、ItemMode、Items、D2Inventory、DrlgRoomTile，以及 OpenDiablo2 AutoStack 字段说明；参考仓库和导出图不纳入源码提交。

宝箱生成、上锁、背包钥匙扣减和普通／特殊箱掉落适配本地 D2MOO 固定 `5596f5c` 的 `OBJECTS/Objects.cpp`、`ObjMode.cpp`、`ITEMS/ItemMode.cpp`、`Items.cpp` 与 `D2Common/DataTbls/MonsterTbls.cpp`；实际 Lockable、MonLvl1、难度等级、TC、ItemRatio、品质、钥匙堆叠、原图／文本／声音仍读取当前 MPQ。`world/chest.cpp`、`content/items/chest_loot.cpp` 的规则来源适用上述 MIT 许可；覆盖、随机流适配和陷阱等限制见 [交互物体](../gameplay/world/OBJECTS.md#上锁宝箱2026-09-27)。
物件靠近／操作范围另适配同快照 `D2Common/src/Units/Units.cpp` 的 `D2Common_10399`、`UNITS_IsObjectInInteractRange`、`UNITS_TestCollisionBetweenInteractingUnits/UNITS_TestCollision` 及 `D2Collision.cpp::COLLISION_RayTrace`，入口核对 `D2Game/src/PLAYER/PlrMsg.cpp` 对象交互分支。对象尺寸仍读取当前 MPQ，规则和原距离表沿用 MIT 归属，未纳入参考仓库或原资源。


祭坛效果适配本地 D2MOO 固定 `5596f5c` 的 `ObjMode.cpp` 各 SHRINES 处理器、`SUnitDmg.cpp` 状态及经验规则、`D2Common/Skills.cpp` 的 shrine_skill；怪物强化适配 `MonsterUnique.cpp` 的选取、初始化、光环和事件，以及 `MonsterSpawn.cpp` 的 setboss 直属随从关系。新增 C++ 类型及执行器使用当前 MPQ 的 Shrines、MonUMod、MonStats/2、MonType、MonLvl、DifficultyLevels、Skills、Missiles 与 States/Overlay；MIT 归属仍为上述 D2MOO 许可。限时祭坛／诅咒／光环图形播放速度仍沿 Diablerie `Overlay.Create` 的参考适配，未声称完整 D2Client 等价。覆盖与限制见 [交互物体](../gameplay/world/OBJECTS.md) 和 [怪物](../gameplay/world/MONSTERS.md)。

战斗关系、公共伤害与死灵法师召唤适配同一 D2MOO 固定快照的 SUnit／SUnitDmg、SkillNec、D2Skills、Monster、AiThink、PlayerPets 及 ObjEval；具体函数见 [阵营](../gameplay/combat/FACTIONS.md) 和 [召唤](../gameplay/skills/NECROMANCER.md)。所有技能参数、骷髅数值、组件和动画仍取当前 MPQ，不纳入参考表或资源。规则适配保留上述 D2MOO MIT 归属。

十项诅咒本批继续核对 D2MOO `SkillNec.cpp::SrvDo030/059/061`、`sub_6FD0B2B0/B3D0/BDA0`、`EventFunc04/05`，以及 `Skills.cpp::sub_6FD10EC0`、`ObjMode.cpp` 持续祭坛、`AiThink.cpp::SpecialState10_17/11`、`AiUtil.cpp::sub_6FCF2920`、`AiTactics.cpp` 逃跑和 `SUnitDmg.cpp` 百分比伤害。等级与免疫求值、事件、AI适配及未移植分支见[诅咒基线](../gameplay/skills/NECROMANCER.md#诅咒逐项实现)。[暴雪诅咒说明](https://classic.battle.net/diablo2exp/skills/necromancer-curses.shtml)补充失明的既有追击和恐惧覆盖边界，并用于核对降低抵抗等级表；D2MOO `D2Common_11033` 重建取整与该表冲突，保留当前MPQ参数的分阶段整数求值，差异明确记录。参考源码及MPQ导出均不纳入源码，MIT归属保持。

角色手持武器组件选择核对本地 D2MOO 固定 `5596f5c` 的 `D2Common/src/D2Inventory.cpp::INVENTORY_GetCompositItem`；世界方向到 DCC 方向帧核对 Diablerie 固定 `9e42ef2` 的 `Engine/Iso.cs::Direction`、`Engine/IO/D2Formats/DirectionMapping.cs` 和 `Engine/Entities/Missile.cs::Create`。两者沿用上文 MIT 归属；弓的组件、女巫 COF／DCC 与 Arrow 的 32 方向仍取用户当前 MPQ，不引入参考资源。源码入口与未验收范围见 [通用攻击](../gameplay/combat/ATTACKS.md#武器组件与朝向)。

场景坐标／绘制／阻挡核对的本地入口汇总见 [地图](../gameplay/world/MAPS.md#坐标绘制与阻挡)。D2MOO 固定 `5596f5c` 的 D2Dungeon、Units、Path、D2Collision、DrlgRoomTile 提供投影、静态／动态坐标、路径形状与掩码、房间初始化和门／出口标志；MonsterSpawn 核对出生掩码与 spawnCol，沿用上述 MIT 归属。OpenD2 固定 `0578244` 的 Engine/DT1.cpp 的 indexTable 交叉核对子格倒行；DGEngine 固定 `ae6dcab` 的 DT1/DC6ImageContainer 和 DGEngine.core 固定 `dd600ab` 的 Sprite2/CompositeSprite 用于帧偏移与纹理原点核对，沿用上述许可。OpenDiablo2 固定 `7f92c57` 的 d2maprenderer/renderer.go、d2mapentity/object.go 与对象字段记录用于低墙／地板、上墙／单位、屋顶及 OrderFlag/DrawUnder 层级证据，GPL-3.0；Diablerie 固定 `9e42ef2` 的 Iso、WorldRenderer、LevelBuilder、COFRenderer、Overlay 提供独立落点排序、阴影层和 PreDraw 交叉证据，MIT。C++ 入口独立实现；原图、Objects/MonStats/Overlay 参数及 DT1 原标记仍来自当前 MPQ，参考表、仓库、导出文件不纳入源码。

亚马逊标枪与长矛页另核对同一 D2MOO 固定 `5596f5c` 的 `SkillAma.cpp::SrvSt05/06/07/09/10`、`SrvDo007/011/013/014`、`D2Common/DataTbls/SequenceTbls.cpp`、`SUnit.cpp::sub_6FCBCFD0`、`MissMode.cpp::SrvDo02/SrvHit02/SrvHit12/SrvHit20/SrvDmg12` 和 `SkillSor.cpp::SKILLS_MissileInit_ChargedBolt`。原引擎内建序列按上述MIT来源适配，技能／弹体参数及A1／A2／毒云／闪电资源仍读当前MPQ；参考中的目标X/Y笔误及近战Calc[0]行为区别明确列于[亚马逊基线](../gameplay/skills/AMAZON.md#标枪与长矛技能整页)，尚未逐条核对零售1.13c二进制。参考仓库、数据表、导出和原素材不纳入源码。

## 格式研究参考

玩家死亡／尸体模块核对本地 D2MOO 的 `PlrModes` 尸体创建／回收和 `EVENTTYPE_ENDANIM` 的 `DT → DEAD` 转换、`PlayerPets` 全类型死亡／佣兵保留、`ItemMode` 装备重试及腰带收缩、`PlrMsg` 单位距离／复活、`Player` 普通单机损失和 `PlrSave2` 原 `JM` 尸体段；依照现有 MIT 参考版本与许可说明。时序／经验罚率从当前 MPQ `AnimData.d2`／`DifficultyLevels` 读取，图形使用原角色 `DD` 或 `DT` 末帧；参考仓库与资源不纳入源码。证据入口、普通单机多尸体限制和未验收边界见 [玩家死亡](../gameplay/characters/PLAYER_DEATH.md)。

全局照明核对同一 D2MOO 固定快照的 `D2Common/src/D2Environment.cpp`、`D2Game/src/GAME/Game.cpp::GAME_UpdateEnvironment`、`D2Gfx/src/CmnSubtile.cpp`：环境初始状态、25 Hz 推进、昼夜整数／色表计算、PL2 `intensity >> 3` 行选择及高质量地板子块的四邻点整数平均独立适配为 C++ 展示代码，沿用 MIT 归属。原表布局交叉核对 OpenD2 `Engine/Palette.hpp` 和 OpenDiablo2 `d2pl2`，物件直径／室内规则交叉核对其 ObjectDetailRecord／LevelDetailsRecord；原 PL2、Levels、Objects、Missiles、MonStats2、Overlay 全部读取当前 MPQ。四个本地参考未提供完整 D2Client 点光衰减／彩光合成程序，保留的空间适配与明确暂缓内容见 [照明](../gameplay/world/LIGHTING.md)。

投掷药瓶客户端特效另核对 [D2R Data Guide（Corrected）的 Missiles.txt](https://locbones.github.io/D2R_DataGuide/#missilestxt)：`CltHit03/HitOilPotion` 为主爆炸加 `CltHitSubMissile2/3` 随机二选一，`CltDo03/04` 分别说明尾迹和区域烟雾子效果。这里只借用函数／字段含义，所有 ID、图像和参数仍读取当前 1.13c MPQ，不复制第三方数据或代码。该说明没有给出旧客户端烟雾精确节拍和随机采样算法；不能据此宣称与原版逐帧一致。RandStart 与旧版 Phrozen Keep 指南的描述存在冲突，暂缓该字段；具体范围见 [通用攻击](../gameplay/combat/ATTACKS.md)。

连续关卡边界的实体可见性核对本地 D2MOO 固定 `5596f5c` 的 `D2Common/src/Drlg/DrlgActivate.cpp::DRLGACTIVATE_ChangeClientRoom`、`DRLGACTIVATE_RoomSetAndPropagateStatus` 及 `DRLGACTIVATE_RoomExPropagateSetStatus`：新旧观察房间通过 `ppRoomsNear` 传播状态，不把当前 LevelId 当作整片单位的绘制开关。本项目用现有房间矩形及地图世界偏移实现相邻可见性，沿用 MIT 归属；未移植完整激活和 AI 调度，边界见 [地图](../gameplay/world/MAPS.md)。

物件标签核对本地 OpenDiablo2 固定 `7f92c57` 的 `d2mapentity/object.go::Label`、`d2player/hud.go` 与 `d2ui/label.go::processColorTokens`：普通物件名走白色 Font16。地面物品标题核对 Diablerie 固定 `9e42ef2` 的 `Engine/Item.cs::GetTitle`，仅金币包含数量。地面动画核对该项目 `Entities/Loot.cs`、`SpriteAnimator.cs` 与 `IO/D2Formats/DC6.cs`，以及 DGEngine 固定 `ae6dcab` 的 `DC6ImageContainer.cpp` 底边偏移；素材／名称路径仍在运行时读取用户 MPQ，没有写入箭矢专用图形或帧数。OpenDiablo2 的 `factory.go::NewItem` 使用 Units 调色板，Diablerie Loot 使用 Act1；参考实现不能单独证明原版客户端逐像素结果，地面原图缓存修复及未验收范围见 [物品完成度](../gameplay/items/SUPPORT.md)。

D2S 读写依据本地 D2MOO 的 `PlrSave2.h/.cpp`、`Items.cpp`、`ItemMods.cpp`、`PlrIntro.cpp` 与第一幕任务代码，原位宽与属性值仍来自当前 MPQ。免费重置字节另核对 [D2CE ActsInfo](https://github.com/WalterCouto/D2CE/blob/c246509f385790462979004aeeaf7a47696ed605/source/d2ce/ActsInfo.h) 的字段布局及同目录 `.cpp`，只作格式证据，未移植实现。`@dschu012/d2s` 2.0.36 仅安装在忽略的 `reference/d2s-validation` 中作独立读写验证，不是运行依赖，不随游戏分发；其附带定义不替代运行时 MPQ。

本地 `reference/` 下的参考仓库均忽略提交，优先在本机核对，再按需要查其他来源：

任务模块与第二幕提示本轮另核对 D2MOO 固定 `5596f5c` 的 `Quests.cpp` 回调登记，`ACT2/A2Q0.cpp`／`A2Intro.cpp` 的欢迎与初见分工、`A2Q1` 至 `A2Q6` 的 active filter／交谈回调，以及 `Quests.h`／`D2Constants.h` 原保存槽／位定义。杰海因生成位置另据 `A2Q4` 的 `InitializeJerhynStartObject`／`InitializeJerhynPalaceObject`／`InitializeJerhynMonster`，实际标记与初始化函数、位置和中立人物资源从当前 MPQ Objects／DS1／MonStats 取得；双位置记录／隐藏切换为本项目适配，守卫、GUID与移动演出未完整移植。杰海因欢迎与法杖材料确认只用这些原位，原对白和 Sounds 身份来自当前 MPQ；本项目独立实现值事实与计划分发，不纳入参考源码或资源。仍沿用 MIT 归属；未实施原全局／队伍任务系统，准确边界见[任务系统](../gameplay/quests/SYSTEM.md)。

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

本轮世界生成代码依据 D2MOO 的 `DrlgMaze`、`DrlgOutPlace`、`DrlgOutWild`、`DrlgOutDesr`、`DrlgOutJung`、`DrlgOutMesa`、`DrlgOutSiege`、`DrlgOutdoors`、`DrlgRoomTile` 和 `DrlgDrlgVer` 适配；附近房间策略参考 `DrlgActivate`。这是实际代码适配来源；五幕共同核心的代表种子原版对照已完成，不认证全部32位种子或所有激活时序。当前入口见 [地图](../gameplay/world/MAPS.md) 与 [数据生命周期](../architecture/DATA_FLOW.md)。

鼠标按住行为参考同一固定 Diablerie 快照的 `Engine/PlayerController.cs::FixedSelection/ControlPlayerUnit`、`Engine/MouseSelection.cs::Update`：按住时不重新选中光标下对象，技能请求区分单位目标与地面坐标。左右键按单位持续请求及近战追击核对同一固定 D2MOO 的 `D2Game/src/PLAYER/PlrMsg.cpp`，尤其 `Rcv0x09_LeftSkillOnUnitHold`、`Rcv0x10_RightSkillOnUnitHold` 与 `sub_6FC836D0`。Diablerie 的右键没有完整单位锁定分支，D2MOO 也不包含完整客户端输入循环；用户原版操作说明补充了左右键按住后不随光标换目标的依据。本项目独立适配到现有命令和角色状态，不引入参考代码或资源到分发包；本轮没有运行验证。

墙角补片规则交叉核对 [D2MOO `DrlgRoomTile.cpp`](https://github.com/ThePhrozenKeep/D2MOO/blob/5596f5cb6c5251a0a07c6637d26458b06099d516/source/D2Common/src/Drlg/DrlgRoomTile.cpp) 与 [Diablerie `LevelBuilder.cs`](https://github.com/mofr/Diablerie/blob/9e42ef257228257825ebbbeb905d4b1d894b6e98/Assets/Scripts/Diablerie/Engine/World/LevelBuilder.cs)：DS1 方向 3 墙角同格绘制方向 4 补片；图像仍只取当前 MPQ。

通用碰撞与障碍目标靠近依据同一固定 D2MOO 的 `D2Collision.cpp`、`DrlgRoomTile.cpp`、`Path/Step.cpp`、`Path/AStar.cpp`、`Units/Units.cpp`、`MISSILES/MissMode.cpp` 和 `OBJECTS/ObjMode.cpp`。第一幕 DS1→Objects 身份索引适配 `DrlgPreset.cpp::DRLGPRESET_GetObjectIndexFromObjPreset`，这是引擎索引映射；对象尺寸／各模式碰撞／是否阻弹及弹体模式／尺寸仍动态读取当前 MPQ，不按石头名或示例记录覆写参数。沿用 MIT 归属和 [D2MOO 许可](../licenses/D2MOO.txt)。

- [D2MOO](https://github.com/ThePhrozenKeep/D2MOO/tree/5596f5cb6c5251a0a07c6637d26458b06099d516)，固定提交 `5596f5cb6c5251a0a07c6637d26458b06099d516`，MIT，Copyright 2020–2025 The Phrozen Keep community。共有 DT1、隐藏空白瓦片和变体权重解释依据 DrlgRoomTile.cpp；生成边界参考 DrlgDrlg.cpp、DrlgPreset.cpp、DrlgMaze.cpp。许可见 [D2MOO.txt](../licenses/D2MOO.txt)。没有移植全部五幕DRLG；正式游戏不调用参考仓库代码，原版DLL仅经忽略目录中既有导出工具作开发对照。

  通用技能状态结构还核对同一快照的 `Skills.cpp::sub_6FD11C90`（按目标原状态组互斥）、`SkillSor.cpp::SKILLS_SrvDo018_DefensiveBuff`／`SKILLS_CurseStateCallback_DefensiveBuff`（状态属性列表、事件注册及移除）和 `D2States.cpp`（死亡保留标志）。当前状态 ID、组、标志、叠层仍读取用户 MPQ 的 States.txt，玩法容器为本项目独立 C++ 实现；不纳入参考表或声称已复刻所有 Buff。

  女巫技能还核对了该快照的 `D2Common/src/D2Skills.cpp`（等级伤害分段、基础等级协同、装备冰伤加成与定点法力）、`D2Game/src/SKILLS/SkillSor.cpp`（传送的 Levels 许可、静电力场生命下限与暴风雪 SrvDo28 目标创建）、`D2Game/src/MISSILES/MissMode.cpp`（冰封球 SrvDo15／16、SrvHit29，暴风雪 SrvDo10／3、随机落点生成、LastCollide、寿命与命中顺序）、`D2Common/src/Units/Missile.cpp`（父子弹体伤害来源）及 `SUnitDmg.cpp`（冷抗、冰冷穿透与 MonsterColdDivisor）。冰封球原图、方向、创建／到期行为核对本地 Diablerie `9e42ef2` 的 `Engine/Entities/Missile.cs`、`Game/MissileFunctions.cs`；音效分组、Compound 与经典版淡出单位参照其 `Engine/Datasheets/SoundInfo.cs`、`Engine/AudioManager.cs`，MIT。PL2 格式与模式核对 OpenD2 `0578244` 的 `Engine/Palette.hpp/Renderer_GL.cpp`、OpenDiablo2 `7f92c57` 的 `d2pl2` 和 `d2enum/draw_effect.go`，GPL-3.0；Units 弹体调色板核对其 `d2mapentity/factory.go`。客户端字段／函数含义另核对 [D2R Data Guide](https://locbones.github.io/D2R_DataGuide/#missilestxt)，不移植其数据，也不把 D2R 音频 tick 单位直接视为经典版客户端规范。PL2 混色表、光源半径／色值、Sounds 分组和原 WAV 采样循环元数据均从当前用户 MPQ 读取；未纳入参考源码或导出资源。冰尖柱另核对 `MissMode.cpp` 的 SrvHit13／到期回调、`Skills.cpp` 的 0x8583 范围过滤及 `SkillAss.cpp` 的逐目标伤害副本、`SUnitDmg.cpp::SUNITDMG_ApplyFreezeState` 的冻结／首领减速与困难度整数除法；客户端 CltHit14 的关联／随机朝向以当前 MPQ 和上述 Data Guide 作证据，Diablerie 的碎冰数量／偏移不能作为经典版规则，明确暂缓项见 [技能](../gameplay/skills/SORCERESS.md#冰尖柱依据与当前实现)。碎冰甲另核对同一 D2MOO 的 SkillSor.cpp SrvDo18／EventFunc03、SUnitDmg.cpp 的近战攻击事件和 Skills.cpp 的所有者元素伤害掷骰；原表与 DCC 确定防御、协同、叠层、声音和高度。States 客户端 87 号闪点函数仍缺完整证据，范围见 [技能](../gameplay/skills/SORCERESS.md#碎冰甲依据与当前实现)。寒冰甲另核对同一 D2MOO SkillSor.cpp EventFunc01（所有者与 ReturnFire 条件、定向反击）、MissMode.cpp SrvDmgHitHandler（未命中／直接接触事件及处理先后）和 SetDamageFlags（纯元素不做普通盾牌格挡）；反击箭、三冰甲互斥、图形和声音均来自当前 MPQ。完整客户端状态事件 1 尚未核实，显示适配及边界见 [技能](../gameplay/skills/SORCERESS.md#寒冰甲依据与当前实现)。原定点路径、完整客户端动画／彩光及音频空间规则仍有缺口，范围见 [技能](../gameplay/skills/SORCERESS.md#冰封球依据与当前实现)，不声明全面复刻。暴风雪以当前 MPQ 的原表、DCC 与无 smpl 段的原 WAV 为数据依据；完整客户端 13／19 号函数未在本地 reference 找到，当前下落／碎裂显示适配与暂缓项见 [技能](../gameplay/skills/SORCERESS.md#暴风雪依据与当前实现)。

  怪物生成阶段还依据该固定提交的 MonsterRegion、MonsterChoose、MonsterSpawn、MonsterUnique、MonsterTbls 和 D2Common Monsters，适配区域选择／密度、随从、精英、固定首领和家族链规则。当前使用本项目的 DS1 空间与随机流适配，不声称逐种子或逐帧等价。代码入口和执行／暂缓字段见 [怪物生成](../gameplay/world/POPULATION.md)；原始 MonStats2、MonPreset、SuperUniques、MonPlace、MonUMod 表来自用户 MPQ。

- [OpenD2](https://github.com/eezstreet/OpenD2)，参考提交 `057824439ca145aa8411f9b024bc3fe6aa8fa450` 的 DT1、DS1、COF 和 Font TBL 结构说明。
- [DGEngine 的 DS1/DT1 实现](https://github.com/dgcor/DGEngine/tree/master/src/Resources)，其说明注明参考 Riiablo、Collin Smith 与 Grant Ramsay。
- [OpenDiablo2 MPQ Viewer](https://github.com/OpenDiablo2/MpqViewer)，其 `listfile.go` 引用 Zezula 的 Diablo II LOD 文件名表。开发时用于发现实际资源路径；运行时使用明确路径和本项目 MPQ 的内置文件名表。


腰带阶段曾使用 `ctrlpnl_popbelt.dc6`、`inv_belt.dc6`、`hlthmana.dc6`、`mediumbuttonblank.dc6`、`baskillicon.dc6` 以及 `potiondrink.wav`、`belt.wav`。当前腰带容量、自动入带及起始消耗品运行时读取原 MPQ 的 `belts.txt`、`misc.txt`、`charstats.txt`；技能图标读取当前职业的 Skills／SkillDesc，不再将野蛮人示例图当作所有职业图标。运行时导入并非支持任意 MOD，缺字段、规则或素材仍受对应消费者限制。

药剂恢复量与基本行为参考 [暴雪 Arreat Summit 药剂资料](https://classic.battle.net/diablo2exp/items/potions.shtml)。该说明包含资料片年代的规则，不是经典试玩 1.04 的逐帧规范；项目使用的持续时长、混用队列和耐力增强详见 [腰带与物品使用](../gameplay/items/BELT_AND_CONSUMABLES.md)，不可据此宣称完整复刻。


私人储物箱读取当前 MPQ 的原 DS1 私人箱实体及 b6 COF/DCC；资料片面板为 `data/global/ui/panel/tradestash.dc6`，格子取自 `inventory.txt` Big Bank Page 1；不再提供经典模式回退。操作范围使用 bank 的原 SizeX/SizeY 与 D2MOO 对象操作规则，OperateRange 不再作为中心圆半径。文件清单与版本边界见 [MPQ 资源](MPQ.md)。

经典 HUD 的分块、球体偏移和 Sky 调色板参考 OpenDiablo2 [hud.go](https://github.com/OpenDiablo2/OpenDiablo2/blob/master/d2game/d2player/hud.go)／`globeWidget.go`。任务日志三列两行、选中时保留全部任务、图像状态帧和只显示已到达幕页签参考 [quest_log.go](https://github.com/OpenDiablo2/OpenDiablo2/blob/master/d2game/d2player/quest_log.go)；外框五块拼接和包裹金币／关闭按钮位置另核对 `d2core/d2ui/frame.go`、`d2game/d2player/inventory.go`，固定提交 `7f92c57`，GPL-3.0。本项目用 C++ 实现布局、绘制与输入，运行时素材来自用户 MPQ；见 [CLASSIC_HUD.md](../gameplay/ui/CLASSIC_HUD.md) 与 [ACT1_QUESTS.md](../gameplay/quests/ACT1.md)。

顶部怪物生命条的布局／透明度／字体核对本地 Diablerie `Assets/Prefabs/EnemyBar.prefab` 与 `Game/UI/EnemyBar.cs`；普通白／勇士蓝／暗金金色分类补充核对[暴雪怪物说明](https://classic.battle.net/diablo2exp/monsters/basics.shtml)，RGB 使用 OpenDiablo2 `color_tokens.go`。地面悬停参照 Diablerie `Engine/Entities/Loot.cs`、`Engine/Materials.cs`、`Resources/Shaders/Sprite.shader`；背包非模态及场景鼠标过滤核对其 `InventoryPanel.cs`、`PlayerController.cs`、`MouseSelection.cs`。面板关闭后的按住隔离另核对同一 MIT 快照 `PlayerController.FlushInput/Update` 的等待松键及 `Ui.Hover` 过滤；独立适配到现有 C++ 鼠标消费状态，在面板关闭／视口改变前记录按下归属。该参考提供输入规则证据，没有完整原版 D2Client 的窗口事件实现。这些客户端参考不等同原版逐像素规范。地面单格占位／放置掩码／空位搜索另核对 D2MOO `D2Game/src/ITEMS/Items.cpp`、`ItemMode.cpp`、`D2Common/src/D2Collision.cpp`、`Path/Path.cpp`、`Units/Units.cpp`；运行时物体与单位尺寸仍来自用户 MPQ，参考仓库和抽取图像不纳入提交。

NPC 提示和初见依据 D2MOO 固定提交 `5596f5c` 的 `A1Intro.cpp`、`A1Q0.cpp`、第一幕各任务 `ActiveFilterCallback` 与 `PLAYER/PlrIntro.cpp`；区分按难度保存的介绍和本局 GUID 反应列表。原图来自当前 `Overlay.txt/npcalert` 和 `NPCSpeechBalloon.dcc`，高度取 `MonStats2.OverlayHeight`；字段含义交叉核对 [Diablo II Data File Guide](https://wolfieeiflow.github.io/diabloiidatafileguide/#overlaytxt)。用户本批八张原版图用于确认接任务、剩余数、清空待领奖、外框和角落地图。本地 `reference/` 中固定版本的开源项目没有原版 D2Client 完整字幕时序／重播间隔实现；这与用户 MPQ 的完整性无关，当前未声称逐帧一致。

感叹号横向定位曾引用本地 OpenDiablo2 `d2core/d2map/d2mapentity/factory.go::NewCastOverlay`。审查确认其将 `Overlay.XOffset/YOffset` 加入 `NewAnimatedEntity` 的实体位置，随后经 `AnimatedEntity.Render` 的等角坐标变换绘制，并非直接加到屏幕 X；不能仅凭该函数证明本项目的屏幕像素偏移符号和单位。当前保留 NPC 提示直接使用原表 `Xoffset=-5` 的待验收适配，未改其他法术叠层，不宣称已与原版定位等价。阿卡拉首次介绍与邪恶洞窟任务文本的独立来源核对本地 D2MOO `A1Intro.cpp::ACT1Intro_Callback00_NpcActivate` 和 `A1Q1.cpp::ACT1Q1_Callback00_NpcActivate`，两段仍读取当前 MPQ 文本；显示先后顺序依据用户原版观察，客户端音频及自动翻页时序未核实。

NPC 初次接触自动播开场白、后续出现交互菜单参考[暴雪《Diablo II》手册](https://ftp.blizzard.com/pub/misc/Diablo%20II%20Manual.pdf)。资料片 I／II 武器标签、W 切换及备用组属性不生效参考[暴雪《毁灭之王》手册](https://ftp.blizzard.com/pub/misc/Diablo%20II%20-%20Lord%20of%20Destruction.pdf)和[Arreat Summit 操作说明](https://classic.battle.net/diablo2exp/basics/controls.shtml)。NPC 菜单、任务、交易和背包的位置、图像状态及文案还对照用户原版截图；程序只读取当前 MPQ 素材。任务与侧栏布局已有参考源码，滚动节奏和字幕自然结束时序仍未核实。

物品 v88 阶段继续使用上述本地固定快照：D2MOO `SUnitNpc.cpp`／`SUnitProxy.cpp` 核对满堆叠售出、普通／魔法库存、赌博／修理商和单人城镇刷新；`Items.cpp` 核对交易费用、赌博价格和折扣（该反编译函数本身有舍入 TODO，未声称所有金币结果逐值等价）；`ItemsMagic.cpp`／`ItemMods.cpp` 核对稀有词缀、物品职业限制、属性层和劣质数值；`Skills.cpp`、`PlrModes.cpp::sub_6FC80B90`、`Items.cpp::ITEMS_IsMagSetRarUniCrfOrTmp` 核对扣数量后的品质分支（普通／优质／劣质移除，魔法及以上保留），`ItemMode.cpp` 核对自动补充；`SkillItem.cpp`／`SUnitDmg.cpp` 核对压碎、撕裂与吸取。OpenDiablo2 `d2core/d2ui/button.go` 提供修理按钮帧索引、`d2core/d2stats/diablo2stats/stat.go` 提供说明函数交叉依据；Diablerie 原文字键辅助核对，但运行时只使用用户 MPQ TBL。参考仓库和抽取资源不纳入提交。

第三至第五幕任务规则适配同一D2MOO `5596f5c` 的 `QUESTS/ACT3/A3Q0–6.cpp`、`ACT4/A4Q1–3.cpp`、`ACT5/A5Q1–6.cpp`及Quests、ObjMode、MonsterUnique、AiThink、SUnitNpc、D2Common Items。任务回调、永久奖励、打孔／署名、俘虏门户、古代人和王座五波分别绑定当前MPQ身份、属性、文本与素材；实现及未移植分支见[逐项基线](../gameplay/quests/ACT3_5.md)。原生任务／跨幕位依据Quests.h／Quests.cpp，空孔与署名位流及进度依据Items／PlrSave2／Clients；规则适配继续保留上述MIT归属，不提交reference、MPQ导出或原资源。

方块／Crafted 补查：同一 D2MOO 的 ItemsMagic::sub_6FC53CD0、Items 的需求和品质8位流、PlrTrade 修理／充能／升级／门户、A1Q4 的牛王与本局限制；OpenDiablo2 TXT Next 跳过 Expansion 保留行，与当前 MagicPrefix 原 BIN 的669行吻合。独立 @dschu012/d2s 核对前后缀一基编号与特殊身份零基。原 patchstring／Diablerie 原字符串确认 Token 右键返还说明；D2MOO 1.10 没有 Token 分支，返还沿本项目既有正式重置入口适配。当前全部146启用配方已实机合成，详细范围见方块文档；未复制参考源码或原数据。

毒素与白骨整页依据同一 D2MOO `5596f5c`：SkillAma SrvDo008／010的整数扇形与骨魂起手，SkillNec SrvDo032／055／060／062／063的毒匕首、尸爆、墙／牢与毒云，MissMode的BoneWallMaker／BoneSpirit和毒云方向，AiThink的BoneWall寿命，SUnitDmg／SUnitEvent的毒伤与吸收事件。当前原MPQ优先；官方Arreat技能页仅交叉核对。参考MonsterSpawn关键函数重建不完整、D2Client尾迹／装甲分片未找到完整证据，墙段搜索、骨魂向量轨迹与尾迹插值明确按适配处理。详见[毒素与白骨](../gameplay/skills/NECROMANCER.md#毒素与白骨技能)；未复制参考源码、表或图片。
