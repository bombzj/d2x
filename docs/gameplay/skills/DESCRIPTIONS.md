# MPQ 技能描述

PD2专项内容：[技能显示](../../pd2/SKILL_DISPLAY.md)。本页保留原版／公共规则及必要的版本边界。

本页唯一维护技能选择器与技能树的描述程序、公式输入、原版依据和缺口。当前公共层按原表分派，七职业全部数值与像素仍需逐项对照。技能执行、人物面板伤害和学习资格分别由职业专题、[人物模块](../../modules/CHARACTER.md)与[公共规范](COMMON.md)负责。

## 数据与入口

| 入口 | 职责 |
| --- | --- |
| [description_data](../../../src/content/skills/description_data.cpp) | 从当前 MPQ 的 SkillDesc、Skills、Missiles、MonStats、MonLvl、DifficultyLevels、ItemStatCost 与 TBL 准备不可变描述目录；导入被引用的隐藏技能，不依赖 SkillSpec 是否已实现 |
| [description_formula](../../../src/content/skills/description_formula.cpp) | 编译只读表达式树；求值使用显式回调，不读取会话、MPQ、设备或随机流 |
| [description_values](../../../src/content/skills/description_values.cpp) | 连接等级、基础技能、已知属性和表参数；复用公共固定点曲线；提供法力、伤害、时间、召唤物与被动数值 |
| [skill_description](../../../src/content/skills/skill_description.cpp) | 按描述程序编号 1–75 分派，使用 TBL 文案及格式；程序 0 无输出，未知编号保留未知描述 |
| [character_projection](../../../src/client/character_projection.cpp) | 同一原包副本投影选择器、当前等级、首次／下一等级及协同区；预览不分配点数、不改变技能资格 |
| [remote_ui_clients](../../../src/client/remote_ui_clients.cpp) | 准备原包已知属性、数量与有效等级；元素支配按已观察装备／状态及 MPQ 被动公式只读投影，已在原状态表携带的被动不重复计入 |
| [skill_eligibility](../../../src/content/skills/skill_eligibility.cpp) | 导入原技能资格及两组手部类型条件；装备显示条件只消费原包准备的类型、标志和数量，保留未知；不以红色显示替代选技能请求权限 |
| [skill_tooltip](../../../src/presentation/hud/skill_tooltip.hpp) | 消费结果，负责字体、颜色、居中、换行和锚点；不再计算技能数值 |

`SkillCatalog.descriptions` 持有公共目录，角色标题、短／长说明及布局仍由现有 SkillCatalog 导入。删除旧 Frozen Armor／Blaze 的专用描述字段、逐技能 hints 和按施法效果拼接的英文说明。描述目录与服务端执行目录独立，因此职业玩法未迁入不阻止读取其原描述。

SkillDesc 的三组字段均导入 `line / texta / textb / calca / calcb`：等级相关 `desc1–6`、通用 `dsc2 1–4`、协同 `dsc3 1–7`。参数、技能／弹体引用、召唤物基础值和语言文本来自当前挂载资源，不从参考仓库附带表复制。

同名表字段按所属表解释：Skills.Range 是目标类型文本，Missiles.Range／LevRange 才是弹体整数寿命。MonStats 只导入被技能 summon 引用的记录及原程序 41 的基础身份，避免将无关的同名怪物行当作唯一描述身份。TBL 数字索引保留全部原记录；重复文本键按原哈希桶的最短前向探测距离选取，再应用资料片／补丁覆盖。当前原表 StrSkill44 的两个记录分别是骷髅战士总数与骷髅法师复数，不能用桶遍历的最后一项覆盖原键查找结果。

### PD2 CN 的技能树与显示边界

