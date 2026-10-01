# 第一幕地图

当前源码按 MPQ `Levels.txt` 的生成类型为第一幕 39 个关卡建立地形入口；探索／任务入口以当前会话与任务模块为准。地图取自运行时 MPQ 的 DS1/DT1、`LvlPrest`、`LvlMaze` 和 `LvlSub`，不使用独立提取的数据文件。2026-09-23 曾用五个原始 MPQ 对 1–39 关逐张短帧启动并截图，39/39 正常退出；这是历史地形冒烟，不涵盖本次源码，也不代替键鼠往返验收。
营地和鲁高因出生点从运行时 DS1 的城镇扫描标记选取；城镇奔跑不消耗耐力。当前开放第一幕与第二幕鲁高因城镇、后宫一层。NPC 与传送点点击范围跟随当前 MPQ 动画帧的非透明区域边界。

## 鲁高因城镇

2026-10-01：Levels 40、LevelType 12、LvlPrest 301 直接读取当前 MPQ 的 File2 `Act2/Town/LutW.ds1` 及原 DT1；不生成替代城市或图块。第二幕对象按 D2MOO `DRLGPRESET_GetObjectIndexFromObjPreset` 的幕别数组解析，NPC 按原 MonPreset、MonStats2 组件和 COF 组合，保留真实中立身份及 DS1 位置。Waypoint 原对象156、私人箱等物件沿原 Objects 的尺寸／碰撞／操作；地形和 NPC／物件选择 Act2 PAL，照明选择 Act2 PL2，跨幕资源键隔离，避免重复 token 使用第一幕颜色。

东行依据 D2MOO `SUnitNpc.cpp` 的 Warriv1 服务：任务资格、换至 LEVEL_LUTGHOLEIN、随行单位换区及激活城镇 waypoint。出生沿当前 `Map::actSpawn` 的原 main30／sub0–4 标记入口；D2MOO `DrlgPreset.cpp` 与 `DrlgDrlgWarp.cpp::sub_6FD788D0` 说明索引0对应该组，当前取首个可走标记，不声称随机流逐点等价。原城镇布局选择由沙漠出口方向决定（`DrlgOutPlace.cpp`），本批尚无第二幕沙漠生成，只开放原 LutW，LutN 和完整幕间地图网络暂缓。

普通 seed=210 运行日志确认原图57×57、943个瓦片、0未解析格、68个对象外观、0待原规则对象；原传送点交互／跨幕菜单和独立存档重载通过。已查看城镇截图，不把零缺砖视为完整客户端逐像素认证；第二幕任务、商店／佣兵／对白服务未开放，中立 NPC 不替换成敌人。角色／弹体等既有公共外观调色适配与精确客户端取光边界仍保留。

## 第二幕类型交付

神秘避难所74：按 D2MOO `PlaceArcaneSanctuary` 的中心＋四支各15房、侧枝8/12不延续主链布局，读取510–528原房，四支固定File1–4、中心File5；原Files=0侧枝采用有限随机默认0，特殊房替换清除旧固定变体。普通seed210/211最终包短帧均退出0、181×181、零缺砖，查看中心截图；证据在 `artifacts/act2-arcane-<seed>-20261002.*`。原局部传送器、宫殿对象入口、召唤者日志／红门和专属玩法未接，四支完整探索不宣称完成。

虫穴（62–64）：按 D2MOO `PlaceAct2LairStuff`、原10格房间482–509与LvlMaze生成，保留前两层上下行房、第三层窄口509／向西宝藏505／向北入口500，不套第一幕主题偏移。当前MPQ部分v16/v18虫穴房的编辑器幕字段为0，其他为1；生成关卡按原Levels.Act归属解释单位，保留旧单位格式版本检查，不修改原文件。普通seed=210三关包内短帧启动退出0、零缺砖，62↔63↔64楼梯链接及路径校验通过；查看64截图，证据在 `artifacts/act2-lair-<level>-20261002.*`。沙漠入口、任务宝箱奖励／虫后玩法仍暂缓。

