# 原服物品与容器模型

所有物品、容器位置、数量、耐久、金币与最终装备属于D2GS。RemoteInventory解码原位流；InventoryView是客户端只读值，不是第二份可提交库存。接口及生命周期见[库存模块](../../modules/INVENTORY.md)。

## 身份、位置与版本

原GUID＋游戏代次确定物品身份，revision防止旧手势操作变更后的物品。mode区分存储0、装备1、腰带2、地面3、Cursor4、掉落过渡5、孔内6；所有者类型区分玩家、NPC及宿主物品。

快照page为原InvPage＋1：背包1、方块4、箱子5；原place请求使用0／3／4。Cursor只容纳一件，不按背包宽高占格。孔内子项保留宿主GUID与顺序，不重复算根库存记录。

## 操作与权限

面板按当前MPQ Inventory／BodyLocs／Belts提供格子与部位预览。本人所有权、Cursor、已确认存储／货架、目标版本及交互代次在发送端复验；关闭／死亡／换区清理未发部分，不能撤销原服结果。

组合操作逐步等光标与相关回包，不用本地原子事务伪造全部成功。无ACK操作明确SentNoAck；TimedOut为未知，不自动重发非幂等请求。完整参数见[原物品命令](../../development/DEBUG_PIPE.md#联网物品操作)。

gameplay/items只保留定义、位置／错误值及纯显示规则；InventoryService、replacement事务、访问令牌和Local缓存已删除。独立D2S保存快照归persistence，不参与联机库存。
