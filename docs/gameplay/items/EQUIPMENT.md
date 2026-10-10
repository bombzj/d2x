# 装备资格、属性贡献与切组

更新：2026-10-10。本页负责武器／防具／首饰／护符的资格、活动装备、套装激活与属性贡献。生成与词缀归[GENERATION](GENERATION.md)／[AFFIXES](AFFIXES.md)，战斗消费归[攻击](../combat/ATTACKS.md)，充能／触发归[物品技能](../skills/ITEM_SKILLS.md)。

## 当前执行

身体需求由公共loadout／contributions／requirements计算，不重复维护力量／敏捷公式。未鉴定、损坏、非活动手部不贡献，护符只在背包生效；普通交换、双手冲突、装备与两组武器切换已有权威执行。属性／需求变化同步重新校验装备，装备资格与人物总值同笔提交。

套装按不同SetItems记录身份计数，同一部件的两个实例不重复计数，条件随机值在生成时冻结；addfunc=1按特定其他部件、addfunc=2按不同部件数激活。公共activeSetItemLayers两端使用，依据D2Common Items::sub_6FDA4380；客户端仅消费已知属性，不安装权威贡献。普通、符文之语和已激活套装层的技能来源沿同一有效装备资格收集。

## 入口与边界

[equipment_loadout](../../../src/gameplay/items/equipment_loadout.cpp)、[contributions](../../../src/gameplay/items/equipment_contributions.cpp)、[requirements](../../../src/gameplay/items/equipment_requirements.cpp)提供纯计算，server/attributes与inventory/equipment规划总值及位置，transactions提交；内容只由hosting准备。武器手位／类型与六通道伤害不在本页再实现。

武器、防具、盾、腰带、靴／手套、戒指／项链、护符及职业专属基础类型共享资格框架，仍须逐条登记特殊属性与消费者。完整双持组合、吸血／吸魔／压碎／撕裂、专用触发和部分职业消费者未完成；能显示属性不能算该效果已执行。四类佣兵装备服务与自身属性贡献已接，类型限制和有限换装证据见[佣兵](../characters/HIRELINGS.md)；女武神装备准备不代表本人或佣兵全类型支持。

有限装备／切组／套装与保存证据见[EVIDENCE](EVIDENCE.md)；全属性、全部套装组合、技能来源失效、多人及三难度未整体认证。
