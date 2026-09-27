# 亚马逊已接入技能

实现以当前 MPQ 和本地 D2MOO 为准；不增加原表以外的技能。当前源码已接入瘟疫标枪（25）与爆炸箭（16）；Windows Release 已编译通过并更新 dist/current，未运行测试或游戏。原技能树的等级、前置、点数、左右槽和快捷键共用已有入口；武器类型、弹药、法力不足或技能延迟期间，已接入技能的图标显示不可用。

## 瘟疫标枪

`content/weapon_skill_data.cpp` 导入 Skills 和 Missiles 原记录，`WeaponSkillSpec` 进入普通武器攻击执行链。只接受 ItemTypes 包含 `jave` 的可用武器；使用原 TH、IAS、WSM 和出手帧，消耗一支标枪。原 `usemanaondo=1` 在成功出手时扣蓝；`delay=100` 在出手后设置共享限时技能延迟。打断出手前动作不消耗标枪或法力，切换装备不把原攻击替换为空手。

法力为 `max(minmana, (mana + (等级 - 1) × lvlmana) × 2^manashift / 256)`；当前一级 7 点，每级增加 0.5 点。技能命中百分比为原 `ToHit + (等级 - 1) × LevToHit`，当前 30% 加每级 9%，与装备百分比在同一处计算。角色面板显示直接命中的武器／元素及完整毒伤合计和命中率数值。

原 HitShift=3，毒素强度按 1/256 生命每帧计算，五段等级成长使用 EMin/EMaxLev1–5；毒枪（15）已学基础等级经 Param8 提供每级 10% 协同，装备技能加值不计协同。毒时长为 ELen/ELevLen1–3，当前一级 75 帧，每级加 10 帧。主标枪继承武器物理和元素；装备毒素与技能毒素先合并范围再掷值，原毒时长之和按装备毒源数取整平均。毒云的 SrcDamage=-1 明确排除武器伤害，只用技能毒素。

主弹体 43 的命中函数 2 生成子弹体 221，和毒瓶共享 `MissileImpactSpec.cloudBurst`。原 16 个方向按主步长 2／次步长 1 取两圈，共 23 团；两圈速度来自子表 Param1/2，经原 75% 速度缩放。Range 60 加三次 SubLoop 的 27 帧为 87 帧。撞墙、到期、命中检定失败也生成云；云按原尺寸 2 碰撞，LastCollide 只抑制前一个目标。毒素施加时计算抗性，弱毒不替换强毒，持续伤害不重复触发受击动作。

表现层读取原 Javelin／PoisonSparks 图，使用原方向、AnimLen、animrate、SubStart/Stop 和退场帧，以及原投掷／命中声音。当前 MPQ 主标枪 pSrvDoFunc=3，不能因参考函数名称带 Plague 就擅自加入有伤害的飞行尾迹。pCltDoFunc=3 的客户端尾迹、pCltDoFunc=4 的 poisonpuff 喷发节拍、动态光及起手声音的 stsounddelay 尚未在本地客户端参考中核实；不声称动画逐帧等价。

## 爆炸箭

弓与弩都按原 `itypea1=miss` 接入，使用各自 A1、IAS／WSM／AnimData 出手帧及匹配弹药。与瘟疫标枪不同，原 usemanaondo 为空，成功开始动作时扣蓝，出手时消耗一支箭／弩矢；打断后不退还已经消耗的起手法力。当前一级 5 点，每级增加 0.5；ToHit 为 20% 加每级 9%，无额外技能延迟。火箭（7）的已学基础等级按 Param8 每级提供 12% 火伤协同。

当前 MPQ 的实际链路是主弹体 41（FireArrow）→命中函数 4→不可见子弹体 656（explodingarrowexp2）→命中函数 1。主箭保存发射时的武器物理、装备元素和命中数据；不再额外附加一次技能火伤。子弹体保存父弹的技能等级，在生成时重新解析来源角色的协同／支配及当前装备火伤，Range=1，即下一个模拟帧执行半径 5 子格的范围火焰命中。只有火焰通道进入范围效果，不带入武器物理、冰／电／毒、压碎或吸取；直接目标可以同时受到箭本体和爆炸伤害。撞墙、到期和箭本体命中失败也产生爆炸，范围命中不再做箭本体的准确率检定。

`MissileImpactSpec.areaMissile` 描述独立服务器子弹体；不把不可见服务器记录的 CelFile=null 当成图像，也不借用药瓶的伤害常量。表现读取 ExplosionMissile 指向的 42（ExpArrowExplode），原 Range=16 帧，并播放其原 explosion_medium_1 声音；飞行图、方向与节拍仍用原 41。CltHitSubMissile1=fireexplosion2 的额外随机图块、RandStart 和动态光仍缺客户端精确发射证据，暂缓；已有中心爆炸不是完整客户端特效等价声明。

## 入口和扩展边界

- 原数据：`content/weapon_skill_data.*`；共用命中结构及资源导入：`content/missile_effects.*`。
- 技能等级解析：`gameplay/skills/resolve.cpp`；动作与消耗：`combat/attacking.cpp`、`physical_projectiles.cpp`；毒伤及云：`weapon_elements.cpp`、`missile_effects.cpp`。
- 证据：D2MOO `Skills.cpp` 的 `sub_6FD11420`、`D2GAME_SKILLS_SetDelay_6FD11C00` 和技能开始／执行末段；`Units/Missile.cpp` 的伤害数据／毒源聚合；`MissMode.cpp` 的 `MISSMODE_CreatePoisonCloudHitSubmissiles`、`SrvHit02`、`SrvHit04`、`SrvHit01`、`MISSMODE_GetDamageValue`、`SrvDmgHitHandler`。
- 前置技能可学点但未实现效果；原被动、穿透、怪物格挡、完整触发及毒素时长抗性等共用缺口见 [通用攻击](COMMON_ATTACKS.md)。临时动作、弹体和技能延迟不写 D2S，格式和保存语义不变。

原表没有玩家技能名为“爆炸标枪”；独立弹体 explodingjavalin（429）按用户澄清暂缓，不挂接到任何虚构技能。
