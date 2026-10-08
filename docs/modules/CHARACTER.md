# 角色属性、技能与显示投影

客户端的角色状态来自连接的权威服务端；原服由D2GS执行，自研嵌入宿主由server独立领域执行，二者均通过原包更新同一客户端。旧会话执行器不恢复。操作及提示规则见[属性](../gameplay/characters/ATTRIBUTES.md)和[公共技能](../gameplay/skills/COMMON.md)。

## 自研服务端属性与成长基础

2026-10-08已构建打包，经验升级、属性点／技能点分配及保存重入有有限冒烟；范围与限制见[基线](../../BASELINE.md#当前运行包与有限冒烟)。

- hosting/character_content准备MPQ经验阈值、Skills学习门槛／前置、CharStats职业定义、难度抗性惩罚和入场物品在全部支持等级的属性值。server不持有Archives／ClassicData或MPQ求值回调。
- attributes/calculation共用纯装备loadout／requirements／contributions／stats，汇总活动装备、背包护符、不同套装原件的条件／固定全套属性、黄金鸟已饮药生命与安亚已用卷轴抗性；生成四属性、资源上限、抗性、防御、移动速度、有效技能等级及装备战斗快照。基础被动包含既有圣骑士基础等级命中／抗性上限、亚马逊六类被动和Warmth；女巫／亚马逊被动、已实现技能状态及临时效果已接；其他职业完整被动与主动执行仍未迁入。
- PlayerStore唯一持有持久CharacterRecord及当前Totals；admit／transactions在公开状态之前同步总值，不设dirty队列或下一帧修补。attributes.evaluate只读快照，step无额外改写。
- progression.execute接原3A属性打包数量（1–100点）及3B单点学技，核对活人、余点、职业、原所需等级＋基础等级、最大基础等级、四属性门槛及基础前置技能。物品授予／加成技能不充当前置；学点不等于施放能力已恢复。
- progression.award为可信服务端结算入口，输入已决定的经验量和局内按玩家递增发生标识。累计值以原最高等级阈值封顶，支持跨多个等级；每级属性点取CharStats，技能点按原规则每级一。发生标识仅在成功事务后提交，拒绝过期／重复，不保留无界集合。普通怪物击杀经验及邪恶洞穴奖励已有权威来源；队伍经验分配和其他任务奖励待补，不接受客户端授予。
- transactions.CharacterEdit与InventoryEdit共同复验人物／库存revision，重算派生值并发布不可变CharacterFact／InventoryFact。升级补满存活人物生命／法力／耐力；体力／精力加点按CharStats四分之一单位增量增加对应资源，再限制到新上限；换装仅限制，不当作治疗。入场按真实总值限制资源，死亡语义沿已有入场规则。
- hosting/native_character_wire共用入场／增量编码：ItemStatCost提供身份、ValShift和Signed，原1F发绝对属性，94发基础清单，21更新基础／额外技能等级（含装备来源撤销后的零等级）。增量仅发改变值，无私有成功ACK或模式分支。人物投影独立细节修复已获用户确认：按当前MPQ的item_poisonlengthresist（ID 110）／item_absorbfire_percent（ID 142）读取毒持续时间抗性／百分比火焰吸收。RemoteUiClients仍按原表Stat／ID／ValShift读取实际收到的属性；旧名在当前表中不存在，修正同时适用于原服，不是自研协议适配。

grant-experience已接GameHost → progression → transactions → 原包输出，暂停实例也可使用；原服拒绝宿主管理。D2S保存基础分配、经验／等级、余点、基础技能与当前资源；总值、有效等级、缓存及奖励发生标识不写盘。v96不变，当前宿主指纹见[存档](SAVES.md)；未分配随机套装值／条件套装需求明确不可用，不重掷或静默忽略。尚未认证全部装备组合、成长及存档往返，攻击显示与完整战斗不在本批完成范围。

## 入口与所有权

| 入口 | 职责 |
| --- | --- |
| [remote_world.cpp](../../src/client/remote_world.cpp) | 归并原身份、属性、资源、技能选择及等级；字段未到保持未知 |
| [remote_combat.cpp](../../src/client/remote_combat.cpp) | 当前 MPQ 资格校验、学习／加点／选择／绑定请求及结果观察 |
| [character_projection.hpp](../../src/client/character_projection.hpp)、[character_projection.cpp](../../src/client/character_projection.cpp) | 原服已知值与 ClassicData 输入，唯一 projectCharacterDisplay 纯投影 |
| [skill_eligibility.cpp](../../src/content/skills/skill_eligibility.cpp) | 唯一 Skills 资格／CharStats 固有技能导入及纯只读判断，人物投影与发送前校验共用 |
| [character_display.cpp](../../src/content/character/character_display.cpp) | 共用原描述及纯显示公式，不读取会话、设备或 GPU |
| [character.hpp](../../src/contracts/character.hpp)、[character_client.hpp](../../src/client/character_client.hpp) | 人物、技能树、技能菜单和 HUD 的值视图及语义意图 |
| [record.hpp](../../src/gameplay/character/record.hpp)、[character_save.hpp](../../src/persistence/character_save.hpp) | 服务端持久人物值及D2S接口；不持有活世界或客户端会话 |

技能资格元数据与只读判断在content/skills/skill_eligibility，人物投影与RemoteCombat共用；未收到基础等级不能按零授权学习，原reqstr／reqdex／reqvit／reqint门槛也使用已知属性。此收拢已入当前运行包，见[基线](../../BASELINE.md)。

原 0x94 全量技能清单（含空清单）区分未学习与未收到；0x21分别维护基础／加成等级。0x23维护左右键选择和 owner，0x7B恢复绑定。基础技能、已学前置、等级上限及属性门槛按原表交给 evaluateSkillEligibility；未知基础等级、点数或必需属性不授权学习。Attack／CharStats.Skill 1–10 由同一个导入函数确认，不要求原0x94再次列出；0x3A／3B请求等待原服属性／基础等级变化，超时结果未知。

## 显示边界

角色面板由[character_panel.cpp](../../src/presentation/hud/character_panel.cpp)消费同一CharacterView，使用当前MPQ invchar6、font6／font16／font8、原TBL strchr系列、level／levelsocket／skillpoints及buysellbtn关闭图。按用户原版截图恢复左右技能伤害／命中行、资源最大值／当前值分列、两行抗性与红色剩余属性点，去掉原版没有的额外属性行；经验加千位分隔，抗性数字不加百分号。资源格灰底属于原图，用户确认常驻，不按悬停绘制。标签／数值基线、加点位置、长姓名字体切换及资源／防御大数值切换核对原1.13c静态布局；字体使用原字形和TBL字宽，随侧栏统一适配。已知负抗性用原PL2红色；基础值／修正值及最大抗性输入尚不完整，增减属性颜色与封顶抗性金色暂未还原。已构建入包并有限查看，不能视为完整像素验收；布局入口及证据见[HUD](../gameplay/ui/CLASSIC_HUD.md#角色面板)。

世界角色外观由 RealmPortraitCatalog 将原服装备身份与当前 MPQ Armor／Weapons／ArmType 解析为 ActorAppearance，交给公共 ActorAnimationCatalog 合成。真实身体组件与未实现的装饰效果分开：魔法等品质、自动词缀、无形及符文之语不再使整个人物不可绘制，equipmentEffectsKnown 标记组件染色／无形透明缺口并由场景诊断报告；缺少真实组件或原 COF／DCC 仍明确不可用，不替换裸装。选角肖像的原染色字节仍要求已有支持，与世界装备组件可绘制性分开。此修正已入包，有限证据见联网交付记录。

技能标题、图标、页／行／列及说明来自 Skills／SkillDesc／TBL 和已有纯公式。选择器读取原 str short、StrSkill2；技能树使用原长说明，零级节点读取StrSkill17作First Level预览，已学习节点使用当前／下一等级标签。原多行说明按顶部到末行输出，当前／下一等级及首次学习预览共用hints，预览不改变施放资格。

技能树节点有点数时按同一canAllocate资格显示正常原图或原PL2灰色图，零级满足条件同样正常；已知没有点数时已学技能仍正常、未学技能灰色，不能点击加点。数字使用原服有效等级，已知高于基础等级用原PL2蓝色、低于基础用红色，未知不伪作零级；数字锚点在图标右下外侧，超过9用原fontformal10。原图、色表和字体取当前MPQ，静态依据与表现边界见[HUD](../gameplay/ui/CLASSIC_HUD.md#技能菜单)。这批修正已入包；页签使用StrSklTree原TBL分行和font16白字，关闭按钮按当前MPQ末行空列选择预留格，见HUD及联网交付记录。

Frozen Armor按当前MPQ descline的1／3／12程序和原TBL文案，逐行显示防御、持续、冻结与法力，复用SkillCastSpec／FreezeAttacker；缺协同清单只将相关持续／冻结时间标未知。Blaze接1／23／27及DescDam 9：Fire Duration读取已解析火焰弹体Range／LevRange存续时间，不误用dm12施法状态时间；每秒平均火伤按25Hz与原描述倍率3转换，取整数范围，缺支配／装备／协同输入保留未知。dsc3保持原顺序，支持绿色标题40、par1–8百分比加成63及已核对的(par7 + 12)/25时间加成67；未支持的协同描述显示未知。所有文案、技能名与倍率参数动态读取MPQ，不宣称覆盖全部SkillDesc描述程序。此提示修正已构建入包；全部描述程序仍未认证。

经验门槛读取 Experience／CharStats，未确认等级不显示 MAX；CharStats／PlayerClass 的 Expansion 分隔行不占职业编号。

未知属性／等级显示 `?`。缺少完整武器／装备输入时不重算武器伤害、命中或最终格挡；缺伤害加成时伤害未知，但已知法力、范围及时间仍可显示。宠物、未支持描述程序和光环必要输入不足时保留未知；不宣称覆盖 SkillDesc 全部描述函数。当前等级的既有法力公式和可投掷装备事实只作为资格判断的可选只读输入；发送适配缺少这些输入时仍由原服裁决，不增加第二套消耗／装备计算。下一等级预览只生成提示，不改变当前施放资格。

人物、技能树、选择器和底栏消费同一结果；没有 LocalCharacterClient 或第二份提示组装。角色身份及 D2S 字段的编解码见[存档](SAVES.md)，运行包与有限证据见[基线](../../BASELINE.md)。