PD2专项说明见[技能显示：技能树与资源覆盖](../../pd2/SKILL_DISPLAY.md#技能显示技能树与资源覆盖)。

### PD2 专用描述输入

PD2专项说明见[技能显示：专用描述输入](../../pd2/SKILL_DISPLAY.md#技能显示专用描述输入)。

### 形态限制显示

选择器非光环分支读取当前 Skills 的 `restrict`、`State1–3` 和 States 的 `restrict` 标记：0 禁止受限形态，1及其他非2值不加限制，2要求处于受限形态并依次匹配三个状态，遇空槽即停止。本人状态必须解码完整、序号与单位一致，所有状态ID均能映射当前目录；不完整时不推断禁用。该判断只影响图标／提示着色，不改变学习资格、技能选择请求或服务端施法权限，充能技能复用相同投影。

SrvDo116在血魔法下仍检查生命，不享有普通法力豁免；依据同一文件 `D2Common_SKILLMANA_CheckStat_6FDB0F50`。非血魔法的变身豁免依赖单位 `UNITFLAGEX_ISSHAPESHIFTED`，不能用 States.restrict 或 transform 掩码代替。其写入者为 `D2StatList.cpp::D2Common_STATES_ToggleState_6FDB8900`：任一disguise状态存在即置位，最后一个移除才清位。现从完整且序号一致的状态快照独立准备disguise事实，未变身时检查原法力，已变身时豁免普通法力；血魔法、startmana及充能分支仍各自保留。状态未知不推断普通法力不足，本项已通过Windows Release构建，实机核对仍待验收。

## 两个界面的原调用规则

原客户端先构造底行在前的文本。本项目生成最终视觉顺序：

- 选择器：短说明与当前有效等级标题，随后 dsc2 逆序、desc 逆序；不把 dsc3 放进选择器。
- 技能树：长说明、通用组、当前等级组、首次／下一等级组，最后 dsc3 正序；非空通用组与说明之间保留空行，协同标题与正文分别交给现有视图。
- 树的 desc 当前／预览分别传当前有效等级与有效等级加一。原树的 dsc2／dsc3 同样传有效等级加一；跨技能引用及 `blvl` 读取当前人物，不为预览修改基础清单或装备。
- `str mana`、StrSkill1／2／17、skilldesc3 等标签由 TBL 决定。多行短／长说明沿原表方向转换；现有绘制器负责屏幕内折行。
- 物品授予：基础等级已知为零且有效等级大于零时，树顶部显示绿色 StrSkill118，当前等级后加 StrSkill117。固有通用技能不视为物品授予；基础清单未知时不推断来源。标题、授予提示和协同标题分别以行块标记着色，长文本换行不改变标题位置。
- 尚未学习任何本职业技能：原普通树分支检查本职业全部技能的基础等级，均为零时在顶部显示绿色 StrSkill119，后隔空行再显示技能名。不是每个零级节点各自触发；物品有效等级和固有通用动作不算已学习职业技能。若同时有物品授予提示，视觉顺序为 StrSkill119、StrSkill118、技能名，各块以空行分隔。当前投影须有完整基础清单才确认“均为零”；红色需求分支不加此提示。状态标题用有序列表及对应行块色标，不按技能身份处理。
- 不可学习的树节点走原红色正文分支，绿色技能标题独立保留；达到原表 maxlvl 时显示 StrSkill116，否则需求等级用 `skilldesc3` 和原表 reqlevel 加当前有效等级，即使人物已超过该等级也仍显示。能学习的未学技能不一律标红；无剩余技能点时，已学节点保留普通正文。客户端根据已知基础清单、有效等级、技能点、等级与必要属性投影该状态；等级已明确不足时，即使剩余点数尚未上报也显示需求分支，点数本身仍保持未知。不从未知输入推断失败，也不改变实际分配点数的资格计算。
- 选择器图标及整块提示（包括技能名）共用原技能使用状态的红色分支，独立于树的学习资格与城镇施法限制。原 AURA 在装备／资源判断之前返回独立状态，不因低法力或城镇条件一律标红。当前显示投影接原表 InGame、aura、itype／etype A/B 两组手部条件、noammo，以及已观察装备类型／损坏／NOEQUIP／弹药数量；类型继承沿当前 ItemTypes，先匹配右手 A，再查左手 B，只有右手 A 不匹配时才尝试相反组别。空手例外按原 weap／mele／h2h 条件处理，不按职业或技能名设例外。
- 选择器资源不足判断独立读取原描述目录，使用未施加 MinMana 的原固定点消耗；已知 blood_mana 改查生命，物品 owner 充能不按普通技能扣法力，startmana 保留 inferno 状态例外。装备空槽必须已观察，弹药解码必须与原物品 revision 一致，资源例外必须有同步的原状态副本；缺少事实时不推断条件失败。这层只决定图标／提示色号，不新增客户端消耗或自研协议，不改变既有选择命令权限。
- 选择器红色图标与红色字体都使用当前 Sky PL2 第 1 号原索引变色；图标不以 RGB 乘红近似。树已有灰色／悬停变色继续使用同一资源准备路径。

火弹的法力在 dsc2，冰箭的法力在 desc，因此两者顺序不同，不能统一成“伤害、时间、法力”模板。协同文本也由程序解释，而非读取 Param 后按固定百分比模板猜测。

## 描述程序与公式

| 程序族 | 实现内容 |
| --- | --- |
| 1 | 原法力固定点、MinMana 限制、ManaShift／等级增量决定整数或小数；公式 A 为显示精度，不是法力覆盖值 |
| 2–7、20–21、32、36、68、70 | 数字前后标签、正负号、百分比、单／复数、零值抑制 |
| 8–14、22、24、26–27、34、39、41、48–50、59 | 命中率、物理／元素范围、元素持续、毒伤总量、每秒平均值、召唤物基础及加成、指定描述弹体；各程序保留原固定点与取整顺序 |
| 12、16、19、23、28–31、37、57 | 帧转秒、时间范围、原单位转码／半径、弹体与子弹体 Range、难度 AiCurseDiv |
| 15、18、25、33、40、45–46、65、69 | 标签、格式化标题、等级条件、上一等级描述及原色号标题 |
| 17、35、38、42–44、47、52、58、60–64、67、72–73 | 范围、分数、固定点精度与协同格式；范围相等时是否合并、标签位置和加号按程序处理 |
| 51、53–56、66、71、74–75 | TBL 格式字符串参数替换及原数量描述；资源格式不交给 printf 执行 |

公式支持整数、一元符号、算术、比较、逻辑、条件、min／max，以及 `skill`、`miss`、`stat`、`sklvl`。技能参数包含 par、ln／dm、lvl／blvl／ulvl、原伤害／持续／法力、技能 Calc／Aura／Passive／Pet 与描述弹体参数。`skill` 读取目标技能有效等级，`sklvl` 使用指定参数求出的等级；伤害协同的 `blvl` 使用基础等级。

词元间双引号与空白同样跳过，单引号引用内容保持不变；依据本地 D2MOO `source/Fog/src/Calc.cpp` 的 `DATATABLS_ParseExpressionToken`。PD2专项说明见[技能显示：能量护盾表达式引号](../../pd2/SKILL_DISPLAY.md#技能显示能量护盾表达式引号)。

`math / madm / macr` 按被动 Stat 身份寻找计算槽，不假定第一、第二、第三槽固定用途。元素曲线复用 `damage_curve`，物理及弹体保留协同在 HitShift 前的顺序；技能元素最小值沿原“固定不超过 1 点且第一阶增量为零”例外。程序 22 的指定弹体先乘 75，再施加所属技能的火／电支配，最后移位；程序 50 不施加该支配。程序 14 先将固定点毒伤率乘持续帧数，再移位。

程序 17／19／24 将 TextB 放在 TextA 数值行之前，不另起一行，也不省略。充能标签因此由 MPQ 的 `Eskillpowerup1–3` 自然组成“Charge N - …”；同一规则覆盖火焰拳、雷电爪和寒冰刃，不按技能身份处理。程序 17 在范围后接原每秒标签，19 在半径后接原单位，24 使用原元素数值；射击次数等固定通用行仍由 dsc2 决定。

召唤描述使用 MonStats 的难度等级和基础值；非 noRatio 行结合 MonLvl 的资料片比例。生命百分比先作用于平均基础生命，平加最后加入；宠物物理伤害按原 Aura／Passive Stat 槽收集百分比及平加。程序 41 的 MonStats 身份 292 是原描述程序常量，实际基础值仍读当前表，不按 Fire Golem 技能名分支。

原小数格式使用截断后的整数部分／小数部分，不用浮点四舍五入或统一的 `%.1f`。固定点范围的 `%d.%d-%d.%d` 保留原程序格式，不另作零填充或去尾零。

SkillCalc／MissCalc词汇已从本机MPQ核对，使用原 `mana / pets / m1el / me3o`等名称，不另造别名。弹体参数分别读取Param、CltParam、HitPar、CltHitPar、DmgParam及各自的线性／递减对。原1.13c D2Common RVA4DC30／ordinal10115确认dm递减参数先截断等级比值，公式与权威曲线共用该原版算子；全部描述程序仍缺逐项对照。原1.13c的`mps`将完整等级法力乘25后除以2；它与D2MOO 1.10f转录不同，以本机原二进制为准。原raw描述弹体回调仍按参数编号除以3，因此`m2eo/ey`读取描述弹体1、`me3o/y`读取描述弹体2；这是已核实的原显示映射，不在描述层擅自改成普通弹体槽号。

充能选择器的数量行使用当前 TBL `ModStre10d` 格式，不保留手写英文 Charges 文案；原 owner／等级／余量语义不变。

## 已知值与未完成边界

描述求值只读。未知不是零：缺少引用等级、装备／状态、难度、必要属性或不支持的表达式时，仅受影响的数值显示 `?`，结果记录对应描述程序；其他已知行继续输出。未收到完整基础清单不推断未学习；基础清单完整且该技能不存在时，才能采用原零基础等级语义。

元素支配 ID 329–332 超过单字节属性包。客户端可以从已解码装备、完整状态快照和当前 MPQ 的被动公式重建所需显示值；数量和效果仍由原包决定，不增加自研服务端专用属性包。装备或状态未完整观察时保持未知，不把支配默认为零。

以下范围尚不能宣称全部数值或像素认证：

- 武器来源伤害接玩家／佣兵共用战斗投影准备的完整原武器值；缺少完整装备输入时仍保持未知。人物面板另按SkillDesc.DescDam／DescAtt调度，不把描述行程序当作动作程序；数值、格挡、原版依据与缺口由[人物属性](../characters/ATTRIBUTES.md)维护，全部特殊动作尚未对齐。
- `stat.base / stat.mod` 不从已收到的累计属性反推；依赖这两种输入的表达式保持未知。随机表达式不消费玩法随机流，尚未支持的词法保持未知。
- 程序 48 接当前资料片路径；旧经典版模式的额外分支不在本项目当前资料片入口认证范围。
- TBL 原标题色号已接现有协同标题契约；任意嵌入颜色、极长多行说明与全部装备／充能状态的最终排版仍需原版截图核对。
- 选择器已接普通技能的成对装备条件、已知资源条件、上述形态限制着色及disguise驱动的SrvDo116普通法力豁免，不等于完整复刻 SKILLS_GetUseState。基础动作仍沿已有投掷事实，主武器优先级／魔法箭物品属性、技能延迟和原客户端动作冷却显示尚未完整投影。未以城镇禁用替代这些状态。
- 全部75个程序参数、复杂装备／充能／变身／冷却、完整像素、多客户端及Linux仍缺完整核对；程序入口齐全和原表覆盖不等于全部参数已对齐。

初始状态快照依据D2MOO `SUNITMSG_FirstFn`／`D2GAME_STATES_SendUnitStates_6FCC58E0`；空列表仍发0xFF终止，后续A7／A8／A9保留。客户端不把缺包默认成零；新准入不携带临时状态属性列表，运行中全状态重同步及所有他人属性组合仍有缺口。装备耐久提示由[经典HUD](../ui/CLASSIC_HUD.md#装备耐久提示)维护。

## 原版证据

参考固定版本与许可见[资料来源](../../resources/THIRD_PARTY.md)。本批独立实现解释器，不加载原 DLL，也不复制原字节码或二进制程序。

| 来源 | 核实内容 |
| --- | --- |
| 本地 D2MOO D2Common `D2Skills.cpp` | `SKILLS_GetSpecialParamValue`、`SKILLS_EvaluateSkillDescFormula`、跨技能／指定等级回调、`D2Common_11032`、法力／伤害／持续及支配顺序 |
| D2MOO `DATATBLS/SkillsTbls.cpp`、`Units/Missile.cpp` | 关键词、Stat 选择器、SkillDesc 字段及弹体参数／曲线 |
| D2MOO `DATATBLS/MonsterTbls.cpp` | `DATATBLS_CalculateMonsterStatsByLevel` 的 noRatio、难度与资料片 MonLvl 比例 |
| 原 1.13c D2Client，MD5 `f5860c629d309b8fc1f96174babcf633` | RVA `0x316C0` 分派、`0x32ABC` 的 75 项跳转表；描述 helper `0x2C790–0x315xx`；`0x33340` 选择器、`0x33C80 / 0x33CF0 / 0x33DB0 / 0x33EA0` 的组别／等级调用 |
| 同套 D2Client 物品授予分支 | `0x335C9–0x335EA` 核对有效／基础等级条件；`0x33C2E–0x33C63` 的树来源判断、`0x33E44–0x33E61` 的 StrSkill117 后缀、`0x34007–0x3404F` 的 StrSkill118 顶部提示；通用组分隔空行见 `0x33ED2–0x33EFD` |
| 同套 D2Client 尚未学习职业技能提示 | 普通树 `0x34056–0x3409C` 调 `0x2C8C0`，返回零时追加 StrSkill119 及空行；`0x2C8C0–0x2C94F` 扫当前职业技能、owner=-1、读取基础等级，只要一个大于零即取消提示。原程序倒序构造文本，所以 StrSkill119 位于 StrSkill118 之前；红色需求树 `0x33550` 不经过此分支 |
| 同套 D2Client 多段描述与不可学习树分支 | `0x31B1B / 0x31BE8 / 0x31CE0` 核对程序 17／19／24 先写 TextB；`0x7899C–0x789D8` 由节点状态选择普通 `0x33B50` 或红色 `0x33550` 描述；`0x338BE–0x33941` 写 reqlevel 加当前有效等级，`0x33B2D` 使用红色色号 1。节点状态来源见 `0x78BE2–0x78C35` 和 `0x78DAD–0x78E8C` |
| 同套 D2Client 选择器使用状态 | `0x3E98E–0x3E9B6` 调 D2Common 使用状态并检查本地动作冷却；`0x3EA43–0x3EA57` 区分 USABLE、AURA 与红色；`0x3E794–0x3E7DD` 将红色色号 1 传给整块选择器提示 `0x33290`，不读取树的学习资格或城镇限制 |
| 同套 D2Common，MD5 `ee1238806ef6d6d9801d12a09d128fe1` | 1.13c ordinal 10435／RVA `0x51B80` 对应描述公式，参数解释入口 `0x51070`；不直接套用 D2MOO 1.10f 的 ordinal |
| 同套 D2Common 使用状态与手部／资源条件 | ordinal 10787／RVA `0x52340` 对应 SKILLS_GetUseState；`0x50610` 手部类型／空手／损坏／弹药，`0x50F00` 成对组别与优先级，`0x50AF0` 原消耗及血魔法／充能，`0x4FDC0` startmana／inferno。与 D2MOO 的 SKILLS_GetUseState、sub_6FDB1130 和 CheckStat／CheckStartStat 对照；本机 1.13c 使用本机导出身份 |

以上原版导出身份和调用点用于定点核对，原表、DLL及截图不纳入源码或分发；结论不依赖临时导出文件长期保留。

## 截图证据与待补

用户提供的原版选择器包括Fire Bolt等级2、Ice Bolt／Frozen Armor／Blaze／Raise Skeleton等级1，并补充这五个技能及Warmth等级2的原版树提示。它们支持标签、排序、法力小数、帧转秒、平均伤害、宠物行和物品授予提示的显示方式；装备、难度、版本与基础技能清单未完整给出，不能据此确认所有公式数值。

用户另补 Holy Fire／Poison Nova／Holy Shield／Fists of Fire／Wake of Inferno 的等级 10 树提示及未学习的 Amplify Damage。Wake of Inferno 是 TBL 显示名，Skills 行名为 `Inferno Sentry`；它们通过 SkillDesc／TBL 关联，不加程序别名。最后一张是死灵诅咒的未学技能样本，不是刺客充能或陷阱的首次等级样本。

选择器全图另含Holy Fire／Holy Shield／Wake of Inferno／Fists of Fire等级10、Poison Dagger等级2提示；随后补齐Poison Nova等级10选择器及Fists of Fire／Wake of Inferno未学习时的普通树提示。Poison Dagger与Poison Nova分别记录，不互相替代两界面的参考条件。

| 树截图 | 对描述层的约束 |
| --- | --- |
| Fire Bolt | 法力仅在通用组，不在 Current／Next 下重复；两项协同依表顺序显示 |
| Ice Bolt | Current／Next 均显示伤害、Cold Length、法力；五项协同无额外猜测模板 |
| Frozen Armor | 防御、总持续、冻结时间、法力依 desc 逆序；dsc3 可交替使用程序 67 的秒数和 63 的百分比 |
| Blaze | 火焰持续、平均每秒伤害、法力；预览独立求等级曲线，协同依当前人物基础清单 |
| Warmth | 只显示数值加 StrSkill23，不额外拼接“Mana regeneration”标签；无协同区 |
| Raise Skeleton（物品授予） | 顶部绿色授予提示、Current 等级的 `(item)`；宠物基础伤害在通用组，Attack／Defense／Life／数量／法力在等级组；协同两项仅技能名 |
| Holy Fire 等级 10 | 半径、附加到攻击的火伤、固定点光环火伤依 desc 逆序；不显示零法力行；协同为 Resist Fire、Salvation |
| Poison Nova 等级 10 | 总毒伤、Over 持续时间在同一描述程序的相邻行，随后法力；预览沿新等级曲线，协同依实际 Poison Dagger／Poison Explosion 基础等级 |
| Holy Shield 等级 10 | Smite Damage 的加号范围、Duration、Defense Bonus、Successful Blocking、法力依表顺序；最终人物格挡与这里的技能加成不是同一值 |
| Fists of Fire 等级 10 | 三项充能各自同一行带 Charge N 前缀，第二项半径、第三项每秒范围；Attack 与法力在其后；唯一协同由当前表指定 Phoenix Strike |
| Wake of Inferno 等级 10 | Shoots 10 Times 单独属于通用组，Current／Next 内仅火伤与法力；Wake of Fire 的每级半径小数与另外三条伤害协同分别按程序解释 |
| Amplify Damage 未学习 | 绿色标题、红色长说明／Required Level／通用伤害加成／First Level 组；无 Current／Next 标题，通用组与首级组有空行；并非所有未学技能都走红色分支 |
| Fists of Fire 未学习（普通分支） | 顶部绿色 StrSkill119 与技能名分别隔空行；白色正文、First Level，三段 Charge 前缀／半径／每秒伤害、Attack 与法力沿同一 desc 程序；没有 Current 0、Required Level 或 Next Level |
| Wake of Inferno 未学习（普通分支） | 白色长说明后是通用组 Shoots，再隔空行显示 First Level 的火伤和法力，最后原协同组；未学习本身不强制红字或需求等级，截图范围内无 StrSkill119 |

| 新选择器截图 | 对共用描述／显示层的约束 |
| --- | --- |
| 圣骑士全图 | 原 ListRow 分开两组光环、战斗和底行通用动作；红色图标不能简单等同于没有学会或城镇不可施放；截图不认证现有菜单全部坐标／排序 |
| Holy Fire | 半径、攻击附加范围、光环固定点范围；无协同组，光环提示保留白字 |
| Holy Shield | Smite 加号范围、持续、防御、格挡、法力与树的当前等级组一致；技能名、短说明、等级与数值整块为红色。具体失败原因仍需装备／资源事实，不能只凭颜色指定为缺盾 |
| Poison Dagger | 总毒伤与持续相邻，随后 Attack 百分比和法力小数；整块红字是使用状态，不是毒伤描述程序自行变色 |
| Poison Nova 等级 10 | 白字短说明、Current Skill Level、总毒伤、Over 持续、法力；程序 14 在同一调用中输出毒伤与持续，树和选择器复用同一等级计算，不加入 dsc3 协同行 |
| Wake of Inferno | 当前等级之后接 dsc2 的射击次数，然后 desc 的火伤与法力；树中通用组在 Current 标题之前，两界面不使用同一固定拼接顺序 |
| Fists of Fire | 短说明保留原空行；三项 Charge N 前缀与数值同一行，随后 Attack 和法力；由程序 24／19／17 组合，不另写技能模板 |

原字体的小图中数字 5 容易与 6 混淆。以本机 MPQ 而非截图 OCR 硬改数值：Fire Bolt 法力为 2.5，Warmth 等级 2／3 为 42／54 percent，Ice Bolt 的协同 Param8 为 15，Frozen Armor 下一级防御为 35 percent。冻结时间仍使用当前人物其他护甲的基础等级；先前选择器与这次树截图的冻结值不同，不足以证明两个界面使用不同公式。

此前请求的三张截图均已收到，不再列为待补。可学习的未学节点与因点数／前置条件不可学习的节点已有不同截图证据；尚未提供完整版本／难度／基础与有效技能清单／装备条件，因此不能只凭图中文字认证全部公式输入与数值。后续完整运行验收仍按本页“已知值与未完成边界”进行，不为现有程序分组和首次等级标题继续索取同类截图。

这些截图用于后续核对原版显示，不将截图中的特定等级数值写成技能分支。
