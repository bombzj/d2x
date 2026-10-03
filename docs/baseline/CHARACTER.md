# 角色成长、客户端与保存值基线

更新：2026-10-03。对应 [技术改造方案](../TECHNICAL_REFACTOR_PLAN.md) P2 的成长／学习规则和保存值边界，以及 P1 的本人角色／技能 UI 切片；完整角色所有权与单位拆分尚未完成。

## 已实施的边界

- [`CharacterRecord`](../../src/gameplay/character/record.hpp) 包含角色身份／未知原生段、成长／已分配点、技能等级／选择／绑定、金币／初见／任务、D2S 支持的资源值及 `HirelingRecord`。只依赖 ID、基础学点／选择值、任务值和标准库；不包含 `PlayerState`、模拟器、会话、技能执行或战斗效果。
- [`CharacterSaveData`](../../src/gameplay/session/character_save.hpp) 的兼容成员 `player` 改为 `CharacterRecord`；容器／物品保存快照继续沿用原库存值类型。它不再通过完整世界状态头取得所有临时人物、怪物、弹体和技能执行类型。
- [`runtime_record.cpp`](../../src/gameplay/character/runtime_record.cpp) 显式完成运行人物 → 保存值、保存值 → 新运行人物的映射。只有该适配实现包含完整状态；接口前置声明运行类型。该文件编入纯玩法目标，不读取 MPQ 或文件。
- 会话保存先采集角色值，按原装备规则限制资源上限，再验证物品／容器。恢复先准备幕城镇、重映射所有者与库存 ID、验证保存值，再创建新人物与佣兵运行态，最后按原顺序推导装备／被动属性、初始化随机流并提交新局。
- `persistence/d2s_codec.cpp` 的任务与佣兵映射改用 `CharacterRecord`，原 D2S 字节编解码逻辑保留。`AttributeAllocation`、`SkillHotkey` 分别移至角色轻量值头，由运行状态与保存值共用定义。

保存值是一次传递快照，不是第二份可变权威角色。当前 `WorldState.player` 仍拥有活角色的成长、选择和动作；尚未把持久记录组合进运行人物，也未提供完整多玩家 `PlayerContext`。下列上下文仅借用原字段，不能据此宣称全部 P2 已完成。

## 成长与学习规则

| 入口 | 职责与限制 |
| --- | --- |
| [`progression.hpp/.cpp`](../../src/gameplay/character/progression.hpp) | 显式 `CharacterProgressionContext` 引用一个 actor 的经验、等级、分配点及余点；经验表和每级属性点由调用方传入 |
| [`learning.hpp/.cpp`](../../src/gameplay/character/learning.hpp) | `CharacterSkillContext`、学习规则及选择资格；校验等级／前置／上限、学习、绑定去重、左右键选择及两种重置；不包含会话、模拟器、完整人物、库存或内容表 |
| [`session_character.cpp`](../../src/gameplay/session/session_character.cpp) | 宿主绑定本人字段、将原内容定义映射为学习规则，并按原顺序重算派生属性／中断引导 |
| [`session_skills.cpp`](../../src/gameplay/session/session_skills.cpp) | 仍准备装备技能来源及施法输入；有效等级算术改调用 `resolveCharacterSkillRank`，基础等级／装备授予／各类加成主动传入 |
| [`intents.hpp`](../../src/gameplay/character/intents.hpp)、[`allocation.hpp`](../../src/gameplay/character/allocation.hpp) | UI 只需的学习／分配／绑定／选择意图与基础属性枚举；无需包含整份命令或属性派生头 |

经验封顶、升级加点及派生值／资源刷新时机沿用原路径。学习失败不消费点数；被动和不允许左键的技能不能绑定为相应主动选择。调试技能重置按等级加调用方统计的任务奖励点重建；Akara 正式重置按实际已分配点退还并清快捷绑定，资格／任务消耗仍由原任务协调检查。任务簿不会传入学习服务。

