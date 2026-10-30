# 编译依赖收缩设计

**状态：设计待审核，源码未实施。** 本文用于设计 PR 审核，规定后续实现范围、文件职责、迁移步骤和验收标准。合并设计不等于代码已经完成，也不自动授权当前会话修改源码或执行构建。实施与验证遵循[编码规范](CODING.md)、[测试指南](TESTING.md)和[架构边界](../architecture/OVERVIEW.md)。

## 1. 设计摘要

本轮只处理三类已经定位的依赖：

1. 三个仅声明函数的头文件，不再包含完整内容或宿主定义。
2. 将临时属性、属性结果与规则聚合／计算 API 分开，切断事件契约向整套规则的传播。
3. `ItemHandle` 改用已有轻量身份头，不再通过它带入身份分配与发布实现。

不改变类型字段、函数签名、所有权、运行时控制流或 CMake 目标。实现按三个可独立审核的提交组织，不扩展为全库头文件清理。

**主要验收目标：** 事件头和属性结果头不再直接或间接包含规则聚合头；在相同构建目标下，规则头变化触发的实际编译单元集合缩小。小型头文件清理与身份头替换另行记录收益，不预先承诺耗时百分比。

## 2. 问题与证据

### 2.1 当前包含链

[events.hpp](../../src/server/runtime/events.hpp) → [calculation.hpp](../../src/server/systems/attributes/calculation.hpp) → [prepared_rules.hpp](../../src/server/runtime/prepared_rules.hpp) → [skill_rules.hpp](../../src/server/runtime/skill_rules.hpp) → [hydra_spec.hpp](../../src/gameplay/skills/hydra_spec.hpp)。

传播原因：

- `CharacterFact` 按值保存 `Totals`，需要结果类型完整定义。
- `Totals` 与属性计算函数声明放在同一个头里。
- `Totals::auraTransient` 为 `optional<TransientAttributes>`，必须看到完整定义。
- `TransientAttributes` 与 `PreparedRules`、`CharacterRules` 同处规则聚合头，导致只消费属性结果的文件也依赖规则定义。

这不是事件系统确实需要全部技能规则，而是值定义的放置位置扩大了依赖。

### 2.2 静态取样与限制

2026-10-30按自有源码双引号 include、当前文件相对目录和源码根目录解析，计算反向传递闭包：

| 头文件 | 潜在受影响的不同 `.cpp` 数量 |
| --- | ---: |
| [prepared_rules.hpp](../../src/server/runtime/prepared_rules.hpp) | 408 |
| [calculation.hpp](../../src/server/systems/attributes/calculation.hpp) | 405 |
| [events.hpp](../../src/server/runtime/events.hpp) | 396 |
| [skill_rules.hpp](../../src/server/runtime/skill_rules.hpp) | 410 |
| [hydra_spec.hpp](../../src/gameplay/skills/hydra_spec.hpp) | 506 |

模拟移除“属性计算头 → 规则聚合头”这一条边，有82个源文件不再通过现有路径到达规则聚合头。这是候选影响，不是实际减少82次编译的承诺。

仅移除[社交系统头](../../src/server/systems/social/system.hpp)对事件头的直接包含，或[成长系统头](../../src/server/systems/progression/system.hpp)对规则聚合头的直接包含，静态影响数量没有下降，因为存在替代路径。因此验收需要检查传递可达性，不能只检查直接 include。

静态扫描未展开条件编译、第三方及未解析包含，未按 CMake 目标过滤，也未以编译器实际依赖校准。数量不能相加，不代表耗时。实施前重新记录所选文件的基线，不持续维护全库排名。

## 3. 范围与不变条件

### 3.1 本轮范围

| 工作包 | 内容 | 完成边界 |
| --- | --- | --- |
| W1：声明头收缩 | 存档、物品价格、商店准备三个接口头 | 使用前置声明和直接依赖，不改变函数声明 |
| W2：属性值契约拆分 | 提取临时属性、提取 `Totals`、迁移结果消费者 | 结果与事件不再依赖规则聚合或计算 API |
| W3：轻量身份入口 | 仅替换 `ItemHandle` 的身份头入口 | 身份值和分配／发布实现不再通过该入口绑定 |

