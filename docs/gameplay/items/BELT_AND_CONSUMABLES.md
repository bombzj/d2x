# 腰带与消耗品

更新：2026-10-10。本页维护腰带使用、药剂与原消耗效果；数量／缩容归[INVENTORY](INVENTORY.md)，永久奖励归[QUEST_ITEMS](QUEST_ITEMS.md)，卷轴／书本技能程序归[GENERAL](../skills/GENERAL.md)。

容量、布局、类型、书页和原图来自当前MPQ Belts／Inventory／Misc／Books／CharStats。连接的服务端拥有自动入带、补位、消耗、回复、状态与持续时间；自研inventory／effects沿同一原协议执行，旧客户端本地执行器已删除。Shift商店购买按原multibuy位提交，服务器决定腰带／书本补满和费用。

## 操作

1–4提交对应腰带列饮用，B展开腰带；右键或拖放沿公共库存面板提交原请求。腰带→Cursor、放入、交换及使用由原物品回包确认，药剂消失与资源回复分别观察，不能只按发送成功认定恢复完成。

回城卷轴／书经RemoteControl选择原技能并创建门户，真实type2门户与位置等原服；鉴定技能栏施放／原0x20使用由原0x3F准备光标，再0x27确认目标；背包右键当前直接准备只读来源并提交0x27。两条路径都等物品鉴定／来源消耗更新，不自动重试；界面不扣卷轴或书页。

## 原规则与限制

原恢复量、长度、pSpell、cstate与stat／calc读取MPQ；职业倍率规则核对本地D2MOO Items::ITEMS_GetBonusLifeBasedOnClass／ManaBasedOnClass。SkillItem::pSpell03／09提供恢复合并与耐力／解毒／解冻原服规则证据，客户端不重新执行这些函数。

自研生命／法力药水已纠正旧单机顺序队列，使用原healthpot／manapot状态、8.8恢复值和len：连续饮用合并剩余帧与剩余恢复值，再整数除法重算每帧率；按实际体力／精力及原随机判定双倍恢复，消费提交后才推进种子。生命超过上限移除Healthpot，法力恢复前已满移除Manapot，到期／死亡移除；补血与毒伤合并后限制至少一生命，毒伤不禁自然回蓝，原nomanaregen才抑制自然回蓝。定时状态、原回包与资源在同一提交边界更新，不保存药水时钟。

腰带脱下／缩容／出售的安置规则只维护在[INVENTORY](INVENTORY.md)。NPC治疗遵循SUnitNpc::HealPlayer，只清毒／冻结与States.curable指定状态；effects准备、transactions提交，只实际治疗时发送原Sound10。诊断healingQueued／manaQueued表示有恢复时钟的原状态数，不表示旧队列瓶数。

技能书／生命药剂／抗性卷轴及Token的资格、消耗与任务来源统一见[QUEST_ITEMS](QUEST_ITEMS.md)，原保存位见[存档](../../modules/SAVES.md)。不能从物品可用图标推断当前人物已获得永久奖励资格。

客户端仅消费原状态／属性，不自设清毒、冷却、叠加或耐力回复。佣兵腰带／光标喂药与城镇治疗已接，范围见[佣兵](../characters/HIRELINGS.md)；完整消耗品目标及组合未认证，有限药剂／卷轴原服观察见[联网记录](../../modules/NETWORK.md#既有有限证据)。回复药水按原8.8上限百分比取整，耐力／解毒／解冻药水读取MPQ状态、长度、清除状态及属性，同状态延长剩余时间；资源上限、自然恢复与耐力耗用归人物／effects，不在物品消费写第二套计算。有限药水／治疗证据与未覆盖边界见[EVIDENCE](EVIDENCE.md)。原资源／声音仍由公共加载与播放模块消费，许可见[资料来源](../../resources/THIRD_PARTY.md)。
