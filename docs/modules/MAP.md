# 地图、区域与客户端地图基线

更新：2026-10-04。本页维护接口；五幕类型、原参数、特殊地图与验证统一见[地图规则](../gameplay/world/MAPS.md)。

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

世界物件定义与活状态尚未完全拆分，任务仍由宿主协调。世界单位／弹体、屋顶及光照仍有会话适配，SceneView／SceneController未完全隔离。多观察者、并行活区、卸载与联网消息未实现。

DRLG随机流／逻辑网格、缺损资源、资格和客户端限制见[地图规则](../gameplay/world/MAPS.md#暂缓难点)。D2S v96及探索侧文件v1不增加地图字段；运行地图规则指纹已更新，探索另按实际地形／房间指纹判断兼容。
