# 技能公共规范与实现边界

更新：2026-10-16。本页维护技能迁入规范、跨职业计算与状态所有权。原服和自研宿主使用同一客户端；客户端提交原请求、读取原包及已知属性，服务端拥有命中、消耗、效果和保存。旧单机执行器不恢复到客户端。

## 当前持续效果入口

`CombatEffectSet`持有状态位、列表、原有符号移除期限和队列1技能事件登记表。当前MPQ的Events／Skills提供事件号、三个AuraEvent／AuraEventFunc槽及覆盖图身份；列表逐写后撤销旧登记并按槽顺序插入链头。触发保存本次节点快照，尚未开始的节点复验是否仍登记，已开始的节点跨背压完成；后续单位的事件在进入该阶段时重新取链。召唤贡献完成后登记石魔事件，没有为无AuraState的宠物编造状态列表。登记表与属性列表投递／撤出分开，骨甲、能量盾和飓风装甲按AbsorbDamage登记链处理；每个回调复验登记，按技能AuraState查最新投递的实际首列表，耗尽走公共撤出。

combat的玩家／怪物／佣兵／宠物命中保存原始伤害、计算结果及阶段游标，先准备防御／护盾，再执行单位事件，最后提交生命扣除。铁处女、偷取生命、荆棘、三种冰甲、血魔回复／主人分担和粘土石魔减速已经迁入这条路径，旧事后反应队列及重复近战扣血入口已撤除。反伤结果32的路径不再递归产生普通伤害事件；回复与覆盖图分别保留完成阶段。普通偷取使用事件后的当前生命上限约束。飞弹returnFire从当前MPQ传递，冰甲的HitByMissile在准确率／格挡／回避及基础扣血前执行，落空仍可反击；范围爆炸不继承父箭的接触事件。

固定原1.13c D2Game.dll（MD5 `978f6f74cd031bce6ecee602cd5988cc`）技能事件表RVA1077A8确认回调1／2／3／4／5／22／23／24／26／27。骨甲RVA4DFE0的耗尽分支扣掉原吸收量，与D2MOO Event22的错误转录不同。近战填充和最终执行各有AttackedInMelee调用：原地址6FCFD97B和6FCFEE62调用RVA37030的事件派发；两次原调用分别保留游标，不把它们当作重试重复。未命中不填充伤害，仍保留最终近战尝试事件。原ExecuteEvents RVADE0C0先派发攻击／受击事件，随后限制物理伤害到当前生命。

Misc耐力／解毒／解冻药剂保留原stat/calc顺序，以同一状态列表延长原期限并逐项写入。耐力恢复的MAXSTAMINA+256由具名事务策略保留。井水先清毒／冻结，再按原状态ID遍历curable；NPC对人物与宠物保留各自解除顺序。宠物按类型升序、每类真实登记顺序遍历，死亡标记与GUID分开，Revive重新登记；换区保留已有单位效果和登记。

诅咒AI的DimVision／Terror／Taunt决策抽到gameplay/monsters/curse_decision。恐惧保留初始目标、参数2首次逃跑与后续失败游走，不把默认30写成随目标距离增长的阈值。嘲讽的-666仅表示保留参数，成功切换AI与分配被拒绝后的目标／下一帧思考分别处理；解除与同级重施保留实际函数选择序号。已实现的沙虫／蝙蝠家庭先经过替代回调再进入特殊AI。怪物Cursed保留逐次公式快照与目标游标。

