# 地图、区域与客户端地图基线

更新：2026-10-05。本页维护接口；五幕类型、原参数、特殊地图与验证统一见[地图规则](../gameplay/world/MAPS.md)。

## 所有权与入口

| 入口 | 职责 |
| --- | --- |
| world/identity.hpp、plan.hpp | 关卡身份、生成选择、配方及目录，不含单位运行态 |
| content/world/world_catalog.* | 导入六张原地图表，按难度解释尺寸 |
| world/map_assembly.*、map_terrain.hpp | DS1拼接、边缘及popup元数据、共享DT1、已选变体与动画帧 |
| world/map.hpp | 导航／碰撞、房间、出生／到达点、任务墓墙修改与恢复 |
| world/region_store.* | 一局稳定槽位、缓存及种子，按需填充，不扩容／搬移已借用网格 |
| world/region_visibility.hpp | 连续邻区显示拓扑，楼梯／门户不串场，区别于模拟激活 |
| gameplay/areas/state.hpp、repository.hpp | 唯一区域战斗状态和休眠缓存，不按观察者复制 |
| gameplay/session/session_regions.cpp | 加载、城镇到达、NPC服务准备、随行单位与任务协调 |

初始化顺序为配方→稳定槽位→请求区及直接连续邻区→出口关联→任务协调；每个新槽位立即准备NPC／商店。已访地图保留，仅一个活区推进模拟。读档先准备幕城镇，再重映射库存ID。

## 客户端显示与操作

IMapClient绑定本地操作者；LocalMapClient按权威版本缓存场景／传送点值，空来源不返回开发目录。MapIntent提交传送点／出口／门户，权威复验接触、资格、落点及版本；自由Travel仅用于调试管道。

地图／自动地图手势位于presentation/world的map_controller、map_view、automap_view，出口与传送点UI在hud。探索由client/automap_exploration.*唯一维护；摄像机更新后采样可见原地形，规则与持久化见[自动地图](../gameplay/world/AUTOMAP.md)和[存档](SAVES.md#自动地图探索2026-10-04)。

IMapAssetSource只借用DS1、共享DT1图像及索引事实，不返回Region／Grid／MPQ／GPU。GPU、摄像机、显示时钟和屋顶渐隐由表现层拥有；玩法不读设备或图形。当前无卸载，解码数据寿命覆盖借用；卸载前需补失效机制。

地板／阴影使用稳定世界格归属，营地连接原道路，见[道路规则](../gameplay/world/MAPS.md#连续地形与道路接头)。熔岩仅切换显示帧，碰撞读frame0；KillEdge、主题与Pops／PopPad在构造阶段处理。

传送点菜单按Levels.Act／Waypoint及激活记录列出，用原图、TBL和sky PL2字体变换；正式菜单不暂停世界。新局／读档强制第零点开启，局前已开启直接ON，本局首次激活NU→OP→ON。界面见[HUD](../gameplay/ui/CLASSIC_HUD.md)，保存见[存档](SAVES.md#传送点初始化与恢复)。

## 限制与后续

第一幕1–39普通世界统一使用 `world/native_map.*`。离线 Map::load／assembleMap 和联网 RemoteTown 消费同一生成核心：离线完整准备连续区域后取当前关快照；联网逐条重放原服房间／位置事件，保留房间创建、释放及有序近邻。旧第一幕野外生成器、悬崖副本和道路铺设副本已删除；显式 `--map/--preset` 仍用于原资源预览，第二至第五幕保留既有入口。

`outdoor/native_act_layout.*`／`native_connections.*` 负责原点、Vis／Warp；`world/maze` 复用现有房间图；`world/retail/` 负责共同预设分房、身份扫描、户外网格、主题、瓦片与碰撞。参数和原图只从当前 MPQ 读取。瓦片流按房间初始种子／high=666重置；主题及延迟单位另用分配流。共享边缘、拐角 next 链和激活碰撞沿原顺序消费，快照输出实际选中的 DT1，不由表现层重抽变体或补墙角。

预设门及楼梯按原单位身份和 LvlWarp 链关联；连续出口拓扑允许可开启的门，实时导航仍保留关门碰撞。河流装饰只作客户端图形；Navi 保留中立身份。`preset_pops.hpp` 共用原屋顶分组、近邻墙数组和500ms渐隐／延迟；`warp_visibility.hpp` 共用原出口普通／亮面链。完整彩光、自动地图精确锚点和原客户端逐帧时序仍不在本次认证范围。

第一幕联网重建失败或4096条事件历史不连续时停止显示和移动，要求重新入局，不退回近似预设。其他幕预设匹配仍独立保留，第三幕原版布局及第二至第五幕随机地图还未认证。世界定义／活状态、任务协调、本地多观察者及卸载仍有架构工作。

本批原版1.13c对照122组全部通过，覆盖第一幕39区及三难度代表种子、兵营额外边界种子；检查房间顺序、预设文件、DT1文件／记录、完整16位碰撞、近邻和单位。五种子各136区域加载及出口关联通过。实际方法和范围见[原版对照](../architecture/MULTIPLAYER.md#原版-dll-对照方法)，包内检查见[地图验证](../gameplay/world/MAPS.md#本批验证)。

地图指纹为 `map-rules-v12-shared-retail-act1`；D2S v96、探索侧文件v1不变。旧探索按实际地形／房间指纹复验，失配明确清除；不向角色档添加私有地图字段。
