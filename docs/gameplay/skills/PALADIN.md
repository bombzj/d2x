# 圣骑士技能

更新：2026-10-10。自研服务端接入当前MPQ的96–125共30项职业技能：10项战斗技能、20项光环。Single Player、LAN及独立PvPGN服务端使用同一内核；客户端提交原技能包，读取权威结果。构建及有限运行证据在本文末尾维护，不以入口齐备代表全参数或原服认证。

## 逐项执行入口

Zeal、Vengeance、Smite、Sacrifice、Charge及Conversion均接原生联系。Sacrifice反噬在目标Drain之前；Conversion成功直接转换／撤出联系，失败才Drain；Charge移除固定超时及手写命中延迟。Smite保留盾伤预填、HitFlags2及仅补成功位。逐程序依据、版本差异和限制见[内核对照](../../modules/SERVER_SYSTEMS.md#旧近战链与随机迁移)。历史冒烟不认证新增迁移。

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

已发布包的effects/paladin持有光环作用／脉冲游标，源状态和目标状态分开；当前未构建源码由effects/right_aura处理玩家PeriodicSkills -1，effects/item_auras处理装备PeriodicStats，单位右手／列表光环由effects/hirelings及unit_aura_selection处理。换技能、等级、区域或死亡会移除自身来源；源码中的20项光环现开始共用原PerDelay公式及signed32周期／寿命算子；目标状态寿命与下一事件的插入钳制分开。技能被动按激活状态抑制，物品加技能不替代基础等级协同。目标索引、随机快照、尸体消费及技能反噬保留在服务端，可靠输出背压不重复伤害或扣费。临时光环、转换、弹体不写存档。

Conversion资源路径已按固定原1.13c二进制证据迁移，并纳入前序Windows Release构建；完整转换流程未运行复验。`gameplay/units/resources`的conversionResources先右移当前生命／上限，再用原百分比算子，左移8位后把低端夹到一个raw单位；只有目标等级高于施放者时保存整点maxhp及原等级。effects/units在候选中更新alignment、撤出States.aura列表、安装conversion_save只读来源并依序写level、hitpoints、maxhp；attributes/monster_resources执行逐变化回调，monsters复验修订后一次发布状态并提交候选。恢复时先更新alignment；有保存来源才恢复等级与生命、写上限、撤出保存来源及后续aura列表，没有保存来源时不擅自执行这段恢复／清理。

原D2Game RVA99830（SrvDo表79）通过RVA99997安装恢复回调98950：降级过程99AB6／99ACD在保存前右移8位，恢复98A0E对生命左移8位，98A1A–98A24却把保存的整点maxhp直接写回原始属性并触发上限回调；这条原圣骑士路径没有合并成“恢复保存的raw上限”。另一转换回调914F0／915BD会把保存上限左移8位，属于刺客路径，不能据此覆盖圣骑士行为。公共上限回调按原D95CC／D95DD／D95DF先除、乘、int64截断，再以signed32低位限幅；完整摘要与只读范围见[资料来源](../../resources/THIRD_PARTY.md#原版113c静态与开发对照证据)。转换的实际命中／耐久／随机采样时点、控制节点及同帧事件仍待后续动作与结算阶段迁移；既有V2只认证下方历史构建，不能认证这一源码变更。

友方光环及Holy Bolt沿服务端共同关系校验；光环的拥有者、普通右手、物品GUID、佣兵／火魔及怪物来源开始共用原始公式与effects/paladin接收入口；目标按当前MPQ相邻房间（含已准备跨区域房间）、整数格平方距离、城镇／NoAura／阵营与mask4射线筛选，组队及PvP边界见[联网](../../modules/NETWORK.md)。这些源码已纳入2026-10-14 Windows Release包，有限冒烟见下节；房间单位链已开始接AreaStore的头插／摘除入口；光环伤害与Holy Freeze伤害后SHATTER又接逐目标游标及单位种子，效果／战斗背压保留当前阶段。佣兵换装／创建与铁魔创建现经共享候选发布接入物品光环回调；非玩家计时器按源GUID、到期帧及当前聚合等级执行，复用逐目标光环游标，死亡取消计时器。死亡清理先逆序撤出状态列表，再读取当前MPQ的staydeath掩码清除包括独立位在内的状态。全局单位事件入口、非玩家逐属性回调与即时技能的完全同步重入、完整死亡动作排序及极端有符号帧回绕仍未精确认证；本次Release构建及三项游戏内冒烟通过，但不认证全部等级、装备组合或完整原版一致性。通用武器吸血／吸魔、压碎／撕裂已接公共[命中效果](../combat/WEAPON_EFFECTS.md)：Smite支持压碎／撕裂并排除普通装备吸取，Sacrifice在吸取后结算反噬。Concentration抗打断与人物恢复锁见[公共动画](COMMON.md#动画帧时钟与恢复)。

Holy Shield保留当前原盾组件。MPQ状态101没有可直接采用的特殊盾图组件规则；未核实原D2Client选择逻辑前不替换或自造图形。Charge移动显示原RN，到达用SequenceTbls中7个A1尾帧；权威位置由原移动／技能通知同步。Blessed Hammer服务端和客户端共用PathMisc路径算法，图形／音效只读取MPQ。

D2S仍为v96，见[存档](../../modules/SAVES.md)。通用职责与证据等级见[COMMON](COMMON.md)。

光环来源准备现共用人物事务候选：玩家、佣兵换装／创建、铁魔材料及女武神装备在实际属性写入处执行回调，嵌套接收者借用同一 `MonsterResourceTrace`；来源程序及活跃借用必须在提交前退出。状态用独立 `State` 来源投影到当前 FullStats，显式查询替换旧投影，避免重复叠加；被动列表使用不可变替换，物品投递／撤出保留 item-applying-op 刷新。

即时与周期净化共用毒／诅咒期限程序，玩家毒候选按修订复验后随状态发布，非玩家沿自身资源候选提交。状态列表保留原有符号到期帧，移除事件单独处理下一帧钳制与连续事件；原始列表到期按最新列表先撤出。物品周期光环保留跨单位登记顺序、持续事件头插顺序、源 GUID 及重试游标；该局部机制不等于完整全局单位事件调度。

库存变化先保留旧投递来源，逐项撤出已卸下、移出库存、破损或切换武器组的贡献，再计算新装备资格；光环零值回调、独立状态位、被动刷新及计时器取消在同一候选内执行，不依靠最终属性重算或自然到期补救。现有铁魔的材料刷新使用真实实体与旧来源，不重新激活未变化的物品光环。

非玩家单技能／全部被动回调读取当前逐写快照，物品充能在 `item_charged_skill` 回调处按真实物品／参数层登记和撤销，光环公式及后续技能查询消费该缓存，避免提前读取最终装备。诊断公开玩家与当前区域单位的物品光环计时器，以及非玩家已登记的充能；运行态来源、计时器和技能缓存不写 D2S。伤害前技能反应已接公共事件路径，完整全局事件优先级、队列2物品事件交错及死亡动作顺序仍随后续动作／结算阶段推进；未运行边界不认证精确原版一致。

## 有限冒烟

2026-10-14光环收尾证据在`artifacts/aura-closure-20261014-170741`。Windows Release配置、客户端／独立服务端／资源工具及最终增量构建通过，两包更新；身份见构建指南。主菜单Single Player入局和保存重入有限通过，最终包再以隔离84级Hero进入城镇，截图可见实际游戏，error为空。最终包力量1级stat25=40；狂热1级stat25=50、68／69=14、119=40；神圣冰冻1级stat54／55=10／15。切换Attack后以上光环状态清除，宿主failures=0。

当前MPQ实际五孔War Axe按The Beast的r30／r03／r22／r23／r17组装，9级狂热raw25=186、68／69=29、119=80；13级变熊充能5/5。运行暴露卸下物品后状态49残留，修复为保留旧投递来源并先执行零值回调；最终包换回普通剑即清状态、GUID计时器及充能。当前MPQ四孔Gothic Plate的Bramble按r08／r27／r29／r05组装，84级Prayer佣兵装备后18级荆棘stat131=930；13级荆棘灵气充能7/33。最终包确认充能在实际物品回调登记，同区卸装后计时器及充能立即清空。修复包另观察佣兵城镇来源向相邻野外玩家投递荆棘；跨区远离佣兵的卸装请求被正常距离资格拒绝，回同区后卸装成功。

修复包神圣冰冻对当前MPQ zombie实际脉冲观察55帧生命12→9.578125（含再生），目标state44的67／68／69均为−30；切换后清除。最终包正常保存、原69退局返回CharacterSelection并退出。管理入口仅准备等级／物品／位置／怪物及固定帧观察；真实装备、切换和退局使用原请求。本轮不认证铁魔材料光环实机、所有装备／等级／多人、全部单位事件优先级及伤害前反应；此后诅咒、祭坛祝福、野蛮人吼叫及伤害前技能事件已完成前序模块构建与列出路径的有限观察，当前继续迁移飞弹／周期事件；各自边界见[公共入口](COMMON.md#当前持续效果入口)及[实施范围](../../architecture/SERVER_COMBAT_RECONSTRUCTION.md#621-持续效果的实施范围与顺序)。


2026-10-14，当前MPQ、Windows Release客户端／独立服／资源工具配置、编译及链接通过，两包更新；证据在`artifacts/aura-smoke-20261014-150146`。隔离35级Paladin通过原协议学习Might／Holy Fire／Blessed Aim／Concentration／Holy Freeze／Fanaticism各1级。实际新包单机正常运行：力量状态33的damagepercent=40；狂热状态49的拥有者damagepercent=50、attackrate／other_animrate=14、命中百分比=40，切换撤销旧状态。神圣冰冻状态43的coldmindam／coldmaxdam=10／15；目标110取得状态44，velocitypercent／attackrate／other_animrate均为-30。55帧内目标生命8→5.11328125；围墙另一侧目标109保持11，观察与filter的mask4射线门一致。停止光环后拥有者43及目标44均撤销，三项原图截图已查看。正常保存／原69离局返回CharacterSelection，新进程恢复35级、六项技能及选中114并重建43；最终宿主failures=0，v96存档解码通过。

首次运行暴露光环切换时重复整树属性求值造成心跳超时；保留失败包／栈和日志，修复后重建、重打包并重跑上述三项。技能等级查询保留真实单位归属标志，读取当前逐写FullStats；仅需组装来源树的读者使用SourcesOnly，完整求值和物品快照仍由需要它们的入口执行。未放宽心跳期限或改公式／取整。此次不覆盖物品光环的佣兵／铁魔装备组合、多玩家、并发背压、所有等级难度与原版事件优先级；非玩家即时回调的完全同步重入仍需继续对齐。

2026-10-10按当前Windows Debug配置构建并打包客户端和独立服务端；本批运行证据在忽略目录`artifacts/paladin-implementation-20261010`。Single Player使用内嵌权威及原online-*包，管理入口只准备等级、资源、怪物与固定步观察条件；未新增测试脚本、用例或专用程序。

99级Paladin逐ID学习96–125，30项均收到原技能确认；20项右手光环逐项切换并收到本人状态，观察Prayer扣费治疗、Cleansing／Meditation协同治疗、抗性／防御／命中属性与切换清理。战斗路径观察Sacrifice命中反噬、带Holy Shield的Smite命中／击退、rank4 Zeal连续攻击、Holy Bolt击杀不死与治疗转换单位、Vengeance伤害／冰冻、Blessed Hammer螺旋弹体及命中、FoH十帧延迟电击、Charge直线接近／命中／击退及障碍中止。Conversion rank20成功转换，所属光环及治疗生效，400帧后恢复敌方归属；Holy Fire／Freeze／Shock／Sanctuary取得脉冲伤害，Holy Freeze取得减速状态，Conviction取得目标防御／三抗削减，Thorns取得受击后的反伤，Redemption消耗五具尸体并恢复生命。

Holy Shield状态101及防御9→12；卸盾后防御降至5、施放拒绝，重新装备恢复12。正常保存退出后，新进程读回30项技能、Zeal4／Conversion20及剩余46技能点；临时Holy Shield未写盘，选中光环重建，换区及重新施放通过。首次运行发现MPQ注释式pSrvHitFunc解析失败及失败后局部UI对象重用空指针，已修复并在完整学习／光环、战斗及新进程换区中复验。

单机完成后抽取Blessed Hammer112、Charge107、Holy Shield117，以同一客户端包连接本机PvPGN→原版D2GS（既有`artifacts/d2gs-local/d2gs/D2GS.exe`，TCP4000，非d2x_server），独立普通房间`PalOrigSmoke`。本轮新建PalNetSmoke的一级原档及charinfo先备份，再用已验证native-v96单机档准备99级／30项技能条件；只修改该测试角色的原名字字段并重算原校验，不覆盖旧角色。D2GS日志确认99级Paladin入局。Holy Shield通过UI施放，法力162→128，原状态101含格挡14；Blessed Hammer通过UI施放，客户端missile92取得路径位置及原图截图，法力扣除，并收到不死怪物生命128→0及死亡原包；Charge单位施放取得位置4830,4564→4842,4588、目标生命128→46和击退／受击原包，UI点地施放取得4846,4588→4859,4587。正常退局后D2GS／D2DBS确认CHARSAVE、CHARINFO保存成功及解锁；本轮客户端退出，既有原服进程与其他房间保留。

上述为有限冒烟，不覆盖所有等级／难度／装备组合、随机分支、并发与背压、全部首领／免疫筛选、完整多人队伍与PvP；原服只认证列出的三个代表技能及当前客户端路径，独立d2x_server按用户要求不重复技能冒烟。Holy Shield特殊盾图仍按上节明确暂缓。
