# 野蛮人技能

PD2专项内容：[职业技能](../../pd2/CLASS_SKILLS.md)。本页保留原版／公共规则及必要的版本边界。

更新于 2026-11-10。当前 MPQ 的 30 项野蛮人技能涵盖 ID 126–155，其中 20 项主动、10 项被动。逐项记录对应 d2decomped、本地原版 1.13c DLL 和当前 MPQ。装备、等级和多人组合的实测边界见下文。

定义入口为 `src/content/skills/barbarian_data.cpp`；纯规则在 `gameplay/skills`，动作、移动、弹体、命中、状态和尸体消耗分别由 server 对应系统拥有。客户端读取原协议状态、播放当前 MPQ 原图并提交命令。MPQ 数值、公式、武器类型、状态、弹体及尸体尺寸动态准备；本地 D2MOO 的 `SkillBar.cpp`、`MissMode.cpp`、`SequenceTbls.cpp`、武器速度和路径函数仅作职责及算法证据，见[资料来源](../../resources/THIRD_PARTY.md)。

公共武器技能资格按D2Common::sub_6FDB1130允许空手满足weap／mele／h2h要求，要求第二只武器的程序仍拒绝空手；不能只给德鲁伊／刺客放行而拒绝同样资格的Bash。实际选手、起手和后续联系复用同一筛选，不给空手伪造物品或武器支配类型。原版有限对照已确认徒手Bash能够命中及击杀，全部双持／专属武器组合仍待对齐。

