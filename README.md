# D2X — Diablo II: Lord of Destruction C++

基于《毁灭之王》原始 MPQ 的单机 C++20 项目：地图、行走、技能、物品容器和存档。不依赖原版 EXE，不使用重制版资源。资料片为唯一运行目标，不提供经典版／试玩版入口。

**总目录：[BASELINE.md](BASELINE.md)。协作 agent 先读 [AGENTS.md](AGENTS.md)。**

## 当前范围

更新：2026-10-03。当前仍是单机项目，联网后端尚未实现；以下为源码能力范围，构建和运行证据按批次分别记录。

- 五幕关卡 1–136 均有原 MPQ 地形生成入口，涵盖预设、迷宫和野外；当前区域及直接连续邻区按需解码，已访区域保留缓存。地形覆盖不等于全部任务入口／变体完成，详见[地图](docs/ACT1_MAPS.md)。
- 七职业 COF/DCC 人物和装备外观、原表成长／四维加点，以及各 30 个技能树节点、学习门槛与 F1–F8 绑定。女巫 26 项主动及四项被动、圣骑士 30 项技能有玩法入口；死灵法师十项诅咒和骷髅召唤、部分亚马逊技能已接入。未实现技能仍拒绝执行，覆盖数量不代表完整原版等价，详见[技能](docs/SKILLS.md)。
- 怪物按原表人口计划和房间激活生成，真实身份、活动词缀与生成指令分离；已支持家族使用原动作／AI，未实现的敌对类型使用授权沉沦魔替身并保留真实身份，中立单位不替换成敌人，详见[怪物](docs/MONSTERS.md)。
- 原表物品、TC 掉落、拾取、背包、腰带、药剂、私人箱、方块、金币、堆叠和两组武器；装备直接属性与已支持战斗效果参与派生。方块已支持原 `cubemain` 赫拉迪克法杖配方，通用配方尚未实现；任务流程见[第二幕计划](docs/ACT2_QUESTS.md)，物品边界见[物品完成情况](docs/ITEM_COMPLETION.md)。
- 第一幕六项和第二幕六项任务有进度／奖励及交互入口，NPC 对白、服务菜单、任务日志与原地图物件已接入；第二幕完整流程及任务首领专属 AI 仍有限制，详见[第一幕任务](docs/ACT1_QUESTS.md)和[第二幕任务](docs/ACT2_QUESTS.md)。
- 使用原版 D2S v96 保存角色成长、技能／绑定、物品／容器、金币、已支持任务及传送点。读档在对应幕城镇建立新局，不保存怪物、弹体或地面掉落；不静默迁移旧内部格式，完整字段和兼容边界见[存档](docs/SAVES.md)。

当前事实先查 [BASELINE.md](BASELINE.md) 和对应专题；[能力与缺口](docs/baseline/STATUS.md)中的较早分批记录不能代替最新基线。

## 代码分工与重构进度

项目沿用组合、领域服务和窄接口：内容层读取 MPQ 并生成类型化定义，玩法执行规则，客户端投影向 UI 提供只读数据，UI 提交意图。目录与库的实际关系见[代码结构](docs/ARCHITECTURE.md)。

| 边界 | 当前入口与状态 |
| --- | --- |
| 会话与本地适配 | `GameSession` 为不透明外观，`GameSessionImpl` 负责权威组装；`client/local_*` 适配已迁移的人物、库存、角色、NPC／任务界面 |
| 角色与保存 | `gameplay/character/` 提供保存值、成长与学习／选择规则；活角色和多玩家上下文尚未完整拆分 |
| 单位与怪物 | `combat/{identity,relations,unit,damage_request}.*` 分别提供身份、关系、公共能力和伤害请求；具体记录绑定只在 `simulation/unit_records.*`，怪物生成头保留窄生成指令 |
| 技能、被动与光环 | `gameplay/skills/` 承担纯求值及当前已实现技能的通用执行；来源传入等级／协同／加成，世界操作经 `world_port.hpp`／`weapon_port.hpp`，具体适配在 `simulation/skill_world.cpp` |
| 后续改造 | 单位／活角色所有权、装备充能／触发来源、商店／佣兵和地图投影、跨领域事务与权威宿主；多人和网络在前置边界之后实施 |

实施顺序与完成标准见[技术改造方案](docs/TECHNICAL_REFACTOR_PLAN.md)，当前边界见[单位基线](docs/baseline/UNITS.md)、[技能基线](docs/baseline/SKILL_RUNTIME.md)和[会话基线](docs/baseline/SESSION.md)。未来服务端保存完整角色／任务／库存，客户端只接收本人必要私有视图与其他可见单位的公开状态；单机继续通过本地权威适配复用规则。

