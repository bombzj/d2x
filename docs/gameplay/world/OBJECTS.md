# 地图物件与交互

Objects／ObjGroup／Levels／Shrines及原DS1、COF／DCC提供身份、尺寸、模式／动画、名称、AutoMap及灯光。静态地形物件与原服可交互GUID分开；操作、开门、井水、神坛、宝箱锁／陷阱、掉落和任务效果由原服执行。

## 碰撞与靠近

共同Grid保留DT1地形层及独立对象层，按真实SizeX／Y、HasCollision0–7、BlocksLight0–7、BlockMissile等模式字段更新；不按画面石头类别猜阻挡，不覆盖重叠物体／原墙。

交互几何核对D2MOO UNITS_IsObjectInInteractRange／D2Common_10399：原整数子格、对象矩形及外侧条带，OperateRange不当中心圆半径。接近点查询完整行走体积与原射线；范围预览不能证明原服操作成功。

## 原服适配

RemoteTown从真实物件／目标和原Selectable／TARGETABLE投影资格；SceneController提交靠近／0x13。神坛InteractType结合Shrines.Code与TBL ShrId显示真实名称，不本地授予效果。箱子0x77、传送点0x63和旅行位置等原回包分别确认。

传送点OperateFn23在mode1／2的点击例外与启动后ON动画／灯光已入包，详见[联网修正](../../modules/NETWORK.md#本批源码修正)；显示投影不改副本模式、碰撞或解锁。

祭坛表现源码补回公共绘制迁移时漏掉的未领取类型图／闪光层及物件COF阴影。真实Objects.SubClass／OperateFn识别祭坛，原服0x51的InteractType经既有shrineStateName映射到当前MPQ States.overlay1／2和Overlay DCC；只在原服mode0显示，mode1开始后移除，角色增益图仍只读原服状态。物件Overlay使用原静态落点及Height1，角色用Height2，secondary层先入队，两层各自按PreDraw排序；不使用物件X／Yoffset移动Overlay。

OperateFn2的非循环OP按D2MOO OBJECTS_OperateFunction02_Shrine所安排的(FrameCnt1+1)/25秒衔接ON，模式动画、Start／帧数及灯光继续读当前MPQ；仅改变显示模式，不写回原服副本、碰撞或授予效果。历史5c65e8d的world_renderer／state_overlay_view保留了上述类型层与阴影，6914308迁移公共世界队列时遗漏类型层，后续联机物件入口没有完整恢复；资源解码／COF合成沿用既有实现。另据ObjMode／SCmd修正0x4D物件操作通知：其技能字段实际是操作者GUID，等级字段是祭坛类型；只对玩家／怪物生成施法动作，不清除物件0x0E模式或生成伪施法效果。本批已构建入包，真实AF经验祭坛未领取图层及角色state137已观察；最终包实机领取另一个class2生命祭坛后mode1／nativeMode=true、actionSkill为空且本体保留，原服确认不可再点击及生命恢复；复验范围见联网交付记录，精确Overlay节拍及原版全部祭坛表现不据此宣称通过。

## 限制与证据

宝箱上锁／钥匙、特殊箱掉落／陷阱、全部神坛效果、特殊任务物件及多场景操作未完整认证。旧grant-shrine／本地宝箱生成入口已删除；保留world/chest与content掉落资料供独立分析，不作为联机结算器。

本地D2MOO Objects／ObjMode／ObjRgn、ItemMode、Items、D2Collision及当前MPQ核对原语义。有限法力神坛、门／传送点／门户观察见[联网记录](../../modules/NETWORK.md#既有有限证据)，地形与生成限制见[地图](MAPS.md)。
