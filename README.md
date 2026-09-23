# D2X — Diablo II Classic C++

基于经典 MPQ 的单机 C++20 项目：地图、行走、技能、物品容器和存档。不依赖原版 EXE，不使用重制版资源。

**总目录：[BASELINE.md](BASELINE.md)。协作 agent 先读 [AGENTS.md](AGENTS.md)。**

## 当前范围

- 第一幕 39 项目录均有地形入口；主线和支线双向连接状态与限制见能力基线。
- MPQ DS1/DT1 地形和碰撞；预设、迷宫、野外类型均已接入。道路、河桥、悬崖与神殿的剩余限制见地图文档。
- 七职业 COF/DCC 人物及装备外观、MPQ 成长和共用四维加点；A* 与 WASD、六种演示技能；MPQ 经典底栏与角色面板。
- 怪物按原表生成计划，附近房间成组创建，远处休眠；未实现类型保留真实身份并使用沉沦魔替身。
- 物品原表、TC 掉落、拾取、包裹、腰带、药剂和私人箱；各品质装备可查看与穿戴，首批明确的直接属性已参与派生，其余效果待核实。
- v17 会话存档，包含职业身份、等级经验、属性分配、地图、怪物及物品；旧档不迁移。

完整限制见 [能力基线](docs/baseline/STATUS.md)。

## 构建与资源

Windows 使用 `scripts/build.ps1`，启动示例（默认读取 `assets/mpq2`）：

```powershell
.\build\bin\d2x.exe --level 1 --map-seed 210
```

本机可用 `Play.cmd`。EXE 默认直接读取完整 `assets/mpq2`；原始 MPQ 保留。其他位置可显式传 `--mpq <目录>`。运行目录在 `dist/d2x-character-attributes-runtime-20260923/`，不制作 ZIP。

Windows Release 构建、完整 MPQ 下七职业外观、角色面板截图、升级加点和 v17 存读档冒烟已通过；Linux 使用原生 CMake，尚未实际编译运行。见 [构建与分发](docs/BUILD_AND_SHARE.md)。

## 操作

| 输入 | 功能 |
| --- | --- |
| 左键／WASD | 寻路／移动；野外边界直接跨区，洞口和楼梯点击进入 |
| 左键点敌人／物品／NPC | 攻击／走近拾取／交互 |
| 右键、F5–F10 | 当前右键技能、选择技能 |
| 点击左右技能槽 | 展开菜单；Shift 左键释放左技能 |
| I、C、1–4、B、F4 | 包裹、角色面板、饮药、展开腰带、走近私人箱 |
| Alt、Tab、空格 | 物品名称、地图、走跑切换 |
| F2、PgUp／PgDn | 开发地图目录、翻页 |
| F11、Ctrl+F11 | 保存、读取；默认 `saves/quick.d2xsave` |
| P、M、R | 暂停、静音、重置当前区 |
| F1、F3、F12 | 帮助、碰撞网格、截图 |
| Ctrl+Alt+G／E／A／T／C | 调试：加金币／经验、重置属性点、天赋重置预留、切换职业 |

参数包括 `--level`、`--map-seed`、`--difficulty normal|nightmare|hell`、`--population-seed`、`--save/--load`。`--seed` 仅控制预留掉落随机状态；模板查看使用 `--preset <Def> --level-type <ID>`。

源码使用 GPL-3.0，暴雪素材权利独立，见 [第三方说明](docs/THIRD_PARTY.md)。
