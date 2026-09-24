# D2X — Diablo II Classic C++

基于经典 MPQ 的单机 C++20 项目：地图、行走、技能、物品容器和存档。不依赖原版 EXE，不使用重制版资源。

**总目录：[BASELINE.md](BASELINE.md)。协作 agent 先读 [AGENTS.md](AGENTS.md)。**

## 当前范围

- 第一幕 39 项目录均有地形入口；主线和支线双向连接状态与限制见能力基线。
- MPQ DS1/DT1 地形和碰撞；预设、迷宫、野外类型均已接入。道路、河桥、悬崖与神殿的剩余限制见地图文档。
- 七职业 COF/DCC 人物及装备外观、MPQ 成长和共用四维加点；七职业各 30 个 MPQ 技能树节点与技能点分配；A* 与 WASD、女巫传送／火弹／火球／冰霜新星／静电力场及其余职业的既有演示技能；MPQ 经典底栏与角色面板。
- 怪物按原表生成计划，附近房间成组创建，远处休眠；未实现类型保留真实身份并使用沉沦魔替身。
- 物品原表、TC 掉落、拾取、包裹、腰带、药剂和私人箱；各品质装备可查看与穿戴，首批明确的直接属性已参与派生，其余效果待核实。
- 会话存档包含职业身份、等级经验、属性与技能分配、F1–F8 技能绑定、初始装备授予技能、地图、怪物 AI／攻击模式状态及物品；当前格式见[存档说明](docs/SAVES.md)，旧档不迁移。玩家击杀怪物会按 MPQ 数据取得经验；资料片私人箱为 6×8 格。

完整限制见 [能力基线](docs/baseline/STATUS.md)。

## 构建与资源

Windows 使用 `scripts/build.ps1`，启动示例（默认读取 `assets/mpq2`）：

```powershell
.\build\bin\d2x.exe --level 1 --map-seed 210
```

本机可用 `Play.cmd`。EXE 默认直接读取完整 `assets/mpq2`；其他位置可显式传 `--mpq <目录>`。

当前 Windows Release 构建、完整六 MPQ 运行目录与 Hell Clan 外观截图、存读档冒烟已通过；Linux 尚未实际编译运行。见 [构建与运行](docs/BUILD_AND_SHARE.md) 和 [开发基线](docs/baseline/DEVELOPMENT.md)。

## 操作

| 输入 | 功能 |
| --- | --- |
| 左键／WASD | 寻路／移动；野外边界直接跨区，洞口和楼梯点击进入 |
| 左键点敌人／物品／NPC | 攻击／走近拾取／交互 |
| 左／右技能菜单悬停 + F1–F8 | 绑定对应鼠标键技能；单按 F1–F8 切换技能，不立即施法 |
| 点击左右技能槽 | 展开当前职业已学技能菜单；Shift 左键使用左键技能 |
| I、C、T、1–4、B、Ctrl+F4 | 包裹、角色面板、技能树、饮药、展开腰带、走近私人箱 |
| Alt、Tab、空格 | 物品名称、地图、走跑切换 |
| Ctrl+F2、PgUp／PgDn | 开发地图目录、翻页 |
| F11、Ctrl+F11 | 保存、读取；默认 `saves/quick.d2xsave` |
| P、M、R | 暂停、静音、重置当前区 |
| Ctrl+F1、Ctrl+F3、F12 | 帮助、碰撞网格、截图 |
| Ctrl+Alt+G／E／A／T／C | 调试：加金币／经验、重置属性点、重置技能点、切换职业 |

参数包括 `--level`、`--map-seed`、`--difficulty normal|nightmare|hell`、`--population-seed`、`--save/--load`。`--seed` 仅控制预留掉落随机状态；模板查看使用 `--preset <Def> --level-type <ID>`。

源码使用 GPL-3.0，暴雪素材权利独立，见 [第三方说明](docs/THIRD_PARTY.md)。