`actor` 表达上下文归属；可信绑定由宿主完成，不能用客户端自报 ID 当授权。当前宿主仍只绑定单个玩家，任务奖励、钱包、装备派生和活资源仍有旧协调入口。技能资格／来源仍由会话准备，起手、效果和光环运行已迁至独立 `SkillRuntime`；人物只提供借用能力，准确范围见 [通用技能基线](SKILL_RUNTIME.md)。角色批次本身仅迁移角色规则和显示计算。

后续 S1 求值切片已将光环纯求值迁至 `skills/aura_resolve.*`，祝福瞄准／抗性光环被动贡献迁至 `skills/passive.*`，恢复、装备派生和提示同步接入；内容准备在 `content/skills/aura_data.*`／`passive_data.*`。当前已实现技能的运行生命周期／被动反应及专精／Warmth等级算术已迁移，技能批次曾通过构建与代表性冒烟；后续公共单位清理也已通过 Windows Release 与简单冒烟，见 [技能基线](SKILL_RUNTIME.md) 和 [单位基线](UNITS.md)。下方角色批次既有结果不认证后续新源码。

## 本人 UI 投影

[`CharacterView`](../../src/contracts/character.hpp) 包含本人必要的成长、派生数值、资源、技能元数据／基础与有效等级、学习资格、选择／快捷绑定及提示行；不含任务簿、背包、完整装备、效果集合或 `SkillCastSpec`。[`ICharacterClient`](../../src/client/character_client.hpp) 读取投影并提交窄角色意图，[`LocalCharacterClient`](../../src/client/local_character_client.cpp) 负责本地映射和原命令转发。

角色面板、技能树、技能选择／F1–F8 绑定及 HUD 数值改读投影。伤害与光环／主动技能提示计算移至 [`content/character/character_display.cpp`](../../src/content/character/character_display.cpp)，显式接收装备／属性快照、已解析技能、基础等级及难度参数，不读取会话或 GPU。原表现布局、字段解释和计算顺序保持。

本地适配按权威更新版本缓存；`tick`（包含 `tick(0)`）、成功恢复及走跑设置完成后推进版本，UI 只在版本变化时复制新值。借用适配视图只到下次读取／销毁，场景保存独立值副本，不保留跨更新内部指针。相同缓存机制也用于已迁移库存。异步权威结果、联网身份认证和远端协议仍未实施。

## 格式、规则与恢复限制

D2S v96、原位与未知段保留规则不变，未引入私有存档格式或迁移。现有运行规则指纹 `act-two-quest-rules-v2-staff-opening` 不变：本次调整职责边界，没有改变原表、编码、任务规则或磁盘保存语义。

角色位置、派生属性、动作／目标、临时效果、随机流与佣兵路线不属于保存值；恢复创建默认运行态，再在原幕城镇建立位置与派生值。佣兵 `hp` 保留原适配语义：编码只写存活／死亡位，解码提供基础生命标记，会话再按装备上限恢复。原生角色 ID 仍不是 D2S 字段，快照 ID 只用于库存所有权验证并在载入时重新绑定。

## 构建与冒烟

本轮成长／学习及 UI 切片已完成 Windows Release 游戏／资源工具构建，以及既有调试管道和真实 UI 输入冒烟；未新增测试脚本、用例或专用程序，仅读写 `artifacts/refactor-character-20261003/` 临时档。

| 本批观察 | 结果与范围 |
| --- | --- |
| 升级及属性按钮 | 普通一级女巫授予 50,000 经验 → 9 级／40 属性点／8 技能点；真实四维按钮各加 1，重置后再加力量／体力各 1，最终余属性 38 |
| 技能树及拒绝 | 实际页签／节点学习 Fire Bolt、Warmth、Charged Bolt、Ice Bolt、Frozen Armor；9 级点击要求 12 级的 Lightning 未学习／未扣点；最终重置后重新学习 Fire Bolt、Warmth 与 Frozen Armor 各 1，余技能 5 |
| 来源与提示 | 火弹基础 1／法杖加成 1；卸装提示伤害 3.0–6.0，重新装备恢复 4.6–7.6。截图已查看，学习等级和装备来源分开 |
| 快捷键及资格 | 实际技能菜单 F1 → F2 同项重绑清旧键，菜单外 F2 选择后施法剩余为 0；被动 Warmth 及左键 Frozen Armor 绑定被拒绝；最终 F1 右手火弹保存恢复 |
| 本人投影及 D2S | 装备移除／恢复、重置、学习及成功读档刷新投影；9 级／经验 50,000、四维 11／25／11／35、余点 38／5、资源和初始 7 件物品恢复 |
| 光环显示 | 新 Paladin 实际技能树学习 Prayer 99 与 Might 98 各 1；两者当前／下级半径、治疗／伤害加成及法力提示截图已查看。此项不认证光环实际传播或全部技能效果 |

