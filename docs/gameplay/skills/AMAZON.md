# 亚马逊技能

PD2专项内容：[职业技能](../../pd2/CLASS_SKILLS.md)。本页保留原版／公共规则及必要的版本边界。

更新：2026-10-09。自研服务端执行24项主动、6项被动，原服与自研使用同一原协议客户端。本页维护逐技能职责、亚马逊专属Clt／Srv差异、原版依据和缺口；公共规范见[COMMON](COMMON.md)。

内视／慢速箭从当前MPQ AuraRangeCalc／AuraLenCalc／AuraStatCalc1读取原公式，公共期限算子保留Curse状态的抗性取整。首项属性在原始列表投递前分配，同级重施保留原首项；状态事实读取实际复用列表。玩家费用、目标属性／资源、旧诅咒撤出及控制回调随同一候选提交，不再只改变单位状态显示。依据D2MOO SkillAma::SrvDo006及AuraCallback_InnerSight_SlowMissiles。内视／慢速箭使用与光环共用的nativeAuraCandidateRecipients：半径保留signed32、目标按当前MPQ相邻房间及房间单位链顺序采样，保留NoAura、Boss过滤、城镇／伤害资格／敌意及整数距离。玩家PvP接收者和非玩家共用首项来源／同级复用规则，扣费与多个玩家／单位候选一起复验；跨区域状态事实按接收者区域发送。零或负半径不遍历目标。全部等级／多人／跨房组合、原资格掩码完整闭包及全局事件时序仍待对齐。

## 30项职责核对

