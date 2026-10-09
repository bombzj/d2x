# 亚马逊技能

更新：2026-10-09。自研服务端执行24项主动、6项被动，原服与自研使用同一原协议客户端。本页维护逐技能职责、亚马逊专属Clt／Srv差异及历史有限证据；公共规范见[COMMON](COMMON.md)，当前包见[基线](../../../BASELINE.md)。

## 30项职责核对

SrvSt／Do为当前MPQ分派，—表示没有该回调，不能据此判断未实现；被动由属性／战斗计算消费。公共计算不拥有世界候选、命中、扣费或实体；表中Clt程序只创建视觉效果。等级／耗蓝／武器伤害等通用语义见[跨职业计算](COMMON.md#跨职业公共计算)。

| ID／技能 | SrvSt／Do | 公共计算与权威执行 |
| --- | --- | --- |
| 6 魔法箭 Magic Arrow | —／— | resolveSkill、weapon_damage：魔法转换及附加伤害；不扣箭，仍复验弓弩／等级 |
| 7 火焰箭 Fire Arrow | 4／— | 原伤害曲线、火转换、SrcDamage；weaponCost提交弹药与法力 |
| 8 内视 Inner Sight | —／6 | amazon_magic的范围、期限和防御减值；effects按原过滤器施加状态属性 |
| 9 双倍打击 Critical Strike | 被动 | amazonPassiveValue原递减曲线；武器命中掷一次暴击，再按原顺序处理致命攻击 |
| 10 戳刺 Jab | 5／7 | amazonWeaponSequence的原seq1及weaponSequenceTick；三次动作事件分别复验目标并结算 |
| 11 冰箭 Cold Arrow | 4／— | 原冷伤／转换／长度；命中及冷时长／难度减免归combat |
| 12 多重箭 Multiple Shot | 4／8 | 整数扇形、原数量和SrcDamage；一次扣费、各箭独立命中及穿透 |
| 13 闪避 Dodge | 被动 | 原递减曲线及weaponAvoidance；站立／近战分支与躲避动作分开 |
| 14 威力一击 Power Strike | 6／2 | 原武器六通道与附加雷伤；skills拥有A1释放帧，combat处理近战命中 |
| 15 毒枪 Poison Javelin | 4／— | 原毒伤／长度、poisonCloudDirections和cloudVelocity；missiles生成尾迹毒云，combat处理持续毒 |
| 16 爆裂箭 Exploding Arrow | 4／— | 箭本体与原火范围分开；命中回调与AlwaysExplode按原MissMode条件执行 |
| 17 慢速箭 Slow Missiles | —／6 | amazon_magic的原减速百分比／期限；单位状态和弹体速度分别消费原值 |
| 18 躲避 Avoid | 被动 | 原递减曲线；站立远程回避，不叠加移动Evade分支 |
| 19 刺爆 Impale | 7／2 | 原seq8、武器ED与磨损概率；命中后耐久／数量由inventory事务提交 |
| 20 闪电球 Lightning Bolt | 4／— | 原物理转雷、自动命中和投掷数量；missiles及combat保留转换后的六通道 |
| 21 急冻箭 Ice Arrow | 4／— | 原冷伤与冻结时长；普通冷却／冻结资格分开，保留目标难度系数 |
| 22 导引箭 Guided Arrow | 4／10 | missileGuidedDirection按remaining转向；服务端寻找真实敌人，客户端执行CltDo18／MissileCltDo7 |
| 23 刺入 Penetrate | 被动 | 原线性命中率加成；有效等级进入派生属性，女武神继承基础等级规则另算 |
| 24 充能一击 Charged Strike | 6／11 | 原近战雷伤、充能弹数量与轨迹；子弹从2×受击点−攻击者点生成，客户端CltDo19 |
| 25 瘟疫标枪 Plague Javelin | 4／— | 原毒曲线／期限、16方向云团和整数速度；弹体爆散与每目标持续毒由独立领域执行 |
| 26 炮轰 Strafe | 8／12 | strafeShotCount及weaponVolley原回滚时钟；CltDo20和服务端共享事件计算，各自筛选目标 |
| 27 牺牲之箭 Immolation Arrow | 4／— | 原箭伤、独立范围伤／地面火及missileDiskOffsets；Srv期限与CltHit12期限／概率分别取原参数 |
| 28 诱饵 Dopplezon | —／15 | resolveAmazonSummon：生命／抗性／期限；原人物伪装／主人信息，不复制物品；固定位置、单体替换及死亡清理 |
| 29 回避 Evade | 被动 | weaponAvoidance的移动分支；移动时仅执行Evade，服务端提交原躲避动作 |
| 30 击退 Fend | 9／13 | weaponVolley原目标数／回滚；每次释放重新取近战敌人，CltDo21使用已知单位表现 |
| 31 冻结之箭 Freezing Arrow | 4／— | 箭本体与冷范围分开；服务端范围冻结，CltHit14按原3组／8方向生成碎片 |
| 32 女武神 Valkyrie | —／16 | resolveAmazonSummon、继承被动及原MonEquip；宿主准备装备，companions持有归属／AI，monsters持有生命／动作 |
| 33 穿透 Pierce | 被动 | amazonPassiveValue及missilePierceCount；原owner pierce_idx独立种子，最多4次穿透，不污染战斗随机流 |
| 34 闪电攻击 Lightning Strike | 10／14 | skillRankBonus完整弹跳数、missileChainSuccessor；主近战与独立连锁分开，CltDo22不套女巫的／5规则 |
| 35 闪电之怒 Lightning Fury | 4／— | MissileTargetBurst／missileBurstTargets：半径、数量及顺序；SrvHit20／CltHit25在单位、墙及期限回调分裂，子雷弹无武器伤害 |

## 专项计算与领域入口

| 范围 | 当前入口与边界 |
| --- | --- |
| 序列／回滚 | `amazon_sequence`、`weapon_volley`求Jab／Impale原序列及Strafe／Fend事件；skills持有IAS时钟和每次目标复验，客户端将相同事件映射至原动作 |
| 箭矢／标枪 | `amazon_missile`、`projectile_path`求扇形／圆盘／毒云、引导／GUID继任及分裂候选；missiles持有接触、期限、毒云／地面火和Pierce游标 |
| 武器伤害 | `weapon_damage`纯计算六通道／转换／Critical／Deadly／SrcDamage；近战先按目标修正范围再掷值，投射先掷值再加目标ED。随机与真实抗性／冷毒／NextDelay归combat，不由客户端重掷 |
| 效果／回避 | `effects/amazon`安装Inner Sight／Slow Missiles及属性；poison处理强毒替换／同强续期，avoidance区分近远程／移动。弹体减速和单位状态分别消费原参数 |
| 召唤 | `resolveAmazonSummon`求纯数值；hosting准备MonEquip，companions持有归属／期限／AI，monsters唯一持有生命／位置／装备。诱饵无装备／空库存替身，女武神持有独立真实物品 |

女武神准备结果绑定请求、独立种子、装备revision、等级、基础技能及区域代次；自然回蓝不取消准备，提交时仍复验法力。物品及孔内项使用实例统一新ID，伙伴退役不发敌怪经验／掉落。死亡、替换、到期及主人离开由companions清理；女武神攻击／同行／换区在 `companions/amazon_ai`，诱饵保持固定位置。

## 原版依据与客户端差异

迁入起点为master `624c927b22bebc849d35031235545683933a7dc8` 的 `physical_projectiles`、`missile_rules`／`missile_dispatch`／`native_projectiles`、`monsters/companions`、`session_necro_summons`及现有 `content/skills/amazon_*_data`。当前MPQ的Skills／Missiles／States／PetType／MonStats／MonStats2／MonEquip／ItemStatCost、COF及AnimData决定参数；本地D2MOO SkillAma、D2Skills／SequenceTbls、SUnitDmg／MissMode／Missiles、SkillNec、PlayerPets／SCmd定位执行职责。参考重建的X／Y笔误及Calc语义差异不能覆盖原表；版本见[资料来源](../../resources/THIRD_PARTY.md)，模块证据见[参考设计](../../architecture/REFERENCE_DESIGN.md#亚马逊公共层的原版核对)。

| 专项 | 原版依据／必须保留的差异 |
| --- | --- |
| Clt程序 | 本地1.13c D2Client基址0x6FAB0000：CltDo18–22对应RVA15C40／15AB0／15820／14FD0／15660；MissileCltDo3／4／7为BBE60／BBE00／B2EB0，CltHit12／14／25为BB070／B0870／BAEF0。静态入口不等于逐帧运行认证 |
| 牺牲箭／闪电攻击 | CltHit12按父弹体Range／LevRange求期限，Srv使用SHitCalc1；CltDo22使用Calc1范围及完整弹跳数，不套女巫Chain Lightning的计数规则 |
| 本人显示 | 已完整解码的IAS、武器速度及Pierce参与时序／表现；远端未知隐藏值仍未知 |
| 宠物投影 | 原7A固定13字节名册独立于AC可见单位，离开视野只移除单位；死亡／退役删除归属，晚入局重建名册。9D的monster ownerType装备不进入本人库存，孔内项沿真实归属清理 |
| 诱饵／女武神 | SrvDo015不生成MonEquip或复制主人库存；状态gfxtype／gfxclass与原主人信息用于人物伪装，出现效果不依赖装备。女武神独立MonEquip。AC按SCmd保留生命字段、总长、31位storedOwner及诱饵UMod21；D2Common的DT／DEAD武器类为HTH，但重甲死亡组件仍待核实 |
| 弓弩／箭袋 | ItemMode::sub_6FC43280允许bow／bowq、xbow／xboq双手组合；RemoteInventory直接发0x1A，不先移除匹配箭袋，已获原服真实回应 |

原服普通本人动作及动作派生弹体的发包原则见[COMMON](COMMON.md#定义来源与客户端状态)，不为自研另写客户端协议或画面分支。

## 边界

- 女武神已生成装备、执行基础伤害／属性／支持被动，附加吸取／压碎／撕裂等装备战斗效果尚未结算；不代表完整物品效果。玩家武器拒绝条件见GENERAL。
- 原宠物名册、伪装及装备已接，宠物头像HUD未接。当前已接血乌奖励罗格不等于完整雇佣／复活／装备／成长，后续范围见[佣兵](../characters/HIRELINGS.md)。
- 女武神重甲死亡时请求的 `amtrhvydthth.dcc` 在当前MPQ缺失；原版组件选择待核实，不猜测替图。`effectLimitations`为空不足以证明这条路径正常。
- 导引箭实际追踪命中、原服Fury子弹画面、所有被动随机分支、像素／音频、完整路径量化、三难度／全装备／取消／背压及全30项原服仍未认证。普通攻击／充能／装备触发范围由GENERAL和库存维护。

## 有限运行证据

2026-10-08历史提交 `53579f3`，当时指纹v18／amazon30、D2S v96，EXE SHA256 `69F50C17AE89D063C8D81333139D820FF63EB17B34466F79535FF010312B4C37`。逐项构建记录在 `artifacts/amazon-implementation-20261008`；运行在 `artifacts/amazon-smoke-20261008`、`artifacts/amazon-d2gs-20261008`，均为忽略目录。历史 `dist/current` 已被后续包替换，这些记录不认证新增分支。

| 环境 | 条件、观察及证据 |
| --- | --- |
| 自研 | Hero，普通难度41级，种子3739460588；全部30项基础等级1。magic-* 不耗箭但首发未命中；multiple-* 两箭及一箭成本；guided-* 释放／轨迹而非导引命中；immolation-* 原 Brute 死亡及230火；jab-* 分次命中；fury-* 原231子弹、两只 Brute 死亡、标枪60→59；final-reload* 跨进程恢复全部基础技能和扣费后的箭／标枪，召唤不恢复 |
| 最终伙伴复查 | wire-valkyrie* 544生命、spr／ful及7A／9D，管理死亡模式 effectLimitations 为空，但 stderr 报缺 amtrhvydthth.dcc，不能据此认证死亡画面；wire-decoy* 固定位置71生命，不复制主人库存，到期移除；wire-final failures=0；最后去除空库存后的 delivery-decoy*／delivery-final 复查原包、出现效果及到期，failures=0、stderr为空。早期带复制装备的 decoy-final 不是最终修复证据 |
| 原服 | 新角色 AmaSmokeOct，原服生成 v96 D2S；用独立 @dschu012/d2s 2.0.36 仅准备这个临时角色，未上传自研 D2S。原服购买短弓及350箭；Multiple Shot 两箭／扣一箭／扣蓝，Guided Arrow 释放轨迹／扣费，Dopplezon 人物伪装／7A且不额外生成9D装备，Valkyrie 7A／9D原 spr／ful装备及最终重入复查，Lightning Fury 标枪60→59、扣蓝及原 Brute 死亡。见 multiple-*、guided-*、decoy*、valkyrie*／wire-valkyrie*、fury-* |

普通施法未见本人4C／4D或相应重复73；既有未消费包仍有诊断。21:27:26原服watchdog报告疑似deadlock并结束房间，原因未知；重启本次启动的D2GS后完成女武神复查，不能计为原因已修复或长期稳定性证明。

列出路径为有限V2，其他已构建路径V1。保存仅认证全部30项基础技能及扣费后的箭／标枪往返，召唤不恢复；格式及当前规则指纹见[SAVES](../../modules/SAVES.md)。没有本批Linux、全参数或逐帧认证。
