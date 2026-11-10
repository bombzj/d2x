# NPC交易与服务

更新于 2026-11-10。本页负责客户端商店交互与原 NPC 服务请求。货架、价格、成交与维修的权威规则见[经济](../items/ECONOMY.md)，任务物品加工见[任务物品](../items/QUEST_ITEMS.md)。

普通货架由交谈0x38 action=1生成，赌博用action=2，9C action11／12更新商品；原0x32购买及multibuy高位、0x33出售、0x35维修、0x34凯恩批量鉴定由RemoteInventory提交。原服和自研宿主共用请求；价格、安置、资格、费用与结果均由服务端决定。

普通开店由货架发布确认。零售 LoD 1.13c 的赌博面板在发送 `38/action 2` 时打开，空货架也可显示，随后以 `9C/action 11` 填入实际商品。`2A` 只用于成交或拒绝。依据为原版 1.13c 阿卡拉开店录包、D2Client RVA `4B07D–4B1F7` 以及 D2MOO `SUnitNpc`。

零售 1.13c D2Client RVA `F3BA0` 的 NPC 菜单表用 MPQ 字符串 `0xD46` 给基德、艾吉斯、艾柯、贾梅拉、安亚和尼拉塞克提供赌博入口。尼拉塞克的菜单只有赌博服务项。客户端菜单、客户端请求和宿主资格共用这组身份。

手动和调试入口经 `RemoteInventory` 提交赌博开店及购买。成交后的自动鉴定由会话层处理，收到本人新物品的 `9C/action 4、14` 或 `9D/action 6`，且物品有 `IFLAG_TARGET`、尚未鉴定时，发送 `37` 与新物品 GUID。`2A` 完成购买请求，`9D/action 21` 更新鉴定结果。AI 功能接口仅提供玩法动作。依据为 D2Client RVA `835A4/83774`、D2Game RVA `CEFE0` 和 d2decomped `docs/research/re/store.md`。

赌博购买拒绝按 `2A` 的结果码取当前 MPQ 文字，在鼠标处显示红色原因和「继续」。资金不足为 12，包裹无可用位置为 10，Cursor 已占用为 7。拒绝回执的空物品字段直接编码为 `FFFFFFFF`。D2X 图形单机已核对基德的资金不足弹框、继续购买、正常下架与鉴定。弹框的原版像素和多人货架保留仍待对照。依据为 D2Client RVA `4B420–4B543`。

公共商店面板只消费货架／库存值，左键手持物品投向货物区提交出售，修理模式左键提交维修。canRequestSale与quote分开：报价未知，不能以零金币伪造原服价格。买卖／维修／批量鉴定等待0x2A明确结果，相关物品提前变化不单独认定成功。

原版出售已装备物品可在2A成功后再发送9D action8卸装备；D2X接收端对已确认出售且已移除的原件只完成清理，不把迟到的移除消息重新投影为Cursor物品。部分维修的费用及恢复边界由经济页维护。

当前NPC GUID、货架来源、本人所有权、Cursor和交互代次发送前复验；结束／重开同一NPC交谈也使旧组合失效。失败／超时不自动重试消费操作。原参数见[物品命令](../../development/DEBUG_PIPE.md#联网物品操作)。

普通经济、个人赌博、回购、批量、充能修理与货架周期已接；灌注／打孔／署名收原38(action=0,npc,item)，等原58服务结果，再投影原物品与个人任务字。具体执行与限制分别见[经济](../items/ECONOMY.md)和[任务物品](../items/QUEST_ITEMS.md)。Akara免费重置已按原0x38 action0、parameter0及slot41资格实现；Warriv／Meshif同一消息的parameter按NPC身份解释为旅行目的地，不能统一当物品GUID。灌注／打孔／署名等服务的任务授予与消费见[任务系统](../quests/SYSTEM.md)所链接的各幕规则，罗格奖励与完整佣兵服务缺口见[佣兵](../characters/HIRELINGS.md)。玩家交易权威与取消／保存边界见[玩家交易](../items/PLAYER_TRADE.md)；没有本地VendorRuntime或旧奖励旁路。原服历史有限证据见[联网记录](../../modules/NETWORK.md#既有有限证据)。

本地D2MOO PlrTrade、SUnitNpc／SUnitProxy、Items及SCmd提供原协议／字段／服务规则证据；报价共用原字段纯函数，未知数据不授权金额，完整原服逐整数认证仍有限。原货架图形与说明取当前MPQ，来源／许可见[资料来源](../../resources/THIRD_PARTY.md)。

## PD2 CN城镇服务

PD2专项说明见[城镇服务：商店与金币](../../pd2/TOWN_CLIENT.md#城镇服务商店与金币)。
