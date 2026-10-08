# 亚马逊技能与公共层

更新：2026-10-08。当前30项均有自研服务端执行入口：24项主动、6项被动；原服与自研宿主使用同一套原协议客户端。每项按技能ID顺序完成后构建、修复编译错误并打包。本轮已完成逐项构建打包及有限自研冒烟、代表性D2GS回归；列出的运行路径为V2，其余为V1，未作逐帧认证。旧单机30／30及女巫冒烟不作为本职业证据。

## 迁入流程与证据

规范沿[公共技能](COMMON.md#技能迁入规范)。女巫批已记录D2Common／D2Game／D2Client职责及原版修复边界，本批把复用流程整理成职业通用规范：先读master单机实现，再读取当前MPQ，最后用本地D2MOO和需要的1.13c原DLL核对公共算法及两端独立的执行职责。

master `624c927b22bebc849d35031235545683933a7dc8` 的 `gameplay/combat/physical_projectiles`、`gameplay/skills/missile_rules`／`missile_dispatch`／`native_projectiles`、`gameplay/monsters/companions`、`gameplay/session/session_necro_summons`及现有 `content/skills/amazon_*_data` 是迁用入口。旧全能会话和客户端本地执行器不恢复；装备、资源、技能、弹体、效果、伙伴各自持有领域状态。

当前MPQ Skills／Missiles／States／PetType／MonStats／MonStats2／MonEquip／ItemStatCost、COF和AnimData决定参数与资源。D2MOO固定版本及许可见[资料来源](../../resources/THIRD_PARTY.md)：SkillAma的SrvSt04–10、SrvDo006–016，D2Common的D2Skills／SequenceTbls，以及SUnitDmg、MissMode、Missiles、SkillNec召唤属性和PlayerPets／SCmd用于核对。参考重建存在目标X／Y笔误及Calc字段语义差异，不能替代当前表或原DLL。

## 30项职责核对

表中SrvSt／Do为当前原表分派，空白表示没有该回调；被动由属性计算而非施法入口执行。公共函数处理纯值，服务端负责资格、候选、命中、扣费与实际状态，客户端负责原Clt程序和资源表现。

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

## 公共层与领域边界

- `gameplay/skills/resolve`、damage_curve、passive、amazon_magic和amazon_summon_resolve求等级、协同、耗蓝、期限与派生参数。协同用基础等级，实际施放用有效等级。纯召唤求值不创建装备或单位。
- `amazon_sequence`、`weapon_volley`、`amazon_missile`与projectile_path提供原序列、回滚、扇形／环形／整数圆盘、引导、充能、GUID继任及分裂候选计算。两端调用同一纯算法，各传Clt／Srv参数及各自可知的候选；世界筛选和碰撞不藏在公共函数里。
- `weapon_damage`保存原发射快照与显式随机，处理六伤害通道、转换、Critical／Deadly和SrcDamage。近战先按目标修正伤害范围后掷值；投射按原MissMode先掷基础值后加目标ED。它是公共纯数值代码，当前权威掷值由服务端调用，客户端不为显示重掷伤害。
- `server/skills/weapon`拥有资格、接近、IAS时钟、释放与取消；`inventory/weapon_cost`规划弹药、投掷数量／耐久及法力；`transactions`将成本与公开动作／人物／物品事实成组提交。每次释放复验装备、目标、区域及资源；背压保留首次准备结果，不重复扣费或掷伤。
- `missiles/weapon`持有飞行、墙／单位接触、毒云、地面火、引导及穿透游标；`combat/spell_damage`拥有命中、目标ED、抗性、冷／毒及NextDelay。原接触命中结果传给后续结算，不再次掷命中；`effects/amazon`持有内视、慢速箭和原状态属性。poison纯函数处理强毒替换／同强续期，avoidance纯函数区分移动及近远程。
- `hosting/companion_content`读取MonEquip并沿现有物品生成准备女武神装备；`companions`的类型化准备结果按请求／独立种子绑定。装备revision、等级、基础技能及区域变化使结果失效，自然回蓝不会取消准备；提交时仍复验当前法力。种子在准备请求建立时预留，失败不会重新掷装备。
- `companions/amazon_ai`执行女武神攻击、同行和换区；诱饵保持原固定位置。女武神装备及孔内项使用实例统一新ID；死亡／替换／期限／主人退出清理由伙伴领域执行。生命、位置、动作和装备只在monsters持有，宠物不奖励敌怪经验／掉落。强化属性从effects读取，不复制玩家真值。

D2Game不是两端共享库。D2MOO用于证明哪些算法属于D2Common，哪些Srv／Clt程序只是可拆成相同的纯计算；后者提取不等于声称原版调用同一函数。仍不合并世界、伤害权限和渲染，也不以自研宿主需求改变原服客户端。

## 原协议及客户端修复依据

客户端消费原4C／4D动作、73弹体、A7／A8／A9状态、7A宠物归属、AC／67／69／6C／6D单位和9D装备。没有私有消息或按宿主种类分支。0x7A固定13字节；按D2MOO PlayerPets／SCmd建立独立全局宠物名册，离开视野只删除单位，归属在死亡／退役时删除，晚入局重建名册。

本人已完整解码的装备IAS、武器速度及Pierce进入公共时序／表现；其他玩家隐藏属性未知时保留未知。States.gfxtype=2／gfxclass决定原人物伪装，使用原MonMode→PlrMode映射与现有COF合成器；9D的ownerType区分人物和怪物装备，孔内项与清理沿真实所有权，不把宠物装备放进本人库存。

本地1.13c D2Client.dll（基址0x6FAB0000）静态核对：技能CltDo18 RVA15C40、19 RVA15AB0、20 RVA15820、21 RVA14FD0、22 RVA15660；弹体CltDo3／4／7为BBE60／BBE00／B2EB0；CltHit12／14／25为BB070／B0870／BAEF0。用于核对追踪、毒烟、箭序列、充能落点、碎片、地面火及Fury分裂。CltHit12的期限使用父弹体Range／LevRange，Srv使用SHitCalc1；CltDo22使用Calc1范围和完整弹跳数，不能套Sorceress ChainLightning计数。以上是静态函数证据；代表技能的有限运行见下节，不能据此认证完整Clt函数或逐帧表现。

## 有限运行证据

2026-10-08按后续授权补做简单冒烟和本地原服代表技能回归。当前包身份及运行范围见[基线](../../../BASELINE.md#亚马逊当前包有限冒烟与原服回归)，不把全部30项升级为原服认证。管理管道只准备真实 MPQ 内容、资源及固定步；学习、选择、释放和装备仍经同一客户端原 C2S 包。

| 环境 | 条件、观察及证据 |
| --- | --- |
| 自研 | Hero，普通难度41级，种子3739460588；全部30项基础等级1。magic-* 不耗箭但首发未命中；multiple-* 两箭及一箭成本；guided-* 释放／轨迹而非导引命中；immolation-* 原 Brute 死亡及230火；jab-* 分次命中；fury-* 原231子弹、两只 Brute 死亡、标枪60→59；final-reload* 跨进程恢复全部基础技能和扣费后的箭／标枪，召唤不恢复 |
| 最终伙伴复查 | wire-valkyrie* 544生命、spr／ful及7A／9D，管理死亡模式 effectLimitations 为空，但 stderr 报缺 amtrhvydthth.dcc，不能据此认证死亡画面；wire-decoy* 固定位置71生命，不复制主人库存，到期移除；wire-final failures=0；最后去除空库存后的 delivery-decoy*／delivery-final 复查原包、出现效果及到期，failures=0、stderr为空。早期带复制装备的 decoy-final 不是最终修复证据 |
| 原服 | 新角色 AmaSmokeOct，原服生成 v96 D2S；用独立 @dschu012/d2s 2.0.36 仅准备这个临时角色，未上传自研 D2S。原服购买短弓及350箭；Multiple Shot 两箭／扣一箭／扣蓝，Guided Arrow 释放轨迹／扣费，Dopplezon 人物伪装／7A且不额外生成9D装备，Valkyrie 7A／9D原 spr／ful装备及最终重入复查，Lightning Fury 标枪60→59、扣蓝及原 Brute 死亡。见 multiple-*、guided-*、decoy*、valkyrie*／wire-valkyrie*、fury-* |

本次原包差异修复：

- 原弓／bowq、弩／xboq为合法双手组合；RemoteInventory不先移除匹配箭袋，直接发原0x1A。依据 D2MOO ItemMode::sub_6FC43280及原服真实装备回应。
- 女武神 MonEquip 物品准备使用现有最大耐久求值；内容异常向宿主诊断明确记录，领域不读取 MPQ。人物 DT／DEAD 武器类按 D2Common COMPOSIT_GetWeaponClassCode 使用 HTH，保留其他外观组件；当前 MPQ 提供 AMDTHTH。
- SrvDo015 诱饵不建立 MonEquip／克隆主人库存；人物伪装来自原状态和 storedOwner，不能借多发9D复制装备。女武神仍独立生成 MonEquip；诱饵没有空库存替身，出现效果投影独立于装备。
- AC 按 SCmd::sub_6FC3FC80 保留固定生命字段和总长，分别编码31位实际主人 actor、诱饵 UMod21及期限规则；7A宠物名册独立于AC可见单位和9D怪物物品。修复后重新入局、伙伴和到期路径已复查。

原服普通玩家施法不回送本人4C／4D，所选技能也未通过73重复生成动作派生程序；自研沿既有收敛规则。原服旧未消费包仍有诊断，不能声称全协议无缺口。21:27:26原服 watchdog 记录疑似 deadlock 后结束房间；原因未知，重启本次启动的 D2GS 后完成女武神复查。有限记录不认证全部参数、导引命中、原服 Fury 分裂画面、逐帧表现或服务端长期稳定性。

## 保存、构建与剩余边界

D2S v96不变。基础技能、选择、热键、扣费后的法力、数量和耐久沿已有字段保存；派生被动、动作／弹体、毒／冷／状态、召唤单位／装备、AI、准备种子与回滚游标不写档。规则指纹为 `d2x-character-admission-v18/native-wire113c/d2s96/amazon30`，详情见[存档](../../modules/SAVES.md)。

30项逐项Windows Release构建及打包记录位于忽略目录 `artifacts/amazon-implementation-20261008`，最终包为 `dist/current`。构建只证明源码可编译；本次补做有限运行，证据在忽略目录 `artifacts/amazon-smoke-20261008`、`artifacts/amazon-d2gs-20261008`，没有Linux构建证据。

仍有通用系统边界：

- 玩家武器的吸血／吸魔、压碎／撕裂仍显式拒绝，不忽略后执行。女武神已生成原装备并执行基础伤害、属性与支持被动，但这些装备附加战斗触发尚未结算；不代表完整物品效果已实现。
- 原宠物归属及世界人物伪装已接，宠物头像HUD尚未接；不自造布局或私有宠物生命协议。完整雇佣兵链另属后续工作。
- 普通攻击仍是既有单武器物理近战范围；亚马逊技能入口不自动扩展普通弓弩／投掷、双持、PvP或通用充能／触发技能。
- 晚入视野恢复单位／状态／装备／归属，不重播历史弹体；女武神重甲死亡组件选择尚待原版核实（当前MPQ无 amtrhvydthth.dcc，不猜测替换）；完整怪物远程攻击宠物、像素／音频、路径定点量化、三难度／全装备／取消／背压及全30项原服技能回归尚未认证；仅列出的保存往返和原服路径有有限证据。

这些缺口不由客户端本地结算兜底，也不把所有30项或整套战斗标为原版V3认证。
