# 地图、区域与地图界面

五幕普通区域1–136共用NativeMapGenerator和当前MPQ。自研宿主与客户端共用原选砖／碰撞算法；宿主按真实房间引用物化，客户端按实际原包顺序重放。宿主通过原LOADACT／0x07传种子、幕及锚点，客户端自行重建，不共享C++地形快照。权威内核持有纯值房间输入、独立物化状态及碰撞。当前自研进入保存幕的城镇，使用原DS1出生标记，不以inspectionArrival替代。实现范围以[基线](../../BASELINE.md)及本页为准，原版地形依据由世界地图专题维护。

原版地形对照及剩余验证范围见[五幕地图](../gameplay/world/MAPS.md#地形准确性与恢复边界)，DLL重放和实机排查步骤见[地图对照排查](../development/MAP_PARITY.md)。宿主已移除固定全区碰撞／随机回放，改用逐房间纯值物化；首差、原DLL及实机证据见[实机首差](../gameplay/world/MAPS.md#实机首差与合成调用边界)。

## 所有权与入口

PD2扩展预设区域复用本模块的生成、房间物化、碰撞和原生出口；区域清单、额外DS1标记、编译怪物身份，以及地图物品／词缀／事件的执行边界统一见[PD2地图基线](../pd2/MAPS.md)。管理旅行只提供调试入口，不能作为普通开图玩法的完成依据；实际开图消费者见[地图词缀](../pd2/MAP_PROPERTIES.md)。

| 入口 | 职责 |
| --- | --- |
| [world_catalog.cpp](../../src/content/world/world_catalog.cpp) | Levels／LvlTypes／LvlPrest／LvlMaze／LvlSub 等只读定义 |
| [native_map.cpp](../../src/world/native_map.cpp)、[native_room_activation.cpp](../../src/world/native_room_activation.cpp) | 原布局、房间生命周期、有序近邻、真实 DT1 与完整碰撞 |
| [generated_area.cpp](../../src/world/generated_area.cpp)、[room_terrain.cpp](../../src/world/room_terrain.cpp) | 准备原型／内容描述与无图像的房间输入；后者以独立会话执行实际选砖、碰撞和随机 |
| [game_content.cpp](../../src/hosting/game_content.cpp) | 读取当前MPQ规则、出生点及物件描述，交付未物化碰撞与纯值房间输入；不执行玩法 |
| [remote_town.cpp](../../src/client/remote_town.cpp) | 原幕／种子、0x07／08及位置事件重放、锚点校验、活动 Map／Grid 和交互值 |
| [remote_control.cpp](../../src/client/remote_control.cpp) | 原整数目标／GUID请求、靠近、旅行；显示路径不写权威位置 |
| [automap_exploration.cpp](../../src/client/automap_exploration.cpp) | 本局区域探索掩码，城镇全揭示／野外可见格规则 |
| [automap_store.cpp](../../src/hosting/automap_store.cpp) | 从本地角色租约路径保存本人观察缓存，校验角色／难度／内容／种子；不读写玩法或D2S |
| [world_renderer.cpp](../../src/presentation/world/world_renderer.cpp)、[scene_assets.cpp](../../src/presentation/scene_assets.cpp) | 公共地形／单位绘制、排序／原图与灯光资源 |

0x07／08只维护客户端视野引用；本人坐标选择当前房间／区域，不额外建立服务器 CLIENT_IN_ROOM 引用。房间事件历史缺口、生成失败或锚点失配明确停止显示／移动，要求重新入局，不回退近似预设。

## 地图缓存与分帧准备

单机的hosting与客户端共用同一Archives挂载；server内核不读取MPQ。sharedWorldCatalog按挂载／三难度复用不可变世界表，TileLibraryCache按规范化路径共享已解码DT1的弱引用，地图和本地缓存持有资源寿命；新增挂载会清除共享条目。NativeMapGenerator按LevelType／Dt1Mask复用只读瓦片选择索引，随机流仍归每个生成器／房间；已分配房间另有坐标分桶，clientRoom只检查命中桶并保留重叠歧义拒绝。

AreaGenerator准备静态内容描述及三层房间引用传播所需的不可变输入；离线Map的碰撞／活动随机不进入权威。prepareWorldAreaContent交付的地形全为未物化0xffff，实际房间创建时才生成碰撞、逻辑格、地板记录及活动初值。原型解码和离线描述投影仍可能耗时；既有4毫秒软预算只分隔描述投影操作，不约束全部解码开销。首个城镇同步准备，相邻区域由world提前请求。

world另以本人实际房间和共同RoomLayout.nearRooms提前请求已准备邻区中近邻房间接触的出口依赖，依据D2MOO DrlgActivate::DRLGACTIVATE_RoomExPropagateSetStatus／DRLGACTIVATE_ChangeClientRoom的近邻传播。内容仍按区域交接，但选砖／碰撞在实际创建事件执行；未接入内容的近邻仍可在同幕纯值会话中建立正确地形状态，内容接入后复用该状态。

宿主NPC、战斗人口、环境单位和任务内容准备按Archives挂载复用不可变MonsterCatalog及AnimDataTable，首次完成后才发布，追加挂载清除缓存。原先环境单位逐类型构造完整目录及区域内多入口重复解析已移除；难度／区域等级、精英随机、人口身份与实体规则仍逐请求准备，不缓存随机结果。原型解码、内容准备和原图上传仍可能阻塞，不据缓存改动宣称无卡顿。

RemoteTown按约4毫秒软预算消费原顺序地图事件，未处理完时即使网络revision不变也继续推进；入局后另按25Hz调用客户端DRLG_Update推进已有引用的邻房预载，网络没有新revision时也不停止这条生命周期，不创建额外视野根；房间锚点与事件缺口校验保留，只用已物化房间选当前显示区域。地形修改的dirty状态跨帧保留；只在实际物化／释放地形或当前级别变化时重建快照，纯视野引用计数变化、同级房间内移动不再单独重建。SceneAssets以调色板／DT1瓦片身份复用Sprite，并持有对应库，避免房间集合变化时重复对已有瓦片逐像素哈希；GPU内容缓存仍负责真正的新图上传。

服务端活动房间由[world::RoomRuntime](../../src/server/systems/world/rooms.cpp)持有，使用生成器导出的原始有序near-room列表，包含跨级邻接，不用距离环补猜。NativeRoomActivation在d2x_navigation中为双方共用纯状态机；每幕保留独立会话，新增区域只扩展图，不清空原引用。目的房间初始化只物化目的房间，随后ChangeClientRoom先增加新引用再释放旧引用，传播0／1／2／3级状态。碰撞按真实房间恢复，未物化格保持0xffff，快照外沿没有房间归属的补边不参与跨区碰撞。Population／Objects只准入已加载房间。普通／特殊怪物与NPC共用休眠节点，回访以同GUID重新构造并完成保存状态与修饰器恢复；等待中的实体暂缓公开。忙碌或未结算单位继续保留房间，完整特殊AI／MapAI及地面物品休眠尚未实现；物件按自身规则重新初始化并重绑身份，细节归[人口](../gameplay/monsters/POPULATION.md)。原07／08按玩家实际近房间增减，表示单客户端视野，不证明服务端活动房间已卸载。

1.13c实机及导入表确认DRLG_Update由客户端调用；bClient=0下的7 tick分支不是原版D2Game的预取调用链，宿主不再执行它。卸载依据为本地D2MOO `Game.cpp::GAME_Update`（每12帧检查、按Act活动房间头插链遍历、无客户端计数超过10才考虑卸载）、`D2Dungeon.cpp::D2Common_10081_GetTileCountFromRoom`及`DRLGACTIVATE_TestRoomCanUnTile`的城镇／Rocky Summit／portal保留条件。D2X另外保留正在提交动作、伤害、弹体或掉落的房间，避免事务背压时释放其碰撞。保留谓词只读资格；NativeRoomActivation另调用tryRetire，完成可暂缓的怪物压缩／效果发布，再压缩物件，成功后才释放碰撞和房间随机。普通休眠节点同时保存CorpseState（不可用、已搜尸、死亡状态集合）及自复活次数，回访不会重新开放已消费的尸体。传送、WP、复活和新入场先激活真实目的房间，再查落点。

宿主[AreaPreparation](../../src/hosting/area_preparation.cpp)以独立MPQ句柄／缓存准备不可变区域蓝图，不共享可变Archives、地图生成器或UI回调；主循环只接收完成结果并按每局原请求顺序提交，离局取消票据，过期结果不进入新局。原型按区域及三层邻接预解码；RoomTerrainSession只接收纯值输入，不保留MPQ句柄或图像。实际创建／释放发生在world所属会话；同一帧等待内容不重复推进卸载计数。全部原版资源回收和SUNITINACTIVE结构仍非逐字复刻，蓝图及休眠实体内存也尚未逐房间回收。客户端原图上传仍在渲染线程，不能仅据工作线程宣称所有卡顿消除。

两端释放活动瓦片／碰撞，重入均按保留的图层、原始种子及当前共享边界状态重新物化。旧Run／initialRoomRandom恢复已删除；已开墓墙从当前实际选中的地板／墙／阴影记录重投影，物件碰撞由objects按当前模式重建。InitRoomGrids重置瓦片流，选砖后再分出活动房间种子；后续玩法消费只更新活动随机。对照范围见[实机核对](../gameplay/world/MAPS.md#实机首差与合成调用边界)。

Play-Diagnostic.cmd的stderr.log中[area-load]记录宿主terrain_ms／content_ms与超过一个25Hz tick的客户端map_ms，供定位剩余开销；诊断入口见[调试管道](../development/DEBUG_PIPE.md)。性能需按当前实现和相同地图／种子／人数核对。

宿主房间引用／状态仍在原提交点逐次发布；静态碰撞和人口布局仅在该幕创建／释放房间、接入区域蓝图或显式墓墙变化后重新投影。变化后按原遍历顺序刷新整幕活动房间，覆盖共享边界和近邻集合变化；无地形变化的固定步不重复复制房间样本，不缓存或重置活动随机。

## 客户端显示与操作

### 共用寻路

D2MOO在D2Common/Path/Path.cpp的公共分派表统一AStar(1)、Toward(2／13)、Straight(7)、WallFollow(15)；MonsterMode::sub_6FC65680只选择类型和失败重试策略，不在D2Game重复实现算法。D2X对应入口为[Grid::nativeUnitPath](../../src/world/navigation.cpp)及[WallFollow](../../src/world/navigation_wall_follow.cpp)，同属d2x_navigation；怪物权威requestMove与RemoteScene按同一个入口计算。共享函数只接收坐标、碰撞值和路径参数，不持有MPQ、网络、服务端单位或UI。WallFollow的左右交替绕障、合流检测、距离上限和折点简化按本地D2MOO 1.13c注释实现；单位目标最小40搜索上限也在公共函数内，双方不各写一份。

双方共享算法而不共享运行状态：宿主决定移动、目标和实际路径类型，自有客户端用原67／68已公开的参数生成表现路径。原环绕算法另由nativeMonsterCirclePath提供；特殊技能／击退不冒充普通Toward。[nativeAStarPath](../../src/world/navigation_astar.cpp)按D2MOO的200节点上限、正交2／斜向3代价、八邻接顺序、同分先插、已访问节点代价传播及78折点边界实现；不再用通用A*的全图搜索／任意直线简化代替原版路径。

玩家权威移动、旅行后续路线和RemoteScene都调用nativePlayerPath（Straight 7，Toward 73步，18格内AStar回退）。玩家及普通非玩家动态路径执行共用[NativePathStep](../../src/world/native_path_step.cpp)：保存16.16定点位置，每个25Hz游戏帧从定点位置重算到下一折点的12位量化方向，单帧最多推进到一个折点，按Step.cpp实际进入的子格检查对应MPQ移动碰撞轮廓；碰撞后停在最后接受格的中心。客户端渲染帧只累积25Hz时间，浮点Vec仅为输出投影，不按渲染帧重算方向或用于DDA经过格扫描。世界／区域原点仅用于碰撞格转换，换房间平移保留定点位置，旅行发布背压回退同时恢复步进状态。128个方向值以数学公式独立推导，源码核对与D2MOO表逐项相同；依据为Step::PATH_GetDirectionVector／sub_6FDAB810／sub_6FDACEC0和PathMisc::D2Common_10226／PATH_ComputeVelocityAndDirectionVectorsToNextPoint。普通玩家路径按D2Common_10226保留剩余折点；房间物化／卸载和无关物件变化不重算途中路径。目标单位换格、显式新命令及原单位目标部分路径续算分别处理；实际新障碍由每步进入格的碰撞检测阻止。无路径的普通攻击／踢击返回不可用，不无限挂起。01／02与03／04明确区分走／跑，自动近战追击和拾取按PlrMsg使用Run，体力不足仍由权威降为走。5F可达性使用同一跨区碰撞域中的原WallFollow 15／77步；部分AI查询及囚犯路线仍有独立用途的Grid::path调用。普通单位实时占位与寻路时的目标脚印撤销已接入，具体范围见下节；门的专用目标偏移、完整移动速度修饰的客户端投影和原版完整房间内存回收尚不等价，不能据共享函数宣称所有种子逐帧一致。

自然边界沿同一完整目标寻路，不再使用接近点／接缝扫描／人为跨界段。Grid::linkedNavigation临时借用已准备、步行相连区域的真实完整碰撞，通过整数原点转换调用同一nativePlayerPath；不复制或修改地形，也不跨越未准备区域或warp。玩家每步由NativePathStep检查进入格的公共movementClear，travel::walked按实际占用子格的RoomLayout归属提交换区事实，保留连续位置、剩余路径、目标GUID和交互接近点；事件背压回退该步、保留路径重试。碰撞版本仅参与显式命令去重；相邻区域的加载／卸载不自动重算已执行的路径。原依据为D2MOO Path.cpp目标房间准备、D2Collision::COLLISION_GetRoomBySubTileCoordinates和Step::PATH_RecacheRoomAtCoordIfNeeded。

原MPQ Objects[267]仓库的SizeX／SizeY均1、HasCollision0=1；寻路按真实碰撞和原定点步进处理，不删除仓库、缩小轮廓或放宽5F校验距离。斜向进入格的顺序由NativePathStep决定，不再混用浮点DDA与世界坐标舍入。

双方不因全图碰撞版本变化自动重算途中路径，按PathMisc::D2Common_10226及Step::PATH_RecacheRoomAtCoordIfNeeded保留剩余折点。共享算法、相同碰撞值和相同执行时序分别核对，不以其中一项替代另外两项。

普通非玩家权威及表现也接入NativePathStep；佣兵区域归属可随实际步行转换，保留剩余路径。远距主人移动历史、原100步参数及近处逻辑区限制统一见[佣兵](../gameplay/characters/HIRELINGS.md#原数据与执行入口)。敌对怪物跨级追击限制没有据此解除。

客户端RemoteTown保留snapshot.collision的完整16位值，与宿主一致，不再只消费低8位；房间选择使用与人物子格中心相符的四舍五入坐标。NPC单位靠近支持同幕步行相连区域中的真实GUID／坐标，不因目标在相邻级别拒绝原02／04；交談仍按原距离6／8门槛复验。公共算法不等于所有原版路径已认证，动态占位的特殊重叠／历史位交错、房间内存回收、怪物跨级追击及全种子／延迟组合仍有边界。

自然边界的区域归属与整数回包、子格中心必须使用同一坐标约定，避免提前换区后把路径起点落到源Grid之外。NPC在相邻级别的交谈仍须复验原距离及连通性，完整跨级追击和特殊占位交错另待闭合。

### 单位占位与入场

玩家准入不再复制固定`AreaDefinition.spawn`，而由`world::nativeActSpawnLocationEx`按当前MPQ房间／出生标记、区域随机、标记偏移(+3,+3)、size2／0x1c09及原扫描顺序选择落点。PlayerStore准入即保留碰撞资格，尚未完成客户端入场的后续角色也必须避让；离场解除资格，重新激活先复验空位。重入存档尸体另走原出生标记及size2／0x9c19／PosIncrement4，不再和活人复制同一坐标。没有空位明确失败，不退回固定出生点。

`spatial::navigationGrid`为原Grid算法提供临时碰撞视图，直接读取AreaStore维护的常驻原生格；寻路作用域撤自身／有效目标并按原顺序恢复。身体、尸体、物品及门户由各自发布入口维护，实际移动每进入一个子格即更新，失败旅行发布按实际写入回滚。普通玩家SmallUnit为十字0x80加中心0x1000；普通怪物、宠物及Wraith分别沿已准备的Unit／Pet／NoPresence模式。服务器死亡后无deadCol的单位改写3×3尸体位，客户端尸体沿无presence的十字；行走掩码不阻挡尸体，宠物也不能统一当成普通实体墙。位置按定点步进对应的`floor(position+.5)`投影，跨级脚印按实际房间归属及幕坐标查询。

`AreaStore::roomAtPosition`直接使用RoomLayout已解析的当前房间及loaded状态做半开边界判断，只有当前房间不接收该坐标时才按原roomNear顺序解析邻室；对应D2MOO `D2Dungeon.h::DUNGEON_GetRoomAtPosition／DungeonTestRoomGame`。该同步快路径不缓存跨提交的房间指针，不改变重叠脚印、跨级边界或未物化房间的拒绝规则。

玩家寻路排除自身与目标脚印，怪物只在原PathType具有0x800标志的1／7／15分支排除目标；实际进入子格时仅排除自身，目标仍参与阻挡。前一个单位提交移动、死亡、换区或离开后，下一个查询立即读取新状态，不等spatial的周期索引。移动不因其他单位换格重建尚未结束的路径，碰撞仍由NativePathStep逐格处理；旧骨墙几何旁路已由公共脚印替代。服务端旅行、复活、传送及5F落点消费同一常驻碰撞格，换幕保留0x1c89／PosIncrement5，旅行和复活输出背压重试重新寻位；复活／击退事务提交还会复验动态碰撞。RemoteScene从公开单位位置、动作和当前MPQ构造同类视图，不读取服务端隐藏状态。

依据为本地D2MOO `Clients::sub_6FC31EF0`、`D2Dungeon::DUNGEON_FindActSpawnLocationEx`、`Path::D2Common_10142`、`Step::sub_6FDAB810`及`SUnit::sub_6FCBDFE0`／`LEVEL_ChangeAct`。1.13c `D2Common` RVA359D0／35A2A／35A83确认玩家与怪物同走SetMaskWithPattern（读取动态路径+0x48并调用4BE90），与本地1.10f转录把玩家放在SetMaskWithSize分支不同；RVA361BD–361CE确认玩家0x80／0x1c09及Straight。DLL摘要见[资料来源](../resources/THIRD_PARTY.md#原版113c静态与开发对照证据)。普通出生与走跑不能据此允许多个活人同格；特殊技能使用各自掩码，不增加统一的“禁止重叠”规则。服务端保留实际Set／Reset历史，客户端从公开状态投影；强制位移重叠、特殊模式切换、门的专用目标偏移及全种子／延迟下的原版双端行为仍未完整认证。

客户端动态脚印从本次公开状态收集到连续数组，按坐标排序并以位或合并，不改变单位处理顺序和随机。服务端不再执行此逐次投影，按原发布点写常驻格，保留历史共享位清除、失败恢复及临时撤脚印的顺序；具体维护入口、回滚与计量范围见[动态碰撞格](SERVER_SYSTEMS.md#动态碰撞格的原版语义与迁移边界)。状态移除子体队列为空时不创建碰撞采样器，非空时沿原排空及背压边界读取当前格。

### 走跑动画与路径结束

RemoteScene的走跑判定取本帧路径推进后是否仍有路径，不再以最近200毫秒内的位置变化延长动画。25Hz两次推进之间保留走跑模式；到达、受阻或取消的同帧转为城内待机5／城外待机1，保留最后移动朝向。位置修正本身不触发走跑。脚步声消费同一状态，图形和帧数读取当前MPQ。

本地显示模式与原协议动作分开：同一走／跑模式改目标不重置相位，走／跑／待机切换从各自动画起点开始，不再按全局渲染时间取循环帧。冻结暂停相位，技能、死亡及脚本序列仍沿自身动作时钟，骑乘锚点与所显示的动画使用同一相位。25Hz逐次累加原8.8动画帧，移动速度修正变化只影响后续推进，不回算已播放相位。

`gameplay/actions/movement_timing.hpp`声明共用纯计算，实现在角色属性计算单元；服务端角色属性和客户端预测／动画共用原整数运算。人物走／跑动画基础速率为213／101，物品FRW使用150递减式，叠加普通velocitypercent、当前MPQ CharStats走跑比值的临时跑步加成，下限25；路径速度与动画速率分别按原8.8截断，动画上限32767。不能直接把AnimData速度当走跑速度。RemoteUiClients复用已有装备／护甲重量、状态和被动属性投影，材料未齐时只使用明确的基础走跑并报告修正不可用；他人的完整私人装备／被动修正仍不可视作已知。变身按当前MPQ States／MonStats／MonStats2及原模式回退选择速度与已准备的怪物动画速率；服务端普通步进也消费已准备的ShapeProfile、当帧真实状态和同一速度百分比，不继续使用人形基础速度。

依据为D2MOO `PathMisc::D2Common_10226`在路径耗尽时立即ResetMovement并返回FALSE，`Units::UNITS_ChangeAnimMode`仅在模式变化时调用`UNITS_SetAnimStartFrame`。同快照1.13c D2Client RVA1C1AE读取原路径推进返回值，1C2F6–1C2FC分支在结束时进入1C47B–1C4AC，按城镇选择1／5并调用ChangeAnimMode；D2Common RVA33920／33938／33952／3395B确认模式比较及重置起点。静态依据与DLL摘要见[资料来源](../resources/THIRD_PARTY.md#原版113c静态与开发对照证据)。本次未启动游戏，全部装备／变身、冻结与网络延迟组合的逐帧原客户端运行对照仍未完成。

### 到达与交互

UNIT_TILE入口按原D2MOO PlrMsg的D2Common_10399单位距离判断：超过50拒绝，超过4先走近，不大于4进入目的区域；不要求站到为目的地准备的半格落点。2026-10-13源码修正了旧旅行等待路径清空后再用0.1格误差比较半格arrival的问题，复用nativeUnitDistance，任务限制与目的房间激活仍由权威旅行处理。

原D2MOO Level::LEVEL_RemoveClient先移除单位再撤房。当前08延后到旅行事实与本轮所有单位投影完成后，07仍先揭示，保留待撤房集合避免重复揭示。2026-10-13原版Join连接D2X宿主时，WP回城收到旧房间08早于单位移除及本人15，随后停止通信且没有69正常退局；这些消息由D2X宿主产生，原版日志没有对应异常，尚不能据此认定掉线根因。原版双端进洞／回城的完整稳定性仍待核对。

拾取双方共用nativePickupReach：D2Common_10399单位距离不大于4，尺寸为玩家2／物品1，另检查0x0804缩短交互射线；移除服务端原有的1.8格欧氏距离门槛。02／04支持地面物品目标。物件使用UNITS_IsObjectInInteractRange对应几何；交谈以单位距离6判断，原13请求在距离7–8时排队走近。普通NPC单位路径按Path.cpp初始化nStepNum=0、PathMisc::sub_6FD5DB70单位距离不大于0停止，预测对普通02／04也逐步执行该判据；不把菜单资格6作为04移动终点。客户端已有足够近的直接点击仍按原13资格，远点靠近在当前本地普通单位路径到达后发送13，服务端复验交談资格；不把稀疏96位置采样当作请求门槛。移除枚举交互距离环并选最近候选点的第二套寻路，物件接近直接使用原单位目标路径。物件13与NPC13都保留待执行意图，到达后执行；新移动／攻击／交互、死亡、离区或目标失效会使旧意图失效。背压重试不重复执行已经成功的交互。

普通攻击起手、伤害释放和客户端就绪检查共用playerMeleeReach，按单位距离、武器rangeadder及原0x0804缩短射线判断，不再把整条行走碰撞轮廓当成近战视线；追击起手包含移动目标的范围增量。客户端以本人及目标的本地连续显示位置检查起手和执行靠近；按原生目标子格变化重算单位路径，不等待本人96／移动回包。服务端独立检查同样的原生子格、资格及路径，命中和伤害仍只认服务端。物件靠近从当前显示位置启动原02／04 GUID路径，不再预选地面终点或要求部分路径已经满足交互资格；旧interactionApproach／RemoteTown::interactionApproachPoint接口已删除。物件部分路径到头且尚未进入交互范围时，双方共用nativeTargetPath按真实目标类型续算Straight；依据为PathMisc::sub_6FD5DB70／D2Common_10236，不能把它解释为无条件改用WallFollow。公共nativePlayerPath另按Straight的0x1000策略准备被阻挡目标：原三条有序正交射线及玩家近目标修整，对应PATH_PreparePathTargetForPathUpdate／PATH_FindValidTargetCoordsByMovingOrthogonally；后者9×9表由公式独立推导，81项逐项核对相同。

### 本人位置回包

宿主0F与18／95／96投影的`PathGetFirstPoint`是D2Common的`tTargetCoord`，不是`PathPoints[0]`。点目标发布可达终点，单位目标保留该单位坐标；`player_projection.cpp`不再把当前第一个拐点当成协议目的地。客户端仍保留原始采样供核对，不借内部宿主状态修正显示。

本机原1.13c D2Client（MD5 `f5860c629d309b8fc1f96174babcf633`）的S2C表RVA `0xDDE60`把18／95／96分派到`0xAECA0`／`0xAEBE0`／`0xAD950`，三者调用`0x2A1B0`：记录服务端整数位置，在每轴偏差超过动作阈值和延迟补偿后，经`0x2A3EA–0x2A40D`上报5F；本人分支不调用他人单位的重新定位。模式1／3／其他的阈值分别为3／7／5，加`(latency+50)>>7`，另有平方距离小于100时的位置趋势判断。15独立走`0xAF640`重新定位。以上为同版本静态分支核对。

D2X的RemoteWorld保留18／95／96的权威位置和路径校验值供诊断／路径校验；RemoteScene不再用这些采样拉回本人，也不拿接近的校验目标替换本地目的地。结束拾取／交谈／移动请求以及无进展超时只停止本地路径，不把旧资源坐标当成传送。RemoteControl按上述距离／趋势核对新采样，通过RealmSession发送原5F；不会取消当前交互或自行修改权威。原15、入场重建及真正的动作／区域变化仍各走原有处理。原版跨房间可见性条件及全部延迟组合尚未实机认证。

SceneController 统一世界手势；RemoteScene 返回当帧命中／绘制输入。SceneView／SceneAssets 共用原地形、Pops、Warp、原对象／单位动画及光照，原服模式与显示模式分开。地图交互和传送点解锁／菜单／旅行都等原服，面板不推断成功。

自动地图只有 AutomapExploration／AutomapCatalog／SceneAssets／SceneView 一套揭示、标记、缓存和绘制；RemoteTown 适配原服活动区域。同局返回记忆保留；单机／TCP/IP沿本地角色租约basename，Realm沿客户端保存根目录／Realm／角色名，按实际seed从原版.map／.ma0–3恢复本人图元；旧D2X探索格式停用。细节见[自动地图](../gameplay/world/AUTOMAP.md)。

自研宿主已接自然边界／UNIT_TILE换区、本人门户／传送点、普通物件交互、中立NPC与第一幕敌对人口，具体资格由travel／objects／npcs／population领域持有；客户端仍消费同一套原协议。人口身份与准入见[怪物模块](MONSTERS.md)，中立单位不可替换为敌人。内容蓝图仍按区域保留，完整单位休眠时序及特殊占位交错仍有缺口。

generateArea的离线碰撞仅留在hosting内容描述中；实际RoomTerrainSession物化结果写入权威Grid.fullTerrainCollision，权威移动经共同movementMask消费完整16位值。既有未提供该字段的粗网格／原服显示仍沿原字节路径。内容交接不保留生成器Grid指针，避免销毁后悬空或跨实例借用。

## 限制与后续

原地形／碰撞对照样本及生成边界见[五幕地图](../gameplay/world/MAPS.md)与[原版对照](../architecture/MULTIPLAYER.md#原版-dll-对照方法)。长距离绕障、动态阻挡、完整房间时序、特殊旅行资格、全种子、精确客户端照明及Linux仍缺完整对照；传送点资格及动画边界见[物件](../gameplay/world/OBJECTS.md#自研传送点)。
