# 亚马逊技能

PD2专项内容：[职业技能](../../pd2/CLASS_SKILLS.md)。本页保留原版／公共规则及必要的版本边界。

更新：2026-10-09。自研服务端执行24项主动、6项被动，原服与自研使用同一原协议客户端。本页维护逐技能职责、亚马逊专属Clt／Srv差异、原版依据和缺口；公共规范见[COMMON](COMMON.md)。

内视／慢速箭从当前MPQ AuraRangeCalc／AuraLenCalc／AuraStatCalc1读取原公式，公共期限算子保留Curse状态的抗性取整。首项属性在原始列表投递前分配，同级重施保留原首项；状态事实读取实际复用列表。玩家费用、目标属性／资源、旧诅咒撤出及控制回调随同一候选提交，不再只改变单位状态显示。依据D2MOO SkillAma::SrvDo006及AuraCallback_InnerSight_SlowMissiles。内视／慢速箭使用与光环共用的nativeAuraCandidateRecipients：半径保留signed32、目标按当前MPQ相邻房间及房间单位链顺序采样，保留NoAura、Boss过滤、城镇／伤害资格／敌意及整数距离。玩家PvP接收者和非玩家共用首项来源／同级复用规则，扣费与多个玩家／单位候选一起复验；跨区域状态事实按接收者区域发送。零或负半径不遍历目标。全部等级／多人／跨房组合、原资格掩码完整闭包及全局事件时序仍待对齐。

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
| 14 威力一击 Power Strike | 6／2 | St6起手Alloc持有原生联系，Do2兑现；六通道／雷伤与源、目标单位种子按原阶段消费 |
| 15 毒枪 Poison Javelin | 4／— | 原毒伤／长度、poisonCloudDirections和cloudVelocity；missiles生成尾迹毒云，combat处理持续毒 |
| 16 爆裂箭 Exploding Arrow | 4／— | 箭本体与原火范围分开；命中回调与AlwaysExplode按原MissMode条件执行 |
| 17 慢速箭 Slow Missiles | —／6 | amazon_magic的原减速百分比／期限；单位状态和弹体速度分别消费原值 |
| 18 躲避 Avoid | 被动 | 原递减曲线；站立远程回避，不叠加移动Evade分支 |
| 19 刺爆 Impale | 7／2 | 原seq8、武器ED与磨损概率；命中后耐久／数量由inventory事务提交 |
| 20 闪电球 Lightning Bolt | 4／— | 原物理转雷、自动命中和投掷数量；missiles及combat保留转换后的六通道 |
| 21 急冻箭 Ice Arrow | 4／— | 原冷伤与冻结时长；普通冷却／冻结资格分开，保留目标难度系数 |
| 22 导引箭 Guided Arrow | 4／10 | missileGuidedDirection按remaining转向；服务端寻找真实敌人，客户端执行CltDo18／MissileCltDo7 |
| 23 刺入 Penetrate | 被动 | 原线性命中率加成；有效等级进入派生属性，女武神继承基础等级规则另算 |
| 24 充能一击 Charged Strike | 6／11 | St6起手分配，Do11先Drain再创建原充能子弹；数量读取calc1，目标方向为2×受击点−攻击者点，CltDo19 |
| 25 瘟疫标枪 Plague Javelin | 4／— | 原毒曲线／期限、16方向云团和整数速度；弹体爆散与每目标持续毒由独立领域执行 |
| 26 炮轰 Strafe | 8／12 | 服务端每次发射后按当前帧、动画锚点及Param6回卷重排；GUID后继选择。strafeShotCount／weaponVolley仅供客户端CltDo20预测 |
| 27 牺牲之箭 Immolation Arrow | 4／— | 原箭伤、独立范围伤／地面火及missileDiskOffsets；Srv期限与CltHit12期限／概率分别取原参数 |
| 28 诱饵 Dopplezon | —／15 | resolveAmazonSummon采样原Calc及Aura属性；主人生命保留raw 8.8，基础／召唤来源逐步安装；原人物伪装／主人信息，不复制物品；固定位置、单体替换及死亡清理 |
| 29 回避 Evade | 被动 | weaponAvoidance的移动分支；移动时仅执行Evade，服务端提交原躲避动作 |
| 30 击退 Fend | 9／13 | 服务端保留当前目标，失效／出范围时找GUID后继；原生Alloc／Drain后按Param2回卷。1.13c查找助手在回绕时也写候选数，与固定D2MOO提前返回不同；CltDo21仍用已知单位预测 |
| 31 冻结之箭 Freezing Arrow | 4／— | 箭本体与冷范围分开；服务端范围冻结，CltHit14按原3组／8方向生成碎片 |
| 32 女武神 Valkyrie | —／16 | 原Calc／SumSkill赋值、原层被动及MonEquip来源；宿主准备装备贡献，属性模块构建原始快照，companions持有归属／AI，monsters持有生命／动作 |
| 33 穿透 Pierce | 被动 | amazonPassiveValue及missilePierceCount；原owner pierce_idx独立种子，最多4次穿透，不污染战斗随机流 |
| 34 闪电攻击 Lightning Strike | 10／14 | St10分配、Do14先Drain再按GUID后继创建连锁，次数读取calc2；采用原1.13c的X／Y目标，保留与固定D2MOO的Y／Y差异；CltDo22不套女巫的／5规则 |
| 35 闪电之怒 Lightning Fury | 4／— | MissileTargetBurst／missileBurstTargets：半径、数量及顺序；SrvHit20／CltHit25在单位、墙及期限回调分裂，子雷弹无武器伤害 |

