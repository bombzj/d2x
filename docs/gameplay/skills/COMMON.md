# 公共技能与状态效果

本页负责技能学习入口、公共效果和状态规则；调用链、参数、端口和文件归属见 [技能模块](../../modules/SKILL_RUNTIME.md)。等级／协同／加成由权威调用方准备，处理器不反向查询完整角色、怪物或会话。

职业实现分别见 [女巫](SORCERESS.md)、[圣骑士](PALADIN.md)、[死灵法师](NECROMANCER.md)、[亚马逊](AMAZON.md)。职业树能显示、等级能保存不等于技能已有执行；未实现技能明确拒绝，不以普通攻击替代。装备充能／触发、多玩家来源和其余未实现技能继续暂缓。

## 原始数据与边界

运行时读取原 MPQ 的 `Skills.txt`、`SkillDesc.txt`、`CharStats.txt`、英文 TBL 字符串和七套 `skltree_*_back.dc6`／职业图标。当前资料片原表中，七个职业各有 30 个技能，分布在 `SkillPage` 1–3、`SkillRow` 1–6、`SkillColumn` 1–3。节点身份是原 `Skills.Id`，位置、图标和页签名称来自原表及原图，不维护抽取后的技能清单。用户提供的角色／法师技能树截图保存在忽略的 `artifacts/character-skill-layout-reference-cn.png`，仅用于核对版面。

当前接入26项女巫主动技能：火弹、充能弹、冰弹、冰封装甲、地狱之火、静电力场、冰霜新星、冰风暴、火球、闪电新星、传送、冰封球、暴风雪、冰尖柱、碎冰甲、寒冰甲、火墙、炽烈之径、能量护盾、强化、雷云风暴、心灵传动、九头海蛇、闪电、连锁闪电和陨石；四项被动为暖气及火焰／闪电／冰冷支配，共30/30项有玩法入口。2026-10-02全部补齐后Release构建、打包及普通临时角色联合冒烟，客户端／路径等限制见各项说明；数量不代表全部原版精确认证。亚马逊三页共30项均有执行／被动入口，数值、原动作和边界见 [亚马逊技能](AMAZON.md)。死灵法师三页共30项均有执行／被动入口，召唤整页十项已补齐；逐项构建／打包、联合冒烟及原型／装备消费者边界见 [死灵法师技能](NECROMANCER.md)。各职业尚未接入的主动技能（包括野蛮人旋风、跳跃攻击和战嗥）没有执行效果；未列出的被动只记录等级。七职业原节点、升级点数、等级／前置门槛保留，名称与页签使用英文原资源。

实施范围中的“一个系”按原技能书的一整页计算；例如死灵法师“毒素与白骨”包含十项，不仅是其中的白骨伤害技能。

## 执行结构

- `content/skills/skill_data.*` 建立原技能身份、学习门槛及 `BasicSkillAction`；`content/skills/sorceress_data.*` 、`content/skills/weapon_skill_data.*` 和 `content/skills/necromancer_data.*` 只为已核实的法术／武器技能提供 `SkillSpec`。无定义的技能不登记执行行为。
- `UseSkill` 是按原 `Skills.Id` 提交的唯一技能命令。`GameSession::useSkill` 在 `session_skills.cpp` 统一检查技能可用性、实现状态、城镇许可、等级及原施法动作，随后生成 `SkillCastSpec`。普通武器攻击仍共用 `Attack`；没有按内部枚举直接施放的旁路。
- `skillAvailable` 表示角色拥有技能，`SkillRecord::executable()` 表示主动效果已接入；学习、选择、绑定不等同于能够施放。未实现主动技能显示禁用色、提示效果未实现、属性面板留空，不扣蓝、不开始动作，也不转为普通攻击。
- `gameplay/skills/resolve.cpp` 是等级、基础等级协同、支配和定点费用的纯计算入口；`casting.cpp` 负责开始、出手、持续引导和结束。`SkillBehavior` 仅标识已实现的执行算法；存档、输入、事件与声音键使用原技能 ID，不使用该枚举的顺序。
- `PlayerState.pendingCast/channel` 持有本次施法的参数快照和目标；武器技能使用 `weaponAttack.skill`、原攻击动作及出手帧，时限技能共用 `skillDelayUntil`。普通 SC 施法按实际武器的原 `AnimData` 时序释放，缺记录明确暂缓；地狱之火使用已核实的 SQ 准备路径。冰封球子弹体保存原技能等级，生成时按施法者当时的协同／装备属性确定伤害范围。UI 只读状态并提交命令。
- 已删除 `SkillDefinition`、演示 `SkillSystem`、固定费用／冷却数组、旋风和跳跃移动状态、旧圆圈／火球绘制及硬编码演示声音。视觉 `Effect` 只记录原弹体／叠层身份；爆炸桶原图尚未接入，不再借用演示火球。`mvp/` 与旧压缩包作为历史资料保留，不参与当前执行链。

