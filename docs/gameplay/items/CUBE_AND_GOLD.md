# 方块、堆叠、书与金币

原物品尺寸、Books配对、堆叠上限、方块格子／原图来自当前MPQ；D2GS执行合并、装书、配方、产物、金币与奖励。客户端不调用旧方块事务或物品生成器。

## 联机操作

方块使用当前MPQ pSpell=7提交打开请求，0x77确认后访问3×4格子。已确认打开、空Cursor且有材料时可0x4F合成；材料消耗与产物看原回包，不以Updated单字段宣称配方成功。关闭清理临时访问与未发送组合。

堆叠、装书、存取金币及丢金币通过原请求；不支持自选分堆数量。0x19金币增量与绝对属性分别归并，0x2A交易金额不重复加钱包。金额高WORD在前等原编码由RemoteInventory处理。

## 保留原表与编码

content/items/cube_data导入当前CubeMain供资源报告；当前MPQ有146条启用配方、36条Crafted，此摘要不表示联机全部配方认证。Crafted原quality=8、词缀／需求、TXT编号、孔／符文之语及署名均使用原位流，不重掷或静默编号迁移；独立编码边界见[存档](../../modules/SAVES.md)。

牛门／钥匙门／最终门、Token重置及特殊配方资格由原服；客户端不打开自造门户或沿用旧任务奖励入口。原规则证据为本地D2MOO HoradricCube Input／OutputParser、PlrTrade、ItemsMagic／Items、A1Q4，数据仍来自当前MPQ。

原请求／参数见[命令](../../development/DEBUG_PIPE.md#联网物品操作)，有限3宝石合成／金币观察见[联网记录](../../modules/NETWORK.md#既有有限证据)。自然取得、全部配方和特殊事件未认证。
