# 圣骑士技能

更新：2026-10-10。自研服务端接入当前MPQ的96–125共30项职业技能：10项战斗技能、20项光环。Single Player、LAN及独立PvPGN服务端使用同一内核；客户端提交原技能包，读取权威结果。本页维护当前实现、原版依据与明确缺口，入口齐备不代表全参数已对齐。

PD2远端Holy Bolt的Calc1还读取extra_holybolts，客户端发射与描述共用本人已核实的装备／状态／被动属性投影，不能从缺少8位属性包推断为零。本地原生PD2服务已核对基础等级1的扣蓝与missile55、真实双手剑上的holysword状态、Sacrifice／Smite／Zeal／Vengeance击杀及Joust位移／伤害。Sacrifice共用原CltDo34血迹入口；本地原服已逐项观察20种光环的基础状态，双客户端组队核对Holy Light将队友生命150恢复至188；当前holylightdelay的CltHit66子体为FFFF，原程序直接返回成功，该无子体分支已接。有效子体的区域回调、额外弹数装备及完整治疗／伤害组合仍待覆盖。Charge虽有原服击杀事实，其替换CltDo112路径表现尚未恢复，不能据此认证位移显示。当前PD2范围见[技能目录](README.md#pd2远端技能范围)，版本与入口见[核心分析](../../resources/PD2_CN_DLL_ANALYSIS.md#角色技能树与客户端程序)。

## 逐项执行入口

Zeal、Vengeance、Smite、Sacrifice、Charge及Conversion均接原生联系。Sacrifice反噬在目标Drain之前；Conversion成功直接转换／撤出联系，失败才Drain；Charge移除固定超时及手写命中延迟。Smite保留盾伤预填、HitFlags2及仅补成功位。逐程序依据、版本差异和限制见[内核对照](../../modules/SERVER_SYSTEMS.md#旧近战链与随机迁移)。

表中数字为当前MPQ的SrvSt／SrvDo编号。定义、等级曲线、协同、状态、范围、筛选和费用由content/skills准备；D2MOO的D2Game/src/SKILLS/SkillPal.cpp、Skills.cpp、SUnitDmg.cpp及D2Common的PathMisc／SequenceTbls／Units提供执行依据，旧master单机实现用作对应接口证据。参考代码不纳入源码，许可见docs/licenses/D2MOO.txt。

| ID | 技能 | 原程序 | 当前执行 |
| --- | --- | --- | --- |
| 96 | Sacrifice | 29／64 | 武器物理增伤、协同、成功命中后的生命反噬；反噬先于目标Drain，背压保留阶段 |
| 97 | Smite | 150 | 必须有盾；盾牌基础伤害、Holy Shield、力量／增伤，自动命中、不可格挡、眩晕／击退，S1动作 |
| 98 | Might | 65 | 自身及所属伙伴的武器增伤 |
| 99 | Prayer | 65 | 周期恢复生命；有治疗对象才扣法力并抑制自然回蓝 |
| 100 | Resist Fire | 65 | 火抗／最大火抗；开启期间抑制本技能的被动最大抗性 |
| 101 | Holy Bolt | missile55／SrvHit7 | 对不死造成魔法伤害；治疗所属友方怪物；穿过不合格目标 |
| 102 | Holy Fire | 66 | 范围火焰脉冲、施法者武器火伤及协同 |
| 103 | Thorns | 65 | 近战命中后的物理反伤；所属伙伴同样接入 |
| 104 | Defiance | 65 | 防御百分比；自身／所属伙伴共用派生属性 |
| 105 | Resist Cold | 65 | 冰抗／最大冰抗与被动切换 |
| 106 | Zeal | 37／13 | 原攻击次数、回滚帧、协同；连续选择范围内目标，单目标可重复；行动期间拒绝移动取消 |
| 107 | Charge | 31／67 | 原跑速／光环速度、直线碰撞及接近阶段，到达后攻击／击退；贴身沿原规则普通攻击 |
| 108 | Blessed Aim | 65 | 主动命中加成及关闭后的基础等级被动贡献 |
| 109 | Cleansing | 65 | 缩短可治疗诅咒／毒剩余时间，Prayer恢复协同 |
| 110 | Resist Lightning | 65 | 电抗／最大电抗与被动切换 |
| 111 | Vengeance | 35／2 | 武器基础物理值生成火／冰／电附加伤害，独立协同与冰冻长度 |
| 112 | Blessed Hammer | 73／missile92 | PathMisc螺旋、穿透魔法伤害、不死／恶魔附加伤害及Concentration增益 |
| 113 | Concentration | 65 | 武器增伤、抗打断状态值及Blessed Hammer专用比例 |
| 114 | Holy Freeze | 81 | 范围冰伤、武器冰伤、受ColdEffect限制的移动／攻击减速、特殊首领过滤及死亡碎裂 |
| 115 | Vigor | 65 | 移速、耐力上限／恢复；参与Charge协同 |
| 116 | Conversion | 32／79 | 合格怪物命中后概率转换；Calc1／AuraLenCalc从原公式采样，阵营及level／maxhp来源随候选提交；低等级施放者保存整点资源，按原1.13c恢复回调与列表撤出顺序处理 |
| 117 | Holy Shield | 36／18 | 盾牌资格，持续时间、格挡、防御／Defiance协同与Smite基础加成；装备变化重算 |
| 118 | Holy Shock | 66 | 范围电伤、施法者武器电伤及协同 |
| 119 | Sanctuary | 66 | 不死限定魔法脉冲／击退，施法者武器对不死的物理抗性绕过 |
| 120 | Meditation | 65 | 玩家法力恢复与Prayer协同；原筛选不作用于怪物 |
| 121 | Fist of the Heavens | 80／SrvHit22 | 单位目标、10帧延迟电击、范围发射Holy Bolt、治疗／不死限定及延迟冷却；原CltHit26分裂 |
| 122 | Fanaticism | 65 | 立即脉冲，施法者完整增伤、所属伙伴半值增伤、命中／攻击速度 |
| 123 | Conviction | 66 | 防御及三抗削减；免疫目标减抗按1/5且刷新不重复折算；高等级自身Conviction抵挡低等级敌方Conviction |
| 124 | Redemption | 82 | 完整死亡且可选择尸体，逐尸体概率、恢复生命／法力、原redeemed状态及不可重复消耗 |
| 125 | Salvation | 65 | 三元素抗性及对应协同输入 |

## 生命周期与边界

effects/paladin持有光环作用／脉冲游标，源状态和目标状态分开；effects/right_aura处理玩家PeriodicSkills -1，effects/item_auras处理装备PeriodicStats，单位右手／列表光环由effects/hirelings及unit_aura_selection处理。换技能、等级、区域或死亡会移除自身来源；20项光环共用原PerDelay公式及signed32周期／寿命算子，目标状态寿命与下一事件的插入钳制分开。技能被动按激活状态抑制，物品加技能不替代基础等级协同。目标索引、随机快照、尸体消费及技能反噬保留在服务端，可靠输出背压不重复伤害或扣费。临时光环、转换、弹体不写存档。

Conversion资源路径按固定原1.13c二进制依据迁移。`gameplay/units/resources`的conversionResources先右移当前生命／上限，再用原百分比算子，左移8位后把低端夹到一个raw单位；只有目标等级高于施放者时保存整点maxhp及原等级。effects/units在候选中更新alignment、撤出States.aura列表、安装conversion_save只读来源并依序写level、hitpoints、maxhp；attributes/monster_resources执行逐变化回调，monsters复验修订后一次发布状态并提交候选。恢复时先更新alignment；有保存来源才恢复等级与生命、写上限、撤出保存来源及后续aura列表，没有保存来源时不擅自执行这段恢复／清理。

原D2Game RVA99830（SrvDo表79）通过RVA99997安装恢复回调98950：降级过程99AB6／99ACD在保存前右移8位，恢复98A0E对生命左移8位，98A1A–98A24却把保存的整点maxhp直接写回原始属性并触发上限回调；这条原圣骑士路径没有合并成“恢复保存的raw上限”。另一转换回调914F0／915BD会把保存上限左移8位，属于刺客路径，不能据此覆盖圣骑士行为。公共上限回调按原D95CC／D95DD／D95DF先除、乘、int64截断，再以signed32低位限幅；完整摘要与只读范围见[资料来源](../../resources/THIRD_PARTY.md#原版113c静态与开发对照证据)。转换的实际命中／耐久／随机采样时点、控制节点及同帧事件仍待后续动作与结算阶段迁移。

友方光环及Holy Bolt沿服务端共同关系校验；光环的拥有者、普通右手、物品GUID、佣兵／火魔及怪物来源共用原始公式与effects/paladin接收入口；目标按当前MPQ相邻房间（含已准备跨区域房间）、整数格平方距离、城镇／NoAura／阵营与mask4射线筛选，组队及PvP边界见[联网](../../modules/NETWORK.md)。房间单位链接AreaStore的头插／摘除入口；光环伤害与Holy Freeze伤害后SHATTER使用逐目标游标及单位种子，效果／战斗背压保留当前阶段。佣兵换装／创建与铁魔创建经共享候选发布接入物品光环回调；非玩家计时器按源GUID、到期帧及当前聚合等级执行，复用逐目标光环游标，死亡取消计时器。死亡清理先逆序撤出状态列表，再读取当前MPQ的staydeath掩码清除包括独立位在内的状态。全局单位事件入口、非玩家逐属性回调与即时技能的完全同步重入、完整死亡动作排序及极端有符号帧回绕仍有缺口。通用武器吸血／吸魔、压碎／撕裂已接公共[命中效果](../combat/WEAPON_EFFECTS.md)：Smite支持压碎／撕裂并排除普通装备吸取，Sacrifice反噬先于目标Drain。Concentration抗打断与人物恢复锁见[公共动画](COMMON.md#动画帧时钟与恢复)。

Holy Shield保留当前原盾组件。MPQ状态101没有可直接采用的特殊盾图组件规则；未核实原D2Client选择逻辑前不替换或自造图形。Charge移动显示原RN，到达用SequenceTbls中7个A1尾帧；权威位置由原移动／技能通知同步。Blessed Hammer服务端和客户端共用PathMisc路径算法，图形／音效只读取MPQ。

D2S仍为v96，见[存档](../../modules/SAVES.md)。通用职责、原版依据与实现边界见[COMMON](COMMON.md)。

光环来源准备现共用人物事务候选：玩家、佣兵换装／创建、铁魔材料及女武神装备在实际属性写入处执行回调，嵌套接收者借用同一 `MonsterResourceTrace`；来源程序及活跃借用必须在提交前退出。状态用独立 `State` 来源投影到当前 FullStats，显式查询替换旧投影，避免重复叠加；被动列表使用不可变替换，物品投递／撤出保留 item-applying-op 刷新。

即时与周期净化共用毒／诅咒期限程序，玩家毒候选按修订复验后随状态发布，非玩家沿自身资源候选提交。状态列表保留原有符号到期帧，移除事件单独处理下一帧钳制与连续事件；原始列表到期按最新列表先撤出。物品周期光环保留跨单位登记顺序、持续事件头插顺序、源 GUID 及重试游标；该局部机制不等于完整全局单位事件调度。

库存变化先保留旧投递来源，逐项撤出已卸下、移出库存、破损或切换武器组的贡献，再计算新装备资格；光环零值回调、独立状态位、被动刷新及计时器取消在同一候选内执行，不依靠最终属性重算或自然到期补救。现有铁魔的材料刷新使用真实实体与旧来源，不重新激活未变化的物品光环。

非玩家单技能／全部被动回调读取当前逐写快照，物品充能在 `item_charged_skill` 回调处按真实物品／参数层登记和撤销，光环公式及后续技能查询消费该缓存，避免提前读取最终装备。诊断公开玩家与当前区域单位的物品光环计时器，以及非玩家已登记的充能；运行态来源、计时器和技能缓存不写 D2S。伤害前技能反应已接公共事件路径，完整全局事件优先级、队列2物品事件交错及死亡动作顺序仍随后续动作／结算阶段推进；未运行边界不认证精确原版一致。

技能等级查询保留真实单位归属标志，读取当前逐写FullStats；仅需组装来源树的读者使用SourcesOnly，完整求值和物品快照由需要它们的入口执行。完整单位事件优先级、伤害前反应及非玩家同步重入的边界见[公共入口](COMMON.md#当前持续效果入口)与[实施范围](../../architecture/SERVER_COMBAT_RECONSTRUCTION.md#621-持续效果的实施范围与顺序)。

## 原服参考与缺口

2026-10-10由D2X客户端连接本机PvPGN→原版1.13c D2GS、TCP4000，在独立普通房间PalOrigSmoke观察Blessed Hammer112、Charge107、Holy Shield117。PalNetSmoke使用独立native-v96档准备99级／30项技能并修改姓名、重算校验；该条件不能作为原服自然成长依据。

Holy Shield施放后原服法力162→128，状态101含格挡14。Blessed Hammer施放后收到不死怪物生命128→0及死亡包，D2X显示missile92；后者不能作为原版客户端图像依据。Charge单位施放得到位置4830,4564→4842,4588、目标生命128→46和击退／受击包，点地施放得到4846,4588→4859,4587。正常退局后D2GS／D2DBS确认CHARSAVE、CHARINFO保存及解锁。

该参考只覆盖列出的三个技能和准备条件。所有等级／难度／装备组合、随机分支、并发与背压、首领／免疫筛选、多人队伍与PvP仍缺完整对照；Holy Shield特殊盾图继续暂缓。