十项被动的提示和实际贡献共用`evaluateBarbarianPassive`；Frenzy、跳跃、尸体搜索及Berserk共用显式递减曲线，呐喊／武器协同及序列命中时钟使用[公共函数](COMMON.md#公共函数抽取)。当前Bash／Stun／Concentrate／Berserk起手Alloc、动作Do2兑现；Bash的calc2只改已分配物理分量。DoubleSwing、Frenzy、Whirlwind及LeapAttack已接原生联系；动作、回绕和单位种子边界见[内核对照](GENERAL.md#旧近战链与随机迁移)。

专心的flag-4增益现跟随动作revision，在结束／中断／替换时撤销；狂战士按原calc2准备期限，非正结果采用原10帧分支；旋风的Aura／Passive及modifierlist元数据走逐写入资源观察，移动状态跟随同一动作。Frenzy保留原skill_frenzy堆叠与逐项速度写入。完整模式切换／事件顺序仍见[公共边界](EFFECTS.md#当前持续效果入口)。

## 逐技能入口

| ID | 技能 | 当前执行 |
| --- | --- | --- |
| 126 | Bash | 近战增伤、追加伤害、命中协同及击退。d2decomped 的 St32／Do2 对照原版 `D2Game+273C0/+A3220`。`calc1` 在起手分配武器伤害，`+27553` 把 `calc2` 定点加到已分配的物理分量，Do2 兑现击退。原版 `D2Client+34C00` 的 DescDam7 先按 `ddam calc1` 计算武器增伤，再加 `ddam calc2`，已将近战面板接入该顺序 |
| 127 | Sword Mastery | 原 swor 类型的命中、伤害及独立致命一击。d2decomped 的通用精通查询 `00645830` 对照原版 `D2Common+4F640`。`swor` 类型层分别提供 `ln12` 命中、`ln34` 伤害和 `dm56` 致命一击，原版按当前持用武器匹配，最多复制 32 层并对每项取最高值 |
| 128 | Axe Mastery | 原 axe 类型精通。d2decomped 的精通分层对照原版 `D2Common+4F6B5–4F70D`。当前表只给 `axe` 层写命中、伤害和致命一击，分别按 `ln12/ln34/dm56`，持用武器满足原类型继承才读取 |
| 129 | Mace Mastery | 原 blun 类型精通。d2decomped 的物品类型匹配对照原版 `D2Common+4F6F0`。表中的层是 `blun`，原类型继承覆盖符合钝器分类的武器，当前以该类型取三项精通，未按技能名称限制到单一武器子类 |
| 130 | Howl | 原环形弹道；等级资格、普通／仆从 AI 恐惧及持续时间。d2decomped 的 Do22／Hit17 对照原版 `D2Game+43AA0/+3EAE0` 和 `D2Client+73CB0`。双方使用 64 个方向及基础速度加 `calc1`，接触只在目标未带恐惧且等级严格低于施放者等级加技能等级及 `par2` 时继续。零售 `+1A90` 取 `par5/6` 作持续时间，`+B2120` 检查 AI 资格并安装 Terror |
| 131 | Find Potion | 软尸体资格、成功率、幕／难度药水选择、掉落与尸体禁止重用。d2decomped 的 Do69 对照原版 `D2Game+27600/+26F70`。先登记尸体禁止选取，用施放者种子抽 `calc1`，成功后再从同一流抽取药水类别。已将两次抽取随施放事务提交。原版 `+10E3E8` 的 15 组幕／难度药水代码与现有表一致，客户端 CltDo38 的尸体效果另见 `D2Client+C63A0` |
| 132 | Leap | 原范围曲线、落点校验、飞行移动状态、落地范围击退。d2decomped 的落地击退对照原版 `D2Game+27F30/+294A0`。原版起手 `+280C9` 调用整数范围投影和三候选空位搜索，已由服务端与客户端共用。落地时 `+29586–295AE` 按 `calc1` 和 `0x8583` 选目标，仅发送成功与击退结果位 |
| 133 | Double Swing | 原序列 11、双手武器及两次命中、Bash 协同和递减法力。d2decomped 的 Do70 对照原版 `D2Game+27B90`，序列偶数动作事件用首目标，奇数事件以 `0x2A783` 查 GUID 后继，然后共用 St32 分配及耐久兑现。当前两手分别使用实际装备及 Bash 硬点协同，费用由表中的负 `lvlmana` 递减，客户端序列对应 `D2Client+C6940` |
| 134 | Pole Arm Mastery | 原 pole 类型精通。d2decomped 的精通分层对照原版 `D2Common+4F6BC–4F70D`，`pole` 由实际物品类型决定。该行 `Param1=30` 的命中曲线独立读取，伤害和致命一击仍按各自公式 |
| 135 | Throwing Mastery | 原 thro 投掷类型精通；近战／投射独立求值。d2decomped 的投掷精通分支 `00645720` 对照原版 `D2Common+4F200`。原函数同时验证武器可投掷、技能首类型继承 `thro` 及 `Range=rng`，再从 `0x159–0x15B` 分层取最大值。弹体伤害和发射初始化已补齐实际武器门槛 |
| 136 | Spear Mastery | 原 spea 类型精通。d2decomped 的类型层匹配对照原版 `D2Common+4F6F0`。当前表的 `spea` 独立于 `pole`，按 `ln12/ln34/dm56` 写近战三槽。装备投影和伤害结算均读取同一武器类型层 |
| 137 | Taunt | 有效目标或附近资格目标、追向施法者、降低命中与伤害。d2decomped 的 Do71 对照原版 `D2Game+29000/+27130`，无合格显式目标时以半径 20 和 `0x8783` 搜索最近候选。已让释放时失效的目标继续走复选，并在资格检查排除不可打断单位。命中和伤害百分比用 `ln12/ln34`，AI 重新指向施放者 |
| 138 | Shout | 原友方环形弹道、防御增益、持续时间协同及刷新。d2decomped 的共同战吼命中入口与原版 `D2Game+29720/+3EA60` 一致，Do68 先生成环形弹幕，再经 `+29240` 为施法者附加状态，友方由 Hit18 附加同一状态。服务端改为先准备弹幕，再在同一释放提交中应用自身状态、消耗资源并发布弹幕。各项防御和时长仍取当前 MPQ |
| 139 | Stun | 武器伤害、命中／伤害协同、分段眩晕时长及 War Cry 协同。d2decomped 的 Bash 族分配和眩晕记录与原版 `D2Game+273C0/+274FE/+9F210` 一致。命中后按 MPQ 叠加 Bash 伤害、Concentrate 命中和 War Cry 时长协同。已有接收链按目标类型限制眩晕。面板复用原版 `D2Client+34C00` 的 DescDam7 计算 |
| 140 | Double Throw | 原序列 15、各手真实弹体／数量与双击协同。d2decomped 的 Do74 与原版 `D2Game+27E30`、`D2Client+C71D0` 一致，序列 15 的两个动作帧分别取对应武器的弹种和数量。原版普通投掷与药瓶分支合流后均写入弹体命中值和 calc1 增伤，服务端补齐药瓶分支 |
| 141 | Increased Stamina | 被动耐力上限百分比。d2decomped 记录了通用被动取值，原版 `D2Common+525B0` 把 `ln12` 写入 `skill_passive_staminapercent`。当前 MPQ 的 ItemStatCost 163 以百分比作用于 `maxstamina`，D2X 原生属性链已使用该条目 |
| 142 | Find Item | 原尸体宝藏表、成功率及 NoDrop 绕过；保留尸体图形并禁止重用。d2decomped 记录的 Param1–4 分段经原版 `D2Game+276D0` 确认会消耗第二次施法者随机数，调用的 `+758D0/+75680` 忽略分段值，仍按尸体实际类别选择掉落表并跳过 NoDrop。服务端修正判定及成功时的施法者随机推进，独立保留尸体掉落种子。客户端 CltDo40 在可用尸体上播放既有特效 |
| 143 | Leap Attack | 固定落点、跳跃／落地攻击序列、目标复选、增伤协同及击退。d2decomped 的 St41／Do78 状态机经 `D2Game+28600/+293B0` 确认。`D2Common+51870` 按目标方向延伸两格，受阻后从原目标搜索空位，随后检查 `0x804` 直线和城镇。客户端与服务端共用落点计算，客户端 CltDo44 显示飞跃时采用这条路径。落地保留有效的起手目标，由战斗链判断能否命中 |
| 144 | Concentrate | 武器攻击、Bash／Battle Orders 协同、Berserk 魔法转换、攻击期间防御状态。d2decomped 的 St32 自身状态与原版 `D2Game+2755E/+648E0` 一致，Alloc 后用模式期状态写入 `ln34` 防御，Do2 兑现伤害。`calc1` 取重击和战斗体制协同，`calc4` 按狂战士硬点转换为魔法。面板已走 DescDam7 |
| 145 | Iron Skin | 被动防御百分比。d2decomped 记录的防御百分比与原版 `D2Common+525B0` 被动写入相符。当前 MPQ 以 `ln12` 写入 `skill_armor_percent`，角色防御继续读取原生合并值 |
| 146 | Battle Cry | 原弹道接触、降防／降伤、时长与状态刷新。d2decomped 的敌方命中与原版 `D2Game+29720/+3ABD0` 一致。Do68 产生弹幕，自身状态字段为空。Hit21 对有 aurafilter 的行用 `0xA783` 筛选，再用施放者计算时长并在目标上写入 `ln34` 防御和 `ln56` 伤害修正 |
| 147 | Frenzy | 原双手序列、伤害协同、命中逐步积累移动／攻击速度、超时清除。d2decomped 的序列 11、命中记忆及递减速度与原版 `D2Game+29600/+28E70`、`D2Client+C6940` 一致。第二次挥击在 `+296F9` 找不到候选时保留原目标，服务端补齐回退。命中记忆先推进 `skill_frenzy`，再按层数计算速度并刷新 `par7` 时长 |
| 148 | Increased Speed | 原递减被动移动速度。d2decomped 的移动属性与原版 `D2Common+525B0` 被动写入一致，当前 MPQ 的 `dm12` 写入 `velocitypercent` 67。D2X 步行和跑步时钟使用该属性，并单独折算装备移动速度 |
| 149 | Battle Orders | 原友方弹道、生命／法力／耐力上限、持续时间协同；不额外治疗。d2decomped 的友方 Hit18 与原版 `D2Game+29720/+3EA60/+29240` 一致，按 `ln34` 顺序写入法力、生命和耐力上限修正。时长取 MPQ 的大叫与战斗指挥硬点。当前资源由原版 `+D95CC/+D95DD/+D95DF` 对应的上限回调按比例更新 |
| 150 | Grim Ward | 软尸体资格、按真实尺寸选原图、起始／持续／结束阶段、固定位置恐惧及尸体消耗。d2decomped 的图腾脉冲与原版 Do75 `D2Game+27C90`、Hit26 `+3C990` 相符。服务端按 `D2Common+4D840` 从尸体位置搜索尺寸 2、掩码 `0x1000` 的放置点。客户端补齐 `D2Client+C7000` 的尸爆及 `+B6120` 的收尾图腾，起始与持续阶段沿用服务端同步 |
| 151 | Whirlwind | 不可打断持续移动、原碰撞掩码、首两次命中与后续武器速度节奏、双持及禁止命中触发。d2decomped 的步行速度与原版 `D2Game+28290/+9E730` 一致，服务端改用职业基础步速、直线路径 7、起步掩码 `0xC01` 和途中掩码 `0x401`。客户端按 `D2Client+C64B0` 的移动方式显示人物旋风。Do76 命中时钟的分档经 `D2Game+26E40` 与 `D2Common+267F0` 核对 |
| 152 | Berserk | 武器物理转魔法、Howl／Shout 协同、递减时长的防御清零状态。d2decomped 的 St39 与原版 `D2Game+289A0/+28AE0` 一致。命中时把物理伤害转换为魔法，分配后无论命中与否都更新自身状态。当前 MPQ 的 `calc2` 计算期限，非正值按原版回退为 10 帧，`armor_override_percent=-100` 将防御归零。攻击可中断性改由 MPQ `interrupt` 控制，面板复用 DescDam7 |
| 153 | Natural Resistance | 原递减火／电／冰／毒抗性。d2decomped 的四抗被动与原版 `D2Common+525B0` 一致，当前 MPQ 的同一 `dm12` 分别写入火、闪电、冰和毒抗性。角色面板和伤害接收继续从原生抗性读取 |
| 154 | War Cry | 原环形物理伤害、三项协同、眩晕与弹体命中冷却。d2decomped 的战吼弹幕与原版 Do68 `D2Game+29720` 一致。物理伤害和三项硬点协同取 Skills，`warcry` 弹体的 Dmg7 `+39600` 用 `+1B10` 计算 `Param1+(等级-1)*Param2` 并写入命中类别 `0x60`。重复命中间隔读取 Missiles 的 `NextDelay=6` |
| 155 | Battle Command | 原友方环形弹道、全技能 +1、被动重新计算、持续时间协同与刷新。d2decomped 的全技能加一与原版 `D2Game+29374/+2937A` 一致。服务端补齐友方弹体写完属性后的显式被动刷新，与施放者共用的状态顺序一致。持续时间取 MPQ 的大叫和战斗体制硬点，刷新复用原状态列表 |

狂乱的命中增益也读取原AuraLen／AuraStat公式：skill_frenzy为独立raw层数，命中后加一并限于技能等级，复用列表并按有符号期限登记移除；攻击速度另写other_animrate，不通过替换列表执行中间撤出。

## 生命周期与限制

友方Shout／Battle Orders／Battle Command已接原始列表生命周期：当前MPQ AuraLenCalc的signed32结果保留为原移除帧，同状态复用列表，六个非零AuraStat按顺序写入，随后执行公共被动刷新；零期限也登记移除事件，列表与状态位不因时间查询提前消失。移除先撤出属性，再清状态和刷新被动。人物费用、资源上限和效果使用已准备资源记录提交，避免把逐写上限回调重放到已经调整过的资源上。依据D2MOO SkillBar::SKILLS_ApplyWarcryStats、SkillDruid::sub_6FCFE0E0及Skills::sub_6FD14C30。完整敌方AI函数／参数、飞弹传播和全局事件排序仍有缺口。敌方Howl／Taunt的原始列表共用RestoreAi移除候选，状态撤出与普通AI恢复随同一单位提交；特殊目标重置与AI函数恢复的区别见[死灵专题](NECROMANCER.md)。

学习、前置、技能上限、左右键、法力、充能和城镇资格仍按共用入口校验。六精通按真实武器类型匹配，同类加成取最高值；空手不冒充武器。双挥／双投／Frenzy 必须同时有两个合格武器，换装后释放复验来源；Whirlwind 允许单武器。投掷分别消耗各手真实数量，整次技能不重复扣法力。被动随有效技能等级和装备重新计算。

Find Potion／Find Item 搜索保留尸体显示并设置 `corpse_noselect`；Grim Ward 消耗并隐藏尸体。这些操作复用原死亡掉落完成资格，避免覆盖未提交的原掉落。尸体不可重复用于搜索、召唤、重生或其他消耗。Grim Ward 的恐惧原点固定为图腾位置。恐惧与嘲讽不控制首领或不允许切换 AI 的单位；War Cry／Stun 复用怪物眩晕资格和精英几率。PD2专项说明见[职业技能：战吼观察边界](../../pd2/CLASS_SKILLS.md#职业技能战吼观察边界)。

Battle Command 在被动求值前加入临时技能来源；协同仍只读基础技能等级。佣兵的临时全技能加成参与内容缓存重算，生效和到期都更新实际技能等级；刷新保留 Battle Orders 的生命上限，BO基数仍取装备后的未加成生命。状态、Frenzy 层数、移动动作、图腾、尸体标记和弹体不写 D2S；技能基础等级与选择沿原 v96 字段保存。保存与校验边界见[存档](../../modules/SAVES.md)。

玩家跳跃与跳跃攻击的落点规则见 ID 132 和 143。两者按 CharStats.RunVelocity 进行 25 Hz 路径步进，客户端用 `D2Client+C65B0/+C7C80` 和表 `+D5F54` 的距离分段重力和初速计算高度。收到 `skill_move` 后显示飞行及落地序列。PD2 CltDo97 使用自身 Calc2 速度。稀疏或迟到的位置、目标在飞行中改变、状态先撤出、动作和影子投影仍待对照。

PvP、多人争用、跨机器与 Linux、六精通的全部武器和等级组合、精英眩晕随机及视听时序仍待实测。吸血、吸蓝、压碎性打击和撕开伤口的公共规则见[命中效果](../combat/WEAPON_EFFECTS.md)。

尸体搜索的空NoDrop结果不等待不存在的落地容量；移动技能通过原16同步，Battle Cry接触采用当前MPQ NextDelay。以上为当前执行规则，不维护自研运行台账。
