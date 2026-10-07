# 地图、区域与地图界面

五幕普通区域 1–136 使用同一纯 C++ NativeMapGenerator 和当前 MPQ。联机原服事件决定活动地形；独立资源工具仍可生成地图报告。产品没有 LocalMapClient 或本地角色直进地图入口。

## 所有权与入口

| 入口 | 职责 |
| --- | --- |
| [world_catalog.cpp](../../src/content/world/world_catalog.cpp) | Levels／LvlTypes／LvlPrest／LvlMaze／LvlSub 等只读定义 |
| [native_map.cpp](../../src/world/native_map.cpp)、[native_room_activation.cpp](../../src/world/native_room_activation.cpp) | 原布局、房间生命周期、有序近邻、真实 DT1 与完整碰撞 |
| [remote_town.cpp](../../src/client/remote_town.cpp) | 原幕／种子、0x07／08及位置事件重放、锚点校验、活动 Map／Grid 和交互值 |
| [remote_control.cpp](../../src/client/remote_control.cpp) | 原整数目标／GUID请求、靠近、旅行；显示路径不写权威位置 |
| [automap_exploration.cpp](../../src/client/automap_exploration.cpp) | 本局区域探索掩码，城镇全揭示／野外可见格规则 |
| [world_renderer.cpp](../../src/presentation/world/world_renderer.cpp)、[scene_assets.cpp](../../src/presentation/scene_assets.cpp) | 公共地形／单位绘制、排序／原图与灯光资源 |

0x07／08只维护客户端视野引用；本人坐标选择当前房间／区域，不额外建立服务器 CLIENT_IN_ROOM 引用。房间事件历史缺口、生成失败或锚点失配明确停止显示／移动，要求重新入局，不回退近似预设。

## 客户端显示与操作

SceneController 统一世界手势；RemoteScene 返回当帧命中／绘制输入。SceneView／SceneAssets 共用原地形、Pops、Warp、原对象／单位动画及光照，原服模式与显示模式分开。地图交互和传送点解锁／菜单／旅行都等原服，面板不推断成功。

自动地图只有 AutomapExploration／AutomapCatalog／SceneAssets／SceneView 一套揭示、标记、缓存和绘制；RemoteTown 适配原服活动区域。同局返回记忆保留，新局清空；不读写 .d2xmap。细节见[自动地图](../gameplay/world/AUTOMAP.md)。

## 限制与后续

原地形／碰撞对照样本及生成边界见[五幕地图](../gameplay/world/MAPS.md)与[原版对照](../architecture/MULTIPLAYER.md#原版-dll-对照方法)。长距离绕障、动态阻挡、完整房间时序、特殊旅行资格、全种子、精确客户端照明及 Linux 未全面认证。最新传送点点击／动画修正未入包，见[基线](../../BASELINE.md)。
