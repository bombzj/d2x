# D2X — Diablo II Classic C++

基于经典 MPQ 的单机 C++20 项目：地图、行走、技能、物品容器和存档。不依赖原版 EXE，不使用重制版资源。

**总目录：[BASELINE.md](BASELINE.md)。协作 agent 先读 [AGENTS.md](AGENTS.md)。**

## 当前范围

- 第一幕 39 项目录、31 项地形；营地至修道院大门及沿途洞穴、墓地、遗忘之塔，共 26 个区域双向连接。
- MPQ DS1/DT1 地形和碰撞；Cave/Crypt 迷宫、矩形野外边界及原预设。完整道路、河桥、悬崖和 LvlSub 待完善。
- 野蛮人 COF/DCC、A* 与 WASD、六种演示技能；MPQ 经典底栏、生命／法力球、左右技能槽和展开式菜单。
- 怪物按原表生成计划，附近房间成组创建，远处休眠；未实现类型保留真实身份并使用沉沦魔替身。
- 物品原表、TC 掉落、拾取、包裹、腰带、药剂、私人箱；魔法、稀有、套装、暗金、优质及劣质装备可展示原属性并穿戴，属性战斗效果留待战斗系统。
- v12 会话存档，包含地图、怪物、物品及同局限量暗金状态；旧档不迁移。

完整限制见 [能力基线](docs/baseline/STATUS.md)。

## 构建与资源

Windows 使用 `scripts/build.ps1`，启动示例（默认读取 `assets/mpq2`）：

```powershell
.\build\bin\d2x.exe --level 1 --map-seed 210
```

本机可用 `Play.cmd`。EXE 默认直接读取完整 `assets/mpq2`；原始 MPQ 保留。其他位置可显式传 `--mpq <目录>`。本轮不打包。

Windows Release 构建、完整 MPQ 的短帧新游戏和 v12 读档冒烟已通过；Linux 使用原生 CMake，尚未实际编译运行。见 [构建与分发](docs/BUILD_AND_SHARE.md)。

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
