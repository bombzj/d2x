# D2X — Diablo II: Lord of Destruction C++

用 C++20 重建《暗黑破坏神 II：毁灭之王》1.13c 的客户端与权威玩法内核，直接读取原 MPQ 中的地图、图形、动画、音效和规则表。

同一客户端支持 Single Player、TCP/IP 局域网和原 D2GS 联机。单机与局域网使用自研宿主；独立控制台服务端复用同一内核，可接入已有 PvPGN D2CS／D2DBS 后端。

[当前基线](BASELINE.md) · [文档目录](docs/README.md) · [代码架构](docs/architecture/OVERVIEW.md)

## 当前进展

| 领域 | 当前接入范围 |
| --- | --- |
| 世界与任务 | 五幕原地图生成、碰撞、跨区旅行、传送点／门户，27 项个人任务主流程 |
| 人物与物品 | 成长、库存／装备／腰带、商店、掉落、方块加工、死亡／尸体与 D2S 保存 |
| 职业技能 | 七职业均有技能程序接入，含刺客武学／陷阱／影子、德鲁伊元素／变形／召唤，以及通用攻击／卷轴技能 |
| 怪物 | 第一、第二幕家族及第三至第五幕逐项 AI／首领程序，含动作、技能、召唤／复活与受击／死亡效果；完整覆盖以专题台账为准 |
| 伙伴 | 四类原佣兵、Hydra、诱饵／女武神、死灵召唤物与重生、刺客影子／陷阱及德鲁伊召唤物，复用实体与归属机制 |
| 多人 | 1–8 人名册、移动／聊天、组队、玩家交易、共享门户、附近经验与友方光环，游戏内命令分流、战网私聊与宿主虚拟人数 |
| 按键设置 | ESC → Options → Configure Controls，主／副键编辑、默认／取消／应用及全角色共享保存，支持16个技能快捷槽；技能循环与角色语音键仍禁用，操作和保存边界见[界面说明](docs/gameplay/ui/CLASSIC_HUD.md#esc-菜单) |

项目仍在开发中。七职业均已有技能程序接入，完整职业组合、第三至第五幕怪物行为、部分战斗机制与多人任务／PvP仍有缺口。当前能力与限制见[基线](BASELINE.md)；原版依据、当前实现和具体差异由对应专题维护，不以自研冒烟划分完成等级。

## PD2 支持

通过 [PlayPD2.cmd](PlayPD2.cmd) 使用同一客户端与主菜单，需自行提供配套原版与 PD2 资源。Single Player／TCP/IP 使用自研宿主，已接入七职业差异、物品与加工、普通地图及部分特殊场景程序；本地角色默认保存在独立的 `saves/pd2`，使用原生 PD2 D2S。Battle.net 可按配置连接已适配的 CN 服务，已有登录、入局、城镇及部分物品操作的有限互通。

PD2 仍属开发中的支持：完整技能／装备／地图组合、原生时序及多人兼容尚未全部对齐；共享／材料仓库不在本地宿主范围，独立 PD2 GS 接入 PvPGN 不在当前支持／验收范围。CN 专属适配不代表所有 PD2 版本通用兼容。详见[能力状态](docs/pd2/STATUS.md)、[运行与配置](docs/pd2/OPERATIONS.md)和[版本与证据](docs/pd2/BUILDS.md)。

## 快速开始

运行需要自行提供完整的 1.13c 资料片原 MPQ，仓库和运行包不包含游戏素材。将其放在 `assets/mpq2/`，或通过 `-Mpq` 指定目录。

Windows 构建需要 CMake 3.25+、支持 C++20 的编译器和 Git；首次配置会下载固定版本的依赖。图形客户端需要支持 OpenGL 3.3 的驱动。在仓库根执行：

```powershell
.\scripts\build.ps1 -Configuration Release
.\Play.cmd -Mpq assets/mpq2
```

PD2 首次资源配置及启动方法见[PD2运行与部署](docs/pd2/OPERATIONS.md#运行与部署统一入口)。

进入主菜单后选择 Single Player、TCP/IP 或 Battle.net。单机／局域网使用本地角色；原服角色由原服保存。联机配置、启动参数及 Linux 构建步骤见[构建与运行](docs/development/BUILD_AND_RUN.md)。Linux 保留构建路径，尚未实际编译或运行验证。

## 游戏内聊天与命令

游戏内Enter输入聊天；`!文字`发送头顶消息，目前尚未绘制气泡；`@角色名 文字`为房间私聊。`/w`（或`/m`、`/msg`、`/whisper`）优先匹配房间角色，`/w *账号 文字`走战网私聊。其余斜杠命令在连接Battle.net时转发账号服务，单机／TCP/IP提示不可用。

`/nopickup`切换防误拾取，开启后按住Alt拾取；`/players N`（1–8）仅单机或TCP/IP本机宿主可用，改变后续敌怪及掉落的人数规则，本局设置不保存。`/fps`、`/framerate`、`soundchaosdebug`暂未实现。准确范围见[命令分流](docs/modules/NETWORK.md#游戏内命令分流)。

## 本地打包

完成构建后执行：

```powershell
.\scripts\package.ps1
.\scripts\package-server.ps1
```

| 包 | 程序与目录 | 用途 |
| --- | --- | --- |
| 客户端 | `dist/current/d2x.exe`，入口 `Play.cmd` | 图形客户端，含单机／局域网嵌入宿主 |
| 独立服务端 | `dist/server/d2x_server.exe` | 无图形 D2GS，连接 PvPGN 后端 |

打包保留已有存档和配置，不包含 MPQ。独立服务端仍需 bnetd、D2CS 和 D2DBS，当前不能视为完整原 D2GS 或生产部署替代；配置与启停方法见[PvPGN 服务端](docs/development/PVPGN_SERVER.md)。

## 参与开发与许可

架构将资源／协议适配、权威玩法、客户端表现和存储分开；玩法内核不读取 MPQ、设备输入或 GPU。开发前阅读[协作约定](AGENTS.md)、[编码规范](docs/development/CODING.md)和[验证指南](docs/development/TESTING.md)。

源码采用 [GPL-3.0](LICENSE)。暴雪游戏素材权利独立，参考项目与第三方许可见[资料来源](docs/resources/THIRD_PARTY.md)。