两个调试实例均退出 0、stderr 空。最终游戏／资源工具构建退出 0、无编译警告；新临时档与此前 P2 原 D2S 的独立加载均退出 0、stderr 空，截图已查看。首次独立加载沿用默认输出回写了此前 P2 临时输入，已从自动备份恢复原字节；最终将它复制到本批目录，并为两次加载显式指定不同输出路径。资源工具 `save-info` 识别新档为 D2S v96／9 级／经验 50,000／已分配 1,0,1,0。Ninja 实际编译依赖确认 `learning.cpp`、`progression.cpp` 不包含会话、模拟器、完整世界状态、`ClassicData`、资源或 GPU 头；角色面板、技能树、HUD 数值／菜单、技能控制器不再包含会话公共／私有头、模拟器、完整世界状态或 `ClassicData`。这些 UI 仍经共享场景／资源头取得 raylib 和原图类型，未测量编译耗时或运行性能。

构建日志为 `artifacts/refactor-character-build.log` 和 `artifacts/refactor-character-final-build.log`；JSON、截图、D2S、依赖清单位于上述日期目录，均忽略不提交。实际 Akara 任务重置、最高等级／跨难度、全部职业技能和长期战斗尚未覆盖；任务重置保留原入口并经源码核对。

此前保存值批次的独立证据如下，不能代替本轮 UI／规则验证。

2026-10-03 Windows Release 游戏与资源工具链接成功。Ninja 的 `d2s_codec.cpp` 实际编译依赖包含 `record.hpp`，不包含 `model/state.hpp`、`simulation.hpp` 或 `session_impl.hpp`；未测量增量编译耗时收益。

使用已有游戏／调试管道，仅读写 `artifacts/refactor-p2-20261003/` 中临时档，载入输入为此前 P0 生成的临时原 D2S；未新增测试脚本、用例或专用程序。

| 观察 | 保存／恢复结果 |
| --- | --- |
| 改造前临时档 | 区域 40，一级女巫、40 生命／35 法力成功载入 |
| 成长与钱包 | 50,000 经验／9 级、力量 11／体力 11、未用属性 38／技能 5、金币 1,234 保留 |
| 技能与选择 | Fire Bolt／Warmth／Frozen Armor 基础等级各 1，F1 绑定右手 Fire Bolt 保留 |
| 资源与装备 | 50 生命／44.2734375 法力保留，初始 7 件物品和法杖位置恢复；内部 ID 按原规则重新绑定 |
| 佣兵 | sourceRow 15／classId 271／merc02、2 级／经验 1,260、存活生命标记 40 保留；重新建立运行位置 |
| 临时效果 | 保存前冰封装甲 state 10／30% 防御状态存在；读档后效果列表为空，技能学习记录保留 |
| 独立进程 | 再次从新 D2S 加载上述成长／资源／佣兵，外观错误为空；截图已查看 |

两实例均退出 0、stderr 空；原资源工具 `save-info` 识别为 D2S v96 并读出上述成长／金币。构建日志：`artifacts/refactor-p2-build.log`；状态、技能、库存、佣兵 JSON 和截图在同名日期目录，均不纳入源码。非空任务进度、其他职业、不同难度、死亡佣兵及全部物品组合尚未覆盖；原位／未知段逻辑经源码审阅，未将本次冒烟称为完整存档兼容认证。
