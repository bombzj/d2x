# 持续效果、状态列表与周期事件

本页维护跨技能持续效果的列表、接收资格、伤害状态、期限与周期回调，保留当前消费者、原版证据及未闭合边界。技能所有权与迁入规范见[公共规范](COMMON.md)，实例调度与发布接口见[内核子系统](../../modules/SERVER_SYSTEMS.md)，专用技能仍归各职业专题。

## 当前持续效果入口

`CombatEffectSet`持有状态位、列表、原有符号移除期限和队列1技能事件登记表。当前MPQ的Events／Skills提供事件号、三个AuraEvent／AuraEventFunc槽及覆盖图身份；列表逐写后撤销旧登记并按槽顺序插入链头。触发保存本次节点快照，尚未开始的节点复验是否仍登记，已开始的节点跨背压完成；后续单位的事件在进入该阶段时重新取链。召唤贡献完成后登记石魔事件，没有为无AuraState的宠物编造状态列表。登记表与属性列表投递／撤出分开，骨甲、能量盾和飓风装甲按AbsorbDamage登记链处理；每个回调复验登记，按技能AuraState查最新投递的实际首列表，耗尽走公共撤出。

combat的玩家／怪物／佣兵／宠物命中保存原始伤害、计算结果及阶段游标，先准备防御／护盾，再执行单位事件，最后提交生命扣除。铁处女、偷取生命、荆棘、三种冰甲、血魔回复／主人分担和粘土石魔减速已经迁入这条路径，旧事后反应队列及重复近战扣血入口已撤除。反伤结果32的路径不再递归产生普通伤害事件；回复与覆盖图分别保留完成阶段。普通偷取使用事件后的当前生命上限约束。飞弹returnFire从当前MPQ传递，冰甲的HitByMissile在准确率／格挡／回避及基础扣血前执行，落空仍可反击；范围爆炸不继承父箭的接触事件。

固定原1.13c D2Game.dll（MD5 `978f6f74cd031bce6ecee602cd5988cc`）技能事件表RVA1077A8确认回调1／2／3／4／5／22／23／24／26／27。骨甲RVA4DFE0的耗尽分支扣掉原吸收量，与D2MOO Event22的错误转录不同。近战填充和最终执行各有AttackedInMelee调用：原地址6FCFD97B和6FCFEE62调用RVA37030的事件派发；两次原调用分别保留游标，不把它们当作重试重复。未命中不填充伤害，仍保留最终近战尝试事件。原ExecuteEvents RVADE0C0先派发攻击／受击事件，随后限制物理伤害到当前生命。

Misc耐力／解毒／解冻药剂保留原stat/calc顺序，以同一状态列表延长原期限并逐项写入。耐力恢复的MAXSTAMINA+256由具名事务策略保留。井水先清毒／冻结，再按原状态ID遍历curable；NPC对人物与宠物保留各自解除顺序。宠物按类型升序、每类真实登记顺序遍历，死亡标记与GUID分开，Revive重新登记；换区保留已有单位效果和登记。

诅咒AI的DimVision／Terror／Taunt决策抽到gameplay/monsters/curse_decision。恐惧保留初始目标、参数2首次逃跑与后续失败游走，不把默认30写成随目标距离增长的阈值。嘲讽的-666仅表示保留参数，成功切换AI与分配被拒绝后的目标／下一帧思考分别处理；解除与同级重施保留实际函数选择序号。已实现的沙虫／蝙蝠家庭先经过替代回调再进入特殊AI。怪物Cursed保留逐次公式快照与目标游标。

普通攻击的直接伤害伪弹体没有MPQ定义，现仅对实际飞弹读取ReturnFire。人物生命进入保存，临时状态不写入D2S。刺客30项及传递依赖已有实现，见[刺客](ASSASSIN.md)；德鲁伊30项与动物／藤蔓／灵体已有实现，边界见[德鲁伊](DRUID.md)。其余帧事件、队列2物品事件与技能事件的完整交错、特殊目标组8及其余未实施家庭AI仍待迁入；完整动作／随机流及所有原版程序也未完成对齐。

`effects/damage_absorption`共用原Event22／24／25。骨甲保留1.13c耗尽时先扣原池值的修正；飓风装甲按火→冰→电消耗同一AuraStat池，保留原版RVA64600的有符号显示更新判断和百分比参数顺序。能量盾按实际接收者资源逐通道扣法力，公式Rand借用其私有种子并随候选提交，方向覆盖图共用原D2Common 11053扇区。玩家、非玩家普通命中及环境伤害分别保留吸收阶段，容量重试不再次消耗池；逐项属性写回不重新登记事件。GetStatListFromUnitAndState共用`CombatEffectSet::findState`的反向投递序，原生复用和状态投影采用同一首列表；旧浮点／批量骨甲吸收入口已撤出，纯伤害缓解不再依赖效果列表。全部吸收组合与原版的行为仍待逐项核对。

SrvDo18补入当前MPQ的飓风装甲、Quickness、Fade和Venom，六AuraStat、五PassiveStat、元数据与期限由公共公式准备，资源／覆盖图／音效仍读取MPQ。同组撤出按原State ID顺序先清位再撤首列表，包含只有状态位的条目。`effects::unitSkill`以实际非玩家属性、资源和投递门槛替代旧佣兵专用入口。Venom的`skill_poison_override_length`进入公共近战毒长度函数；覆盖长度不除装备毒源数，并保留技能长度。`rollMonsterUnitHit`读取实际单位、当前Mode和公共效果的完整属性，火／电／冰／魔法／毒依次投掷，毒率不左移8位；不再仅给死灵召唤物叠加buff元素或重复投掷光环伤害。

召唤物上限替换、主人死亡和主动遣散通过`stageRetirements`／`stageDamageStates`合并旧单位的死亡列表撤出、资源回调、状态事实与人物／新单位提交；寿命到期和环境陷阱结束共用`scriptDeath`，容量拒绝保留当前阶段。零MAXHP的原生单位允许死亡资源候选，存活模式与HP继续分离。真正移除实体时同步清除效果、光环／附魔／Web／物品光环来源缓存及帧事件；不把离区仍存在的实体当作销毁。完整队列1／2登记及回调交错、HPREGEN击杀回调、其余持续技能程序和全局帧顺序仍未完成，不能据这些源码变更宣布全部buff或战斗已收尾。

## 帧事件底座与持续效果期限

