# 第一幕地图

当前：39 项目录、31 项地形；Levels 1–26 组成连续探索路线。地图来自 MPQ 的 DS1/DT1，没有第三方地图 JSON。

## 连续路线

`罗格营地 → 血腥荒地 → 冰冷之原 → 石块旷野 → 地下通道一层 → 黑暗森林 → 黑色荒地 → 泰摩高地 → 修道院大门`

支线：邪恶洞窟；洞窟、地洞、深坑一／二层；地下通道二层；埋骨之地及两座墓穴；遗忘之塔入口及地窖一至五层。

27、32、33、37、38 的完整预设仍可独立查看，尚未接入主线。28–31、34–36、39 的生成器未实现。

野外通过边界行走；洞口／楼梯按原 LvlWarp 选择区域点击进入。返回保留区域状态。F2 是开发目录，不是原版传送点系统。

## 数据与代码

| 输入／模块 | 职责 |
| --- | --- |
| Levels、LvlPrest、LvlTypes | 身份、生成类型、尺寸、DS1 槽位、DT1 掩码 |
| LvlMaze | 各难度 Rooms、SizeX/Y、Merge |
| LvlWarp | 方向、选择框、入口偏移、返回落点 |
| `world/outdoor_layout.*` | 原区域连接表、矩形摆放、共享边界 |
| `world/outdoor.*` | 8×8 地块、边界开口、洞口、墓地、特殊 DS1 |
| `world/maze.*` | Cave/Crypt 房间图、方向、合并、主题、特殊房 |
| `world/map_assembly.*` | 原图层与对象平移，共享 DS1 额外边行 |
| `world/exits.cpp` | Vis/Warp 与野外边界双向关联、可达性约束 |
| `gameplay/session_exits.cpp` | 走近、跨区、返回、持续移动目标 |

野外基础草地使用原 DRLG 的 `0x40002` 地板规则及 `0x44103` DT1 掩码。不同关卡放置对应原预设，包括凯恩石阵、艾尼弗斯树、塔楼、营地、房屋和洞口。

DT1 图像共享；像素与碰撞使用同一瓦片变体。隐藏出口仍参与关联。缺少所需瓦片时报错，不铺替代地板。

## 种子和工具

- `--map-seed`：地图种子，默认 210；`--population-seed`：独立刷怪种子。
- `--difficulty normal|nightmare|hell`：同时影响迷宫房间数和刷怪配置。
- `d2x_assets assets/mpq2 maze 10 210 0`：洞穴／地穴房间配方。
- `d2x_assets assets/mpq2 outdoor 6 210`：野外配方、边界及碰撞条带。
- 连接营地的朝向由种子选择；独立模板可用 `--preset 1 --level-type 1 --variant 0..3` 查看。

## 边界

- 原规则的局部适配，不承诺种子逐比特一致；DT1 rarity 仍使用位置散列。
- 野外已接矩形边界／预设分支；完整悬崖、河桥、道路、LvlSub、传送点与神殿布局待实现。
- 可选 Trees2（Def 40）在其原 DT1 掩码中缺少所引用的阴影瓦片，暂跳过该装饰；艾尼弗斯树仍用独立原预设。
- 地形整体装载，怪物按附近房间创建／休眠。完整地形流式加载、任务门和原版屋顶语义待实现。
- 当前停止构建、运行和打包；此前两种种子已有加载记录，最终键鼠往返待人工查看。

参考固定 [D2MOO](https://github.com/ThePhrozenKeep/D2MOO/tree/5596f5cb6c5251a0a07c6637d26458b06099d516) 的 DrlgMaze、DrlgOutPlace、DrlgOutWild、DrlgOutdoors、DrlgRoomTile、DrlgActivate；改编保留 MIT 归属，见 [许可](licenses/D2MOO.txt)。