允许修改上述变更直接暴露的缺失包含：只能在实际使用完整类型、计算函数或分配器的文件中补对应 include，不顺带修改逻辑。

### 3.2 必须保持

- 类型命名空间、字段类型、顺序、默认初始化、成员函数及特殊成员语义不变。
- 原函数名称、签名、默认参数、返回类型和符号不变；不复制类型或建立第二套状态。
- 不改变分配、共享、复制、空值和生命周期行为，不把按值字段改成指针。
- 不改玩法数值、随机消费、调用次序、事务发布、存档或原协议语义。
- 不改优化等级、警告选项、目标图或平台支持范围。
- 既有 MSVC 兼容代码保持原样，本轮不重新设计 `swap` 或异常安全。

### 3.3 明确不做

不调整 public／internal 目录，不增加库或移动已有实现文件，不引入 PImpl、PCH、unity build、缓存或新检查工具，不修改构建脚本。

不继续清理其他身份消费者，不拆分技能规则，不处理组合根和宿主接口隔离。上述内容属于后续独立设计，不能随本轮实施自动扩展。

## 4. 文件与接口设计

### 4.1 W1：三个声明头

| 文件 | 修改 |
| --- | --- |
| [save_file.hpp](../../src/persistence/save_file.hpp) | 移除内容聚合头包含，在 `d2x` 中前置声明 `struct ClassicData`；保留存档类型及文件接口所需依赖 |
| [item_pricing.hpp](../../src/content/items/item_pricing.hpp) | 移除内容聚合头包含；前置声明 `ClassicData`、`VendorDefinition`，保留 `ItemInstance` 前置声明；直接包含 `<cstdint>`、`<optional>`、`<span>`、`<string_view>` |
| [merchant_content.hpp](../../src/hosting/merchant_content.hpp) | 移除宿主与内容聚合头包含；前置声明 `class GameHost`、`struct ClassicData`，直接包含[玩家／游戏身份契约](../../src/server/player_identity.hpp)以提供 `GameHandle` |

对应实现若读取类型成员，应直接包含完整定义。其他消费者原来依靠上述头获得完整定义时，也在使用处补齐，不把旧聚合头通过另一个公共头加回来。

W1不新建源码文件，不改变任何函数参数、默认值或函数体。

### 4.2 W2：临时属性值契约

新增 `src/server/runtime/transient_attributes.hpp`，将 `TransientAttributes` 从[规则聚合头](../../src/server/runtime/prepared_rules.hpp)原样移入：

- 保留 `d2x::server` 命名空间。
- 原样保留 `modifiers`、`states`、`sources`、`effects` 及 `statSources()`。
- 直接包含属性修饰值、`ModifierStatSource` 和所需标准类型定义。
- 对 `CombatEffectSet` 使用前置声明，其成员是 `shared_ptr<const CombatEffectSet>`，此处不读取实现。
- 不包含任何服务端规则聚合头、系统实现头或属性计算头。

规则聚合头改为包含这个新头，保留原来从该入口使用 `TransientAttributes` 的源码兼容性；不在本轮强制迁移所有旧入口消费者。

所有按值或以 `optional` 持有临时属性的头，必须通过直接值契约获得完整定义，不能只前置声明。只声明引用／指针或返回值的地方按实际要求处理。

### 4.3 W2：属性结果值契约

新增 `src/server/systems/attributes/totals.hpp`，将 `Totals` 从[属性计算头](../../src/server/systems/attributes/calculation.hpp)原样移入：

