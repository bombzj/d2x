# 地图、区域与客户端地图基线

更新：2026-10-04。对应用户选择的第六项和[技术改造方案](../architecture/REFACTOR_PLAN.md) P6 基础边界、P1 地图 UI 切片。当前仍使用单玩家激活策略，完整 P6 和客户端隔离尚未完成。第六项原按要求未测试，本轮随第七项授权完成 Windows Release 与有限开发旅行／地图显示冒烟；不复用第五项或较早地图证据认证本批。

## 所有权与入口

| 入口 | 当前职责 |
| --- | --- |
| [`world/identity.hpp`](../../src/world/identity.hpp)、[`plan.hpp`](../../src/world/plan.hpp) | 原关卡 ID／名称与生成选择、配方计划和加载目录；不包含人物、怪物运行态或会话 |
| [`world/map_terrain.hpp`](../../src/world/map_terrain.hpp) | DS1 解码地形、共享 DT1 所有权、瓦片索引与已选变体；`Map` 组合此值，原瓦片选择算术不变 |
| [`world/map.hpp`](../../src/world/map.hpp) | 活地图的网格、物件碰撞、房间索引、出生／到达点及墓墙修改／恢复；拆墙仍同步修改地形与碰撞，不是完全不可变地图资产 |
| [`world/region_store.*`](../../src/world/region_store.hpp) | 一局的固定区域槽位、地图资源缓存、原目录和加载种子；按显式 RegionId 查找／加载，填充既有槽位，不在加载／旅行时扩容或搬移网格 |
| [`world/region_visibility.hpp`](../../src/world/region_visibility.hpp) | 按显式起始区查询连续地面的显示拓扑；直接邻区场景与递归自动地图共用实现，楼梯／门户不串场，区别于模拟激活 |
| [`gameplay/areas/state.hpp`](../../src/gameplay/areas/state.hpp)、[`repository.hpp`](../../src/gameplay/areas/repository.hpp) | 区域战斗状态及休眠缓存；进入时移交到模拟器，离开时移回，一份怪物／弹体／效果状态，不按观察者复制 |
| [`session_regions.cpp`](../../src/gameplay/session/session_regions.cpp) | 本地宿主连接按需加载、NPC 名称／运动／货架准备、城镇到达点、人物／佣兵跨区与任务协调；具体业务仍由原服务执行 |
| [`world/object.hpp`](../../src/world/object.hpp)、[`level_exit.hpp`](../../src/world/level_exit.hpp) | 从原 region 聚合头拆出的权威物件／出口类型；物件身份、模式、NPC 路径及碰撞字段未另建重复运行态 |

`RegionStore` 仅在建局时初始化一次。权威适配得到可变槽位 span，不能通过此入口扩容；兼容外观的 `regions()` 仍提供原只读容器。模拟器借用网格／房间索引，网格的连续邻区引用在出口重建时绑定，所有者寿命覆盖模拟和显示资源借用。当前区索引仍属于单人宿主，不是未来的全局活跃区。

按需顺序保持：原配方规划 → 创建全部稳定槽位 → 加载请求区 → 按原配方顺序加载直接连续邻区 → 重建已加载出口 → 原任务物件协调。每个新槽位加载完成后立即执行原 NPC／商店宿主准备，再继续下一槽位，保留 ID 分配与随机流消费顺序。原人口首次创建、返回休眠区、读档先加载幕城镇再重映射库存 ID、恢复墓墙后按任务状态重建的顺序保留。生成准确性和既有暂缓仍见[地图规则](../gameplay/world/MAPS.md)。

## 客户端显示与操作

传送点列表本批源码修正（2026-10-04）：按用户原版截图和当前 MPQ 的 `waygatebackground` 四块（256×256、64×256、256×176、64×176）拼接 320×432 面板，对齐原 36 像素行距、文字区域和关闭按钮；补 TBL `waypointsheader` 标题，关闭按钮复用原 `buysellbtn` 第 10 帧。原 `waygateicons` 第 0 帧只用于当前地点，第 3 帧用于其他已激活地点，未激活格保留空图标；未解锁幕页隐藏。白／蓝／深灰字体由当前 MPQ `sky/pal.pl2` 的 `TextColorShifts`（0／3／5）变换原 `font16` 索引像素，不用近似 RGBA 着色；菜单原图按 sky 调色板加载。布局依据原图与截图；包内 EXE 实机截图已查看，标题、当前蓝门／深灰锁定行及原关闭按钮可用。sky PL2 白色变换表全零，白字直接保留原字体索引，蓝／灰仍按变换表读取。不宣称完整 D2Client 像素或按下动画等价。

正式传送点菜单（有 `waypointSource`）不暂停世界或环境音；`blocksInput` 保持菜单消费输入、清旧战斗目标，`blocksWorld` 管理游戏菜单等既有暂停窗口。开发地图目录、启动参数 `--maps`、独立暂停／静音快捷键及区域重置已删除，菜单仅保留真实传送点目的地；PgUp／PgDn 继续用于 NPC 对话、商店及佣兵列表滚动。传送仍由会话复验两端激活、源点接触与角色动作，不因 UI 显示可点就跳过权限。

