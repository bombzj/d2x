# 腰带与消耗品

本页维护腰带使用、药剂与原消耗效果；数量／缩容归[INVENTORY](INVENTORY.md)，永久奖励归[QUEST_ITEMS](QUEST_ITEMS.md)，卷轴／书本技能程序归[GENERAL](../skills/GENERAL.md)。

容量、布局、类型、书页和原图来自当前MPQ Belts／Inventory／Misc／Books／CharStats。连接的服务端拥有自动入带、补位、消耗、回复、状态与持续时间；自研inventory／effects沿同一原协议执行，旧客户端本地执行器已删除。Shift商店购买按原multibuy位提交，服务器决定腰带／书本补满和费用。

## 操作

1–4只使用对应列最底格，不越过空底格寻找上层药水；鼠标悬停腰带自动展开、移出后收起，~切换固定展开，I／B打开背包时显示全部容量。右键可直接使用任意可见格，左键拿起后进入Cursor；拿起与消耗都会将该列剩余物品保持次序向下补齐，放入／交换不会压缩其他格。可手动混放不同种类的1×1原belt物品。

Shift+1–4将药水交给佣兵，仅接受生命、回复、解毒、解冻药水；没有存活佣兵时原腰带请求回落为人物自用，法力／耐力药水不消耗。腰带喂药按本人存活佣兵查找，不额外添加距离条件。拿着Cursor物品时不使用腰带药水；商店打开时数字键仍可使用。腰带→Cursor、放入、交换及使用由原物品回包确认，药剂消失与资源回复分别观察，不能只按发送成功认定恢复完成。药水图形按HUD比例绘制，使用当前MPQ DC6，不沿背包侧面板比例缩小。

背包Shift入带已使用原0x63直接请求，不再先移入Cursor；服务端复验本人背包来源和空Cursor。自动空位按原GetFreeBeltSlot优先选择底格同类药水列，生命hp1–hp5、法力mp1–mp5、回复rvs／rvl各自不同等级可同列；同一代码也可匹配。ItemTypes中的rpot同时继承hpot与mpot，不能按继承类型把三组混列；只有MPQ AutoBelt允许另开空底格，不能任意填入非同类列上方。自有客户端空位预览同步该规则，手动拖放仍使用原0x23。

腰带卷轴沿原0x26使用：鉴定卷轴先等0x3F再选目标，回城卷轴仅野外成功创建门户后消耗，消耗后同列补位。回城卷轴／书也可经RemoteControl选择原技能并创建门户，真实type2门户与位置等原服；鉴定技能栏施放／原0x20使用由原0x3F准备光标，再0x27确认目标；背包右键沿原0x20请求准备来源。两条路径都等物品鉴定／来源消耗更新，不自动重试；界面不扣卷轴或书页。

自研私人门户共用travel::createPortal：源端使用原房间大小3、0x3e01寻位及0x0c01字段检查；目标端复用world::nativeLinkPortalPosition的TileInfo11、房间激活和0xbe11寻位。城镇有准备锚点不代表落点房间已加载，不能直接用该锚点的未驻留碰撞拒绝开门。两端房间随门户保留，成功后才共同提交耗页／卷轴及旧门替换。依据D2MOO `Skills::D2GAME_CreatePortalObject_6FD13DF0`／`D2GAME_CreateLinkPortal_6FD13B20`；普通难度第一幕卷轴已对照开门、往返、主人返回关闭及替换，其他幕／门户资格和全部失败副作用仍待核对。

## 原版定位

本地1.13c D2Client.dll（MD5 f5860c629d309b8fc1f96174babcf633）RVA29990／29B10为腰带数字键、Shift标志与底格查找，RVA26FEC–270AB为悬停展开／离开收起，RVA28DC0为展开绘制。D2Game.dll（MD5 978f6f74cd031bce6ecee602cd5988cc）RVAD4D30为取出，RVAD8BA0为使用，二者调用RVAD2540按列稳定补位；佣兵分支按原类型筛选并在没有存活佣兵时保留人物目标。D2MOO ItemMode::sub_6FC49140的重建索引有误，补位以同版本DLL为准。

