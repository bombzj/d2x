# D2X — Diablo II Classic C++ MVP

使用 C++20 重现 Diablo II 经典版的等距地图、角色动作和技能战斗。原版画面从 MPQ 中实时解码，运行不依赖 Diablo II 的 EXE，也不需要安装重制版。

**本机直接双击 `Play.cmd`。** 默认优先读取约 13.06 MiB 的 `assets/mpq2/d2x-act1.mpq`，也可直接使用用户提供的完整 `assets/mpq2`。原始 MPQ 保留，试玩与完整包不混用。

## 已实现

- 第一幕 39 个原表关卡的目录，13 个完整预设地形可进入：营地、四处洞穴二层、塔入口／地窖五层、修道院大门、内外回廊、大教堂、地下墓穴四层和崔斯特瑞姆。三个旧模板单独标识，F2 分页选择。随机迷宫、户外生成及关卡连接尚未完成，见 [第一幕地图](docs/ACT1_MAPS.md)。
- MPQ 六张原表 → WorldCatalog → MapRecipe → DS1/DT1；按 LevelType 与 Dt1Mask 选瓦片库，共享解码，区分隐藏与碰撞，保留 DS1 替换标签及对象 flags。地形没有使用第三方 JSON。
- 每格地图的 5×5 子格碰撞，鼠标 A* 寻路与路径平滑，WASD 行走，走/跑切换和耐力。
- 野蛮人 16 方向、9 层 COF/DCC 合成，站立、行走、奔跑、攻击、施法、受击和死亡动作。
- 按 DS1 原坐标放置 NPC、鸡、牛、篝火、火把、旗帜、墓地物件、私人储物箱和传送点。阿卡拉可恢复生命/法力；瓦瑞夫和传送点打开旅行菜单。
- MPQ 区域怪物名单、难度、密度、群组、随从和 DS1 固定首领已接入；未实现怪物暂用沉沦魔替身并保存真实身份。安达利尔、女伯爵、格里斯瓦德可按原数据出现。当前位置使用 DS1 场景适配，原版 DRLG 分区尚未完成，见 [怪物生成](docs/MONSTER_POPULATION.md)。
- 六种技能：火球、冰霜新星、旋风斩、传送、跳跃攻击、战吼；包含弹道、范围命中、法力消耗、冷却和视觉反馈。
- 经典版施法音效、挥砍声、脚步声，原版字体、手形光标和生命/法力球装饰；小地图、自动地图、技能说明、暂停和截图。
- 原版物品落地动画、名称、点击走近拾取、兼容堆叠自动合并和容量不足提示。未经核实的自定怪物掉落已停用，等待原版生成规则接入。
- 4／8／12／16 格腰带、自动入带、同列补位、13 种药剂；原版腰带、完整包裹面板、药水图标与音效。初始带四瓶生命药剂，右键饮用或按 1–4。
- 私人储物箱：原地图实体、原版 bank.dc6 面板、6×4 格、自动走近、跨容器拖放／交换／堆叠和整件存取。F4 在营地走向箱子，Shift 点击存入／取出。
- 完整包运行时导入 659 条物品、734 条怪物和 852 条 TreasureClassEx，保留类型、基础参数、Picks／NoDrop／分支权重；旧试玩适配器仍支持 361／410／95 条记录。独立物品实例、10×4 包裹和通用容器规则支持拖动、交换、合并、拆分、丢弃。当前加载 34 种物品美术，不代表全部装备效果已实现。
- 带版本、资源指纹和校验的会话存档，保留玩家、区域、物品、ID、随机数及死亡结算；保存覆盖时保留上一份备份。

## 操作

| 输入 | 功能 |
| --- | --- |
| 左键点击/按住 | 移动；点敌人追击；点 NPC 交互；点击物品/名称走近拾取 |
| 按住 Alt | 显示屏幕内的地面物品名称；悬停也可显示单件名称 |
| I / 点击 HUD 包裹计数 | 打开/关闭包裹；悬停查看物品详情 |
| 包裹内左键拖动 | 移动、交换；同类型堆叠默认合并，Ctrl 拖动改为交换 |
| Shift 点击 / AUTO PLACE | 箱子打开时 Shift 存入／取出；否则药水入带、腰带物品入包或穿脱腰带；AUTO PLACE 自动入包；SPLIT 拆分，DROP 丢弃 |
| WASD | 按屏幕方向移动 |
| 右键按住 | 释放当前选中技能 |
| 1–4 / B | 饮用对应列药水 / 展开或收起腰带 |
| F5–F10 | 向光标处释放对应技能 |
| 包裹／腰带中右键 | 饮药；腰带装备则穿戴／脱下；拖动过程中取消手势 |
| 点击技能格 | 选择右键技能；悬停查看说明 |
| 空格 | 切换行走/奔跑 |
| F2 / PgUp / PgDn | 地图目录／上一页／下一页；点击可用地形进入 |
| F4 | 走近当前地图的私人储物箱；已打开时关闭 |
| Tab / F1 | 自动地图 / 帮助 |
| M / P | 静音 / 暂停 |
| R | 恢复角色并按当前种子重新生成本区怪物 |
| F11 / Ctrl+F11 | 保存 / 读取会话，默认 `saves/quick.d2xsave` |
| F3 / F12 | 碰撞网格 / 保存截图到 `artifacts` |
| Esc | 先取消包裹手势/关闭面板，再关闭其他菜单或退出 |

按 **F2** 选择可用地图，或用 `--level 38` 直接进入崔斯特瑞姆。敌人来自原表和固定放置点，尚未实现的类型暂用沉沦魔；悬停可看真实身份。掉落执行链仍待接入，可丢弃初始物品后按住 **Alt** 查看并拾取。旅行保留角色与区域状态，F11 保存、Ctrl+F11 恢复。本轮新增刷怪身份，存档格式升至版本 2，旧存档明确拒绝，见 [存档](docs/SAVES.md)。

