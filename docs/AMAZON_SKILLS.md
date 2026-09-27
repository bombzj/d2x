# 亚马逊已接入技能

实现以当前 MPQ 和本地 D2MOO 为准；不增加原表以外的技能。当前源码已接入瘟疫标枪（25）；尚未构建或运行验收。原技能树的等级、前置、点数、左右槽和快捷键共用已有入口。

## 瘟疫标枪

`content/weapon_skill_data.cpp` 导入 Skills 和 Missiles 原记录，`WeaponSkillSpec` 进入普通武器攻击执行链。只接受 ItemTypes 包含 `jave` 的可用武器；使用原 TH、IAS、WSM 和出手帧，消耗一支标枪。原 `usemanaondo=1` 在成功出手时扣蓝；`delay=100` 在出手后设置共享限时技能延迟。打断出手前动作不消耗标枪或法力，切换装备不把原攻击替换为空手。

法力为 `max(minmana, (mana + (等级 - 1) × lvlmana) × 2^manashift / 256)`；当前一级 7 点，每级增加 0.5 点。技能命中百分比为原 `ToHit + (等级 - 1) × LevToHit`，当前 30% 加每级 9%，与装备百分比在同一处计算。角色面板显示直接命中的武器／元素及完整毒伤合计和命中率数值。

原 HitShift=3，毒素强度按 1/256 生命每帧计算，五段等级成长使用 EMin/EMaxLev1–5；毒枪（15）已学基础等级经 Param8 提供每级 10% 协同，装备技能加值不计协同。毒时长为 ELen/ELevLen1–3，当前一级 75 帧，每级加 10 帧。主标枪继承武器物理和元素；装备毒素与技能毒素先合并范围再掷值，原毒时长之和按装备毒源数取整平均。毒云的 SrcDamage=-1 明确排除武器伤害，只用技能毒素。

主弹体 43 的命中函数 2 生成子弹体 221，和毒瓶共享 `MissileImpactSpec.cloudBurst`。原 16 个方向按主步长 2／次步长 1 取两圈，共 23 团；两圈速度来自子表 Param1/2，经原 75% 速度缩放。Range 60 加三次 SubLoop 的 27 帧为 87 帧。撞墙、到期、命中检定失败也生成云；云按原尺寸 2 碰撞，LastCollide 只抑制前一个目标。毒素施加时计算抗性，弱毒不替换强毒，持续伤害不重复触发受击动作。

表现层读取原 Javelin／PoisonSparks 图，使用原方向、AnimLen、animrate、SubStart/Stop 和退场帧，以及原投掷／命中声音。当前 MPQ 主标枪 pSrvDoFunc=3，不能因参考函数名称带 Plague 就擅自加入有伤害的飞行尾迹。pCltDoFunc=3 的客户端尾迹、pCltDoFunc=4 的 poisonpuff 喷发节拍、动态光及起手声音的 stsounddelay 尚未在本地客户端参考中核实；不声称动画逐帧等价。

## 入口和扩展边界

- 原数据：`content/weapon_skill_data.*`；共用命中结构及资源导入：`content/missile_effects.*`。
- 技能等级解析：`gameplay/skills/resolve.cpp`；动作与消耗：`combat/attacking.cpp`、`physical_projectiles.cpp`；毒伤及云：`weapon_elements.cpp`、`missile_effects.cpp`。
- 证据：D2MOO `Skills.cpp` 的 `sub_6FD11420`、`D2GAME_SKILLS_SetDelay_6FD11C00` 和技能执行末段；`Units/Missile.cpp` 的伤害数据／毒源聚合；`MissMode.cpp` 的 `MISSMODE_CreatePoisonCloudHitSubmissiles`、`SrvHit02`、`SrvDmgHitHandler`。
- 前置技能可学点但未实现效果；原被动、穿透、怪物格挡、完整触发及毒素时长抗性等共用缺口见 [通用攻击](COMMON_ATTACKS.md)。临时动作、弹体和技能延迟不写 D2S，格式和保存语义不变。

原表没有玩家技能名为“爆炸标枪”；独立弹体 explodingjavalin（429）按用户澄清暂缓，不挂接到任何虚构技能。
