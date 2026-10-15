# 公共规则与原版算法设计

## 1. 原则与范围

本文负责公共函数的提取粒度、输入输出、原版算法采用方式及精确性契约。[领域设计](COMBAT_DOMAIN_DESIGN.md)负责状态与模块，[总纲](SERVER_COMBAT_RECONSTRUCTION.md)负责完整复刻范围、证据索引和验收。本文不重复各职业完成清单，也不规定当前文件怎么搬迁。

**有 D2MOO／同版本原版依据的算法，优先保留其计算步骤、条件分支和数据语义，而不是凭经验另造近似公式。** 可准确适配的原公共函数应当直接成为设计依据；位于 D2Game 的纯计算也可以抽取。函数所在 DLL 既不是能否共享的充分条件，也不是限制进一步共享的理由。

“直接参考”是保留算法，不是盲目复制 Win32 ABI、全局表、裸指针和原内存布局。原函数同时计算与写状态时，应保留原控制顺序，分离显式输入、局部算法和领域提交；不能以现代化为由删掉难以解释的有效分支。

本地固定参考为 D2MOO `5596f5cb6c5251a0a07c6637d26458b06099d516`，主要基于 1.10f；目标为经典资料片 1.13c。参数来自当前 MPQ，差异按同版本原证据处理。许可和适配归属遵守[资料来源](../resources/THIRD_PARTY.md)，不将参考仓库、资源或原 DLL 纳入发布。

## 2. 四种复用层次

| 层次 | 函数形态 | 适合共享的内容 | 状态归属 |
| --- | --- | --- | --- |
| 无状态计算 | 明确参数 → 值／有类型错误 | 比例、曲线、距离、命中率、需求、减免 | 不持状态 |
| 局部状态推进 | 规则＋局部状态＋输入 → 下一状态＋局部结果 | 动作相位、弹体方向／速度、有限路径、随机递推 | 调用者拥有状态；两端各有实例 |
| 领域纯规划 | 已捕获事实＋规则＋必要随机 → 计划 | 属性贡献、生成物品、召唤属性、费用、效果覆盖 | 计划不是权威，领域复验与提交 |
| 服务端共享机制 | 窄读端口＋权威上下文 → 有序执行 | 来源解析、目标枚举、触发、准入、尸体竞争 | 仅服务端，多个内容族复用 |

不要求共享函数全为数学纯函数；要求副作用范围显式、状态归属清楚。性能需要时可以传入仅由调用者拥有的局部状态引用／输出缓冲，不能因此允许访问整局世界。最后一类不为客户端开放，也不伪装成 gameplay 公共查询。

## 3. 原函数采用等级

| 等级 | 采用方式 | 需要完成的核对 |
| --- | --- | --- |
| 算法直接适配 | 原步骤与分支明确、输入可独立提供，保持整数语义 | 原函数实现、直接调用上下文、单位／范围、版本差异及许可 |
| 拆分后适配 | 原函数混合计算、stat 查询、世界修改或实体创建 | 逐段读写／随机／回调顺序；不能改变采样与提交时点 |
| 结构参考 | 提供原职责、数据关系与执行阶段，但尚有未知语义 | 列出待核实控制分支，不照抄可疑返回值作产品默认 |
| 暂缓 | 必要输入／算法／版本证据缺失 | 明确受影响定义与程序，禁止近似替代成功 |

源码可读不等于同版本行为已认证；但不要求对每个已明确的原语句重复逆向。以行为风险和版本差异决定需要补的证据：纯整数步骤可直接适配并保留来源；跨版本时序、未明字段、异常边界和客户端专用分支应重点裁定。

参考本身的 bug 与目标游戏的 bug 分开：前者不能为了“忠实”复制，后者若属于目标有效行为则不可悄悄修正。安全防护拒绝非法输入与改变合法输入结果也是两回事。

## 4. 公共算法目录

