# 女巫技能

女巫30项使用同一份当前MPQ定义、技能树和纯公式。自研服务端执行26项主动技能及4项被动；客户端无单机执行分支，原服与自研宿主共用选择、学习、目标、动画、声音和原包消费者。当前源码已接以下执行路径；当前包见[基线](../../../BASELINE.md)，历史运行条件及限制见本页有限证据。

## 执行范围

| 技能 | 服务端执行与客户端表现 |
| --- | --- |
| Fire Bolt、Fire Ball | 原SC动作帧释放；直线飞行、火抗、单体或一次范围伤害；客户端由4C／4D派生原图 |
| Ice Bolt、Ice Blast、Glacial Spike | 原冷伤与难度冷时长；减速、普通怪物冻结、冰尖柱接触点范围；状态A7／A9、命中0C |
| Charged Bolt | 原数量、77步格点路径及逐弹伤害；共享chargedBoltPath |
| Frost Nova、Nova | 原64方向环射、穿透与冷时长；共享整数环形方向 |
| Frozen Orb | 沿途旋转散射、自然到期环射及冰弹转向；撞墙不补到期爆发，子弹出生时重算当前支配／协同 |
| Blaze、Fire Wall | 状态随移动放火；墙生成器与独立火段，逐目标每帧原火伤；客户端按CltDo26／跨格程序及Blaze状态重建，常规生成不重复广播73 |
| Meteor、Blizzard | 陨石延迟、一次范围爆炸及18个原地面火偏移；暴风雪剩余帧节拍、原格点随机散布与下落碎片 |
| Lightning、Chain Lightning | 原seq12／19步SC、第7步释放；穿透、NextDelay和按GUID环选连锁，子弹继承剩余跳数；客户端补CltHit16 |
| Inferno | 原seq6／SQ、15步、释放帧及循环姿态；服务端逐帧射流／周期扣蓝、停止与转向；客户端持续原图射流，转向不重启动作 |
| Static Field、Telekinesis | 抗性后按生命比例伤害及难度下限；心灵传动单体、原击退、远程合格物品／仓库／传送点／本人门户，失败物品音效2C |
| Teleport | 原同区资格／碰撞、位置与扣蓝事务，公开15校正；不扩展同行者传送 |
| Frozen Armor、Shiver Armor、Chilling Armor | 互斥装甲状态、期限与防御；命中后冻结、近战尝试反击、合格ReturnFire远程命中反击分别由effects排队；反击不递归 |
| Enchant | 玩家自身／同区友方玩家、已接友方单位及真实NPC；原火伤／命中率状态，跨玩家扣蓝和加成成组提交；不能假造NPC攻击 |
| Energy Shield | 原吸收比例、基础Telekinesis协同耗蓝，先吸收再减免；蓝耗尽清盾，生命／蓝／状态一起提交 |
| Thunder Storm | 原周期、轮换目标、距离／射线、雷伤；原A3瞬时技能事件触发目标落雷图，不伪造移动弹体 |
| Hydra | 原三种头、S2／A1／DT时序、召唤上限／期限／AI概率；companions管理归属与生命周期，技能337原4C／4D派生火弹 |
| Warmth、Fire Mastery、Lightning Mastery、Cold Mastery | attributes／skills纯计算：暖气恢复，有效支配等级，协同只用基础等级；冰冷支配不能破免疫 |

## 代码所有权与失败边界

`hosting/skill_content`准备纯规则、碰撞、ClientSend／ReturnFire、动画及Hydra定义；内核不读MPQ。`skills/casting`拥有动作、延迟、释放与射流；`missiles/launch`／`programs`拥有飞行和子弹，`combat/spell_damage`拥有抗性、逐目标提交与NextDelay；`effects`拥有玩家／友方单位状态、反击、护盾和周期；`companions`拥有Hydra，`monsters`仍唯一持有其单位生命／动作，`death`统一掉落和经验。远程物品、仓库、传送点及门户复用各领域资格，不绕过其事务。

