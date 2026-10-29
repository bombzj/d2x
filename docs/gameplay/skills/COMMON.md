# 技能公共规范与实现边界

本页维护技能迁入规范、跨职业公共入口、状态所有权及执行路径核对；持续效果的具体机制由[状态列表与周期事件](EFFECTS.md)维护，客户端施法输入归[HUD](../ui/CLASSIC_HUD.md#客户端普通施法与移动输入)。原服和自研宿主使用同一客户端；客户端提交原请求、读取原包及已知属性，服务端拥有命中、消耗、效果和保存。旧单机执行器不恢复到客户端。

迷乱目标选择的阵营抽数已改为与怪物AI共用一次随机借用，动作前发布或在思考结束发布；避免选择器先推进单位流、后续决策却提交旧种子。源码与边界见[怪物台账121项](../monsters/ACT3.md#逐项接入)；迷乱战斗／背压与原逐随机轨迹仍待对齐。

## 技能执行路径源码核对

怪物动作的1／2标记先于同帧ENDANIM兑现，`monsterActionEvent`不能因该帧同时结束动画而跳过SrvDo或最后一次原生联系；原版依据为D2MOO MonsterMode的技能动作回调和`sub_6FD13410`。当前MPQ的E9S2HTH仅两帧，第二帧标记1，陷阱的实际飞弹出生依赖这一公共顺序，不能只发布4C动作而漏掉服务端效果。

伙伴AI区分逻辑决策期限与动作锁造成的实际唤醒期限；后者不属于原版显式未来AITHINK等待。AI101／104成功UseSkill后保留已到期的逻辑期限，公共ENDANIM沿MonStats aidel设置下一次思考；`companions::think`不把物理延迟改写成逻辑等待。依据D2MOO AiThink::AITHINK_Fn101_AssassinSentry及MonsterMode::sub_6FC64310；其他AI显式等待保持其原逻辑期限。不同宿主墙钟推进速度应先校准，不能据原包秒数直接替换帧参数。

核对范围按七职业主动／被动、通用技能、怪物／精英、佣兵／召唤物和物品来源的生产入口追踪。玩家动作由`skills::cast/weaponCast`发布actions时间线，非玩家由`requestCast`发布怪物动作事件；战斗、飞弹、状态及归属分别归对应领域。被动通过属性来源的原生技能刷新／StatList计算，不为每项被动制造施法动作。公共入口相同不要求不同原St／Do、资格或采样时点被强行合并。

当前源码已修正以下分歧；专用路径与原版的完整行为仍待核对：

- 玩家尸爆、物品触发尸爆与死亡卫哨共用`effects::corpseExplosion`，使用当前原表公式、尸体基础生命表、原房间候选和一个物理／元素伤害结构。尸体不可用／隐藏状态、尸体种子及玩家消耗随同一候选发布，逐目标同步回调保留背压游标；旧双伤害分支及其专属生命／比例／半径快照已删除。
- 怪物／佣兵／伙伴以真实属性选择St／Do等级并登记MPQ单位事件，预分配近战保留St结果，失效释放同步丢弃联系。孵化、Bestow、钻地回复、冲锋及怪物诅咒改用当次原公式／属性；四处hosting无实际单位的数值快照和旧怪物飞弹备用执行器已撤除，动作标记／动画准备继续保留。
- 物品触发的战吼、尸爆、刺客与德鲁伊专用程序复用`activate`分派并保留释放记录；战吼不再只安装拥有者状态而跳过原扩散弹体。充能／普通技能来源继续独立，清空热键不要求旧充能源仍有效；切换引导技能同时比较技能与来源身份。
- 使用MPQ动态filter的范围／状态查询补入实际执行帧，满足JUSTHIT及尸体筛选的公共输入契约。
- 无消费者的`CombatEffectEvent/EffectAction/reactions`旧反应模型、旧精通／充能／穿透辅助入口、精通曲线运行副本、通用summon空入口、Hydra直接提交旧入口，以及无消费者的施法／覆盖图时长、雷云旧周期和能量盾旧吸收投影已删除。实际单位事件、护盾池、周期事件和召唤候选仍由各自公共机制持有；隐藏Kick仅使用已分配的原生近战联系。
- 火魔与精英死亡效果改用真实修饰符事件及同步伤害联系，保留槽顺序和背压进度；火强化采用当前MPQ倍率及1.13c程序内难度缩放，冰强化改为原64向新星。伙伴AI死亡爆炸和精英效果期限轮询已移除。
- 静态力场／心灵传动单位伤害共用原生直接程序，雷云风暴创建真实临时飞弹、执行命中回调并移除；烈焰之径／蛛网由移动模式读取原PATH跨格标志、坐标与方向，旧Effects位置轮询及WebTrail载体已移除。

死亡／GH由`monsters::prepareActorPublication`／`effects::stageDamageStates`准备修饰符事件，`effects::monsterModifierEvent`按槽顺序执行；非GH电强化接同步联系中的`monsterDamageEffect`，GH采用原两帧事件。活冻结拥有者的未开始一次性MONUMOD被跳过并消耗，已开始回调继续完成。QueenDeath由`monsterDeathEffect`处理，关联死亡候选另保留一次事件补齐，以死亡中断身份去重。尚有修饰符回调的来源延后移除，不要求主人同区或存活。火魔采用UMod31四帧延迟、六subtile半径、同一结构100物理＋100火焰；火强化在回调时按实际等级查原生命表及DifficultyLevels.MonsterCEDamagePercent，再应用1.13c难度缩放。电强化及甲虫／女王保留四个基向、每向两个独立路径；爆炸保留真实飞弹、私有种子、原房间筛选和径向阻挡。

静态力场按Calc2→Calc1→元素长度→范围采样，不生成飞弹或抽取普通基础伤害，接触时读取当前生命、难度下限并补偿负抗性。心灵传动按原物理／元素投掷和击退判定处理单位伤害，保留HitFlags／ResultFlags及命中类别，其他目标仍走各自事务。物品触发复用释放记录，背压不重抽或再次扣费。雷云风暴保留真实临时飞弹的被飞弹命中回调，完成同步接触后移除，再发布技能通知。`movement::frameEvent`／`monsters::advanceMode`在推进PATH前调用`effects::movementSkills`；烈焰之径读取状态首列表的modifierlist_skill／level，蛛网读取实际最高技能等级，按原方向表及速度3生成spidergoolay，再由原飞弹程序生成地面效果。施放使用当前原公式、Passive写入和单位事件登记，AI查询实际状态列表。

怪物动态规则入口为`skills::requestCast/releaseMonsters`、`combat::prepareNativeSkillContact`、`effects::monsterCurse`及`monsters::healSkill/startSkillMotion/activateWeb`。hosting保留原表、动画、程序身份与初始技能赋值；St／Do分别保存采样及背压进度。孵化按当次Calc1准备子体，共同发布并保留后续进度，零数量也进入死亡处理。Bestow先Calc2再Calc1，当前MPQ两式为空，保持零治疗；钻地回复以当次生命计算。冲锋在St读取当前velocitypercent及原Run，保留最低50%限制。怪物诅咒准备范围、期限、六槽属性及原filter，再逐目标处理抗性与覆盖；精英Cursed保留有意的来源快照。客户端／内容仍有消费者的resolveSkill接口继续保留。

以上规则核对当前MPQ Skills／Missiles／DifficultyLevels、本地D2MOO MonsterUnique／MonsterMode／SUnitDmg、SkillSor Do20／21／23／29、PlrModes、Path/Step及SkillMonst；1.13c与参考转录的差异和地址见[资料来源](../../resources/THIRD_PARTY.md#原版113c静态与开发对照证据)。仍待核对重复修饰符／复合强化、冻结死亡、自然死亡／退休／上限替换、主人换区、同步回调背压，及静态力场下限／抗性、心灵传动各类目标、雷云通知顺序、移动轨迹／跨房间生成和St／Do之间属性变化。这里是源码入口核对，未完成全部逐ID／来源组合、当前MPQ完整数值或原版逐帧对齐。

## 客户端普通施法与移动输入

正文见[客户端普通施法与移动输入](../ui/CLASSIC_HUD.md#客户端普通施法与移动输入)；本标题保留旧引用，规则只在负责页维护。

## 当前持续效果入口

正文见[当前持续效果入口](EFFECTS.md#当前持续效果入口)；本标题保留旧引用，规则只在负责页维护。

## 帧事件底座与持续效果期限

正文见[帧事件底座与持续效果期限](EFFECTS.md#帧事件底座与持续效果期限)；本标题保留旧引用，规则只在负责页维护。

## 跨单位接收与载体边界

正文见[跨单位接收与载体边界](EFFECTS.md#跨单位接收与载体边界)；本标题保留旧引用，规则只在负责页维护。

### 按原程序区分接收资格

正文见[按原程序区分接收资格](EFFECTS.md#按原程序区分接收资格)；本标题保留旧引用，规则只在负责页维护。

### 性能优化的复刻约束核对

正文见[性能优化的复刻约束核对](EFFECTS.md#性能优化的复刻约束核对)；本标题保留旧引用，规则只在负责页维护。

## 文档分工

| 内容 | 负责页面 |
| --- | --- |
| 通用规范、代码所有权、跨职业计算、整体技能状态 | 本页 |
| 持续效果、列表投递／接收、冷毒状态、期限与周期回调 | [持续效果机制](EFFECTS.md)；实例队列／跨领域发布仍归内核模块 |
| 原模块职责、DLL调用及公共提取的结构证据 | [参考设计](../../architecture/REFERENCE_DESIGN.md#6-d2moo的公共层究竟共用什么) |
| 全分类索引、逐技能原函数、专项算法、客户端差异与原版参考结果 | [技能目录](README.md)链接七职业、通用、怪物、同行者、物品及特殊条目的负责页 |
| 学习／选择资格、树／提示投影 | [人物](../../modules/CHARACTER.md)；原图布局见[HUD](../ui/CLASSIC_HUD.md) |
| 充能／装备触发、弹药／耐久及物品使用事务 | [库存](../../modules/INVENTORY.md) |
| 协议／客户端副本、动画／弹体表现 | [联网](../../modules/NETWORK.md)、[攻击与弹体](../combat/ATTACKS.md) |
| 保存格式与校验边界、状态口径及后续实施顺序 | [存档](../../modules/SAVES.md)、[总计划](../../architecture/MULTIPLAYER.md#21-统一状态口径) |

职业页不重复公共规范，参考设计不维护第二份职业实现台账。长期文档记录当前实现、原版依据、已知差异及待核实项；原版参考须保留版本、条件和关键行为，不能只引用临时目录。构建、自研运行及打包情况在当次交付中说明，修复过程由Git追溯。

## 冰冷、冻结与毒伤

正文见[冰冷、冻结与毒伤](EFFECTS.md#冰冷冻结与毒伤)；本标题保留旧引用，规则只在负责页维护。

## 职业与证据

| 范围 | 当前自研服务端 | 客户端、原版依据与缺口 |
| --- | --- | --- |
| 通用0–5、217–220 | 十项执行入口；Kick是隐藏破桶程序 | 原程序及普通桶KK、Left Hand Throw／Swing等组合边界见[GENERAL](GENERAL.md)；左手弓与相反手弹药规则见[装备](../items/EQUIPMENT.md#手位与双手剑) |
| 女巫36–65 | 26项主动、4项被动 | 原Clt表现已接；原服参考及完整参数／随机／时序缺口见[SORCERESS](SORCERESS.md) |
| 亚马逊6–35 | 24项主动、6项被动 | 原Clt表现及宠物归属／装备已接；原版依据、女武神死亡图等缺口见[AMAZON](AMAZON.md) |
| 圣骑士96–125 | 10项战斗技能、20项光环及随状态切换的被动贡献 | 原版D2GS三个代表技能的参考结果、原函数依据及组队／PvP／Holy Shield特殊盾图限制见[PALADIN](PALADIN.md) |
| 死灵66–95 | 27项主动、3项被动；诅咒、毒／白骨、召唤及铁魔材料生命周期 | 完整召唤流程与联机／原版对齐仍有缺口，见[NECROMANCER](NECROMANCER.md) |
| 野蛮人126–155 | 20项主动、10项被动；武器精通、双持、移动、呐喊及尸体技能 | 跳跃高度曲线及完整装备／难度／多人组合边界见[BARBARIAN](BARBARIAN.md) |
| 刺客251–280 | 28项主动／2项被动，武学／陷阱／影子及持续程序已有实现 | 原版依据及完整事件／随机／组合缺口见[ASSASSIN](ASSASSIN.md) |
| 德鲁伊 | 30项及变形／动物／藤蔓／灵体依赖已有实现 | 公共接触、列表、原生出生、形态时钟与状态客户端，完整原版对齐边界见[DRUID](DRUID.md) |
| 怪物／同行者／物品及其他非职业程序 | 第一幕怪物／精英、部分伙伴及物品来源已有入口，其余范围未完整迁入 | 状态与证据分别见[MONSTERS](MONSTERS.md)、[COMPANIONS](COMPANIONS.md)、[ITEM_SKILLS](ITEM_SKILLS.md)、[SPECIAL](SPECIAL.md)，不计入七职业完成数 |

充能和物品授予可以调用已准备的技能程序，不能据角色职业或装备条目推断未实现程序可用。全部入口齐备仍须逐项闭合来源、目标、动作、状态、随机及生命周期；不按构建和自研运行建立完成等级，统一口径见[总计划](../../architecture/MULTIPLAYER.md#21-统一状态口径)。

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
| [item_triggers.cpp](../../../src/server/systems/reactions/item_triggers.cpp) | 装备触发队列、概率与目标解析、效果已执行／通知待发送状态；itemTrigger复用程序分类，但保留零消耗、死亡与目标限制 |
| [client_missile_program.hpp](../../../src/presentation/world/client_missile_program.hpp) | 已支持CltDo能力目录，集中静止、创建来源配对和子弹体图形资格；程序号来自MPQ，不是技能ID |
| [client_missile_view.cpp](../../../src/presentation/world/client_missile_view.cpp) | 按CltDo单次分派逐帧专用行为，之后共用运动、碰撞、子弹体队列和表现时钟；不运行权威命中 |

服务端策略表用具名枚举绑定成员函数，编译期检查重复、遗漏和空入口；新增枚举必须登记处理器。未支持行为不再默认转交弹体：普通施放在创建动作前返回NotImplemented，释放也有显式Unsupported入口。弹体行为清单须与missiles/launch中已实现程序同时维护；增加字段或内容定义不自动授予执行支持。客户端能力表检查程序号唯一且有序，未知CltDo不创建弹体。

普通释放与装备触发不可机械合并：Enchant接收者、Telekinesis物件扣费与背压、死亡触发、武器多段以及引导具有不同上下文。共享程序分类与确定性目标选择，不共享两端世界状态，不重新扣费、掷值或执行已接受的效果。MPQ解析器中核实公式／资源的条件不是热路径分派，保留明确校验；本轮不引入通用脚本解释器、动态注册或每技能虚类。

新增技能若复用既有程序，只准备当前MPQ规则与资格；新增执行程序需登记策略并实现领域操作，新增CltDo需登记能力并实现其逐帧／接触／到期行为。数据、执行与表现支持分别维护，不能通过目录注册宣称新职业完成；完整程序／来源组合与重构边界仍须逐项按原版核对。

1. 先读master已实现的定义／执行与当前公共函数，复用可用规则；耦合GameSession的部分只迁规则和必要状态。
2. 依据当前MPQ登记Skills的SrvSt／SrvDo、Missiles的Do／Hit／Dmg、动作事件和状态。用本地D2MOO定位职责与缺漏；1.10f重建、1.13c DLL和当前表的差异分别记录，参考附带参数不覆盖MPQ。
3. 提取真正相同的纯计算，明确基础／有效等级、来源、取整、帧相位、种子归属和实体／区域代次；返回纯值、下一小状态或动作描述，两端分别创建权威／视觉对象。Clt／Srv参数、触发条件和随机顺序有原版差异时保留，不能为代码一致抹平。
4. 服务端区分开始资格和释放复验，资源／数量／耐久／充能与事实按事务提交。背压保留首次准备结果，不重复扣费、掷值或命中；异步内容准备绑定请求并只预留一次独立种子。未准备或未知规则明确拒绝。
5. 客户端只改有原版依据的公共提取和修复，每处记录MPQ／原包／原函数依据。禁止为兼容自研宿主另写客户端玩法分支、私有确认消息或推算远端隐藏值。
6. 更新职业台账的实现范围、原版依据及明确缺口；保存语义变化同步SAVES与校验边界。构建／打包／运行仅按有效授权，不编写测试脚本、用例或专用程序；实际执行情况在当次交付中说明，不进入长期功能台账。

## 跨职业公共计算

`server/systems/skills/system.hpp`不暴露私有施法／怪物释放结构，它们位于仅技能实现使用的`runtime.hpp`；求值、支配、充能源逻辑移入`evaluation.cpp`，公共头只声明函数。普通技能实现改动限定在对应编译单元，私有队列布局改动限定在技能模块；共享规则结构变化仍会影响真正使用这些类型的调用者。参与者关系通过轻量声明读取，不包含PlayerStore／monsters系统头。公共战斗规则、提交阶段与运行限制见[内核](../../modules/SERVER_SYSTEMS.md#战斗关系与提交)。当前Release包已包含，未实测增量编译耗时。

| 计算入口 | 必须保留的语义 |
| --- | --- |
| `resolve`、`rank_sources`、`rank_bonus`、`passive` | 基础等级用于协同，来源的有效等级用于施放；安装属性贡献仍归服务端 |
| `damage_curve` | 原法力定点曲线与MinMana消耗分开；技能元素伤害先HitShift再协同，独立弹体先协同再HitShift，随后应用支配；8.8最小值≤256且首级增量为0时跳过协同 |
| `cast_timing`、`amazon_sequence`、`weapon_volley` | 原AnimData事件、SC／seq6／seq12及武器序列／回滚；CAST按整数求min(100+120×FCR/(120+FCR),175)，不叠加OtherAnimationRate；Inferno seq6固定速率 |
| `projectile_path`、`amazon_missile`、combat几何 | 速度、格点、环射／扇形、remaining调度、引导／连锁／分裂等纯值；各端持有候选、时钟、碰撞及创建队列 |
| `weapon_damage`、`poison`、`avoidance` | 武器六通道／转换、显式随机、毒替换及近远程／移动回避；不因纯函数可用就授权客户端结算 |

#group 9特殊目标保留独立实例顺序及清理标记；生成身份和当前随从归属分开，单位候选一起复验。原始状态／移除回调、护盾池、被动和资源回调共用准备入口，玩法不读取MPQ、不持有平台依赖，不将控制、目标节点或期限写入D2S。具体实现、原版依据与缺口由负责技能页维护。

## 公共函数抽取

2026-10-11对照本地D2MOO、当前MPQ准备代码及既有原1.13c静态证据，收敛下列重复计算。函数只接收显式规则与输入，不持有世界、MPQ或随机流；客户端和服务端通过各自入口调用。

| 公共入口 | 已接调用与保留条件 |
| --- | --- |
| `rank_bonus::skillRankBonus`、`skillDiminishingBonus`、`skillDiminishingRatioBonus` | 亚马逊被动、圣骑士光环／盾／转换、死灵诅咒／石魔、野蛮人被动／Frenzy／跳跃／尸体搜索／Berserk共用曲线。固定原1.13c D2Common RVA4DC30／ordinal10115确认先截断110×等级/(等级+6)，正等级包装现共用nativeSkillDiminishing；公式的dm78／飞弹等级零分支直接调用原始算子。两条旧函数名只保留调用兼容，不再维护不同数值规则；完整技能流程仍待原版对齐 |
| `skillRank`、`skillRankSum`、`skillSynergyBonus` | 女巫甲胄／伤害、圣骑士光环／武器、死灵白骨装甲、野蛮人呐喊／武器等统一查询、协同等级求和与加权求和；调用者仍区分基础等级与有效等级 |
| `damage_curve::skillDurationBonus` | 技能元素长度与骷髅法师弹体长度共用8／16级分段。技能长度入口保留1–255资格，法师内部合成等级不强加这个上限；技能／弹体伤害的HitShift和协同顺序不合并 |
| `evaluateBarbarianPassive`、`evaluateNecromancerPassive` | 六武器精通及四项野蛮人被动的提示／实际贡献，骷髅支配／石魔支配／召唤抵抗的提示／召唤求值读取同一纯结果；死灵提示不再扫描Skills表。参数仍由当前MPQ准备，属性安装与宠物创建由服务端完成 |
| `attack_timing::animationFrameTicks`、`weaponSequenceTick`、`weaponSequenceHitTicks` | 施法序列、Jab／Impale、双挥／Frenzy／双投的服务端命中与客户端动画共用8.8时钟及事件遍历。施法释放允许第0帧，武器事件仍至少第1 tick；序列内容、手位、动作及Leap Attack阶段保持独立 |
| `targeting::withinSkillArea` | Static Field、Telekinesis、尸爆／诅咒，连锁／范围／毒云弹体，人物／佣兵／精英光环共用几何。接口显式区分坐标截断、向下取整和连续距离；半径、房间活动、阵营、候选顺序及碰撞仍由调用者提供 |
| `targeting::skillAiControlAllowed` | Taunt与死灵AI诅咒共用switchAi、非boss、Normal／Minion资格；恐惧等级限制、附身者排除和吸引特殊覆盖仍在原调用点 |
| `content/skills/table_values` | 15个内容加载编译单元共用数字、身份引用及公式文本读取。缺列、空值、坏数字、允许空身份及引号处理按显式策略保留原拒绝／默认规则；它是内容辅助层，不进入玩法热路径，也不是通用Calc解释器 |
| `units/resources::resourceOnMaximumChange`、`conversionResources`、`conversionRestoredLife` | 最大资源回调保留先除后乘、int64截断后signed32窄化再限幅；转换先按整点百分比计算、最后左移8位，资源写入及回调由服务端候选执行。原圣骑士恢复上限不左移，刺客的另一回调左移，不合并两者；具体原1.13c依据与尚未完成的转换事件路径见[圣骑士](PALADIN.md#生命周期与边界) |
| `skills/native_damage`、`assassin_progressive`、`combat/weapon_block` | 原物理→元素掷值／写入、蓄气／吸取／鞋子踢伤／释放步骤与最多32参数层格挡查询；显式来源、接收者和种子，不持有世界或第二套层数。调用阶段、武学及普通攻击释放和双爪格挡的实现与原版依据见[刺客](ASSASSIN.md) |

核对范围包含七职业和通用／怪物／佣兵／宠物／物品／其他来源。已实现职业直接接入上述公共值；佣兵和物品来源沿既有resolve／aura／武器／弹体入口复用，怪物与精英复用几何和既有弹体计算。刺客已接真实职业执行与非玩家施法，复用公共动作／属性／状态／接收；德鲁伊已接真实职业及隐藏宠物程序，复用原生出生、接触、状态、资源与帧事件，并补入共享变形模式／速率及原生尸体资格。其他未支持程序仍明确拒绝，不为未来技能添加虚拟执行器、通用世界回调上下文或未核实的Calc解释器。

公共抽取本身不改变规则语义或D2S v96格式；全部职业、等级、来源及多人组合仍须按具体消费者核对。原函数职责与共享边界见[公共层](../../architecture/REFERENCE_DESIGN.md#6-d2moo的公共层究竟共用什么)，第一幕怪物接入与缺口归[第一幕](../monsters/ACT1.md)。

具体调用点和原版例外在职业逐项台账；数值／模块证据见参考设计的[女巫核对](../../architecture/REFERENCE_DESIGN.md#女巫公共层的原版核对)及[亚马逊核对](../../architecture/REFERENCE_DESIGN.md#亚马逊公共层的原版核对)。

### 诅咒、祭坛与战吼的公共生命周期

正文见[诅咒、祭坛与战吼的公共生命周期](EFFECTS.md#诅咒祭坛与战吼的公共生命周期)；本标题保留旧引用，规则只在负责页维护。

### 周期技能帧

正文见[周期技能帧](EFFECTS.md#周期技能帧)；本标题保留旧引用，规则只在负责页维护。

### 原时段属性输入

服务端幕环境时钟开始由world推进，AreaStore持唯一幕时钟，公共`advanceActEnvironment`／`actStatTime`只接受纯值；不取系统时间、不读取MPQ或使用Win32。幕在人物入场、进入请求或房间激活时建立，预准备地图不预先推进未加载幕。普通初始化为noon／rate128；黑暗太阳开始使用rate4、eclipse的sunrise初值，结束恢复noon／rate128，保留原nPrev。任务恢复、开始与祭坛完成已投递相同入口。

固定原1.13c D2Common.dll（MD5 `ee1238806ef6d6d9801d12a09d128fe1`）普通／第四幕／日蚀周期表RVA9B160／9B1A8／9B1F0，UpdateTicks在RVA37570，UpdateCycleIndex在37660。375A7第三幕夜间额外加8，与D2MOO转录加9不同；375D4–375F9未日蚀时选普通表，日蚀时才按act选择另外两表；周期重置仍按普通／日蚀表。公共算子保留这些原1.13c分支、严格大于下一周期阈值、超整圈归零，以及与上次通知度数差严格大于16的物品刷新边界，不用均匀日长或修正后的循环替代。

人物入场／事务、玩家资源轨迹、普通怪物与当前原始召唤初始化开始传入原幕时段。`StatList.sampledActTime`和`Totals.time`是该次属性计算的只读输入快照，查询缓存不推进幕时钟；新候选仍可显式传入当前输入。op6／7继续使用公共`statByTimeAdjustment`的原打包、15度取整和整数比例，手位视图、属性／被动刷新及来源重绑定保留该上下文。

幕通知开始按原act0→4逐项推进，某幕通知后的全部玩家刷新完成前不推进下一幕；attributes保存本次通知身份及最新入场者优先的玩家游标，通过人物事务重算原属性／手位和资源回调。当前幕的游戏中接收者在同一私有提交内追加不可变EnvironmentFact；hosting编码原53的cycle／ticks／eclipse。固定原1.13c D2Game RVA2CA40–2CA6B调用幕更新及原环境查询，2CADB–2CAE6对每位玩家更新物品并RefreshInventory，2CAEB–2CB19确认游戏中／同幕过滤及10字节53。循环无需等待当前玩家物品发生显式修改。

Simulation开始持有环境、已运行领域、交易与尾部阶段游标；环境刷新或交易输出背压时，GameInstance保留原逻辑帧，重试不再重放已处理命令或已完成领域阶段，等待中的新命令留到下一帧。入场和跨幕开始发送真实幕快照。黑暗太阳原ACT2Q3的固定周期5／2任务包独立保留，与连续幕时钟通知分别生产，不将其当作普通环境快照。人物装备刷新已接逐物品资格／投递／撤出与中间回调；完整时段／装备组合、佣兵等其余生产端、跨幕属性迁移、其余阶段的原事件优先级及完整帧游标仍待闭合，按时段装备全过程尚未完成。

## 定义、来源与客户端状态

选择器与技能树的 MPQ 描述程序、公式及截图证据由[技能描述](DESCRIPTIONS.md)维护，与执行和学习资格分开。

Skills／SkillDesc／CharStats、TBL及原树／图标提供ID、前置、手位／城镇资格与描述；显示节点不表示服务端已执行。`projectCharacterDisplay`使用原包等级和已知值，缺必要武器、协同、支配或描述程序时保持未知。普通、固有、物品授予和充能来源不能混用，源失效不退回同ID普通技能；充能及ItemEffect／ItemCltEffect限制由[物品技能](ITEM_SKILLS.md)维护。

本人已收到属性或完整解码的装备／状态可以用于FCR／IAS／FHR／FBR／Pierce显示，本人佣兵完整投影也参与计时；远端缺隐藏值时仅使用基础显示速率，不据画面推断权威断点。学习、选择、数量与热键的原包及等待条件统一见人物模块，保存字段和运行态排除项统一见SAVES。

## 动画帧时钟与恢复

`gameplay/combat/attack_timing`统一8.8固定点速度及25Hz整数帧。MPQ AnimData／COF提供帧数、速度和动作事件，宿主将纯值交给服务端。装备有效速率为整数`120×bonus/(120+bonus)`，先截断再合并。IAS合并技能AttackRate并扣武器速度，最终百分比限制15–175；双持基础武器速度在装备计算中取平均。FCR基础100、上限175，不叠加OtherAnimationRate；FHR／FBR基础50，圣盾的FBR基础100，按最终整数速度计算，不能用帧数的连续比例代替。

玩家普通动作时长为`max(1,ceil((frames-startFrame)×256/speed)-1)`，玩家序列时长为`ceil(frames×256/speed)`；事件按同一时钟释放，攻击至少第1 tick。Amazon／Sorceress普通攻击的起始帧奖励不传入重组序列。Jab／Impale、双挥／双投使用各自帧表和事件；Inferno／Leap固定速度、Leap Attack阶段及Strafe／Fend／Zeal回滚保持专用路径。客户端采样也量化到25Hz，序列不继承单段COF的起始帧或结束时间。

玩家`effects::Recovery::actionUntil`阻止恢复期间的新技能、移动和旅行，恢复成功后中断可中断动作并清路线。GH沿原伤害／生命／完整HitClass概率判断，元素／暴击高位不剥除；再次满足受击资格会重启GH。眩晕先于普通阈值／概率判断，玩家眩晕上限250tick、佣兵13tick；周期毒伤不播放GH。非可中断动作不被GH／BL／KB打断。格挡成功与播放BL分开，`lastBlockTick`及`FBR/8+15`间隔抑制连续BL，格挡判定仍有效。KB复用GH图形，玩家固定速度213、怪物取原WalkAnimSpeed，不使用FHR；击退成功也安装动作锁，怪物Smite的击退资格取原ResultFlags。伤害、恢复状态与随机数只在事务／输出成功后安装，容量失败保留原状态。死亡清恢复锁；这些时钟均不保存到D2S，不改变v96格式。原0x0D action19仍只校正生命／位置，GH／BL另发原action6／18，KB沿原action20。

怪物ENDANIM使用`monsterAnimationTicks`的`max(2,ceil((frames-startFrame)×256/speed))`，与玩家普通动作的减一规则分开；动画结束后的NU思考等待另按MonStats三难度aidel安排，依据与边界见[怪物动作时钟](../monsters/COMMON.md#动作死亡与奖励)。怪物与佣兵恢复使用MPQ原帧／速度及当前FHR／FBR，伙伴成功格挡也发布BL；近战、技能弹体及环境弹体统一调用monsters::block，容量失败不提交接触随机或推进目标。普通怪物、死灵召唤物、女武神和佣兵动作保留原整数帧，按当前装备／状态计算事件；Kick沿当前武器IAS／WSM。第一、二幕怪物MonSeq按行次序计时，不将显示帧、倒放或A1／A2切换换算成时间；SQ初始速度256，只有原Skills.UseAttackRate允许时才按IAS更新，不套玩家SQ的-30。普通SC走FCR，其他特殊模式按原UseAttackRate选择AttackRate或OtherAnimationRate，非攻击SQ保持固定时钟。佣兵Jab保留多事件，Inferno保持引导时钟。`server-snapshot.animationTiming`只读提供当前武器类下的基础动作速度、时长、事件tick及恢复锁，不是复制／存档来源，也不代表特殊序列的最终调度。

依据为当前MPQ、本地D2MOO `Units.cpp`／`SequenceTbls.cpp`速率与序列函数、`SUnitDmg.cpp`恢复／眩晕分支。本地1.13c `D2Common.dll`的`0x6FD7EFC0`确认玩家GH为mode4、怪物GH为mode3；`0x6FD80BD0`确认怪物按模式表继续查询UseAttackRate。重建代码分别误写BLOCK和遗漏monster分支break，不采用这些笔误；DLL版本摘要见[资料来源](../../resources/THIRD_PARTY.md#原版113c静态与开发对照证据)。刺客执行与德鲁伊变形转换现已接入，形态时钟入口为`gameplay/actions/shape_timing`与`skills/shape`；全部武器、装备、技能、难度及原服组合仍需分别对齐。

SceneController／RemoteCombat处理首次、Hold、锁定与停止，客户端动作来自原包或已发送请求的表现预测；服务端省略本人通知不表示没有施放。ClientSend是原弹体通知资格，不能解释成所有Clt程序都等待0x73；具体源配对与表现见攻击专题。客户端视觉碰撞不是原服命中证据。

鼠标松开发出的原C2S `0x12`只结束引导，不能当作通用施法取消。依据D2MOO `PlrMsg.cpp::D2GAME_PACKETCALLBACK_Rcv0x12_6FC84670`，它只清`STATE_INFERNO`；RemoteScene按当前表CltDo24／seq6识别已支持的引导程序，普通动作及待释放飞弹继续到原动作帧。动作结束清本人表现状态，已释放飞弹独立计时；移动、受击、死亡及场景代次变化仍走真正的中断入口。自研skills的Stop同样不删除普通攻击的待接近请求。

背包右键回城卷轴／书本通过`create_town_portal`发送原3C／0C，并与技能栏共用`record_combat`登记已发送施法意图，不能依赖通常省略的本人4C／4D。这只驱动动作表现；数量、落点、门对象及选择恢复仍来自原包，不自行创建门户或伪造确认。观察入口见[调试管道](../../development/DEBUG_PIPE.md)。

States／Overlay定义覆盖层和状态属性；客户端消费A7–AA，真实期限、互斥、反击／吸收、周期及死亡清理由服务端持有。晚入视野恢复已公开单位／状态／装备／归属，不重播历史施法／弹体。尸体资格、碎冰及未完成客户端程序见联网与对应职业专题。

覆盖图定位由`presentation/world/overlay_geometry.hpp`统一消费当前MPQ `Overlay.Xoffset/Yoffset/Height1..4`：玩家高度索引为1，怪物／NPC为`MonStats2.OverlayHeight-1`，物件为0；不用MonStats2.Height替代。索引-1沿原分支加75，0..3选对应Height，其余不加高度；Xoffset保持原符号。绘制从单位地面屏幕坐标加8，再叠加原表偏移及单位当前图形／抬升偏移，保留原DCC锚点；祭坛领取后的shrine状态标记、诅咒及其他已接状态覆盖图共用该入口。NPC感叹号也复用此定位，动画按`AnimRate×25/16`且量化25Hz。依据为本地D2MOO `UNITS_GetOverlayHeight`和原1.13c D2Client RVA6B300／6AEE0的高度选择、坐标与图形偏移分支；当前MPQ仍决定实际图形和高度，不按截图写死各效果位置。最近定位修正已通过Windows Release构建，运行仍待验收，全部覆盖图、跳跃与多人逐帧对照仍待验收。

原生救赎的尸体判定由`monsters::selectableCorpse`集中读取实际死亡动作、CorpseSel和Udead状态，不以经验／掉落结算完成代替死亡模式。玩家／非玩家的资源与种子、尸体REDEEMED及不可选标记通过同一候选提交。拉达门特的回收者是实际飞弹，求值和恢复不借用死亡怪物的资源；救赎及复活专用分支的完整原版行为仍待对齐。

## 原服施法参考

本机1.13c D2GS／PvPGN参考栈的冰冷之原观察：Fire Ball／Ice Bolt收到Stop后仍有后续弹体表现；使用回城卷轴可收到技能219施法和原59门户分配，物品数量从1变0。该混合连接使用D2X客户端，服务端消息可作为所列协议行为参考，客户端画面不等同于原版客户端画面。全部参数、逐帧／像素、音频与多人边界仍待核对；参考栈曾出现AF后加载超时，原因未确定。