- 保留 `d2x::server::attributes` 命名空间，完整保留所有字段、顺序、初始值和注释。
- 直接包含字段所需的角色属性、装备值、贡献值、技能来源、属性快照、资源及临时属性契约。
- 对仅由 `shared_ptr` 持有的类型使用合法前置声明；按值、`optional` 等需要完整定义的字段保留完整类型依赖。
- 不包含 `calculation.hpp`、`prepared_rules.hpp`、`skill_rules.hpp`、`events.hpp` 或领域系统头，也不得通过其他路径间接包含它们。
- 不添加计算逻辑、构造函数、转换函数或新缓存。

将[属性计算头](../../src/server/systems/attributes/calculation.hpp)限定为计算接口：

- 包含新结果头，维持实际计算调用者获得 `Totals` 定义的入口。
- 保留 `AuraSourceObservation`、`AuraSourceObserver`、`AuraSourceReader`、`AuraMutation`、`InitialAuraMutation`、`PassiveCacheRefresh` 及原计算函数声明。
- 移除规则聚合头依赖；对仅用于函数引用参数的 `server::CharacterRules`、`server::EquipmentRules` 使用前置声明。
- 对 `ItemCatalog` 使用其真实声明种类 `class`，其他类型同样核实，不把别名当作结构体前置声明。
- 原有计算函数签名与默认参数不变，直接包含接口声明所需依赖。
- [计算实现](../../src/server/systems/attributes/calculation.cpp)直接包含它实际读取的角色／装备等规则定义；不依靠别的实现头偶然带入。

本轮只新增上述两个头文件，不新增编译单元，不修改 CMake 目标。规则定义本身仍保留原位置。

### 4.4 W2：消费者迁移清单

| 消费者 | 目标入口与要求 |
| --- | --- |
| [events.hpp](../../src/server/runtime/events.hpp) | 用结果头替换计算头；事件值保持不变，不再传递包含规则聚合和计算 API |
| [player_state.hpp](../../src/server/player_state.hpp) | 用结果头替换计算头；由于按值持有 `PreparedRules`，直接包含规则聚合头，不伪装成纯结果消费者 |
| [transactions/plan.hpp](../../src/server/systems/transactions/plan.hpp) | 用结果头替换计算头；临时属性、世界状态及事件等直接依赖按真实需要保留 |
| [attributes/system.hpp](../../src/server/systems/attributes/system.hpp) | 公开声明只使用结果时改为结果头；计算入口由实现直接包含 |
| [hosting/native_character_wire.cpp](../../src/hosting/native_character_wire.cpp) | 若只读取结果，改含结果头；确认不调用计算入口 |
| [effects/attribute_projection.hpp](../../src/server/systems/effects/attribute_projection.hpp) | 使用观察者／回调别名，保留计算接口头，不新增规则聚合依赖 |
| [effects/system.hpp](../../src/server/systems/effects/system.hpp) | 按回调接口和临时属性的真实使用保留直接依赖，不强制改成只含结果头 |
| [attributes/stat_sources.hpp](../../src/server/systems/attributes/stat_sources.hpp)、[inventory/eligibility.hpp](../../src/server/systems/inventory/eligibility.hpp) | 仅收尾本次拆分暴露的声明依赖，不顺带开展第二轮接口拆分 |
| 其余真正执行 `calculate`／`loadout` 的实现 | 直接包含计算接口及自己读取的规则定义 |

特别检查曾经只包含事件头、却调用属性计算函数或读取规则成员的实现文件。这类编译失败应在使用处补头，不能恢复“事件头 → 计算／规则”的旧链。

拆分期间消费者可能短暂缺少传递声明；W2必须作为一个完整、可编译的提交交付，不能将未完成迁移状态作为独立提交。

### 4.5 W3：轻量身份入口

[ItemHandle](../../src/gameplay/items/handle.hpp)只保存 `EntityId` 与修订号，将其 `core/id.hpp` 包含替换为已有的[core/entity_id.hpp](../../src/core/entity_id.hpp)。

不改变结构体，不新建身份类型。真正使用 `EntityIds`、`NativeUnitIds` 或发布对象的消费者，在使用处直接包含相应完整头。

