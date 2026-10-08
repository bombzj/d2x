# 资料、代码与素材来源

本页维护来源、固定版本、证据入口与许可；功能完成度和运行证据见[联网模块](../modules/NETWORK.md)，源码／包差异见[基线](../../BASELINE.md)。规则先查本地reference及当前MPQ，参考附带表不能代替用户资源。客户端正式运行不加载参考仓库或原游戏DLL，也不移植服务端结算。

## 原始素材

当前游戏使用用户提供的assets/mpq2资料片五个原始MPQ，名称、摘要与挂载顺序见[MPQ](MPQ.md)。原图、表、字体、DC6／DCC／COF、PL2、TBL与WAV属于Blizzard Entertainment；素材权利独立于代码GPL-3.0。MPQ、导出资源、截图、角色资料与参考仓库不纳入源码提交。

历史试玩来源保留作素材归属：Blizzard历史下载导航、[ModDB试玩页](https://www.moddb.com/games/diablo-2/downloads/diablo-ii-demo)及实际下载的[Internet Archive存档](https://archive.org/details/DiabloIiDemo)。包大小138,309,685字节、MD5 `9ae5033551a078937cd5d1f388cd8438`；未执行安装器。scripts/fetch-demo.ps1保留原范围下载／摘要识别，试玩MPQ不再作为产品运行入口。公开下载不授予本项目素材再分发权。

## 实际使用的开源项目

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
| [D2MOO](https://github.com/ThePhrozenKeep/D2MOO) | Cave／Crypt 迷宫、矩形户外布局／边界及人口规则适配；女巫／亚马逊与通用技能公共计算，第一幕AI／精英／首领、服务端执行与原包分工证据 | `5596f5cb6c5251a0a07c6637d26458b06099d516` | MIT，见 [许可](../licenses/D2MOO.txt) |

`src/resources/dcc.cpp` 已标注改动：C++ 索引帧接口、一次性解码所有方向、输入范围检查。`src/resources/presets.hpp` 从 OpenDiablo2 的第一幕数据筛选生成。项目整体使用根目录 `LICENSE` 的 GPL-3.0 文本；相关许可保存在 `docs/licenses`。

BNCSutil仅构建认证子集，作为可替换动态库；包内保留原许可／声明，不引入NLS／GMP。协议码字、DCC直接改造及预设数据的既有归属不因本地执行器删除而移除。固定构建入口为CMakeLists.txt、cmake/Dependencies.cmake及Networking.cmake。

## 本地参考目录

| 本地目录 | 固定提交 | 适用范围 |
| --- | --- | --- |
| `reference/d2moo/` | `5596f5c` | D2Common／D2Game 的规则、地图生成、掉落与 NPC；此快照没有 D2Client 角色属性／技能面板布局实现 |
| `reference/opend2/` | `0578244` | DT1、DS1、COF、字体结构及旧客户端菜单；不是 OpenDiablo2，游戏内角色面板尚无可用布局 |
| `reference/dgengine/` | `ae6dcab` | DCC、DS1、DT1 解码实现；项目目标为 Diablo I，引擎 UI 不可直接用于本作布局 |
| `reference/dgengine-core/` | `dd600ab` | DGEngine 的通用 2D 引擎组件；不提供 Diablo II 规则或面板布局 |
| `reference/opendiablo2/` | `7f92c57` | 怪物 `TransLvl + 2`、COF 逐层轮廓阴影、默认 `ohand.dc6` 鼠标及 Units 调色板；另有角色／任务面板参考 |
| `reference/diablerie/` | `9e42ef2` | COF Shadow／透明字段与半高斜投影交叉参考；MIT；参考附带表不替代当前 MPQ |

四类主要参考分工：D2MOO核对原服／D2Common规则，OpenD2核对旧格式／协议与菜单，OpenDiablo2核对原图布局／字段，Diablerie交叉核对客户端动画／输入／声音。DGEngine及其core补充DCC／DS1／DT1与图形偏移证据。D2MOO没有完整D2Client，DGEngine偏Diablo I，简化客户端不认证经典版精确行为。

## 当前代码的证据入口

| 领域 | 参考入口与采用边界 |
| --- | --- |
| 协议／认证 | PvPGN `9cd173f4e02ba3d9f8f15a67ca308b5eb78723e4`（GPL-2.0-or-later）SID／MCP头与处理器；diablo2-protocol `173e55723b90a37aa8baabe913cd8e4dfb3fb9b4`（MIT，Louis Beaumont）仅交叉参考，冲突长度以本机1.13c与D2PacketDef为准；不复制运行后端 |
| 大厅原图／流程 | 当前MPQ的bnet、creategamebckg／joingamebckg、gamebuttonblank与其他bigmenu按钮；正常帧0／按下帧1、Units调色板及fontridiculous字体参照OpenDiablo2 d2gui/style.go。PvPGN connection::conn_send_welcome／message的0x0F info／error核对MOTD；handle_d2cs::on_client_gamelistreq只在count非零时发终止包，未终止保持未知，不伪造房间。独立适配，未构建／运行验收 |
| 原服副本／技能／成长 | D2MOO PlrMsg／SCmd／D2PacketDef／D2Skills／SUnitMsg；0x1A／1B为经验增量、0x1C绝对值以原D2Game RVA ABDA0及实际回包校正重建疑点；角色学习／奖励由原服 |
| 本人移动→施法 | PlrModes::PLRMODE_StartXY_AttackCastThrowKickSpecialSequence保留当前位置，PathSetTargetPos只改目标；PlrMsg::sub_6FC81D20通常省略本人通知。清移动请求不撤销连续显示位置，明确校正独立 |
| 路径／交互 | Path／PathMisc的Straight／Toward／RayTrace、Step与Units原距离；18格是局部A*门槛，不是命令分段长度。PlrMsg::sub_6FC828D0距离≤6非busy交谈、7／8靠近、≥9不回复；显示只否决过早交谈，不授予服务权限 |
| 拾取等待／导航 | PlrMsg::Rcv0x16与sub_6FC828D0的UNIT_ITEM分支核对0x16参数、50距离限制、4以内拾取及远处靠近；ItemMode::PickupItem_6FC43340的Cursor／busy／安置失败分支没有通用拾取ACK。PlrMsg::Rcv0x01／03及PlrModes原动作入口接受新的导航目标。客户端仅结束等待／显示跟踪，不伪造原服物品或确认取消成功 |
| 地图／碰撞 | DrlgDrlg／OutPlace／OutWild／OutDesr／OutJung／OutMesa／OutSiege／Maze／Preset／TileSub／RoomTile／Activate、D2Collision；原房间／随机消耗保留。libd2 `f92423bfd4df8a1ff162967dd9d894052e1da457`（1.14d）tilegen／lut_town_skip仅交叉证据，不能单独认证1.13c |
| 世界／动画 | OpenDiablo2 renderer／object::setMode／composite与Diablerie WorldRenderer／COFRenderer／Iso／DirectionMapping；D2MOO SequenceTbls的女巫Lightning及亚马逊Jab／Impale序列、SUnit动作回滚。原COF／DCC／AnimData／MonSeq仍取当前MPQ，缺序列证据不猜 |
| NPC空组件／城镇地图 | MonStats2CompositLinker、D2Common_11069／11070与SCmd0xAC：启用但变体空、索引0按空组件省略，非零非法索引拒绝。DrlgPreset AutoMap及pfTownAutomap核对城镇全揭示，共用AutomapExploration规则 |
| 传送点／尸体最新修正 | ObjMode::OBJECTS_OperateFunction23_Waypoint接受mode1／2；ENDANIM::sub_6FC74AC0与ObjectsTbls固定点帧数核对ON衔接。MonsterMsg::sub_6FC65C70的DEAD动作9无目标分支发当前位置。这些2026-10-07修正未构建／入包，不能当运行认证 |
| 怪物／原弹体 | MonsterMsg／MonsterMode、SkillMonst SrvDo088／091／092／097、Missiles::SyncToClient与SCmd；真实身份／动作／ClientSend消费，不导入AI或伤害。固定Utrans依据下方原1.13cRVA |
| 人物／技能提示 | D2Skills／SkillDesc字段、Diablerie SkillPanelSlot；纯公式用原服已知值，缺装备／支配／基础等级保留未知。女巫SkillSor／MissMode、亚马逊SkillAma／SequenceTbls／MissMode／PlayerPets／SCmd、死灵SkillNec／SUnitEvent、圣骑士SkillPal只作规则线索，不复制执行器 |
| 声音 | MonsterTbls::LoadMonSoundsTxt；Diablerie MonSound／SoundSystem／SoundInfo／AudioManager核对25Hz延迟、原声组、Compound与武器音量；SoundSystem.OnLootFlipped及Item.dropSoundDelay核对起始item_flippy与指定帧落地音，Item的暗金／套装覆盖只作线索。实际字段与声音组读取当前MPQ Sounds／MonSounds／Weapons／Armor／Misc／UniqueItems／SetItems。OpenDiablo2 FsOff解释为推测，非零脚步相位仍暂缓 |
| 光照／混色 | D2Environment、GAME_UpdateEnvironment、D2Gfx CmnSubtile；OpenD2 Palette／Renderer_GL及OpenDiablo2 d2pl2核对PL2。高质量四邻点平均与标量行选择有证据，完整点光衰减／彩光／天气仍未核实，见[照明](../gameplay/world/LIGHTING.md) |
| HUD／手势 | OpenDiablo2 hud／globeWidget／mini_panel／skill_select_panel／quest_log／escape_menu；Diablerie PlayerController::FlushInput／Update、MouseSelection、EnemyBar／Loot。原UI消费按下直到松开，原图读取当前MPQ，参考不是完整D2Client窗口时序 |
| 任务／对白 | Quests／D2QuestRecord、各幕Q0／Q1–6回调、PlrIntro／NpcMessage／SUnitNpc；私有／公共字和欢迎／初见／本局GUID反应分开。1.13c D2Common RVA 0x3D120（ordinal10778）只筛menu=0，D2Client RVA 0x4BA39用它选自动对白；menu=2由D2Common RVA 0x3D160（ordinal10109）筛选、D2Client RVA 0x49EC6生成话题。不能沿用D2MOO 1.10f的0-or-2筛选解释。本地A1Intro的初见为0、PlrIntro保存已介绍旗标，A1Q1的重复任务评论为2。原TBL首行a1npc SPEED去除仅用于对白，日志布局参考不作资格／奖励依据 |
| 物品／城镇 | Items::SerializeItemCompact／Complete、D2Items原品质编号、SCmd9C／9D／3E／42／97／2A、PlrMsg16–29／60、ItemMode／SUnitMsg的DROPTOGROUND与普通地面同步、Units原DROPPING动画、PlrTrade／SUnitNpc；当前MPQ提供位宽／类型／布局。客户端只提交请求，不移植原定价／配方／库存事务 |
| 属性／镶嵌／掉落资料 | Items／ItemMods／ItemsMagic／HoradricCube／SUnitDmg核对Properties、carry1、孔内顺序、符文之语、TXT编号与原配方；ItemMods::ITEMMODS_PropertyFunc13核对当前MPQ dur%的百分比最大耐久属性投影，公共准备／D2S校验补该函数，未改原客户端已保存属性消费；SUnitNpc／SUnitProxy、PlrTrade、CubeParser、SCmd核对原赌博／货架／批量、方块与3E属性包；这些证据用于当前物品权威实现，准确执行及未完成范围见库存模块。MonsterRegion／Choose／Spawn／Unique与AiThink用于怪物准入与AI核对，范围见怪物模块 |
| 死亡／佣兵 | PlrModes／PlayerPets／ItemMode／PlrMsg／Player／PlrSave2，MonsterAI／AiThink::Fn061_Hireable／SUnitNpc；原死亡惩罚、复活／佣兵费用与技能仍由原服。死亡重入1血满蓝另有用户原版实测证据 |
| D2S | PlrSave2／Items／ItemMods／PlrIntro／D2QuestRecord；[D2CE ActsInfo](https://github.com/WalterCouto/D2CE/blob/c246509f385790462979004aeeaf7a47696ed605/source/d2ce/ActsInfo.h)只核对原重置位。`@dschu012/d2s` 2.0.36仅在忽略目录作独立格式对照，不分发／不替代MPQ；支持边界见[存档](../modules/SAVES.md) |

上述D2MOO规则适配保留MIT归属，Copyright 2020–2025 The Phrozen Keep community，见[D2MOO许可](../licenses/D2MOO.txt)。其他GPL／MIT来源依上表及原始声明保留；参考代码／导出表不纳入提交。

## 原版1.13c静态与开发对照证据

本机 1.13c 静态证据：`D2Common.dll` MD5 `ee1238806ef6d6d9801d12a09d128fe1`（ImageBase `0x6fd50000`），`D2Client.dll` MD5 `f5860c629d309b8fc1f96174babcf633`（ImageBase `0x6fab0000`）。第二至第五幕补查同快照的DrlgOutDesr／OutJung／OutMesa／OutSiege及DrlgMaze，DS1单位按文件自身Act解析。1.13c RVA `0x90080` 确认瓦片重映射表包含type19；本地D2MOO ObjMode的OperateFunction29／61核对黏液门和哈洛加斯大门开门／结束动画。D2Client RVA `0x53aa5` 确认固定怪物变换8–37按减8索引30张RandTransforms，修正新地图中莎莉娜的变换越界。D2Common RVA `0x695d0` 的墙合并、`0x69722` 的真实 next 读取及 `0x68f00` 的双拐角插入确认共享链语义；`0x4ca20`／`0x1330` 确认活动碰撞改选先找拥有房间，再找活动近邻。RVA `0x7c101` 重置房间 low=initialSeed、high=666，区别于分配／延迟单位随机流。D2Client RVA `0xac440`／`0xac3d0` 的 0x07／0x08 处理调用 D2Common ordinal 10401／11099，进入／离开房间视野；ordinal 10401 wrapper RVA `0x3cca0` 调用视野引用与传播。RVA `0xba0c`–`0xbabe` 确认 DS1 替换组按声明数量直接读取四字段，没有 EOF 截断；Trees 第14组因此读取文件范围外的缺失字段，既有明确要求按 D2MOO 保留14组抽签，共同解码采用明确的零尺寸末组兼容，最终瓦片及完整碰撞已动态对照一致，详见[MPQ](MPQ.md#treesds1-原尾部兼容)。静态核对不修改原文件；原DLL另用于开发导出，未分发。D2Common RVA A6A0／A180核对Pops时间与分组，D2Client RVA62AA0／62580／61880核对自动地图已处理标志0x40000；动态Pops逐帧视觉未作原客户端认证。

开发对照使用既有 [d2mapapi_mod](https://github.com/soarqin/d2mapapi_mod) `f61d05244f323409aa48b326033639326d7c285c` 的32位1.13c导出入口；补充实际房间顺序、近邻、单位、Pops、DT1文件／记录和完整16位碰撞，记录进入／采样／离开事件。DT1身份由D2CMP RVA0x15D90的活动记录关联真实父文件，不把主次编号相同的不同记录当同一瓦片。第一幕此前122组、第二至第五幕364组原版新进程结果与共同C++核心一致，具体覆盖及屏蔽标志见[原版对照](../architecture/MULTIPLAYER.md#原版-dll-对照方法)。另曾查阅 [d2bs](https://github.com/noah-/d2bs) `f4b99bbe8de6916384991dfdd198ecf234cef1c0`；客户端运行不接入原DLL地图辅助器，reference、导出文件和原DLL均不提交或分发。

原DLL仅只读核对或由忽略目录的既有导出工具作开发对照，不随运行包分发。样本、原点／DT1／16位碰撞、屏蔽标志及限制统一维护在[原版对照](../architecture/MULTIPLAYER.md#原版-dll-对照方法)。

持物占格高亮另核对同快照D2Client RVA 0x8C600逐格调用DrawBox mode=0、0x8C6B0初始化绿色RGB(0,128,0)／禁止红色(128,0,0)。D2gfx 1.13c ordinal10014才是DrawBox入口（RVA 0xBA30），不能套用D2MOO 1.10f的ordinal10055；D2DDraw RVA 0x6A25选择trans[2]，0x6850按背景索引×256＋颜色索引读取。D2Win ordinal10190经D2CMP ordinal10049（RVA 0x9D30）以RGB平方距离取首个最近项。当前MPQ Act PL2偏移0x23500提供实际混色结果；Diablerie InventoryGrid／InventorySlot只补充底色在物品下方、无描边的布局证据，其简化RGBA不作为原混色值。本批仅只读静态核对，未构建或运行。

第一幕精英另只读核对同一D2Client：RVA20340／20BF0／20BC0为电强化路径及GH／生命回调，20C40／A1020／A0DB0为冰强化死亡新星，4E095／4E14D／1FCA0为GH和生命高位发射资格。原版两端时钟、共享范围见[参考设计](../architecture/REFERENCE_DESIGN.md#第一幕怪物公共计算依据)，DLL与反汇编输出只留忽略目录。

## 补充资料的边界

[Blizzard原手册](https://ftp.blizzard.com/pub/misc/Diablo%20II%20Manual.pdf)、[LoD手册](https://ftp.blizzard.com/pub/misc/Diablo%20II%20-%20Lord%20of%20Destruction.pdf)与Arreat Summit用于交叉核对操作、物品／药剂／技能用途；[D2R Data Guide](https://locbones.github.io/D2R_DataGuide/#missilestxt)及原数据字段指南只补函数／字段含义，不能把D2R时钟、参数或简化客户端行为当1.13c规范。

RandStart、非零FsOff、字幕节拍、雨线／天气、精确Fade／光照及多个客户端程序仍缺证据；明确暂缓，不导入第三方数值、地图或素材。资源路径发现曾使用OpenDiablo2 MPQ Viewer／Zezula文件名表；运行时只读明确路径与当前MPQ内置清单。
