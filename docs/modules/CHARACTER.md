# 角色属性、技能与显示投影

当前产品的角色状态来自原服。升级、分配点、学习、资源回复与保存由 D2GS 执行；本地成长／学习执行器已删除。操作及提示规则见[属性](../gameplay/characters/ATTRIBUTES.md)和[公共技能](../gameplay/skills/COMMON.md)。

## 入口与所有权

| 入口 | 职责 |
| --- | --- |
| [remote_world.cpp](../../src/client/remote_world.cpp) | 归并原身份、属性、资源、技能选择及等级；字段未到保持未知 |
| [remote_combat.cpp](../../src/client/remote_combat.cpp) | 当前 MPQ 资格校验、学习／加点／选择／绑定请求及结果观察 |
| [character_projection.hpp](../../src/client/character_projection.hpp)、[character_projection.cpp](../../src/client/character_projection.cpp) | 原服已知值与 ClassicData 输入，唯一 projectCharacterDisplay 纯投影 |
| [skill_eligibility.cpp](../../src/content/skills/skill_eligibility.cpp) | 唯一 Skills 资格／CharStats 固有技能导入及纯只读判断，人物投影与发送前校验共用 |
| [character_display.cpp](../../src/content/character/character_display.cpp) | 共用原描述及纯显示公式，不读取会话、设备或 GPU |
| [character.hpp](../../src/contracts/character.hpp)、[character_client.hpp](../../src/client/character_client.hpp) | 人物、技能树、技能菜单和 HUD 的值视图及语义意图 |
| [record.hpp](../../src/gameplay/character/record.hpp)、[character_save.hpp](../../src/persistence/character_save.hpp) | 独立 D2S 保存值；无活世界或产品恢复用途 |

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