验收约束是 `ItemHandle` 头的传递依赖不再到达[身份分配／发布实现](../../src/core/id.hpp)或[原生身份注册实现](../../src/core/native_unit_id.hpp)。不要求真正使用这些实现的其他文件停止包含它们。

## 5. 实施顺序与文件归属

建议后续实现 PR 按以下三个提交组织，分别可审核、可撤回：

| 提交 | 工作与前置条件 | 提交前检查 |
| --- | --- | --- |
| 1 | W1；无代码前置条件 | 三个声明头及全部受影响消费者编译通过，未改变函数接口 |
| 2 | W2；修改前记录独立基线，不依赖W1的代码改动 | 两个新头、原定义移除、消费者迁移和规则显式包含一次完成；主要依赖与增量验收通过 |
| 3 | W3；无代码前置条件 | 轻量入口正确，真正使用分配器的消费者已补直接包含 |

提交顺序不代表W2／W3在技术上依赖W1。若跨提交测量，分别记录每个提交的基线，不把前一项收益记到后一项。

公共类型、新头、事件、玩家状态和事务计划由同一集成开发者负责。若多人协作，其他开发者仅负责提前分配的消费者 include 收尾；不得同时编辑公共模型、事务实现或 CMake。实现中的额外需求超出本节范围时，先回到设计审核，不自行扩项。

## 6. 验证设计

### 6.1 环境与基线

实施前固定源码版本和工作区差异，记录编译器、STL、CMake、生成器、目标、配置、并行度、编译缓存与并发负载。

Windows可复用现有 `build-vs2022` 的 VS2022 Release 配置，客户端目标为 `d2x`，并行度固定为8。若实施环境不同，使用对应有效配置并在结果中写明。仅在产物不完整时补必要基线构建，不先 clean，也不为了取证另起全量构建。

配置、编译、静态库更新和程序链接分别计时。不要在同一对照中混用[build.ps1](../../scripts/build.ps1)与直接 `cmake --build` 的耗时：前者包含 configure 开销；单配置构建目录也不得在对照中切换 Release／Debug。

### 6.2 必须满足的结构检查

1. `TransientAttributes` 和 `Totals` 各只有一个定义，命名空间、字段及默认初始化与改前一致。
2. 新临时属性头、新结果头和事件头均不能传递到达规则聚合头或技能规则头。
3. 新结果头和事件头不能传递到达计算接口头；计算接口可以依赖结果，反向不允许。
4. `ItemHandle` 不能传递到达身份分配／发布或原生身份注册实现。
5. 头文件直接包含自身需要的定义；既有源文件按通常方式单独编译，不靠 unity build 掩盖缺失依赖。
6. 不通过新增别的聚合头把禁止的依赖重新带回，也不通过修改对象表示来满足检查。

采用定点静态包含分析与已有编译器依赖记录交叉核对。所有受支持配置中的源码依赖约束都适用；单个配置的成功编译不证明其他条件分支也满足。

### 6.3 增量对照场景

前后使用同一工具链、目标、参数和工作区条件。每个触发案例都从输入稳定、上一轮构建成功的状态开始，一次只触发一个文件。

| 场景 | 改前／改后操作 | 预期与判定 |
| --- | --- | --- |
| 无修改 | 再次构建同一目标 | 零源文件编译，无非必要库更新或链接；否则先调查，暂停性能比较 |
| 普通源文件 | 触发同一个局部 `.cpp` | 编译范围符合实际目标登记，不因本轮迁移扩大 |
| W1声明头 | 触发同一个选定内容定义头 | 记录全部实际编译单元；若其他路径仍存在，如实记录零范围收益，不宣称删 include 等于少编译 |
| W2规则聚合 | 触发 `prepared_rules.hpp` | 改后实际编译单元集合是改前的真子集；列出被移除单元，并证明其不读取规则，不能只比较总数 |
| W2拆出值定义 | 分别触发原规则头和新临时属性头，并在改前以原规则头中的相应值定义修改作为对照 | 记录原职责的新影响范围，确认收益不是把所有依赖原样转移到另一个聚合头 |
| W2计算接口 | 触发 `calculation.hpp` | 只需结果的消费者不再通过新结果／事件入口依赖计算 API；实际单元若因另一条真实计算路径仍依赖它，单独说明 |
| W3身份管理 | 触发 `core/id.hpp` | 核对只需 `ItemHandle` 的路径已解除，其他路径仍使用分配器的单元允许保留 |