输出背压保留首次准备的弹体、随机值、目标伤害和提交游标。重试不重复扣蓝、不重掷、不重复命中。未释放动作随移动／死亡／换区取消；已释放弹体允许施法者死亡后完成，但离开玩家或区域代次变化清理。受控Hydra不发敌怪奖励。持续施法的快照按skills的释放队列／忙碌状态判断，不能把每帧续射的结束刻当作站立时刻；原0D只在真实结束后发送。显式重复选技也返回原23确认，不依赖数值发生变化。短时状态、弹体、Hydra及周期不写D2S；基础技能、选择、热键和已提交资源沿v96保存，规则指纹admission-v17。

## 公共层及原版证据

优先迁用master的`session_skills`、`skills/projectile_launch`、`frozen_orb`、`blizzard`、`arc`、`glacial_spike`、`meteor`、`firewall`、`elemental_runtime`、`applied_effects`、`reactions`、`shield`和`monsters/companions`。旧GameSession不进入新内核。

`gameplay/skills/resolve`、`cast_timing`、`projectile_path`和`gameplay/combat/geometry`为无状态公共计算。不是把每个技能的整个SrvDo复制给客户端；以下表格区分实际调用和仍未提取的执行部分。

| 范围 | 当前公共计算与调用者 | 两端执行边界 |
| --- | --- | --- |
| 主动技能数值 | `resolveSkill`由server/skills/evaluation及client/character_projection调用；基础／有效等级、协同、支配显式输入 | 客户端只做说明／数值预览，原服和自研共用；服务端重算、随机掷伤、抗性／免疫、资源及事务独占 |
| Fire Bolt／Fire Ball／Ice Bolt／Ice Blast／Glacial Spike | `missileVelocityFixed`由resolve及ClientMissile调用；geometry共用墙面／单位交点 | 两端各持弹体状态；服务端伤害／穿透／范围，客户端原图接触／爆炸 |
| Charged Bolt／Frost Nova／Nova／Frozen Orb | `chargedBoltPath`、`missileRingDirection`／`missileRingEmission`／`missileRingBurst`、`missileDiagonalTurn`均有两端调用 | 调用端传Clt／Srv参数与帧相位；生成权威或视觉子弹体的队列、清理仍各端执行 |
| Lightning／Chain Lightning／Inferno | `playerCastSequence`共用seq12／seq6步序、释放步和hold步；`missileChainSuccessor`共用连锁选择 | ActorAnimation把步序映射到原图；服务端转换动作时钟并执行真实候选、周期射流／扣蓝 |
| Blaze／Fire Wall／Meteor／Blizzard | `missileWallDirection`及`blizzardOffset`均有两端调用；速度／几何复用上述公共函数 | Blaze跨格、火墙跨格、陨石落地、暴风雪周期的执行调度尚分属两端；陨石服务端18偏移与客户端图像分布也不是同一程序 |
| 三种装甲／Enchant／Energy Shield／Thunder Storm／Hydra | 技能数值沿公共resolve，Hydra规则由MPQ内容准备；没有第二份客户端伤害或AI | 服务端效果／反击／吸收／周期／召唤及AI；客户端只消费原状态／动作／瞬时事件并渲染 |
| Static Field／Telekinesis／Teleport／四项被动 | 主动数值沿resolve；四项被动的线性等级值沿`skillRankBonus`，由客户端提示、服务端attributes及mastery调用；位移几何可共用 | 服务端资格／实际位移／交互、资源恢复及属性安装；客户端只消费原包和预测显示 |

`SkillRuleSpec`保存准备好的纯规则及原协议效果编号／期限；`SkillSpec`在内容层附加原图、音效路径和绘制参数，`rules()`只投影规则。hosting发布`SkillRuleSpec`，server不携带这些资源路径；客户端提示也把同一规则交给resolve。原表公式目前由content校验已支持表达式并准备类型化参数，不复制D2MOO的全局数据表／单位指针或Calc字节码引擎；未知表达式明确拒绝，不能宣称通用公式解释器已完成。