## 专项计算与领域入口

武器费用由inventory/weapon_cost与transactions原子提交弹药、数量／耐久、法力及公开事实。接触命中结果只掷一次交给combat；范围回调与箭本体保留不同发生标识，背压不重复扣费或掷伤。

| 范围 | 当前入口与边界 |
| --- | --- |
| 序列／回滚 | `amazon_sequence`准备Jab／Impale原序列，服务端timeline驱动标记与回绕；weaponVolley仅供客户端表现。Impale的专用耐久／数量机会与普通Drain磨损分别执行 |
| 箭矢／标枪 | `amazon_missile`、`projectile_path`求扇形／圆盘／毒云、引导／GUID继任及分裂候选；missiles持有接触、期限、毒云／地面火和Pierce游标 |
| 武器伤害 | `weapon_damage`纯计算六通道／转换／Critical／Deadly／SrcDamage；近战先按目标修正范围再掷值，投射先掷值再加目标ED。随机与真实抗性／冷毒／NextDelay归combat，不由客户端重掷 |
| 效果／回避 | `effects/amazon`安装Inner Sight／Slow Missiles及属性；poison处理强毒替换／同强续期，avoidance区分近远程／移动。弹体减速和单位状态分别消费原参数 |
| 召唤 | `resolveAmazonSummon`求纯数值；hosting准备MonEquip，companions持有归属／期限／AI，monsters唯一持有生命／位置／装备。诱饵无装备／空库存替身，女武神持有独立真实物品 |

诱饵和女武神现在都经过现有准备边界，结果绑定请求、独立种子、装备revision、等级、基础技能及区域代次；自然回蓝不取消准备，提交时仍复验法力。物品及孔内项使用实例统一新ID，伙伴退役不发敌怪经验／掉落。死亡、替换、到期及主人离开由companions清理；女武神攻击／同行／换区在 `companions/amazon_ai`，诱饵保持固定位置。

女武神已有WL／RN路线时，AI轮询只等待路线结束，不重选敌人或再次提交同一路径，避免重置定点步进；换区／远距归位仍先处理，诱饵不进入移动AI。依据为本地D2MOO Fn067_NecroPet、AiTactics移动及MonsterMode的WL／RN结束回调；完整目标选择、同行规则与原版逐帧时序仍待对齐。

NativeSummonStats保存显式主人公式输出：Calc1／Calc2／诱饵Calc3、五项Passive加值、六项Aura非零写入、五个SumSkill／SumSkCalc槽及普通／L列MonLvl加值。诱饵主人生命不先截成整点；初始及百分比生命使用原整数算子并保留Fixed8。当前两个MonStats记录按已核实noRatio=1准备，初始生命先掷整点、限23位再左移8位；其他ratio模式不由此推断支持，完整诱饵／女武神流程仍待对齐。

attributes/prepareSummonStatSources从基础列表依序安装初始技能、召唤等级／MonLvl加值、Passive基础AddStat、Aura独立列表、生命Calc增长和SumSkill赋值，再投递装备来源。继承技能按参数层与原同等级缓存调用共享被动刷新；装备加技能后重新求有效等级。最大生命变化沿MonsterResourceTrace回调，原始生命／上限与来源一起落入单位候选。MonEquip贡献从同一native_sources入口保留原始属性、孔内项和参数层，临时物品ID及其嵌套来源统一绑定实际ID；基础MonStats抗性与技能／装备抗性不合并为另一份基础值。怪物公式、状态和选定武器查询已能读取这份来源，模式表共用当前MPQ的不可变目录。

当前源码中，女武神在HP前按原组件计数抽签；MonEquip只跳过开头等级过高的记录，后续连续记录均消费选槽随机，重复槽位仍抽单选。选槽消费宠物单位流，实际装备构造使用游戏流双种子及公共物品品质程序，保留NOSELL、禁止自然无形／加孔和修复耐久；强制装备仅在局部统计时暴露完整属性，不改真实鉴定与需求值。宿主返回分配次数和游戏流终值，服务端复验并随召唤事务提交，不在内容等待前提前提交单位种子。首个AI事件直接按诱饵25帧／女武神20帧进入出生候选，避免额外下一帧空回调。