SrvSt／Do为当前MPQ分派，—表示没有该回调，不能据此判断未实现；被动由属性／战斗计算消费。公共计算不拥有世界候选、命中、扣费或实体；表中Clt程序只创建视觉效果。等级／耗蓝／武器伤害等通用语义见[跨职业计算](COMMON.md#跨职业公共计算)。

| ID／技能 | SrvSt／Do | 公共计算与权威执行 |
| --- | --- | --- |
| 6 魔法箭 Magic Arrow | —／— | Missile SrvDmg1 按 DmgCalc1 转换物理伤害，附加物理伤害来自技能表；不扣箭，仍复验弓弩／等级 |
| 7 火焰箭 Fire Arrow | 4／— | 原伤害曲线、火转换、SrcDamage；weaponCost提交弹药与法力 |
| 8 内视 Inner Sight | —／6 | 1.13c D2Game RVA 4C2A0／4C220 从技能表取范围、期限、首项属性及筛选掩码，安装独立 innersight 状态。减防允许防御小于零，保留原 UNITS_GetDefense 的有符号计算。d2decomp 的零值下限是重建简化。技能说明读 ln56／-edmn／ln34，单位覆盖图来自 States.overlay1 |
| 9 双倍打击 Critical Strike | 被动 | MPQ 的 dm12 写入 passive_critical_strike，技能说明使用同一递减曲线。近战按专精、双倍打击、致命攻击依次检查。1.13c D2Common RVA 6B180 的弹体出生顺序为双倍打击、致命攻击、专精，首掷即使概率为零也消费无符号随机值。任一成功即停止检查并使物理通道翻倍 |
| 10 戳刺 Jab | 5／7 | 原 seq1 复用同一命中及音效标记，三次近战事件各自复验目标并结算 Calc1。1.13c D2Client CltDo16 RVA 14F40 在事件 3 上按玩家／怪物读取 dosound a／b，怪物序列事件来自 MonSeq |
| 11 冰箭 Cold Arrow | 4／— | 原冷伤／转换／长度；命中及冷时长／难度减免归combat |
| 12 多重箭 Multiple Shot | 4／8 | 整数扇形、原数量和 SrcDamage；SrvDo8／CltDo17 将 Calc2 交给弹体构造器作为激活延迟；一次扣费、各箭独立命中及穿透 |
| 13 闪避 Dodge | 被动 | dm12 写入 passive_dodge。1.13c D2Game RVA DB104–DB142 在静止分支检查近战闪避，RVA DCFAD 在处理闪避反应前检查 UNINTERRUPTABLE。成功闪避仍挡下伤害，受保护的当前动作不因闪避切入 S1 |
| 14 威力一击 Power Strike | 6／2 | 1.13c D2Game RVA 4C720 起手以零技能命中修正分配近战，Do2 在动作帧兑现。面板 DescDam5 使用完整武器伤害再加技能雷伤，DescAtt1 仍显示表里的 ToHit 加成 |
| 15 毒枪 Poison Javelin | 4／— | 服务端 Do2 在换格后创建尾迹毒云，客户端 Do3 各帧创建表现，持续毒由 combat 结算。1.13c D2Client DescDam22 RVA 34640／2E190 分开显示投掷武器伤害与按原持续帧计算的绿色技能总毒伤 |
| 16 爆裂箭 Exploding Arrow | 4／— | 箭本体与原火范围分开；命中回调与 AlwaysExplode 按原 MissMode 条件执行；CltHit1 在圆盘内布置火爆效果并采样子弹体 RandStart |
| 17 慢速箭 Slow Missiles | —／6 | 1.13c D2Game 弹体构造器 RVA 6FA5C–6FAA3 仅在 CanSlow 下采样主人的 slowmissiles 列表，以 skill_handofathena 作为剩余速度百分比。客户端根弹及普通子弹体按各自出生时的已知主人状态取值 |
| 18 躲避 Avoid | 被动 | dm12 写入 passive_avoid，1.13c D2Game RVA DB145 在静止弹体分支检查。原版把无 GetHit／击退的回避降为轻受击，其余在 RVA DD25B 读取 avoid 状态选择 S1，受 UNINTERRUPTABLE 保护时保留原动作 |
| 19 刺爆 Impale | 7／2 | 原 seq8 单次命中。1.13c D2Game RVA 4CC50／4C9E0 在起手命中后按 Calc2／3 检查专项磨损。堆叠武器先检查剩余数量，扣量后补满保留堆的耐久，普通 Drain 磨损另行结算 |
| 20 闪电球 Lightning Bolt | 4／— | 1.13c SrvDmg12 转换物理并重掷雷伤，弹体无 ToHit 检查。D2Client DescDam21 RVA 308C0 只把投掷物理与装备雷伤按 SrcDam 折算后加技能雷伤。CltHit10 RVA B2570 在单位接触处添加 ProgOverlay |
| 21 急冻箭 Ice Arrow | 4／— | 原冷伤与冻结时长；普通冷却／冻结资格分开，保留目标难度系数 |
| 22 导引箭 Guided Arrow | 4／10 | 单位目标直接进入追踪，点地发射到期限后按原生 GUID 找目标。SrvHit10／CltHit13 重设总帧和当前帧；服务端 Do7 按当前帧、客户端 Do7 按已过帧检查转向 |
| 23 刺入 Penetrate | 被动 | 1.13c D2Common RVA 5257F–52637 按有效等级将 ln12 写入独立被动列表的 item_tohit_percent。人物面板在缺少已解码被动列表时由有效等级补入属性，已观察列表只算一次。女武神 ToHitCalc 另取主人的基础等级 |
| 24 充能一击 Charged Strike | 6／11 | St6 起手分配，Do11 先 Drain 再按 Calc1 创建子弹，Srv／CltDo19 都从受击点朝 2×受击点−攻击者点发射。D2Client DescDam17 RVA 340F0 将武器伤害和单发雷伤分行显示，无命中率行 |
| 25 瘟疫标枪 Plague Javelin | 4／— | 1.13c SrvHit2 RVA 3E330 以接触单位或弹体为爆散原点，CltHit2 RVA BB1B0／B6BC0 始终使用飞弹所在子格。两组方向、速度和循环读取各自的 HitPar；DescDam22 分开显示投掷伤害与技能总毒伤 |
| 26 炮轰 Strafe | 8／12 | 服务端每次发射后按当前帧、动画锚点及 Param6 回卷重排；客户端 CltDo20 的可见目标预测同样允许 GUID 回绕到唯一目标 |
| 27 牺牲之箭 Immolation Arrow | 4／— | 原箭伤、独立范围伤／地面火及missileDiskOffsets；Srv期限与CltHit12期限／概率分别取原参数 |
| 28 诱饵 Dopplezon | —／15 | 1.13c D2Game RVA 4D8C0 按主人 8.8 最大生命与 Calc3 计算生命，沿原人物伪装及主人关系生成固定诱饵，独立于主人库存。原 MONUMOD 事件在创建帧＋Calc2 到期，调度保留同一截止帧 |
| 29 回避 Evade | 被动 | dm12 写入 passive_evade。1.13c D2Game RVA DB030／DD26C 在行走和跑步时单独检查，成功后发送附着声音事件 12，保留当前动作。D2Client RVA 23FBA 从 Evade 技能的 StSound 播放该事件 |
| 30 击退 Fend | 9／13 | 服务端按近战候选数与 Calc1 上限计数，当前目标失效／出范围时找 GUID 后继。1.13c CltDo21 RVA 14FD0 检查目标、递减剩余次数并按 Param2 回卷。客户端用已知近战候选预测次数，区分同用 Do13 的热诚起手规则 |
| 31 冻结之箭 Freezing Arrow | 4／— | 箭本体与冷范围分开；服务端范围冻结，CltHit14 使用父弹体的视觉随机流从原 3 组／8 方向生成碎片 |
| 32 女武神 Valkyrie | —／16 | 1.13c D2Game RVA 4D710 生成原 MonEquip 与 SumSkill 被动，置 Valkyrie 状态并在 20 帧后进入 AI。当前 MonStats 指定 NecroPet，沿公共 Fn067 的近战分支执行 |
| 33 穿透 Pierce | 被动 | dm12 写入 skill_pierce。1.13c D2Game RVA CE557 的技能输入推进玩家 pierce_idx，RVA 6F6AC 出生时以该独立种子掷装备与技能穿透之和，最多 4 次。本人客户端按 D2Client RVA 1A13D／B0AFE 随输入推进预测种子，收到 73 后采用同步次数。远端隐藏数值仍取已知状态或同步包 |
| 34 闪电攻击 Lightning Strike | 10／14 | St10 分配近战命中，1.13c Do14 RVA 4CE10 结算后按原生 GUID 选后继，目标坐标依次读取 X／Y。CltDo22 读取完整 Calc2，CltHit16 用弹体所在格和原生 GUID 选目标，选中刚击中的单位即停止。DescDam17 分两行显示武器和闪电伤害，无攻击准确率栏 |
| 35 闪电之怒 Lightning Fury | 4／— | 1.13c SrvHit20 RVA 3DD60／CltHit25 RVA BAEF0 在单位、墙及期限回调分裂，从标枪所在格按 AuraRangeCalc 和 Calc1 选目标，允许刚被击中的单位入选。服务端从主人相邻房间按原单位链顺序遍历。客户端按当前 AuraFilter、NoAura 与已知单位预测分裂，顺序受可见单位限制。子雷弹无武器伤害，DescDam22 分两行显示投掷与闪电伤害 |

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

女武神与近战死灵伙伴复用 Fn067 的主人目标、本体目标、三射线资格及 PetMove。已有 WL／RN 路线时保留路径，由动作结束回到 AI。诱饵不进入移动 AI。多人目标优先级和逐帧时序仍待实机核对。

NativeSummonStats保存显式主人公式输出：Calc1／Calc2／诱饵Calc3、五项Passive加值、六项Aura非零写入、五个SumSkill／SumSkCalc槽及普通／L列MonLvl加值。诱饵主人生命不先截成整点；初始及百分比生命使用原整数算子并保留Fixed8。当前两个MonStats记录按已核实noRatio=1准备，初始生命先掷整点、限23位再左移8位；其他ratio模式不由此推断支持，完整诱饵／女武神流程仍待对齐。

attributes/prepareSummonStatSources从基础列表依序安装初始技能、召唤等级／MonLvl加值、Passive基础AddStat、Aura独立列表、生命Calc增长和SumSkill赋值，再投递装备来源。继承技能按参数层与原同等级缓存调用共享被动刷新；装备加技能后重新求有效等级。最大生命变化沿MonsterResourceTrace回调，原始生命／上限与来源一起落入单位候选。MonEquip贡献从同一native_sources入口保留原始属性、孔内项和参数层，临时物品ID及其嵌套来源统一绑定实际ID；基础MonStats抗性与技能／装备抗性不合并为另一份基础值。怪物公式、状态和选定武器查询已能读取这份来源，模式表共用当前MPQ的不可变目录。

当前源码中，女武神在HP前按原组件计数抽签；MonEquip只跳过开头等级过高的记录，后续连续记录均消费选槽随机，重复槽位仍抽单选。选槽消费宠物单位流，实际装备构造使用游戏流双种子及公共物品品质程序，保留NOSELL、禁止自然无形／加孔和修复耐久；强制装备仅在局部统计时暴露完整属性，不改真实鉴定与需求值。宿主返回分配次数和游戏流终值，服务端复验并随召唤事务提交，不在内容等待前提前提交单位种子。首个AI事件直接按诱饵25帧／女武神20帧进入出生候选，避免额外下一帧空回调。

普通出生寻位现复用怪物出生搜索的−1／4回退、原房间边界、辅助碰撞和原生动态占位；搜索贷款随召唤事务提交，寻位失败只保留实际房间随机消费。宿主仍保留部分装备动画／过程参数准备；跨区域出生、创建失败随机副作用、异步准备期间的完整主人属性采样、所有物品／Aura事件订阅、动作／移动／恢复时序及生命周期仍待后续迁移。这里是源码来源接入，完整SrvDo015／016逐帧流程仍待对齐。

召唤装备按构造顺序记录库存链与原始属性来源；服务端发布前重绑真实物品／孔内项、装备容器主人、位置及注册武器，预先核对数量与重复身份。宿主结果安装重放组件、初始生命及女武神选槽投掷，游戏随机终值由人物事务复验并发布。此处只补足身份及随机交接，不代表全部逐件装备回调完成。

## 原版依据与客户端差异

迁入起点为master `624c927b22bebc849d35031235545683933a7dc8` 的 `physical_projectiles`、`missile_rules`／`missile_dispatch`／`native_projectiles`、`monsters/companions`、`session_necro_summons`及现有 `content/skills/amazon_*_data`。当前MPQ的Skills／Missiles／States／PetType／MonStats／MonStats2／MonEquip／ItemStatCost、COF及AnimData决定参数；本地D2MOO SkillAma、D2Skills／SequenceTbls、SUnitDmg／MissMode／Missiles、SkillNec、PlayerPets／SCmd定位执行职责。参考重建的X／Y笔误及Calc语义差异不能覆盖原表；版本见[资料来源](../../resources/THIRD_PARTY.md)，模块证据见[参考设计](../../architecture/REFERENCE_DESIGN.md#亚马逊公共层的原版核对)。

| 专项 | 原版依据／必须保留的差异 |
| --- | --- |
| Clt程序 | 1.13c D2Client 的 CltDo17–22 RVA 为 15DA0／15C40／15AB0／15820／14FD0／15660。MissileCltDo3／4／7 为 BBE60／BBE00／B2EB0，CltHit1／12／13／14／25 为 BC1A0／BB070／B1350／B0870／BAEF0 |
| 弓弩人物面板 | 1.13c D2Client DescDam5／6／7 RVA 35270／35540／34C00。普通箭的面板先用完整武器投影及 SrcDam，再合入技能曲线；爆裂、牺牲和冻结分别显示武器与对应元素的范围伤害，范围值包含装备同元素伤害；导引和炮轰从 SkillDesc 的 ddam calc1 求武器伤害加成，导引无命中率行 |
| 参考版本 | 本地 d2decomped 重建目标为 1.14d，炮轰的固定时间间隔等简化流程不用于判定 1.13c 的回卷和弹体行为。1.13c D2Game SrvDo8 RVA 4D420、SrvHit10 RVA 3B140／397C0 及当前 MPQ 决定对应参数 |
| 牺牲箭／闪电攻击 | CltHit12按父弹体Range／LevRange求期限，Srv使用SHitCalc1；CltDo22使用Calc1范围及完整弹跳数，不套女巫Chain Lightning的计数规则 |
| 本人显示 | 已完整解码的IAS、武器速度及Pierce参与时序／表现；远端未知隐藏值仍未知 |
| 宠物投影 | 原7A固定13字节名册独立于AC可见单位，离开视野只移除单位；死亡／退役删除归属，晚入局重建名册。9D的monster ownerType装备不进入本人库存，孔内项沿真实归属清理 |
| 诱饵／女武神 | SrvDo015不生成MonEquip或复制主人库存；状态gfxtype／gfxclass与原主人信息用于人物伪装，出现效果不依赖装备。女武神独立MonEquip。AC按SCmd保留生命字段、总长、31位storedOwner及诱饵UMod21；D2Common的DT／DEAD武器类为HTH，但重甲死亡组件仍待核实 |
| 弓弩／箭袋 | ItemMode::sub_6FC43280允许bow／bowq、xbow／xboq双手组合；RemoteInventory直接发0x1A，不先移除匹配箭袋，已获原服真实回应 |

原服普通本人动作及动作派生弹体的发包原则见[COMMON](COMMON.md#定义来源与客户端状态)，不为自研另写客户端协议或画面分支。

A7／A8／A9承载伪装与单位状态属性；7F向主人同步宠物生命／区域，独立于可见单位。头像投影与未专项复验边界见[召唤物与佣兵头像](../characters/HIRELINGS.md#召唤物与佣兵头像)。运行态伙伴、独立装备及内容准备不写角色D2S；全部PvP伙伴组合与完整受击动作仍待对齐。

## 边界

- 女武神装备近战经`combat::monsterMelee`采样真实武器与当前属性，沿公共`prepareContactProcedures`及吸取执行阶段处理压碎／撕裂与吸取；不能再将这些效果概括为未结算。完整装备、目标类型、随机／触发及重试组合仍待验收，不代表完整物品效果已认证。玩家武器拒绝条件见GENERAL。
- 原宠物名册、伪装及装备已接；头像按原PetType显示女武神，诱饵的icontype=0不显示。佣兵服务及头像的实现、原版依据与缺口见[佣兵](../characters/HIRELINGS.md)。
- 女武神重甲死亡时请求的 `amtrhvydthth.dcc` 在当前MPQ缺失；原版组件选择待核实，不猜测替图。`effectLimitations`为空不足以证明这条路径正常。
- 弓弩客户端以可见单位和已知房间预测弹道。逐帧路径、随机初值、多人目标过滤、装备触发及原服画面仍待实机验证。原版牺牲之箭爆炸清除冷时长和 STAT_ITEM_FREEZE，自研弹体当前仅建模前者。原服 Fury 子弹、被动随机分支和全职业技能组合的边界仍归各自程序。

## 原服参考

2026-10-08使用D2X客户端连接原服，角色AmaSmokeOct由原服生成v96 D2S，独立`@dschu012/d2s` 2.0.36仅用于准备该临时角色，未上传自研D2S。原服购买短弓及350箭后，Multiple Shot生成两箭并扣一箭／法力；Guided Arrow有释放轨迹及扣费，未取得导引命中的完整对照；Dopplezon提供人物伪装／7A，不额外生成9D装备；Valkyrie提供7A／9D及原spr／ful装备，重新入局后亦有复查；Lightning Fury使标枪60→59、扣蓝，目标Brute死亡。上述服务端消息与数量可作为所列原服行为参考，客户端轨迹和伪装画面仍由D2X绘制。

普通施法未见本人4C／4D或相应重复73；既有未消费包仍有诊断。21:27:26原服watchdog报告疑似deadlock并结束房间，原因未知；重启本次启动的D2GS后完成女武神复查，不能计为原因已修复或长期稳定性证明。

角色档保存基础技能及物品数量，召唤不跨局恢复；格式及校验边界见[SAVES](../../modules/SAVES.md)。完整参数、逐帧行为及Linux适用边界仍待核实，不按自研运行记录划分完成等级。

原版闪电标枪的客户端补入CltDo46／Hit51：B7880在elapsed0／1从DynamicPath.tTargetCoord与当前整数格计算垂直方向，读取上次换格位后向正反两侧各创建CltSubMissile1；CltParam1仅在子项SubLoop启用时增加(SubStop-SubStart)×循环数的寿命，保持Skill／Level与原图。当前431／438的参数3、子432／439的Range20及SubLoop2–8得到38帧，不以视觉拖尾驱动伤害。Hit51=BA9A0沿A25C0原方向表创建cHitPar1数量的CltHitSubMissile1；cHitPar2非零才装原charged-path初始化，保留合法但当前表未启用分支，当前10／18数量仍从MPQ读取。普通投掷／原73继续走已有创建配对，背压不由表现重新造成命中。新增路径仅零售，新增源码已构建；原视觉随机初值、初始路径／完整同步交错及图形表现仍待实测。

原表393／394的Distraction仍有独立客户端分支，不能按名称当作诱饵已完成。新增Clt45=BB500复用原B9470（与Clt4同一助手）的剩余帧0强制生成／机会／数量／半径与父种子路径，子项显式继承Skill／Level；其父Clt44的主人UsedSkill复验和自然生产者尚未补齐，不从诱饵或状态自行生成393。新增源码仅限零售，已构建，行为仍待实测。
