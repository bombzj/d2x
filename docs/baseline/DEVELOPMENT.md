# 开发与交接

当前源码存档 v54／规则 v82。普通怪物三难度生命、近战伤害／命中、防御、抗性、暴击、再生和 `El1–3` 按原表共用解析；`brute1–4`、`zombie1–5`、`skeleton1–5`、`fallen1–5` 使用原动作及 MPQ 所列声音。僵尸、骷髅、沉沦魔和 Brute 同族变体分别共用各自 AI，按 `MonStats.TransLvl` 选择原 `palshift.dat` 颜色映射；火焰、闪电、冰冷与毒素命中接通用元素攻击，冰冷与毒素保存角色持续状态。Fallen 家族在原死亡动作期间见附近尸体后逃离，使用原 AI 参数发同组命令并播放 MPQ S2 喊叫；Brute 在两次攻击掷骰后可绕目标行走。其他普通怪物仍保留真实身份与类型替身。分工和下一种怪物的条件见 [怪物实施计划](../MONSTERS.md)；精英、固定首领、Boss 和通用逐帧 AI 调度仍待逐项核对。旧存档不迁移。

Windows Release 构建通过。完整六 MPQ 目录 `dist/d2x-runtime-20260924-v54-brute4/` 在邪恶洞窟启动；`brute4` 原形 `substitute=false`、`sourceAi=Brute`，MPQ 普通难度生命／A1／A2／防御／抗性已返回，v54 存读档正常，击杀获得 6 经验。原调色截图保存于 `artifacts/monster-smoke/brute4-v54-in-view.png`。共享绕行行为已在 `brute2` 现场量测，此变体不重复数值测试。运行目录与截图不提交，不生成 ZIP 或测试脚本／用例。音频播放效果仍待用户实机听验。

## 当前交接状态

- 本轮调试快捷键、MPQ 城镇出生标记、NPC／传送点可见帧热区、城镇耐力规则及第一幕野外神殿分组已接入源码。用户随后明确要求打包和冒烟，Windows Release 构建、五个原始 MPQ 下 1–39 关逐张短帧启动及截图已完成；完整交互仍待验收。

- NPC 对话阶段见 [实施顺序](../NPC_COMPLETION.md)；购买阶段见 [NPC 交易](../NPC_TRADE.md)。当前格式 v15／规则 v40。原 MPQ 的 Act I 对话与 DS1 路径已动态解析；Akara 治疗、凯恩付费鉴定、长对话滚动和通用闲聊已接。Windows Release 构建、新档与读档短帧、Akara 原文和 Gossip、未鉴定稀有物品拾取后凯恩扣 100 金币鉴定、四名营地 NPC 移动及保存恢复均通过。未打包、未写测试脚本／用例；任务、商店出售／回购／赌博／修理及魔法货品、音频和路径动作 2／4／5 仍未实现。购买界面已完成 Windows 构建及 Akara 商店截图，购买操作仍待验收；画面和英文原版参照见忽略的 artifacts/ui-review-20260923/。以下旧格式现场均是历史记录。
- 调试管道入口见 [调试协议](../DEBUG_PIPE.md)：`interact` 可等待走近并打开 NPC 菜单，`talk` 返回原文，`shop` 查询原表货架，`buy` 执行购买事务，`gossip` 查看下一段，`identify` 执行真实付费事务，`grant-gold` 只为本地调试增加钱包金币；`objects` 返回 NPC 原身份、速度与有效路径节点数。完整 MPQ 运行时读取，没有独立导出对话／路径文件。
- 本轮对象动画纠正已纳入构建及短帧启动：普通物件读取 Objects.FrameCnt／FrameDelta／CycleAnim／Start，传送点和蓝门为 OP 一次段后进入 ON 循环，蓝门使用透明混合。以下传送点、蓝门截图和交互结论均是修正前现场，不能作为本轮视觉交互结果。
- 传送阶段 v10／规则 v30 记录：默认种子下第一幕九个传送点均可绘制；新游戏激活表为空，调试 travel 不解锁；实际交互激活营地和冰冷之原，第二次交互开菜单，已开启点往返、未开启目标拒绝、保存恢复保留激活记录均通过。截图 waypoint-inactive-v10.png、waypoint-activating-v10.png、waypoint-active-v10.png、waypoint-menu-v10.png 位于 artifacts。
- 回城卷轴已验证野外消耗一张、回营地、营地保存读档、返回原位置关闭门、旧门拒绝；城镇使用拒绝且卷轴不消耗。此前 v9 中间现场不兼容最终 v10。传送点模板是当前生成适配；多种子、回城门重开替换、远距离／死亡门禁尚未全面实测。所有代理调试实例已正常退出，未关闭用户游戏。