普通出生寻位现复用怪物出生搜索的−1／4回退、原房间边界、辅助碰撞和原生动态占位；搜索贷款随召唤事务提交，寻位失败只保留实际房间随机消费。宿主仍保留部分装备动画／过程参数准备；跨区域出生、创建失败随机副作用、异步准备期间的完整主人属性采样、所有物品／Aura事件订阅、动作／移动／恢复时序及生命周期仍待后续迁移。这里是源码来源接入，完整SrvDo015／016逐帧流程仍待对齐。

召唤装备按构造顺序记录库存链与原始属性来源；服务端发布前重绑真实物品／孔内项、装备容器主人、位置及注册武器，预先核对数量与重复身份。宿主结果安装重放组件、初始生命及女武神选槽投掷，游戏随机终值由人物事务复验并发布。此处只补足身份及随机交接，不代表全部逐件装备回调完成。

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

A7／A8／A9承载伪装与单位状态属性；7F向主人同步宠物生命／区域，独立于可见单位。头像投影与未专项复验边界见[召唤物与佣兵头像](../characters/HIRELINGS.md#召唤物与佣兵头像)。运行态伙伴、独立装备及内容准备不写角色D2S；全部PvP伙伴组合与完整受击动作仍待对齐。

## 边界

- 女武神已生成装备、执行基础伤害／属性／支持被动，附加吸取／压碎／撕裂等装备战斗效果尚未结算；不代表完整物品效果。玩家武器拒绝条件见GENERAL。
- 原宠物名册、伪装及装备已接；头像按原PetType显示女武神，诱饵的icontype=0不显示。佣兵服务及头像的实现、原版依据与缺口见[佣兵](../characters/HIRELINGS.md)。
- 女武神重甲死亡时请求的 `amtrhvydthth.dcc` 在当前MPQ缺失；原版组件选择待核实，不猜测替图。`effectLimitations`为空不足以证明这条路径正常。
- 导引箭实际追踪命中、原服Fury子弹画面、所有被动随机分支、像素／音频、完整路径量化、三难度／全装备／取消／背压及全30项原服仍未认证。普通攻击／充能／装备触发范围由GENERAL和库存维护。

## 原服参考

2026-10-08使用D2X客户端连接原服，角色AmaSmokeOct由原服生成v96 D2S，独立`@dschu012/d2s` 2.0.36仅用于准备该临时角色，未上传自研D2S。原服购买短弓及350箭后，Multiple Shot生成两箭并扣一箭／法力；Guided Arrow有释放轨迹及扣费，未取得导引命中的完整对照；Dopplezon提供人物伪装／7A，不额外生成9D装备；Valkyrie提供7A／9D及原spr／ful装备，重新入局后亦有复查；Lightning Fury使标枪60→59、扣蓝，目标Brute死亡。上述服务端消息与数量可作为所列原服行为参考，客户端轨迹和伪装画面仍由D2X绘制。

普通施法未见本人4C／4D或相应重复73；既有未消费包仍有诊断。21:27:26原服watchdog报告疑似deadlock并结束房间，原因未知；重启本次启动的D2GS后完成女武神复查，不能计为原因已修复或长期稳定性证明。

角色档保存基础技能及物品数量，召唤不跨局恢复；格式及校验边界见[SAVES](../../modules/SAVES.md)。完整参数、逐帧行为及Linux适用边界仍待核实，不按自研运行记录划分完成等级。

原版闪电标枪的客户端补入CltDo46／Hit51：B7880在elapsed0／1从DynamicPath.tTargetCoord与当前整数格计算垂直方向，读取上次换格位后向正反两侧各创建CltSubMissile1；CltParam1仅在子项SubLoop启用时增加(SubStop-SubStart)×循环数的寿命，保持Skill／Level与原图。当前431／438的参数3、子432／439的Range20及SubLoop2–8得到38帧，不以视觉拖尾驱动伤害。Hit51=BA9A0沿A25C0原方向表创建cHitPar1数量的CltHitSubMissile1；cHitPar2非零才装原charged-path初始化，保留合法但当前表未启用分支，当前10／18数量仍从MPQ读取。普通投掷／原73继续走已有创建配对，背压不由表现重新造成命中。新增路径仅零售，新增源码已构建；原视觉随机初值、初始路径／完整同步交错及图形表现仍待实测。

原表393／394的Distraction仍有独立客户端分支，不能按名称当作诱饵已完成。新增Clt45=BB500复用原B9470（与Clt4同一助手）的剩余帧0强制生成／机会／数量／半径与父种子路径，子项显式继承Skill／Level；其父Clt44的主人UsedSkill复验和自然生产者尚未补齐，不从诱饵或状态自行生成393。新增源码仅限零售，已构建，行为仍待实测。
