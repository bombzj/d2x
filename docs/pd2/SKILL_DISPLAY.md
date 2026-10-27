# 技能显示

技能树、描述输入、形态状态与显示公式的PD2差异；公共描述程序仍由原版技能文档维护。 目标身份与证据口径见[版本与证据](BUILDS.md)。

<a id="技能显示技能树与资源覆盖"></a>

## 技能显示：技能树与资源覆盖

来源：[docs/gameplay/skills/DESCRIPTIONS.md](../gameplay/skills/DESCRIPTIONS.md)。

RemoteDisplay 在导入前核对当前 Skills／SkillDesc TXT 与 BIN 的记录数、步长、职业技能 ID、描述及三项前置技能链接、等级／属性需求、CltSt／CltDo 编号、法力基础／递增／下限／起手值／移位、HitShift、EMin／EMax 及页面／行列／图标／选择器布局。该校验不认证公式字节码等价。描述与前置技能链接按原表的大小写无关规则关联；`blade dance` 能找到 `Blade Dance`，不能当作缺资源。

只有页 1–3、行 1–6、列 1–3 的记录形成可学习节点。隐藏且有 maxlvl 的真实技能保留身份、图标、描述和服务器／物品授予入口，但不显示在树上、不允许分点；无等级的占位行不进入职业目录。UI 与学习请求共用这个节点资格，原版仍要求每职业 30 个节点。七职业均要求三页非空，页内不能有重复格子，可学习节点的前置技能须指向同职业的实际记录；隐藏旧技能允许保留指向占位行的引用；不将 PD2 节点数量写成原版常量。

2026-10-22 当前 MPQ 的七职业树数据与原图覆盖如下。各背景均由 pd2assets 的 `skltree_<token>_back.dc6` 提供16帧；除亚马逊沿原 d2data 的图标表，其余图标表在 pd2assets。图标列统计整张 DC6 的帧数，成对帧用于普通／按压显示；当前所有有等级职业技能（含隐藏身份）的图标索引都在帧范围内。

| 职业 | 第1／2／3页节点数 | 可见总数 | 背景 token | 图标 token／帧数 |
| --- | --- | --- | --- | --- |
| 亚马逊 | 10／9／10 | 29 | a | am／60 |
| 女巫 | 12／10／11 | 33 | s | so／68 |
| 死灵法师 | 12／10／11 | 33 | n | ne／70 |
| 圣骑士 | 13／10／10 | 33 | p | pa／68 |
| 野蛮人 | 11／8／10 | 29 | b | ba／156 |
| 德鲁伊 | 10／10／11 | 31 | d | dr／62 |
| 刺客 | 11／10／10 | 31 | i | as／64 |

当前219个可见节点均能从实际英文 string／expansionstring／patchstring 组合找到名称、短说明和长说明，页面标题键也齐全；无需按职业截图后抄写数据。职业记录的180条非空前置引用与 BIN +0x17E／0x180／0x182 的有符号 WORD 一致，空引用为 -1。截图仅在目标资源或 DLL 与显示矛盾、缺输入或最终排版需要核实时补取；七职业数据覆盖不等于全部提示数值、学习操作或像素已实机认证。

