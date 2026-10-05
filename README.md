# D2X — Diablo II: Lord of Destruction C++

基于《毁灭之王》原始 MPQ 的 C++20 项目，包含单机地图、战斗、技能、物品容器、五幕任务与原 D2S v96 存档。单机不依赖原版 EXE；联网认证只读原版客户端文件。不使用重制版资源，资料片是唯一运行目标。

[文档目录](docs/README.md) · [当前基线](BASELINE.md) · [代码架构](docs/architecture/OVERVIEW.md) · [协作约定](AGENTS.md)

## 当前范围

人物、库存、角色／技能、NPC／任务、商店／佣兵及地图界面已有客户端契约和本地适配；玩法以组合、公共能力和独立领域规则组织。五幕任务已有主要流程，部分职业技能、怪物 AI、物件和装备效果仍有缺口，准确范围见各模块／玩法专题。

源码与 `dist/current` 已接主菜单→登录／注册→Realm→服务器建角／选角→创建或加入房间→第一幕营地显示及移动。Windows Release 与双账号互见／移动、正常退局／角色重入有限冒烟通过；联网出城、战斗、拾取和完整库存尚未接入。配置、流程和准确范围见 [联网模块](docs/modules/NETWORK.md)，当前基线见 [BASELINE.md](BASELINE.md)。

## 构建与运行

需要当前支持的原始 MPQ。本机资源位于 `assets/mpq2`；也可通过 `--mpq <目录>` 指定，或把完整原包放在 EXE 同目录。搜索顺序及各包职责见 [开发指南](docs/development/BUILD_AND_RUN.md) 和 [资源清单](docs/resources/MPQ.md)。

Windows 在项目根目录执行：

```powershell
.\scripts\build.ps1 -Configuration Release
.\Play.cmd
```

当前源码普通启动先显示主菜单：Single Player 进入已有本地角色列表，Battle.net 进入账号登录。可直接运行 `build/bin/d2x.exe`；显式 `--class Sorceress --level 8` 创建临时单机角色直入场景，未指定保存路径时退出不自动保存。

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
| F11、Ctrl+F11 | 保存、读取；默认 `saves/quick.d2s` |
| Esc | 先关闭面板／对话，无面板时打开游戏菜单；Save and Exit Game 保存并返回角色列表 |
| Ctrl+F1、Ctrl+F3、F12 | 帮助、碰撞网格、截图 |
| Ctrl+Alt+G／E／A／T／W | 调试：加金币／经验、重置属性点、重置技能点、激活当前 MPQ 中已构建区域的传送点 |
| Ctrl+Alt+B | 在脚边掉落一件赫拉迪克方块；已有方块时不会重复生成 |


地图种子可用 `--seed` 固定整局，或 `--map-seed / --population-seed` 单独指定；难度为 `--difficulty normal|nightmare|hell`。加载 D2S 后从城镇建立新局，原地图探索由同名 `.d2xmap` 保存。格式限制见 [存档](docs/modules/SAVES.md)，开发参数和命令见 [调试管道](docs/development/DEBUG_PIPE.md)。

源码使用 GPL-3.0，暴雪素材权利独立；参考代码及依赖许可见 [资料来源](docs/resources/THIRD_PARTY.md)。
