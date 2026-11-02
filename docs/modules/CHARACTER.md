# 角色属性、技能与显示投影

PD2专项内容：[客户端接入](../pd2/CLIENT_INTEGRATION.md)。本页保留原版／公共规则及必要的版本边界。

客户端的角色状态来自连接的权威服务端；原服由D2GS执行，自研嵌入宿主由server独立领域执行，二者均通过原包更新同一客户端。旧会话执行器不恢复。操作及提示规则见[属性](../gameplay/characters/ATTRIBUTES.md)和[公共技能](../gameplay/skills/COMMON.md)。

属性加点与单点学习记录提交前余点，只有目标属性／基础技能增加且余点减少才确认；两类更新允许任意到达顺序。未知值、拒绝与超时不补成本地成功。RemoteDisplay的本人走跑与元素支配共用knownSelfStat，读取完整装备、状态和当前Skills被动，避免重复计算已携带数值的被动；装备按级属性按effect而非原始属性名归并。

## 自研服务端属性与成长基础

本人四属性及生命／法力／耐力上限的1D／1E／1F表示基础列表，不能发送含装备／状态的最终值来补客户端派生。`native_character_wire`从CharStats、分配点及永久任务生命计算基础值；入场也不依赖临时拼装Totals里并不存在的来源图。`RemoteUiClients::projectObservedAttributes`只读完整装备／状态，按当前ItemStatCost运算依赖及CharStats上下文调用公共StatList求值，再投影四属性与资源上限，不能按技能名添加百分比。原版有限依据：10级Battle Orders状态携带生命／法力／耐力百分比62，不另发最终上限；同一角色应由1033／199派生1673／322。Wolf／Bear的上限切换沿同一入口，全部装备、按时间属性与复杂来源组合仍未认证。

本节维护属性、成长与保存的实际入口；原版依据和缺口由对应专题登记，自研运行结果只在当次交付中说明。

