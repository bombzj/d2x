# 技能公共规范与实现边界

更新：2026-10-10。本页维护技能迁入规范、跨职业计算与状态所有权。原服和自研宿主使用同一客户端；客户端提交原请求、读取原包及已知属性，服务端拥有命中、消耗、效果和保存。旧单机执行器不恢复到客户端。

## 文档分工

| 内容 | 负责页面 |
| --- | --- |
| 通用规范、代码所有权、跨职业计算、整体技能状态 | 本页 |
| 原模块职责、DLL调用及公共提取的结构证据 | [参考设计](../../architecture/REFERENCE_DESIGN.md#6-d2moo的公共层究竟共用什么) |
| 全分类索引、逐技能原函数、专项算法、客户端差异与有限运行证据 | [技能目录](README.md)链接七职业、通用、怪物、同行者、物品及特殊条目的负责页 |
| 学习／选择资格、树／提示投影 | [人物](../../modules/CHARACTER.md)；原图布局见[HUD](../ui/CLASSIC_HUD.md) |
| 充能／装备触发、弹药／耐久及物品使用事务 | [库存](../../modules/INVENTORY.md) |
| 协议／客户端副本、动画／弹体表现 | [联网](../../modules/NETWORK.md)、[攻击与弹体](../combat/ATTACKS.md) |
| 当前包、保存格式／指纹、后续实施顺序 | [基线](../../../BASELINE.md)、[存档](../../modules/SAVES.md)、[总计划](../../architecture/MULTIPLAYER.md) |

职业页不重复公共规范、全局指纹或当前包状态；参考设计不再维护第二份职业实现台账。历史证据保留角色条件、源码／包身份、记录路径及未覆盖项，修复过程由Git追溯。

## 职业与证据

| 范围 | 当前自研服务端 | 客户端与验证边界 |
| --- | --- | --- |
| 通用0–5、217–220 | 十项执行入口；Kick是隐藏破桶程序 | 武器／药瓶／物品技能／Unsummon代表路径有限V2；普通桶KK及左手等未覆盖路径V1，见GENERAL |
| 女巫36–65 | 26项主动、4项被动 | 原Clt表现已接；历史自研、同机TCP及原服代表路径有限V2，见SORCERESS |
| 亚马逊6–35 | 24项主动、6项被动 | 原Clt表现及宠物归属／装备已接；历史代表路径与保存往返有限V2，女武神死亡图等缺口见AMAZON |
| 圣骑士96–125 | 10项战斗技能、20项光环及随状态切换的被动贡献 | 单机30项学习／状态、战斗及保存恢复有限V2，原版D2GS三个代表技能有限V2；依据和组队／PvP／Holy Shield特殊盾图限制见PALADIN |
| 死灵 | 职业主动尚未迁入；定义、纯提示或部分基础被动不代表执行 | 通用请求／树／提示及部分原Clt程序；专题列出明确缺口 |
| 野蛮人／德鲁伊／刺客 | 职业执行尚未迁入 | 共用定义／请求／树／提示；专题已预留，原图或文档存在不表示技能执行完成 |
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

新增技能若复用既有程序，只准备当前MPQ规则与资格；新增执行程序需登记策略并实现领域操作，新增CltDo需登记能力并实现其逐帧／接触／到期行为。数据、执行与表现支持分别维护，不能通过目录注册宣称新职业完成。本次已随佣兵批构建、打包，四类佣兵代表技能取得有限单机证据；完整程序／来源组合与重构边界未认证；配置与产物身份见基线，以前的有限V2不认证此次重构。

1. 先读master已实现的定义／执行与当前公共函数，复用可用规则；耦合GameSession的部分只迁规则和必要状态。
2. 依据当前MPQ登记Skills的SrvSt／SrvDo、Missiles的Do／Hit／Dmg、动作事件和状态。用本地D2MOO定位职责与缺漏；1.10f重建、1.13c DLL和当前表的差异分别记录，参考附带参数不覆盖MPQ。
3. 提取真正相同的纯计算，明确基础／有效等级、来源、取整、帧相位、种子归属和实体／区域代次；返回纯值、下一小状态或动作描述，两端分别创建权威／视觉对象。Clt／Srv参数、触发条件和随机顺序有原版差异时保留，不能为代码一致抹平。
4. 服务端区分开始资格和释放复验，资源／数量／耐久／充能与事实按事务提交。背压保留首次准备结果，不重复扣费、掷值或命中；异步内容准备绑定请求并只预留一次独立种子。未准备或未知规则明确拒绝。
5. 客户端只改有原版依据的公共提取和修复，每处记录MPQ／原包／原函数依据。禁止为兼容自研宿主另写客户端玩法分支、私有确认消息或推算远端隐藏值。
6. 更新职业台账及明确缺口；保存语义变化同步SAVES与规则指纹。构建／打包／运行仅按有效授权，不编写测试脚本、用例或专用程序；源码核对、构建、有限运行和原版验收分别记录。

## 跨职业公共计算

2026-10-10源码依赖边界：`server/skills/system.hpp`不暴露私有施法／怪物释放结构，它们位于仅技能实现使用的`runtime.hpp`；求值、支配、充能源逻辑移入`evaluation.cpp`，公共头只声明函数。普通技能实现改动限定在对应编译单元，私有队列布局改动限定在技能模块；共享规则结构变化仍会影响真正使用这些类型的调用者。参与者关系通过轻量声明读取，不包含PlayerStore／monsters系统头。公共战斗规则、提交阶段与运行限制见[内核](../../modules/SERVER_SYSTEMS.md#战斗关系与提交)。本批未构建，未实测增量编译耗时。

| 计算入口 | 必须保留的语义 |
| --- | --- |
| `resolve`、`rank_sources`、`rank_bonus`、`passive` | 基础等级用于协同，来源的有效等级用于施放；安装属性贡献仍归服务端 |
| `damage_curve` | 原法力定点曲线与MinMana消耗分开；技能元素伤害先HitShift再协同，独立弹体先协同再HitShift，随后应用支配；8.8最小值≤256且首级增量为0时跳过协同 |
| `cast_timing`、`amazon_sequence`、`weapon_volley` | 原AnimData事件、SC／seq6／seq12及武器序列／回滚；CAST按整数求min(100+120×FCR/(120+FCR),175)，不叠加OtherAnimationRate；Inferno seq6固定速率 |
| `projectile_path`、`amazon_missile`、combat几何 | 速度、格点、环射／扇形、remaining调度、引导／连锁／分裂等纯值；各端持有候选、时钟、碰撞及创建队列 |
| `weapon_damage`、`poison`、`avoidance` | 武器六通道／转换、显式随机、毒替换及近远程／移动回避；不因纯函数可用就授权客户端结算 |

具体调用点和原版例外在职业逐项台账；数值／模块证据见参考设计的[女巫核对](../../architecture/REFERENCE_DESIGN.md#女巫公共层的原版核对)及[亚马逊核对](../../architecture/REFERENCE_DESIGN.md#亚马逊公共层的原版核对)。

## 定义、来源与客户端状态

Skills／SkillDesc／CharStats、TBL及原树／图标提供ID、前置、手位／城镇资格与描述；显示节点不表示服务端已执行。`projectCharacterDisplay`使用原包等级和已知值，缺必要武器、协同、支配或描述程序时保持未知。普通、固有、物品授予和充能来源不能混用，源失效不退回同ID普通技能；充能及ItemEffect／ItemCltEffect限制由[物品技能](ITEM_SKILLS.md)维护。

本人已收到属性或完整解码的装备／状态可以用于FCR／IAS／Pierce显示；远端缺隐藏值时不猜测。学习、选择、数量与热键的原包及等待条件统一见人物模块，保存字段和运行态排除项统一见SAVES。

SceneController／RemoteCombat处理首次、Hold、锁定与停止，客户端动作来自原包或已发送请求的表现预测；服务端省略本人通知不表示没有施放。ClientSend是原弹体通知资格，不能解释成所有Clt程序都等待0x73；具体源配对与表现见攻击专题。客户端视觉碰撞不是原服命中证据。

鼠标松开发出的原C2S `0x12`只结束引导，不能当作通用施法取消。依据D2MOO `PlrMsg.cpp::D2GAME_PACKETCALLBACK_Rcv0x12_6FC84670`，它只清`STATE_INFERNO`；RemoteScene按当前表CltDo24／seq6识别已支持的引导程序，普通动作及待释放飞弹继续到原动作帧。动作结束清本人表现状态，已释放飞弹独立计时；移动、受击、死亡及场景代次变化仍走真正的中断入口。自研skills的Stop同样不删除普通攻击的待接近请求。

背包右键回城卷轴／书本通过`create_town_portal`发送原3C／0C，并与技能栏共用`record_combat`登记已发送施法意图，不能依赖通常省略的本人4C／4D。这只驱动动作表现；数量、落点、门对象及选择恢复仍来自原包，不自行创建门户或伪造确认。通用修复的历史代表冒烟见下节；观察入口见[调试管道](../../development/DEBUG_PIPE.md)。

States／Overlay定义覆盖层和状态属性；客户端消费A7–AA，真实期限、互斥、反击／吸收、周期及死亡清理由服务端持有。晚入视野恢复已公开单位／状态／装备／归属，不重播历史施法／弹体。尸体资格、碎冰及未完成客户端程序见联网与对应职业专题。

## 通用施法表现有限证据

2026-10-09 Windows Release、规则v27／D2S v96；EXE SHA256 `BF767C41680FAD44DD825B8BD877D82D30629C03CCB0B45A79038243FD55A52A`，DLL `9F7B1FAF7AF453E3DC481F05207C76460AECC38FF33A50E92823032228B2969F`。证据：`artifacts/skill-presentation-20261009`，只使用既有客户端／调试入口。

- Single Player实际UI快速按下／松开后，Fire Ball62、Ice Bolt59、Frozen Orb260／261仍生成／移动／散射，有原图截图；火球将管理准备的zombie1生命7→0。Inferno有10个视觉弹体，Stop后施法／弹体／pendingReleases清零；背包卷轴消费并创建蓝门。最终failures／ignoredPackets为0，mapErrors／effectLimitations及stderr为空。
- 同包连接本机1.13c D2GS／PvPGN，在冰冷之原复查Fire Ball／Ice Bolt的Stop后表现；背包开门取得219施法、数量1→0及原59门户证据。原服既有ignoredPackets=16；此前一次AF后加载超时，重新选角恢复，原因未认定修复。

仅认证列出路径，不覆盖全部职业／参数、多人、三难度、精确逐帧／像素或Linux，也不认证后续源码。最新运行包身份见基线。