墓穴（55–61、66–73）：按 D2MOO `DRLGMAZE_PlaceAct2TombPrev_Act5BaalPrev` 建三岔固定入口，按 `PlaceAct2TombStuff` 放原楼梯、57 waypoint、59箱／首领／宝藏、60方块房；61直接原480/Serpent1，73直接原481/Duriel。七墓真墓／首领墓由原种子循环选不同编号并应用三倍／两倍房间数及对应原特殊房，随机流调度仍非逐比特认证。普通seed=210十五关包内短帧启动均退出0、零缺砖，55↔59、56↔57↔60、58↔61出口链接；已查看61/73截图，证据在 `artifacts/act2-tomb-<level>-20261002.*`。真墓墙、任务祭坛／方块奖励、都瑞尔入口及首领玩法仍未实现；沙漠侧出口等待野外类型。

宫殿（51–54）：依据 D2MOO `DRLGMAZE_PickRoomPreset` 的角房映射和四房初始环，原354–361房间按16格拼接，不套普通主题偏移；52西北房固定File3 waypoint，54西北／东南固定File4。普通seed=210包内四张图均退出0、33×33、零缺砖，50↔51↔52↔53↔54原双楼梯链接且出口路径检查通过；已查看52截图，证据在 `artifacts/act2-palace-<level>-20261002.*`。神秘避难所对象传送门及宫殿任务门未实现，不以普通楼梯代替。

下水道家族（47–49、65）：依据 D2MOO `DRLGMAZE_ScanReplaceSpecialAct2SewersPresets`，读取原12格房间302–352、LvlMaze房间数／Merge，首层北／东延伸放两个城镇入口，二层原waypoint房、三层拉达曼特房、古代通道箱房。原房间图与主题替换共用既有生成器，随机流仍是适配。普通seed=210四关包内两帧启动均退出0、零缺砖，40↔47↔48↔49出口链接；65的沙漠目标尚未开放。查看48截图，日志／图在 `artifacts/act2-sewer-<level>-20261002.*`。未实现拉达曼特专属玩法／任务，未认证原DRLG种子等价或鼠标全程探索。

2026-10-02：后宫一层（Level 50、LevelType 14）读取 LvlPrest 353 的唯一 File1 `Act2/Palace/Harem2.ds1`、Dt1Mask 79，按 D2MOO DrlgPreset 的完整关卡加载路径，不当作随机房间。共用出口扫描开放第二幕，原 Vis/Warp 标记建立鲁高因40与后宫50的双向楼梯；未加载目标仍禁用，未绕过宫殿任务门规则。Windows Release、固定包更新及包内普通 seed=210 两帧启动通过，进程退出0，截图和日志在 `artifacts/act2-harem-20261002.*`；只验证地形和出口关联，未验证任务资格及鼠标楼梯往返。第二幕后续类型仍逐项实施。

## 连续路线

`罗格营地 → 血腥荒地 → 冰冷之原 → 石块旷野 → 地下通道一层 → 黑暗森林 → 黑色荒地 → 泰摩高地 → 修道院大门`

支线：邪恶洞窟；洞窟、地洞、深坑一／二层；地下通道二层；埋骨之地及两座墓穴；遗忘之塔入口及地窖一至五层。

外侧回廊 27、兵营 28、监牢 29–31、内侧回廊 32、大教堂 33、地下墓穴 34–37 已通过原 Vis/Warp 或依赖关卡边界接入。崔斯特瑞姆 38 和牛场 39 可独立选关查看地形；它们在 `Levels.txt` 没有 Vis 连接，原入口由任务条件创建传送门。任务传送门由会话／任务模块管理，不能把两张图无条件接成普通出口。

野外通过边界行走；洞口／楼梯按原 LvlWarp 选择区域点击进入。返回保留区域状态。Ctrl+F2 是开发目录，不是原版传送点系统。

步行交界处不使用楼梯式默认落点。此前只保留坐标连续并拒绝阻挡落点，仍把跨区点击导向固定开口中点；用户已确认该版本在城镇进出时走错方向、卡在栅栏。当前源码同时修正通道识别和寻路，不再把拒绝错误落点当作完整修复。

