# 库存与装备显示

D2GS 唯一拥有物品、容器、金币、装备效果和库存事务。客户端解码已知字段、预览格子并发原请求；InventoryService 和 LocalInventoryClient 已删除。

## 入口与所有权

| 入口 | 职责 |
| --- | --- |
| [online_items.hpp](../../src/contracts/online_items.hpp) | 原服物品、所有者、位置、revision 及请求状态 |
| [remote_inventory.cpp](../../src/client/remote_inventory.cpp) | 当前 MPQ 位流／布局元数据，0x9C／9D与增量消费、操作资格及原请求 |
| [remote_ui_clients.cpp](../../src/client/remote_ui_clients.cpp) | 原物品副本 → InventoryView；组合操作逐步等待光标／请求结果 |
| [inventory.hpp](../../src/contracts/inventory.hpp)、[inventory_client.hpp](../../src/client/inventory_client.hpp) | 只读面板值、占格预览与语义意图 |
| [inventory_view.cpp](../../src/client/inventory_view.cpp) | 客户端值查询、格子命中和装备位置查询 |
| [item_display.cpp](../../src/content/items/item_display.cpp) | 名称／提示及显式显示上下文，公共面板与地面物品共用 |
| [presentation/inventory](../../src/presentation/inventory) | 包裹、腰带、箱子、方块、提示、鼠标消费与拖放 |

物品实例位置以原服为准。操作保留 UI 提交时的连接／游戏／区域／本人／交互上下文和 GUID／revision，发送端再次核对；死亡、换幕、目标移除或重新交谈使旧组合失效。关闭面板不会回滚已执行结果。

`accepted` 只表示入队／发送。Pending 等相关回包；结果未知不自动重试。买卖／维修／批量鉴定等待原0x2A，物品提前变化不单独证明服务成功。Cursor 与容器安置逐步推进，不创建客户端物品替代回包。

地面Pickup等待不锁住导航。新的有效移动／交互或施法提交后，旧拾取跟踪标Interrupted，清理其对应的显示移动目标；相关原服物品更新或超时也清理旧目标。迟到回包仍更新真实库存，不将Interrupted或Updated当作拾取成功。其他库存组合仍保持Pending互斥。局内错误提示带游戏代次和错误序号，离开原世界后不带入选角页，新的连接／退局错误仍显示。此修正对应2026-10-07用户“关闭背包捡衣服后画面正常但无法行走”报告；未复现具体物品／原服响应，未构建或运行。

## 显示与支持边界

原表提供尺寸、槽位、腰带、Books、类型、属性位宽与说明。原服品质1–8与内部ItemQuality顺序不同，联机显示和独立D2S映射共用quality.hpp的原编号表；原9（Tempered）未支持，明确解码失败，不强制映射到现有品质。地面标签／提示／货架使用同一已转换品质，保留原蓝／黄／绿／金／橙及带孔／无形灰色规则。未鉴定暗金／套装不推测缺失fileIndex；已鉴定时才读取原特殊名称、invfile／flippyfile覆盖。

原服DROPTOGROUND（action=2）或新mode=5建立独立掉落表现版本；后续属性／ONGROUND更新不重播。公共SceneView恢复原25Hz flippy，绘制、高亮和命中取同一当前帧，切区／退局／拾取清理时钟；mode=5显示但不授权拾取，mode=3仍发原0x16并等待原服。ItemDropSoundEvent 交给公共 SceneAudio 消费，起始翻转音及 MPQ 指定帧落地音分别只消费一次，音效配置统一在 SoundCatalog。动画／音效只影响表现，不创建物品或决定掉落。

RemoteUiClients 保留请求前已知数量及归属基线：普通拾取等待原服将同一地面 GUID 分配给本人容器／Cursor；金币同时要求原堆移除和已知钱包增加；合堆／卷轴入书读取已有目标数量增加及源数量减少／移除，数量必须对齐。自动合堆后的本人容器剩余量计入整次拾取，手动部分合入仅提示确认增量。相关 Updated、单独物品消失、解码失败或单侧属性变化不宣称成功；请求替换／中断／超时、死亡、换区清理跟踪，相关更新后最多保留5秒等待后续分包，不改变原请求结果。连续物品通知有界排队并带普通提示级别。音效与数量反馈尚未构建、运行或打包，限制见[物品支持](../gameplay/items/SUPPORT.md)。

gameplay/items 剩余定义及装备纯计算供显示／工具，不能据此宣称装备触发或本地库存可执行。人物必要输入不足时保持未知；商店报价暂未知，不能显示伪造零价。

已接操作、参数及诊断只维护在[调试管道](../development/DEBUG_PIPE.md#联网物品操作)。玩家交易、赌博、多买、分堆、佣兵装备和任务特殊服务仍有限。原值模型见[物品模型](../gameplay/items/MODEL.md)，显示缺口见[物品支持](../gameplay/items/SUPPORT.md)，独立编码见[存档](SAVES.md)。
