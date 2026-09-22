# 开发与交接

## 当前交接状态

- 当前任务转为怪物掉落，装备剩余工作暂停，既有修改保留。掉落 TC 选择阶段已构建通过；现有资源工具验证 NoDrop、负 Picks、自动类别、品质修正继承和根等级升级，未编写测试脚本／用例。游戏内品质与实例生成尚未接通，不宣称实际击杀掉落可用，源码未提交。
- 当前掉落查询入口：`d2x_assets assets/mpq2 treasure "Act 1 Champ A"`；可追加种子与怪物等级。游戏加载 1012 个 TC，恢复 `artifacts/equipment-v7-current.d2xsave` 后运行 60 帧成功，截图为 `artifacts/loot-tc-v7-restored.png`；未改变 v7／规则 v24，未重新打包。
- 装备 LoD 普通事务、指定位置卸下、基础武器伤害、防御、格挡及非堆叠耐久已接入；满包回滚、双手冲突、过期版本、键鼠穿脱和实际战斗效果仍待定向验收。高级效果、修理、武器组及动态外观未完成。
- Windows 构建通过，现有 `d2x_assets item` 查询验证了 2hs、rin、buc、sbw、sst、ba1 的部位、职业、继承与双手／弹药字段。初始装备实际使用原 CharStats 的 hax/rarm 和 buc/larm，经正式事务穿戴，不另造测试装备或物品创建命令。
- 当前 Windows 构建通过；完整源 MPQ 下营地装备面板运行 60 帧、v7 保存及恢复 60 帧通过。现场为 `artifacts/equipment-v7-current.d2xsave`，截图为 `artifacts/equipment-v7-current.png`、`artifacts/equipment-v7-restored.png`。短帧启动不证明完整交互正确，资源包／存档／截图不提交。
- 耐久分支曾在规则 v22 下运行洞窟 1800 帧；只读存档摘要显示生命 250、斧 28/28、盾 12/12、防御 4，战斗随机状态未变化，因此没有覆盖真实受击或耐久消耗。当前 v24 尚无这些效果的定向运行证据。历史 v4 独立装备包不代表当前资源闭包，当前版本未重新打包验证。
- 怪物阶段 `skeleton1` 与 `corruptrogue1` 已完成构建、原场景运行、保存恢复及独立包验证；SK 四动作分别 10/10、10/10、10/10、8/8 组件，CR 四动作均 9/9。历史普通怪物包为 `artifacts/d2x-ordinary-monsters-v18-20260923.mpq`，当前装备规则为 v24、保存格式 v7；旧怪物现场不可直接恢复。原专属 AI 和定向交互验收仍有缺口。

- 地图按类型逐项实现；按改动量分批构建、资源打包、短帧启动及截图验证，不编写测试用例、脚本或专用测试程序。
- 公共近战此前在 v16 规则下完成邪恶洞窟 900 帧运行、恢复后继续 300 帧；隔墙、动态绕行及控制状态的定向交互验收仍待人工完成。本轮新增类型复用该逻辑，不声称专属 AI 已完成。
- 当前 Windows Release 构建通过。种子 210 普通难度、种子 20260922 地狱难度的牛场、野外边界、河桥、道路及悬崖洞口已加载，未报告缺瓦片；石阵、艾尼弗斯树与塔入口完整性检查通过。种子 1 实际选中 TownSTrans2，营地双向出口关联通过。牛场原牛王身份被识别，仍使用敌对替身。
- 地图阶段独立资源包 `artifacts/d2x-act1-terrain-v15-20260923.mpq` 曾通过种子 20260922 地狱牛场启动。当前新增怪物需本节 v18 包或完整源 MPQ。39/39 目录地形及零已知缺资源的报告仅针对已实现生成分支，不覆盖尚未执行的通用 LvlSub 主题。纯河水预设不执行独立区域到达点初始化。
- 地图阶段 v15 存档恢复曾通过，当前 v24 规则及 v7 格式不兼容旧存档。历史地图截图保留于 artifacts，三帧启动和截图不代替交互验收；资源包、截图和存档均不提交。
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
