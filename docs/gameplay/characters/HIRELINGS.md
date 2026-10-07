# 资料片佣兵

当前联机尚未完成雇佣、复活、装备、喂药与归属的完整原协议链。佣兵面板存在不表示服务可用；旧本地佣兵AI／技能／成长执行器已删除，其历史冒烟不认证联机。

## 保留数据与用途

当前MPQ的Hireling、MonStats／MonStats2、Skills、Missiles、MonSeq、States和AnimData提供真实身份、难度分段、成长、技能权重、原图及动画。content/npc/hireling_data保留原表适配，保存值与persistence保留原类型、名字偏移、种子、经验、死亡位和装备编码；没有原当前生命字段，不能补写私有字段。

第一幕罗格、第二幕沙漠守卫、第三幕铁狼和第五幕野蛮人按原表区分；第四幕没有独立佣兵类型。不能把佣兵当敌对替身，不能在客户端补造雇佣单位、技能、经验或天然光环。获得资格、费用、生命／资源、同行及任务奖励都等原服。

## 核对入口与限制

本地D2MOO固定版本：AiThink::Fn061_Hireable、MonsterAI::MONSTERAI_UpdateMercStatsAndSkills、SUnitNpc雇佣／复活／治疗、PlayerPets同行与死亡，以及PlrMsg原0x61装备处理。它们用于核对原请求与字段，不作为客户端执行器；许可见[资料来源](../../resources/THIRD_PARTY.md)。

剩余工作见[联机计划M8阶段](../../architecture/MULTIPLAYER.md#实施状态)，保存工具边界见[存档](../../modules/SAVES.md)。全部类型、分段及装备／技能表现尚未原服验收。