按D2Common职责补齐`skillManaCostFixed`、`skillElementalLength`、`evaluateSkillDamageFixed`／`evaluateSkillMinimumDamage`、`evaluateMissileDamageFixed`及`skillDiminishingBonus`。法力原曲线与MinMana有效消耗分开，技能元素协同在HitShift后计算，独立弹体元素协同在HitShift前计算；支配随后应用。闪电／连锁的固定1点下限跳过协同。四项被动、数量和Inferno长度复用`skillRankBonus`。客户端提示与服务端重算调用同一resolve，只有服务端安装贡献、掷伤和提交资源。

`prepareCastAnimationTiming`统一原AnimData事件读取；`normalCastTiming`／`playerCastSequenceTiming`统一FCR、SC／seq12释放及完成帧，seq6固定速率。按D2Common的CAST分支，OtherAnimationRate不叠加进施法速率。本人FCR优先使用已收到的原属性；没有累计属性时，从完整原装备包、激活槽／NOEQUIP／BROKEN、套装列表和已解码状态列表汇总；复用物品属性解析，装备统计也与商店减价共用读取入口。缺列表保持未知，不猜其他玩家的隐藏属性或为自有服务端另写客户端分支。未知FCR的远端显示仍用原基础速率。

冰封球散射和子弹转向已按原remaining修正；`missileEmissionDue`、`missileRingEmission`、`missileOrbTurn`统一调度，保留Clt／Srv表参数。73带剩余寿命时，客户端时钟包含已过帧前缀，不能再扣一次前缀。`advanceMissileVelocity`共用每五帧加速／限速计算，`missileChangedCell`共用火墙跨格条件；客户端固定点、服务端cells/second表示仍各自适配，完整路径量化尚未统一。Blizzard保留原反向偏移／独立随机流；Meteor服务端18个真实火偏移和客户端分层图像分布分别执行。这些原版两端执行程序不强行合为伤害或渲染执行器。公共函数不查询GameSession／GameInstance、MPQ、GPU、可见世界或隐式随机。

本地D2MOO的SkillSor、MissMode、Missiles、SUnitDmg、MonsterMsg核对反击、连锁、召唤、ClientSend及67击退包。当前MPQ决定参数、原图和身份。D2MOO缺D2Client处采用本地原1.13c DLL只读证据：CltDo24的RVA74D50／74930选择两种Inferno火图；CltMissile13的BBC10／B8DF0按四种Blizzard图、剩余帧和globalX生成格点。客户端暴风雪随机偏移符号与服务端不同，公共函数显式保留，不能强行合并成一条轨迹。固定参考版本见[资料来源](../../resources/THIRD_PARTY.md)。

原服回归进一步核对CltDo26 RVA74770（两侧垂直火墙生成器及中心火）和CltDo28 RVA73DF0 → A1540 → AFF10（在目标点创建陨石／暴风雪中心），均不以ClientSend阻止本地创建。该标志是73视野同步资格，不是每次施法必须回包。自研hosting省略普通本人4C／4D；missiles省略这些可重建程序的常规73，领域伤害仍独立执行。

## 30项公共职责核对

下表逐项按当前MPQ的Skills／Missiles函数号对应本地D2MOO；空SrvDo经通用`D2GAME_SKILLS_Handler_6FD12BA0`创建原srvmissile，不是空技能。D2MOO是1.10f结构证据，参数及1.13c差异按当前MPQ／原DLL核实，不能用参考附带表覆盖资源。C＝公共法力／等级公式；D＝公共技能元素伤害／协同／支配；L＝公共元素长度；P＝公共线性被动；其余列出专项公共函数。数值提示共用resolve／rank_bonus，客户端不执行实际伤害、状态安装或AI。