**源码与运行包状态**：公共单位／冗余清理已通过 Windows Release 编译／链接和三职业简单冒烟，覆盖女巫火弹、死灵诅咒／尸体召唤、圣骑士光环切换及脉冲；三个临时实例均退出 0、stderr 空，截图已查看。准确范围见[单位基线](docs/baseline/UNITS.md#验证范围)。未打包，`dist/current` 尚未包含这些重构改动；Linux、完整回归和联机未验证。

## 构建与资源

Windows 使用 `scripts/build.ps1`，启动示例（项目原资源位于 `assets/mpq2`）：

```powershell
.\build\bin\d2x.exe --level 1 --map-seed 210
```

本机可用 `Play.cmd`。未指定 `--mpq` 时，EXE 先查工作目录、EXE 同目录中的原始 MPQ，再查原有 `assets/mpq2` 路径；其他位置可显式传 `--mpq <目录>`。目录以 `d2data.mpq`／`D2Data.mpq` 为识别入口，命中后读取同目录的全部 MPQ，不递归搜索子目录。

本地打包运行 `scripts/package.ps1`，只更新固定 `dist/current/` 的程序和运行文件，不抽取或复制 MPQ。可在仓库根执行 `dist/current/Play.cmd -Mpq assets/mpq2`；包内默认存档与截图写入包目录。历史 `artifacts/` 中的重复 MPQ 和旧程序已清理，存档及画面资料保留，勿再使用旧包入口。

调试时可直接创建指定职业进入游戏，不需要角色界面或已有存档：`Play.cmd -Class Sorceress`；附加 `-Level 8 -DebugPaused` 可直接进入暂停的邪恶洞窟现场。EXE 对应参数为 `--class Sorceress --level 8 --debug-pipe d2x-debug`。使用存档场景则传 `-Load <角色.d2s>`，需要保存结果时另传 `-Save <输出.d2s>`；`-Class` 与 `-Load` 互斥。新建直达角色未指定 `-Save` 时不自动保存。

当前源码只使用原版 D2S v96，已接角色创建／列表与保存读取，不维护内部存档版本或旧档迁移。Windows 已有构建、原生存档往返和运行记录，具体批次／验证范围见[构建与运行](docs/BUILD_AND_SHARE.md#当前本地分发目录)及对应模块基线；最新重构的验证范围以上文为准。零售版客户端未验收，Linux 尚未实际编译运行。准确支持边界见[存档](docs/SAVES.md)；佣兵 Hire／O 窗口和快速授予见[佣兵](docs/HIRELINGS.md)。

## 操作

| 输入 | 功能 |
| --- | --- |
| 左键／方向键 | 寻路／辅助移动；野外边界直接跨区，洞口和楼梯点击进入 |
| 左键点敌人／物品／NPC | 单次攻击／走近拾取／交互；按住怪物持续攻击并锁定该目标 |
| 右键按住怪物／空地 | 持续使用右技能；怪物目标不随鼠标移开改变，空地施法跟随朝向 |
| NPC Trade 窗口左／右键点击背包或装备 | 确认出售／直接出售；修理模式左键修理 |
| 左／右技能菜单悬停 + F1–F8 | 绑定对应鼠标键技能；单按 F1–F8 切换技能，不立即施法 |
| 点击左右技能槽 | 展开当前职业已学技能菜单；Shift 左键使用左键技能 |
| I、A／C、S／T、1–4、B、Ctrl+F4 | 包裹、角色面板、技能树、饮药、展开腰带、走近私人箱 |
| O | 佣兵属性和装备面板，需已有佣兵 |
| W／背包 I–II 标签 | 切换两组武器 |
| Alt、Tab、R、按住 Ctrl | 物品名称、地图、走跑切换、临时跑步 |
| Ctrl+F2、PgUp／PgDn | 开发地图目录、翻页 |
| F11、Ctrl+F11 | 保存、读取；默认 `saves/quick.d2s` |
| Esc | 先关闭当前面板／对话，无面板时保存有存档归属的角色并返回角色列表 |
| P、M、Ctrl+R | 暂停、静音、开发用重置当前区 |
| Ctrl+F1、Ctrl+F3、F12 | 帮助、碰撞网格、截图 |
| Ctrl+Alt+G／E／A／T／W | 调试：加金币／经验、重置属性点、重置技能点、激活当前 MPQ 中已构建区域的传送点 |
| Ctrl+Alt+B | 在脚边掉落一件赫拉迪克方块；已有方块时不会重复生成 |

参数包括 `--class`、`--level`、`--map-seed`、`--difficulty normal|nightmare|hell`、`--population-seed`、`--save/--load`。新游戏默认生成新种子；`--seed <uint32>` 固定整局初始随机源，`--map-seed`／`--population-seed` 可单独覆盖地图／人口计划。载入 D2S 保留原地图种子，玩法使用新局随机流，见 [随机机制](docs/RANDOMNESS.md)；模板查看使用 `--preset <Def> --level-type <ID>`。城镇按原 `Skills.InTown` 限制施法，攻击法术与传送不可用，冰封装甲等原表允许的技能仍可用。

源码使用 GPL-3.0，暴雪素材权利独立，见 [第三方说明](docs/THIRD_PARTY.md)。