拾取后按 **I** 整理包裹；拖动时绿色/红色显示放置是否可行。右键或 `Esc` 取消手势，拖到左侧世界画面会丢到人物脚边，拖到腰带格可放入或交换，HUD 其他区域只取消。包裹打开时世界继续运行，`P` 可暂停；详细操作见 [包裹界面](docs/INVENTORY_UI.md)。

## 构建和资源

```powershell
.\scripts\build.ps1
.\scripts\fetch-demo.ps1
.\scripts\play.ps1
```

Windows 需要 CMake 3.25+ 和 C++20 编译器。脚本支持当前环境的 MinGW + Ninja，也可使用 Visual Studio 的 CMake 生成器。raylib 5.5 和 StormLib 会从固定版本的官方 GitHub 仓库获取；已有 `external` 源码时直接使用本地副本。

Linux 使用原生 CMake 构建，不使用 `Play.cmd` 或 PowerShell 脚本。系统依赖、完整命令和发给别人时的最小文件清单见 [Linux 与源码分发](docs/BUILD_AND_SHARE.md)。目前已在 Windows 编译运行；Linux 已检查源码与依赖支持，尚未在 Linux 环境实际编译运行。

`fetch-demo.ps1` 从经典 1.04 试玩包的存档镜像按 HTTP 字节范围读取四个 MPQ，总下载约 74 MiB。逐个核对 SHA-256，不下载音乐/过场，不执行安装器。程序加载全部场景所需素材后生成 `assets/mpq/d2x-mvp.mpq`，默认清理临时的源 MPQ。`-KeepSource` 可以保留源包。

也可指定版本一致的经典完整版／资料片 MPQ 目录；当前已接入用户提供的资料片命名字段表：

```powershell
.\scripts\play.ps1 -Mpq 'D:\Games\Diablo II'
```

补丁优先于资料片和基础包。完整原始资源已解决当前已知的第一幕文件缺项，暂无需继续下载；随机区域仍需生成代码。`fetch-demo.ps1` 仅作为旧试玩资源获取途径保留。

## 资源工具

```powershell
.\build\bin\d2x_assets.exe assets/mpq2 maps 37
.\build\bin\d2x_assets.exe assets/mpq2 item hax
.\build\bin\d2x_assets.exe assets/mpq2 drops fallen1
.\build\bin\d2x_assets.exe assets/mpq2 population 25 normal 210
.\build\bin\d2x_assets.exe assets/mpq list '*.dcc'
.\build\bin\d2x_assets.exe assets/mpq extract 'data/global/palette/act1/pal.dat' palette.dat
.\build\bin\d2x_assets.exe assets/mpq preview 'data/global/chars/ba/hd/bahdlitnuhth.dcc' head.png
```

`preview` 导出最多前 16 帧的图集。无内部文件名表的原始 MPQ 可以按已知资源路径读取；本项目生成的精简 MPQ 自带 listfile。

程序支持 `--level <原版ID>`、`--variant 0..5`、`--maps`、`--preset <Def> --level-type <ID>`，以及 `--mpq`、`--map`、`--seed`、`--inventory`、`--stash`、`--screenshot`、`--frames`、`--hidden`、`--pack`、`--save/--load`。新增 `--difficulty normal|nightmare|hell` 和 `--population-seed <uint32>` 控制刷怪；`--seed` 仍仅关联待实现的掉落随机状态。`--pack` 收录地图变体、物件动画和本轮怪物原表。没有新增测试脚本或测试用例。

## 重构后的扩展入口

代码按 `core`、`resources`、`content`、`world`、`gameplay`、`persistence`、`presentation`、`app` 分层。玩法状态由会话统一管理，输入提交命令，死亡/施法/交互产生事件；HUD 只读取状态。技能和怪物参数集中在 `src/gameplay/definitions.cpp`。

物品、容器、包裹、腰带、私人储物箱及第 6 项存档均已接入。原版物品数据入口在 `content/classic_data.cpp`，快照校验在 `gameplay/session_snapshot.cpp`，文件格式与写入在 `persistence`。怪物掉落生成及地图箱子仍等待对应版本的原版算法，不再使用自定权重补齐。参见 [原版物品数据](docs/ITEM_DATA.md)、[存档](docs/SAVES.md)、[架构](docs/ARCHITECTURE.md) 和 [阶段计划](docs/ITEMS_NEXT.md)。

## 当前边界

这是一套可玩的 MVP 和继续扩展的引擎基础。角色/地图像素取自经典版；战斗数值、怪物 AI、技能时间、部分粒子和跳跃姿势采用本项目模拟规则。野蛮人可以在技能演示中使用法师技能，尚未按职业技能树限制。

原版随机迷宫、户外布置、出入口连接、全剧情任务与联网尚未实现。TreasureClass 执行、金币、词缀及完整装备属性仍待接入。私人箱继续使用原版 Bank Page 1 小箱布局，资料片大箱页尚未启用。洞穴隐藏空白格已按 Blank.dt1 处理；真正缺少资源时会明确报错。

本轮源码包为 `dist/d2x-source-population-20260922.zip`。源码导览见 [架构](docs/ARCHITECTURE.md)，资源和项目来源见 [第三方说明](docs/THIRD_PARTY.md)。代码遵循 GPL-3.0；暴雪素材的权利独立于代码许可，不随源码提交到 Git。

按最新要求，后续不以自制素材或未经核实的原版规则补齐缺口；已存在的简化实现会明确保留边界说明。需要的完整经典 MPQ、寻找方法和当前资源核对结果见 [MPQ 资源](docs/MPQ_RESOURCES.md)。
