# 原服物品与容器模型

更新：2026-10-09。本页负责身份、位置／版本与模型的权限语义；事务规范见[COMMON](COMMON.md)，执行归属见[目录](README.md)。

所有物品、容器位置、数量、耐久、金币与最终装备属于连接的权威服务端（原D2GS或自研宿主）。RemoteInventory解码同一原位流；InventoryView是客户端只读值，不是第二份可提交库存。代码入口见[库存模块](../../modules/INVENTORY.md)，地面生命周期见[GROUND](GROUND.md)。

## 身份、位置与版本

原GUID＋游戏代次确定物品身份，revision防止旧手势操作变更后的物品。mode区分存储0、装备1、腰带2、地面3、Cursor4、掉落过渡5、孔内6；所有者类型区分玩家、NPC及宿主物品。

快照page为原InvPage＋1：背包1、方块4、箱子5；原place请求使用0／3／4。Cursor只容纳一件，不按背包宽高占格。孔内子项保留宿主GUID与顺序，不重复算根库存记录。

## 操作与权限

面板按当前MPQ Inventory／BodyLocs／Belts提供格子与部位预览。本人所有权、Cursor、已确认存储／货架、目标版本及交互代次在发送端复验；关闭／死亡／换区清理未发部分，不能撤销原服结果。

组合操作逐步等光标与相关回包，不用本地原子事务伪造全部成功。无ACK操作明确SentNoAck；TimedOut为未知，不自动重发非幂等请求。完整参数见[原物品命令](../../development/DEBUG_PIPE.md#联网物品操作)。

gameplay/items定义物品／位置／库存值类型及公共纯规则，实例可变真值由server领域持有，不在公共模型增加全局库存。自研PlayerStore拥有持久库存，inventory／crafting／merchant规划，transactions提交；D2S快照由hosting／persistence持有，客户端不访问。基础耐久、AutoMagic身份、真实属性掷值和孔内子项沿原字段保留。