| ID／技能 | 原SrvSt／SrvDo及执行证据 | 公共计算／定义 | 两端执行边界 |
| --- | --- | --- | --- |
| 36 Fire Bolt | 通用Handler；MissMode SrvDo1 | C、D、速度／碰撞几何、SC时钟 | 服务端单体命中；客户端原图飞行 |
| 37 Warmth | D2Common RefreshSkill/passivecalc | P：skillRankBonus | 服务端attributes安装恢复贡献；客户端提示 |
| 38 Charged Bolt | SkillSor SrvDo17／MissileInit_ChargedBolt | C、D、数量曲线、chargedBoltPath | 独立权威／视觉弹体，索引和77步上限相同 |
| 39 Ice Bolt | 通用Handler；MissMode SrvDo1 | C、D、L、速度／碰撞、SC | 真实减速归服务端；客户端接状态 |
| 40 Frozen Armor | SkillSor SrvDo18、EventFunc02 | C、等级防御／期限／冻结公式 | effects互斥安装、近战伤害事件；客户端状态覆盖层 |
| 41 Inferno | SkillSor SrvSt11／SrvDo19／DoInferno | C、D、线性射流长度、seq6步序／固定时钟 | 服务器持续释放／扣蓝；客户端24选择两种火图 |
| 42 Static Field | SkillSor SrvDo20／AuraCallback_StaticField | C、比例／下限／范围准备值 | 真实生命、难度下限和抗性仅服务端；客户端效果 |
| 43 Telekinesis | SkillSor SrvSt12／SrvDo21 | C、D、距离／击退几何 | 服务端资格、拾取／物件事务；客户端原目标／确认 |
| 44 Frost Nova | SkillSor SrvDo22 | C、D、L、missileRingBurst／Direction | 原CltDo25视觉环；服务器弹体及冷伤 |
| 45 Ice Blast | 通用Handler；MissMode SrvDmg4 | C、D、L、速度／碰撞 | 服务端难度冻结长度；客户端接原状态／冰图 |
| 46 Blaze | SkillSor SrvDo23／CreateBlazeMissile；MissMode SrvDo5／Dmg3 | C、D、期限；整数跨格规则 | 服务端状态移动留火／逐帧伤；客户端state setfunc3及CltDo5 |
| 47 Fire Ball | 通用Handler；MissMode SrvHit1 | C、D、速度／碰撞／范围几何 | 服务端范围目标与伤害；客户端CltHit1爆炸 |
| 48 Nova | SkillSor SrvDo22 | C、D、missileRingBurst／Direction | 原CltDo25视觉环；服务器雷伤 |
| 49 Lightning | 通用Handler；MissMode SrvDo1 | C、D最小值门槛、seq12、速度／碰撞 | 服务端穿透／NextDelay；客户端CltDo8链段图 |
| 50 Shiver Armor | SkillSor SrvDo18、EventFunc03 | C、D、L、等级防御／期限 | 服务端近战尝试反击；客户端状态／命中 |
| 51 Fire Wall | SkillSor SrvDo24；MissMode SrvDo6／Dmg3 | C、D、missileWallDirection、跨格、速度 | 两侧maker生成各端执行，服务器真实火段伤害 |
| 52 Enchant | SkillSor SrvDo25 | C、D、线性期限／命中率 | 服务端友方资格、来源及成组事务；客户端原状态 |
| 53 Chain Lightning | SkillSor SrvDo26；MissMode SrvHit12 | C、D最小值门槛、seq12、跳数、GUID successor | 真实候选／再次命中归服务端；客户端CltHit16可见链段 |
| 54 Teleport | SkillSor SrvDo27 | C、位置／碰撞几何 | 服务端资格／位移；客户端原位置校正 |
| 55 Glacial Spike | 通用Handler；MissMode SrvHit13 | C、D、L、范围／冻结等级曲线 | 服务端接触范围冷伤／冻结；客户端CltHit14冰图 |
| 56 Meteor | SkillSor SrvDo28；MissMode SrvHit14 | C、D；地面火evaluateMissileDamageFixed；范围／期限 | 18个权威火偏移与客户端CltDo9／Hit18图层分开 |
| 57 Thunder Storm | SkillSor SrvSt13／SrvDo29 | C、D、linear期限、dm56递减周期 | 服务端定时选敌／落雷；客户端原A3瞬时图 |
| 58 Energy Shield | SkillSor SrvDo23、EventFunc24 | C、吸收上限／TK协同、dm12期限、absorbSkillShield | 服务端受击扣蓝／生命／状态事务；客户端原状态 |
| 59 Blizzard | SkillSor SrvDo28；MissMode SrvDo10／CreateMissileWithCollisionCheck | C、D、missileEmissionDue／blizzardOffset | 独立随机、客户端反向偏移／四变体，实际碎片碰撞归服务端 |
| 60 Chilling Armor | SkillSor SrvDo18、EventFunc01 | C、D、L、防御／期限、速度 | 服务端ReturnFire资格／反击弹体；客户端原状态／动作 |
| 61 Fire Mastery | D2Common RefreshSkill/passivecalc | P：skillRankBonus，D中支配阶段 | 服务端有效等级加伤，客户端已知值提示 |
| 62 Hydra | SkillSor SrvSt14／SrvDo144；MonsterAI Hydra | C、D、linear期限／名额、337原弹体曲线 | 服务端三头身份／归属／生命周期／AI；客户端原指派与动作 |
| 63 Lightning Mastery | D2Common RefreshSkill/passivecalc | P：skillRankBonus，D中支配阶段 | 服务端有效等级加伤，客户端提示 |
| 64 Frozen Orb | 通用Handler；MissMode SrvDo15／16／SrvHit29 | C、D、L、remaining调度／环形／转向／速度 | 独立Clt19／20／Hit30程序，服务端子弹重新求伤害 |
| 65 Cold Mastery | D2Common RefreshSkill/passivecalc | P：skillRankBonus | 服务端抗性穿透且不破免疫，客户端提示；不是冷伤百分比 |