自动装入参考D2MOO D2Inventory::INVENTORY_GetFreeBeltSlot与Items::ITEMS_ComparePotionTypes。当前MPQ Misc的belt／AutoBelt／useable决定物品资格与开新列，Belts.boxes决定容量，ItemTypes提供继承关系；原hp／mp／回复代码分组属于原算法条件。客户端beltSpace与服务端Draft::space共用belt_rules.hpp的分组，Draft::compactBeltColumn负责取出／自用／佣兵使用后的补位。

腰带消耗的原包依据为D2MOO `SkillItem::SKILLITEM_pSpell_Handler` → `ItemMode.cpp::sub_6FC49220` → `SCmd.cpp::D2GAME_SendP0x9C_ItemAction_RemoveFromBelt_6FC3EBE0`：成功执行pSpell后先用3F取消本来源选靶，再发送9C/action15，保留INBELT模式和原格位，并临时附加IFLAG_QUANTITY（0x20），再移除服务端原件。自研成功药剂事务提交对应ItemTargetingFact，事务出口将取消事实置于物品移除回执之前。补位由`sub_6FC49140`标记NEWITEM／UNK1并清除NOEQUIP，沿`InvMode.cpp::sub_6FC410E0`的NEWITEM分支发送9D/action21（UpdateStats）；UNK1保留在原物品flags，保存边界见[存档](../../modules/SAVES.md)。该处D2MOO格位循环有前述版本边界，不能照搬索引，但回包分支可独立定位。PutInBelt对应入带命令标志，SwapInBelt会让被替换物进入Cursor，不能用于同列补位。

自研`nativeInventoryDelta`按上述路径区分腰带移除、同带位置更新和Cursor交换；事务事实另存已删除原件的不可变值，编码不查询后续实时库存，也不把已删除物品放回权威状态。原版客户端Join D2X Host使用第四列蓝瓶时曾观察到服务端已消费、补位却误发9C/action16，随后原客户端持续请求丢弃补位GUID而宿主无对应Cursor；该兼容缺陷已按上述源码路径修正，修正后的原版联机表现仍待复验。

## 原规则与限制

原恢复量、长度、pSpell、cstate与stat／calc读取MPQ；职业倍率规则核对本地D2MOO Items::ITEMS_GetBonusLifeBasedOnClass／ManaBasedOnClass。SkillItem::pSpell03／09提供恢复合并与耐力／解毒／解冻原服规则证据，客户端不重新执行这些函数。

自研生命／法力药水已纠正旧单机顺序队列，使用原healthpot／manapot状态、8.8恢复值和len：连续饮用合并剩余帧与剩余恢复值，再整数除法重算每帧率；按实际体力／精力及原随机判定双倍恢复，消费提交后才推进种子。生命超过上限移除Healthpot，法力恢复前已满移除Manapot，到期／死亡移除；补血与毒伤合并后限制至少一生命，毒伤不禁自然回蓝，原nomanaregen才抑制自然回蓝。定时状态、原回包与资源在同一提交边界更新，不保存药水时钟。

腰带脱下／缩容／出售的安置规则只维护在[INVENTORY](INVENTORY.md)。NPC治疗遵循SUnitNpc::HealPlayer，只清毒／冻结与States.curable指定状态；effects准备、transactions提交，只实际治疗时发送原Sound10。诊断healingQueued／manaQueued表示有恢复时钟的原状态数，不表示旧队列瓶数。

技能书／生命药剂／抗性卷轴及Token的资格、消耗与任务来源统一见[QUEST_ITEMS](QUEST_ITEMS.md)，原保存位见[存档](../../modules/SAVES.md)。不能从物品可用图标推断当前人物已获得永久奖励资格。

客户端仅消费原状态／属性，不自设清毒、冷却、叠加或耐力回复。佣兵腰带／光标喂药与城镇治疗已接，范围见[佣兵](../characters/HIRELINGS.md)；完整消耗品目标及组合仍待对齐，药剂／卷轴原服消息参考见[联网记录](../../modules/NETWORK.md#既有有限证据)。回复药水按原8.8上限百分比取整，耐力／解毒／解冻药水读取MPQ状态、长度、清除状态及属性，同状态延长剩余时间；资源上限、自然恢复与耐力耗用归人物／effects，不在物品消费写第二套计算。原资源／声音仍由公共加载与播放模块消费，许可见[资料来源](../../resources/THIRD_PARTY.md)。
