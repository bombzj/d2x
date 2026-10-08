# 地图、区域与地图界面

五幕普通区域1–136共用NativeMapGenerator和当前MPQ。自研宿主generateArea与客户端RemoteTown使用同一reveal函数及房间顺序；宿主通过原LOADACT／0x07传种子、幕及锚点，客户端自行重建，不共享C++地形快照。权威内核只持有独立碰撞值。当前自研进入保存幕的城镇，使用原DS1出生标记，不以inspectionArrival替代。新协议链的Act1营地入局和走跑已有限冒烟，其他幕／区域未在本轮复验，见[基线](../../BASELINE.md)。

## 所有权与入口

| 入口 | 职责 |
| --- | --- |
| [world_catalog.cpp](../../src/content/world/world_catalog.cpp) | Levels／LvlTypes／LvlPrest／LvlMaze／LvlSub 等只读定义 |
| [native_map.cpp](../../src/world/native_map.cpp)、[native_room_activation.cpp](../../src/world/native_room_activation.cpp) | 原布局、房间生命周期、有序近邻、真实 DT1 与完整碰撞 |
| [generated_area.cpp](../../src/world/generated_area.cpp) | 共用完整区域入口，接收幕／区域／种子／难度，输出持有DT1库寿命的只读Map与原点 |
| [game_content.cpp](../../src/hosting/game_content.cpp) | 读取当前MPQ职业、原出生点及静态物件初始阻挡，准备独立权威碰撞值；不执行玩法 |
| [remote_town.cpp](../../src/client/remote_town.cpp) | 原幕／种子、0x07／08及位置事件重放、锚点校验、活动 Map／Grid 和交互值 |
| [remote_control.cpp](../../src/client/remote_control.cpp) | 原整数目标／GUID请求、靠近、旅行；显示路径不写权威位置 |
| [automap_exploration.cpp](../../src/client/automap_exploration.cpp) | 本局区域探索掩码，城镇全揭示／野外可见格规则 |
| [world_renderer.cpp](../../src/presentation/world/world_renderer.cpp)、[scene_assets.cpp](../../src/presentation/scene_assets.cpp) | 公共地形／单位绘制、排序／原图与灯光资源 |

0x07／08只维护客户端视野引用；本人坐标选择当前房间／区域，不额外建立服务器 CLIENT_IN_ROOM 引用。房间事件历史缺口、生成失败或锚点失配明确停止显示／移动，要求重新入局，不回退近似预设。

## 客户端显示与操作

SceneController 统一世界手势；RemoteScene 返回当帧命中／绘制输入。SceneView／SceneAssets 共用原地形、Pops、Warp、原对象／单位动画及光照，原服模式与显示模式分开。地图交互和传送点解锁／菜单／旅行都等原服，面板不推断成功。

自动地图只有 AutomapExploration／AutomapCatalog／SceneAssets／SceneView 一套揭示、标记、缓存和绘制；RemoteTown 适配原服活动区域。同局返回记忆保留，新局清空；不读写 .d2xmap。细节见[自动地图](../gameplay/world/AUTOMAP.md)。

自研宿主已接自然边界／UNIT_TILE换区、本人门户／传送点、普通物件交互、中立NPC与第一幕敌对人口，具体资格由travel／objects／npcs／population领域持有；客户端仍消费同一套原协议。人口身份与准入见[怪物模块](MONSTERS.md)，中立单位不可替换为敌人。完整区域加载不等于原服按需活区，完整房间时序及动态占位仍有缺口。

generateArea把原生完整16位地形碰撞保留在Grid.fullTerrainCollision，权威移动经共同movementMask消费；既有未提供该字段的粗网格／原服显示仍沿原字节路径。区域边界拒绝外部Grid指针，内容准备后才复制给实例，避免生成器销毁后悬空或跨实例借用。

## 限制与后续

原地形／碰撞对照样本及生成边界见[五幕地图](../gameplay/world/MAPS.md)与[原版对照](../architecture/MULTIPLAYER.md#原版-dll-对照方法)。长距离绕障、动态阻挡、完整房间时序、特殊旅行资格、全种子、精确客户端照明及 Linux 未全面认证。传送点点击／动画修正已随当前Release入包，第二幕样本未专项复验，见[基线](../../BASELINE.md)。
