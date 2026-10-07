# 地图物件与交互

Objects／ObjGroup／Levels／Shrines及原DS1、COF／DCC提供身份、尺寸、模式／动画、名称、AutoMap及灯光。静态地形物件与原服可交互GUID分开；操作、开门、井水、神坛、宝箱锁／陷阱、掉落和任务效果由原服执行。

## 碰撞与靠近

共同Grid保留DT1地形层及独立对象层，按真实SizeX／Y、HasCollision0–7、BlocksLight0–7、BlockMissile等模式字段更新；不按画面石头类别猜阻挡，不覆盖重叠物体／原墙。

交互几何核对D2MOO UNITS_IsObjectInInteractRange／D2Common_10399：原整数子格、对象矩形及外侧条带，OperateRange不当中心圆半径。接近点查询完整行走体积与原射线；范围预览不能证明原服操作成功。

## 原服适配

RemoteTown从真实物件／目标和原Selectable／TARGETABLE投影资格；SceneController提交靠近／0x13。神坛InteractType结合Shrines.Code与TBL ShrId显示真实名称，不本地授予效果。箱子0x77、传送点0x63和旅行位置等原回包分别确认。

传送点OperateFn23在mode1／2的点击例外与启动后ON动画／灯光是最新未入包源码，详见[联网修正](../../modules/NETWORK.md#尚未入包的源码修正)；显示投影不改副本模式、碰撞或解锁。

## 限制与证据

宝箱上锁／钥匙、特殊箱掉落／陷阱、全部神坛效果、特殊任务物件及多场景操作未完整认证。旧grant-shrine／本地宝箱生成入口已删除；保留world/chest与content掉落资料供独立分析，不作为联机结算器。

本地D2MOO Objects／ObjMode／ObjRgn、ItemMode、Items、D2Collision及当前MPQ核对原语义。有限法力神坛、门／传送点／门户观察见[联网记录](../../modules/NETWORK.md#既有有限证据)，地形与生成限制见[地图](MAPS.md)。