- hosting/character_content准备MPQ经验阈值、Skills学习门槛／前置、CharStats职业定义、难度抗性惩罚和入场物品在全部支持等级的属性值。server不持有Archives／ClassicData或MPQ求值回调。
- attributes/calculation共用纯装备loadout／requirements／contributions／stats，汇总活动装备、背包护符、不同套装原件的条件／固定全套属性、黄金鸟已饮药生命与安亚已用卷轴抗性；生成四属性、资源上限、抗性、防御、移动速度、有效技能等级及装备战斗快照。基础被动包含既有圣骑士基础等级命中／抗性上限、亚马逊六类被动和Warmth；女巫／亚马逊及已实现职业的被动、技能状态与临时效果已接；各职业执行范围由[技能目录](../gameplay/skills/README.md)维护，未迁入的职业与完整组合不由此推定支持。
- PlayerStore唯一持有持久CharacterRecord及当前Totals；admit／transactions在公开状态之前同步总值，不设dirty队列或下一帧修补。attributes.evaluate只读快照，step无额外改写。
- progression.execute接原3A属性打包数量（1–100点）及3B单点学技，核对活人、余点、职业、原所需等级＋基础等级、最大基础等级、四属性门槛及基础前置技能。物品授予／加成技能不充当前置；学点不等于施放能力已恢复。
- progression.award为可信服务端结算入口，输入已决定的经验量和局内按玩家递增发生标识。累计值以原最高等级阈值封顶，支持跨多个等级；每级属性点取CharStats，技能点按原规则每级一。发生标识仅在成功事务后提交，拒绝过期／重复，不保留无界集合。普通怪物击杀经验、邪恶洞穴技能点、第二幕技能书及后三幕生命／属性／技能点／抗性／古代人经验奖励已有权威来源；附近同队经验分配和分任务奖励资格已接；完整房间邻接及特殊多人事件仍有限制，经验范围见[组队](NETWORK.md#组队邀请客户端)，不接受客户端授予。任务奖励与服务见[任务系统](../gameplay/quests/SYSTEM.md)；古代人升级沿人物事务提交LevelUp触发。
- transactions.CharacterEdit与InventoryEdit共同复验人物／库存revision，重算派生值并发布不可变CharacterFact／InventoryFact。升级补满存活人物生命／法力／耐力；体力／精力加点按CharStats四分之一单位增量增加对应资源；装备／状态造成的最大资源变化沿原回调按比例调整并限幅，不当作治疗；上限不变的普通事务不额外裁剪已存在的超上限生命／法力。入场恢复保存的生命／法力，耐力按原版补满，死亡语义沿已有规则。
- hosting/native_character_wire共用入场／增量编码：ItemStatCost提供身份、ValShift和Signed，原1F发绝对属性，94发基础清单，21更新基础／额外技能等级（含装备来源撤销后的零等级）。增量仅发改变值，无私有成功ACK或模式分支。人物投影独立细节修复已获用户确认：按当前MPQ的item_poisonlengthresist（ID 110）／item_absorbfire_percent（ID 142）读取毒持续时间抗性／百分比火焰吸收。RemoteUiClients仍按原表Stat／ID／ValShift读取实际收到的属性；旧名在当前表中不存在，修正同时适用于原服，不是自研协议适配。

零售客户端的94清单只表示基础等级，入场不保证另有21携带装备加成。完整已观察装备／护符／套装及状态沿`projectCombatEquipment`的原生属性列表与公共`resolveNativePlayerSkillRank`重建已指派技能等级，再供人物技能显示和RemoteCombat输入资格／debug策略读取；不改写原包副本、不与21额外等级重复相加。原缺陷是在新物品／状态修订到来时先清战斗目录，面板尚未更新便将有效等级短暂退回94基础等级；静电场因此可能在首次选定首领时被错误跳过。现RemoteCombat先解码当前状态，再经生命周期绑定的只读查询复用当前装备投影，不等待面板下一帧更新；装备投影缓存按游戏、物品／解码修订、状态序列、武器组、人物等级及原技能清单复验，未知装备不新增推测。CN继续沿既有投影，不据此认证其扩展技能来源。

grant-experience已接GameHost → progression → transactions → 原包输出，暂停实例也可使用；原服拒绝宿主管理。D2S保存基础分配、经验／等级、余点、基础技能与当前资源；总值、有效等级、缓存及奖励发生标识不写盘。v96不变，保存与校验边界见[存档](SAVES.md)；未分配随机套装值／条件套装需求明确不可用，不重掷或静默忽略。全部装备组合、成长及存档往返仍需完整核对。攻击显示的实现与原版依据见[人物属性](../gameplay/characters/ATTRIBUTES.md#原数据与显示)，完整战斗由战斗及职业专题维护。

Akara每难度免费重置使用原slot41资格及0x38 parameter0，与Token共用`gameplay/character/respec`纯退款函数；清基础分配／技能并退款，清选择与热键，不把装备加成退成人物点数。资格消费、属性和技能由同一CharacterEdit提交。来源、slot41保存及资格边界见[第一幕任务](../gameplay/quests/ACT1.md)。

宿主管理`reset-attributes`／`reset-skills`经GameHost → progression.resetPoints → CharacterEdit复用同一退款函数的独立属性／技能范围；Akara及Token仍默认整体退款。管理入口不消耗任务或物品资格；基础属性只退已分配值，技能退款清基础等级、双武器组选择／来源与16个快捷键，不退固有或装备加成。只接收存活、已入局且没有活动技能的角色；溢出、规则不足或事务背压明确拒绝，成功后重算装备与派生值并限制当前资源，不额外治疗。参数见[调试管道](../development/DEBUG_PIPE.md#嵌入宿主管理命令)。

## 入口与所有权

| 入口 | 职责 |
| --- | --- |
| [remote_world.cpp](../../src/client/remote_world.cpp) | 归并原身份、属性、资源、技能选择及等级；字段未到保持未知 |
| [remote_combat.cpp](../../src/client/remote_combat.cpp) | 当前 MPQ 资格校验、学习／加点／选择／绑定请求及结果观察 |
| [character_projection.hpp](../../src/client/character_projection.hpp)、[character_projection.cpp](../../src/client/character_projection.cpp) | 原服已知值与 ClassicData 输入，唯一 projectCharacterDisplay 纯投影 |
| [skill_eligibility.cpp](../../src/content/skills/skill_eligibility.cpp) | 唯一 Skills 资格／CharStats 固有技能导入及纯只读判断，人物投影与发送前校验共用 |
| [character_display.cpp](../../src/content/character/character_display.cpp) | 人物面板动作值投影，不读取会话、设备或 GPU |
| [combat_display.cpp](../../src/content/character/combat_display.cpp) | 原包已观察装备／状态的公共只读战斗投影；玩家与佣兵复用装备贡献、需求、防御、格挡及武器纯函数 |
| [skill_description.cpp](../../src/content/skills/skill_description.cpp) | 选择器／技能树共用 MPQ 描述程序；数据、公式及已知值边界见[描述层](../gameplay/skills/DESCRIPTIONS.md) |
| [character.hpp](../../src/contracts/character.hpp)、[character_client.hpp](../../src/client/character_client.hpp) | 人物、技能树、技能菜单和 HUD 的值视图及语义意图 |
| [record.hpp](../../src/gameplay/character/record.hpp)、[hireling_record.hpp](../../src/gameplay/character/hireling_record.hpp)、[character_save.hpp](../../src/persistence/character_save.hpp) | 服务端持久人物／佣兵值及D2S接口；佣兵记录可独立使用，不持有活世界或客户端会话 |

技能资格元数据与只读判断在content/skills/skill_eligibility，人物投影与RemoteCombat共用；未收到基础等级不能按零授权学习，原reqstr／reqdex／reqvit／reqint门槛也使用已知属性。

原 0x94 全量技能清单（含空清单）区分未学习与未收到；0x21分别维护基础／加成等级。0x23维护左右键选择和 owner，0x7B恢复绑定。基础技能、已学前置、等级上限及属性门槛按原表交给 evaluateSkillEligibility；未知基础等级、点数或必需属性不授权学习。Attack／CharStats.Skill 1–10 由同一个导入函数确认，不要求原0x94再次列出；0x3A／3B请求等待原服属性／基础等级变化，超时结果未知。

## 显示边界

角色面板由[character_panel.cpp](../../src/presentation/hud/character_panel.cpp)消费同一CharacterView，使用当前MPQ invchar6、font6／font16／font8、原TBL strchr系列、level／levelsocket／skillpoints及buysellbtn关闭图。按用户原版截图恢复左右技能伤害／命中行、资源最大值／当前值分列、两行抗性与红色剩余属性点，去掉原版没有的额外属性行；经验加千位分隔，抗性数字不加百分号。资源格灰底属于原图，用户确认常驻，不按悬停绘制。标签／数值基线、加点位置、长姓名字体切换及资源／防御大数值切换核对原1.13c静态布局；字体使用原字形和TBL字宽，随侧栏统一适配。已知负抗性用原PL2红色；最终抗性与最大抗性输入已接共用装备／状态投影；增减属性颜色与封顶抗性金色暂未还原。完整像素仍待原版对照，布局入口及依据见[HUD](../gameplay/ui/CLASSIC_HUD.md#角色面板)。

世界角色外观由RealmPortraitCatalog将原服装备身份与当前MPQ Armor／Weapons／ArmType解析为ActorAppearance，交给公共ActorAnimationCatalog合成。真实身体组件与未实现的装饰效果分开：魔法等品质、自动词缀、无形及符文之语不使整个人物不可绘制，equipmentEffectsKnown标记组件染色／无形透明缺口并由场景诊断报告；未知组件身份或缺少原COF仍明确不可用；已解析为真实身份的组件若对应DCC／DC6均不存在，则按下述原DLL的通用逐层规则省略并保留缺图路径诊断，不替换裸装或借图。选角肖像接受已知编码版本4／10／13；弓按原组件编号保持LH（`netnbow.cof`仅有LH弓层，MPQ弓DCC在`lh/`），弩则同时填充RH／LH（当前MPQ `netnxbw.cof`、`amtnxbw.cof`均有两层，弩DCC也分布于两个手部目录）。弩的双层补齐依据D2MOO `INVENTORY_GetItemSaveGfxInfo/sub_6FD917B0`，世界装备与选角共用，不能将弩当作只有LH的弓。非FF染色字节（14–24）不再整体丢弃卡片：与游戏内单位预览同样按基础组件图合成、不套逐组件调色板，染色外观仍是缺口。PD2专项说明见[客户端接入：选角预览](../pd2/CLIENT_INTEGRATION.md#客户端接入选角预览)。组件编号表按原初始化顺序构造：去重只扫描已推进的连续前缀，不能扫描全部255个预留槽，否则后续头盔／护甲的线上编号会偏移。依据为D2MOO INVENTORY_InitializeComponentArray及本机1.13c D2Common（MD5 ee1238806ef6d6d9801d12a09d128fe1）RVA 0x1E920，0x1EAB9–0x1EAE0确认去重循环以当前前缀为界；固定类别表位于文件偏移0x9D888。PD2专项说明见[客户端接入：肖像组件边界](../pd2/CLIENT_INTEGRATION.md#客户端接入肖像组件边界)。

世界装备组件按D2MOO `INVENTORY_GetCompositItem`区分：只有1hs／1ht／ht1武器使用原包的主副手编号，其余部件读取当前MPQ的component。`SCmd::D2GAME_SendPacket0x9D`调用的`UNITS_GetItemComponentId`可将刺客／野蛮人非主手物品（包括盾牌）标为LH，不能直接作为身体图层。初始刺客CharStats配Katar与Buckler，当前MPQ小盾component=7（SH）；误用包头6（LH）会请求不存在的`ailhbuctnhth.dcc`并使完整合成失败。源码已按真实盾层解析，原版Host初次可见、武器切换及双持组合仍待实机验收。

世界单位按原`INVENTORY_GetWieldType`保留实际左右手槽占用，BROKEN／NOEQUIP只排除图层，不清除槽位占用；野蛮人可单手使用的双手剑旁有不可用副手时仍使用单手姿态。选角肖像没有完整装备槽事实，仍只按实际提供的可见组件解析。弩的双层及损坏副手姿态组合待实机验收。历史`neshne2*`和`sohdgth*`缺图分别属于已修正的组件层／编号问题，不据旧日志认定当前源码仍请求这些路径。

原1.13c的逐层缺图处理不限定职业／动作：`Graphics::composite`对全部chars组件，在已知真实身份但DCC／DC6均不存在时保留路径诊断并只省略该层，其他部件继续按原COF绘制。当前MPQ的实例包括死灵BL引用S3却没有`nes3ne2blhth`图形、德鲁伊TH有`dzthhth.cof`却没有`dztrmedthhth`及部分专用头盔图形、圣骑士A2／HTH的SH没有`pashpa1a2hth`图形。已有文件的解码失败、未知组件身份、缺COF以及全部图层均不可绘制仍明确不可用，不借用站立、受击或其他装备图形；资源后来补齐时仍加载真实图形。静态资源组合不等于实机动作覆盖，七职业全部装备／动作组合仍待实机验收。

上述省略依据为本机1.13c静态DLL链：D2Client（MD5 `f5860c629d309b8fc1f96174babcf633`）RVA `6E543–6E56F`按当前模式／组件准备图形，`6E474–6E483`读取该COF层的武器类别；本体逐层绘制`6C7E0–6CADC`独立调用组件解析／后端绘制，`6C880`失败分支转`6CACA`继续下一层；阴影循环`6BE60–6BEEC`同样逐层推进。D2CMP（MD5 `555b795747c9e032e379b0cca58ed9eb`）ordinal10005／RVA `E1D0`经`D350`与`C2D0`按当前组件／动作构造路径；文件打开失败在`D45B–D46A`转`D431`登记缺失，`DD9F–DDB3`／`DEA4–DEB4`返回无cell，未切换到其他动作。D2Gdi（MD5 `dc117a4fa4379c157248bb3dcca022df`）RVA `65EC–65F8`调用ordinal10005并在返回0时直接结束这一层绘制，D2Client随后继续COF层循环。上述D2Client循环与D2CMP缺文件分支均没有死灵／BL限定，因此使用公共chars省略规则；已有文件的解析失败不按文件缺失处理。结论来自同快照原DLL与当前MPQ，不据此认证其他后端或全部装备组合；DLL、反汇编及资源导出不进入源码提交。

投掷的COF类别按D2MOO `D2Common_10884_COMPOSIT_unk`及同快照D2Common（MD5 `ee1238806ef6d6d9801d12a09d128fe1`）RVA `44099–440CA`解析：TH只保留1hs／1ht／1js／1jt／1ss／1st，其他类别转HTH；RVA `43820–43857`有相同的类别处理。`ActorAnimationCatalog::composite`对人物TH同时使用解析后的类别读取COF及AnimData，不再用弓、长柄或爪的常驻武器类别直接请求TH资源。原动作／组件身份继续保留，不因COF缺失任意切换姿态。全部投掷来源与组合仍待实机验收。

技能标题、图标、页／行／列来自Skills／SkillDesc／TBL。选择器与技能树已接共用MPQ表驱动描述层，按原描述程序分别生成通用、当前等级、首次／下一等级及协同区；不依赖自研技能执行是否实现。原版分组／等级调用、公式、已知值、截图条件及缺口由[技能描述](../gameplay/skills/DESCRIPTIONS.md)唯一维护。

技能树节点有点数时按同一canAllocate资格显示正常原图或原PL2灰色图，零级满足条件同样正常；已知没有点数时已学技能仍正常、未学技能灰色，不能点击加点。数字使用原服有效等级，已知高于基础等级用原PL2蓝色、低于基础用红色，未知不伪作零级；数字锚点在图标右下外侧，超过9用原fontformal10。原图、色表和字体取当前MPQ，静态依据与表现边界见[HUD](../gameplay/ui/CLASSIC_HUD.md#技能菜单)。页签使用StrSklTree原TBL分行和font16白字，关闭按钮按当前MPQ末行空列选择预留格。

经验门槛读取 Experience／CharStats，未确认等级不显示 MAX；CharStats／PlayerClass 的 Expansion 分隔行不占职业编号。

未知属性／等级显示 `?`。完整装备与状态已接玩家／佣兵共用战斗投影；完整入场空列表的合法零值按原规则确认，解码失败不补零。缺少完整武器／装备输入时不重算武器伤害、命中或最终格挡；缺伤害加成时伤害未知，但已知法力、范围及时间仍可显示。描述程序入口已接 1–75，但缺少必要原包输入／尚未支持的表达式仍保留未知；全技能数值及像素验收状态见描述专题。当前等级的既有法力公式和可投掷装备事实只作为资格判断的可选只读输入；发送适配缺少这些输入时仍由原服裁决，不增加第二套消耗／装备计算。下一等级预览只生成提示，不改变当前施放资格。

人物、技能树、选择器和底栏消费同一结果；没有LocalCharacterClient或第二份提示组装。角色身份及D2S字段的编解码见[存档](SAVES.md)，当前投影、原版依据与缺口见[人物属性](../gameplay/characters/ATTRIBUTES.md)及对应技能专题。