依据入口：[D2Skills.cpp](../../../reference/d2moo/source/D2Common/src/D2Skills.cpp)的EvaluateSkillFormula／GetManaCosts／GetMinElemDamage／GetMaxElemDamage／GetElementalLength／D2Common_11033，[Units/Missile.cpp](../../../reference/d2moo/source/D2Common/src/Units/Missile.cpp)的MISSILE_CalculateDamageData及元素曲线，[SkillSor.cpp](../../../reference/d2moo/source/D2Game/src/SKILLS/SkillSor.cpp)全部SrvSt／SrvDo／EventFunc，[MissMode.cpp](../../../reference/d2moo/source/D2Game/src/MISSILES/MissMode.cpp)的弹体程序。序列／客户端原DLL导入和已认证差异见[参考设计](../../architecture/REFERENCE_DESIGN.md#女巫公共层的原版核对)。这张表核对公共职责、函数分派及本次计算差异，不把源码核对当作全参数、逐帧原服认证。

## 边界

本页覆盖女巫主动／被动执行；后续亚马逊及通用技能范围见对应专题。自研其他职业主动、PvP、充能／触发技能、跨区弹体、完整受击恢复、完整怪物远程AI或同行者传送。原服客户端仍无权判伤。心灵传动的客户端交互等待复用原仓库／传送点确认机制，地面物品目标来自原物品副本；失败拾取由原2C音效13（十六进制）反馈。原表CelFile=null且有Light的弹体仅保留光源，不能制造替代贴图。晚入局恢复当前单位及状态，不重播已开始的弹体／施法。碎冰及爆炸图的精确密度、连锁在延迟／可见性变化下的像素级时序、射流循环音等仍不能宣称原版逐帧一致；旧单机冒烟不是本轮认证。

## 历史女巫有限运行证据

2026-10-08女巫30项改造对应提交2fcc439的历史源码和包。全部技能源码写完后开始Windows Release构建及冒烟，发现缺口后修复、重新构建和打包；没有新增测试脚本、用例或专用程序。最终增量构建无编译warning／error，build/bin与dist/current的SHA256一致：EXE `C19749017A53A1F226205F4E2E519369F38E487554F3FE643B77A5CA8FF86A37`；DLL `9F7B1FAF7AF453E3DC481F05207C76460AECC38FF33A50E92823032228B2969F`。D2S仍为v96，规则指纹admission-v17/native-wire113c/d2s96/sorceress30。产物、JSON、日志、截图及临时存档保留在忽略目录`artifacts/sorceress-smoke-20261008`及`artifacts/sorceress-d2gs-20261008`。

主要角色为普通难度41级女巫Hero、种子1961888707，30项基础等级均为1，初始法杖使FireBolt有效等级2。原3B逐项学习、原3C选择、原4C／4D动作、73／A3／A7／A9等公开结果均由同一客户端消费。管理管道仅准备真实MPQ怪物／资源、施加指定伤害和推进固定步；伤害仍进入combat／effects／death。运行覆盖不等于原版逐帧或全参数验收。

| 技能范围 | 实际观察与证据 |
| --- | --- |
| 直线、环射与范围 | FireBolt、ChargedBolt、IceBolt、FrostNova、IceBlast、FireBall、Nova、Lightning、ChainLightning、GlacialSpike、Meteor、Blizzard、FrozenOrb均观察释放、扣蓝及普通怪物生命变化；`skill-<ID>-release/hit/client.json`。冰弹减速、冰风暴冻结普通Brute5有独立快照。新星／陨石表现缺口修复后用`final-skill-44/48/56/59.json`复查，effectLimitations为空、ignoredPackets=0 |
| 持续、周期与召唤 | Inferno开始／转向不重置started，停止后待释放和弹体清零；同机TCP另一端查看持续火焰、转向及停止，`tcp-inferno-fixed-*`及截图。Blaze移动产生六段真实火；FireWall段伤及到期、ThunderStorm目标原A3、Hydra三头原身份／337攻击／受强化／期限清理已观察，见对应blaze／firewall／thunder／hydra证据 |
| 装甲、强化与护盾 | 三种装甲状态互斥；FrozenArmor冻结近战攻击者、ShiverArmor反击造成冷伤；ChillingArmor通过真实敌怪来源及原ReturnFire弹体93的管理命中入口观察权威264反击。EnergyShield承受20物理伤害，生命扣16且同时扣蓝。Enchant自身、Hydra及同机TCP玩家Buddy的状态16与施法者扣蓝已观察，见shield-hit、chilling-reaction及tcp-enchant-* |
| 直接技能与远程交互 | StaticField按生命比例减血、Telekinesis伤害／原击退、Teleport位置校正与扣蓝已观察。TK对真实仓库／传送点由原77／63确认开窗；药水远程移入背包，手斧仍在地面并收到原2C音效0x13。`tk-*-final/fixed.json`与最终`delivery-tk-stash-*`截图确认开关界面 |
| 被动与保存 | 全30项基础技能原包及保存往返；Warmth恢复已观察，三支配参与服务端纯计算。F3右手FrozenOrb绑定经原51／7B跨进程恢复。最终`delivery-reload.json`／`delivery-native.json`保留等级41、体力110、精力135、30项基础等级和热键，短时状态／施法／弹体／Hydra不恢复；拾尸后法杖仍在本人装备容器 |

冒烟发现并修复：Hydra A1事件使用原flag2、HydraMissile337从原表独立取得；原表CelFile=null且有Light的弹体不要求DCC；有限WAV在StopInst=0时允许自然FadeOut；重复选择当前技能仍发送原23确认；持续Inferno快照不能按每帧until误发站立包；TK仓库／传送点需要客户端交互等待，地面物品从原物品副本选目标；存储关闭后公共面板清理。修复各归内容准备、领域、hosting或公共客户端，没有自研协议／玩法分支。

原服代表技能回归见下节；有限冒烟未覆盖全30项原服验收、跨机器LAN、Linux、长期稳定性、PvP／充能／触发、全部装备／协同等级／三难度及免疫边界、极限召唤／输出背压和断线压力。ChillingArmor只验证服务端指定命中反击入口，未认证完整怪物远程AI与客户端实际远程碰撞反击；音频原表及程序已接，没有全技能人工听音或逐帧像素比对。晚入局不重播历史施法／弹体。当前为V2有限运行，不能标为原版完整认证。

最终交付进程failures=0、lastFailure为空、stderr为空，正常退出后角色锁释放。此前技能和TCP冒烟使用修复过程中的包；对应过程包已验证30项存档／热键恢复及TK仓库开关。最终包补做下述原服及自研发包收敛复验。公共层收尾又移除了弹体速度、seq6／seq12定义与被动线性值重复，完整共享范围及原版模块证据见[公共层及原版证据](#公共层及原版证据)和[参考设计](../../architecture/REFERENCE_DESIGN.md#女巫公共层的原版核对)。全部30项已按D2MOO核对公共职责及原函数分派，冰封球remaining、本人FCR、施法不叠加OtherAnimationRate、固定最小雷伤及Meteor地面火取整已按确认的原版依据修正；SkillRuleSpec与资源表现定义分离。完整路径量化和原版两端独立的地面程序仍各自适配，不能声称完整技能执行器已共用。本批纳入git提交。

### 原服代表技能与发包收敛

使用本机既有1.13c D2GS／PvPGN栈及测试角色SkillTestSor（普通难度99级、30项基础等级1）。原服角色来自D2DBS；没有将自研存档上传或使用自研管理命令修改原服。宿主是本地适配的RElesgoe构建，不能据此认证所有纯原版规则。证据在忽略目录`artifacts/sorceress-d2gs-20261008`。

- 原服实际观察：心灵传动真实仓库／传送点的77／63开窗；传送位置校正及扣蓝；暴风雪可见并造成怪物死亡；连锁闪电目标从128生命比例变为0；Hydra三头原351／352／353身份及4C技能337；冰封球散射；陨石标记、落地火区；火墙两侧展开；烈焰之径移动留火。Inferno只确认开始、转向和停止的客户端显示，未认证原服连续伤害／扣蓝节奏。关键截图及对应完整JSON按技能命名。
- 原服常规施法没有回送本人4C／4D，也没有发送上述地面程序的73创建包。依据D2MOO的PlrMsg::sub_6FC81D20、SUnitMsg::FirstFn／MISSILES_SyncToClient及原D2Client的CltDo26／28，修复公共客户端在目标点重建火墙／陨石／暴风雪、火墙跨格生成火段与状态移动火段；ClientSend不再解释成必须等待73。73仍可消费视野同步；无GUID的可重建弹体仅在有界释放时窗配对不同生成来源，不合并两次独立施法。
- 自研服务端同时修正：上述四类技能及其子火段不再重复广播常规73，本人普通动作不回送4C／4D，其他可见客户端仍发送动作。最终包在`server-final`复验火墙／暴风雪／陨石／烈焰之径显示，原包计数中没有对应重复73或本人4C／4D；心灵传动药水由地面mode3移入mode0背包；Inferno转向、停止后casts／pendingReleases／missileCount均清零，failures=0。地面物品预测也复用原物品位置，不依赖本人动作回包。

公共层核对后的最终包在`common-final`再次运行：自研连接逐项释放Lightning／ChainLightning／FrozenOrb／Meteor／Blizzard／Inferno，观察扣蓝及对应权威弹体；停止Inferno后casts／pendingReleases／missileCount均为0，failures=0，客户端effectLimitations为空。同一客户端连接D2GS，在真实鲜血荒地复验雷系、冰封球散射、陨石标记及暴风雪显示，effectLimitations为空、协议仍为ProtocolReady；此轮地狱火点施法未捕获持续火焰，不能据此增加其原服认证范围。未另行认证非零FCR装备、所有协同／等级或精确数值／时序。JSON与截图记录实际观察，不代替原版逐帧比对。

原服既有未消费包仍保留诊断（早一轮连接ignoredPackets=16，公共层最终连接为40），不能声称全协议无缺口。一次原服游戏握手超时，经单独正常重启本次启动的D2GS后恢复；原有账号／Realm／D2DBS服务未重启。最终客户端正常退出、角色锁释放、stderr为空；本次启动的D2GS已正常关闭。未复验所有女巫参数、多人观察时序、晚入视野弹体同步或原服启动稳定性。
