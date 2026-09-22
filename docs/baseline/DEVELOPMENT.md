# 开发与交接

## 当前交接状态

- 最新授权为补充简单普通怪物后提交累计源码，Boss／复杂行为不做。本轮 `skeleton1` 与 `corruptrogue1` 均完成 Windows 构建、600 帧原场景运行及同规则恢复继续 300 帧；SK 四动作分别 10/10、10/10、10/10、8/8 组件，CR 四动作均 9/9。仍未完成原专属 AI 和定向交互验收。
- 普通怪物验证包为 `artifacts/d2x-ordinary-monsters-v18-20260923.mpq`，仅挂载该包分别运行埋骨之地和冰冷之原 300 帧并保存成功。截图为 `artifacts/ordinary-skeleton-v18-packed.png`、`artifacts/ordinary-rogue-v18-packed.png`。当前规则 v18，不兼容此前 v15／v16／v17 规则存档。验证产物不提交，未写测试脚本。

- 地图按类型逐项实现；按改动量分批构建、资源打包、短帧启动及截图验证，不编写测试用例、脚本或专用测试程序。
- 公共近战此前在 v16 规则下完成邪恶洞窟 900 帧运行、恢复后继续 300 帧；隔墙、动态绕行及控制状态的定向交互验收仍待人工完成。本轮新增类型复用该逻辑，不声称专属 AI 已完成。
- 当前 Windows Release 构建通过。种子 210 普通难度、种子 20260922 地狱难度的牛场、野外边界、河桥、道路及悬崖洞口已加载，未报告缺瓦片；石阵、艾尼弗斯树与塔入口完整性检查通过。种子 1 实际选中 TownSTrans2，营地双向出口关联通过。牛场原牛王身份被识别，仍使用敌对替身。
- 地图阶段独立资源包 `artifacts/d2x-act1-terrain-v15-20260923.mpq` 曾通过种子 20260922 地狱牛场启动。当前新增怪物需本节 v18 包或完整源 MPQ。39/39 目录地形及零已知缺资源的报告仅针对已实现生成分支，不覆盖尚未执行的通用 LvlSub 主题。纯河水预设不执行独立区域到达点初始化。
- 地图阶段 v15 存档恢复曾通过，当前 v18 规则不兼容旧规则存档。历史地图截图保留于 artifacts，三帧启动和截图不代替交互验收；资源包、截图和存档均不提交。
- `d2x_assets assets/mpq2 substitutions 6` 明确拒绝 `data/global/tiles/act1/outdoors/trees.ds1: Truncated game resource`。原文件声明的分组数量超过尾部完整记录数，详见 STATUS；通用主题执行暂缓，不修改原资源、不猜补分组。该缺口不因本阶段提交而视为解决。
- 后半幕此前已验证三个回廊朝向、两组种子／难度和独立包启动，现有全地图加载仍执行出口关联及区域内部出口可达性检查。完整键鼠往返、门交互和 Linux 运行仍待确认。
- 原始五个 MPQ 未改动。旧 `d2x-act1.mpq` 仍是 HUD 阶段的精简包，不含本轮全部生成模板。
- 当前启动优先完整 `assets/mpq2`；旧精简包和 `dist/` 压缩包不代表此基线。

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
