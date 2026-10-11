# 装备资格、属性贡献与切组

更新：2026-10-11。本页负责武器／防具／首饰／护符的资格、活动装备、套装激活与属性贡献。生成与词缀归[GENERATION](GENERATION.md)／[AFFIXES](AFFIXES.md)，战斗消费归[攻击](../combat/ATTACKS.md)，充能／触发归[物品技能](../skills/ITEM_SKILLS.md)。

## 当前执行

身体需求由公共loadout／contributions／requirements计算，不重复维护力量／敏捷公式。未鉴定、损坏、非活动手部不贡献，护符只在背包生效；普通交换、双手冲突、装备与两组武器切换已有权威执行。属性／需求变化同步重新校验装备，装备资格与人物总值同笔提交。

套装按不同SetItems记录身份计数，同一部件的两个实例不重复计数，条件随机值在生成时冻结；addfunc=1按特定其他部件、addfunc=2按不同部件数激活。公共activeSetItemLayers两端使用，依据D2Common Items::sub_6FDA4380；客户端仅消费已知属性，不安装权威贡献。普通、符文之语和已激活套装层的技能来源沿同一有效装备资格收集。

## 手位与双手剑

`equipment_rules`共用装备双手冲突和伤害握持资格，客户端准备原请求，服务端最终校验。MPQ ItemTypes的BodyLoc1／2允许盾牌、弓／弩及箭袋装在任一手位；弓／弩只能配相应Shoots／Quiver类型，不能配盾牌或另一件武器；射击从持弓手的另一侧扣除弹药。盾牌只能配可单手使用的武器；两面盾和两种不匹配弹药不能共存。切组沿当前活动武器组判断，不把I组的另一手误算到II组。

野蛮人仅对Weapons.1or2handed标记的武器拥有单手例外，当前原表是双手剑；双手斧、长柄和弓／弩不因职业放宽。单独持双手剑取2handmindam／2handmaxdam和2handedwclass；另一手装备武器或盾时取mindam／maxdam和wclass，两武器使用原双持动画类。另一手是否占位按实际装备记录判断，损坏或需求失效只停止属性贡献，不让双手剑自动改成双手握持。佣兵不获得玩家野蛮人的该例外。

物品说明以查看角色职业为上下文：野蛮人查看原1or2handed武器同时显示单手和双手伤害，其他职业仅显示双手伤害。装备后的角色伤害和动作按实际握持计算，不用说明中两组基础伤害替代人物数值。选角与世界原组件投影也按剑盾／单剑选择正确武器类，保留原图。

依据本地D2MOO `Items::ITEMS_Is1Or2Handed`、`Inventory::INVENTORY_GetWieldType`、`ItemMode::sub_6FC43280`及当前MPQ；原D2S v96仍保存既有body位置／武器组，没有新增握持字段或私有段，重入从原装备位置推导。没有存档格式迁移。本轮代表运行范围与包身份由[EVIDENCE](EVIDENCE.md)及[运行包记录](../../development/BUILD_AND_RUN.md#运行包记录)维护。

## 入口与边界

[equipment_loadout](../../../src/gameplay/items/equipment_loadout.cpp)、[contributions](../../../src/gameplay/items/equipment_contributions.cpp)、[requirements](../../../src/gameplay/items/equipment_requirements.cpp)提供纯计算，server/attributes与inventory/equipment规划总值及位置，transactions提交；内容只由hosting准备。武器手位／类型与六通道伤害不在本页再实现。

武器、防具、盾、腰带、靴／手套、戒指／项链、护符及职业专属基础类型共享资格框架，仍须逐条登记特殊属性与消费者。玩家吸血／吸魔／压碎／撕裂已接公共[武器命中效果](../combat/WEAPON_EFFECTS.md)，按实际出手武器收集本件贡献。完整双持组合、专用触发和部分职业消费者仍有缺口；能显示属性不能算该效果已执行。四类佣兵装备服务与自身属性贡献已接，类型限制和有限换装证据见[佣兵](../characters/HIRELINGS.md)；女武神装备准备不代表本人或佣兵全类型支持。

有限装备／切组／套装与保存证据见[EVIDENCE](EVIDENCE.md)；全属性、全部套装组合、技能来源失效、多人及三难度未整体认证。