`gameplay/events/frame_queue`持有原类别顺序、64个到期桶及持续队列。原1.13c RVA8F070确认飞弹→玩家→怪物→物件→物品，每类持续事件先于到期事件；RVA8EA80确认signed期限归一化与到期尾插，RVA8E810的持续登记忽略显式回调并头插；RVA8E6F0确认执行中的删除先标记。回调保存值快照和下一个节点，取消下一个节点时推进游标；背压保留执行节点，不进入后续类别或逻辑帧。节点与拥有者索引在候选准备时分配，领域接受后无分配链接，候选放弃不登记真实事件。

`GameSystems.frameEvents`拥有实例队列；`effects/frame_events`将玩家／单位状态期限与其状态候选一起发布；`monsters/frame_events`按真实AIRESET身份分别清除迷乱／吸引选择器，保留DimVision／Terror／Taunt函数。普通效果的兼容期限仍留在原有维护入口。当前源码中，item_auras按GUID处理PeriodicStats，right_aura处理右手custom ID -1，storm处理雷云正skill ID，hirelings处理单位右手／列表光环周期，unit_aura_selection准备原SetSkills／AssignSkill与跨单位候选；周期结束才采样当前PerDelay并重排，旧玩家／佣兵光环与风暴轮询已移除。雷云临时弹体的种子依据D2MOO SUNIT_InitSeed与MissMode：game seed派生私有种子，伤害掷数不直接消费人物种子；原最高技能存在判定与列表存储等级、右手active等级分别保留。自然怪物光环附魔的创建登记及都瑞尔AI首次分配已接原custom ID -1；子体创建开始在对应操作采样公式并以game seed初始化私有种子，玩家／怪物发射与寒冰甲反射也共用此分配来源。敌方伤害来源缓存、min/max投掷时点、完整飞弹程序与来源生命周期尚未收尾。完整周期、AI思考、动作、中断／死亡及同步命中还没有全部迁入，不能将当前类别队列视作全战斗顺序已对齐。活冻结怪物的AIRESET由原RVA769A0确认：跳过回调后一次性事件仍消耗，持续事件等解冻后再执行；这一组合仍待对齐。