- Play.cmd 现默认开启 d2x-debug 并正常运行，支持 -DebugPaused、-PipeName、-NoDebugPipe；实际通过 Play.cmd 启动和默认客户端 status 验证 paused=false，随后正常退出。直接 EXE 的 --debug-pipe 仍默认暂停，新增 --debug-run 显式恢复启动模式，详见调试协议。
- 历史视觉／移动阶段 v8／规则 v27，最后提交 a2fc351。Windows 构建通过；管道短路径从 (379.5,181.5) 到 (380.5,181.5)，20 步后到达，再推进 20 步坐标与朝向不变，routePoints=0。原斧盾卸下、空手组合、重新装备和保存／恢复通过，场景 `artifacts/visual-v27.d2xsave`；截图 `visual-equipped-v27.png`、`visual-unarmed-v27.png`、`visual-flavie-v27.png` 保留于 artifacts。
- 历史 NPC 查询确认 Flavie、native.navi.0.16、坐标 (20,100)、renderable=true；当时怪物行走读取原 Velocity、生命／伤害仍为 MVP。洞口缺块尚未定位，未改地图／DT1 隐藏语义；完整方向、所有装备组合和怪物速度位移量仍未全面验收。该次调试实例已退出，原资源未改动。当前怪物数值范围见本页首段。
- 历史掉落阶段 v8／规则 v26：最后提交 `dfa9029` 后接入死亡入口、品质请求、金币钱包、普通消耗品／箭袋数量和 Windows 调试管道；未知品质实例整批暂缓。该阶段不代表当前格式或完整掉落可用。
- 本轮实际通过通用脚本调用管道：普通怪物四次击杀、四个已结算 ID；箭袋 aqv 数量 196、等级 2 正常入包；金币 5 正常入钱包、不占格，地面金币消失；重复击杀拒绝。保存／恢复后金币、箭袋、结算数及随机状态保持。现场 `artifacts/gold-pipe-v8.d2xsave`，截图 `artifacts/gold-wallet-v8.png`；原冠军金币 TC 种子 0、等级 4 的 mul=1280 输出 20 金币，旧 v7 明确拒绝。调试实例已正常退出；满钱包边界、六件上限及跨用户拒绝未实际验证。
- 最新用户明确授权新增命名管道和 PowerShell 调用入口。使用 [调试协议](../DEBUG_PIPE.md) 和 `scripts/Send-D2XCommand.ps1`，不是专用测试程序；Win32 仅位于 app，主线程处理命令，默认不监听。固定 JSON 3.11.3 依赖及 SHA256，首次构建需下载或提供 FetchContent 缓存。
- 历史 v25 品质检查：hp1 强制普通不消耗品质随机数，hax 的 MF=0／200 分母和 TC=1024 暗金修正、ba1 职业比例通过；非法等级 0 拒绝。当时的 v7 现场不能按当前 v8 恢复。当前真实交互证据以上述管道现场为准。
- 死亡入口构建与现有 `loot-entry` 查询通过：普通冠军 fallen1 为等级 3／TC2、独特为等级 4／TC3；噩梦邪恶洞窟普通 fallen1 为区域等级 36；Corpsefire 为等级 4／Act 1 Super A。andariel 缺任务状态、minion 缺所属群组修正时明确 Deferred。恢复 v7 现场运行 60 帧成功，截图 `artifacts/loot-entry-v7-restored.png`；未覆盖定向击杀，不写测试脚本／用例，不重新打包。
- 当前掉落查询入口：`d2x_assets assets/mpq2 treasure "Act 1 Champ A"`；可追加种子与怪物等级。游戏加载 1012 个 TC，恢复 `artifacts/equipment-v7-current.d2xsave` 后运行 60 帧成功，截图为 `artifacts/loot-tc-v7-restored.png`；未改变 v7／规则 v24，未重新打包。
- 装备 LoD 普通事务、指定位置卸下、基础武器伤害、防御、格挡及非堆叠耐久已接入；满包回滚、双手冲突、过期版本、键鼠穿脱和实际战斗效果仍待定向验收。高级效果、修理、武器组及动态外观未完成。
- Windows 构建通过，现有 `d2x_assets item` 查询验证了 2hs、rin、buc、sbw、sst、ba1 的部位、职业、继承与双手／弹药字段。初始装备实际使用原 CharStats 的 hax/rarm 和 buc/larm，经正式事务穿戴，不另造测试装备或物品创建命令。
- 当前 Windows 构建通过；完整源 MPQ 下营地装备面板运行 60 帧、v7 保存及恢复 60 帧通过。现场为 `artifacts/equipment-v7-current.d2xsave`，截图为 `artifacts/equipment-v7-current.png`、`artifacts/equipment-v7-restored.png`。短帧启动不证明完整交互正确，资源包／存档／截图不提交。
- 耐久分支曾在规则 v22 下运行洞窟 1800 帧；只读存档摘要显示生命 250、斧 28/28、盾 12/12、防御 4，战斗随机状态未变化，因此没有覆盖真实受击或耐久消耗。当时 v24 尚无这些效果的定向运行证据。历史 v4 独立装备包不代表当前资源闭包，当前版本未重新打包验证。
- 怪物阶段 `skeleton1` 与 `corruptrogue1` 已完成构建、原场景运行、保存恢复及独立包验证；SK 四动作分别 10/10、10/10、10/10、8/8 组件，CR 四动作均 9/9。历史普通怪物包为 `artifacts/d2x-ordinary-monsters-v18-20260923.mpq`，当时装备规则为 v24、保存格式 v7；旧怪物现场不可直接恢复。原专属 AI 和定向交互验收仍有缺口。