`WAYPOINTS_AllocWaypointData` 与 `WAYPOINTS_CopyAndValidateWaypointData` 均强制第零个传送点激活；新局从当前 MPQ `Levels.Waypoint=0` 动态定位对应关卡（当前为第一幕营地），不按地名或固定关卡号判断。新局默认激活和 D2S 恢复的已激活点以负时间表示局前已开启，原 `ON` 动画／光源立即生效；本局首次野外激活仍走原 `NU → OP → ON`，第一次点击激活、下一次点击打开列表，依据 `OBJECTS_OperateFunction23_Waypoint`。运行规则指纹新增 `waypoint-rules-v1`，原 D2S v96 不增加字段；旧档缺必选位的提示与保存方式见 [存档](SAVES.md#传送点初始化与恢复)。本批 Windows Release 构建、打包与有限冒烟已完成：新角色和新进程读档均默认开启第一点，菜单打开时祭坛剩余时间 95.92→83.56 秒，原关闭按钮实际点击关闭；未验收野外首次激活、全部目的地旅行或像素等价。证据位于 `artifacts/package-smoke-20261004/`，没有编写测试程序。

[`contracts/map.hpp`](../../src/contracts/map.hpp) 提供本人观察区、自动地图尺寸／连续区偏移／可揭示房间、可显示物件标记、出口选择框、门户与传送点幕资格；`TravelMenuView` 提供原排序和可用状态。没有人物任务簿、其他玩家背包、怪物运行态、地图种子或权威导航网格。

[`IMapClient`](../../src/client/map_client.hpp) 绑定当前本地操作者；[`LocalMapClient`](../../src/client/local_map_client.cpp) 解释原状态，按权威版本缓存场景，按版本＋来源＋幕页缓存传送点列表；空来源不返回开发目录。场景保存独立值，借用仅至下一次对应读取／销毁；投影不授权操作。[`areas/intents.hpp`](../../src/gameplay/areas/intents.hpp) 的 `MapIntent` 仅提供传送点、出口及门户意图，经原权威命令再次校验接触、距离、资格、到达点和门户 revision。自由旅行 `Travel` 只供应用调试管道使用，不再暴露给地图客户端；区域重置命令与模拟器重置入口已删除。

自动地图绘制／揭示、出口提示、传送点菜单和地图手势分别在 `presentation/world/{automap_view,map_view,map_controller}.cpp`、`hud/{exit_view,waypoint_view}.cpp`，不再直接查询会话或完整状态。原普通物件提示单独留在 `world/object_hint.cpp`，主控制器的地图处理位置及凯恩门户 → 普通门户 → 出口的优先级保留。探索掩码由 `client/automap_exploration.*` 唯一拥有，SceneView 根据原 DT1 地板／墙图像与可用场景视口的相交采集可见格，在更新摄像机后将邻房候选内的格交给客户端合并，不再整房揭示；相机、屋顶渐隐、GPU 及自动地图图形继续由表现层拥有。`app/automap_save.*` 将探索保存到角色同名 `.d2xmap`，按难度／种子／内容和地形指纹恢复；原生 D2S 不加私有数据。文件格式、冲突处理和双文件保存限制见[存档](SAVES.md#自动地图探索2026-10-04)。

[`IMapAssetSource`](../../src/client/map_asset_source.hpp) 是单独的本地资源读取接口，不是网络契约。它只借用解码 DS1、共享 DT1 图像及图形索引事实，不返回 Region、WorldObject、Grid、MPQ 句柄或 GPU。地形修改由权威步骤完成，显示同步读取同一地形；此借用不复制第二份地图。当前没有卸载，解码数据／瓦片寿命覆盖源和场景；未来卸载须先增加句柄／版本与失效规则。

地图瓦片源同步、调色板与自动地图资源索引改用此接口。原按需 GPU 上传、原 LvlPrest／LevelType／Levels.Pal 选择以及读档后立即同步新加载城镇资源的入口保留。怪物／人物／物件动画资源准备仍有原会话适配，未把整个 `SceneAssets` 称为独立资源目录。

## 限制与后续

- `WorldObject` 内的定义事实与活模式／NPC 决策尚未全面分组件，世界任务物件仍由原宿主协调；第七项已将 NPC 路径／接触及交谈／进入区域规则拆出，见 NPC／任务基线。
- 世界单位／弹体绘制、屋顶与光照网格查询仍有会话兼容入口；`SceneView`／`SceneController` 及表现目标仍依赖会话，不是完整客户端／服务端隔离。
- 当前仅一个活区域在模拟器推进；连续邻区显示不表示邻区 AI／跨关战斗已激活。多观察者房间激活、并行活区、卸载、异步资源和联网消息尚未实现。
- 原 DRLG、任务专用入口与客户端图形暂缓保持；本批不新增地图、替代图形、规则参数或随机消费。

第六项重构时未改 D2S v96、保存字段、规则指纹或原 MPQ，未读写用户存档、打包或提交 Git。当时集成构建修复 DS1 声明、怪物声音的显式依赖及表现所需 `world/object_animation.hpp` 值头。有限冒烟覆盖开发旅行 1→5→1→40→74 与已加载区返回、74 地形／自动地图显示（揭示 793 格），截图已查看，实例退出 0、stderr 空；连续边界往返、出口／传送点／门户实际旅行与读档资源同步未验收。准确证据与限制见[NPC／任务基线](NPC_QUEST.md#npc任务与奖励协调第七项)，不称完整 P6 验证。

自动地图本轮证据（2026-10-04）：原区域3步行探索136→152→168，修复内容指纹后新进程重进并开发旅行返回3，原168格全部保留且因不同落点新增至298，丢失0。修复后两实例退出0、stderr空；区域8原墙线截图已查看。Windows Release与运行包更新，不新增测试程序、不提交Git；完整视口／跨区／传送及像素等价未认证。输入指纹固定八张原表，不能再用依赖已读资源集合的会话指纹作为探索兼容键；具体入口和日志见[自动地图](../gameplay/world/AUTOMAP.md)。