亚马逊双倍打击、闪避／避免／回避、刺入和穿透已沿有效技能等级和装备派生接入，纯魔法飞弹／武器命中、原S1、发射速度与穿透都有实际消费者；其他职业未实现被动仍仅记录等级，准确范围见[亚马逊](AMAZON.md#被动与魔法技能整页)。

## 状态效果系统

`gameplay/effects/state.*` 提供可由任意单位拥有的 `CombatEffectSet`。当前女巫三种冰甲接入该容器；内视／慢速箭也已接入目标状态，女武神装备效果沿同一属性聚合；它不是全技能 Buff 已完成的声明。

| 部分 | 当前接口与约束 |
| --- | --- |
| 原状态定义 | `content/skills/state_data.*` 从当前 MPQ 的 `States.txt` 导入原 ID、group、remhit、玩家／怪物／首领的 staydeath、hide／shatter／udead 和叠层名；不从技能名猜状态。 |
| 来源与目标 | 容器归属于受影响单位；`EffectSource` 独立记录来源类别、实体、原技能／物品等定义 ID 和等级快照，施法者与受影响单位不混用。 |
| 实例与时间 | 每容器分配不复用的 `EffectHandle`；开始／到期使用绝对 25 Hz `WorldState.frame`。无 duration 表示由来源显式维持的状态；跨区保留，读档创建新容器。 |
| 重施与互斥 | 调用者显式选择 ReplaceState、ReplaceSource 或 Independent。非零原 group 在目标容器内互斥，不因来源不同而叠加；替换先准备再提交。 |
| 数值 | 实例持有 `CharacterModifiers` 快照，经原有属性派生入口统一聚合；开始、替换、到期及死亡移除立即刷新角色派生缓存，不通过 UI 加减属性。 |
| 事件 | 实例拥有类型化 `EffectReaction`；派发前复制动作、来源、状态 ID 与实例句柄，事件处理不持有容器内引用。三种冰甲分别使用近战受伤冻结、近战尝试反击及弹体接触反击，删除实例同时删除其反应。 |
| 移除 | expire、onDeath、onHit、按句柄／原状态／来源移除和 clear 返回包含完整实例及原因的记录；到期与死亡已接模拟循环。onHit、驱散及来源注销是显式调用入口，尚无对应技能时不擅自添加触发条件。 |
| 表现 | 原叠层由状态的只读 `EffectVisual` 驱动；图形与声音资源由表现层加载，状态容器不持有资源或回调指针。 |

冰封装甲通过 `SkillCastSpec.appliedEffect` 提交通用状态，施法执行器不再分别写防御／冻结字段。当前 MPQ 为状态 10，三种冰甲的 group 均为 1，死亡保留标志均为空；源码实现到期、重施互斥和死亡移除，近战实际物理伤害后执行无伤害冻结回击。参考依据为本地固定 D2MOO `Skills.cpp::sub_6FD11C90`、`SkillSor.cpp::SKILLS_SrvDo018_DefensiveBuff`、`SKILLS_CurseStateCallback_DefensiveBuff` 和 `D2States.cpp`。两种高级冰甲已接源码，未进行交叉施放验收。

扩展新技能时，先在内容适配层核实公式、状态、目标过滤和事件函数，再增加类型化行为、解析及实际消费者。诅咒强弱覆盖、光环范围／刷新周期、周期伤害的叠加与来源归属、变形／控制状态、召唤物和装备触发需要各自的原规则，不能用上述三种重施策略替代所有原版规则。周期执行应按原模拟帧产生独立玩法事件，不能借用渲染时钟或每次属性重算触发。现有怪物冰冷／毒素／蛛网、药剂和祭坛仍使用各自已有状态；将来迁入此容器时必须移除旧消费者，避免重复计时或双重加成。容器本身不依赖玩家、MPQ、GPU 或 D2S，怪物／佣兵接入须同步其属性与生命周期消费者。

## 冻结死亡与尸体资格

公共伤害入口在进入死亡流程之前处理本次命中的冻结。冰尖柱范围命中、冰风暴直接命中及冰封装甲回击共用冻结规则；冰尖柱一击致死不再因生命已归零而跳过冻结。已有冻结期间被任何其他伤害杀死，也在清理冻结计时之前保留死亡状态。难度 FreezeDiv 把正持续时间整除为零时仍保留本次原 freeze 位，可影响同一帧的致死判定，但不人为增加冻结时长。

当前 MPQ 的 freeze（1）为 `monstaydeath=1`、`shatter=1`、`hide=1`、`udead=1`，bossstaydeath 为空；shatter（107）另保留 bossstaydeath。公共死亡入口按真实怪物的普通／首领死亡保留掩码处理这些标志，分别记录隐藏尸体、碎裂表现和尸体不可选；udead 与 hide 不混用。碎裂目标不播放普通 DT／DD，也不能作为 Raise Skeleton、沉沦魔巫师复活或沉沦魔见尸逃跑的尸体。原实体身份仍在本局保留，EnemyDied／击杀数／经验／掉落／任务照常结算一次；原 MonUMod 35 的独立碎尸标志也走此入口。

首领、冠军、暗金／超级暗金及佣兵按原冻结入口降为冰冷减速；无法冻结、冷免疫、普通怪物非负 ColdEffect 和当前 uninterruptable（54）状态仍受原校验约束，不把蓝色减速一律当作必碎尸。冰封装甲回击也使用同一原长度、抗性／冰冷穿透、半冻结时间与难度整除入口。普通冰冷的随机 shatter 规则尚未接入，此次不为所有冰伤补造碎尸概率。

表现层从当前 Missiles 读取 icebreaksmall／medium／large（272–274）及 smallmelt／largemelt（344–345），加载原 IceBreakSmall／Large 与融化 DCC；破碎图本身包含整组冰块，不生成自造粒子。原 AnimLen=15／12、animrate=1024、Range=30／150、四方向和 Trans=1 使用公共原图／PL2 绘制。当前按怪物碰撞尺寸选择一份完整破碎图，在原 Range 到期时接相同方向的融化图；客户端大小分档、随机变体分布、融化创建帧及 smoke 组合未在四个本地 reference 中找到完整 D2Client 证据，这些显示适配不声明逐帧等价。原 impact_shatter_1/2/3 由破碎弹体的 TravelSound 加载，Group Size=3、Volume=255、Compound=0、Defer Inst=1；播放中的同一 WAV 拒绝重复请求，音频与图形随机流均不参与伤害或掉落。

规则依据为本地 D2MOO `SUnitDmg.cpp::SUNITDMG_ApplyFreezeState`／伤害执行顺序、`SkillSor.cpp::SKILLS_EventFunc02_FrozenArmor`、`D2States.cpp::STATES_UpdateStayDeathFlags`、`MonsterMode.cpp` 死亡入口及 `MonsterUnique.cpp::MONSTERUNIQUE_ApplyShatterState`。OpenDiablo2 `states_record.go` 交叉核对 hide／shatter／udead 与 uberminion 的特殊表现；本次未开放 uberminion 爆炸状态。当前 MPQ 证据与原图预览在忽略的 `artifacts/frozen-death-20261001/`。仅修改源码与文档；未构建、运行检查、编写或运行测试、打包或提交。所有怪物冻结／碎尸标志仍是临时会话状态，D2S v96、编码和规则指纹机制不变。

## 出生通用动作与基础远程攻击

`CharStats.txt` 每职业的 `Skill 1` 至 `Skill 10` 列给出通用动作；`StartSkill` 仅女巫和死灵法师有值，原版由第一件初始装备提供，而非无条件送一点技能等级。当前源码按这些原列建立通用技能和初始装备授予关系，装备仍在手中且满足需求时可选择初始职业技能。普通攻击、Throw、Kick、Unsummon、允许的左手动作使用 MPQ 的原图标；卷轴／书本技能继续通过物品使用入口，辨识目标与书本施法暂缓。Kick 的普通攻击替代已删除；Kick、Unsummon 在对应规则／目标体系实现前明确暂缓。

点击左／右技能槽展开菜单，鼠标悬停目标图标后按 `F1–F8` 绑定到该鼠标键；之后单按对应功能键只切换技能，不立即施法。已绑定技能在菜单图标上标注功能键，绑定写入当前存档。调试管道的 `skills` 返回当前 MPQ 的技能 ID、前置及现有绑定；`bind-skill-hotkey` 使用同一会话规则，可用于快速检查。`grant-experience` 升级后按原门槛调用 `learn-skill` 可逐项解锁传送等技能。原操作依据[暴雪 Arreat Summit 控制说明](https://classic.battle.net/diablo2exp/basics/controls.shtml)。

普通攻击、弓弩、Throw／左右手动作现在共用原 AnimData 出手帧及逐武器数值；出手帧扣除匹配箭袋或投掷堆叠，弹体保存发射快照。六种投掷药瓶按当前 MPQ 的真实弹体映射接入。爆炸、命中视觉与移动毒云由通用 `MissileImpactSpec` 配置，火球也使用同一执行器；药瓶没有独占的爆炸／毒云代码。数值、入口、原版证据及未完成细节见 [通用攻击与导弹效果](../combat/ATTACKS.md)。当前运行包包含2026-09-27的原表复查修订及2026-10-05亚马逊三页；这些技能逐项构建／打包并完成有限联合冒烟，详见亚马逊专题。魔法箭不消耗弹药，炮轰按原开始函数一次扣弹药；不能将普通攻击标为全面复刻。