以上新伤害事件接入的Windows Release CMake配置与all目标构建通过，两包已更新。普通攻击发现直接伤害伪弹体没有MPQ定义，现仅对实际飞弹读取ReturnFire。骨甲耗尽、伤害前偷取生命、铁处女及三种冰甲有历史有限Single Player观察；正常退局／新进程保留人物生命，临时状态清空。刺客30项及传递依赖现已实现、构建打包并完成代表单机观察，见[刺客](ASSASSIN.md)。其余帧事件、队列2物品事件与技能事件的完整交错、特殊目标组8、其余未实施家庭AI与德鲁伊程序仍由后续大模块完成；完整动作／随机流及所有原版程序的验收也未完成。构建、实际包身份和有限运行范围由[运行包记录](../../development/BUILD_AND_RUN.md#运行包记录)维护。

`effects/damage_absorption`共用原Event22／24／25，已纳入当前工作区all目标构建。骨甲保留1.13c耗尽时先扣原池值的修正；飓风装甲按火→冰→电消耗同一AuraStat池，保留原版RVA64600的有符号显示更新判断和百分比参数顺序。能量盾按实际接收者资源逐通道扣法力，公式Rand借用其私有种子并随候选提交，方向覆盖图共用原D2Common 11053扇区。玩家、非玩家普通命中及环境伤害分别保留吸收阶段，容量重试不再次消耗池；逐项属性写回不重新登记事件。GetStatListFromUnitAndState共用`CombatEffectSet::findState`的反向投递序，原生复用和状态投影采用同一首列表；旧浮点／批量骨甲吸收入口已撤出，纯伤害缓解不再依赖效果列表。本轮未专项重测全部吸收组合，不能将构建通过当作这些组合的运行认证。

SrvDo18补入当前MPQ的飓风装甲、Quickness、Fade和Venom，六AuraStat、五PassiveStat、元数据与期限由公共公式准备，资源／覆盖图／音效仍读取MPQ。同组撤出按原State ID顺序先清位再撤首列表，包含只有状态位的条目。`effects::unitSkill`以实际非玩家属性、资源和投递门槛替代旧佣兵专用入口。Venom的`skill_poison_override_length`进入公共近战毒长度函数；覆盖长度不除装备毒源数，并保留技能长度。`rollMonsterUnitHit`读取实际单位、当前Mode和公共效果的完整属性，火／电／冰／魔法／毒依次投掷，毒率不左移8位；不再仅给死灵召唤物叠加buff元素或重复投掷光环伤害。

召唤物上限替换、主人死亡和主动遣散通过`stageRetirements`／`stageDamageStates`合并旧单位的死亡列表撤出、资源回调、状态事实与人物／新单位提交；寿命到期和环境陷阱结束共用`scriptDeath`，容量拒绝保留当前阶段。零MAXHP的原生单位允许死亡资源候选，存活模式与HP继续分离。真正移除实体时同步清除效果、光环／附魔／Web／物品光环来源缓存及帧事件；不把离区仍存在的实体当作销毁。完整队列1／2登记及回调交错、HPREGEN击杀回调、其余持续技能程序和全局帧顺序仍未完成，不能据这些源码变更宣布全部buff或战斗已收尾。

## 帧事件底座与持续效果期限

`gameplay/events/frame_queue`持有原类别顺序、64个到期桶及持续队列。原1.13c RVA8F070确认飞弹→玩家→怪物→物件→物品，每类持续事件先于到期事件；RVA8EA80确认signed期限归一化与到期尾插，RVA8E810的持续登记忽略显式回调并头插；RVA8E6F0确认执行中的删除先标记。回调保存值快照和下一个节点，取消下一个节点时推进游标；背压保留执行节点，不进入后续类别或逻辑帧。节点与拥有者索引在候选准备时分配，领域接受后无分配链接，候选放弃不登记真实事件。

`GameSystems.frameEvents`拥有实例队列；`effects/frame_events`将玩家／单位状态期限与其状态候选一起发布；`monsters/frame_events`按真实AIRESET身份分别清除迷乱／吸引选择器，保留DimVision／Terror／Taunt函数。普通效果的兼容期限仍留在原有维护入口。当前源码中，item_auras按GUID处理PeriodicStats，right_aura处理右手custom ID -1，storm处理雷云正skill ID，hirelings处理单位右手／列表光环周期，unit_aura_selection准备原SetSkills／AssignSkill与跨单位候选；周期结束才采样当前PerDelay并重排，旧玩家／佣兵光环与风暴轮询已移除。雷云临时弹体的种子依据D2MOO SUNIT_InitSeed与MissMode：game seed派生私有种子，伤害掷数不直接消费人物种子；原最高技能存在判定与列表存储等级、右手active等级分别保留。自然怪物光环附魔的创建登记及都瑞尔AI首次分配已接原custom ID -1；子体创建开始在对应操作采样公式并以game seed初始化私有种子，玩家／怪物发射与寒冰甲反射也共用此分配来源。敌方伤害来源缓存、min/max投掷时点、完整飞弹程序与来源生命周期尚未收尾，该切片已有Windows Release构建与代表路径Single Player冒烟，当前两包已包含这些输入，仍未完成全模块验收。完整周期、AI思考、动作、中断／死亡及同步命中还没有全部迁入，不能将当前类别队列视作全战斗顺序已对齐。此底座／持续效果期限大模块的CMake配置、all构建和两包更新通过；迷乱替换、虚弱到期、吸引的独立重置、玩家祭坛到期及新进程恢复完成有限Single Player观察，实际包与范围见运行包记录。活冻结怪物的AIRESET由原RVA769A0确认：跳过回调后一次性事件仍消耗，持续事件等解冻后再执行；此组合未运行认证。

`gameplay/missiles/damage_data`开始提供原CalculateDamageData／AddStatsToDamage／CalculateFinalDamage／SetDamageStats的纯值入口，物理／元素／吸取／燃烧范围、原整数缩放、毒覆盖长度及计数、参数层写入与holy标志分别持有；RollDamageValue与FullDamageParams／GetDamageValue保持创建与命中投掷的不同阶段。原1.13c D2Common RVA6B180确认加倍判定使用unsigned余数，暴击始终消费一次投掷，随后仅非零致命攻击、存在武器且非零精通才继续；RVA6BFD4..6C03B确认来源飞弹的恶魔ED仅加一次，未照搬参考中重复相加；SetDamageStats保留随后由BurnLength重写FIRELENGTH，以及原128层damage_vs_montype复制边界。对应反汇编片段已与本地DLL字节匹配，证据保留在frame-events-20261015-025625。现有范围投掷共用新RollDamageValue，雷云无SrcDam／无holy／无物理分量的当前程序开始经过共同数据／完整投掷入口；普通敌方弹体已保存范围，逐目标命中进入combat/missile_contact的保留阶段；命中、格挡／闪避、怪物暴击分别借用实际攻击者、受击者及攻击者种子，范围使用飞弹自身种子；双爪格挡保留原32参数层边界。内容层从当前MPQ导入MonType三等价关系，伤害匹配复制的128参数层。子体创建借用真实拥有者或原SrcMissDmg父体种子，先准备再一次发布，未再提前掷固定敌方子体伤害。公共combat/drain保留六小数位／难度除数／Drain分支，来源补资源在伤害前回调后、目标HP前完成；玩家恢复投掷与HP候选一起提交受击者种子。NextHit的JUSTHIT开始走nativeMissileHitDelay：原状态、空列表和REMOVESTATE一起准备，零NextDelay仍排期，门控等实际回调解除，兼容旧期限仍独立保留。佣兵弹体程序参数已由无随机的prepareWeaponSkillProcedures抽出，取消创建时固定伤害投掷；所有者精通使用当前usedSkill的共享Range/ITypeA[0]判定。damage_program把SrvDmg放在完整范围投掷之后、SetDamageFlags之前，包含1／12转换与选定元素重新投掷、2／4／10冻结长度、3软命中随机、5／13目标加成、7／9眩晕和14尺寸击退；6／8保留无宝藏及ProgOverlay的领域发布，11读取实际TargetX绝对到期帧；未初始化的原生零值使毒无效，不用飞弹寿命猜测。冷却／冻结长度独立保留，玩家冻结路由冷却也分别消费随机；GetHit、SoftHit与KnockBack资格取当前MPQ，格挡后的动画与伤害成功分开。原1.13c D2Game的表6FD2DB68有31槽，1–14入口及片段已对DLL字节确认，证据为native-server-damage-table-113c.json。玩家武器弹体固定伤害快照已在后续源码撤出，普通弹体与怪物弹体开始共用数值来源与接触前奏／SrvHit／HP阶段，见[飞弹收尾](../../modules/SERVER_SYSTEMS.md#飞弹数值与接触收尾源码)。统一技能／物品事件顺序、狂犬病参数、原生冷冻／眩晕列表、部分来源组合与特殊SrvHit／燃烧程序仍在迁移，纯函数存在不代表全部适配或运行通过。此前冒烟源码已按授权通过Release构建与代表路径Single Player冒烟；后续跨单位源码已纳入当前all构建与两包，运行认证仅限运行包记录列出的路径；未覆盖数值和组合仍待验收，不新增测试程序。

## 跨单位接收与载体边界

`CombatEffectSet`、`CombatEffectSpec`与`StatList`是公共机制，单位身份、敌我和主人关系不能决定是否另建一套Buff模型。玩家和非玩家各持唯一主列表，技能、药剂与损伤状态均投递到该列表；`effects::System::unitEffects`借用实际列表，`unitStatSources`／`unitStates`／`unitStateStats`投影实际来源与原生状态。玩家已有总属性包含状态来源，公共查询不再次叠加。非玩家药剂来源按列表实际内容参与，不以佣兵身份作为数值查询开关；复制层从公共查询读取，不维护另一份状态集合。技能列表移除仍由原帧事件完成，不能按查询时刻提前丢弃仍投递的原始列表。

接收资格分开处理：`NativeMonsterStateTarget::acceptsCurseList`表达D2MOO `SKILLS/Skills.cpp::sub_6FD10EC0`的原UNIT_MONSTER检查，即MonStats.npc为假且MonStats2.isAtt为真；名称中的Curse来自原结构，该入口也被Enchant调用，并非仅用于负面诅咒。NoAura、技能filter、敌我／队伍／主人、死亡与技能专用AI条件分别检查；Necro部分程序的mA1条件继续保留。不能以isAtt替代mA1，也不能把“可作为原生状态接收者”等同于“敌对怪物”。这些值由hosting从当前MPQ准备，玩法不读取资源。自然怪物、佣兵、死灵／亚马逊／刺客召唤物、Hydra通过公共状态规则准备入口读取资格。

| 单位 | 当前承载 | 边界 |
| --- | --- | --- |
| 玩家 | 人物基础来源、资源轨迹与公共效果列表 | 由人物候选发布，总属性不重复叠加 |
| 怪物、佣兵、已有召唤物 | 非玩家Actor、原生属性来源、MonsterResourceTrace与公共效果列表 | 角色、阵营、主人及各技能资格分别判断 |
| 城镇服务NPC | 同一个非玩家Actor；AreaNpc只保存GUID、出生点和交互／剧情能力 | 原表空Level／HP为零，存活模式独立于HP；不能伪造生命，也不接敌怪AI |
| 友军剧情NPC | 同一个非玩家Actor及公共Participants／空间索引／属性／资源／复制路径 | act5pow保留npc=0、isAtt=1、mA1=0、noAura=0、Align=1；不创建影子怪物 |

NPC规则由`hosting/npc_content`准备：`loadMonsterCombatProfile(NativeNpc)`共用MonStats／MonLvl缩放，`prepareMonsterLifecycle`共用状态、组件、动作时钟和技能来源。World分配原GUID后由`monsters::System::admitNpc`创建唯一运行实体；`AreaNpc.spawnPosition`只作出生内容，不随移动回写。NPC对话、交互靠近、任务距离和囚犯逃离读取真实Actor位置。Actor拥有HP、maxhp、随机种子、属性根和动作状态，`MonsterResourceTrace`处理状态投递／撤出与最大生命变化；NPC任务／服务能力不会授权攻击或治疗。

囚犯移动消费公共速度修正、恢复／冰冻／眩晕／击退状态；移动提交成功后才推进路径节点，提交被拒绝时保留原路径游标。死亡使用既有状态死亡掩码，离区逃离则撤销Actor、房间链及该GUID的状态／周期事件和效果缓存。区域休眠不是实体删除，其他来源已经投递的有限状态仍由真实期限维护。NPC与其他非玩家单位共用AC、动作和A7／A8／AA状态及数值快照；hosting的NPC专用投影只保留任务感叹号，按玩家任务可见性过滤，避免重复分配GUID或维护第二套NPC状态。调试快照的NPC位置来自运行实体，出生点单独显示。

### 按原程序区分接收资格

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

证据来自固定D2MOO `5596f5cb6c5251a0a07c6637d26458b06099d516`：`D2Common/DataTbls/MonsterTbls.cpp::DATATBLS_CalculateMonsterStatsByLevel`、`D2Game/MONSTER/Monster.cpp::MONSTER_InitializeStatsAndSkills`、`SKILLS/Skills.cpp::sub_6FD10EC0`、`SkillSor.cpp::SKILLS_SrvDo025_Enchant`、`SkillNec.cpp::sub_6FD0B2B0/sub_6FD0B450`、`SkillBar.cpp::SKILLS_ApplyWarcryStats`、`MISSILES/MissMode.cpp::SrvHit18/SrvHit21`、`MONSTER/MonsterUnique.cpp::MONSTERUNIQUE_CurseCallback_ApplyAmplifyDamage`、`OBJECTS/ObjMode.cpp`祭坛程序以及`PLAYER/PlrMsg.cpp`佣兵物品使用入口。当前MPQ证据保留于忽略目录`artifacts/unit-effect-carrier-20261015/`。这组参考仍为1.10f，不能直接认证所有1.13c分支。

净化复用`CombatEffectSet::findState`查询最新的实际投递列表，再按句柄撤出；没有列表时不清除独立状态位。`NativeStateCure`分别表达显式毒／冻结查询与curable掩码遍历所要求的状态位条件，顺序由hosting准备。治疗者对玩家先毒／冻结后curable，对宠物先curable后毒／冻结；井水对二者均先毒／冻结后curable。`effects::restoreWell`以`WellRestoration.usesCharge`区分恢复事务成功与井水次数消耗，允许只清宠物状态的事务提交；井水模式只在原次数分界改变。对应依据为`ObjMode.cpp::OBJECTS_OperateFunction22_Well/OBJMODE_PetIterate_Heal`与`UNIT/SUnitNpc.cpp::SUNITNPC_PetIterate_Heal/D2GAME_NPC_HealPlayer_6FCCB080`。物件恢复时钟仍使用当前objects领域时钟，完整对象AITHINK队列顺序属于后续全局调度迁移。

原生Direct列表分配与公共诅咒／状态组投递分别选择策略；直接分配不隐式套用Attract保护或跨状态组互斥。`effects/skill_state`按原程序区分六槽公式来源：FingerMageSpider／SpiderGoo及BattleCry读取接收者，战吼读取施法者，感染读取最初感染者；逐槽写入对后一槽可见，attackrate紧接other_animrate。玩家和非玩家共用数值／资源／期限／状态投影及实际公式单位的随机提交；BattleCry保留公共诅咒接收门槛、抗性与等级策略，战吼使用Direct列表。原版1.13c的FingerMageSpider和SpiderGoo首次分配后仍给六槽写入函数传空指针，已有列表才写属性；此分支按DLL保留。时长分别使用AuraLenCalc或SpiderGoo的Calc4，不混成同一技能期限。

原生冷／冻／眩晕、毒／烧伤／开放伤口现由`gameplay/effects/native_status`和`effects/damage_states`投递主列表及REMOVESTATE。冷状态写velocitypercent／attackrate／other_animrate，移除清shatter；冻结移除恢复中立模式，应用／刷新及AI切换通过公共LifeChange更新思考时钟。原怪物生成先设置UNIQUE位，再添加CHAMPION位；`nativeUniqueMonster`统一表示这一原生位组合，冻结资格和眩晕概率包含勇士，眩晕判定消费实际攻击者种子。非玩家毒元数据从列表实际HPREGEN、来源及期限投影，不另算持续扣血。蛛网施放状态与受击减速也进入主列表，旧Slow额外数值载体及轮询到期已撤出。

药剂投递共用`gameplay/consumables/potions::applyPotionState`，按原signed到期帧、旧列表速率和剩余时长合并，逐项原生属性写入；玩家和佣兵均由实际单位种子判定加倍。玩家治疗按血魔石魔原登记槽分配实际可加生命，佣兵恢复与物品消费原子提交。旧Recovery.healing／mana、Actor.healing及potionEffects均已撤出。`effects/player_regeneration`按HP→耐力→法力顺序执行，`monsters/regeneration`统一正／负HPREGEN、life掩码、PreventHeal、城镇和严格溢出取消事件；毒／烧伤刷新、受击与非玩家新药剂列表重排STATREGEN。不可中断动作中的零HP使用DEATH_DELAY和原模式存活语义，等待状态撤出再进入死亡模式，不伪造1HP。

持续效果性能修复源码：玩家STATREGEN的普通路径读取已发布FullStats，只提交生命／耐力／法力变化；满资源且无列表撤出时只重排下一帧事件，不创建人物变更，不复制库存／装备／被动图。药剂列表／原状态位实际撤出时仍走完整资源轨迹和有序回调，保留HP→耐力→法力及整数边界。单纯资源变化输出不可变ResourcesFact，复用原属性增量编码，不引入私有游戏消息；非玩家未变化列表不每帧重贴，查询到同一有序状态列表及相同标准化值时复用已存FullStats，输入／顺序／上下文不同则走原重算。Blaze仅实际跨格时求值，物品恢复仅到期时复制库存候选。

普通无聚气、无外部费用候选的玩家飞弹释放复用`transactions::release`：法力、拥有者随机游标及已准备的飞弹事实由同一输出预留／修订复验后提交，容量或资格失败不提前消费资源、随机或实体ID；带物品聚气、嵌套费用候选及非玩家来源继续各自原有事务。单技能等级查询直接消费同一原生等级公式，避免先构造全部已分配技能的等级映射。

属性图求值中，非扩展叶子直接读取已有BaseStats并继续拒绝叶子拥有子贡献；它没有依赖拥有者的运算，不为每个缺失属性再建立两份零值缓存。扩展列表的生产者、反向操作、上下文、存储值和最小值规则仍由原公共求值器执行，不改变属性数值、来源顺序或随机消费。

依据是本地1.10f D2MOO的`PLAYER/PlrModes::{EVENTS_HpRegen,EVENTS_StaminaRegen,EVENTS_ManaRegen,D2GAME_EVENTS_StatRegen_6FC80F80}`、`D2Common/D2StatList::STATLIST_UnitGetStatValue`及`SKILLS/Skills::D2GAME_SKILLMANA_Consume_6FD10A50`的普通法力分支：原单位读取已发布属性，普通资源加减不重穿装备／刷新全体被动。此轮没有重新反汇编1.13c对应函数，沿用已迁移数值，不将工程优化宣称为新确认的1.13c规则；这也不认证尚未统一的全部技能费用／Blood Mana入口。队列与管道的可让出调度属于D2X实现，语义核对和当前源码边界见下节。构建身份、测量及有限运行边界统一由[运行包记录](../../development/BUILD_AND_RUN.md#运行包记录)维护。

### 性能优化的复刻约束核对

本节为源码与固定D2MOO的对照结论，不是逐帧／逐随机运行比对。2026-10-15复核修正已纳入Windows Release收尾构建与两包；当前客户端为`FE70B8F1…`，治疗／法力药剂、寒冰装甲消耗及带临时状态保存／新进程恢复获有限观察，范围见[运行包记录](../../development/BUILD_AND_RUN.md#当前物品显示与生成收尾)。未复验全部性能窗口或野外飞弹费用分支，不能用这些观察认证逐事件／随机等价。

| 路径 | 可保留的优化及约束 |
| --- | --- |
| 属性叶子、已发布FullStats | D2Common `STATLIST_GetTotalStat_6FDB63E0`按扩展标志选择FullStats或BaseStats；叶子直接读取不改变整数值。扩展运算／MinAccr仍用原公共求值器；不能把已发布FullStats随意替换成从头汇总，因为原增量传播／回调可能保留不同的中间结果。状态查询复用要求完整有序来源身份、标准化值、叶子标志及上下文一致，变化时撤出缓存。 |
| 玩家STATREGEN | 普通路径只读当前发布值，保留HP→耐力→法力、8.8与原整数运算，不合并多帧再生。可能撤出药剂列表／状态位时进入资源轨迹。复核修正原慢路径：HEALTHPOT撤出回调先执行，随后写入撤出前缓存的生命结果；MANAPOT撤出后，把按撤出前法力／上限钳制的增量加到回调后的当前法力。法力按先上界、后下界钳制，不依赖`std::clamp`的边界有序前提。 |
| 非玩家列表维护 | 只在存活、已有FullStats且无到期工作时跳过重贴；复核补充FullStats存在性门槛。真正投递／撤出仍同步发布属性、资源、控制与速度。普通到期、原RemoveState事件、死亡撤出不被此快捷分支替代。 |
| 技能等级、Blaze、物品恢复 | 单技能查询使用原等级公式及相同输入；Blaze仅把无共享随机回调的`skills::evaluate`延迟到实际跨格创建分支，创建资格与飞弹构造顺序未动，参照`SkillSor::SKILLS_CreateBlazeMissile`先查移动／列表／城镇再创建。物品恢复维持原物品遍历与到期顺序，只延迟候选库存复制。无匹配效果的erase仅省去复制，当前所有私有谓词均无副作用。 |
| 飞弹费用与随机 | 构造仍在全局／拥有者随机游标的副本上执行，未移动`launch/make`内的抽取位置；身份、容量与费用失败不提交这些游标或实体ID，重试从未消费的游标重算。普通费用快捷事务在复验后一起提交费用、拥有者随机候选和事实，聚气／复合费用／死亡触发保留原路径。复核修正快捷事务的无条件ManaFact，带随机候选的释放改用ResourcesFact，维持旧CharacterFact相同资源的增量编码及零费用不重复发属性包。事实类型／事务诊断编号不同不代表原生事件等价。 |
| 宿主让出与调试I/O | 4毫秒预算仅在一个逻辑帧完成后检查；背压保持同帧、队列执行节点及Simulation阶段，完成前不扣帧债务，批量单步不把重试计为完成。I/O线程不执行玩法。对相同逻辑帧输入，不新增随机抽取、不按墙钟重排同帧回调；实时网络命令所属逻辑帧可能因宿主处理时机变化，不能据此承诺相同墙钟输入具有相同整场随机轨迹。 |

尚不能认证整个战斗与原版逐事件一致：现有Simulation领域阶段并非完整原队列1／2、AI／动作／同步命中交错，STATREGEN的下一次事件仍由D2X事务在成功后发布；D2MOO在回调开头直接插入，涉及嵌套定时器时还需要全局顺序迁移。普通技能费用也尚未统一Blood Mana分支；玩家HP百分比的`LAST_SENT_HP_PCT`差值大于4发送策略未在本次再生入口闭合。这些是既有缺口，不能由性能冒烟或本轮局部对照宣称完成。后续对齐须以当前MPQ及1.13c函数证据裁决版本差异，不用降低频率、合并随机、改目标顺序来绕过成本。

Howl／GrimWard使用独立TerrorUtility入口：投递Terror技能／等级1及普通移除回调，不借用死灵mA1／Possessed筛选和RestoreAi回调；抗性／Attract拒绝列表时仍执行原目标切换及一帧idle。既有构建与有限运行覆盖以运行包记录为准，本次复核修正的构建与有限资源观察见上述记录，未新增相关AI行为运行认证。完整Kill／Killed事件、所有技能生产端、专属飞弹及全局原顺序仍在迁移，不能把公共列表接入当作全部战斗验收。

## 文档分工

| 内容 | 负责页面 |
| --- | --- |
| 通用规范、代码所有权、跨职业计算、整体技能状态 | 本页 |
| 原模块职责、DLL调用及公共提取的结构证据 | [参考设计](../../architecture/REFERENCE_DESIGN.md#6-d2moo的公共层究竟共用什么) |
| 全分类索引、逐技能原函数、专项算法、客户端差异与有限运行证据 | [技能目录](README.md)链接七职业、通用、怪物、同行者、物品及特殊条目的负责页 |
| 学习／选择资格、树／提示投影 | [人物](../../modules/CHARACTER.md)；原图布局见[HUD](../ui/CLASSIC_HUD.md) |
| 充能／装备触发、弹药／耐久及物品使用事务 | [库存](../../modules/INVENTORY.md) |
| 协议／客户端副本、动画／弹体表现 | [联网](../../modules/NETWORK.md)、[攻击与弹体](../combat/ATTACKS.md) |
| 当前包、保存格式与校验边界、后续实施顺序 | [运行包记录](../../development/BUILD_AND_RUN.md#运行包记录)、[存档](../../modules/SAVES.md)、[总计划](../../architecture/MULTIPLAYER.md) |

职业页不重复公共规范或当前包状态；参考设计不再维护第二份职业实现台账。历史证据保留角色条件、源码／包身份、记录路径及未覆盖项，修复过程由Git追溯。

## 冰冷、冻结与毒伤

`gameplay/combat/damage_resolution`拥有抗性／穿透和持续时间的纯计算，`poison`拥有装备毒率支配与来源数时长计算；服务端结算和只读属性显示共用数值函数。`hosting/monster_actions_content::prepareMonsterColdRules`从当前MPQ准备States死亡掩码、MonStats2.deadCol、不可打断状态及难度除数，服务端不读取表。

冷冻命中向`monsters::System::damage`提交未经抗性／难度缩短的原始帧数。目标先按CannotBeFrozen、HalfFreezeDuration、抗性／Cold Pierce处理长度，再按身份选择冻结或冰冷：普通及符合条件的仆从可冻结，首领／精英／佣兵退回冰冷，不可打断状态拒绝冻结；ColdEffect及难度除数仍取原表。冰冷最少一帧，冻结在难度除法后即使归零，也参与该次命中的死亡状态处理。各弹体、武器、反击、环境和怪物来源不重复缩短长度。

死亡在同次命中安装状态后读取States的monstaydeath／bossstaydeath、hide、shatter、udead。`Actor::deathStates`保留原状态，尸体资格和原包投影共同消费；运行中的冷冻计时器清理不能撤销死亡碎冰。普通冰冷、Holy Freeze与UMod35使用原shatter状态；普通冰冷按目标随机流掷20%，且MonStats2.deadCol必须为零。冰冷与Holy Freeze对同一shatter位写入，活动状态结束时按原回调清位。原客户端继续按原状态绘制icebreak／隐藏尸体，无自研服务端专用表现。

通用直线弹体的触地、碰撞与自然到期均进入原SrvHit对应路径；冰尖柱在命中时重求范围／冻结长度，火球执行范围伤害。范围查询保留活动房间限制；不为原过滤器未要求的路径添加视线条件。

毒率使用固定点每帧伤害，命中当帧计入一次伤害，后续由单一毒状态的HPREGEN期限处理。伤害抗性与毒长度抗性分别求值，Poison Pierce分别作用于两个通道；玩家／佣兵毒长度应用难度惩罚和75%上限，普通怪物保留其原抗性上限。更弱毒不改当前毒率／期限；等强或更强只更新毒率／期限，保留原状态的施毒者及击杀归属，不叠加多个毒状态。持续毒对玩家保留1生命，怪物可致死；解毒、死亡和状态移除清理持续毒。

毒状态的下次扣血帧随提交推进，输出背压后结清仍欠的持续帧再到期；刷新不得覆盖尚未提交的区间。装备毒率先计算Poison Mastery，淬毒匕首将技能长度和装备长度合并后除以装备毒来源数，技能不额外增加来源数。`resolveSkill`接收显式毒伤支配，服务端及原服客户端数值投影使用同一入口。Venom等未实现职业的专用毒长度覆盖不由这些公共函数代替。

依据：本地D2MOO `SUnitDmg::{CalcDamage,ApplyResistancesAndAbsorb,ApplyColdState,ApplyFreezeState,ApplyPoisonDamage}`、`MonsterMode::D2GAME_MONSTER_ApplyStatRegen_6FC63440`、`MissMode::{HandleMissileCollision,SrvHit01,SrvHit13}`、`SkillNec::SrvSt16_PoisonDagger`与`SkillPal::AuraCallback_HolyFreeze`。本地原1.13c `D2Game.dll` MD5 `978f6f74cd031bce6ecee602cd5988cc`：RVA `DC4F7–DC519`包含毒率的命中总伤害，`DC780`冰冷、`DDAC0`冻结、`DCAB0`施毒、`DCBCE–DCC07`等强／更强毒刷新但不换owner。原`DC8F8–DC91C`为随机数小于20且MonStats2第19位deadCol为零才置shatter；此处与D2MOO快照的反向重建条件冲突，以1.13c为准。DLL及表导出只作证据，不作为运行依赖或提交内容。

## 职业与证据

| 范围 | 当前自研服务端 | 客户端与验证边界 |
| --- | --- | --- |
| 通用0–5、217–220 | 十项执行入口；Kick是隐藏破桶程序 | 武器／药瓶／物品技能／Unsummon代表路径有限V2；普通桶KK及Left Hand Throw／Swing未覆盖路径V1，见GENERAL；左手弓与相反手弹药另见装备证据 |
| 女巫36–65 | 26项主动、4项被动 | 原Clt表现已接；历史自研、同机TCP及原服代表路径有限V2，见SORCERESS |
| 亚马逊6–35 | 24项主动、6项被动 | 原Clt表现及宠物归属／装备已接；历史代表路径与保存往返有限V2，女武神死亡图等缺口见AMAZON |
| 圣骑士96–125 | 10项战斗技能、20项光环及随状态切换的被动贡献 | 单机30项学习／状态、战斗及保存恢复有限V2，原版D2GS三个代表技能有限V2；依据和组队／PvP／Holy Shield特殊盾图限制见PALADIN |
| 死灵66–95 | 27项主动、3项被动；诅咒、毒／白骨、召唤及铁魔材料生命周期 | 已构建打包，典型单机路径有限V2；未执行联机／原服回归，边界见NECROMANCER |
| 野蛮人126–155 | 20项主动、10项被动；武器精通、双持、移动、呐喊及尸体技能 | 构建打包，代表单机路径及30项保存重载有限V2；跳跃高度曲线与未认证组合见BARBARIAN |
| 刺客251–280 | 28项主动／2项被动，武学／陷阱／影子及持续程序已实现、构建打包 | 三系代表路径、宠物上限／继承／解散及30项保存重入有限通过，精确性与未验矩阵见ASSASSIN |
| 德鲁伊 | 职业执行尚未迁入 | 共用定义／请求／树／提示；原图或文档存在不表示技能执行完成 |
| 怪物／同行者／物品及其他非职业程序 | 第一幕怪物／精英、部分伙伴及物品来源已有入口，其余范围未完整迁入 | 状态与证据分别见[MONSTERS](MONSTERS.md)、[COMPANIONS](COMPANIONS.md)、[ITEM_SKILLS](ITEM_SKILLS.md)、[SPECIAL](SPECIAL.md)，不计入七职业完成数 |

充能和物品授予可以调用已准备的技能程序，不能据角色职业或装备条目推断未实现程序可用。V1为构建，V2仅认证列出的有限路径；全部入口齐备不等于全参数V3，旧单机、其他职业或旧包证据不认证新增路径。

## 公共层与所有权

| 层／入口 | 负责内容 | 不承担的内容 |
| --- | --- | --- |
| `content/skills`、[skill_content](../../../src/hosting/skill_content.cpp) | 当前MPQ定义、资格、动画及类型化规则准备 | 不持有一局施法、伤害或宠物状态 |
| [SkillSpec](../../../src/gameplay/skills/spec.hpp) → [SkillRuleSpec](../../../src/gameplay/skills/rule_spec.hpp) | 前者含图／声音等内容，`rules()`只发布纯规则值和原效果编号 | server不携带资源路径；现有表达式准备不等于通用Calc解释器 |
| `gameplay/skills`、`gameplay/combat` | 显式输入的等级、曲线、时序、几何、随机及小状态计算 | 不访问MPQ、设备、GPU、世界实体、会话或隐式随机 |
| `server/skills`、`missiles`、`combat`、`effects` | 动作／释放、权威弹体、候选／命中、反应／周期；通过transactions提交成本与事实 | 不由hosting会话或客户端复制执行 |
| `hosting/companion_content`、`server/companions`、`monsters` | 内容准备、伙伴生命周期／AI、单位生命／位置／装备分别持有 | 纯召唤求值不创建装备；宠物装备不归本人库存 |
| `client/remote_combat`、`character_projection`、`presentation` | 原请求与副本、已知值提示、原Clt程序、动画／音效 | 不判权威命中、不扣资源、不安装真实状态、不重掷伤害 |

公共函数不要求两端都调用。伤害掷值即使是纯计算，目前也只由服务端调用；共享的是算法，不是随机流、世界候选或实体。D2Common含可变状态基础接口，D2Game含权威执行；客户端导入D2Game或本机Host运行D2Game均不能证明完整技能两端共用，证据见参考设计。

## 技能迁入规范

### 程序分派与扩展

技能数量不等于程序数量。参考本地D2MOO `D2Game/src/SKILLS/Skills.cpp`的`gpSkillSrvStartFnTable_6FD408B0`与`gpSkillSrvDoFnTable_6FD40A20`：多个技能共用开始／执行程序；本项目沿当前MPQ准备类型化规则，不复制旧版程序编号或将每个技能做成独立运行时类。

| 入口 | 分派职责 |
| --- | --- |
| [casting.cpp](../../../src/server/systems/skills/casting.cpp) | 开始资格、目标绑定、动作／引导、延迟释放及背压重试；activationProgram统一将已准备规则分类，普通释放使用成员函数策略表 |
| [activation.cpp](../../../src/server/systems/skills/activation.cpp) | 具名效果处理器，通过窄领域端口执行物品、召唤、状态、旅行、直接命中和弹体；不持有另一份施法时钟 |
| [weapon.cpp](../../../src/server/systems/skills/weapon.cpp) | 原武器资格、成本、多段／回滚时序及释放；运行时weapon优先于普通效果分类 |
| [item_triggers.cpp](../../../src/server/systems/effects/item_triggers.cpp) | 装备触发队列、概率与目标解析、效果已执行／通知待发送状态；itemTrigger复用程序分类，但保留零消耗、死亡与目标限制 |
| [client_missile_program.hpp](../../../src/presentation/world/client_missile_program.hpp) | 已支持CltDo能力目录，集中静止、创建来源配对和子弹体图形资格；程序号来自MPQ，不是技能ID |
| [client_missile_view.cpp](../../../src/presentation/world/client_missile_view.cpp) | 按CltDo单次分派逐帧专用行为，之后共用运动、碰撞、子弹体队列和表现时钟；不运行权威命中 |

服务端策略表用具名枚举绑定成员函数，编译期检查重复、遗漏和空入口；新增枚举必须登记处理器。未支持行为不再默认转交弹体：普通施放在创建动作前返回NotImplemented，释放也有显式Unsupported入口。弹体行为清单须与missiles/launch中已实现程序同时维护；增加字段或内容定义不自动授予执行支持。客户端能力表检查程序号唯一且有序，未知CltDo不创建弹体。

普通释放与装备触发不可机械合并：Enchant接收者、Telekinesis物件扣费与背压、死亡触发、武器多段以及引导具有不同上下文。共享程序分类与确定性目标选择，不共享两端世界状态，不重新扣费、掷值或执行已接受的效果。MPQ解析器中核实公式／资源的条件不是热路径分派，保留明确校验；本轮不引入通用脚本解释器、动态注册或每技能虚类。

新增技能若复用既有程序，只准备当前MPQ规则与资格；新增执行程序需登记策略并实现领域操作，新增CltDo需登记能力并实现其逐帧／接触／到期行为。数据、执行与表现支持分别维护，不能通过目录注册宣称新职业完成。本次已随佣兵批构建、打包，四类佣兵代表技能取得有限单机证据；完整程序／来源组合与重构边界未认证；配置与产物身份见[运行包记录](../../development/BUILD_AND_RUN.md#运行包记录)，以前的有限V2不认证此次重构。

1. 先读master已实现的定义／执行与当前公共函数，复用可用规则；耦合GameSession的部分只迁规则和必要状态。
2. 依据当前MPQ登记Skills的SrvSt／SrvDo、Missiles的Do／Hit／Dmg、动作事件和状态。用本地D2MOO定位职责与缺漏；1.10f重建、1.13c DLL和当前表的差异分别记录，参考附带参数不覆盖MPQ。
3. 提取真正相同的纯计算，明确基础／有效等级、来源、取整、帧相位、种子归属和实体／区域代次；返回纯值、下一小状态或动作描述，两端分别创建权威／视觉对象。Clt／Srv参数、触发条件和随机顺序有原版差异时保留，不能为代码一致抹平。
4. 服务端区分开始资格和释放复验，资源／数量／耐久／充能与事实按事务提交。背压保留首次准备结果，不重复扣费、掷值或命中；异步内容准备绑定请求并只预留一次独立种子。未准备或未知规则明确拒绝。
5. 客户端只改有原版依据的公共提取和修复，每处记录MPQ／原包／原函数依据。禁止为兼容自研宿主另写客户端玩法分支、私有确认消息或推算远端隐藏值。
6. 更新职业台账及明确缺口；保存语义变化同步SAVES与校验边界。构建／打包／运行仅按有效授权，不编写测试脚本、用例或专用程序；源码核对、构建、有限运行和原版验收分别记录。

## 跨职业公共计算

`server/systems/skills/system.hpp`不暴露私有施法／怪物释放结构，它们位于仅技能实现使用的`runtime.hpp`；求值、支配、充能源逻辑移入`evaluation.cpp`，公共头只声明函数。普通技能实现改动限定在对应编译单元，私有队列布局改动限定在技能模块；共享规则结构变化仍会影响真正使用这些类型的调用者。参与者关系通过轻量声明读取，不包含PlayerStore／monsters系统头。公共战斗规则、提交阶段与运行限制见[内核](../../modules/SERVER_SYSTEMS.md#战斗关系与提交)。当前Release包已包含，未实测增量编译耗时。

| 计算入口 | 必须保留的语义 |
| --- | --- |
| `resolve`、`rank_sources`、`rank_bonus`、`passive` | 基础等级用于协同，来源的有效等级用于施放；安装属性贡献仍归服务端 |
| `damage_curve` | 原法力定点曲线与MinMana消耗分开；技能元素伤害先HitShift再协同，独立弹体先协同再HitShift，随后应用支配；8.8最小值≤256且首级增量为0时跳过协同 |
| `cast_timing`、`amazon_sequence`、`weapon_volley` | 原AnimData事件、SC／seq6／seq12及武器序列／回滚；CAST按整数求min(100+120×FCR/(120+FCR),175)，不叠加OtherAnimationRate；Inferno seq6固定速率 |
| `projectile_path`、`amazon_missile`、combat几何 | 速度、格点、环射／扇形、remaining调度、引导／连锁／分裂等纯值；各端持有候选、时钟、碰撞及创建队列 |
| `weapon_damage`、`poison`、`avoidance` | 武器六通道／转换、显式随机、毒替换及近远程／移动回避；不因纯函数可用就授权客户端结算 |

#group 9特殊目标保留独立实例顺序及清理标记；生成身份和当前随从归属分开，单位候选一起复验。原始状态／移除回调、护盾池、被动和资源回调共用准备入口，玩法不读取MPQ、不持有平台依赖，不将控制、目标节点或期限写入D2S。源码与包身份及有限运行证据分别由负责技能页和[构建指南](../../development/BUILD_AND_RUN.md#运行包记录)维护。

## 公共函数抽取

2026-10-11对照本地D2MOO、当前MPQ准备代码及既有原1.13c静态证据，收敛下列重复计算。函数只接收显式规则与输入，不持有世界、MPQ或随机流；客户端和服务端通过各自入口调用。

| 公共入口 | 已接调用与保留条件 |
| --- | --- |
| `rank_bonus::skillRankBonus`、`skillDiminishingBonus`、`skillDiminishingRatioBonus` | 亚马逊被动、圣骑士光环／盾／转换、死灵诅咒／石魔、野蛮人被动／Frenzy／跳跃／尸体搜索／Berserk共用曲线。固定原1.13c D2Common RVA4DC30／ordinal10115确认先截断110×等级/(等级+6)，正等级包装现共用nativeSkillDiminishing；公式的dm78／飞弹等级零分支直接调用原始算子。两条旧函数名只保留调用兼容，不再维护不同数值规则；此算子迁移已纳入前序Windows Release构建，完整技能流程未认证 |
| `skillRank`、`skillRankSum`、`skillSynergyBonus` | 女巫甲胄／伤害、圣骑士光环／武器、死灵白骨装甲、野蛮人呐喊／武器等统一查询、协同等级求和与加权求和；调用者仍区分基础等级与有效等级 |
| `damage_curve::skillDurationBonus` | 技能元素长度与骷髅法师弹体长度共用8／16级分段。技能长度入口保留1–255资格，法师内部合成等级不强加这个上限；技能／弹体伤害的HitShift和协同顺序不合并 |
| `evaluateBarbarianPassive`、`evaluateNecromancerPassive` | 六武器精通及四项野蛮人被动的提示／实际贡献，骷髅支配／石魔支配／召唤抵抗的提示／召唤求值读取同一纯结果；死灵提示不再扫描Skills表。参数仍由当前MPQ准备，属性安装与宠物创建由服务端完成 |
| `attack_timing::animationFrameTicks`、`weaponSequenceTick`、`weaponSequenceHitTicks` | 施法序列、Jab／Impale、双挥／Frenzy／双投的服务端命中与客户端动画共用8.8时钟及事件遍历。施法释放允许第0帧，武器事件仍至少第1 tick；序列内容、手位、动作及Leap Attack阶段保持独立 |
| `targeting::withinSkillArea` | Static Field、Telekinesis、尸爆／诅咒，连锁／范围／毒云弹体，人物／佣兵／精英光环共用几何。接口显式区分坐标截断、向下取整和连续距离；半径、房间活动、阵营、候选顺序及碰撞仍由调用者提供 |
| `targeting::skillAiControlAllowed` | Taunt与死灵AI诅咒共用switchAi、非boss、Normal／Minion资格；恐惧等级限制、附身者排除和吸引特殊覆盖仍在原调用点 |
| `content/skills/table_values` | 15个内容加载编译单元共用数字、身份引用及公式文本读取。缺列、空值、坏数字、允许空身份及引号处理按显式策略保留原拒绝／默认规则；它是内容辅助层，不进入玩法热路径，也不是通用Calc解释器 |
| `units/resources::resourceOnMaximumChange`、`conversionResources`、`conversionRestoredLife` | 最大资源回调保留先除后乘、int64截断后signed32窄化再限幅；转换先按整点百分比计算、最后左移8位，资源写入及回调由服务端候选执行。原圣骑士恢复上限不左移，刺客的另一回调左移，不合并两者；具体原1.13c证据与尚未完成的事件路径见[圣骑士](PALADIN.md#生命周期与边界)。这些资源算子已纳入前序Windows Release构建，完整转换流程未认证 |
| `skills/native_damage`、`assassin_progressive`、`combat/weapon_block` | 原物理→元素掷值／写入、蓄气／吸取／鞋子踢伤／释放步骤与最多32参数层格挡查询；显式来源、接收者和种子，不持有世界或第二套层数。调用阶段、武学及普通攻击释放和实际双爪格挡的有限证据见[刺客](ASSASSIN.md) |

核对范围包含七职业和通用／怪物／佣兵／宠物／物品／其他来源。已实现职业直接接入上述公共值；佣兵和物品来源沿既有resolve／aura／武器／弹体入口复用，怪物与精英复用几何和既有弹体计算。刺客已接真实职业执行与非玩家施法，复用公共动作／属性／状态／接收；德鲁伊仍只共用定义、来源、资格与客户端投影，职业执行器尚未迁入。其他未支持程序仍明确拒绝，不为未来技能添加虚拟执行器、通用世界回调上下文或未核实的Calc解释器。

公共抽取本身不改变规则语义或D2S v96格式；与第一幕怪物补缺合并后的历史Windows Release客户端及独立服务端已构建／打包。最终包完成第一幕逐身份、Telekinesis击退及隔离角色保存重入的有限V2，没有新增测试脚本、用例或专用程序；未专项复验抽取后的全部职业、等级、来源及多人组合，各职业此前的有限V2仍只认证对应历史构建。具体原函数证据见[公共层](../../architecture/REFERENCE_DESIGN.md#6-d2moo的公共层究竟共用什么)，该批产物身份与验证范围见[第一幕](../monsters/ACT1.md#有限运行证据)，当前包见[运行包记录](../../development/BUILD_AND_RUN.md#运行包记录)。

具体调用点和原版例外在职业逐项台账；数值／模块证据见参考设计的[女巫核对](../../architecture/REFERENCE_DESIGN.md#女巫公共层的原版核对)及[亚马逊核对](../../architecture/REFERENCE_DESIGN.md#亚马逊公共层的原版核对)。

### 诅咒、祭坛与战吼的公共生命周期

curse_resolve::curseStateDuration保留原期限减去单独取整百分比、负抗性及零期限规则，死灵、亚马逊状态和祭坛共用。rank_sources::nativeShrineSkillBonus读取准备的shrine_skill状态身份，玩家逐写被动查询与最终等级查询使用同一状态加成。CombatEffectSet的nativeExpiration区分普通诅咒的非零期限与战吼的零期限移除事件；refreshPassiveSkills和耐力恢复序号供原回调边界的候选观察消费，属于运行态，不写D2S。三种友方战吼和限时祭坛开始按逐写来源／资源轨迹提交，源码及未覆盖项由BARBARIAN、NECROMANCER和OBJECTS维护。本轮伤害事件接入的构建／运行状态见页首；不能将共用集合视作完整全局单位事件调度已完成。诅咒的RestoreAi和ClearTargets现持独立回调序号，单位候选消费普通AI恢复及目标阵营恢复，嵌套来源准备保留已准备控制；AIRESET由怪物持有独立事件，不随列表缩短／移除被替换。当前局部队列保留重复和连续事件，但尚未并入原全局单位队列，不能认证与AITHINK／REMOVESTATE的完整同帧顺序。 MonsterResourceTrace现从修改前状态基线保留回调序号，在撤出／回调观察时准备控制与原始阵营变化；后续属性写入和嵌套光环读取当时的候选。内视／慢速箭也已接该原始来源观察，重施的状态投影读取复用列表，不另覆盖一份最终属性。 原房间单位查询现抽出NativeAuraTargetQuery，半径使用signed32，OwnerRange保留光环／SrvDo006的零过滤器默认值及拥有者排除，TargetRange保留诅咒的目标中心、零过滤器及零范围区别。AreaStore::roomAtPosition只查原当前房间再按MPQ相邻房间顺序查目标点，不全幕扫描。光环、内视／慢速箭及群体诅咒读取同一房间链、整数距离和过滤程序；玩家PvP与非玩家接收者共用，跨区域事实按接收区域发布。仍未开放的飞弹／物件／物品、近战／JUSTHIT筛选明确要求对应原单位查找程序，不用普通单位近似替代。

### 周期技能帧

公共`aura_resolve::skillPeriodicFrame`按固定1.13c实现`max(5, PerDelay) * ((frame + max(5, PerDelay) - 1) / max(5, PerDelay)) + 1`，括号中的除法先执行，所有加／乘使用原signed32窄化。D2Game.dll同一固定快照RVA9E970–9E9A4确认PerDelay公式调用、下限5、IDIV→IMUL→INC；D2MOO SKILL_ComputePeriodicRate转录中的运算分组不能直接沿用。`skillPeriodicTick`另按原事件插入的signed到期比较把已到期帧推到下一帧，证据RVA8EAB8–8EAC2。-1是持续事件哨兵，目前定时光环适配明确拒绝该哨兵，完整持续事件待统一调度实现。

已接玩家光环首次非immediate调度和每次重新排期，从当前人物公共公式输入采样原PerDelay；佣兵／火魔现有光环循环的重新排期共用帧算子，其完整动态公式／AI调用仍待迁移。例：周期50时，当前帧32或50的下一事件均为51，当前帧51则为101；不能以“当前帧加周期”代替。即时程序、状态寿命、周期事件与普通动作帧仍是不同机制。此周期帧源码已随2026-10-14冰封球验证构建入包；该运行不覆盖光环及同帧全流程。

周期事件执行现开始区分原帧计算与状态寿命：skillPeriodicDuration保留`skillPeriodicFrame(frame, PerDelay) - frame + 1`的signed32运算，不以事件插入后钳制的帧重算。固定1.13c D2Game SrvDo表RVA1074A8第65／66项分别为9A8D0／9AF50；9A955–9A988确认PerDelay采样、下限5、IDIV→IMUL→减当前帧→加2，9AFE5–9AFFF确认调用周期函数后减当前帧再加1。普通正寿命进入现有效果期限；持续哨兵及非正回绕寿命仍要求完整单位事件调度，目前明确拒绝。准备层此次补入Skills.Delay／PerDelay公式，避免公共求值因缺字段返回原-1偏移的0。

普通右键和已准备的装备周期光环开始共用advanceAuraCycle的目标／作用／脉冲游标。状态寿命在本次程序开始时采样并保留；脉冲排队成功后保留pulseCommitted，事件重排背压重试不再重复排入伤害。光环阶段未完成时不执行后面的恢复／再生阶段，减少整逻辑帧重试的重复工作；其余恢复及跨域阶段的完整游标仍待统一。

公共SkillFormulaSnapshot保留一次同步属性回调的来源根、属性／手位值、原始资源、基础／有效／引用技能等级及查询限制。捕获复制根及值，贡献叶为不可变共享来源；不保存借用的RuleRandom闭包，执行方必须在同一调用边界提供单位随机候选。input只允许仍存活的左值快照，返回借用输入，不把周期光环改为读取旧注册快照。PlayerSkillRefresh的item_aura非零激活和零值撤销均保留该时刻输入，区别于最后装备快照。公共itemAuraCallbackInstructions按固定1.13c的激活顺序准备CancelTimers→ScheduleTimer→ExecuteImmediate，撤销为RemoveState→CancelTimers；refreshItemAuraTimers仅消费其中的计时器步骤。executeItemAuraCallback为未发布候选提供同步执行入口：先验证所需状态撤出／immediate钩子，再沿指令顺序修改计时器并调用钩子；指令和公式快照拥有独立生命周期，嵌套属性回调不能覆盖外层输入。钩子只能准备候选，不发布权威状态。attributes::calculate另提供AuraMutation／AuraSourceObserver同步入口；绑定方在每个状态来源撤出、投递和写入之后报告候选来源与状态。计算器持有本次临时来源／状态，先更新来源树，再按StatKey顺序传播原始差值，嵌套技能回调、后续装备资格和资源上限采样读取当前候选；变化后的临时投影只放入Totals.auraTransient，不自行发布。PlayerSkillRefresh先记外层回调，再进入钩子，嵌套回调顺序及外层公式快照保留。普通周期消费者已改用共享applyNativeAuraState列表分配／写入入口，供即时候选复用。CombatEffectSet现为值语义共享存储，修改时分离；来源观察中保留的旧集合不会被后续写入修改。新句柄计数先推进再通知投递观察，快照不保留已使用的下一个句柄；逐写观察之后继续修改也会再次分离，不持有跨观察的可变列表引用。TransientAttributes.effects携带运行时完整集合，状态／来源投影及生命周期候选指向同一变化；不进入D2S。AuraSourceObservation可另外报告直接ResourceValues，当前资源立即影响后续公式，Totals.auraResources只保存最终候选，不重放旧账本。

人物事务已绑定零值item_aura的撤出步骤：先按当前AuraState撤出集合，逐来源观察传播属性／上限，再取消同GUID计时器。计时器在同步回调中更新，包括嵌套回调；事务不在末尾重放整张账本。单人、双库存及人物成组提交在输出批次接受后一起发布集合；新Recovery节点先准备，已存在节点只交换共享存储。效果系统后续候选发布沿人物已提交的完整集合收尾，避免旧副本把撤出的状态放回。CombatEffectSet开始区分原始状态位与列表：setNativeState／stateIds管理显式位，hasState先读显式位，尚未迁移的生产端才按活动列表推导。已接原始光环及普通数值诅咒在新列表分配时置位，同级复用保留原回调；到期不自动改变显式位。撤出先通知属性来源观察，再执行该列表的状态清理与被动刷新观察，符合D2Common ExpireStatList先传播差值、再调用fpStatRemove的顺序。刷新序号属于运行时集合，属性候选在回调边界刷新全部被动，不把请求延至最终装备求值。物品DeactivateAura先关位、再按状态查找并撤出列表，不能因位已关闭就跳过存在的列表。禁止回蓝现只设置独立位，不分配空限时列表；拥有者BasicAura移除回调再清除此位。固定1.13c D2Game BasicAura回调入口9A1A0在9A220安装98450；98450–98481确认清自身位、检查／清0x55禁止回蓝、随后调用被动／刷新入口。HolyFreeze拥有者的专用回调不沿用BasicAura的禁止回蓝／被动刷新操作。AI诅咒、完整死亡／专用移除回调、单位事件注销、全部状态生产端及原事件期限仍待迁移，不能据这一混合适配认定完整States／StatList复刻。非零immediate现开始在原属性回调中执行拥有者SrvDo065，仍先取消／排期再执行；不设置used／左右技能，也不等待下一周期帧。当前MPQ的10项immediate光环均为65、允许城镇、基础／逐级法力为零、无UseManaOnDo／数量／飞弹／延迟操作。拥有者从该次SkillFormulaSnapshot计算原Aura／Passive值，复用applyNativeAuraState，元数据后按PassiveState撤出独立缓存被动列表；后续装备资格读取变化后的来源。直接生命／法力／耐力写入借AuraSourceReader读取当时资源和最大值，再报告ResourceValues。原强制入口允许绕过正等级／拥有技能检查，原始状态的signed等级不沿用普通效果的非负校验；时长仍用原周期表达式。固定1.13c D2Game ADBD5–ADBE4传入1／1／0调用A19A0，A1A7E–A1AC3确认强制参数绕过used／最高技能正等级检查。Might的MinMana=1不意味着费用：D2MOO SkillMana_Consume先在基础／逐级法力都为零时返回，不进入MinMana分支；同名owned used-skill现开始按该所属物品／该槽等级读取原充能层，原Handler成功Do后同步消费；没有物品或可用充能时不撤回Do，零充能的属性回调按原规则清理技能实例。充能层的本件缓存、来源、物品修订与装备规则在同一候选里更新，不调用另一次人物事务。其他强制执行器／飞弹／延迟、PLR和非资源直接属性仍要求完整单位候选。

跨玩家接收者现开始纳入同一准备／提交：auraCandidateRecipients使用整数格、signed32平方距离、当前相邻房间与filter规则，当前10项immediate的0x12003跳过城镇接收房间，但拥有者单独执行。withinNativeAuraArea共用PATH_ComputeSquaredDistance的减／乘／加及半径比较，未使用浮点距离；filter的0x200射线使用整数坐标及原掩码4。接收者初始Aura操作在其同步属性候选中执行，与拥有者共用写入入口，保留来源GUID、同级复用、元数据和缓存被动撤出；同一外层回调多次触及接收者时，后一次借用前一次的临时候选，不重读旧权威属性。

单人提交持有独立auraPlayers计划，提交前复验全部接收者身份、两种修订、区域与生成号，再一次接受全部输出，随后一起发布人物／效果快照及used-skill清理；输出背压不提前发布拥有者或接收者。相同属性的状态事实不重复发送，但列表寿命仍更新。怪物／佣兵接收者现开始进入AuraUnitPublication：MonStats.noaura与MonStats2.isAtt单独从当前MPQ准备，NPC／NoAura单位不进入此BasicAura接收路径，不以mA1或curseable替代。每个单位保存修改时分离的列表候选、基础来源与生命候选；同一外层程序多次触及该单位时累积前一次结果。原始属性逐项写入及元数据共用applyNativeAuraState，随后按PassiveState撤出缓存贡献；破免读取原基础抗性，佣兵豁免，速度／attackrate按当前ColdEffect下限，直接属性读取候选当前值及MaxStat。提交前同时复验单位修订、区域、效果集合快照身份和生命／速度所需修订容量；缺失效果节点在准备阶段预留。单位状态事实与玩家输出一起接受后，再一起发布列表、生命／基础来源和速度；背压不会提前改变单位。嵌套光环现开始复用活跃接收者的列表与来源观察／读取入口；prepareGroup以一个候选图按主事务顺序准备，接收者修改可回到尚在计算的拥有者。交易、双目标增益、群体诅咒及单位增益／治疗共同复验和发布，既有单位来源与生命候选可作为该图的初始输入，旧效果副本不再覆盖新光环。入场先保留角色保存资源，再以无旧装备缓存执行当前逻辑帧即时回调；失败不进入游戏。普通／物品／佣兵／火魔／怪物的20项圣骑士光环开始共用原始公式，目标沿MPQ相邻房间次序查询，包含已准备跨区域房间，并按接收区域提交状态／脉冲。AreaStore现持有独立房间单位链，按D2MOO UNITROOM头部插入／原房间摘除更新；玩家、NPC、怪物及召唤物的入场、移动、跨房间、传送、移除接入该链。新单位节点在创建准备阶段预留，移动和发布不另分配节点；光环按MPQ相邻房间次序和实时单位链查找，玩家／怪物不再分成两批遍历。尚未进行同版运行认证，完整原创建／休眠顺序仍由后续生命周期阶段核对。中间来源采样开始保留被动缓存，元数据写完后撤出PassiveState列表，原移除回调触发缓存刷新；怪物／佣兵补入对应刷新。普通／BasicAura／Holy Freeze移除策略和死亡保留位、限时列表分开，存活拥有者的Holy Freeze回调单独清SHATTER。非玩家PLR毒期限开始作为LifeChange状态候选随生命／列表复验和发布；普通、物品、佣兵／火魔及怪物光环开始按拥有者状态→伤害抽样→逐目标状态／伤害／Holy Freeze SHATTER推进，每目标保留状态、伤害及后处理游标。伤害范围／目标搜索在拥有者回调后采样，城镇66／81分支提前结束。伤害抽样与Redemption使用单位当前种子，SHATTER在伤害后使用接收者种子；玩家种子由入场父种子按SUNIT_InitSeed公共算子准备，不写D2S。毒期限与SHATTER有复验后的发布边界。玩家其余技能／攻击的随机入口尚未统一，因此这不认证全局原版随机序列。Simulation保留Effects／Combat阶段，effects另持有阶段及人物资源／技能完成标记，背压不重放已提交的再生、周期伤害或后处理。非玩家物品计时器和按staydeath清理独立状态位已接共享候选；原生PeriodicStats驱动属于当前正在收尾增量，完整死亡顺序、全部单位事件及原全队列优先级仍待收尾。原始列表的分配／复用及属性查询开始按仍投递的列表处理，不因期限已到就自行忽略／清掉无关原始列表；实际撤出由移除阶段完成。完整接收者查找、原事件先后、GFX／被动列表生命周期仍待闭合，不能据拥有者分支认定完整immediate已实现。

公共evaluateNativeAura读取当前MPQ的AuraStatCalc／PassiveCalc、AuraRangeCalc、PerDelay及公共原伤害／法力参数，不再为这两类玩家周期消费者另列类型化公式。SrvDo065用Aura六槽同时准备拥有者与目标，遇缺槽停止；拥有者法力严格大于费用且PassiveState≤0时，才准备额外Passive五槽。固定1.13c D2Game RVA9AA63–9AA75确认这两个额外门；狂热的damagepercent按原SetStat覆盖半额。SrvDo066／081的拥有者用Passive五槽，非玩家可越过法力门，目标六槽仍要求当前raw法力≥费用；082只准备拥有者Passive五槽，尸体过程独立。数值保留各Stat原单位，不能将ValShift=0的元素附加伤害再当8.8移动。拥有者直接Passive写入与BasicAura回调分开：后者处理生命／有害时长，逐项写入非零属性、attackrate紧接other_animrate、末尾写技能／等级元数据，随后才写拥有者额外Passive。同等级状态复用保留来源及未写值，覆盖重复键。服务端接收者调整读取怪物基础抗性作破免判定，玩家／佣兵豁免；速度与attackrate按原ColdEffect下限处理，显式other_animrate槽独立。原始周期消费者现保留Direct及有害时长操作在原槽序中的位置，拥有者额外Passive仍走列表SetStat。ItemStatCost.Direct／MaxStat由当前MPQ链接到StatRule；公共nativeAuraDirectStatValue按signed32加法及最大值比较生成原值，不添加零下限。玩家的生命／法力／耐力沿资源候选写入；具备原始来源的非玩家用同一生命或基础属性候选，数值不改变时不计入生效。列表非零指令计入生效，作用谓词仅随该接收者提交成功积累；当前周期用途等价于原field_40>0，不保存原完整计数。毒／诅咒期限改用公共nativePercentage，现有不可治愈诅咒仍计入生效但不缩短；零期限直接更新现有句柄，不重投整张毒属性表。持续／负回绕期限仍明确要求完整单位调度。

周期费用在接收者及脉冲准备后独立提交，并保留manaCommitted，末尾重排背压不重复扣费。没有生效时撤销禁止回蓝，有生效时先设置再调用费用分支；法力不足不撤销已经执行的作用。Blood Mana从当前MPQ的blood_mana及其来源技能Param5准备：生命不足先撤出列表再写256 raw生命；否则代扣生命，低于Param5<<8时撤出。固定1.13c D2Game RVA9A326–9A36B确认直接值相加、MaxStat上限、值改变才计数，9A30A–9A31F确认attackrate伴随写入；9A5CC–9A5FE确认按计数设置／清除禁止回蓝再调用9ED20，9ED20–9ED6C确认Blood Mana优先及普通不足法力返回，BE20–BEDC确认生命代扣、Param5阈值及不足生命写256。独立状态位／列表撤出及被动刷新边界已接前序持续效果模块，这些光环费用与来源程序也已纳入构建；完整直接属性回调（包括玩家单位非资源基础值）、随机流、目标排序及同帧事件／伤害完成顺序仍待闭合。当前原生周期事件驱动已通过本轮构建与代表路径观察；前序有限观察不认证全部费用／反噬组合。

### 原时段属性输入

服务端幕环境时钟开始由world推进，AreaStore持唯一幕时钟，公共`advanceActEnvironment`／`actStatTime`只接受纯值；不取系统时间、不读取MPQ或使用Win32。幕在人物入场、进入请求或房间激活时建立，预准备地图不预先推进未加载幕。普通初始化为noon／rate128；黑暗太阳开始使用rate4、eclipse的sunrise初值，结束恢复noon／rate128，保留原nPrev。任务恢复、开始与祭坛完成已投递相同入口。

固定原1.13c D2Common.dll（MD5 `ee1238806ef6d6d9801d12a09d128fe1`）普通／第四幕／日蚀周期表RVA9B160／9B1A8／9B1F0，UpdateTicks在RVA37570，UpdateCycleIndex在37660。375A7第三幕夜间额外加8，与D2MOO转录加9不同；375D4–375F9未日蚀时选普通表，日蚀时才按act选择另外两表；周期重置仍按普通／日蚀表。公共算子保留这些原1.13c分支、严格大于下一周期阈值、超整圈归零，以及与上次通知度数差严格大于16的物品刷新边界，不用均匀日长或修正后的循环替代。

人物入场／事务、玩家资源轨迹、普通怪物与当前原始召唤初始化开始传入原幕时段。`StatList.sampledActTime`和`Totals.time`是该次属性计算的只读输入快照，查询缓存不推进幕时钟；新候选仍可显式传入当前输入。op6／7继续使用公共`statByTimeAdjustment`的原打包、15度取整和整数比例，手位视图、属性／被动刷新及来源重绑定保留该上下文。

幕通知开始按原act0→4逐项推进，某幕通知后的全部玩家刷新完成前不推进下一幕；attributes保存本次通知身份及最新入场者优先的玩家游标，通过人物事务重算原属性／手位和资源回调。当前幕的游戏中接收者在同一私有提交内追加不可变EnvironmentFact；hosting编码原53的cycle／ticks／eclipse。固定原1.13c D2Game RVA2CA40–2CA6B调用幕更新及原环境查询，2CADB–2CAE6对每位玩家更新物品并RefreshInventory，2CAEB–2CB19确认游戏中／同幕过滤及10字节53。循环无需等待当前玩家物品发生显式修改。

Simulation开始持有环境、已运行领域、交易与尾部阶段游标；环境刷新或交易输出背压时，GameInstance保留原逻辑帧，重试不再重放已处理命令或已完成领域阶段，等待中的新命令留到下一帧。入场和跨幕开始发送真实幕快照。黑暗太阳原ACT2Q3的固定周期5／2任务包独立保留，与连续幕时钟通知分别生产，不将其当作普通环境快照。幕时钟及通知迁移已纳入前序Windows Release构建，人物装备刷新也已接逐物品资格／投递／撤出与中间回调；完整时段／装备组合未运行认证，佣兵等其余生产端、跨幕属性迁移、其余阶段的原事件优先级及完整帧游标仍待闭合，不能宣称按时段装备全过程已完成。

## 定义、来源与客户端状态

选择器与技能树的 MPQ 描述程序、公式及截图证据由[技能描述](DESCRIPTIONS.md)维护，与执行和学习资格分开。

Skills／SkillDesc／CharStats、TBL及原树／图标提供ID、前置、手位／城镇资格与描述；显示节点不表示服务端已执行。`projectCharacterDisplay`使用原包等级和已知值，缺必要武器、协同、支配或描述程序时保持未知。普通、固有、物品授予和充能来源不能混用，源失效不退回同ID普通技能；充能及ItemEffect／ItemCltEffect限制由[物品技能](ITEM_SKILLS.md)维护。

本人已收到属性或完整解码的装备／状态可以用于FCR／IAS／FHR／FBR／Pierce显示，本人佣兵完整投影也参与计时；远端缺隐藏值时仅使用基础显示速率，不据画面推断权威断点。学习、选择、数量与热键的原包及等待条件统一见人物模块，保存字段和运行态排除项统一见SAVES。

## 动画帧时钟与恢复

`gameplay/combat/attack_timing`统一8.8固定点速度及25Hz整数帧。MPQ AnimData／COF提供帧数、速度和动作事件，宿主将纯值交给服务端。装备有效速率为整数`120×bonus/(120+bonus)`，先截断再合并。IAS合并技能AttackRate并扣武器速度，最终百分比限制15–175；双持基础武器速度在装备计算中取平均。FCR基础100、上限175，不叠加OtherAnimationRate；FHR／FBR基础50，圣盾的FBR基础100，按最终整数速度计算，不能用帧数的连续比例代替。

玩家普通动作时长为`max(1,ceil((frames-startFrame)×256/speed)-1)`，玩家序列时长为`ceil(frames×256/speed)`；事件按同一时钟释放，攻击至少第1 tick。Amazon／Sorceress普通攻击的起始帧奖励不传入重组序列。Jab／Impale、双挥／双投使用各自帧表和事件；Inferno／Leap固定速度、Leap Attack阶段及Strafe／Fend／Zeal回滚保持专用路径。客户端采样也量化到25Hz，序列不继承单段COF的起始帧或结束时间。

玩家`effects::Recovery::actionUntil`阻止恢复期间的新技能、移动和旅行，恢复成功后中断可中断动作并清路线。GH沿原伤害／生命／完整HitClass概率判断，元素／暴击高位不剥除；再次满足受击资格会重启GH。眩晕先于普通阈值／概率判断，玩家眩晕上限250tick、佣兵13tick；周期毒伤不播放GH。非可中断动作不被GH／BL／KB打断。格挡成功与播放BL分开，`lastBlockTick`及`FBR/8+15`间隔抑制连续BL，格挡判定仍有效。KB复用GH图形，玩家固定速度213、怪物取原WalkAnimSpeed，不使用FHR；击退成功也安装动作锁，怪物Smite的击退资格取原ResultFlags。伤害、恢复状态与随机数只在事务／输出成功后安装，容量失败保留原状态。死亡清恢复锁；这些时钟均不保存到D2S，不改变v96格式。原0x0D action19仍只校正生命／位置，GH／BL另发原action6／18，KB沿原action20。

怪物ENDANIM使用`monsterAnimationTicks`的`max(2,ceil((frames-startFrame)×256/speed))`，与玩家普通动作的减一规则分开；动画结束后的NU思考等待另按MonStats三难度aidel安排，依据与边界见[怪物动作时钟](../monsters/COMMON.md#动作死亡与奖励)。怪物与佣兵恢复使用MPQ原帧／速度及当前FHR／FBR，伙伴成功格挡也发布BL；近战、技能弹体及环境弹体统一调用monsters::block，容量失败不提交接触随机或推进目标。普通怪物、死灵召唤物、女武神和佣兵动作保留原整数帧，按当前装备／状态计算事件；Kick沿当前武器IAS／WSM。第一、二幕怪物MonSeq按行次序计时，不将显示帧、倒放或A1／A2切换换算成时间；SQ初始速度256，只有原Skills.UseAttackRate允许时才按IAS更新，不套玩家SQ的-30。普通SC走FCR，其他特殊模式按原UseAttackRate选择AttackRate或OtherAnimationRate，非攻击SQ保持固定时钟。佣兵Jab保留多事件，Inferno保持引导时钟。`server-snapshot.animationTiming`只读提供当前武器类下的基础动作速度、时长、事件tick及恢复锁，不是复制／存档来源，也不代表特殊序列的最终调度。

依据为当前MPQ、本地D2MOO `Units.cpp`／`SequenceTbls.cpp`速率与序列函数、`SUnitDmg.cpp`恢复／眩晕分支。本地1.13c `D2Common.dll`的`0x6FD7EFC0`确认玩家GH为mode4、怪物GH为mode3；`0x6FD80BD0`确认怪物按模式表继续查询UseAttackRate。重建代码分别误写BLOCK和遗漏monster分支break，不采用这些笔误；反汇编证据在`artifacts/animation-timing/native-*.txt`。德鲁伊／刺客执行和变身转换尚未接入，本路径不新增变身玩法或虚构速度；全部武器、装备、技能、难度及原服组合仍需分别认证。

SceneController／RemoteCombat处理首次、Hold、锁定与停止，客户端动作来自原包或已发送请求的表现预测；服务端省略本人通知不表示没有施放。ClientSend是原弹体通知资格，不能解释成所有Clt程序都等待0x73；具体源配对与表现见攻击专题。客户端视觉碰撞不是原服命中证据。

鼠标松开发出的原C2S `0x12`只结束引导，不能当作通用施法取消。依据D2MOO `PlrMsg.cpp::D2GAME_PACKETCALLBACK_Rcv0x12_6FC84670`，它只清`STATE_INFERNO`；RemoteScene按当前表CltDo24／seq6识别已支持的引导程序，普通动作及待释放飞弹继续到原动作帧。动作结束清本人表现状态，已释放飞弹独立计时；移动、受击、死亡及场景代次变化仍走真正的中断入口。自研skills的Stop同样不删除普通攻击的待接近请求。

背包右键回城卷轴／书本通过`create_town_portal`发送原3C／0C，并与技能栏共用`record_combat`登记已发送施法意图，不能依赖通常省略的本人4C／4D。这只驱动动作表现；数量、落点、门对象及选择恢复仍来自原包，不自行创建门户或伪造确认。通用修复的历史代表冒烟见下节；观察入口见[调试管道](../../development/DEBUG_PIPE.md)。

States／Overlay定义覆盖层和状态属性；客户端消费A7–AA，真实期限、互斥、反击／吸收、周期及死亡清理由服务端持有。晚入视野恢复已公开单位／状态／装备／归属，不重播历史施法／弹体。尸体资格、碎冰及未完成客户端程序见联网与对应职业专题。

## 动画断点有限冒烟

2026-10-11 Windows Release、D2S v96；本节记录前包客户端SHA256 `076F14C3800E4B7332D94D950E0CFF3E25F4A33AB06AEA386A769E35E6CA6142`，证据在`artifacts/animation-timing`。该批早期包验证Fire Bolt释放和FCR70 Lightning释放；恢复锁修复后的最终包复查保存重入的FCR70时钟、换装断点、FCR50 Lightning、GH／BL、圣盾及双客户端同步，早期释放快照不单独认证该批最终包。仅使用既有客户端和调试管道；装备取当前MPQ原唯一物品属性，管理授予成长、生成物品／敌人、旅行和补满资源用于准备条件，不认证自然获得、任务通行或完整战斗。没有新建测试程序，也未运行旧构建目录残留的`d2x_tests`。

| 核对项 | 实际结果及证据 |
| --- | --- |
| 女巫普通施法与攻击 | SC基础13tick；Suicide Branch／Magefist合计FCR70为9tick、释放第5tick。相同魔杖IAS20→30，A1从15→14tick。FCR70→50，SC从9→10tick、释放5→6tick；`sor/equipped.json`、`bloodfist-final.json`，实际Fire Bolt释放见`fire-start.json`、`fire-release.json` |
| Lightning序列 | FCR70为14tick，释放前pending为1、释放后清零并生成权威弹体；FCR50为15tick。见`sor/lightning-*.json`、`fcr50-lightning-final.json`；TCP最终包重复取得15tick |
| 受击／格挡锁 | FHR20的GH为11tick，FHR50为9tick；FBR20的BL为7tick。最终包自然攻击触发GH／BL，锁内移动和施法均conflict、casts不新增，客户端保留action6／18；锁结束可再施法。见`sor/gethit-*-final.json`、`block-*-final.json`、`recovered-cast-final.json` |
| 圣盾 | 圣骑士同一盾的BL基础5tick；实际施放Holy Shield117、收到state101后为2tick，见`pal/before-holy.json`、`holy-final.json` |
| 保存重入 | 正常退局后新进程加载，同一装备／技能和断点保留，恢复锁及lastBlockTick归零；`sor/reentry-final.json`。恢复时钟没有写入D2S |
| 双客户端TCP/IP | 最终包主客独立进程进入同局，两参与者就绪；客端收到主机skill49序列、GH action6、BL action18；GH锁内主机移动／施法均conflict，客端action6保持。见`tcp-host/lightning.json`、`recovery-6.json`、`recovery-18.json`、`locked-events.json`及`tcp-peer`对应原协议快照 |

TCP最终快照ignoredPackets为0、mapErrors为空；仍有已知的组件装备着色／无形透明限制，不能以原动作包替代逐帧像素验收。该批没有佣兵实战、所有武器／职业序列、德鲁伊／刺客变身、三难度、跨机器、Linux或原D2GS认证；怪物与佣兵的纯整数接入完成构建，运行证据只覆盖表中路径。

## 服务端动画补齐有限冒烟

2026-10-11 Windows Release、D2S v96；该批客户端SHA256 `8E6405C5684F22EE3BA18B60BD8664A379270271A2EB8182E1527C71E107A302`、独立服务端 `29AD217F4AB17950F72CAA4D55009A9ADB4F5E98978245A56F8FEF0EA75E81E3`，最新包另见[运行包记录](../../development/BUILD_AND_RUN.md#运行包记录)。证据在`artifacts/animation-timing/server-audit`，只运行既有游戏和调试管道。管理成长、人数、敌人、旅行及补满资源只准备条件；伤害、命中、技能动作及恢复由游戏正常执行，没有新增测试脚本、用例或专用程序。

`sor`／`barb`证据来自本轮中间包客户端SHA256 `45D8BC9372617C210AA824E1C28F4F8F4C6325D6304EC56D12E6708538EE8E6D`；最终增量只补沙虫女王S1的原帧准备，最终包重新运行`final`中的序列、重载和画面路径，不将中间包结果计作最终包逐项复测。

| 典型路径 | 实际结果及证据 |
| --- | --- |
| 连续受击与锁内命令 | 女巫第一次GH结束tick465，未结束时再次受击延长到469；锁内移动／施法均conflict，原action6保持。`sor/first-gh.json`、`before-restart.json`、`restarted-gh.json`、`locked-events.json` |
| 眩晕期间再次命中 | 野蛮人实际Stun139命中brute1，收到state21／怪物原GH mode3；随后普通攻击再次产生GH，interruption从1到2。`barb/stun-hit.json`、`stun-events.json`、`stunned-normal-hit.json` |
| 怪物Smite与击退锁 | Duriel实际Smite97在tick916命中，玩家位移并收到action20／state21，KB锁到922（6tick）；tick919移动／施法均conflict，结束后recovering清零，眩晕状态仍保留到独立期限。GH原图可见。`barb/knockback.json`、`knockback-world.json`、`knockback-locked-events.json`、`knockback-finished.json`、`knockback.png` |
| 怪物与佣兵Jab | 最终包Duriel技能10起始238、结束251（13tick）；第二幕佣兵技能10起始198、结束212（14tick）。按权威attack事件起点核算，不用采样快照tick减结束tick。`final/combat-events.json`、`duriel-jab.json`、`hireling-jab.json`；原MonSeq事件行已接入调度，未逐次认证所有多事件伤害 |
| 保存重载及画面 | 最终包新进程加载同一45级野蛮人、技能及45级第二幕佣兵，recoveryUntil／lastBlockTick均0；自然战斗画面正常，最终ignoredPackets为0、mapErrors为空、stderr为空。`final/reentry.json`、`finished-world.json`、`scene.png` |

客户端与独立服务端均完成Release构建／打包，嵌入宿主与独立服务端共用同一领域实现；本轮没有运行PvPGN／原D2GS后端。抽取上述典型路径，不穷举每个怪物、武器或职业；女武神／死灵召唤物、全部特殊模式／装备状态速率组合、三难度、跨机器和Linux仍未运行认证。前包的全部断点及双客户端结果保留在上节，不能替代本包对应路径复验。

原生救赎的尸体判定由`monsters::selectableCorpse`集中读取实际死亡动作、CorpseSel和Udead状态；不以经验／掉落结算完成代替死亡模式。玩家／非玩家的资源与种子、尸体REDEEMED及不可选标记通过同一候选提交。拉达门特的回收者是实际飞弹，求值和恢复不借用死亡怪物的资源；相关后续源码尚未构建或运行。

## 通用施法表现有限证据

2026-10-09 Windows Release、D2S v96；EXE SHA256 `BF767C41680FAD44DD825B8BD877D82D30629C03CCB0B45A79038243FD55A52A`，DLL `9F7B1FAF7AF453E3DC481F05207C76460AECC38FF33A50E92823032228B2969F`。证据：`artifacts/skill-presentation-20261009`，只使用既有客户端／调试入口。

- Single Player实际UI快速按下／松开后，Fire Ball62、Ice Bolt59、Frozen Orb260／261仍生成／移动／散射，有原图截图；火球将管理准备的zombie1生命7→0。Inferno有10个视觉弹体，Stop后施法／弹体／pendingReleases清零；背包卷轴消费并创建蓝门。最终failures／ignoredPackets为0，mapErrors／effectLimitations及stderr为空。
- 同包连接本机1.13c D2GS／PvPGN，在冰冷之原复查Fire Ball／Ice Bolt的Stop后表现；背包开门取得219施法、数量1→0及原59门户证据。原服既有ignoredPackets=16；此前一次AF后加载超时，重新选角恢复，原因未认定修复。

仅认证列出路径，不覆盖全部职业／参数、多人、三难度、精确逐帧／像素或Linux，也不认证后续源码。最新运行包身份见[运行包记录](../../development/BUILD_AND_RUN.md#运行包记录)。
