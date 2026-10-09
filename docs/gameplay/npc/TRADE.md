# NPC交易与服务

普通货架由交谈0x38 action=1生成，赌博用action=2，9C action11／12更新商品；原0x32购买及multibuy高位、0x33出售、0x35维修、0x34凯恩批量鉴定由RemoteInventory提交。原服和自研宿主共用请求；价格、安置、资格、费用与结果均由服务端决定。

公共商店面板只消费货架／库存值，左键手持物品投向货物区提交出售，修理模式左键提交维修。canRequestSale与quote分开：报价未知，不能以零金币伪造原服价格。买卖／维修／批量鉴定等待0x2A明确结果，相关物品提前变化不单独认定成功。

当前NPC GUID、货架来源、本人所有权、Cursor和交互代次发送前复验；结束／重开同一NPC交谈也使旧组合失效。失败／超时不自动重试消费操作。原参数见[物品命令](../../development/DEBUG_PIPE.md#联网物品操作)。

普通经济、个人赌博、回购、批量、充能修理与货架周期已接；灌注／打孔／署名收原38(action=0,npc,item)，等原58服务结果，再投影原物品与个人任务字。具体执行、前置任务和限制只维护在[库存模块](../../modules/INVENTORY.md)。Akara免费重置已按原0x38 action0、parameter0及slot41资格实现；Warriv同一消息的parameter按NPC身份解释为旅行目的地，不能统一当物品GUID。第一幕任务授予的服务见[第一幕](../quests/ACT1.md)，罗格奖励与完整佣兵服务缺口见[佣兵](../characters/HIRELINGS.md)。玩家交易权威仍待实现；没有本地VendorRuntime或旧奖励旁路。原服历史有限证据见[联网记录](../../modules/NETWORK.md#既有有限证据)。

本地D2MOO PlrTrade、SUnitNpc／SUnitProxy、Items及SCmd提供原协议／字段／服务规则证据；报价共用原字段纯函数，未知数据不授权金额，完整原服逐整数认证仍有限。原货架图形与说明取当前MPQ，来源／许可见[资料来源](../../resources/THIRD_PARTY.md)。
