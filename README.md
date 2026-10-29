# D2X — Diablo II: Lord of Destruction

D2X 是用 C++20 重建《暗黑破坏神 II：毁灭之王》1.13c 客户端与游戏服务的开源项目，以还原原版规则和行为为目标，直接读取 MPQ 中的地图、图形、动画、音效和数据表。

同一客户端提供 Single Player、TCP/IP 和 Battle.net 入口。单机与局域网使用自研宿主，远端联机使用原 D2GS 协议；独立服务端与嵌入宿主复用同一玩法内核。资源加载、网络、玩法、画面表现和存档分别组织，便于对照原版、定位差异和持续完善。

**项目仍在开发中，玩法、画面与原版兼容性正在逐项对齐。** 当前能力与已知限制统一维护在[项目基线](BASELINE.md)，详细规则与实现见[文档目录](docs/README.md)。

项目也在推进 Project Diablo 2（PD2）适配。支持范围取决于具体资源、核心版本与连接方式，见[PD2 能力状态](docs/pd2/STATUS.md)和[版本说明](docs/pd2/BUILDS.md)。

## 快速开始

### 准备游戏资源

请自行提供合法持有的原版资料片 MPQ，资源应来自一致的安装版本。仓库和运行包不包含暴雪游戏素材。

将所需 MPQ 放入 `assets/mpq2/`，或在启动时通过 `-Mpq` 指定目录。文件清单及语音、音乐等资源的说明见[MPQ 资源](docs/resources/MPQ.md)。

### Windows 构建与启动

需要 CMake 3.25+、支持 C++20 的编译器、Git，以及支持 OpenGL 3.3 的图形驱动。首次配置会下载固定版本的依赖。在仓库根目录执行：

```powershell
.\scripts\build.ps1 -Configuration Release
.\Play.cmd -Mpq assets/mpq2
```

进入主菜单后选择游玩方式。联机配置、启动参数和打包方法见[构建与运行](docs/development/BUILD_AND_RUN.md)。PD2 使用 `PlayPD2.cmd`，需先按[PD2 运行与部署](docs/pd2/OPERATIONS.md)准备配套资源与配置。

Windows 已有实际构建与运行记录；Linux 保留 CMake 构建路径，尚未实际编译或运行验证，步骤见[Linux 构建说明](docs/development/BUILD_AND_RUN.md#linux-支持状态)。

## 文档导航

| 想了解的内容 | 入口 |
| --- | --- |
| 当前能力、限制与开发状态 | [项目基线](BASELINE.md) |
| 构建、启动、配置与打包 | [构建与运行](docs/development/BUILD_AND_RUN.md) |
| 独立服务端与 PvPGN 部署 | [服务端指南](docs/development/PVPGN_SERVER.md) |
| PD2 的使用、差异与版本边界 | [PD2 文档](docs/pd2/README.md) |
| 代码结构、模块职责与数据流 | [架构速览](docs/architecture/OVERVIEW.md) |
| 玩法规则、模块说明与原版依据 | [完整文档目录](docs/README.md) |

## 参与开发

开发前请阅读[协作约定](AGENTS.md)、[编码规范](docs/development/CODING.md)和[验证指南](docs/development/TESTING.md)。报告问题时，请说明资源版本、运行方式、复现步骤，以及预期与实际行为。

## 许可与致谢

源码采用 [GPL-3.0](LICENSE)。游戏素材的权利归各自权利人所有，不随源码许可授予。

项目使用并参考了多个开源项目，依赖、代码来源及各自许可见[资料来源与第三方许可](docs/resources/THIRD_PARTY.md)。
