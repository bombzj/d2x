# 资料片佣兵

佣兵资格、费用、归属、同行、战斗与保存由所连接的服务端决定。自研宿主已实现第一幕血乌奖励的真实罗格伙伴，尚未完成完整雇佣、复活、装备、喂药与经验成长服务。佣兵面板存在不表示这些服务可用；旧客户端本地佣兵执行器与其历史冒烟不认证当前联机。

## 任务奖励与运行态

[第一幕任务](../quests/ACT1.md)负责一次性奖励资格；hosting从当前Hireling表准备原Kashya奖励，已有佣兵（包括死亡记录）不被替换。异步准备复验人物身份、佣兵sourceRow与等级，缺原身份、动画或技能参数明确暂缓。

[hireling_content.cpp](../../../src/hosting/hireling_content.cpp)目前只准备第一幕罗格，读取Hireling、MonStats／MonStats2、Skills、Missiles与AnimData等原数据，使用原成长、技能权重及Fn061同行／目标选择规则。[companions/hireling.cpp](../../../src/server/systems/companions/hireling.cpp)管理规则准备与归属、出生、同行／远距归位、射击及Inner Sight；伤害、弹体和死亡复用monsters／effects与事务。伙伴为真实owned monster，不使用敌对替身，不由客户端补造单位或技能。

第三幕基德宾的铁狼与第五幕囚犯救援的野蛮人奖励已由hosting/quest_content准备原Hireling身份并保存，保留已有佣兵；具体资格及证据见[第三至第五幕](../quests/ACT3_5.md)。这两类的完整活动实体／AI尚未迁入，不能把身份领奖／保存当作同行战斗支持。

个人佣兵身份与死亡记录通过PlayerStore导出，原0x7A等消息投影归属。罗格奖励／同行／新进程恢复证据见[第一幕任务](../quests/ACT1.md#有限运行证据)，铁狼身份奖励／恢复与保留已有佣兵证据见ACT3–5，不据此认证全部AI分支或完整佣兵系统。

## 数据与保存

当前MPQ的Hireling、MonStats／MonStats2、Skills、Missiles、MonSeq、States和AnimData提供真实身份、难度分段、成长、技能权重、原图及动画。content/npc/hireling_data负责表适配；第一幕罗格、第二幕沙漠守卫、第三幕铁狼和第五幕野蛮人按原表区分，第四幕没有独立佣兵类型。读取所有类型的表不等于已经实现所有类型的运行态。

原D2S保留类型、名字偏移、种子、经验、死亡位和装备编码；没有当前生命字段，不补写私有字段。入局重新准备运行生命与规则，临时AI、位置、时钟不写盘。保存边界见[存档](../../modules/SAVES.md)。

## 核对入口与缺口

本地D2MOO：AiThink::Fn061_Hireable、MonsterAI::MONSTERAI_UpdateMercStatsAndSkills、SUnitNpc雇佣／复活／治疗、PlayerPets同行与死亡，以及PlrMsg原0x61装备处理。参考用于核对共享规则、服务端执行和原字段，不能恢复客户端执行器；许可见[资料来源](../../resources/THIRD_PARTY.md)。

后续需补雇佣货架、费用与替换、复活、装备／喂药、经验成长、其余佣兵类型及完整原消息链。全部类型、难度分段、装备与技能表现尚未原服验收，阶段依赖见[完整实施计划](../../architecture/MULTIPLAYER.md)。
