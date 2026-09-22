# D2X — Diablo II Classic C++

基于经典 MPQ 的单机 C++20 项目：地图、行走、技能、物品容器和存档。不依赖原版 EXE，不使用重制版资源。

**总目录：[BASELINE.md](BASELINE.md)。协作 agent 先读 [AGENTS.md](AGENTS.md)。**

## 当前范围

- 第一幕 39 项目录、31 项地形；营地至修道院大门及沿途洞穴、墓地、遗忘之塔，共 26 个区域双向连接。
- MPQ DS1/DT1 地形和碰撞；Cave/Crypt 迷宫、矩形野外边界及原预设。完整道路、河桥、悬崖和 LvlSub 待完善。
- 野蛮人 COF/DCC、A* 与 WASD、六种演示技能；MPQ 经典底栏、生命／法力球、左右技能槽和展开式菜单。
- 怪物按原表生成计划，附近房间成组创建，远处休眠；未实现类型保留真实身份并使用沉沦魔替身。
- 物品原表、拾取、包裹、腰带、药剂、私人箱；原版掉落执行与完整装备效果尚未接入。
- v3 会话存档，包含地图种子、区域状态、待生成怪物和物品。旧 v1/v2 不兼容。

完整限制见 [能力基线](docs/baseline/STATUS.md)。

## 构建与资源

获准继续构建后，Windows 使用 `scripts/build.ps1`，启动示例：

```powershell
.\build\bin\d2x.exe --mpq assets/mpq2 --level 1 --map-seed 210
```

本机可用 `Play.cmd`。启动优先完整 `assets/mpq2`；原始五个 MPQ 保留。旧精简包缺少本轮地图模板，本轮不重新打包。

Windows 已有构建运行记录；Linux 使用原生 CMake，尚未实际编译运行。见 [构建与分发](docs/BUILD_AND_SHARE.md)。

## 操作

| 输入 | 功能 |
| --- | --- |
| 左键／WASD | 寻路／移动；野外边界直接跨区，洞口和楼梯点击进入 |
| 左键点敌人／物品／NPC | 攻击／走近拾取／交互 |
| 右键、F5–F10 | 当前右键技能、选择技能 |
| 点击左右技能槽 | 展开菜单；Shift 左键释放左技能 |
| I、1–4、B、F4 | 包裹、饮药、展开腰带、走近私人箱 |
| Alt、Tab、空格 | 物品名称、地图、走跑切换 |
| F2、PgUp／PgDn | 开发地图目录、翻页 |
| F11、Ctrl+F11 | 保存、读取；默认 `saves/quick.d2xsave` |
| P、M、R | 暂停、静音、重置当前区 |
| F1、F3、F12 | 帮助、碰撞网格、截图 |

参数包括 `--level`、`--map-seed`、`--difficulty normal|nightmare|hell`、`--population-seed`、`--save/--load`。`--seed` 仅控制预留掉落随机状态；模板查看使用 `--preset <Def> --level-type <ID>`。

源码使用 GPL-3.0，暴雪素材权利独立，见 [第三方说明](docs/THIRD_PARTY.md)。
