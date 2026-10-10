# 库存操作、数量与容器权限

更新：2026-10-10。本页维护物品移动、交换、合堆和书本转装的权威规则。GUID／mode／page与版本语义归[MODEL](MODEL.md)，界面操作归[INVENTORY_UI](INVENTORY_UI.md)，事务规范见[COMMON](COMMON.md)。

## 已接移动与数量

背包、Cursor、腰带、装备、两组武器、普通交换、双手冲突、腰带缩容、合堆与卷轴装书已有执行；领域内部保留自动拾取的同类书合并，手动原0x29只接受Cursor卷轴和匹配的stored书本，不开放书页互转。本人所有权、来源／目标句柄、活动装备组及授权在固定步复验；书本在仓库／方块时还要求对应真实存储授权及携带方块。无法安置的普通移物保持原库存，不靠关闭面板清除Cursor。装备总值见[EQUIPMENT](EQUIPMENT.md)，装书原包与显示时序见[PRESENTATION](PRESENTATION.md)。

合堆校验基底、品质、file index、无形、物理伤害及无孔条件；普通／超强／劣质可合并，魔法品质不合并。最大数量取原基础加item_extra_stack并复验9位上限。书本使用Books配对／charges，根数量仍为1；新生成页数按原minstack／spawnstack选取，网络和磁盘数量映射为页数。自动拾取允许提交实际合并量并保留地面余物。数量归零按原品质／item_throwable规则保留或移除。

腰带收缩先移背包，余药按原ItemMode::sub_6FC45930落地；同一规划供脱下、换带和商店出售使用，实际落点／世界容量失败不提交任何一边。删除物品时事务裁剪EquipmentRules，不保留耗尽药剂／卷轴的无主规则。腰带布局与饮用见[BELT_AND_CONSUMABLES](BELT_AND_CONSUMABLES.md)。

## 容器与其他来源

仓库与方块授权独立；只有真实交互／原确认后才允许对应容器操作，关闭、死亡、换区、交互失效或移走方块使授权失效。详细规则分别见[STORAGE](STORAGE.md)和[CUBE](CUBE.md)。

地面拾取有自己的自动安置／部分合并规则，见[GROUND](GROUND.md)；尸体回收按装备／腰带／背包顺序、余物保留，不套地面合书／合堆。原服客户端见[人物死亡](../characters/PLAYER_DEATH.md)，自研执行与持久化边界见[服务端死亡](../../modules/SERVER_SYSTEMS.md#死亡与尸体)。普通库存命令不授权访问别人的尸体、佣兵装备或交易格。鉴定来源、目标与授权见[IDENTIFICATION](IDENTIFICATION.md)，物品技能／充能选择和热键见[物品技能](../skills/ITEM_SKILLS.md)，不以普通移物替代扣费或确认。

原0x22没有自选分堆执行，不自造协议；复杂容量、全物品／容器组合、多人争用与输出背压未完整验收。已接操作不代表所有位置／品质／来源组合都可用，有限证据见[EVIDENCE](EVIDENCE.md)。
