# D2X 项目初步审查与优化方向

审查范围：AGENTS.md、BASELINE.md、架构速览、编码规范、测试指南、CMakeLists.txt、src 目录结构与抽样源码、Git 状态。未构建、未运行，结论均为静态观察。

## 一、整体印象

**规模**：src 下 1722 个源文件、约 21 万行；docs 下 191 篇文档、约 4.4 万行；近 30 天 145 次提交，迭代很快。

| 模块 | 文件 | 行数 |
| --- | --- | --- |
| server | 521 | 72k |
| presentation | 125 | 27k |
| app（其中 debug 约 15k） | 72 | 19k |
| content | 191 | 18k |
| gameplay | 398 | 18k |
| hosting | 120 | 16k |
| world | 123 | 12k |
| 其他（client／resources／network／persistence 等） | — | 28k |

**做得好的地方**

1. **分层清晰且大体落地**。“hosting 准备／适配 → server 权威执行 → 事实 → 原包投影”的链路在文档和 CMake 目标中一致。抽查 `src/server/**` 没有任何 `resources/`、`content/`、`network/`、`presentation/`、raylib、Win32 头的包含，内核隔离是真实的。
2. **状态所有权明确**。每类状态有唯一主人（PlayerStore、AreaStore、EventOutbox 等），GameInstance／GameSystems 只做组装，避免了“万能会话”回潮。
3. **对原版忠实度的纪律很强**：数据优先取 MPQ、调用链对照 D2MOO、缺依据就明确拒绝而非自造；源码中 TODO/FIXME 仅 4 处，说明“未实现”多以显式拒绝表达，而不是暗藏的占位。
4. **文档如实区分“源码／已构建／已实测”**，不夸大完成度，这一点难得。
5. 编译告警已开到 `/W4` 与 `-Wall -Wextra`。

**主要隐忧**（按影响排序）

1. 大量改动处于“源码已改、未构建／未运行”状态，验证闭环弱，缺陷会累积到后期集中暴露。
2. 分层约束主要靠约定，构建系统只能部分兜底，已出现少量反向依赖。
3. 文档体量大、单段密度高，BASELINE 正在变成“过程记录”，与自身维护原则冲突。
4. 工程化基础设施（CI、提交粒度、调试代码隔离、CMake 组织）跟不上开发速度。

## 二、优化与改造方向

### 1. 验证闭环（优先级最高）

现状：BASELINE.md 中“未构建／未运行／待验收／待实测”类标记约 20 处；AGENTS.md 规定默认不构建、不写测试，验证完全依赖人工实机与调试 AI。

方向：
- **至少保证“每次提交可编译”**。把“未构建”从常态变成例外：合并前跑一次 Release 构建（现有 `scripts/build.ps1` 即可），文档里就不必反复标注“新增源码未构建”。
- **把已有调试设施沉淀为可重放的回归手段**，不新增测试程序：headless 入口、调试管道、宿主单步、调试 AI 已经具备“固定种子 + 固定输入 → 权威事实流”的能力。建议录制少量关键场景的 C2S 输入与 S2C／事实输出作为基准，改动后用同一入口重放比对。这与“不写测试用例”的约定是否冲突，需要你来定；即使只在大重构（战斗 R1–R5 收尾）期间启用，收益也很高。
- **随机流／执行次序**是项目明确要保持的语义，恰恰最难靠人工观察发现回归，最需要上面这种轨迹比对。

### 2. 依赖边界由“约定”变为“构建强制”

现状：
- `d2x_core` 以 `PUBLIC` 方式暴露整个 `src/` 为包含根，任何目标都能 `#include` 任何头，只有链接阶段才可能报错（纯头文件依赖根本不会报错）。
- 已发现反向依赖：`gameplay/actions/timeline.cpp`、`gameplay/actions/monster_timing.cpp`、`gameplay/events/frame_queue.cpp` 包含 `server/runtime/events.hpp`，而 `d2x_gameplay` 并不链接 `d2x_server`，依赖方向为 gameplay → server，违反架构图。
- content 中约 50 个文件（含多个公开头）直接包含 `resources/archive.hpp`，“已准备的纯值定义”与“从 MPQ 加载”的接口混在同一头里，下游只想用定义也会间接依赖归档类型。

方向：
- 把上述三处共享的类型下沉到 `gameplay` 或 `contracts`，消除反向包含。
- content 按“定义头（纯值）／加载头（依赖 Archive）”拆分，hosting 只在准备阶段包含加载头。
- 增加一个轻量的包含方向检查（按目录规则匹配 `#include`，可放在构建脚本或 CI 中），把架构图变成可执行规则。这属于构建检查而非测试用例，是否接受由你决定。
- 长期可考虑按库设置各自的 include 根，让越界包含在编译阶段直接失败。

### 3. 文档治理

现状：
- BASELINE.md 自述“只维护能力概况，不放过程记录”，但多数段落已经是长句堆叠的过程状态（“此后续源码未构建／试听”“用户画面暴露……进一步修正”），单段可达数百字，阅读成本高，agent 也难以精确更新。
- `docs/resources/pd2-core/functions/*.md` 单文件可达 657KB，属于逆向导出的资料，与手写设计文档混放；`docs/development/DEBUG_AI.md` 达 142KB。

