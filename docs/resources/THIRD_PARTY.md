# 资料、代码与素材来源

本页维护来源、固定版本、原版依据入口与许可；当前实现及缺口见[基线](../../BASELINE.md)和对应专题。原版参考保留版本、函数／地址、条件及关键行为，不依赖临时目录长期存在；构建、自研运行和包身份仅在当次交付中说明。理解规则与调用链优先读本地D2MOO，资源和参数取当前MPQ；发现版本差异、可疑常量或行为矛盾后再定点核对原版DLL，不把反汇编逐条推读作为默认入口。参考附带表不能代替用户资源。客户端正式运行不加载参考仓库或原游戏DLL，也不移植服务端结算。

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
| [D2MOO](https://github.com/ThePhrozenKeep/D2MOO) | Cave／Crypt 迷宫、矩形户外布局／边界及人口规则适配；跨职业等级／协同／被动／时序与范围公共计算，第一幕AI／精英／首领、服务端执行与原包分工证据 | `5596f5cb6c5251a0a07c6637d26458b06099d516` | MIT，见 [许可](../licenses/D2MOO.txt) |

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
| `reference/pd2-bh/` | `92e9d319f20113b5fbd382ad0fd3df9480116d04` | [PD2官方BH](https://github.com/Project-Diablo-2/BH)：原客户端钩子、MCP局部扩展、物品提示／过滤与PD2核心导出调用；AGPL-3.0；不是完整PD2核心或汉服源码 |
| `reference/pd2-cn-bh/` | `68f7343342eed668190d464b7111849a6cc0ce89` | [历史BH-CN](https://github.com/weili0677/BH-CN)：2022-12-27的S6汉化分支，原MPQ辅助表读取、输入／物品及大厅改动；AGPL-3.0；不代表当前CN版本 |
| `reference/pd2-cn-analysis/` | 目标SHA-256见[PD2兼容分析](../pd2/BUILDS.md#已取得的-cn-目标样本) | CN样本的只读分析产物：输入／工具摘要、PE／LLVM、提取表、REA Evidence、静态主补丁及收发长度对照；地址与限制见[DLL取证](PD2_CN_DLL_ANALYSIS.md)，不是公开核心源码，不进入构建或源码提交 |
| `reference/pd2-launcher/` | `48e8cb3b9a218618403b532d88b7f0221cd132f2` | [PD2官方启动器](https://github.com/Project-Diablo-2/PD2Launcher)：文件更新清单、校验和Game.exe启动参数；MIT；不提供游戏协议实现 |
| `reference/pd2-installer/` | `5c0d6b293c975923afb8e5038bec28914618171b` | [PD2官方安装器](https://github.com/Project-Diablo-2/ProjectD2Installer)：ProjectD2目录与基础LoD安装依赖；未见仓库级许可证；仅作安装结构参考，不执行附带程序 |
| `reference/rea/` | `052956af3416fa01d93837e54fdec3ad2739f6d1` | [REA正式上游](https://github.com/morluto/rea)，源码版本6.7.0，MIT；Ghidra适配、Evidence及跨构建函数匹配的纯文本节选，清单见`SOURCE_REFERENCE.json`；此目录不是可运行检出，另行安装的运行环境见下一行 |
| `reference/rea-runtime/` | npm `rea-agents@6.7.0`，本地 package-lock；Ghidra 12.1.4 | 实际运行的stdio MCP／Windows Ghidra静态后端，使用Node24.19.0与现有JDK21.0.2；REA MIT、Ghidra Apache-2.0及各自依赖许可。官方Ghidra归档摘要与取证范围见[工具记录](PD2_CN_DLL_ANALYSIS.md#工具证据与复核入口)，不注册全局配置，不进入D2X构建或运行依赖 |

四类主要参考分工：D2MOO核对原服／D2Common规则，OpenD2核对旧格式／协议与菜单，OpenDiablo2核对原图布局／字段，Diablerie交叉核对客户端动画／输入／声音。DGEngine及其core补充DCC／DS1／DT1与图形偏移证据。D2MOO没有完整D2Client，DGEngine偏Diablo I，简化客户端不认证经典版精确行为。

PD2参考用于区分原版共用路径与有源码依据的局部扩展，不加入构建或运行依赖。已确认功能、代码消费者及未知项统一维护于[PD2领域基线](../pd2/README.md)，目标核心／资源摘要及地址解释见[版本与证据](../pd2/BUILDS.md)。初始化、发行与代码隔离见[兼容设计](../architecture/PD2_COMPATIBILITY.md)，具体协议和位流见[DLL取证](PD2_CN_DLL_ANALYSIS.md)。所列公开仓库未提供完整ProjectDiablo.dll核心及PD2_EXT.dll实现，不足以据此重建完整PD2游戏服务；目标协议与资源须按配套版本核实。

固定核心的[完整函数目录与功能概览](pd2-core/README.md)保存地址、真实分析器函数边界、调用关系、补丁与程序表登记、原生解析绑定及未归属范围。目录是静态分析事实，不是公开核心源码；自动名称和BH地址匹配均保留证据级别。目标二进制、整批反编译文本、Ghidra工程及分析脚本仍留在忽略的reference目录。

公开BH源码的模块分析、直接抽取与独立实现的许可边界、原客户端注入层替换及D2X接入方式见[BH独立接入方案](../architecture/BH_CODE_ISOLATION.md)。该方案未将BH源码加入产品，也未证明当前BH.dll与公开提交一致；不能因目录另含Apache许可文本就将整个BH改标为Apache。

REA属于开发期分析工具，不进入D2X构建或玩法依赖。上述快照的`docs/windows-ghidra-p0.md`及`src/ghidra/GhidraProviderCapabilities.ts`支持Windows x64上的原生x86／x64 PE与DLL只读分析；本次已用固定npm版本实际查询核心、Storm、当前BH及原D2Client，并将有效Evidence与失败诊断分开。运行依赖与main文档须分开核对；npm6.7.0使用`--mcp`启动stdio，并不提供main文档中所有CLI形式。静态DLL导入使用不可变副本，不调用目标入口。`docs/native-function-matching.md`及`src/domain/native/functionMatching.ts`基于已有Evidence给出名称、完整函数字节或结构候选关系，不证明行为等价或恢复原始源码。用于PD2时先从公开BH／原D2MOO定位，再定点追查核心DLL的初始化补丁登记、收发包与物品位流入口；保留目标摘要、模块／RVA、调用关系和未知项。运行时改写须另追补丁目标，不能把磁盘原函数或客户端可见逻辑当作当前服务器完整实现。

## 当前代码的证据入口

| 领域 | 参考入口与采用边界 |
| --- | --- |
| 协议／认证 | PvPGN `9cd173f4e02ba3d9f8f15a67ca308b5eb78723e4`（GPL-2.0-or-later）SID／MCP头与处理器；diablo2-protocol `173e55723b90a37aa8baabe913cd8e4dfb3fb9b4`（MIT，Louis Beaumont）仅交叉参考，冲突长度以本机1.13c与D2PacketDef为准；不复制运行后端 |
| 大厅原图／流程 | 当前MPQ的bnet、creategamebckg／joingamebckg、gamebuttonblank与其他bigmenu按钮；正常帧0／按下帧1、Units调色板及fontridiculous字体参照OpenDiablo2 d2gui/style.go。PvPGN connection::conn_send_welcome／message的0x0F info／error核对MOTD；handle_d2cs::on_client_gamelistreq只在count非零时发终止包，未终止保持未知，不伪造房间。完整大厅行为仍待原版对齐 |
| 原服副本／技能／成长 | D2MOO PlrMsg／SCmd／D2PacketDef／D2Skills／SUnitMsg；0x1A／1B为经验增量、0x1C绝对值以原D2Game RVA ABDA0及实际回包校正重建疑点；角色学习／奖励由原服 |
| 本人移动→施法 | PlrModes::PLRMODE_StartXY_AttackCastThrowKickSpecialSequence保留当前位置，PathSetTargetPos只改目标；PlrMsg::sub_6FC81D20通常省略本人通知。清移动请求不撤销连续显示位置，明确校正独立 |
| 路径／交互 | Path／PathMisc的Straight／Toward／RayTrace、Step与Units原距离；18格是局部A*门槛，不是命令分段长度。PlrMsg::sub_6FC828D0距离≤6非busy交谈、7／8靠近、≥9不回复；显示只否决过早交谈，不授予服务权限 |
| 拾取等待／导航 | PlrMsg::Rcv0x16与sub_6FC828D0的UNIT_ITEM分支核对0x16参数、50距离限制、4以内拾取及远处靠近；ItemMode::PickupItem_6FC43340的Cursor／busy／安置失败分支没有通用拾取ACK。PlrMsg::Rcv0x01／03及PlrModes原动作入口接受新的导航目标。客户端仅结束等待／显示跟踪，不伪造原服物品或确认取消成功 |
| 地图／碰撞 | DrlgDrlg／OutPlace／OutWild／OutDesr／OutJung／OutMesa／OutSiege／Maze／Preset／TileSub／RoomTile／Activate、D2Collision；原房间／随机消耗保留。libd2 `f92423bfd4df8a1ff162967dd9d894052e1da457`（1.14d）tilegen／lut_town_skip仅交叉证据，不能单独认证1.13c |
| 世界／动画 | OpenDiablo2 renderer／object::setMode／composite与Diablerie WorldRenderer／COFRenderer／Iso／DirectionMapping；D2MOO SequenceTbls的女巫Lightning及亚马逊Jab／Impale序列、SUnit动作回滚。原COF／DCC／AnimData／MonSeq仍取当前MPQ，缺序列证据不猜 |
| NPC空组件／城镇地图 | MonStats2CompositLinker、D2Common_11069／11070与SCmd0xAC：启用但变体空、索引0按空组件省略，非零非法索引拒绝。DrlgPreset AutoMap及pfTownAutomap核对城镇全揭示，共用AutomapExploration规则 |
| 客户端组队与动态地图标记 | D2MOO D2Roster／PartyScreen／PlayerList及SCmd的75／7F／8B／8D／90核对身份、邀请、离队、生命与公开位置；D2MOO不含D2Client界面实现。本地同快照原D2Client RVA 754A0／76790核对区域截断、38像素行距和分组，5BA90／5B5A0核对职业头像、TRANS50、56像素列距与生命阈值，5F1C0／D2DE8／61490／5F090核对12段单位标记、颜色及门户分支。图形、字体、区域与文案仍读当前MPQ；导出原图和静态记录不纳入源码或分发 |
| 传送点／尸体 | ObjMode::OBJECTS_OperateFunction23_Waypoint接受mode1／2；ENDANIM::sub_6FC74AC0与ObjectsTbls固定点帧数核对ON衔接。MonsterMsg::sub_6FC65C70的DEAD动作9无目标分支发当前位置；实际接入和剩余差异由对应专题维护 |
| 怪物休眠／特殊恢复 | D2MOO SUnitInactive的SUNITINACTIVE_CompressInactiveUnit、RestoreInactiveUnits及Save／RestoreSpecialMonsterParameters；MonsterUnique的D2GAME_SpawnMonster_6FC6F220、sub_6FC6FBA0、sub_6FC6FDC0用于区分普通／TYPE_OTHER／MINION与姓名／主从恢复。当前MPQ提供Restore、碰撞及实际身份；共用生命周期与未认证边界见[人口](../gameplay/monsters/POPULATION.md#公共出生压缩与特殊恢复)，参考源码不进入运行依赖 |
| 怪物／原弹体 | MonsterMsg／MonsterMode、SkillMonst SrvDo088／091／092／097、Missiles::SyncToClient与SCmd；真实身份／动作／ClientSend消费，不导入AI或伤害。固定Utrans依据下方原1.13cRVA |
| 人物／技能提示 | D2MOO D2Skills／SkillsTbls／Missile／MonsterTbls 核对描述公式与参数；本地原1.13c D2Client RVA316C0／32ABC核对75项描述程序，33340／33C80–33EA0核对选择器及树调用。固定DLL摘要、helper、输入限制与截图见[技能描述](../gameplay/skills/DESCRIPTIONS.md)；缺装备／支配／基础等级保留未知。女巫SkillSor／MissMode、亚马逊SkillAma／SequenceTbls／MissMode／PlayerPets／SCmd、死灵SkillNec／SUnitEvent、圣骑士SkillPal只作规则线索，不复制执行器 |
| 玩家／佣兵属性 | D2MOO UNITS_GetDefense／UNITS_GetBlockRate、SKILLS_GetConcentrationDamageBonus、ACT5Q3_UpdateResistances、MonsterAI_SendMercStats及SUnitMsg状态编码；本地原1.13c D2Client RVA F5210／F52A0动作伤害／命中调度表。共享装备纯计算、原基础列表与最终显示的区别见[人物属性](../gameplay/characters/ATTRIBUTES.md)，佣兵有限边界见[佣兵](../gameplay/characters/HIRELINGS.md)；参考二进制不作为运行依赖 |
| 声音 | MonsterTbls::LoadMonSoundsTxt；Diablerie MonSound／SoundSystem／SoundInfo／AudioManager核对25Hz延迟、原声组、Compound与武器音量；SoundSystem.OnLootFlipped及Item.dropSoundDelay核对起始item_flippy与指定帧落地音，Item的暗金／套装覆盖只作线索。实际字段与声音组读取当前MPQ Sounds／MonSounds／Weapons／Armor／Misc／UniqueItems／SetItems。OpenDiablo2 FsOff解释为推测，非零脚步相位仍暂缓 |
| 光照／混色 | D2Environment、GAME_UpdateEnvironment、D2Gfx CmnSubtile及OpenD2／OpenDiablo2 PL2；当前已接本机1.13c D2Client A8760／A9728的整数距离／定点衰减、A8A40的独立强度饱和累加与颜色合成、A8310／A8470／A9100／A9480的传播遮挡，以及A9A20普通房间底光、24920整数物件半径和189A0玩家光色。DT1 LightDirection、DED18／DEEF8墙脚六点、GDI／DDraw的32像素取光及来源先取光后混色已接；Client 79F20／7A340的逻辑区域前墙判定、500ms共享Pops过渡与D2gfx 9660的三档PL2透明墙已接，原表之外的方向明确拒绝；另已接原Stats基础光照属性、客户端装备／状态叠加及Overlay实例按25Hz计数变化单步推进；Objects.BlocksLight按Client 251AE／25572／25B0D控制绘制阴影，碰撞挡光只读0x22；地形阴影按GDI 60A0使用PL2 trans[0]。完整创建链交错、飞弹首帧、特殊环境、全部索引／图元分支及3D后端彩光／Gamma和天气仍未接齐；摘要、全部RVA与适用边界见[照明](../gameplay/world/LIGHTING.md#原113c静态定位与当前差异) |
| HUD／手势 | OpenDiablo2 hud／globeWidget／mini_panel／skill_select_panel／quest_log／escape_menu；Diablerie PlayerController::FlushInput／Update、MouseSelection、EnemyBar／Loot。原UI消费按下直到松开，原图读取当前MPQ，参考不是完整D2Client窗口时序 |
| 物件靠近／踢桶 | 同快照原1.13c D2Client RVA29E80／2AE30核对物件范围与0x804交互碰撞，2AA40发送02／04并登记待交互，2B090复验续行；2AC10的物件分支经2ACB3查隐藏技能1、2AD83尝试本地动作后，在2AD96构造13。客户端靠近请求不能当作已经操作；源码与限制见[物件](../gameplay/world/OBJECTS.md#碰撞与靠近)。DLL摘要见本页原版静态证据，原二进制与摘录不提交或分发 |
| 任务／对白 | Quests／D2QuestRecord、各幕Q0／Q1–6回调、PlrIntro／NpcMessage／SUnitNpc；私有／公共字和欢迎／初见／本局GUID反应分开。1.13c D2Common RVA 0x3D120（ordinal10778）只筛menu=0，D2Client RVA 0x4BA39用它选自动对白；menu=2由D2Common RVA 0x3D160（ordinal10109）筛选、D2Client RVA 0x49EC6生成话题。不能沿用D2MOO 1.10f的0-or-2筛选解释。本地A1Intro的初见为0、PlrIntro保存已介绍旗标，A1Q1的重复任务评论为2。原TBL首行a1npc SPEED去除仅用于对白，日志布局参考不作资格／奖励依据 |
| 物品／城镇 | Items::SerializeItemCompact／Complete、D2Items原品质编号、SCmd9C／9D／3E／42／97／2A、PlrMsg16–29／60、ItemMode／SUnitMsg的DROPTOGROUND与普通地面同步、Units原DROPPING动画、PlrTrade／SUnitNpc；当前MPQ提供位宽／类型／布局。客户端只提交请求，不移植原定价／配方／库存事务 |
| 属性／镶嵌／掉落资料 | Items／ItemMods／ItemsMagic／HoradricCube／SUnitDmg核对Properties、carry1、孔内顺序、符文之语、TXT编号与原配方；ItemMods::ITEMMODS_PropertyFunc13核对当前MPQ dur%的百分比最大耐久属性投影，公共准备／D2S校验补该函数，未改原客户端已保存属性消费；SUnitNpc／SUnitProxy、PlrTrade、CubeParser、SCmd核对原赌博／货架／批量、方块与3E属性包；这些证据用于当前物品权威实现，准确执行及未完成范围由[物品目录](../gameplay/items/README.md)链接负责专题。MonsterRegion／Choose／Spawn／Unique与AiThink用于怪物准入与AI核对，范围由[怪物目录](../gameplay/monsters/README.md)链接负责专题 |
| 死亡／佣兵 | PlrModes／PlayerPets／ItemMode／PlrMsg／Player／PlrSave2，MonsterAI／AiThink::Fn061_Hireable／SUnitNpc；原死亡惩罚、复活／佣兵费用与技能仍由原服。原1.13c D2Client RVA BEE60–BF0CE及D2Win ordinal10138／10184／10150核对死亡原图、Font30、红色PL2、居中／行距、goldlost及普通／专家／难度分支，详见[玩家死亡](../gameplay/characters/PLAYER_DEATH.md)；死亡重入1血满蓝另有用户原版实测证据 |
| D2S | PlrSave2／Items／ItemMods／PlrIntro／D2QuestRecord；[D2CE ActsInfo](https://github.com/WalterCouto/D2CE/blob/c246509f385790462979004aeeaf7a47696ed605/source/d2ce/ActsInfo.h)只核对原重置位。`@dschu012/d2s` 2.0.36仅在忽略目录作独立格式对照，不分发／不替代MPQ；支持边界见[存档](../modules/SAVES.md) |

上述D2MOO规则适配保留MIT归属，Copyright 2020–2025 The Phrozen Keep community，见[D2MOO许可](../licenses/D2MOO.txt)。其他GPL／MIT来源依上表及原始声明保留；参考代码／导出表不纳入提交。

## 原版1.13c静态与开发对照证据

动态碰撞另核对本地D2MOO `Path/Path.cpp::PATH_SetUnitDeadCollision`、`PLAYER/PlrModes.cpp::D2GAME_CORPSE_Handler_6FC7FBD0`及`PlrMsg.cpp::D2GAME_PACKETCALLBACK_Rcv0x41_Resurrect_6FC87480`：服务器死亡增加3×3 CORPSE，不把原活人路径掩码替换为CORPSE；独立尸体与复活玩家是不同GUID。复活撤旧活人轮廓时不得清掉保留尸体位。常驻格实现范围和未认证组合见[内核碰撞](../modules/SERVER_SYSTEMS.md#动态碰撞格的原版语义与迁移边界)。

03／04加载边界只读核对本机1.13c：`D2Game.dll` MD5 `978f6f74cd031bce6ecee602cd5988cc`、ImageBase `0x6fc20000`；`D2Common.dll` MD5 `ee1238806ef6d6d9801d12a09d128fe1`、ImageBase `0x6fd50000`；`D2Client.dll` MD5 `f5860c629d309b8fc1f96174babcf633`、ImageBase `0x6fab0000`。D2Game RVA `2DA8B`／`2DAE6`经导入stub `B306`调用D2Common ordinal11047／RVA `3BE90`，后者在`3BEB7–3BEC3`比较活动近邻数与DRLG原近邻数，在`3BEE0–3BEED`逐个检查Room1+0x34的位1；门控通过后`2DAA1`／`2DB0B`写一个字节并经`8A3E0`发送，`2DA0A`确定该字节为4。对应本地D2MOO的`D2Common_10073`及Game初次／CHANGING_ACT分支，不能套用1.10f同号ordinal。

D2Client的03处理RVA `ADB50`读取幕号、地图种子、城镇及尾部种子，调用`43F90`；`43FCF`经D2Common ordinal10951加载幕，`44039–44042`保存两个种子。05处理`ACB70`在`ACB7F`清完成标志、`ACB89`置加载标志；04处理`ADFE0`置完成标志并撤加载标志，`ADFFD–AE005`取得本地玩家房间，缺少玩家／房间进入`AE057`断言。此原版同步消费要求由当前客户端的有序地图事件中的04及本人房间检查承接，不能用网络收包先后越过本地异步准备。03尾部物件种子来源另参考D2MOO `OBJRGN_AllocObjectControl`返回的初始控制种子、`LEVEL_SynchronizeDayNightCycleWithClient`及`SCmd::D2GAME_PACKETS_SendPacket0x03_6FC3C810`；当前只保存既有初始化low值，不新增随机消费。原版实机传送点消息的房间／本人定位／04先后见[原版互通](../modules/NETWORK.md#tcpip原版互通参考与缺口)。DLL和反汇编不提交／分发；静态核对不能认证全部混合多人、复活、断线及本地资源加载表现。

走跑结束另核对下述同快照D2Client（MD5 `f5860c629d309b8fc1f96174babcf633`，ImageBase `0x6fab0000`）：RVA1C1AE经C3E2调用D2Common ordinal10342（RVA E230，D2MOO `PathMisc::D2Common_10226`），1C2F6–1C2FC消费路径结束标志；1C47B–1C493按城镇选择模式1／5，1C4A1–1C4AC调用ChangeAnimMode。Common ordinal11090／RVA33920在33938比较当前模式，变化时33952存新模式、3395B调用335F0重置动画起点；同模式分支不重置，与D2MOO `UNITS_ChangeAnimMode`／`UNITS_SetAnimStartFrame`一致。Common RVA2FB50–2FB5F另确认人物模式3返回101、其余走跑返回213；2FB40–2FB4F确认怪物按模式15读取原MonStats RunAnimSpeed，否则WalkAnimSpeed，与`UNITS_GetBaseAnimSpeed`一致。FRW150递减、25下限、CharStats跑步临时加成及动画／路径8.8整数缩放参考`UNITS_GetAnimStatBasePercentage`／`UNITS_CharacterStartRunningOrKnockback`／`UNITS_UpdateRunWalkAnimRateAndVelocity`。路径结束无额外200毫秒走跑延迟，实际显示入口及未运行边界见[地图](../modules/MAP.md#走跑动画与路径结束)。原DLL、反汇编与摘录留忽略目录，不修改、提交或分发。

玩家动态占位另核对同一1.13c D2Common（MD5 `ee1238806ef6d6d9801d12a09d128fe1`，ImageBase `0x6fd50000`）：RVA359D0入口在359DF–359EC将type0／1共同送入35A2A，35A31读取DynamicPath+0x48的碰撞pattern，35A83调用4BE90按pattern写位；type2另走矩形。玩家与怪物同有presence，与本地1.10f D2MOO `PATH_AddCollisionFootprintForUnit`给玩家调用SetMaskWithSize的转录不同。RVA361BD–361CE确认玩家FootprintMask=0x80、MoveTestMask=0x1c09及Straight；RVA363D0–36406核对自身与0x800目标临时撤脚印，364B8–364CA按序恢复。实际入场、走跑、死亡与旅行入口及仍未认证的历史位交错见[地图](../modules/MAP.md#单位占位与入场)。原DLL只读，不修改、提交或分发。

同快照的持久碰撞位补查：RVA4BB70按房间坐标定位16位格，4BBA0–4BBA2执行NOT／AND；Set入口4B930的4B960直接OR该格，均无重叠计数。ResetPattern入口4BCF0的4BD30–4BD3A对SmallUnit另清0x1000，4BD90–4BD9A对SmallPet另清0x2000；对应D2MOO `COLLISION_ResetMaskWithPattern`。1.13c RVA4D5C0的4D5D7调用4BCF0撤旧位，4D5F1调用4CE90检查新格，无碰撞在4D60D调用4BE90置新位，失败在4D62A恢复旧位；对应D2MOO `COLLISION_TrySetUnitCollisionMask`。这些证据确认状态随操作历史变化，不能把当前领域脚印的位或重建或简单贡献计数当成等价实现；迁移入口与保留边界见[动态碰撞格](../modules/SERVER_SYSTEMS.md#动态碰撞格的原版语义与迁移边界)。

第三至第五幕怪物逐项接入使用同一1.13c D2Game只读快照（MD5 `978f6f74cd031bce6ecee602cd5988cc`）。AI表RVA `10E788`第27项指向`808A0`：ThornHulk剩余连击、普通攻击冷却、ENDANIM加aip5及移动失败标志7；SrvDo表RVA `1074A8`第109项指向`B6790`，经`B4520`伤害／Drain后调用`28E70`叠加Frenzy，栈数上限为技能等级。准入、当前实现及缺口由[第三幕台账](../gameplay/monsters/ACT3.md#逐项接入)维护。

战斗资源与转换只读核对原1.13c `D2Game.dll`，MD5 `978f6f74cd031bce6ecee602cd5988cc`、ImageBase `0x6fc20000`。SrvDo指针表RVA1074A8的第79项为99830，99997安装98950恢复回调；99ACD保存整点上限，98A0E左移恢复生命，98A1A–98A24直接写未移位保存上限。另一回调914F0／915BD左移保存上限，不能与圣骑士路径混用。D95CC／D95DD／D95DF及D95E4–D95F2确认最大生命回调的除／乘、int64截断、signed32低位比较限幅，D9626–D9635确认上限右移8位、乘DamageRegen、signed右移4位后SetStat(HPREGEN)；原百分比算子为RVA14D0。D2Common同快照ordinal10887／RVA3A740为SetUnitStat入口。该依据为二进制静态核对，原文件不修改、提交或分发；落点及未完成流程见[圣骑士](../gameplay/skills/PALADIN.md#生命周期与边界)。

战斗递减参数只读核对原1.13c `D2Common.dll`，MD5 `ee1238806ef6d6d9801d12a09d128fe1`、ImageBase `0x6fd50000`。RVA `0x4dc30`（ordinal10115）先计算并截断`110*level/(level+6)`，再乘参数差、signed除100并只作上限裁剪；RVA `0x4e624`／`0x4e674`／`0x4e6c4`参数包装调用该入口。该乘法分组与1.10f D2MOO转录不同，公共原版规则已按此依据实现。原DLL仅只读，不修改、提交或分发。

启动内容准备另只读核对同一 `D2Common.dll`：RVA `0x5f09d` 检查全局加载模式，RVA `0x99e20` 的文件初值为 1；非零分支在 `0x5f0a6` 使用 `.bin` 后缀与 `DATA/GLOBAL/EXCEL` 路径，`0x5f125` 读取文件首个 u32 记录数，`0x5f127` 返回首部后 4 字节的记录区。与本地 D2MOO `DATATBLS_CompileTxt` 的普通 BIN 路径一致；不据此推断所有表的记录布局或原版启动耗时。当前 MPQ 枚举也存在 `charstats.bin` 与 `charstats.txt`。原 DLL 不改动、不分发。

本机 1.13c 静态证据：`D2Common.dll` MD5 `ee1238806ef6d6d9801d12a09d128fe1`（ImageBase `0x6fd50000`），`D2Client.dll` MD5 `f5860c629d309b8fc1f96174babcf633`（ImageBase `0x6fab0000`）。第二至第五幕补查同快照的DrlgOutDesr／OutJung／OutMesa／OutSiege及DrlgMaze，DS1单位按文件自身Act解析。1.13c RVA `0x90080` 确认瓦片重映射表包含type19；本地D2MOO ObjMode的OperateFunction29／61核对黏液门和哈洛加斯大门开门／结束动画。D2Client RVA `0x53aa5` 确认固定怪物变换8–37按减8索引30张RandTransforms，修正新地图中莎莉娜的变换越界。D2Common RVA `0x695d0` 的墙合并、`0x69722` 的真实 next 读取及 `0x68f00` 的双拐角插入确认共享链语义；`0x4ca20`／`0x1330` 确认活动碰撞改选先找拥有房间，再找活动近邻。RVA `0x7c101` 重置房间 low=initialSeed、high=666，区别于分配／延迟单位随机流。D2Client RVA `0xac440`／`0xac3d0` 的 0x07／0x08 处理调用 D2Common ordinal 10401／11099，进入／离开房间视野；ordinal 10401 wrapper RVA `0x3cca0` 调用视野引用与传播。RVA `0xba0c`–`0xbabe` 确认 DS1 替换组按声明数量直接读取四字段，没有 EOF 截断；Trees 第14组因此读取文件范围外的缺失字段，既有明确要求按 D2MOO 保留14组抽签，共同解码采用明确的零尺寸末组兼容，最终瓦片及完整碰撞已动态对照一致，详见[MPQ](MPQ.md#treesds1-原尾部兼容)。静态核对不修改原文件；原DLL另用于开发导出，未分发。D2Common RVA A6A0／A180核对Pops时间与分组，D2Client RVA62AA0／62580／61880核对自动地图已处理标志0x40000；动态Pops逐帧视觉未作原客户端认证。

开发对照使用既有 [d2mapapi_mod](https://github.com/soarqin/d2mapapi_mod) `f61d05244f323409aa48b326033639326d7c285c` 的32位1.13c导出入口；补充实际房间顺序、近邻、单位、Pops、DT1文件／记录和完整16位碰撞，记录进入／采样／离开事件。DT1身份由D2CMP RVA0x15D90的活动记录关联真实父文件，不把主次编号相同的不同记录当同一瓦片。第一幕此前122组、第二至第五幕364组原版新进程结果与共同C++核心一致，具体覆盖及屏蔽标志见[原版对照](../development/MAP_PARITY.md#原版-dll-对照方法)。另曾查阅 [d2bs](https://github.com/noah-/d2bs) `f4b99bbe8de6916384991dfdd198ecf234cef1c0`；客户端运行不接入原DLL地图辅助器，reference、导出文件和原DLL均不提交或分发。

房间恢复另核对同一1.13c D2Common：RVA `0x7c101`／`0x7c104`重置瓦片流low=initialSeed／high=666；`0x3c788`／`0x3c78b`初始化活动Room1的low／high（结构偏移0x6c／0x70）。RVA `0x8184b`／`0x8185b`及只读数据`0x8a758`／`0x8a7a8`确认逻辑转移表负索引读取，`0x81c1c`确认节点楼层掩码；`0x45550`按槽调用`0x453d0`配对Vis，不跨槽保留1.10f参考实现的bSkip。当前动态对照及适用范围见[地图验证](../gameplay/world/MAPS.md#当前源码逐事件复核)。

实机调用链另由同一1.13c Game.exe单机进程取证：D2Common ordinal10951／10053／10622／10715分别为LoadAct、StreamRoomAtCoords、ChangeClientRoom、DRLG_Update；D2Game调用ChangeClientRoom的返回点RVA为0x4b205。原生GetTileCache在D2Common RVA0x68b90，共享砖查找返回点0x69925；InitRoom返回点0x7c247来自Update预建，0x7c39a来自立即物化入口。采样包含原生DT1父文件／记录、房间、碰撞及选砖／随机，首差原因及实际释放／重建范围见[实机首差](../gameplay/world/MAPS.md#实机首差与合成调用边界)，不以合成AddRoomData或bClient=0冒充完整服务端链。

宿主逐房间修正另核实1.13c D2Game不导入10715，原版实机40,753次DRLG_Update均为客户端调用；服务端7 tick分支不能作为GAME_Update预取依据。ordinal10150／RVA3CA20确认RemoveRoomFromAct；RVA7C0E0／3CC5C／3CC92记录初始化与ChangeClientRoom作用域，3C7B8记录活动房间出生时的两条随机。这些依据不涵盖完整单位随机或多人交错。

持物占格高亮另核对同快照D2Client RVA 0x8C600逐格调用DrawBox mode=0、0x8C6B0初始化绿色RGB(0,128,0)／禁止红色(128,0,0)。D2gfx 1.13c ordinal10014才是DrawBox入口（RVA 0xBA30），不能套用D2MOO 1.10f的ordinal10055；D2DDraw RVA 0x6A25选择trans[2]，0x6850按背景索引×256＋颜色索引读取。D2Win ordinal10190经D2CMP ordinal10049（RVA 0x9D30）以RGB平方距离取首个最近项。当前MPQ Act PL2偏移0x23500提供实际混色结果；Diablerie InventoryGrid／InventorySlot只补充底色在物品下方、无描边的布局依据，其简化RGBA不作为原混色值。

自动地图显示另只读核对同一1.13c D2Client的5F9F0／608D0／60C40布局与居中、5FB10–5FB40方向键回调、60346–604EA原Fade及裁剪、62760城镇拼图；D2gfx ordinal10079与D2DDraw接口+0x98核对原DC6基线和三档PL2选择。当前MPQ原图／PL2的读取及完整参数、扩展分辨率与未运行边界见[自动地图](../gameplay/world/AUTOMAP.md#输入与选项)，反汇编／提取留忽略目录，不改动或分发原DLL和资源。

第一幕精英另只读核对同一D2Client：RVA20340／20BF0／20BC0为电强化路径及GH／生命回调，20C40／A1020／A0DB0为冰强化死亡新星，4E095／4E14D／1FCA0为GH和生命高位发射资格。原版两端时钟、共享范围见[参考设计](../architecture/REFERENCE_DESIGN.md#第一幕怪物公共计算依据)，DLL与反汇编输出只留忽略目录。

技能路径迁移另只读核对同一1.13c D2Game.dll：UMod表RVA10F448，第31项死亡回调265E0／模式回调21A90确认火魔爆炸；火强化266B0在267BF–26804于MPQ难度倍率之后，Normal扣1/4、Nightmare扣1/3、Hell除8，此步在本地1.10f D2MOO转录中缺失。冰强化262D0调用A2800，A28B0–A28D8循环构造64个方向；蛛网B4880–B49CE确认原方向表、正常X/Y坐标、技能173、飞弹143及速度3。参数仍读取当前MPQ Skills／Missiles／DifficultyLevels，原DLL与摘录不纳入提交或分发；专用回调入口及缺口见[技能公共规范](../gameplay/skills/COMMON.md#技能执行路径源码核对)。



自然生成规则另只读核对同一1.13c D2Game：VA6FC51079–6FC5113D确认PopulateFn1接受的类别，6FC50528–6FC50576确认Fn5为AND循环，6FC4EC22确认主题ID与候选数量的原资格判断，6FC527D0–6FC52A02确认祭坛治疗替换／次数及间距。RVAE1430确认133–136的零售预设排除与Forgotten Sands计数分支，不使用后续D2MOO版本额外插入的事件服首领。Init表VA6FD2E050第62项为6FC7C850，调用6FC7C2A0：三组坐标去重、25次参数5的真实act5pow出生、成功至多5人及0x03000000原标志。参数读取当前MPQ Objects／ObjGroup／Levels／MonStats／MonStats2／MonPlace／MonPreset／SuperUniques／Shrines；原DLL与表不修改、提交或分发。源码入口及缺口见[人口](../gameplay/monsters/POPULATION.md)与[物件](../gameplay/world/OBJECTS.md#自然生成源码边界)。

萨满另只读核对同一1.13c D2Game：AI表第65项RVA87BB0、SrvDo表第95项B6A60；死亡结束76040附近调用AFC50，以原GUID更改class并重新初始化。MPQ原表／动画提供当前资源，此处为静态依据；实际接入及缺口由[第三幕台账](../gameplay/monsters/ACT3.md#逐项接入)维护。

本机1.13c还定点核对囚犯Fn131（D2Game RVA A7A10）：A7A51／A7A59分别读取门Y／X，纠正D2MOO的X／X转录；RVA59290读取组计数114并判非零，不与蓝门关闭所需五人混为一条件。方块4F回调D1470经74200→734E0→72FC0→72220到特殊输出表FA5E8：第1项72210调用牛门674A0，第2／3项70180／70170均xor eax,eax;ret。原版据此保留钥匙门／最终门空输出，不移植带D2_VERSION_HAS_UBERS的D2MOO 1.14d门户／奖励扩展；PD2仍以其当前模块／MPQ的专用消费者为准。上述为静态依据，新增源码已构建，行为仍待实测，具体边界见任务、方块及原版缺口文档。

死亡金币补查同一1.13c：RVA37F80为DT／玩家金币掉落路径（3801A上限2,000,000,000、38068成功后扣钱包）；CORPSE_Handler在796AE–796C3先清gold再调用78C50，与D2MOO旧版失败保留钱包有差异。78C7D／38037共用11CA0，11CD7–11CE6读取当前区域等级并减1，11D33调用F780的Misc类型4选择，再预搜索／构造，不能拿固定ilvl1的普通金币堆构造代用。新增源码及待测边界见[玩家死亡](../gameplay/characters/PLAYER_DEATH.md)。

局内聊天本地化只读核对同一1.13c D2Client RVA71034–7104D（WideCharToMultiByte：CP_ACP、flags0x250）、70AF7–70B6D（语言字节及原生正文）、7B4F0–7B570（语言6／7／9／12的MultiByteToWideChar ACP/1及同语言／7↔12资格）；D2Lang #10062 RVA8320是单字节展开。结合D2MOO PlrMsg的原正文／语言透传，不能把D2Lang其他toUtf消费者误当聊天线路UTF-8证据。实现与平台边界见[联网](../modules/NETWORK.md#多人只读副本)及[聊天UI](../gameplay/ui/CLASSIC_HUD.md#聊天与消息日志)。

头顶气泡只读核对同一1.13c D2Client RVA7DC00／7CA40／7B240：单位坐标上移30、正文框再上移50及原行数留白；FontInGameChat、180像素换行、15像素行距、20像素横向留白、10行上限和DrawBox mode1。D2Win字体名表1C588第13项为FontInGameChat，10150（12FA0）及12940确认原字形基线；D2Common #10454（18280）确认125+8×长度的显示／过期时钟，不能沿用1.10的旧导出号。新增源码与剩余边界见[经典HUD](../gameplay/ui/CLASSIC_HUD.md)。

Holy Shield特殊盾图只读核对同一1.13c D2Client RVA6E1E0，6E289–6E2A6限定组件7、状态101及真实GetEquippedShield，返回DWORD20687368（hsh）；D2Common #10854（1FF40）分别检查活动右／左手、BROKEN／NOEQUIP及ANY_SHIELD类型。状态身份由当前MPQ按holyshield读取，不写死101。实现与待测范围见[圣骑士](../gameplay/skills/PALADIN.md)。

玩家跳跃另只读核对同一1.13c D2Client CltDo表DEA48：43为C6ED0、44为C6D00，共用C5F20／C65B0的RunVelocity与图形初始化；D5F54分段表为(27,-270,8462)／(36,-72,1224)，C65B0只以27分界并把重力低端夹到500，不能把第二行36擅自增加为新分支。C7C80给出初速及11位定点，C7FB0先加速度到位置再加加速度到速度，计数到零后还执行一次原更新。新增共用消费者与待测范围见[野蛮人](../gameplay/skills/BARBARIAN.md)。

## 补充资料的边界

PD2认证资源的固定模块、地址与SHA-256依据见[认证资源证据](../pd2/RESOURCE_LOADING.md#认证资源证据)；原资源、密钥和目标DLL不提交或分发，不执行native模块。

当前MPQ的MonStats重复名称以编译`monstats.bin`交叉核对：734行、每行0x1A8，第617／723行的名称链接ID均为617，616行NextInClass也指向617；字符串使用首行，数值类别仍保持各自索引。源码据此保留重复行的独立属性／经验／掉落，原二进制与提取不纳入源码提交。

MonStats基础攻击空值另以当前MPQ的TXT／BIN和同快照1.13c D2Common交叉核对：RVA `5C385–5C4A0`登记S1TH／S1MinD／S1MaxD及三难度字段，记录偏移206／242／248；`councilmember1`行345的三组WORD均为0。原版跨幕消息同时采自1.13c Host／D2X Join的真实传送点旅行；D2Client RVA `AC3D0`／`ADB50`／`ADFE0`／`AF640`及D2Common RVA `3CBE0`／`3C990`用于定点核对消费者，序列、故障和未覆盖边界见[原版互通](../modules/NETWORK.md#tcpip原版互通参考与缺口)。

[Blizzard原手册](https://ftp.blizzard.com/pub/misc/Diablo%20II%20Manual.pdf)、[LoD手册](https://ftp.blizzard.com/pub/misc/Diablo%20II%20-%20Lord%20of%20Destruction.pdf)与Arreat Summit用于交叉核对操作、物品／药剂／技能用途；[D2R Data Guide](https://locbones.github.io/D2R_DataGuide/#missilestxt)及原数据字段指南只补函数／字段含义，不能把D2R时钟、参数或简化客户端行为当1.13c规范。

RandStart、非零FsOff、字幕节拍、雨线／天气、精确Fade／光照及多个客户端程序仍缺证据；明确暂缓，不导入第三方数值、地图或素材。资源路径发现曾使用OpenDiablo2 MPQ Viewer／Zezula文件名表；运行时只读明确路径与当前MPQ内置清单。

零售客户端任务表现补核：同一1.13c D2Client的packet89回调表F2138按0–19为52440／52D00／空／53450／空／空／52C50／空／空／空／空／空／533F0／52420／53220／空／52B70／52AF0／空／52410。FX0由52440置计数0，53720–53756在30帧后只完成一次；53550给8区房间创建最多3个287号光束、最多25次探针及掩码5，536D3／536F0／440C0给出房间／活动链入口。CltDo表DD8A0的23=B2980使用当前帧<100→500；本机D2Common #10737 RVA6A270／#10970 RVA6A6D0是对应读写，不能套旧导出号。新增消费者及未齐范围见[第一幕任务](../gameplay/quests/ACT1.md)。

单位声音槽依据D2MOO SUnit::SUNIT_AttachSound覆盖nUpdateType／pUpdateUnit并置HASEVENTSOUND，SUnitMsg::D2GAME_UpdateUnit按pUpdateUnit资格发2C、MonsterMsg检查该标记，以及SUnit的帧末清标记。PlrMsg的3F接收在HASEVENTSOUND未清时拒绝。Den的A1Q1清场先置个人目标、向PRIMARYGOALDONE单位AttachSound(35,自己)，再TriggerFX(0)；晚入资格为CanClientFXBeTriggered的bNotIntro且fState>=4。客户端35另核对同一1.13c D2Client RVA232A9–232CE（职业speech-base+2，经45570）；当前MPQ对应七职业*_act1_complete_den，E24C0引用各职业声音／文本表；45570另已证实是任务音乐／延后声音槽，当前消费者与完整恢复边界见后文，不能把其首参数当TBL。消费者与边界见[第一幕任务](../gameplay/quests/ACT1.md)及[内核输出](../modules/SERVER_SYSTEMS.md)。

零售客户端剧情补充定位：Den环境524D0／A9A20读取5E初始化表第1项，RGB255／64／48、80×Fog10083量化余弦后截断；Fog本机16730按(index+128)&511读单精度表，30个输出强度已静态核对。熔炉FX14=53220，CltDo38=BA7E0→B4FB0、39=B2480、40=BA880→B5380、41=B24A0，CltHit表DDA10中42=B5300／43=B51C0；Common10754本机122B0按近房间／单位链查类376，Common10534本机6A290读取TotalFrames。控制373／碎片374–376／灵魂377的引擎身份和方向属原代码常量，其资源及Range仍读当前MPQ；原53220的45570第1参数已核实为任务Sounds4689；其后另调用88A70播放Sounds35，不把它当附着单位事件35。此批新源码已构建，行为仍待实测，不认证视觉随机初值或原全局交错。

客户端路径回调补充：本机Clt16=B8AA0，Common10203=CE90读取DynamicPath.flags&8；D2MOO Path/Step.cpp::sub_6FDAD5E0在整数位置改变时设置该位，回调先读前次结果再执行公共更新。子火墙的四选随机非零写创建flags0x4021，3/4抑制光；零写0x21。Clt59=B1CC0以Gfx.positionZ>>11和有符号TargetX/Y界限决定移除；Clt63=B1F20每次按(x+y-TargetY,y-x+TargetX)改第一路径点并重算，不能套用Clt60偶数帧门槛。新增源码已构建，行为仍待实测，Worldstone上游尚未齐；Nihlathak上游新接范围见下段。

Nihlathak原链补充：D2MOO A5Q4::Callback08_MonsterKilled→TriggerFX17，UnitIterate_AttachCompletionSound对PRIMARYGOALDONE玩家AttachSound82／自身。零售客户端52AF0查Monster526，创建606／603并设置FlagEx.NODRAW；Clt62=BA4A0→B3D60／B3C40，Clt63=B1F20，CltHit60=B5770。23297–232AF以职业speech-base+(event-33)计算声音：本机F7694的职业表各+24字段给出base2961／3273／3117／3195／3039／3429／3351，82对应3010／3322／3166／3244／3088／3478／3400，与当前MPQ各职业_act5_defeat_nihlathak匹配。Common10240本机6A2B0、11029本机6A2D0是飞弹Skill／Level；AFF10入口的末两参数是Skill／Level，不能误当目标坐标。各子创建显式继承，FX0／14／17初始Skill0、Level1。任务音乐槽见下段，其他语音消费者及逐视觉随机仍未认证，新增源码已构建，行为仍待实测。

任务音乐调度定位：45570接六个参数，存任务曲ID、当前客户端帧+启动／结束延迟、人物声音ID及其延迟／启用位；45660–4595E在到期启动曲目、玩家非死亡时播放延后声、到期后恢复环境曲，88930／888B0按原Sounds Fade Out停曲。35分支232B5–232CE取4688／0／300／职业speech-base+2／250／1，82分支2342C–23445取4697／0／550／职业speech-base+49／400／1；两者EBX1保存环境曲Block1。FX14的5324F–53263取4689／0／350／0／0／0、EBX0保存恢复点0，随后53268–53271以Sounds35播放熔炉声。当前MPQ三任务曲均Stream1／Music Vol0／Loop0／Fade Out125，实际曲资源分别act1/denofevilaction.wav、act4/forgeaction.wav、act5/nihlathakmusic.wav；首参数不是TBL编号。源码已接公共单槽，完整后端交错／曲目恢复未实测。

Clt37本机B2500先读Common10737的CurrentFrame：150时写震屏幅度25及0／4000／0毫秒，50时以NULL单位调用88A70播放原Sounds4638（当前monster_diablo_taunt_1），随后公共Do1；不是按elapsed150／50。上游已继续接FX12与372，完整原交错尚未运行。

混沌清场依据D2MOO A4Q2::KillAllMonstersInCS，先置bSanctumCleared／TriggerFX12，再按Act房间链及单位链对非243／333的Evil活怪物ModeChange(DEATH)，不是由开封印玩家造成致死伤害。SpawnDiablo以QuestUpdater累计十次；原1.13c D2Game BBDC0的BBDDC比较10、Init55与对象GUID检查，BA850使用-1／5／10出生并置flags0x3000000。原Timer创建33AF0将Timeout1加到QuestControl.tick，33B70的33BC7–33BD1用严格小于、33C2D–33C35用当前tick+timeout重设，GameUpdate2E0EE–2E101仅在游戏帧%20==0调用。客户端FX12=533F0创建372；52200／A9A20仅107／108使用255／64／48、160底光，5375B–5378D在结束计数30时清位。FX13=52420的90秒期限仅非资料片路径有宿主发生依据，当前LoD不添加自动退局。6A0C0读取1A8字节怪物记录，243为Diablo资源预备，不能误作覆盖图或Sounds243。新源码已构建，行为仍待实测，第一次全局相位及跨域提交交错仍待实测。

Clt30=B79F0：elapsed==cCltPar1置TargetY，非零后按elapsed%max(cCltPar2,1)向CltSubMissile1提供父坐标与两次半径偏移；1380与1330均为同一原SEED有限随机，前者从ESI取范围。Clt46=B7880用Common10983／10764（本机34AB0／34AA0）读取DynamicPath+10／+12的tTargetCoord，而非路径队列头；elapsed0／1缓存垂线、旧PATH.flags&8控制正反双发，创建flags0xB的循环延长按原SubLoop字段。Hit51=BA9A0复用A25C0七组方向表，cHitPar2非零安装73B60路径初始化；D2X沿已有missileChargedBurstDirections及charged-path初始化消费者接入。数据范围与未运行边界见职业／第一幕文档。

零售安雅演出补缺依据：D2MOO A5Q3::OBJECTS_OperateFunction67_FrozenAnya成功消耗药剂并设奖励位后调用QUESTS_TriggerFX(16)，再处理同队人物与定时器。本机上述D2Client身份的FX表F2138[16]=52B70，用Common10754找Objects460，创建528、Skill0／Level1、TargetX25。Clt表DD8A0[54]=B9C10，蒸汽阶段和成功分配后的三次随机见B9CA4–B9E27；震屏与7帧喷口见B9E2D–B9E8D，B39E0保留原四探针及X／Y非对称拒绝，mask4合格创建615、616。Clt68=B77A0使用Common10203路径换格、零子ID判断、flag9及CltParam1子循环；B3430–B344E按子表SubLoop加寿命。Gfx构造C7B00清零、C7BF0置bit8。当前MPQ的441子项为空，不能据回调存在自造子弹。新增源码已构建，行为仍待实测。

罗达门特客户端依据为同一零售D2Client DD8A0[32]=B2340、D3688六组三元组(300,25,1)/(250,10,3)/(100,8,4)/(75,6,4)/(30,4,3)/(0,100,0)，按CurrentFrame依次越过小于等于阈值；创建348并写TargetX／Y。DDA10[37]=B49F0非单位接触，Common10614／RVA36740把低种子设为TargetY、高字置666，再抽双Random(20)-10，双零重试；创建349后调用73B60。Common10637／69FB0及10019／69F70分别写MissileData+28／+2C（TargetX／Y）。不据已有347服务端来源推断新客户端源码已验证。

零售Clt57=B6F60先调用B2890随主人定位，再以主人为输入调用A1340；目标单位、DynamicPath.tTargetCoord及A0B30朝向后备各有明确原分支，朝向>>3的八偏移X=[0,-1,-2,-1,0,1,2,1]、Y=[2,1,0,-1,-2,-1,0,1]，任一坐标为0走后备。创建子项后Common PATH_SetType(14)并重新求路径；D2MOO Path.h确认为PATHTYPE_BLESSEDHAMMER。当前技能277引用553为srvmissilea，但D2MOO SrvSt28仅验证声明并安装状态、Do54不直接创建该飞弹，客户端函数为空，因此不从状态158自行补造根控制器；原实际创建来源仍须核实。新消费者只接零售，已构建，行为仍待实测。

零售Clt45=BB500的有效定义分支与Clt4=BBE00均将CltParam1／2／3及CltSubMissile1传入B9470，再执行公共Do1；45仅在无效父定义时直接移除，4则继续公共Do1。B9470先验证子定义，CurrentFrame0跳过机会，否则Random(CltParam1)==0才生成；每个子项双Random(2*radius)减radius，再按符号向外偏radius。当前394的36／1／6及395资源保留原表，Clt44／实际生产者仍待补，不因Distraction名称关联亚马逊诱饵。