- 地图按预设、迷宫、野外三类逐项核对。1–39 关分别从仅含五个原始 MPQ 的运行目录短帧启动，39 份截图和日志位于忽略的 `artifacts/act1-map-review-20260923/`；第四层另截首领房。没有编写测试用例、脚本或专用测试程序。
- 公共近战此前在 v16 规则下完成邪恶洞窟 900 帧运行、恢复后继续 300 帧；隔墙、动态绕行及控制状态的定向交互验收仍待人工完成。本轮新增类型复用该逻辑，不声称专属 AI 已完成。
- 当前 Windows Release 构建通过。种子 210 普通难度、种子 20260922 地狱难度的牛场、野外边界、河桥、道路及悬崖洞口已加载，未报告缺瓦片；石阵、艾尼弗斯树与塔入口完整性检查通过。种子 1 实际选中 TownSTrans2，营地双向出口关联通过。牛场原牛王身份被识别，仍使用敌对替身。
- 地图阶段独立资源包 `artifacts/d2x-act1-terrain-v15-20260923.mpq` 曾通过种子 20260922 地狱牛场启动。当前新增怪物需本节 v18 包或完整源 MPQ。39/39 目录地形及零已知缺资源的报告仅针对已实现生成分支，不覆盖尚未执行的通用 LvlSub 主题。纯河水预设不执行独立区域到达点初始化。
- 地图阶段 v15 存档恢复曾通过，当时 v24 规则及 v7 格式不兼容旧存档。历史地图截图保留于 artifacts，三帧启动和截图不代替交互验收；资源包、截图和存档均不提交。
- `d2x_assets assets/mpq2 substitutions 6` 明确拒绝 `data/global/tiles/act1/outdoors/trees.ds1: Truncated game resource`。原文件声明的分组数量超过尾部完整记录数，详见 STATUS；此次核对 `d2exp.mpq`、`Patch_D2.mpq` 和 `d2x-act1.mpq` 均无另一份。通用主题执行暂缓，不修改原资源、不猜补分组。
- 后半幕此前已验证三个回廊朝向、两组种子／难度和独立包启动，现有全地图加载仍执行出口关联及区域内部出口可达性检查。完整键鼠往返、门交互和 Linux 运行仍待确认。
- 原始五个 MPQ 未改动。旧 `d2x-act1.mpq` 仍是 HUD 阶段的精简包，不含本轮全部生成模板。
- 当前默认启动直接读取完整 `assets/mpq2`。本机可用 `Play.cmd` 或 `build/bin/d2x.exe`；旧包仅作历史保留。

## 构建入口

需要 CMake 3.25+、C++20 编译器；依赖固定于 `cmake/Dependencies.cmake`。

```powershell
.\scripts\build.ps1
.\build\bin\d2x.exe --mpq assets/mpq2 --level 1 --map-seed 210
```

Linux 使用原生 CMake，见 [构建与分发](../BUILD_AND_SHARE.md)。格式化使用根目录 `.clang-format`。

常用代码导航：

| 工作 | 优先阅读 |
| --- | --- |
| 地图 | `world/region_catalog.cpp`、`maze.cpp`、`outdoor*.cpp`、`map_assembly.cpp` |
| 出口 | `world/exits.cpp`、`gameplay/session_exits.cpp`、`presentation/exit_view.cpp` |
| 怪物 | `content/monster_catalog.cpp`、`world/population.cpp`、`gameplay/monster_activation.cpp` |
| 物品 | `content/classic_data.cpp`、`lod_data.cpp`、`gameplay/items` |
| 技能／HUD | `gameplay/definitions.cpp`、`skills.cpp`、`presentation/hud_layout.hpp`、`skill_assets.cpp` |
| 保存 | `gameplay/session_snapshot.cpp`、`persistence/save_codec.cpp` |

## 修改约定

- 不编写测试脚本／用例／专用测试程序。检查范围服从当前用户指令。
- 修改前确认 MPQ 字段和参考代码；明确区分已导入、已执行、暂缓。
- 保留原始资源。提交显式选择源码与文档，不使用无范围的 `git add .`。
- 排除 `build/`、`external/`、`reference/`、`artifacts/`、`dist/`、存档、MPQ、`mvp/` 和旧 ZIP。
- 文档维护当前结论，不堆积工作过程；能力变化同步 STATUS 和对应专题。
- 交接说明改动、入口、未检查项；公共接口变更应先与集成方协调。
