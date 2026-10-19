# 角色属性、技能与显示投影

客户端的角色状态来自连接的权威服务端；原服由D2GS执行，自研嵌入宿主由server独立领域执行，二者均通过原包更新同一客户端。旧会话执行器不恢复。操作及提示规则见[属性](../gameplay/characters/ATTRIBUTES.md)和[公共技能](../gameplay/skills/COMMON.md)。

## 自研服务端属性与成长基础

本节维护属性、成长与保存的实际入口；原版依据和缺口由对应专题登记，自研运行结果只在当次交付中说明。

- hosting/character_content准备MPQ经验阈值、Skills学习门槛／前置、CharStats职业定义、难度抗性惩罚和入场物品在全部支持等级的属性值。server不持有Archives／ClassicData或MPQ求值回调。
- attributes/calculation共用纯装备loadout／requirements／contributions／stats，汇总活动装备、背包护符、不同套装原件的条件／固定全套属性、黄金鸟已饮药生命与安亚已用卷轴抗性；生成四属性、资源上限、抗性、防御、移动速度、有效技能等级及装备战斗快照。基础被动包含既有圣骑士基础等级命中／抗性上限、亚马逊六类被动和Warmth；女巫／亚马逊及已实现职业的被动、技能状态与临时效果已接；各职业执行范围由[技能目录](../gameplay/skills/README.md)维护，未迁入的职业与完整组合不由此推定支持。
- PlayerStore唯一持有持久CharacterRecord及当前Totals；admit／transactions在公开状态之前同步总值，不设dirty队列或下一帧修补。attributes.evaluate只读快照，step无额外改写。
- progression.execute接原3A属性打包数量（1–100点）及3B单点学技，核对活人、余点、职业、原所需等级＋基础等级、最大基础等级、四属性门槛及基础前置技能。物品授予／加成技能不充当前置；学点不等于施放能力已恢复。
- progression.award为可信服务端结算入口，输入已决定的经验量和局内按玩家递增发生标识。累计值以原最高等级阈值封顶，支持跨多个等级；每级属性点取CharStats，技能点按原规则每级一。发生标识仅在成功事务后提交，拒绝过期／重复，不保留无界集合。普通怪物击杀经验、邪恶洞穴技能点、第二幕技能书及后三幕生命／属性／技能点／抗性／古代人经验奖励已有权威来源；附近同队经验分配和分任务奖励资格已接；完整房间邻接及特殊多人事件仍有限制，经验范围见[组队](NETWORK.md#组队邀请客户端)，不接受客户端授予。任务奖励与服务见[任务系统](../gameplay/quests/SYSTEM.md)；古代人升级沿人物事务提交LevelUp触发。
- transactions.CharacterEdit与InventoryEdit共同复验人物／库存revision，重算派生值并发布不可变CharacterFact／InventoryFact。升级补满存活人物生命／法力／耐力；体力／精力加点按CharStats四分之一单位增量增加对应资源，再限制到新上限；换装仅限制，不当作治疗。入场按真实总值限制资源，死亡语义沿已有入场规则。
- hosting/native_character_wire共用入场／增量编码：ItemStatCost提供身份、ValShift和Signed，原1F发绝对属性，94发基础清单，21更新基础／额外技能等级（含装备来源撤销后的零等级）。增量仅发改变值，无私有成功ACK或模式分支。人物投影独立细节修复已获用户确认：按当前MPQ的item_poisonlengthresist（ID 110）／item_absorbfire_percent（ID 142）读取毒持续时间抗性／百分比火焰吸收。RemoteUiClients仍按原表Stat／ID／ValShift读取实际收到的属性；旧名在当前表中不存在，修正同时适用于原服，不是自研协议适配。

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

世界角色外观由RealmPortraitCatalog将原服装备身份与当前MPQ Armor／Weapons／ArmType解析为ActorAppearance，交给公共ActorAnimationCatalog合成。真实身体组件与未实现的装饰效果分开：魔法等品质、自动词缀、无形及符文之语不使整个人物不可绘制，equipmentEffectsKnown标记组件染色／无形透明缺口并由场景诊断报告；缺少真实组件或原COF／DCC仍明确不可用，不替换裸装。选角肖像的原染色字节仍要求已有支持，与世界装备组件可绘制性分开。

技能标题、图标、页／行／列来自Skills／SkillDesc／TBL。选择器与技能树已接共用MPQ表驱动描述层，按原描述程序分别生成通用、当前等级、首次／下一等级及协同区；不依赖自研技能执行是否实现。原版分组／等级调用、公式、已知值、截图条件及缺口由[技能描述](../gameplay/skills/DESCRIPTIONS.md)唯一维护。

技能树节点有点数时按同一canAllocate资格显示正常原图或原PL2灰色图，零级满足条件同样正常；已知没有点数时已学技能仍正常、未学技能灰色，不能点击加点。数字使用原服有效等级，已知高于基础等级用原PL2蓝色、低于基础用红色，未知不伪作零级；数字锚点在图标右下外侧，超过9用原fontformal10。原图、色表和字体取当前MPQ，静态依据与表现边界见[HUD](../gameplay/ui/CLASSIC_HUD.md#技能菜单)。页签使用StrSklTree原TBL分行和font16白字，关闭按钮按当前MPQ末行空列选择预留格。

经验门槛读取 Experience／CharStats，未确认等级不显示 MAX；CharStats／PlayerClass 的 Expansion 分隔行不占职业编号。

未知属性／等级显示 `?`。完整装备与状态已接玩家／佣兵共用战斗投影；完整入场空列表的合法零值按原规则确认，解码失败不补零。缺少完整武器／装备输入时不重算武器伤害、命中或最终格挡；缺伤害加成时伤害未知，但已知法力、范围及时间仍可显示。描述程序入口已接 1–75，但缺少必要原包输入／尚未支持的表达式仍保留未知；全技能数值及像素验收状态见描述专题。当前等级的既有法力公式和可投掷装备事实只作为资格判断的可选只读输入；发送适配缺少这些输入时仍由原服裁决，不增加第二套消耗／装备计算。下一等级预览只生成提示，不改变当前施放资格。

人物、技能树、选择器和底栏消费同一结果；没有LocalCharacterClient或第二份提示组装。角色身份及D2S字段的编解码见[存档](SAVES.md)，当前投影、原版依据与缺口见[人物属性](../gameplay/characters/ATTRIBUTES.md)及对应技能专题。