- `alignPresetBoundary` 在相邻区域接触范围内使用原 DT1 碰撞和内部连通区域寻找实际边缘通路；城镇起点取 DS1 出生标记，其他预设使用原有内部检查点。探测时只取消接触范围内引擎附加的封边，不清除原瓦片阻挡。移除“内侧三格靠近中点”及固定八格宽度的开口猜测。
- `LevelExit.passages` 保存按世界坐标配对的离开点和对侧到达点，两端必须连接各自地图内部。城镇、野外和回廊／兵营等内部边界共用规则，取消只对少数地图编号检查对侧碰撞的分支。默认边界入口也取已验证的内部路径，不再向内偏移八格后无条件找最近可走格。
- `beginBoundaryExit` 比较“角色到通道”和“对侧通道到点击终点”的可达路径长度，排除不连通通道。选中的通道保留至实际过界；持续拖动可重新规划，过界后继续走向原世界坐标终点，而不是默认出口点。对侧点击阻挡格时，以最接近点击点的可达路径终点选择通道；确实没有通路仍拒绝，不穿栅栏。
- 步行边界只匹配反向边界，不误配同目的地的楼梯；楼梯仍保留显式激活和原 LvlWarp 到达规则。可走落点不吸附格心，步行换区相机按区域坐标偏移平移，这两项保留此前实现。
- `Grid::segment` 逐格检查线段经过的碰撞格，经过对角交点时检查两侧格；不再靠间隔采样遗漏窄拐角。A* 路径加入起点格心并验证平滑首段，避免非格心起点连到第一个节点时穿过障碍；坐标入口拒绝非有限值和越界值。
- 所有已接入弹体改用 `Grid::missileSegment`，按各自原表 `CollideType/Size` 查询地形及对象标记，不再共用行走阻挡。原 DT1 各层和 DS1 第 16/17 位按位合并；行走同时保留 `WALL/NOPLAYER`，第二层地板不能清掉第一层障碍，墙角配片和屋顶的原碰撞也参与合并。依据见 [技能](SKILLS.md#通用弹体与地形碰撞)。此修改未构建、测试或打包。
- `MoveTo` 允许 A* 返回最接近目标的可达节点，点击石头／墙内不会因终点阻挡立即丢弃整条路径；路径首段和平滑仍经过完整行走碰撞。依据 D2MOO `PATH_AStar_ComputePath` 保留并输出 `pBestNode` 的行为，搜索顺序与同距节点选择是本项目适配，不宣称路径逐点相同。一般可达性判断默认仍要求精确到达，避免部分路径被误作连通证明。
- `Objects.txt` 的原尺寸、各模式 `HasCollision` 与原 `BlockMissile` 写入对象层，角色、寻路和弹体读取同一份状态；已开启／已破坏对象不靠删除整块地形来放行，重叠物体也不互相清除阻挡。入口与动态边界见 [物体](INTERACTIVE_OBJECTS.md)。
- 新移动命令先取消旧拾取／交互，包括跨区规划失败的情况。键盘整步被挡时推进至碰撞前的安全位置，避免一步越出网格失败后停在过界触发范围外；近战动作未结束时不执行键盘位移。

用户此前已构建并确认跨区行走修复可用；随后随界面修订构建、打包和提交，助手未运行游戏或测试。该确认不涵盖本次通用碰撞／障碍目标靠近修改，也不等于穷举营地四种朝向、普通野外、回廊／兵营内部边界和楼梯往返。当前 `Grid::segment` 供行走、近战视线和交互使用，弹体独立查询；本次均未运行验收，不宣称全部地图或逐种子行为已通过。

## 数据与代码

连续边界的怪物显示（2026-09-27 源码）：跨区时原 `AreaState` 已保留在会话中，旧绘制却只遍历当前区域，导致进城后门外怪物／尸体整批消失。`GameSession::sceneRegions/areaState/roomVisible` 现在让地形与怪物共同访问当前及直接步行相连区域；按两侧真实房间的世界坐标判断相邻，不把观察者吸附到另一关的最近房间。怪物、阴影、悬停高亮、名字／血条复用原实体，保留 ID、生命和原位置；方向缓存用世界坐标，动画相位用实体 ID，换区不制造位移或跳相位。规则覆盖所有连续边界，楼梯／传送门不串场。依据 D2MOO `DrlgActivate.cpp` 的相邻房间状态传播；这是可见性修复，邻区 AI／战斗仍沿用现有区域休眠，跨关追击、隔界攻击和未进入区域的预激活尚未接入完整原版调度。未修改存档语义，未构建、测试、运行游戏或打包。

| 输入／模块 | 职责 |
| --- | --- |
| Levels、LvlPrest、LvlTypes | 身份、生成类型、尺寸、DS1 槽位、DT1 掩码 |
| LvlMaze | 各难度 Rooms、SizeX/Y、Merge |
| LvlWarp | 方向、选择框、入口偏移、返回落点 |
| `world/outdoor/outdoor_layout.*` | 原区域连接表、矩形摆放、共享边界 |
| `world/outdoor/outdoor.*` | 8×8 地块、边界开口、洞口、墓地、特殊 DS1 |
| `world/outdoor/outdoor_shrines.*` | MPQ 神殿／治疗井分组放置 |
| `world/maze.*` | Cave/Crypt 房间图、方向、合并、主题、特殊房 |
| `world/map_assembly.*` | 原图层与对象平移，共享 DS1 额外边行 |
| `world/exits.cpp` | Vis/Warp 与野外边界双向关联、可达性约束 |
| `gameplay/session/session_exits.cpp` | 双侧通道选路、走近、跨区、返回、持续移动目标 |
| `world/navigation.*` | 逐格直线碰撞、连通区域、A* 与路径平滑 |

## 按原表生成类型核对

| `DrlgType` | 第一幕关卡 ID | 当前地形入口 |
| --- | --- | --- |
| 预设 `2` | 1、13–16、20、25–27、32–33、37–38 | 读取 `LvlPrest` 对应 DS1/DT1；回廊和大教堂的依赖边界已关联，38 仍缺任务入口 |
| 迷宫 `1` | 8–12、18–19、21–24、28–31、34–36 | 洞穴／墓穴／塔楼、兵营、监牢、地下墓穴各有房间生成器 |
| 野外 `3` | 2–7、17、39 | 主线野外、埋骨之地与牛场各有生成入口；39 仍缺任务入口 |

`LevelType` 另区分城镇、草地、洞穴、塔楼、回廊、兵营、监牢、教堂、地下墓穴和崔斯特瑞姆的瓦片库。`DrlgType` 与 `LevelType` 的值都由 MPQ 读取，不按关卡名猜测。

野外基础草地使用原 DRLG 的 `0x40002` 地板规则及 `0x44103` DT1 掩码。不同关卡放置对应原预设，包括凯恩石阵、艾尼弗斯树、塔楼、营地、房屋和洞口。

DT1 图像共享；像素与碰撞使用同一瓦片变体。隐藏出口仍参与关联。缺少所需瓦片时报错，不铺替代地板。DS1 方向 3 的上方右墙角在同格叠绘当前 MPQ 中主／子索引相同、方向 4 的墙角部分；该配对规则见 D2MOO `DrlgRoomTile.cpp` 与 Diablerie `LevelBuilder.cs`，用于补齐营地与野外建筑的同类墙角缺口。屋顶独立于墙体最后绘制；DS1 orientation 10 的成对区域标记限定进入范围和对应屋顶主索引，角色进入时只让该组屋顶渐隐，离开后恢复，普通墙体不透明。拼接地图中的标记随预设一同平移。本轮墙角修正尚未构建或运行验收。

## 坐标、绘制与阻挡

2026-09-30 源码修订，仅核对本地参考源码和当前 MPQ，未构建、运行检查、测试、游戏或打包。

| 范围 | 当前规则与修正 | 本地证据 |
| --- | --- | --- |
| 等距投影 | 子格 `(x,y)` 投影到 `(16(x−y),8(x+y))`；现有投影无需改动。动态单位保留精确坐标，初始为格心；静态对象、地面物品和对象式传送门按整数子格。修复静态对象统一加 `.5` 导致下移 8 像素。 | D2MOO `D2Dungeon.cpp`、`Units/Units.cpp::UNITS_InitializeStaticPath`、`Path/Path.cpp::PATH_AllocDynamicPath` |
| 图像锚点 | DCC／DC6 的原帧偏移、COF 组件层序保留；不能用透明包围盒底边重新居中。DT1 地板 x=−80/y=0，上墙／影子按原块 minY+80，屋顶用 −RoofHeight。Objects Xoffset/Yoffset 只偏移原图及其命中框，不移动世界位置、碰撞或所属 Overlay。 | 当前营地 floor/fence/objects DT1、Objects；OpenD2 DT1、DGEngine DT1/DC6、DGEngine.core Sprite2、Diablerie COFRenderer |
| 绘制顺序 | 先低墙（16–19）、地板及 DS1 阴影，再 COF 地面阴影；上墙与单位按宏格对角线／地面落点排序；同格墙角配片保持 DS1 层序，屋顶最终绘制。移除墙体 +64、尸体 −1、物品 −0.1 的深度猜测。按 Objects DrawUnder 和逐模式 OrderFlag 放置物体，悬停选择共用该顺序及像素偏移。 | OpenDiablo2 renderer 的四个 pass、Object.GetLayer；Diablerie Iso.SortingOrder／WorldRenderer／LevelBuilder |
| 弹体与状态图 | 弹体、独立法术效果进入同一地面落点队列；当前及直接步行相邻区域共用偏移。附着 Overlay、祭坛图和 NPC 提示按 Overlay.PreDraw 在所属单位前／后绘制，再接受屋顶遮挡和世界暗层。 | 当前 Overlay；Diablerie Overlay.Create；OpenDiablo2 Missile.GetLayer |
| 原阻挡 | DT1 的 25 标记按 `(4−sy)*5+sx` 读取，列序不反转；图像和阻挡取同一变体，按位合并地板、上／低墙、墙角配片、真实出口和屋顶，阴影不产生碰撞。DS1 FillLOS→0x04，Unwalkable→0x01，门／真实出口／Linkage→PRESET 0x10。真实房间初始为零；无地板图不自动造墙，无房间空隙用原无效掩码 0x27。 | D2MOO D2Collision／DrlgRoomTile、OpenD2 indexTable；当前 DT1 原标记 |
| 行走体积 | 玩家 mask=0x1c09，普通怪物=0x3c01，飞行=0x1804，Wraith 家族=0x0804；动态 size 1/2 的路径 pattern 为十字，size 3 为 3×3。NPC、佣兵及召唤物按真实 MonStats/2 身份查询。人口和巢穴找位另读 spawnCol（0/default→0x3c01，1→0x01c0，2→0x3f11，3→0）及原 SizeX 的 point/cross/square；不把穿墙／飞行移动规则用作出生掩码。A*、平滑、键盘步进、传送落点和交互靠近站位使用对应规则，交互视线仍为点查询。连续边界体积按世界偏移读对侧真实标记和动态对象，不把额外 DS1 图形边行当碰撞房间。 | D2MOO Path.AllocDynamicPath／D2Collision.CheckMaskWithPattern；当前 MonStats.flying、MonStats2.SizeX/spawnCol |
| 逐模式对象 | SizeX/SizeY／HasCollision、UNITS_GetCollisionMask 与 BlocksLight 各模式独立保留；开门／破坏后同步改变对象层与光照缓存，不清除底下地形或重叠对象。 | 当前 Objects；D2MOO Units／COLLISION_CreateBoundingBox |

入口：`presentation/world/scene_geometry.hpp` 区分地面锚点与绘制顺序，`world_renderer.cpp` 组织层级，`world/map.cpp` 合并地形标记，`world/navigation.*` 查询形状与掩码，`world/exits.cpp` 绑定稳定相邻网格，`content/monsters/monster_catalog.*` 导入身份规则，`world/region.*` 同步对象模式。网格不持有 MPQ 或 GPU，UI 不写玩法状态，邻格引用只在区域容器装载完成后建立，不进入存档。

这是对已核实规则的修正，不是完整 D2Client/DRLG 移植。动态单位 NO_PATH/PET 占位和拥挤绕行仍未完整进入网格；怪物 OpenDoors 的自动操作／锁门分支未接入，普通地面怪物暂保留 DOOR 阻挡，不因去掉掩码而穿过闭门。原客户端精确混合、阴影强度、Overlay 时钟／偏移与屋顶渐隐时序仍需画面对照；步行边界之外仍有项目的封边保护。新坐标、遮挡和窄门通路待用户实机查看。

## 种子和工具

- `--map-seed`：覆盖地图种子；`--population-seed`：覆盖人口计划种子；默认由新局随机源派生，`--seed <uint32>` 固定整局。
- `--difficulty normal|nightmare|hell`：同时影响迷宫房间数和刷怪配置。
- `d2x_assets assets/mpq2 maze 10 210 0`：洞穴／地穴房间配方。
- `d2x_assets assets/mpq2 outdoor 6 210`：野外配方、边界及碰撞条带。
- 连接营地的朝向由种子选择；独立模板可用 `--preset 1 --level-type 1 --variant 0..3` 查看。

## 边界

- 原规则的局部适配，不承诺种子逐比特一致；DT1 rarity 已按 D2Seed 加权选取，但流的调度仍为项目适配。
- 野外已接矩形边界／预设分支、悬崖／河桥和适配道路；传送点使用 `LvlSub` 类型 4 原分组。此次接入类型 5 的神殿／治疗井：`Levels.SubShrine` 给出 LvlSub 类型编号，从该类型的前四条原表记录循环选择，在可用宏格叠加原 DS1 分组和对象。原版每区五处的规则来自 D2MOO `DRLGOUTDOORS_SpawnAct12Shrines`；宏格内位置和随机流是当前适配，已在本轮截图检查，位置仍为项目适配。
- 通用野外主题仍缺失。当前唯一原始 `Trees.ds1` 位于 `d2data.mpq`，v12 文件声明 14 个分组，但尾部只有 13 个完整记录和 4 个字节；`d2exp.mpq`、`Patch_D2.mpq` 与 `d2x-act1.mpq` 没有另一份。解码器拒绝这份不完整数据；取得可核实的完整原资源或有依据的格式解释前不截断分组、不造替代主题。
- 可选 Trees2（Def 40）在其原 DT1 掩码中缺少所引用的阴影瓦片，暂跳过该装饰；艾尼弗斯树仍用独立原预设。
- 地形整体装载，怪物按附近房间创建／休眠。完整地形流式加载仍待实现；任务门进度见任务模块；屋顶已按 DS1 标记做局部渐隐，尚未与原客户端逐场景核对时序和所有屋顶分组。
- 本轮用种子 210、普通难度对 1–39 关各运行两帧，39 份截图及日志在忽略的 `artifacts/act1-map-review-20260923/`；日志没有未解析 DT1 瓦片。第四层另将镜头移到原 `andariel` 实体 (46.5,46.5) 并截取首领房。怪物身份正确，外观仍为已授权的敌对怪物替身。修道院大门出生画面中人物被立面半透明遮挡，需后续与原版画面对照；完整路径、门交互及更多种子未在此冒烟中验收。

参考固定 [D2MOO](https://github.com/ThePhrozenKeep/D2MOO/tree/5596f5cb6c5251a0a07c6637d26458b06099d516) 的 DrlgMaze、DrlgOutPlace、DrlgOutWild、DrlgOutdoors、DrlgRoomTile、DrlgActivate；改编保留 MIT 归属，见 [许可](licenses/D2MOO.txt)。
屋顶区域标记与渐隐参考固定 [Diablerie LevelBuilder](https://github.com/mofr/Diablerie/blob/9e42ef257228257825ebbbeb905d4b1d894b6e98/Assets/Scripts/Diablerie/Engine/World/LevelBuilder.cs)、[Popup](https://github.com/mofr/Diablerie/blob/9e42ef257228257825ebbbeb905d4b1d894b6e98/Assets/Scripts/Diablerie/Engine/World/Popup.cs)；独立屋顶层另与 [OpenDiablo2 renderer](https://github.com/OpenDiablo2/OpenDiablo2/blob/7f92c571bf04057a7fbdfb5d25a486f7d775e3c3/d2core/d2map/d2maprenderer/renderer.go) 核对。参考项目的行为是实现依据，运行时标记和图形均从当前 MPQ 读取。