以下是**设计候选与读取入口**，不是声明全表函数已逐项移植或通过 1.13c 认证。已展开的控制链见[总纲证据地图](SERVER_COMBAT_RECONSTRUCTION.md#2-d2moo-证据地图)；已有同版本专项证据见[参考设计](REFERENCE_DESIGN.md)。输入输出列是建议契约，不是原函数的原样签名。

| 算法族 | D2MOO 直接入口 | 公共输入／输出 | 采用边界 |
| --- | --- | --- | --- |
| 原随机 | [D2Seed.h](../../reference/d2moo/source/D2Common/include/D2Seed.h)：SEED_RollRandomNumber／LimitedRandomNumber | 双字种子／范围 → 下一种子／结果 | 优先直接适配；流归属、派生时点由调用者决定 |
| 整数比例 | [MonsterUnique.cpp](../../reference/d2moo/source/D2Game/src/MONSTER/MonsterUnique.cpp#L234)：MONSTERUNIQUE_CalculatePercentage | 有符号数／比例／分母 → 原分支结果 | 在 D2Game 也可共享；不可无依据统一成宽乘除 |
| 技能参数／公式 | [D2Skills.cpp](../../reference/d2moo/source/D2Common/src/D2Skills.cpp)：SKILLS_GetSpecialParamValue／EvaluateSkillFormula | 程序＋等级／stat 查询输入 → 整数值 | 拆出显式参数环境；避免用万能单位指针 |
| 技能数值曲线 | [D2Skills.h](../../reference/d2moo/source/D2Common/include/D2Skills.h)：GetManaCosts、GetMin／MaxElemDamage、GetElementalLength 等 | 原定义＋来源等级＋协同／支配 → 原单位数值 | 耗蓝计算与实际扣蓝、技能伤害与飞弹伤害不混用 |
| 飞弹伤害继承 | [Units/Missile.h](../../reference/d2moo/source/D2Common/include/Units/Missile.h)：CalculateDamageData／CalculateFinalDamage／CalculateMasteryBonus | 原飞弹／技能规则＋发射来源数值 → 分通道伤害数据 | 将查属性和写弹体 stat 分离；保存 SrcDamage 与继承顺序 |
| 飞弹公式／寿命 | [Units/Missile.cpp](../../reference/d2moo/source/D2Common/src/Units/Missile.cpp)：公式、曲线及 remaining 访问 | 定义＋局部帧／等级／上下文 → 数值／相位 | 原 current、total、remaining 语义不能按名字猜；随机公式显式传流 |
| 动画模式／速率 | [Units.cpp](../../reference/d2moo/source/D2Common/src/Units/Units.cpp)：UNITS_UpdateAnimRateAndVelocity 及模式分支 | 形态、原模式、武器上下文、速率属性、元数据 → 动画／移动速率 | 拆出选择与计算；模式准入和写位置不属于纯计算 |
| 序列与动作帧 | [SequenceTbls.cpp](../../reference/d2moo/source/D2Common/src/DataTbls/SequenceTbls.cpp)、Units 的序列推进 | 序列、旧相位、速率／回滚策略 → 新相位／模式／事件 | 客户端显示与服务端消费事件可共享推进；各自持状态 |
| 几何／路径局部运算 | [PathMisc.cpp](../../reference/d2moo/source/D2Common/src/Path/PathMisc.cpp)、[Units.cpp](../../reference/d2moo/source/D2Common/src/Units/Units.cpp)、[D2Collision.cpp](../../reference/d2moo/source/D2Common/src/D2Collision.cpp) | 坐标、单位尺寸、掩码、明确地形视图 → 距离／方向／接触 | 候选世界与动态占位归各端，不能把通用欧氏距离替换原距离 |
| 属性查询／聚合 | [D2StatList.cpp](../../reference/d2moo/source/D2Common/src/D2StatList.cpp) | StatKey＋基础／来源列表＋op 规则 → 相应查询值 | 原列表修改不是纯函数；列表语义保留，存储结构可重设计 |
| 被动贡献 | [D2Skills.cpp](../../reference/d2moo/source/D2Common/src/D2Skills.cpp)：SKILLS_RefreshSkill | 来源等级、被动定义、抑制状态 → 贡献增删计划 | 属性安装／移除、事件订阅留权威领域 |
| 物品属性程序 | [ItemMods.cpp](../../reference/d2moo/source/D2Common/src/Items/ItemMods.cpp)：ItemAssignProperty／PropertyFunc | 属性步骤、物品上下文、种子 → 原始 stat／实例变化计划 | 首函数结果复用、多步顺序和副作用必须保留 |
| 物品需求／耐久／价格 | [Items.cpp](../../reference/d2moo/source/D2Common/src/Items/Items.cpp) | 物品、角色／商店已知条件、规则 → 需求／上限／报价 | 报价能共享，真实买卖／维修授权与提交不能共享 |
| 品质／词缀／生成 | [ItemsMagic.cpp](../../reference/d2moo/source/D2Game/src/ITEMS/ItemsMagic.cpp)、[D2Game Items.cpp](../../reference/d2moo/source/D2Game/src/ITEMS/Items.cpp) | 规则、等级／类型、资格快照、种子 → 生成计划 | 真实唯一性、TC 发生、库存和世界安装在服务端 |
| 命中／伤害算子 | [SUnitDmg.cpp](../../reference/d2moo/source/D2Game/src/UNIT/SUnitDmg.cpp) | 双方类别／属性、攻击数据、显式掷数 → 中间结果 | 拆出数值算子；不能把带事件／生命修改的全函数当纯计算 |
| 范围／光环几何与谓词 | [Skills.cpp](../../reference/d2moo/source/D2Game/src/SKILLS/Skills.cpp)、[SkillPal.cpp](../../reference/d2moo/source/D2Game/src/SKILLS/SkillPal.cpp) | 原过滤标志、几何、已知关系／能力 → 资格 | 关系事实和世界枚举由调用者提供；未知不是友方 |
| 飞弹族局部程序 | [MissMode.cpp](../../reference/d2moo/source/D2Game/src/MISSILES/MissMode.cpp) | Do／Hit 参数、remaining、局部状态、必要随机 → 轨迹／发射描述 | 可抽周期／环射／转向；保留 Srv／Clt 差异，真实准入由服务端 |
| 怪物数值／精英贡献 | [MonsterMode.cpp](../../reference/d2moo/source/D2Game/src/MONSTER/MonsterMode.cpp)、[MonsterUnique.cpp](../../reference/d2moo/source/D2Game/src/MONSTER/MonsterUnique.cpp) | 原身份／难度／rank／规则 → 数值或贡献计划 | 初始化、受击和死亡程序不统合为一个增强系数 |
| 召唤数值／配额规划 | [Skills.cpp](../../reference/d2moo/source/D2Game/src/SKILLS/Skills.cpp)、[PlayerPets.cpp](../../reference/d2moo/source/D2Game/src/PLAYER/PlayerPets.cpp) | 主人已知属性、技能／宠物定义、现有配额 → 数值／替换计划 | 不自动复制主人全部装备；世界出生、替换和材料消费需提交 |
| AI 家族决策 | [AiThink.cpp](../../reference/d2moo/source/D2Game/src/AI/AiThink.cpp)、[AiTactics.cpp](../../reference/d2moo/source/D2Game/src/AI/AiTactics.cpp) | 感知、家族状态、原参数、随机 → 意图／等待／下一状态 | 通常只在服务端多处复用，不要求客户端暴露隐藏感知 |

## 5. 函数提取单位

### 当前源码适配边界

- [动作计算](../../src/gameplay/actions/timing.hpp)、[武器时序](../../src/gameplay/actions/weapon_timing.hpp)、怪物时序、恢复与连击回滚沿用既有纯计算；依据为Units／SequenceTbls及服务端动作路径，不代表全部模式或序列已复刻。
- [整数比例](../../src/gameplay/rules/arithmetic.hpp)适配DATATBLS_ApplyRatio／MONSTERUNIQUE_CalculatePercentage的分支与窄化，并区分普通32位乘除及D2StatList的浮点百分比。技能／飞弹曲线分别保留协同与HitShift顺序；`Fixed8`持权威资源与伤害，显示转换不反写权威值。[通道接收](../../src/gameplay/combat/reception.hpp)按SkillSor能量护盾的物理、火、电、冰、魔法顺序共享法力，再处理来源允许的生命／法力／耐力损失。分通道减免保留原负中间量和吸收，再以原32位语义求和；来源类别比例在减免前应用，穿抗在上限前处理。吸收回血先封顶，再扣伤害；正伤害扣减后不足256单位归零，毒率参与接触总伤害后安装持续毒。完整原事件订阅顺序、来源采样时点及全部技能／飞弹程序仍未完成；这些公共数值及飞弹／周期切片已纳入Windows Release构建，有限观察及未覆盖范围以[运行包记录](../development/BUILD_AND_RUN.md#运行包记录)为准。固定本地1.13c DLL的新增反汇编核对只证明已引用控制分支，不认证完整运行结果。
- [飞弹伤害区间](../../src/gameplay/missiles/damage.hpp)依据MISSMODE_RollDamageValue保留非正值判断、倒置区间交换及整数范围抽样。普通非武器弹体创建时保存区间，在接触候选中逐目标抽样并保存结果，背压不重掷；区间缩放／相加使用整数。当前[飞弹伤害数据](../../src/gameplay/missiles/damage_data.hpp)拆分原范围继承、属性写入和命中投掷；范围投掷及雷云风暴的限定入口开始接入。武器、怪物及其余直接／持续伤害尚未全部迁入，创建与命中的采样时点、支配及来源生命周期仍待收尾。移除创建时浮点伤害抽样改变了随机消费顺序，不宣称已恢复完整原版随机序列。
- 圣光弹按MISSMODE_SrvHit07_HolyBolt_FistOfTheHeavenBolt区分友方治疗和敌方伤害抽样，治疗非正时才回退到伤害区间。当前仍依赖既有友方资格与技能准备值，未由此补齐全部HitPar策略。环境伤害对非玩家的吸收回血已交给同一次怪物伤害提交，不再扣血后单独回血。
- `effects::unitStatValues`共用属性／当前资源查询：缓存FullStats提供派生属性，玩家生命／法力／耐力和非玩家生命从各自唯一可写资源持有者覆盖，避免回收弹体等消费者误把缺省值零当作当前资源。单位视图不另持可写生命副本。
- 非玩家普通接触同样复用不足一点资源归零规则，持续毒伤／撕裂伤口的逐帧扣血及城镇佣兵下限保持独立分支。来源绑定变化后的后续吸取、转换和反冲仍沿既有取消策略，尚未认证全部跨帧背压组合。
- [表达式核心](../../src/gameplay/rules/formula.hpp)按Fog的原运算优先级、左结合幂、两支条件求值、除零和32位回绕处理，显式随机入口按原闭区间规则消费调用者种子，求值使用64槽操作数栈。描述接口使用相同核心、缺随机输入则返回未知。它不是原字节码ABI；完整战斗参数目录、属性查询上下文及准备器迁移仍属后续任务，未经运行认证。
- [单位整数距离](../../src/gameplay/units/distance.hpp)依据AiUtil的带尺寸与不带尺寸两种计算；调用者保留各自坐标转换，不能据此统一floor与截断。
- [属性程序](../../src/gameplay/items/property_program.hpp)、[技能来源](../../src/gameplay/skills/use_source.hpp)、效果查询及装备贡献说明已从内容／执行器拆出；没有新增原本不支持的属性函数或触发程序，也没有宣称完整StatList语义。 [StatList](../../src/gameplay/stats/stat_list.hpp)已补入writeNativeBaseStat及propagateNativeStatDelta：基础零值删除，派生FullStats按KeepZero保留；父级按原reverse-op／op-base依赖顺序逐项写入并通知，存储raw值与MinAccr查询值分别表达。装备的完整列表投递／撤出、套装全局重写和玩家被动已开始使用该入口，状态和怪物模式也修正基础零值语义；人物与佣兵已开始保存本件及单位不可变FullStats；普通查询读取缓存，itemStatsAfterUnitChange在挂接／解除时显式刷新item-applying op，hasUnit与hasOwner分开表达原pUnit／pOwner。人物原地属性／充能变化开始逐层传播原delta，主手切换仅更新DamageRelated项。[技能属性回调](../../src/gameplay/skills/stat_callbacks.hpp)区分本技能、全部被动、充能和光环；玩家候选已开始接前三区分，单技能新条目保留基础1→0两次刷新，反向op传播支持读取回调更新后的来源树，技能链顺序来自CharStats及原追加行为。光环激活／撤销已接属性逐写回调、状态来源及计时器候选；装备光环的原PeriodicStats事件驱动已接入，完整原交错与来源组合仍未验收，见[当前持续效果入口](../gameplay/skills/COMMON.md#当前持续效果入口)。[技能装备／恢复](../../src/gameplay/skills/equipment.hpp)承载纯配对手位和选择恢复门，原MPQ flags及类型组由宿主准备；选择清理事实随属性候选提交，库存边界按原同ID／GUID及NoLevel／NoQuantity条件恢复，法力／冷却不代替该门。特殊基础动作仍需真实主武器GUID，缺少时明确Unknown。全部缓存延续、未投递物品、使用中技能、单位事件和特殊来源仍未迁移完整。具体来源边界与DLL入口见[装备](../gameplay/items/EQUIPMENT.md#当前执行)。

以上为源码状态和证据边界，不是构建、运行或全内容验收记录。

### 重点战斗数值的当前公共入口

下表区分原速率／数值算子与完整执行路径。普通输入的速率步骤吻合，不代表全部形态、手位、序列、动作准入及帧事件已经对齐；FAS按攻击速度理解，装备属性为IAS，转换后的有效值为EIAS。表中除法均为整数除法，动画基础速度来自准备好的原动画元数据。

| 数值 | 当前公共入口 | 原依据与静态结论 | 仍需收拢的边界 |
| --- | --- | --- | --- |
| FHR | `actions/timing::effectiveRecoverySpeed`、`actions/recovery::recoveryAnimationTiming` | `UNITS_UpdateGetHitAnimRateAndVelocity`：先求`120*FHR/(120+FHR)`，再以`50+有效值`缩放基础动画速度；支持输入内步骤吻合 | 是否触发GH由`monsters/damage::monsterHitRecovery`另判；玩家／怪物完整恢复时钟、模式与中断不是速率函数的输出 |
| FCR | `actions/timing::effectiveCastSpeed`、`skills/cast_timing::normalCastTiming` | `UNITS_UpdateCastAnimRateAndVelocity`：先求有效FCR，再以不超过175的`100+有效值`缩放基础速度；支持输入内步骤吻合 | 序列是否消费FCR、起始相位、动作事件与全部形态尚未统一；原总帧数和原动作帧分别参与计算 |
| FBR | `actions/timing::effectiveRecoverySpeed` | `UNITS_UpdateBlockAnimRateAndVelocity`：普通基础率50，Holy Shield基础率100，再加有效FBR；支持输入内步骤吻合 | 格挡资格、连续格挡是否播放BL和恢复期限由权威执行另判 |
| IAS／EIAS | `actions/timing::effectiveAttackSpeed`、`actions/weapon_timing`、`actions/volley` | `UNITS_UpdateAttackAnimRateAndVelocity`的有效IAS及15–175速率限制已有公共入口；序列减30由调用者提供，装备层已处理一部分双持基础速度 | 原函数还合并双持stat并选择形态基础速度；当前显式参数不能证明这些输入、全部手位和序列回滚均已一致 |
| 变形模式／攻击基础速度 | `actions/shape_timing::shapeMonsterMode`／`shapeAttackSpeed`、`skills/shape` | 原玩家→怪物模式表／回退，`D2Common_11043`与`ITEMS_GetWeaponAttackSpeed`；武器自身速度先形成原人形A1周期，再求形态基础速度并调用共享EIAS | 当前MPQ狼／熊已接；全部oskill／双持／手位组合仍需实证，不能用人形帧数代替形态帧数 |
| 单位距离与尸体范围 | `units/distance::unitDistanceWithSizes`／`unitCoordinateDistanceWithSize`、`companions/druid_corpse` | `UNITS_GetDistanceToOtherUnit`先减双方半尺寸并逐轴截零；AI FullUnitSize保留单侧尺寸的原绝对值；UnitFind尸体范围另用平方圆判定 | 世界候选与资格属于服务器，不能把以上三种距离合并为一个欧氏距离或通用最近目标 |
| 技能／飞弹基础伤害、耗蓝、长度 | `skills/damage_curve` | `SKILLS_CalculateDamageBonusByLevel`、`SKILLS_GetManaCosts`、`SKILLS_GetMin/MaxElemDamage`、`MISSILE`对应曲线；保留技能／飞弹不同缩放次序与最小伤害分支；伤害／费用用原8.8值 | 帧时钟迁移、完整公式上下文和程序角色仍未完成；当前具名准备器不是全公式覆盖 |
| 百分比、抗性、吸收、护盾 | `rules/arithmetic`、`combat/mitigation::mitigateDamageChannels`、`combat/shield`、`combat/reception` | 原算术分为百分比／普通乘除／stat百分比，玩家及非玩家接收消费共同六通道算子，保留负中间量 | 护盾已接技能事件登记与伤害前回调；完整事件交错、Hit／Result标志和未迁程序仍未闭合，已有飞弹／周期构建不能认证完整DMG链精确 |

对应原函数集中在[D2MOO Units.cpp](../../reference/d2moo/source/D2Common/src/Units/Units.cpp)、[D2Skills.cpp](../../reference/d2moo/source/D2Common/src/D2Skills.cpp)、[SUnitDmg.cpp](../../reference/d2moo/source/D2Game/src/UNIT/SUnitDmg.cpp)及[MonsterUnique.cpp](../../reference/d2moo/source/D2Game/src/MONSTER/MonsterUnique.cpp)。服务端和表现层已经调用相同的常见动作速率函数，客户端仍只消费已知输入。公式、完整动作推进和完整伤害流程分别按各自原契约继续核对，不将这些算子推广为所有模式的默认成功实现。

### 5.1 先找稳定语义，再找重复代码

值得独立的单位应同时满足：输入可命名、输出有业务含义、原分支可描述、隐藏依赖可以列全、调用者不需复制内部规则。共享伤害曲线可以保留技能／飞弹不同入口，共享序列推进可以接收不同动作策略；不能为了减少行数把语义不同的公式强行合并。

同一算法即使暂时只有一个调用者，也可以因未来明确的玩家／怪物／客户端使用而放公共层；但没有明确规则边界的十行代码不必抽成工具。原函数很大时按真正的计算与副作用边界拆，而不是每几十行拆一个函数。

### 5.2 推荐契约形态

下列为语义签名，不是当前 API 或要求立刻生成代码。

```text
evaluateFormula(program, typedInputs, arithmeticPolicy) -> Value | Error
evaluateRandomFormula(program, typedInputs, randomState) -> ValueAndRandom | Error
resolveSkillRank(source, learnedRanks, effectiveBonuses, rules) -> RankBreakdown
advanceAction(definition, cursor, modePolicy, rateInputs) -> ActionAdvance
resolvePropertyProgram(program, itemContext, randomState) -> PropertyPlan
aggregateStats(base, contributions, queryContext, rules) -> StatResult
prepareMissileDamage(definition, skillInput, sourceInput) -> MissileDamageData
advanceMissileMotion(definition, state, geometryInput) -> MotionAdvance
resolveDamageStage(stageRule, damage, source, target, reactionInput) -> StageResult
planEffectChange(definition, source, existingEffects, frame) -> EffectPlan
```

签名中的上下文不是可任意查询的世界对象。`typedInputs` 应按该程序所需提供具名值或受限只读属性／定义访问；不隐藏文件、网络、分配单位或世界遍历。`arithmeticPolicy` 是规则包绑定的兼容语义，不是调用者随意选择“更精确”或“更快”结果的选项。

只读目录接口可以避免复制所有数据，但不能把 `getAnything(string)` 重新包装成弱类型服务定位器。热路径可以借用小 span／窄视图；异步计划必须捕获可稳定解释的值或带版本句柄。

### 5.3 返回值与错误

合法零值、未满足资格、输入未知、规则未支持、数据损坏和容量不足分别表达。完整服务端输入缺失一般是准备／契约错误，不应该继续近似结算；客户端缺隐藏输入则可以是合法 Unknown，由显示适配器处理。

不要把所有函数都返回复杂计划：单值计算返回值，多个相关结果返回小结构，只有真正涉及多个领域效果才产出类型化计划。边界清晰不等于每个整数操作都需要虚接口、堆分配或错误字符串。

## 6. 精确性契约卡

每个真正采用的公共算法在负责专题或紧邻设计记录中维护以下内容。原来源精确到文件与符号，必要时含同版本 RVA；不为每个小函数建立独立文档，也不新建第二份职业完成台账。

| 契约项 | 必须说明 |
| --- | --- |
| 身份 | 原函数／程序角色、参考提交、适用版本、目标算法名 |
| 输入 | 原始单位、位宽／有符号性、等级来源、必要属性层、缺失策略 |
| 输出 | 原始量而非显示量、范围、错误与不适用区别 |
| 运算 | 分支、取整、shift、乘除顺序、溢出／截断、钳位与除零 |
| 时间 | 属性采样点、当前帧／remaining 含义、是否读取旧状态 |
| 随机 | 所有者、调用顺序、范围端点、哪些分支消费、父子种子 |
| 副作用 | 原代码的写入、回调、分配；目标哪些改为计划，何时安装 |
| 使用者 | 服务端哪些机制，客户端哪些已知输入；并不要求两端都使用 |
| 证据 | 已读控制链、同版本静态／实际观察、差异与未验证范围 |

### 6.1 动作推进示例

依据是 Units 的初始化／推进、SequenceTbls 的映射和模式消费者，而不只是动画帧数。输入区分序列相位、图像帧、剩余长度、速度、帧奖励／回滚及模式；输出保留原事件的覆盖与消费语义。服务端不能从图像回调发伤害，客户端不能用可见动作事件写生命。

如果经过多个图像帧只保留最后一个原事件，这是应核对的原控制语义，不应因为“事件列表更完整”而擅自改成多次攻击。相反，原 MonSeq 或连击确实有多个释放时，也不能由单个 releaseTick 丢失它们。

### 6.2 属性程序示例

依据是 ItemAssignProperty 顺序调用与首结果复用，以及各 PropertyFunc 的随机／stat／实例修改。输入应含已解析字段、物品等级／类型／上下文和正确种子；输出不是只有 stat 数组，还可能包含初始化耐久、无形或其他实例变化。展示层只读最终已知属性，不重新执行生成随机。

### 6.3 伤害算子示例

抗性／吸收可以拆成纯计算，但它的输入必须是正确阶段的伤害和抗性，包括类别缩放、护盾反应、穿透资格等已完成的前置；调用者不能因函数独立便调换这些步骤。ColdMastery、破免、固定减伤、时长与毒速率的准确组合仍由原路径决定，不把全部规则写成一条乘法链。

## 7. 公式核心与属性查询

公式核心处理已核实的运算、参数引用和控制结构；技能、飞弹、物品和描述的参数环境分别定义。既可共用解析／执行基础，也不必假定同名变量在所有环境都相同。技能／飞弹原曲线有明确差异时保留不同函数，仅复用真正一致的步骤。

公式编译产出读取依赖：哪些 stat、哪个查询层、哪些技能等级、目标或物品属性、是否需要随机。静态依赖用于校验、准备和失效判定，不意味着运行时可以提前读取未执行分支或消耗随机。条件短路、求值次序和递归引用按原语义核实。

属性查询明确区分 base、item/skill、total、bonus、参数层和手位上下文。装备上的增强伤害不应仅因 stat ID 相同就提前和全身增强合并；套装、符文之语、孔内属性也须保留原始来源／查询语义。

缓存键至少覆盖程序／规则版本和实际依赖版本；带世界目标、时钟或随机的表达式不能简单按 skillId＋rank 缓存。缓存是优化，不改变输出、取整、随机消费或生命周期。

## 8. 客户端共享契约

| 客户端场景 | 可调用的公共能力 | 不可推断的权威结果 |
| --- | --- | --- |
| 本人技能描述／预览 | 已知技能等级、装备与公式、伤害范围／费用 | 敌方隐藏抗性、命中结果或尚未确认的扣费 |
| 他人／怪物动作显示 | 原消息、已知模式与原动画元数据、局部相位推进 | 未公开加速来源、真实中断／释放是否成功 |
| 视觉飞弹 | 对应 Clt 规则、已知起点／方向、共享几何和局部运动 | 服务端目标集、隐藏随机、实际伤害子弹数量 |
| 物品说明／报价 | 已观察实例和允许查询的纯计算 | 未鉴定隐藏属性、真实商店成交许可或生成资格 |
| 资源显示 | 已确认快照和原单位换算 | 用预测恢复覆盖服务器生命、法力或存档 |

共享代码只保证相同语义和完整相同输入时结果一致。客户端缺输入时不能猜测默认值，也不能为提高预测准确性增加私有协议泄露隐藏状态。连接原服与自研服沿同一客户端适配路径。

Srv／Clt 本来不同的参数和程序分别保留。可以共享环射方向、remaining 周期和转向步骤，但不能因为图形看起来应相同而合并独立的数量、寿命、落点符号或创建时机。原版某客户端是否调用该函数，需要具体调用证据；D2Common 导出本身不是证明。

## 9. 如何避免下阶段返工

公共底座在大量补内容前需要稳定的不是所有最终类名，而是：数值单位、属性键／来源、技能来源、动作事件、分通道中间结果、飞弹继承、目标资格、随机归属和提交时点。新内容必须能够表达这些信息，不靠增加若干含义不明的布尔参数绕过。

先形成原程序与公共算法之间的可追溯映射：哪些程序直接调用同一算法，哪些用同一算法但不同参数，哪些本就不同，哪些未核实。由这个映射决定共享，不按相似技能名称猜测。

旧内容也要成为这些契约的消费者。只给德鲁伊／刺客和新怪物使用新公共函数，而保留旧职业复制公式，会重新产生两套规则；但迁移任务应在目标契约明确后编写，不反过来用现有函数签名限制设计。

公共层完成的标志：每个开放函数输入完整、单位明确、原出处可查、输出与副作用界限清楚、消费者不复制规则、不需拿整局对象才能调用。准确性最终仍需相应证据，不以“参考了 D2MOO”一句话替代。