时间戳触发只证明构建系统的失效传播，不证明内容修改后编译仍正确；结构迁移本身必须通过真实编译。仅时间戳触发须获准，不改正文或覆盖用户已有修改。

W2是本轮主要收益门槛。若编译集合未收缩，不能以静态82个候选或W1的小型清理替代验收；需检查消费者补头和替代路径，必要时缩回无收益改动并说明阻塞。

### 6.4 编译、行为与平台

- Windows至少覆盖 `d2x`、`d2x_pvpgn`、`d2x_assets` 的实际受影响目标与下游编译／链接；复用已有产物，不要求每轮全部重编译。
- Linux使用已有有效配置覆盖对应受影响目标，保持相同C++20契约。未取得验证结果时，明确标记平台验收未完成，不以Windows成功替代。
- 按[构建验收要求](BUILD_AND_RUN.md#构建验收要求)检查退出码及编译／链接零警告。基线已有警告单独列出并标为验收阻塞，不在本轮顺带修复无关问题，也不关闭警告规避。
- 本设计不改运行逻辑，不新增测试脚本、用例或专用程序。若实现需要改变所有权、布局、成员语义或生命周期，停止扩展，单独修订设计并定义现有运行入口的针对性回归。
- 宣称耗时收益时，对关键场景改前／改后各测3次，报告中位数、范围及缓存状态。不把迁移后的首次适配构建与热增量构建比较。

### 6.5 交付材料

实现 PR 附上：改动前后版本、工具链与目标、结构约束检查结果、规则头触发的实际编译单元集合差异、构建警告／错误、平台覆盖及未完成项。测量耗时则附上同条件结果，不预先设定百分比。

原始日志和依赖记录留在忽略目录或PR附件，不提交资源、缓存、构建产物或测试辅助程序。设计正文维护结论和边界，不追加会话日志。

## 7. 替代方案与取舍

| 方案 | 本轮决策 | 原因 |
| --- | --- | --- |
| 只删几条直接 include | 不作为完整方案 | 替代路径可能保留全部重编译范围 |
| 将属性结果／规则改成指针或PImpl | 不采用 | 引入分配、空值、布局及生命周期变化，超出依赖问题所需 |
| 全面迁移public／internal目录 | 后置 | 当前源码根目录公开，搬目录不能自动隔离；也不是切断本轮链的前提 |
| 首先启用PCH、unity build或缓存 | 后置 | 不直接解除失效传播，且会改变测量条件或编译行为 |
| 新建两个值头，原入口保留合法兼容包含 | 采用 | 不改类型与运行时语义，可逐个迁移真正的结果消费者 |

## 8. 审核关注点与后续边界

设计审核重点：

- 两个值契约的归属是否正确，字段与命名空间是否保持原样。
- 保留回调别名在计算接口、结果头不反向依赖计算接口的边界是否明确。
- 玩家状态真实依赖规则，与事件只消费结果的区别是否保留。
- 消费者补头是否会恢复旧链，主要收益是否可按实际编译集合验收。
- 三个工作包是否足够小，平台验证与已有警告是否如实列为门槛。

设计获批后，按本范围提交实现PR；本设计PR不携带源码优化。无需为批准设计改写[项目基线](../../BASELINE.md)中的能力状态。实现完成后，在本页更新实际边界与验收结果，并按职责同步[架构文档](../architecture/OVERVIEW.md)及相关模块入口。

宿主组合根隔离、其他身份消费者、技能定义隔离和构建层public／private治理继续作为后续候选，不由本设计授权。