场景显示公式复用只读描述目录，读取实际技能等级、完整性标志及已观察属性，不要求准备原版宿主执行规则；缺输入仍返回未知。元素支配与本人移动共用`knownSelfStat`，合并完整装备、已观察状态和当前Skills被动公式，已携带相应数值的被动状态不重复计算；装备每级属性按派生effect归并。该投影不提供PD2最终武器攻防。专用 CltDo 的接入和 DLL 证据见[角色技能差异](../resources/PD2_CN_DLL_ANALYSIS.md#角色技能树与客户端程序)。当前没有完成全部 PD2 描述数值认证：核心另改写了伤害显示 helper，75 个原描述程序入口齐全不证明这些改写已覆盖。高级角色不是树布局核对的前提；1 级角色的三个树页和未学技能提示即可提供布局／文字证据，实际施放与高等级数值另行核对。

用户提供的 CN 女巫三系树截图与当前表的节点位置相符：Cold 11、Lightning 10、Fire 12，共33。新增节点 Ice Barrage 位于 Cold 第5行第2列，Combustion 位于 Fire 第5行第2列；Cold Enchant、Lesser Hydra 的图标位置也与当前表相符。背景箭头仍使用目标 `skltree_s_back.dc6`，不按原版前置手工画线。截图是裁剪图，尚不能认证完整窗口坐标或全部缩放像素。

两个新增技能的英文原文已在当前 `pd2data.mpq` 的 `data/local/lng/eng/patchstring.tbl` 找到，后装载的 pd2assets／pd2maps／pd2monchars 不含该同名成员。现有 `ClassicStrings` 英文路径及 PlayPD2 默认 `eng` 可直接读取；不新增技能名分支或截图翻译常量。IceBarrageLongDesc 的两行沿原 TBL 倒序转为最终正文。

| 技能／英文键 | 英文长说明 | 用户未学提示与表的对应 |
| --- | --- | --- |
| Combustion／CombustionLongDesc | Creates an expanding ring of fire balls to engulf your enemies | 需求24；Cooldown 2.5 seconds（63帧截取一位小数）；法力10；Fire Bolt／Fire Ball 各级火伤协同12%。截图火伤19–44是当时角色的观察值，不能写成基础伤害 |
| Ice Barrage／IceBarrageLongDesc | Creates a barrage of magical ice lances that damage and slow nearby enemies | 需求24；Radius 4.6 yards（7子格）；首级3 Ice Lances、法力8；Ice Bolt／Glacial Spike 各级冰伤协同8%。零协同／零支配时，HitShift7 将 EMin32／EMax46 转为16–23，与截图相符 |

Combustion 的 TXT 与 BIN 均为 HitShift7、EMin24／EMax56，即未加成首级12–28；截图19–44尚缺基础技能、装备及火焰支配输入，不能只凭蓝色技能等级反推协同，亦不能据此指定为某一项加成。两张提示支持原顺序：绿色标题、红色正文／需求、通用半径（有则显示）、First Level、逆序等级行、最后协同组；不把半径或冷却统一塞进固定模板。

<a id="技能显示专用描述输入"></a>

## 技能显示：专用描述输入

来源：[docs/gameplay/skills/DESCRIPTIONS.md](../gameplay/skills/DESCRIPTIONS.md)。

`remote_ui_character.cpp` 在 RemoteDisplay 下为以下可见技能补充 `stat(... .accr)` 输入，复用 `knownSelfStat`：优先使用已收到的累计属性，否则要求装备完整、物品解码 revision 一致、状态快照序号一致及基础技能清单完整，再合并装备、状态和当前 MPQ 被动公式。原状态已携带的被动不重复计算；缺失事实仍显示未知，不将未同步属性当作零。属性身份、数值和描述公式来自当前 MPQ，不硬编码技能等级、数量或减免。

| 技能 | 已接描述输入／范围 |
| --- | --- |
| Joust | `joustreduction`、`joustreduction_leorics`、`joustreduction_zeraes`，冷却减免 |
| Gust | `gustreduction`，冷却减免 |
| Blood Warp | `blood_warp_life_reduction`，生命消耗减免 |
| Cold Arrow | `extra_cold_arrows`，附加箭数 |
| Raise Skeleton | `extra_skele_war`，骷髅战士数量 |
| Raise Skeletal Mage | `extra_skele_mage`、`grims_extra_skele_mage`，法师数量 |
| Raise Skeleton Archer | `extra_skele_archer`，弓手数量 |
| Bone Spear | `extra_bonespears`，附加骨矛数量 |
| Holy Bolt | `extra_holybolts`，附加圣光弹数量 |
| Revive | `extra_revives`，重生数量 |
| Corpse Explosion | `corpseexplosionradius`，半径附加值 |
| Energy Shield | `es_efficiency`，效率修正；同时修复子表达式双引号的词法处理 |
| Raven | `eaglehorn_raven`，原伤害表达式中的附加值 |
| Fist of the Heavens | `passive_mag_mastery`，原描述弹体公式中的魔法支配倍率 |
| Curse Mastery | `max_curses`，包括当前表被动 `blvl / 10`；描述公式自行加基础数量 |

这批改动只补描述输入与词法，不认证全部公式字节码、技能执行或像素。`pvp_cd`、基础格挡等不能仅从装备词条还原的输入仍须独立事实；不以装备加总伪造。Blade Dance 的跨职业前置是当前 TXT／BIN 的真实隐藏记录引用，不是可见树节点的链接失败，未修改它的前置规则。

<a id="技能显示形态限定节点"></a>

## 技能显示：形态限定节点

来源：[docs/gameplay/skills/DESCRIPTIONS.md](../gameplay/skills/DESCRIPTIONS.md)。

当前 PD2 表的形态限定节点为 Feral Rage、Rabies、Fury（wolf），Maul、Hunger、Shock Wave（bear），Fire Claws（wolf或bear）；没有按技能ID写分支。依据本地 D2MOO `D2Common/src/D2Skills.cpp::D2Common_SKILLS_CheckShapeRestriction_6FDB1380` 与 `D2States.cpp::STATES_CheckStateMaskRestrictOnUnit`。PD2核心是否另有使用资格改写及原客户端着色对照仍待核实，不能据表字段存在宣称全部兼容。

<a id="技能显示状态位编译"></a>

## 技能显示：状态位编译

来源：[docs/gameplay/skills/DESCRIPTIONS.md](../gameplay/skills/DESCRIPTIONS.md)。

当前PD2的States BIN共有245条、步长60；全部TXT行与BIN偏移16的64位标志逐位匹配确认：restrict为第38位，disguise为第16位，均取TXT整数最低位。delerium／cowset的restrict=2编译结果为未置位，不可按非零解释，也不能作为非法布尔值拒绝加载。新增两字段按此位规则读取；D2MOO旧版本restrict第17位不能直接用于本机BIN。

<a id="技能显示人物伤害剩余输入"></a>

## 技能显示：人物伤害剩余输入

来源：[docs/gameplay/characters/ATTRIBUTES.md](../gameplay/characters/ATTRIBUTES.md)。

PD2的ddam calc1附加百分比及武器掌握helper仍未完整接入。

41d49的2EF620实际按输入模式在普通绑定表与PlayerData+5BE的16项第二绑定表间选择，后者的辅助WORD非零选择另一技能手位，物品来源GUID仍需在执行时重新解析。393C40接收2E/sub4并更新这张表；原9c2b文档中的2ED970／298D20不能直接当作17d5或41d49入口。2BE450／2EE550的输入模式切换及原ControllerSkillSelect资源也参与消费者，因此剩余工作是第二输入模式的选择、绑定、来源撤销与执行闭环，不能简单扩成32个普通键盘槽或覆盖原16槽。D2X当前仍仅保存第二表报告，未宣称这条执行链已完成。

<a id="技能显示能量护盾表达式引号"></a>

## 技能显示：能量护盾表达式引号

来源：[docs/gameplay/skills/DESCRIPTIONS.md](../gameplay/skills/DESCRIPTIONS.md)。

当前 PD2 Energy Shield 的 dsc2calca1 在子表达式两侧含双引号，仅去掉整条公式外侧引号不足以解析它。此修正不改变运算优先级，也不声称全部 PD2 编译表达式已核对。