方向：
- BASELINE 改为**结构化状态表**：每项能力一行，列为“源码／已构建／已实测（条件）／主要缺口／专题链接”。“未构建”等状态进入表格列而不是正文，构建通过后只改一格。
- 把机器导出的逆向资料移到 `reference/` 或单独的资料目录，`docs/` 只放手写的事实、入口和限制。
- 对超长专题（DEBUG_AI、MAP 等）按“当前行为／配置语法／原版依据／已知限制”拆分子页。

### 4. 调试代码与主程序隔离

现状：`src/app/debug/` 约 1.5 万行（调试 AI、导航搜索、自动行走、在线命令等）直接编进 `d2x` 主程序，构建后还会复制调试 AI 配置。

方向：
- 拆成独立静态库（如 `d2x_debug`），由 CMake 选项（如 `D2X_ENABLE_DEBUG_TOOLS`）控制是否链接；发布包可不带调试 AI 与命令管道，减少攻击面和体积。
- 调试 AI 是目前最重要的自动验证手段，独立出来也便于它单独演进，不与前端代码互相牵连。

### 5. 构建与工程化

- **CMake 组织**：单个 654 行的根 CMakeLists.txt 手工列出全部源文件。AGENTS.md 已经把 CMake 列为多 agent 冲突热点；建议按模块拆分为 `src/<module>/CMakeLists.txt`，各自维护源文件列表，减少并发冲突。
- **CI**：仓库没有 CI（`.github/` 下只有与本项目无关的 modernize 钩子目录，可删除）。建议至少在 Windows 与 Linux 上做编译检查。文档称保留 Linux 路径但“未认证”，没有持续编译，这条路径很容易悄悄失效。
- **编译成本**：MinGW 需要 `-Wa,-mbig-obj`，原因是领域事实 variant 模板实例化超过 COFF 节数上限。规范中已提出“布局独立的 DomainFact”，建议继续推进：缩小 variant、按领域拆分或类型擦除，以降低编译时间和目标文件体积。
- **提交粒度**：近期提交常一次改动 80–366 个文件，标题为“Improve A, B, C”。建议按主题拆分提交（例如战斗、UI、音频、调试 AI 分开），便于回溯和二分定位回归，这对“保持随机消费与次序”的项目尤其重要。
- **代码风格**：存在 `.clang-format`，但抽查代码有较密的单行写法（如 `frame_queue.cpp` 中 `(void)handle;if(...) break;`）。建议只对修改过的文件逐步格式化，避免一次性全量格式化污染 blame。

### 6. 数据驱动与硬编码

- `src/resources/presets.cpp` 4588 行，是来自 OpenDiablo2 的第一幕 DS1 对象查找表的硬编码。这与“数据尽可能从 MPQ 动态读取”的约定存在张力。建议核实能否由 MPQ 表（如 monpreset、objects、monstats 系列）推导；若原版本身就是硬编码在 DLL 中，则保留，但应在文档中注明依据，并说明其他幕如何处理。
- 建议对源码中的大型常量表做一次盘点，区分“协议／算法常量”（可以写死）与“数据参数”（应来自 MPQ），与编码规范中的划分对齐。

### 7. 热点文件

服务端领域规模大致均衡（effects 8.4k、skills 8.1k、ai 7k、monsters 5.9k 行），单文件都控制在 1000 行以内，状态良好。客户端和调试侧有几个 1500–2100 行的文件：`ai_navigation.cpp`、`remote_scene_effects.cpp`、`realm_frontend.cpp`、`client_missile_view.cpp`、`remote_world.cpp`、`frontend.cpp`、`remote_town.cpp`。可以在下次实际修改时顺带按职责拆分，不需要专门立项。

### 8. 工作区卫生

- `artifacts/` 已有 102 项、约 8.3GB（虽已被忽略），包括大量按日期命名的运行记录。建议定期归档或清理，或者改到工作区之外的目录，以免拖慢编辑器索引和搜索。
- 根目录中的本地配置（`client-settings.json`、`*.local.json`、`.lock`）已被忽略，但它们与源码放在一起，容易让人误以为是项目配置。可以考虑统一放到 `local/` 之类的目录。
- 当前有未提交的改动（README.md、docs/modules/MAP.md、remote_scene 两个文件，以及未跟踪的 docs/development/AI_TEST_REC.md），本次审查没有动它们。

## 三、建议的推进顺序

1. 确立“每次提交可编译”，并补上 Windows／Linux 编译 CI。
2. 修复 gameplay → server 的反向包含，加上包含方向检查。
3. 把 BASELINE 改为结构化状态表。
4. 拆分 CMake，并把调试工具独立成库。
5. 结合战斗重构收尾，引入基于现有 headless／调试管道的轨迹重放比对（需你决定是否放宽“不写测试”的约定）。
6. 其余各项（硬编码表、大文件、编译成本、工作区清理）随日常修改逐步处理。
