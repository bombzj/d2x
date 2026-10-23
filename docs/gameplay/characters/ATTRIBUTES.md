# 角色属性与成长

角色视图和请求归[角色模块](../../modules/CHARACTER.md)。本人身份、基础属性、等级、经验、资源、可用点和技能等级来自连接的权威服务端；升级／学习／加点不在客户端执行。

## 原数据与显示

- CharStats／PlayerClass提供职业显示、基础动作和参数，Experience提供原等级门槛；Expansion分隔行不占职业编号。
- ItemStatCost.ValShift用于原服资源／属性单位转换。原绝对stat覆盖较旧紧凑资源采样；经验0x1A／1B为正增量，0x1C为绝对值。
- 人物／技能／HUD消费同一projectCharacterDisplay；伤害、命中、防御、格挡和抗性共用只读装备投影，不再给人物面板传空 EquipmentStats。活动装备、孔内物品、背包护符、套装属性及状态必须解码完整且 revision 一致；破损／NOEQUIP 与副武器组沿原标志及公共装备规则处理。
- 四基础属性和资源取原包总值；装备计算不重复叠加已收到的力量／敏捷。防御与格挡复用公共 deriveEquipmentStats，技能护甲及覆盖百分比来自原状态表，圣盾按状态携带的 modifierlist_skill／level 求 Skills.calc1（求值参数名 clc1）。格挡显示遵循 UNITS_GetBlockRate，不按走跑按钮偏好除以三；实际跑步中的战斗格挡惩罚仍由权威玩法处理。圣骑士被动取基础等级，其他已实现被动取有效等级；已携带数值的原被动状态不重复叠加。
- 原1D／1E／1F抗性条目是永久奖励列表，安亚由 ACT5Q3_UpdateResistances 单独发送；显示另加装备／状态／被动、DifficultyLevels 惩罚，并应用各自上限。原单位入场列表为空时的零值只在身份、核心属性、完整装备和原0xAA状态快照齐备后确认；解码失败／丢失快照不能用零替代。
- 面板动作读取 SkillDesc.DescDam／DescAtt。普通武器路径复用已有装备／施法纯计算；通用法术、持续火焰及毒总量使用与提示相同的 MPQ 固定点求值，祝福之锤使用专注状态的 damagepercent 和 Skills.Param1／8。原程序无伤害／命中行时保留空行，不伪造数值。有DescDam但缺少执行定义且没有支持的显示程序时保留`?`，不将PD2武器技能误显为空白；光环及已有执行定义的特殊显示不受该回退影响。PD2的ddam calc1附加百分比及武器掌握helper仍未完整接入。
- 提示复用当前MPQ与纯属性／等级公式；预览不改变副本，不据本地计算补升级或奖励。

核对入口：本地 D2MOO `D2Common/src/Units/Units.cpp::UNITS_GetDefense`、`D2Skills.cpp::SKILLS_GetConcentrationDamageBonus`、`D2Game/src/QUESTS/ACT5/A5Q3.cpp::ACT5Q3_UpdateResistances` 和 `UNIT/SUnitMsg.cpp::D2GAME_STATES_SendUnitStates_6FCC58E0`；原1.13c D2Client 的 DescDam／DescAtt 调度表 RVA F5210／F52A0。表值以当前MPQ为准，原DLL及参考仓库不成为运行依赖。

全部职业特殊动作、装备／套装／孔内及难度组合、负抗性、跑步格挡和Linux仍缺完整原版对照；缺少事实仍明确未知，不通过删问号或伪造零值掩盖。`online-status.characterUi`是同一页面视图，server-snapshot仅用于权威数值观察，不能作为原版画面依据。

## 操作与保存

人物面板提交0x3A分配属性、技能树提交0x3B学习，资格按已知点数、职业、原reqlevel＋基础等级、前置及上限校验；最终结果等原包回包，超时未知，不自动重试。0x2C事件2仅播放原cursor_level_up表现。

R／底栏切换走跑，按住Ctrl临时跑步；选择保存在客户端偏好。资源消耗／回复由权威服务端决定。原服持久化由D2GS／D2DBS，自研由hosting／persistence保存D2S；客户端不读档或执行成长。自研属性／成长基础及剩余边界见[角色模块](../../modules/CHARACTER.md#自研服务端属性与成长基础)，原服成长／属性消息参考见[联网](../../modules/NETWORK.md#既有有限证据)；全部升级边界与任务奖励仍需完整核对。
