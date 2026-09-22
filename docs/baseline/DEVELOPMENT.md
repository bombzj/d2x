# 开发与交接

## 当前交接状态

- 本轮只提交源码和文档，等待用户查看；不打包、不再执行构建或运行检查。
- 停止检查前已有 Windows 构建记录；默认种子 210 与地狱种子 20260922 的地图加载通过。
- 已观察：石块旷野地狱场景计划 207 个单位，附近创建 14 个、待生成 193 个，保存成功。
- 最终整理后的提交未追加验证。完整键鼠往返、最终代码的读档恢复和 Linux 运行仍待确认。
- 原始五个 MPQ 未改动。旧 `d2x-act1.mpq` 仍是 HUD 阶段的精简包，不含本轮全部生成模板。
- 当前启动优先完整 `assets/mpq2`；旧精简包和 `dist/` 压缩包不代表此基线。

## 后续获准时的构建入口

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
