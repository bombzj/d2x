# D2X — Diablo II: Lord of Destruction C++

基于《毁灭之王》原始 MPQ 的 C++20 项目，开发目标已改为全面支持既有 PvPGN／D2CS／D2GS 联机，放弃单机兼容；所有 UI 服务联机，任何菜单或面板都不应暂停游戏。完整路线见 [D2GS 全面联机开发计划](docs/architecture/MULTIPLAYER.md)。当前源码已改为联机唯一入口，并开始独立网络服务、测试表现暂停与快捷入局；本批已 Windows Release 构建并更新运行包，测试暂停、库存 UI 和保存重入等有限原服证据见基线；完整计划仍未完成。联网认证只读原版客户端文件，不使用重制版资源，资料片是唯一运行目标。

[文档目录](docs/README.md) · [当前基线](BASELINE.md) · [代码架构](docs/architecture/OVERVIEW.md) · [协作约定](AGENTS.md)

## 当前范围

项目已有原生地图、MPQ 原图／原表、客户端 UI 契约和本地玩法成果；可复用部分将服务于联机客户端，本地技能／任务／库存实现不计作联机完成度。

源码与 `dist/current` 已接局前流程、五幕地图、部分移动／物品／战斗／成长和共用 UI，并有有限连服及保存重入证据；完整鼠标行走、所有 UI 操作、任务／佣兵、复杂技能和多人功能仍未完成。配置、流程和准确范围见 [联网模块](docs/modules/NETWORK.md)，源码／包差异见 [BASELINE.md](BASELINE.md)。

## 构建与运行

需要当前支持的原始 MPQ。本机资源位于 `assets/mpq2`；也可通过 `--mpq <目录>` 指定，或把完整原包放在 EXE 同目录。搜索顺序及各包职责见 [开发指南](docs/development/BUILD_AND_RUN.md) 和 [资源清单](docs/resources/MPQ.md)。

Windows 在项目根目录执行：

```powershell
.\scripts\build.ps1 -Configuration Release
.\Play.cmd
```

当前源码普通启动先显示主菜单，由 Battle.net 进入账号登录和服务器选角。本地角色／D2S／地图种子等产品参数已移除并明确拒绝。先通过 UI 登录一次后，可用 `Play.cmd -OnlinePlay <角色名>` 自动创建随机命名的普通房间，或用 `Play.cmd -OnlineCharacter <角色名> -OnlineCreateGame <房间名>` 自动执行正常登录到建房流程；已有房间用 `-OnlineJoinGame`。快捷入局仅尝试一次，不跳过原服认证或取票。

`scripts/package.ps1` 更新固定 `dist/current`，只复制已构建程序、脚本和文档，不复制 MPQ。在仓库根可用 `dist/current/Play.cmd -Mpq assets/mpq2` 启动已有包；保存与截图归包目录。构建、打包、Linux 依赖及详细参数见 [开发指南](docs/development/BUILD_AND_RUN.md)。Linux 尚未实际编译或运行。

## 操作

| 输入 | 功能 |
| --- | --- |
| 左键／方向键 | 寻路／辅助移动；野外边界直接跨区，洞口和楼梯点击进入 |
| 左键点敌人／物品／NPC | 单次攻击／走近拾取／交互；按住怪物持续攻击并锁定该目标 |
| 右键按住怪物／空地 | 持续使用右技能；怪物目标不随鼠标移开改变，空地施法跟随朝向 |
| NPC Trade 窗口 | 左键拿起物品后投向货物区出售；右键仍执行普通库存操作；修理模式左键修理 |
| 左／右技能菜单悬停 + F1–F8 | 绑定对应鼠标键技能；单按 F1–F8 切换技能，不立即施法 |
| 点击左右技能槽 | 展开当前职业已学技能菜单；Shift 左键使用左键技能 |
| I、A／C、S／T、1–4、B、Ctrl+F4 | 包裹、角色面板、技能树、饮药、展开腰带、走近私人箱 |
| O | 佣兵属性和装备面板，需已有佣兵 |
| W／背包 I–II 标签 | 切换两组武器 |
| Alt、Tab、R、按住 Ctrl | 物品名称、地图、走跑切换、临时跑步 |
| Esc | 先关闭面板／对话，无面板时打开游戏菜单；Save and Exit Game 保存并返回角色列表 |
| Ctrl+F1、Ctrl+F3、F12 | 帮助、碰撞网格、截图 |


地图种子、人物与持久保存来自原服。ESC 只关闭面板或打开菜单，Save and Exit 请求原服保存退局。显式调试管道的 `pause/resume` 只冻结客户端表现，网络与服务器继续；不提供原服单步。命令及源码／包差异见[调试管道](docs/development/DEBUG_PIPE.md)与[基线](BASELINE.md)。

源码使用 GPL-3.0，暴雪素材权利独立；参考代码及依赖许可见 [资料来源](docs/resources/THIRD_PARTY.md)。
