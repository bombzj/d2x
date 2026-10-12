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
| [automap_store.cpp](../../src/hosting/automap_store.cpp) | 从本地角色租约路径保存本人观察缓存，校验角色／难度／内容／种子；不读写玩法或D2S |
| [world_renderer.cpp](../../src/presentation/world/world_renderer.cpp)、[scene_assets.cpp](../../src/presentation/scene_assets.cpp) | 公共地形／单位绘制、排序／原图与灯光资源 |

0x07／08只维护客户端视野引用；本人坐标选择当前房间／区域，不额外建立服务器 CLIENT_IN_ROOM 引用。房间事件历史缺口、生成失败或锚点失配明确停止显示／移动，要求重新入局，不回退近似预设。

## 客户端显示与操作

### 共用寻路

D2MOO在D2Common/Path/Path.cpp的公共分派表统一AStar(1)、Toward(2／13)、Straight(7)、WallFollow(15)；MonsterMode::sub_6FC65680只选择类型和失败重试策略，不在D2Game重复实现算法。D2X对应入口为[Grid::nativeUnitPath](../../src/world/navigation.cpp)及[WallFollow](../../src/world/navigation_wall_follow.cpp)，同属d2x_navigation；怪物权威requestMove与RemoteScene按同一个入口计算。共享函数只接收坐标、碰撞值和路径参数，不持有MPQ、网络、服务端单位或UI。WallFollow的左右交替绕障、合流检测、距离上限和折点简化按本地D2MOO 1.13c注释实现；单位目标最小40搜索上限也在公共函数内，双方不各写一份。

双方共享算法而不共享运行状态：宿主决定移动、目标和实际路径类型，自有客户端用原67／68已公开的参数生成表现路径。原环绕算法另由nativeMonsterCirclePath提供；特殊技能／击退不冒充普通Toward。[nativeAStarPath](../../src/world/navigation_astar.cpp)按D2MOO的200节点上限、正交2／斜向3代价、八邻接顺序、同分先插、已访问节点代价传播及78折点边界实现；不再用通用A*的全图搜索／任意直线简化代替原版路径。

玩家权威移动、旅行后续路线和RemoteScene都调用nativePlayerPath（Straight 7，Toward 73步，18格内AStar回退）。世界坐标与网格中心只在入口转换，逐帧经过的格子用segment检查；不在每个小数步重跑有偏的PATH_RayTrace。追击保留目标未换格且碰撞版本未变的路径，目标移动或对象碰撞变化再重算；无路径的普通攻击／踢击返回不可用，不无限挂起。01／02与03／04明确区分走／跑，自动近战追击和拾取按PlrMsg使用Run，体力不足仍由权威降为走。5F可达性验证、部分AI查询及囚犯路线仍有独立用途的Grid::path调用；动态单位占位、门的专用目标偏移和完整房间状态尚不等价，不能据共享函数宣称所有种子逐帧一致。

### 到达与交互

拾取双方共用nativePickupReach：D2Common_10399单位距离不大于4，尺寸为玩家2／物品1，另检查0x0804缩短交互射线；移除服务端原有的1.8格欧氏距离门槛。02／04支持地面物品目标。物件使用UNITS_IsObjectInInteractRange对应几何；交谈以单位距离6判断，原13请求在距离7–8时排队走近。物件13与NPC13都保留待执行意图，到达后执行；新移动／攻击／交互、死亡、离区或目标失效会使旧意图失效。背压重试不重复执行已经成功的交互。

普通攻击起手、伤害释放和客户端就绪检查共用playerMeleeReach，按单位距离、武器rangeadder及原0x0804缩短射线判断，不再把整条行走碰撞轮廓当成近战视线；追击起手包含移动目标的范围增量。客户端以当前连续显示位置检查起手，命中和伤害仍只认服务端。物件接近点也从当前显示位置规划，不从滞后的服务端采样重新选边。

### 本人位置回包

宿主0F与18／95／96投影的`PathGetFirstPoint`是D2Common的`tTargetCoord`，不是`PathPoints[0]`。点目标发布可达终点，单位目标保留该单位坐标；`player_projection.cpp`不再把当前第一个拐点当成协议目的地。客户端仍保留原始采样供核对，不借内部宿主状态修正显示。

本机原1.13c D2Client（MD5 `f5860c629d309b8fc1f96174babcf633`）的S2C表RVA `0xDDE60`把18／95／96分派到`0xAECA0`／`0xAEBE0`／`0xAD950`，三者调用`0x2A1B0`：记录服务端整数位置，在每轴偏差超过动作阈值和延迟补偿后，经`0x2A3EA–0x2A40D`上报5F；本人分支不调用他人单位的重新定位。模式1／3／其他的阈值分别为3／7／5，加`(latency+50)>>7`，另有平方距离小于100时的位置趋势判断。15独立走`0xAF640`重新定位。静态证据在忽略目录`artifacts/movement-alignment-20261012/native-client-position-verification.txt`，未运行原版游戏。

D2X的RemoteWorld保留18／95／96的权威位置和路径校验值供诊断／交互约束；RemoteScene不再用这些采样拉回本人，也不拿接近的校验目标替换本地目的地。结束拾取／交谈／移动请求以及无进展超时只停止本地路径，不把旧资源坐标当成传送。RemoteControl按上述距离／趋势核对新采样，通过RealmSession发送原5F；不会取消当前交互或自行修改权威。原15、入场重建及真正的动作／区域变化仍各走原有处理。原版跨房间可见性条件及全部延迟组合尚未实机认证。

SceneController 统一世界手势；RemoteScene 返回当帧命中／绘制输入。SceneView／SceneAssets 共用原地形、Pops、Warp、原对象／单位动画及光照，原服模式与显示模式分开。地图交互和传送点解锁／菜单／旅行都等原服，面板不推断成功。

自动地图只有 AutomapExploration／AutomapCatalog／SceneAssets／SceneView 一套揭示、标记、缓存和绘制；RemoteTown 适配原服活动区域。同局返回记忆保留；单机／TCP/IP沿本地角色租约basename，Realm沿客户端保存根目录／Realm／角色名，按实际seed从原版.map／.ma0–3恢复本人图元；旧D2X探索格式停用。细节见[自动地图](../gameplay/world/AUTOMAP.md)。

自研宿主已接自然边界／UNIT_TILE换区、本人门户／传送点、普通物件交互、中立NPC与第一幕敌对人口，具体资格由travel／objects／npcs／population领域持有；客户端仍消费同一套原协议。人口身份与准入见[怪物模块](MONSTERS.md)，中立单位不可替换为敌人。完整区域加载不等于原服按需活区，完整房间时序及动态占位仍有缺口。

generateArea把原生完整16位地形碰撞保留在Grid.fullTerrainCollision，权威移动经共同movementMask消费；既有未提供该字段的粗网格／原服显示仍沿原字节路径。区域边界拒绝外部Grid指针，内容准备后才复制给实例，避免生成器销毁后悬空或跨实例借用。

## 限制与后续

原地形／碰撞对照样本及生成边界见[五幕地图](../gameplay/world/MAPS.md)与[原版对照](../architecture/MULTIPLAYER.md#原版-dll-对照方法)。长距离绕障、动态阻挡、完整房间时序、特殊旅行资格、全种子、精确客户端照明及 Linux 未全面认证。传送点点击／动画修正已随当前Release入包，第二幕样本未专项复验，见[基线](../../BASELINE.md)。