`gameplay/missiles/damage_data`开始提供原CalculateDamageData／AddStatsToDamage／CalculateFinalDamage／SetDamageStats的纯值入口，物理／元素／吸取／燃烧范围、原整数缩放、毒覆盖长度及计数、参数层写入与holy标志分别持有；RollDamageValue与FullDamageParams／GetDamageValue保持创建与命中投掷的不同阶段。原1.13c D2Common RVA6B180确认加倍判定使用unsigned余数，暴击始终消费一次投掷，随后仅非零致命攻击、存在武器且非零精通才继续；RVA6BFD4..6C03B确认来源飞弹的恶魔ED仅加一次，未照搬参考中重复相加；SetDamageStats保留随后由BurnLength重写FIRELENGTH，以及原128层damage_vs_montype复制边界。现有范围投掷共用新RollDamageValue，雷云无SrcDam／无holy／无物理分量的当前程序开始经过共同数据／完整投掷入口；普通敌方弹体已保存范围，逐目标命中进入combat/missile_contact的保留阶段；命中、格挡／闪避、怪物暴击分别借用实际攻击者、受击者及攻击者种子，范围使用飞弹自身种子；双爪格挡保留原32参数层边界。内容层从当前MPQ导入MonType三等价关系，伤害匹配复制的128参数层。子体创建借用真实拥有者或原SrcMissDmg父体种子，先准备再一次发布，未再提前掷固定敌方子体伤害。公共combat/drain保留六小数位／难度除数／Drain分支，来源补资源在伤害前回调后、目标HP前完成；玩家恢复投掷与HP候选一起提交受击者种子。NextHit的JUSTHIT开始走nativeMissileHitDelay：原状态、空列表和REMOVESTATE一起准备，零NextDelay仍排期，门控等实际回调解除，兼容旧期限仍独立保留。佣兵弹体程序参数已由无随机的prepareWeaponSkillProcedures抽出，取消创建时固定伤害投掷；所有者精通使用当前usedSkill的共享Range/ITypeA[0]判定。damage_program把SrvDmg放在完整范围投掷之后、SetDamageFlags之前，包含1／12转换与选定元素重新投掷、2／4／10冻结长度、3软命中随机、5／13目标加成、7／9眩晕和14尺寸击退；6／8保留无宝藏及ProgOverlay的领域发布，11读取实际TargetX绝对到期帧；未初始化的原生零值使毒无效，不用飞弹寿命猜测。冷却／冻结长度独立保留，玩家冻结路由冷却也分别消费随机；GetHit、SoftHit与KnockBack资格取当前MPQ，格挡后的动画与伤害成功分开。原1.13c D2Game的表6FD2DB68有31槽，1–14入口及片段已对DLL字节确认；DLL版本摘要见[资料来源](../../resources/THIRD_PARTY.md#原版113c静态与开发对照证据)。玩家武器弹体固定伤害快照已在后续源码撤出，普通弹体与怪物弹体开始共用数值来源与接触前奏／SrvHit／HP阶段，见[飞弹收尾](../combat/MISSILES.md#飞弹数值与接触收尾源码)。统一技能／物品事件顺序、狂犬病参数、原生冷冻／眩晕列表、部分来源组合与特殊SrvHit／燃烧程序仍在迁移，纯函数存在不代表全部消费者已适配；完整数值和组合仍待原版对齐。

## 跨单位接收与载体边界

`CombatEffectSet`、`CombatEffectSpec`与`StatList`是公共机制，单位身份、敌我和主人关系不能决定是否另建一套Buff模型。玩家和非玩家各持唯一主列表，技能、药剂与损伤状态均投递到该列表；`effects::System::unitEffects`借用实际列表，`unitStatSources`／`unitStates`／`unitStateStats`投影实际来源与原生状态。玩家已有总属性包含状态来源，公共查询不再次叠加。非玩家药剂来源按列表实际内容参与，不以佣兵身份作为数值查询开关；复制层从公共查询读取，不维护另一份状态集合。技能列表移除仍由原帧事件完成，不能按查询时刻提前丢弃仍投递的原始列表。

接收资格分开处理：`NativeMonsterStateTarget::acceptsCurseList`表达D2MOO `SKILLS/Skills.cpp::sub_6FD10EC0`的原UNIT_MONSTER检查，即MonStats.npc为假且MonStats2.isAtt为真；名称中的Curse来自原结构，该入口也被Enchant调用，并非仅用于负面诅咒。NoAura、技能filter、敌我／队伍／主人、死亡与技能专用AI条件分别检查；Necro部分程序的mA1条件继续保留。不能以isAtt替代mA1，也不能把“可作为原生状态接收者”等同于“敌对怪物”。这些值由hosting从当前MPQ准备，玩法不读取资源。自然怪物、佣兵、死灵／亚马逊／刺客召唤物、Hydra通过公共状态规则准备入口读取资格。

| 单位 | 当前承载 | 边界 |
| --- | --- | --- |
| 玩家 | 人物基础来源、资源轨迹与公共效果列表 | 由人物候选发布，总属性不重复叠加 |
| 怪物、佣兵、已有召唤物 | 非玩家Actor、原生属性来源、MonsterResourceTrace与公共效果列表 | 角色、阵营、主人及各技能资格分别判断 |
| 城镇服务NPC | 同一个非玩家Actor；AreaNpc只保存GUID、出生点和交互／剧情能力 | 原表空Level／HP为零，存活模式独立于HP；不能伪造生命，也不接敌怪AI |
| 友军剧情NPC | 同一个非玩家Actor及公共Participants／空间索引／属性／资源／复制路径 | act5pow保留npc=0、isAtt=1、mA1=0、noAura=0、Align=1；不创建影子怪物 |

NPC规则由`hosting/npc_content`准备：`loadMonsterCombatProfile(NativeNpc)`共用MonStats／MonLvl缩放，`prepareMonsterLifecycle`共用状态、组件、动作时钟和技能来源。预设NPC的GUID由World分配；动态任务NPC由`monsters::System::admitNpc`复验区域来源后，与Actor及装备共同发布新GUID；`AreaNpc.spawnPosition`只作出生内容，不随移动回写。NPC对话、交互靠近、任务距离和囚犯逃离读取真实Actor位置。Actor拥有HP、maxhp、随机种子、属性根和动作状态，`MonsterResourceTrace`处理状态投递／撤出与最大生命变化；NPC任务／服务能力不会授权攻击或治疗。

囚犯移动消费公共速度修正、恢复／冰冻／眩晕／击退状态；移动提交成功后才推进路径节点，提交被拒绝时保留原路径游标。死亡使用既有状态死亡掩码，离区逃离则撤销Actor、房间链及该GUID的状态／周期事件和效果缓存。区域压缩共用[怪物休眠流程](../monsters/POPULATION.md#公共出生压缩与特殊恢复)，保存必要节点后退役活动实体与效果；它不等同于囚犯逃离后的永久移除，也不承诺保留全部短时状态。NPC与其他非玩家单位共用AC、动作和A7／A8／AA状态及数值快照；hosting的NPC专用投影只保留任务感叹号，按玩家任务可见性过滤，避免重复分配GUID或维护第二套NPC状态。调试快照的NPC位置来自运行实体，出生点单独显示。

## 按原程序区分接收资格

| 程序 | 玩家 | 佣兵／召唤物／怪物／友军剧情NPC | 服务NPC | 独立规则 |
| --- | --- | --- | --- | --- |
| BasicAura等公共状态投递 | 技能filter、阵营／队伍决定 | 原类型为MONSTER时再要求非npc且isAtt；不能按hostile排除友军 | 原npc标记拒绝公共列表 | NoAura只在原搜索程序要求时检查；伤害、直接资源写与状态列表不混为同一资格 |
| Enchant SrvDo025 | 原SUNIT_AreUnitsAligned；不合资格的已知目标退回本人 | 相同阵营判断后走公共列表，不附加mA1 | 原公共列表拒绝 | 复用实际属性根，不以图标代替数值成功 |
| 死灵诅咒／使用其回调的程序 | 非AI诅咒可按filter作用于敌对玩家 | 公共列表之外还保留sub_6FD0B2B0的动作位／possessed条件，AI诅咒另判控制资格 | 拒绝 | mA1不等于isAtt；Attract的先改阵营／选择器再尝试分配顺序独立 |
| 增益战吼 SrvHit18／BasicShout | 本人直接施加；飞弹命中的同阵营玩家 | 原碰撞要求可攻击且同阵营，直接分配战吼列表；不借用死灵mA1或公共npc门槛 | 原碰撞的可攻击标志通常排除 | 依次写入六项属性后刷新被动，期限和撤出共用基座 |
| BattleCry SrvHit21 | 原42883过滤后走公共列表 | 相同过滤＋公共列表；不额外套死灵mA1／possessed回调 | 公共列表拒绝 | `CurseSpec::TargetProcedure`区分直接列表与死灵回调，提交前保留过滤 |
| Cursed附魔的Amplify回调 | 敌对目标＋公共列表 | 敌对目标＋公共列表，不附加死灵专用mA1 | 公共列表拒绝 | 不把怪物施法与附魔回调合并为一种接收策略 |
| 祭坛增益／资源回复 | 交互者；状态来源是祭坛GUID | 不因公共列表能承载就自动广播给附近单位 | 不自动获得 | ObjMode的具体回调决定交互目标、原状态及资源写；井水／治疗者另有宠物遍历 |
| 饮用／喂食药剂 | 原物品使用者 | 喂食命令只授权本人的合资格佣兵；列表数值消费不再限于佣兵身份 | 无自动饮用入口 | 独立校验物品、邻接房间、主人、药剂种类；NoAura／诅咒门槛不参与药剂接收 |
| 井水／治疗者净化 | 交互者按原回调恢复资源、解除状态 | 按原宠物登记槽遍历本人的宠物；不扩展到全部友军 | 作为服务来源，不自动净化自己 | 井水对满血宠物只清状态时不消耗次数；宠物实际补血或交互者恢复／解除列表才标记消耗 |

证据来自固定D2MOO `5596f5cb6c5251a0a07c6637d26458b06099d516`：`D2Common/DataTbls/MonsterTbls.cpp::DATATBLS_CalculateMonsterStatsByLevel`、`D2Game/MONSTER/Monster.cpp::MONSTER_InitializeStatsAndSkills`、`SKILLS/Skills.cpp::sub_6FD10EC0`、`SkillSor.cpp::SKILLS_SrvDo025_Enchant`、`SkillNec.cpp::sub_6FD0B2B0/sub_6FD0B450`、`SkillBar.cpp::SKILLS_ApplyWarcryStats`、`MISSILES/MissMode.cpp::SrvHit18/SrvHit21`、`MONSTER/MonsterUnique.cpp::MONSTERUNIQUE_CurseCallback_ApplyAmplifyDamage`、`OBJECTS/ObjMode.cpp`祭坛程序以及`PLAYER/PlrMsg.cpp`佣兵物品使用入口，字段与资格以当前MPQ为准。这组参考仍为1.10f，不能直接确认所有1.13c分支。

净化复用`CombatEffectSet::findState`查询最新的实际投递列表，再按句柄撤出；没有列表时不清除独立状态位。`NativeStateCure`分别表达显式毒／冻结查询与curable掩码遍历所要求的状态位条件，顺序由hosting准备。治疗者对玩家先毒／冻结后curable，对宠物先curable后毒／冻结；井水对二者均先毒／冻结后curable。`effects::restoreWell`以`WellRestoration.usesCharge`区分恢复事务成功与井水次数消耗，允许只清宠物状态的事务提交；井水模式只在原次数分界改变。对应依据为`ObjMode.cpp::OBJECTS_OperateFunction22_Well/OBJMODE_PetIterate_Heal`与`UNIT/SUnitNpc.cpp::SUNITNPC_PetIterate_Heal/D2GAME_NPC_HealPlayer_6FCCB080`。物件恢复时钟仍使用当前objects领域时钟，完整对象AITHINK队列顺序属于后续全局调度迁移。

原生Direct列表分配与公共诅咒／状态组投递分别选择策略；直接分配不隐式套用Attract保护或跨状态组互斥。`effects/skill_state`按原程序区分六槽公式来源：FingerMageSpider／SpiderGoo及BattleCry读取接收者，战吼读取施法者，感染读取最初感染者；逐槽写入对后一槽可见，attackrate紧接other_animrate。玩家和非玩家共用数值／资源／期限／状态投影及实际公式单位的随机提交；BattleCry保留公共诅咒接收门槛、抗性与等级策略，战吼使用Direct列表。原版1.13c的FingerMageSpider和SpiderGoo首次分配后仍给六槽写入函数传空指针，已有列表才写属性；此分支按DLL保留。时长分别使用AuraLenCalc或SpiderGoo的Calc4，不混成同一技能期限。

原生冷／冻／眩晕、毒／烧伤／开放伤口现由`gameplay/effects/native_status`和`effects/damage_states`投递主列表及REMOVESTATE。冷状态写velocitypercent／attackrate／other_animrate，移除清shatter；冻结移除恢复中立模式，应用／刷新及AI切换通过公共LifeChange更新思考时钟。原怪物生成先设置UNIQUE位，再添加CHAMPION位；`nativeUniqueMonster`统一表示这一原生位组合，冻结资格和眩晕概率包含勇士，眩晕判定消费实际攻击者种子。非玩家毒元数据从列表实际HPREGEN、来源及期限投影，不另算持续扣血。蛛网施放状态与受击减速也进入主列表，旧Slow额外数值载体及轮询到期已撤出。

药剂投递共用`gameplay/consumables/potions::applyPotionState`，按原signed到期帧、旧列表速率和剩余时长合并，逐项原生属性写入；玩家和佣兵均由实际单位种子判定加倍。玩家治疗按血魔石魔原登记槽分配实际可加生命，佣兵恢复与物品消费原子提交。旧Recovery.healing／mana、Actor.healing及potionEffects均已撤出。`effects/player_regeneration`按HP→耐力→法力顺序执行，`monsters/regeneration`统一正／负HPREGEN、life掩码、PreventHeal、城镇和严格溢出取消事件；毒／烧伤刷新、受击与非玩家新药剂列表重排STATREGEN。不可中断动作中的零HP使用DEATH_DELAY和原模式存活语义，等待状态撤出再进入死亡模式，不伪造1HP。

持续效果性能修复源码：玩家STATREGEN的普通路径读取已发布FullStats，只提交生命／耐力／法力变化；满资源且无列表撤出时只重排下一帧事件，不创建人物变更，不复制库存／装备／被动图。药剂列表／原状态位实际撤出时仍走完整资源轨迹和有序回调，保留HP→耐力→法力及整数边界。单纯资源变化输出不可变ResourcesFact，复用原属性增量编码，不引入私有游戏消息；非玩家未变化列表不每帧重贴，查询到同一有序状态列表及相同标准化值时复用已存FullStats，输入／顺序／上下文不同则走原重算。Blaze仅实际跨格时求值，物品恢复仅到期时复制库存候选。

普通无聚气、无外部费用候选的玩家飞弹释放复用`transactions::release`：费用、拥有者随机游标及已准备的飞弹事实由同一输出预留／修订复验后提交，容量或资格失败不提前消费资源、随机或实体ID；带物品聚气、嵌套费用候选及非玩家来源继续各自原有事务。单技能等级查询直接消费同一原生等级公式，避免先构造全部已分配技能的等级映射。

属性图求值中，非扩展叶子直接读取已有BaseStats并继续拒绝叶子拥有子贡献；它没有依赖拥有者的运算，不为每个缺失属性再建立两份零值缓存。扩展列表的生产者、反向操作、上下文、存储值和最小值规则仍由原公共求值器执行，不改变属性数值、来源顺序或随机消费。

依据是本地1.10f D2MOO的`PLAYER/PlrModes::{EVENTS_HpRegen,EVENTS_StaminaRegen,EVENTS_ManaRegen,D2GAME_EVENTS_StatRegen_6FC80F80}`、`D2Common/D2StatList::STATLIST_UnitGetStatValue`及`SKILLS/Skills::D2GAME_SKILLMANA_Consume_6FD10A50`的普通法力分支：原单位读取已发布属性，普通资源加减不重穿装备／刷新全体被动。该性能优化未重新反汇编1.13c对应函数，沿用已迁移数值，不将工程优化宣称为新确认的1.13c规则；这一依据也不涵盖后续费用／Blood Mana入口。队列与管道的可让出调度属于D2X实现，语义核对和当前源码边界见下节。

## 性能优化的复刻约束核对

本节记录源码与固定D2MOO的对照结论；全部性能窗口、野外飞弹费用及全局事件／随机顺序仍待核对，局部源码对照不能说明整场执行与原版等价。

| 路径 | 可保留的优化及约束 |
| --- | --- |
| 属性叶子、已发布FullStats | D2Common `STATLIST_GetTotalStat_6FDB63E0`按扩展标志选择FullStats或BaseStats；叶子直接读取不改变整数值。扩展运算／MinAccr仍用原公共求值器；不能把已发布FullStats随意替换成从头汇总，因为原增量传播／回调可能保留不同的中间结果。状态查询复用要求完整有序来源身份、标准化值、叶子标志及上下文一致，变化时撤出缓存。 |
| 玩家STATREGEN | 普通路径只读当前发布值，保留HP→耐力→法力、8.8与原整数运算，不合并多帧再生。可能撤出药剂列表／状态位时进入资源轨迹。复核修正原慢路径：HEALTHPOT撤出回调先执行，随后写入撤出前缓存的生命结果；MANAPOT撤出后，把按撤出前法力／上限钳制的增量加到回调后的当前法力。法力按先上界、后下界钳制，不依赖`std::clamp`的边界有序前提。 |
| 非玩家列表维护 | 只在存活、已有FullStats且无到期工作时跳过重贴；复核补充FullStats存在性门槛。真正投递／撤出仍同步发布属性、资源、控制与速度。普通到期、原RemoveState事件、死亡撤出不被此快捷分支替代。 |
| 技能等级、Blaze、物品恢复 | 单技能查询使用原等级公式及相同输入；Blaze仅把无共享随机回调的`skills::evaluate`延迟到实际跨格创建分支，创建资格与飞弹构造顺序未动，参照`SkillSor::SKILLS_CreateBlazeMissile`先查移动／列表／城镇再创建。物品恢复维持原物品遍历与到期顺序，只延迟候选库存复制。无匹配效果的erase仅省去复制，当前所有私有谓词均无副作用。 |
| 飞弹费用与随机 | 构造仍在全局／拥有者随机游标的副本上执行，未移动`launch/make`内的抽取位置；身份、容量与费用失败不提交这些游标或实体ID，重试从未消费的游标重算。普通费用快捷事务在复验后一起提交费用、拥有者随机候选和事实，聚气／复合费用／死亡触发保留原路径。复核修正快捷事务的无条件ManaFact，带随机候选的释放改用ResourcesFact，维持旧CharacterFact相同资源的增量编码及零费用不重复发属性包。事实类型／事务诊断编号不同不代表原生事件等价。 |
| 宿主让出与调试I/O | 4毫秒预算仅在一个逻辑帧完成后检查；背压保持同帧、队列执行节点及Simulation阶段，完成前不扣帧债务，批量单步不把重试计为完成。I/O线程不执行玩法。对相同逻辑帧输入，不新增随机抽取、不按墙钟重排同帧回调；实时网络命令所属逻辑帧可能因宿主处理时机变化，不能据此承诺相同墙钟输入具有相同整场随机轨迹。 |

整个战斗尚未与原版逐事件对齐：现有Simulation领域阶段并非完整原队列1／2、AI／动作／同步命中交错，STATREGEN的下一次事件仍由D2X事务在成功后发布；D2MOO在回调开头直接插入，涉及嵌套定时器时还需要全局顺序迁移。普通技能Blood Mana费用已接人物事务；玩家HP百分比的`LAST_SENT_HP_PCT`差值大于4发送策略尚未在再生入口闭合。后续对齐须以当前MPQ及1.13c函数证据裁决版本差异，不用降低频率、合并随机、改目标顺序来绕过成本。

Howl／GrimWard使用独立TerrorUtility入口：投递Terror技能／等级1及普通移除回调，不借用死灵mA1／Possessed筛选和RestoreAi回调；抗性／Attract拒绝列表时仍执行原目标切换及一帧idle。完整Kill／Killed事件、所有技能生产端、专属飞弹及全局原顺序仍在迁移，公共列表接入不代表全部战斗已完成。

## 冰冷、冻结与毒伤

`gameplay/combat/damage_resolution`拥有抗性／穿透和持续时间的纯计算，`poison`拥有装备毒率支配与来源数时长计算；服务端结算和只读属性显示共用数值函数。`hosting/monster_actions_content::prepareMonsterColdRules`从当前MPQ准备States死亡掩码、MonStats2.deadCol、不可打断状态及难度除数，服务端不读取表。

冷冻命中向`monsters::System::damage`提交未经抗性／难度缩短的原始帧数。目标先按CannotBeFrozen、HalfFreezeDuration、抗性／Cold Pierce处理长度，再按身份选择冻结或冰冷：普通及符合条件的仆从可冻结，首领／精英／佣兵退回冰冷，不可打断状态拒绝冻结；ColdEffect及难度除数仍取原表。冰冷最少一帧，冻结在难度除法后即使归零，也参与该次命中的死亡状态处理。各弹体、武器、反击、环境和怪物来源不重复缩短长度。

死亡在同次命中安装状态后读取States的monstaydeath／bossstaydeath、hide、shatter、udead。`Actor::deathStates`保留原状态，尸体资格和原包投影共同消费；运行中的冷冻计时器清理不能撤销死亡碎冰。普通冰冷、Holy Freeze与UMod35使用原shatter状态；普通冰冷按目标随机流掷20%，且MonStats2.deadCol必须为零。冰冷与Holy Freeze对同一shatter位写入，活动状态结束时按原回调清位。原客户端继续按原状态绘制icebreak／隐藏尸体，无自研服务端专用表现。

通用直线弹体的触地、碰撞与自然到期均进入原SrvHit对应路径；冰尖柱在命中时重求范围／冻结长度，火球执行范围伤害。范围查询保留活动房间限制；不为原过滤器未要求的路径添加视线条件。

毒率使用固定点每帧伤害，命中当帧计入一次伤害，后续由单一毒状态的HPREGEN期限处理。伤害抗性与毒长度抗性分别求值，Poison Pierce分别作用于两个通道；玩家／佣兵毒长度应用难度惩罚和75%上限，普通怪物保留其原抗性上限。更弱毒不改当前毒率／期限；等强或更强只更新毒率／期限，保留原状态的施毒者及击杀归属，不叠加多个毒状态。持续毒对玩家保留1生命，怪物可致死；解毒、死亡和状态移除清理持续毒。

毒状态的下次扣血帧随提交推进，输出背压后结清仍欠的持续帧再到期；刷新不得覆盖尚未提交的区间。装备毒率先计算Poison Mastery，淬毒匕首将技能长度和装备长度合并后除以装备毒来源数，技能不额外增加来源数。`resolveSkill`接收显式毒伤支配，服务端及原服客户端数值投影使用同一入口。Venom已由刺客程序提供专用毒长度覆盖，Rabies由德鲁伊程序提供感染／传播初始化；它们的独立规则不由一般持续毒公式代替。

依据：本地D2MOO `SUnitDmg::{CalcDamage,ApplyResistancesAndAbsorb,ApplyColdState,ApplyFreezeState,ApplyPoisonDamage}`、`MonsterMode::D2GAME_MONSTER_ApplyStatRegen_6FC63440`、`MissMode::{HandleMissileCollision,SrvHit01,SrvHit13}`、`SkillNec::SrvSt16_PoisonDagger`与`SkillPal::AuraCallback_HolyFreeze`。本地原1.13c `D2Game.dll` MD5 `978f6f74cd031bce6ecee602cd5988cc`：RVA `DC4F7–DC519`包含毒率的命中总伤害，`DC780`冰冷、`DDAC0`冻结、`DCAB0`施毒、`DCBCE–DCC07`等强／更强毒刷新但不换owner。原`DC8F8–DC91C`为随机数小于20且MonStats2第19位deadCol为零才置shatter；此处与D2MOO快照的反向重建条件冲突，以1.13c为准。DLL及表导出只作证据，不作为运行依赖或提交内容。

## 诅咒、祭坛与战吼的公共生命周期

curse_resolve::curseStateDuration保留原期限减去单独取整百分比、负抗性及零期限规则，死灵、亚马逊状态和祭坛共用。rank_sources::nativeShrineSkillBonus读取准备的shrine_skill状态身份，玩家逐写被动查询与最终等级查询使用同一状态加成。CombatEffectSet的nativeExpiration区分普通诅咒的非零期限与战吼的零期限移除事件；refreshPassiveSkills和耐力恢复序号供原回调边界的候选观察消费，属于运行态，不写D2S。三种友方战吼和限时祭坛开始按逐写来源／资源轨迹提交，源码及缺口由BARBARIAN、NECROMANCER和OBJECTS维护；完整全局单位事件调度尚未完成。

诅咒的RestoreAi和ClearTargets现持独立回调序号，单位候选消费普通AI恢复及目标阵营恢复，嵌套来源准备保留已准备控制；AIRESET由怪物持有独立事件，不随列表缩短／移除被替换。当前局部队列保留重复和连续事件，但尚未并入原全局单位队列，与AITHINK／REMOVESTATE的完整同帧顺序仍待对齐。MonsterResourceTrace现从修改前状态基线保留回调序号，在撤出／回调观察时准备控制与原始阵营变化；后续属性写入和嵌套光环读取当时的候选。内视／慢速箭也已接该原始来源观察，重施的状态投影读取复用列表，不另覆盖一份最终属性。

原房间单位查询现抽出NativeAuraTargetQuery，半径使用signed32，OwnerRange保留光环／SrvDo006的零过滤器默认值及拥有者排除，TargetRange保留诅咒的目标中心、零过滤器及零范围区别。AreaStore::roomAtPosition只查原当前房间再按MPQ相邻房间顺序查目标点，不全幕扫描。光环、内视／慢速箭及群体诅咒读取同一房间链、整数距离和过滤程序；玩家PvP与非玩家接收者共用，跨区域事实按接收区域发布。仍未开放的飞弹／物件／物品、近战／JUSTHIT筛选明确要求对应原单位查找程序，不用普通单位近似替代。

## 周期技能帧

公共`aura_resolve::skillPeriodicFrame`按固定1.13c实现`max(5, PerDelay) * ((frame + max(5, PerDelay) - 1) / max(5, PerDelay)) + 1`，括号中的除法先执行，所有加／乘使用原signed32窄化。D2Game.dll同一固定快照RVA9E970–9E9A4确认PerDelay公式调用、下限5、IDIV→IMUL→INC；D2MOO SKILL_ComputePeriodicRate转录中的运算分组不能直接沿用。`skillPeriodicTick`另按原事件插入的signed到期比较把已到期帧推到下一帧，证据RVA8EAB8–8EAC2。-1是持续事件哨兵，目前定时光环适配明确拒绝该哨兵，完整持续事件待统一调度实现。

已接玩家光环首次非immediate调度和每次重新排期，从当前人物公共公式输入采样原PerDelay；佣兵／火魔现有光环循环的重新排期共用帧算子，其完整动态公式／AI调用仍待迁移。例：周期50时，当前帧32或50的下一事件均为51，当前帧51则为101；不能以“当前帧加周期”代替。即时程序、状态寿命、周期事件与普通动作帧仍是不同机制，完整光环及同帧流程仍待对齐。

周期事件执行现开始区分原帧计算与状态寿命：skillPeriodicDuration保留`skillPeriodicFrame(frame, PerDelay) - frame + 1`的signed32运算，不以事件插入后钳制的帧重算。固定1.13c D2Game SrvDo表RVA1074A8第65／66项分别为9A8D0／9AF50；9A955–9A988确认PerDelay采样、下限5、IDIV→IMUL→减当前帧→加2，9AFE5–9AFFF确认调用周期函数后减当前帧再加1。普通正寿命进入现有效果期限；持续哨兵及非正回绕寿命仍要求完整单位事件调度，目前明确拒绝。准备层此次补入Skills.Delay／PerDelay公式，避免公共求值因缺字段返回原-1偏移的0。

普通右键和已准备的装备周期光环开始共用advanceAuraCycle的目标／作用／脉冲游标。状态寿命在本次程序开始时采样并保留；脉冲排队成功后保留pulseCommitted，事件重排背压重试不再重复排入伤害。光环阶段未完成时不执行后面的恢复／再生阶段，减少整逻辑帧重试的重复工作；其余恢复及跨域阶段的完整游标仍待统一。

公共SkillFormulaSnapshot保留一次同步属性回调的来源根、属性／手位值、原始资源、基础／有效／引用技能等级及查询限制。捕获复制根及值，贡献叶为不可变共享来源；不保存借用的RuleRandom闭包，执行方必须在同一调用边界提供单位随机候选。input只允许仍存活的左值快照，返回借用输入，不把周期光环改为读取旧注册快照。PlayerSkillRefresh的item_aura非零激活和零值撤销均保留该时刻输入，区别于最后装备快照。公共itemAuraCallbackInstructions按固定1.13c的激活顺序准备CancelTimers→ScheduleTimer→ExecuteImmediate，撤销为RemoveState→CancelTimers；refreshItemAuraTimers仅消费其中的计时器步骤。executeItemAuraCallback为未发布候选提供同步执行入口：先验证所需状态撤出／immediate钩子，再沿指令顺序修改计时器并调用钩子；指令和公式快照拥有独立生命周期，嵌套属性回调不能覆盖外层输入。钩子只能准备候选，不发布权威状态。attributes::calculate另提供AuraMutation／AuraSourceObserver同步入口；绑定方在每个状态来源撤出、投递和写入之后报告候选来源与状态。计算器持有本次临时来源／状态，先更新来源树，再按StatKey顺序传播原始差值，嵌套技能回调、后续装备资格和资源上限采样读取当前候选；变化后的临时投影只放入Totals.auraTransient，不自行发布。PlayerSkillRefresh先记外层回调，再进入钩子，嵌套回调顺序及外层公式快照保留。普通周期消费者已改用共享applyNativeAuraState列表分配／写入入口，供即时候选复用。CombatEffectSet现为值语义共享存储，修改时分离；来源观察中保留的旧集合不会被后续写入修改。新句柄计数先推进再通知投递观察，快照不保留已使用的下一个句柄；逐写观察之后继续修改也会再次分离，不持有跨观察的可变列表引用。TransientAttributes.effects携带运行时完整集合，状态／来源投影及生命周期候选指向同一变化；不进入D2S。AuraSourceObservation可另外报告直接ResourceValues，当前资源立即影响后续公式，Totals.auraResources只保存最终候选，不重放旧账本。

人物事务已绑定零值item_aura的撤出步骤：先按当前AuraState撤出集合，逐来源观察传播属性／上限，再取消同GUID计时器。计时器在同步回调中更新，包括嵌套回调；事务不在末尾重放整张账本。单人、双库存及人物成组提交在输出批次接受后一起发布集合；新Recovery节点先准备，已存在节点只交换共享存储。效果系统后续候选发布沿人物已提交的完整集合收尾，避免旧副本把撤出的状态放回。CombatEffectSet开始区分原始状态位与列表：setNativeState／stateIds管理显式位，hasState先读显式位，尚未迁移的生产端才按活动列表推导。已接原始光环及普通数值诅咒在新列表分配时置位，同级复用保留原回调；到期不自动改变显式位。撤出先通知属性来源观察，再执行该列表的状态清理与被动刷新观察，符合D2Common ExpireStatList先传播差值、再调用fpStatRemove的顺序。刷新序号属于运行时集合，属性候选在回调边界刷新全部被动，不把请求延至最终装备求值。物品DeactivateAura先关位、再按状态查找并撤出列表，不能因位已关闭就跳过存在的列表。禁止回蓝现只设置独立位，不分配空限时列表；拥有者BasicAura移除回调再清除此位。固定1.13c D2Game BasicAura回调入口9A1A0在9A220安装98450；98450–98481确认清自身位、检查／清0x55禁止回蓝、随后调用被动／刷新入口。HolyFreeze拥有者的专用回调不沿用BasicAura的禁止回蓝／被动刷新操作。AI诅咒、完整死亡／专用移除回调、单位事件注销、全部状态生产端及原事件期限仍待迁移，不能据这一混合适配认定完整States／StatList复刻。非零immediate现开始在原属性回调中执行拥有者SrvDo065，仍先取消／排期再执行；不设置used／左右技能，也不等待下一周期帧。当前MPQ的10项immediate光环均为65、允许城镇、基础／逐级法力为零、无UseManaOnDo／数量／飞弹／延迟操作。拥有者从该次SkillFormulaSnapshot计算原Aura／Passive值，复用applyNativeAuraState，元数据后按PassiveState撤出独立缓存被动列表；后续装备资格读取变化后的来源。直接生命／法力／耐力写入借AuraSourceReader读取当时资源和最大值，再报告ResourceValues。原强制入口允许绕过正等级／拥有技能检查，原始状态的signed等级不沿用普通效果的非负校验；时长仍用原周期表达式。固定1.13c D2Game ADBD5–ADBE4传入1／1／0调用A19A0，A1A7E–A1AC3确认强制参数绕过used／最高技能正等级检查。Might的MinMana=1不意味着费用：D2MOO SkillMana_Consume先在基础／逐级法力都为零时返回，不进入MinMana分支；同名owned used-skill现开始按该所属物品／该槽等级读取原充能层，原Handler成功Do后同步消费；没有物品或可用充能时不撤回Do，零充能的属性回调按原规则清理技能实例。充能层的本件缓存、来源、物品修订与装备规则在同一候选里更新，不调用另一次人物事务。其他强制执行器／飞弹／延迟、PLR和非资源直接属性仍要求完整单位候选。

跨玩家接收者现开始纳入同一准备／提交：auraCandidateRecipients使用整数格、signed32平方距离、当前相邻房间与filter规则，当前10项immediate的0x12003跳过城镇接收房间，但拥有者单独执行。withinNativeAuraArea共用PATH_ComputeSquaredDistance的减／乘／加及半径比较，未使用浮点距离；filter的0x200射线使用整数坐标及原掩码4。接收者初始Aura操作在其同步属性候选中执行，与拥有者共用写入入口，保留来源GUID、同级复用、元数据和缓存被动撤出；同一外层回调多次触及接收者时，后一次借用前一次的临时候选，不重读旧权威属性。

单人提交持有独立auraPlayers计划，提交前复验全部接收者身份、两种修订、区域与生成号，再一次接受全部输出，随后一起发布人物／效果快照及used-skill清理；输出背压不提前发布拥有者或接收者。相同属性的状态事实不重复发送，但列表寿命仍更新。怪物／佣兵接收者现开始进入AuraUnitPublication：MonStats.noaura与MonStats2.isAtt单独从当前MPQ准备，NPC／NoAura单位不进入此BasicAura接收路径，不以mA1或curseable替代。每个单位保存修改时分离的列表候选、基础来源与生命候选；同一外层程序多次触及该单位时累积前一次结果。原始属性逐项写入及元数据共用applyNativeAuraState，随后按PassiveState撤出缓存贡献；破免读取原基础抗性，佣兵豁免，速度／attackrate按当前ColdEffect下限，直接属性读取候选当前值及MaxStat。提交前同时复验单位修订、区域、效果集合快照身份和生命／速度所需修订容量；缺失效果节点在准备阶段预留。单位状态事实与玩家输出一起接受后，再一起发布列表、生命／基础来源和速度；背压不会提前改变单位。嵌套光环现开始复用活跃接收者的列表与来源观察／读取入口；prepareGroup以一个候选图按主事务顺序准备，接收者修改可回到尚在计算的拥有者。交易、双目标增益、群体诅咒及单位增益／治疗共同复验和发布，既有单位来源与生命候选可作为该图的初始输入，旧效果副本不再覆盖新光环。入场先保留角色保存资源，再以无旧装备缓存执行当前逻辑帧即时回调；失败不进入游戏。普通／物品／佣兵／火魔／怪物的20项圣骑士光环开始共用原始公式，目标沿MPQ相邻房间次序查询，包含已准备跨区域房间，并按接收区域提交状态／脉冲。AreaStore现持有独立房间单位链，按D2MOO UNITROOM头部插入／原房间摘除更新；玩家、NPC、怪物及召唤物的入场、移动、跨房间、传送、移除接入该链。新单位节点在创建准备阶段预留，移动和发布不另分配节点；光环按MPQ相邻房间次序和实时单位链查找，玩家／怪物不再分成两批遍历。尚未进行同版运行认证，完整原创建／休眠顺序仍由后续生命周期阶段核对。中间来源采样开始保留被动缓存，元数据写完后撤出PassiveState列表，原移除回调触发缓存刷新；怪物／佣兵补入对应刷新。普通／BasicAura／Holy Freeze移除策略和死亡保留位、限时列表分开，存活拥有者的Holy Freeze回调单独清SHATTER。非玩家PLR毒期限开始作为LifeChange状态候选随生命／列表复验和发布；普通、物品、佣兵／火魔及怪物光环开始按拥有者状态→伤害抽样→逐目标状态／伤害／Holy Freeze SHATTER推进，每目标保留状态、伤害及后处理游标。伤害范围／目标搜索在拥有者回调后采样，城镇66／81分支提前结束。伤害抽样与Redemption使用单位当前种子，SHATTER在伤害后使用接收者种子；玩家种子由入场父种子按SUNIT_InitSeed公共算子准备，不写D2S。毒期限与SHATTER有复验后的发布边界。玩家其余技能／攻击的随机入口尚未统一，因此这不认证全局原版随机序列。Simulation保留Effects／Combat阶段，effects另持有阶段及人物资源／技能完成标记，背压不重放已提交的再生、周期伤害或后处理。非玩家物品计时器和按staydeath清理独立状态位已接共享候选；原生PeriodicStats驱动属于当前正在收尾增量，完整死亡顺序、全部单位事件及原全队列优先级仍待收尾。原始列表的分配／复用及属性查询开始按仍投递的列表处理，不因期限已到就自行忽略／清掉无关原始列表；实际撤出由移除阶段完成。完整接收者查找、原事件先后、GFX／被动列表生命周期仍待闭合，不能据拥有者分支认定完整immediate已实现。

公共evaluateNativeAura读取当前MPQ的AuraStatCalc／PassiveCalc、AuraRangeCalc、PerDelay及公共原伤害／法力参数，不再为这两类玩家周期消费者另列类型化公式。SrvDo065用Aura六槽同时准备拥有者与目标，遇缺槽停止；拥有者法力严格大于费用且PassiveState≤0时，才准备额外Passive五槽。固定1.13c D2Game RVA9AA63–9AA75确认这两个额外门；狂热的damagepercent按原SetStat覆盖半额。SrvDo066／081的拥有者用Passive五槽，非玩家可越过法力门，目标六槽仍要求当前raw法力≥费用；082只准备拥有者Passive五槽，尸体过程独立。数值保留各Stat原单位，不能将ValShift=0的元素附加伤害再当8.8移动。拥有者直接Passive写入与BasicAura回调分开：后者处理生命／有害时长，逐项写入非零属性、attackrate紧接other_animrate、末尾写技能／等级元数据，随后才写拥有者额外Passive。同等级状态复用保留来源及未写值，覆盖重复键。服务端接收者调整读取怪物基础抗性作破免判定，玩家／佣兵豁免；速度与attackrate按原ColdEffect下限处理，显式other_animrate槽独立。原始周期消费者现保留Direct及有害时长操作在原槽序中的位置，拥有者额外Passive仍走列表SetStat。ItemStatCost.Direct／MaxStat由当前MPQ链接到StatRule；公共nativeAuraDirectStatValue按signed32加法及最大值比较生成原值，不添加零下限。玩家的生命／法力／耐力沿资源候选写入；具备原始来源的非玩家用同一生命或基础属性候选，数值不改变时不计入生效。列表非零指令计入生效，作用谓词仅随该接收者提交成功积累；当前周期用途等价于原field_40>0，不保存原完整计数。毒／诅咒期限改用公共nativePercentage，现有不可治愈诅咒仍计入生效但不缩短；零期限直接更新现有句柄，不重投整张毒属性表。持续／负回绕期限仍明确要求完整单位调度。

周期费用在接收者及脉冲准备后独立提交，并保留manaCommitted，末尾重排背压不重复扣费。没有生效时撤销禁止回蓝，有生效时先设置再调用费用分支；法力不足不撤销已经执行的作用。Blood Mana从当前MPQ的blood_mana及其来源技能Param5准备：生命不足先撤出列表再写256 raw生命；否则代扣生命，低于Param5<<8时撤出。固定1.13c D2Game RVA9A326–9A36B确认直接值相加、MaxStat上限、值改变才计数，9A30A–9A31F确认attackrate伴随写入；9A5CC–9A5FE确认按计数设置／清除禁止回蓝再调用9ED20，9ED20–9ED6C确认Blood Mana优先及普通不足法力返回，BE20–BEDC确认生命代扣、Param5阈值及不足生命写256。独立状态位／列表撤出及被动刷新边界已接持续效果模块；完整直接属性回调（包括玩家单位非资源基础值）、随机流、目标排序、同帧事件／伤害完成顺序及费用／反噬组合仍待闭合。

普通技能费用现通过显式SkillManaPayment标记进入人物／库存事务，覆盖当前法术、武器、召唤、传送及Telekinesis扣费入口；伤害／法力吸取不使用此标记。零售1.13c CheckMana（9EC80–9ED1D）仍检查法力，Consume（9F850–9F969）在mana／lvlmana均为零时提前返回，聚气先走物品次数；普通费用持blood_mana才进入BE20。通用Start／Do调用点9FE87／A1C8A不检查其返回值，因此生命不足不撤销已接受的程序。生命小于费用时先逐项撤出列表、执行状态／被动回调，再写1生命；否则先减生命，再按来源技能Param5阈值撤出，恰好相等允许写0。普通费用与AuraConsume共用纯数值函数；事务保留原法力资格检查，已准备方案重建时不重复扣费，效果发布沿人物已提交集合收尾。恰好零生命后的死亡／再生、背压、聚气及其余费用组合仍待核对，全部费用执行时序尚未与原版对齐。
