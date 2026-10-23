# 野蛮人技能

更新：2026-10-11。自研宿主已接当前 MPQ 的 30 项野蛮人技能（126–155）：20 项主动、10 项被动。本页维护当前实现、原版依据与缺口；入口齐备不表示全部装备、等级和多人组合已对齐。

定义入口为 `src/content/skills/barbarian_data.cpp`；纯规则在 `gameplay/skills`，动作、移动、弹体、命中、状态和尸体消耗分别由 server 对应系统拥有。客户端读取原协议状态、播放当前 MPQ 原图并提交命令。MPQ 数值、公式、武器类型、状态、弹体及尸体尺寸动态准备；本地 D2MOO 的 `SkillBar.cpp`、`MissMode.cpp`、`SequenceTbls.cpp`、武器速度和路径函数仅作职责及算法证据，见[资料来源](../../resources/THIRD_PARTY.md)。

十项被动的提示和实际贡献共用`evaluateBarbarianPassive`；Frenzy、跳跃、尸体搜索及Berserk共用显式递减曲线，呐喊／武器协同及序列命中时钟使用[公共函数](COMMON.md#公共函数抽取)。当前Bash／Stun／Concentrate／Berserk起手Alloc、动作Do2兑现；Bash的calc2只改已分配物理分量。DoubleSwing、Frenzy、Whirlwind及LeapAttack已接原生联系；动作、回绕和单位种子边界见[内核对照](../../modules/SERVER_SYSTEMS.md#旧近战链与随机迁移)。

专心的flag-4增益现跟随动作revision，在结束／中断／替换时撤销；狂战士按原calc2准备期限，非正结果采用原10帧分支；旋风的Aura／Passive及modifierlist元数据走逐写入资源观察，移动状态跟随同一动作。Frenzy保留原skill_frenzy堆叠与逐项速度写入。完整模式切换／事件顺序仍见[公共边界](COMMON.md#当前持续效果入口)。

## 逐技能入口

| ID | 技能 | 当前执行 |
| --- | --- | --- |
| 126 | Bash | 近战增伤、追加伤害、命中协同及击退 |
| 127 | Sword Mastery | 原 swor 类型的命中、伤害及独立致命一击 |
| 128 | Axe Mastery | 原 axe 类型精通 |
| 129 | Mace Mastery | 原 blun 类型精通 |
| 130 | Howl | 原环形弹道；等级资格、普通／仆从 AI 恐惧及持续时间 |
| 131 | Find Potion | 软尸体资格、成功率、幕／难度药水选择、掉落与尸体禁止重用 |
| 132 | Leap | 原范围曲线、落点校验、飞行移动状态、落地范围击退 |
| 133 | Double Swing | 原序列 11、双手武器及两次命中、Bash 协同和递减法力 |
| 134 | Pole Arm Mastery | 原 pole 类型精通 |
| 135 | Throwing Mastery | 原 thro 投掷类型精通；近战／投射独立求值 |
| 136 | Spear Mastery | 原 spea 类型精通 |
| 137 | Taunt | 有效目标或附近资格目标、追向施法者、降低命中与伤害 |
| 138 | Shout | 原友方环形弹道、防御增益、持续时间协同及刷新 |
| 139 | Stun | 武器伤害、命中／伤害协同、分段眩晕时长及 War Cry 协同 |
| 140 | Double Throw | 原序列 15、各手真实弹体／数量与双击协同 |
| 141 | Increased Stamina | 被动耐力上限百分比 |
| 142 | Find Item | 原尸体宝藏表、成功率及 NoDrop 绕过；保留尸体图形并禁止重用 |
| 143 | Leap Attack | 固定落点、跳跃／落地攻击序列、目标复选、增伤协同及击退 |
| 144 | Concentrate | 武器攻击、Bash／Battle Orders 协同、Berserk 魔法转换、攻击期间防御状态 |
| 145 | Iron Skin | 被动防御百分比 |
| 146 | Battle Cry | 原弹道接触、降防／降伤、时长与状态刷新 |
| 147 | Frenzy | 原双手序列、伤害协同、命中逐步积累移动／攻击速度、超时清除 |
| 148 | Increased Speed | 原递减被动移动速度 |
| 149 | Battle Orders | 原友方弹道、生命／法力／耐力上限、持续时间协同；不额外治疗 |
| 150 | Grim Ward | 软尸体资格、按真实尺寸选原图、起始／持续／结束阶段、固定位置恐惧及尸体消耗 |
| 151 | Whirlwind | 不可打断持续移动、原碰撞掩码、首两次命中与后续武器速度节奏、双持及禁止命中触发 |
| 152 | Berserk | 武器物理转魔法、Howl／Shout 协同、递减时长的防御清零状态 |
| 153 | Natural Resistance | 原递减火／电／冰／毒抗性 |
| 154 | War Cry | 原环形物理伤害、三项协同、眩晕与弹体命中冷却 |
| 155 | Battle Command | 原友方环形弹道、全技能 +1、被动重新计算、持续时间协同与刷新 |

狂乱的命中增益也读取原AuraLen／AuraStat公式：skill_frenzy为独立raw层数，命中后加一并限于技能等级，复用列表并按有符号期限登记移除；攻击速度另写other_animrate，不通过替换列表执行中间撤出。

## 生命周期与限制

PD2远端Double Throw走替换CltDo98，按两手真实Items.MissileType选择A／B／C及两次动画释放；Split Throw走118及原整数扇形，连锁次数与半径由当前公式／弹体输入准备。本人跳跃在服务器省略4C时消费最新skill_move状态结束飞行、进入落地动画；未确认预测沿既有15秒预算回收，新技能可替换失效预测。104中Find Potion／Find Item已接原38／40的尸体血迹，本地原服确认搜索后corpse_noselect及原血迹飞弹；原版模式的Find Potion也已核对。CltDo97已恢复本人经skill_move确认的飞行：按当前AuraRangeCalc准备落点、Calc2*41+2560取速度，原序列第5帧起飞，复用原整数路径步进及距离重力。移动状态提前撤出时仍完成已确认弧线，避免画面停在最终坐标之前。本地原服核对Leap／Leap Attack二十子格位移、落地及随后正常走路；其他玩家／怪物的97、全部碰撞及装备速度组合仍待覆盖。依据与范围见[核心分析](../../resources/PD2_CN_DLL_ANALYSIS.md#角色技能树与客户端程序)。

友方Shout／Battle Orders／Battle Command已接原始列表生命周期：当前MPQ AuraLenCalc的signed32结果保留为原移除帧，同状态复用列表，六个非零AuraStat按顺序写入，随后执行公共被动刷新；零期限也登记移除事件，列表与状态位不因时间查询提前消失。移除先撤出属性，再清状态和刷新被动。人物费用、资源上限和效果使用已准备资源记录提交，避免把逐写上限回调重放到已经调整过的资源上。依据D2MOO SkillBar::SKILLS_ApplyWarcryStats、SkillDruid::sub_6FCFE0E0及Skills::sub_6FD14C30。完整敌方AI函数／参数、飞弹传播和全局事件排序仍有缺口。敌方Howl／Taunt的原始列表共用RestoreAi移除候选，状态撤出与普通AI恢复随同一单位提交；特殊目标重置与AI函数恢复的区别见[死灵专题](NECROMANCER.md)。

学习、前置、技能上限、左右键、法力、充能和城镇资格仍按共用入口校验。六精通按真实武器类型匹配，同类加成取最高值；空手不冒充武器。双挥／双投／Frenzy 必须同时有两个合格武器，换装后释放复验来源；Whirlwind 允许单武器。投掷分别消耗各手真实数量，整次技能不重复扣法力。被动随有效技能等级和装备重新计算。

Find Potion／Find Item 搜索保留尸体显示并设置 `corpse_noselect`；Grim Ward 消耗并隐藏尸体。这些操作复用原死亡掉落完成资格，避免覆盖未提交的原掉落。尸体不可重复用于搜索、召唤、重生或其他消耗。Grim Ward 的恐惧原点固定为图腾位置。恐惧与嘲讽不控制首领或不允许切换 AI 的单位；War Cry／Stun 复用怪物眩晕资格和精英几率。本地原生PD2已核对Howl的terror56、Battle Cry的89状态及36／171减值；Taunt靠近目标后收到27状态及25／119各−5，先前目标已逃远的请求不算通过。完整AI行为、精英与玩家目标组合仍待核对。

Battle Command 在被动求值前加入临时技能来源；协同仍只读基础技能等级。佣兵的临时全技能加成参与内容缓存重算，生效和到期都更新实际技能等级；刷新保留 Battle Orders 的生命上限，BO基数仍取装备后的未加成生命。状态、Frenzy 层数、移动动作、图腾、尸体标记和弹体不写 D2S；技能基础等级与选择沿原 v96 字段保存。保存与校验边界见[存档](../../modules/SAVES.md)。

吸血／吸蓝、压碎／撕裂的公共实现与组合缺口见[命中效果](../combat/WEAPON_EFFECTS.md)；完整PvP、多人争用、跨机器与Linux仍缺对照。跳跃的原图飞行／落地序列已接，原客户端垂直高度曲线尚无已核实实现，不用自造高度替代。六精通全部武器／层级、精英眩晕随机、装备／状态组合和视觉／声音时序仍未完整对齐。

尸体搜索的空NoDrop结果不等待不存在的落地容量；移动技能通过原16同步，Battle Cry接触采用当前MPQ NextDelay。以上为当前执行规则，不维护自研运行台账。
