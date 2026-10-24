# D2X — Diablo II: Lord of Destruction C++

用 C++20 重建《暗黑破坏神 II：毁灭之王》1.13c 的客户端与权威玩法内核，直接读取原 MPQ 中的地图、图形、动画、音效和规则表。

同一客户端支持 Single Player、TCP/IP 局域网和原 D2GS 联机。单机与局域网使用自研宿主；独立控制台服务端复用同一内核，可接入已有 PvPGN D2CS／D2DBS 后端。

[当前基线](BASELINE.md) · [文档目录](docs/README.md) · [代码架构](docs/architecture/OVERVIEW.md)

## 当前进展

| 领域 | 已实现范围 |
| --- | --- |
| 世界与任务 | 五幕原地图生成、碰撞、跨区旅行、传送点／门户，27 项个人任务主流程 |
| 人物与物品 | 成长、库存／装备／腰带、商店、掉落、方块加工、死亡／尸体与 D2S 保存 |
| 职业技能 | 女巫、亚马逊、圣骑士、死灵法师、野蛮人各 30 项，以及通用攻击／卷轴技能 |
| 怪物 | 第一幕 63 类型／25 类 AI；第二幕 74 身份／33 类 AI，含动画、30 项技能槽接线、召唤／复活及固有受击／死亡效果 |
| 伙伴 | 四类原佣兵、Hydra、诱饵／女武神、死灵召唤物与重生，原宠物头像／数量／生命条 |
| 多人 | 1–8 人名册、移动／聊天、组队、玩家交易、共享门户、附近经验与友方光环，游戏内命令分流、战网私聊与宿主虚拟人数 |

项目仍在开发中。七职业均已有技能程序接入，完整职业组合、第三至第五幕怪物行为、部分战斗机制与多人任务／PvP仍有缺口。当前能力与限制见[基线](BASELINE.md)；原版依据、当前实现和具体差异由对应专题维护，不以自研冒烟划分完成等级。

## 快速开始

运行需要自行提供完整的 1.13c 资料片原 MPQ，仓库和运行包不包含游戏素材。将其放在 `assets/mpq2/`，或通过 `-Mpq` 指定目录。

Windows 构建需要 CMake 3.25+、支持 C++20 的编译器和 Git；首次配置会下载固定版本的依赖。图形客户端需要支持 OpenGL 3.3 的驱动。在仓库根执行：

```powershell
.\scripts\build.ps1 -Configuration Release
.\Play.cmd -Mpq assets/mpq2
```

PD2 CN可用`PlayPD2.cmd`，需独立副本与当前固定版本资源；已支持登录、既有角色、大厅和基础罗格营地显示，玩法与扩展仍有限制，见[PD2入口](docs/development/BUILD_AND_RUN.md#pd2-cn远端账号大厅)。

`PlayPD2Local.cmd`使用同一PD2资源进入自有单机／TCP/IP城镇宿主，角色隔离保存在`saves/pd2`。当前只开放城镇与普通服务；采用原生PD2存档；野外战斗、多堆叠方块分配及高级加工仍有缺口，见[本地PD2入口](docs/development/BUILD_AND_RUN.md#pd2自有单机与tcpip城镇宿主)。

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
