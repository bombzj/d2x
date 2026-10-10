# 圣骑士技能

更新：2026-10-10。自研服务端接入当前MPQ的96–125共30项职业技能：10项战斗技能、20项光环。Single Player、LAN及独立PvPGN服务端使用同一内核；客户端提交原技能包，读取权威结果。构建及有限运行证据在本文末尾维护，不以入口齐备代表全参数或原服认证。

## 逐项执行入口

表中数字为当前MPQ的SrvSt／SrvDo编号。定义、等级曲线、协同、状态、范围、筛选和费用由content/skills准备；D2MOO的D2Game/src/SKILLS/SkillPal.cpp、Skills.cpp、SUnitDmg.cpp及D2Common的PathMisc／SequenceTbls／Units提供执行依据，旧master单机实现用作对应接口证据。参考代码不纳入源码，许可见docs/licenses/D2MOO.txt。

| ID | 技能 | 原程序 | 当前执行 |
| --- | --- | --- | --- |
| 96 | Sacrifice | 29／64 | 武器物理增伤、协同、成功命中后的生命反噬；背压保留后置阶段 |
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
| 116 | Conversion | 32／79 | 合格怪物成功命中后概率转换；临时所属AI／阵营，等级／生命比例保存，400帧到期恢复并清除光环 |
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

server/effects/paladin.cpp按右手当前武器组的有效等级持有发射周期；源状态和目标状态分开。换技能、等级、区域或死亡会移除自身来源；目标效果沿原周期加一帧到期。技能被动按激活状态抑制，物品加技能不替代基础等级协同。目标索引、随机快照、尸体消费及技能反噬保留在服务端，可靠输出背压不重复伤害或扣费。临时光环、转换、弹体不写存档。

当前没有组队／敌意系统。沿D2MOO同队资格，友方光环及Holy Bolt治疗范围为本人和本人所属佣兵／召唤物／转换怪物；不能把其他玩家自动当队友。PvP、队伍加成、其他职业／装备自带的光环发射及复杂双持组合暂缓。通用武器吸血／吸魔、压碎／撕裂仍沿既有明确拒绝，不能由本职业入口推断这些装备效果完成。Concentration的抗打断状态已投影；完整人物受击恢复规则仍属公共战斗缺口。

Holy Shield保留当前原盾组件。MPQ状态101没有可直接采用的特殊盾图组件规则；未核实原D2Client选择逻辑前不替换或自造图形。Charge移动显示原RN，到达用SequenceTbls中7个A1尾帧；权威位置由原移动／技能通知同步。Blessed Hammer服务端和客户端共用PathMisc路径算法，图形／音效只读取MPQ。

D2S仍为v96，准入指纹升至`d2x-character-admission-v31/native-wire113c/d2s96/act1-hireling/pvpgn-newbie89/player-trade-chat/paladin30`；旧规则不静默迁移，见[存档](../../modules/SAVES.md)。通用职责与证据等级见[COMMON](COMMON.md)。

## 有限冒烟

2026-10-10按当前Windows Debug配置构建并打包客户端和独立服务端；本批运行证据在忽略目录`artifacts/paladin-implementation-20261010`。Single Player使用内嵌权威及原online-*包，管理入口只准备等级、资源、怪物与固定步观察条件；未新增测试脚本、用例或专用程序。

99级Paladin逐ID学习96–125，30项均收到原技能确认；20项右手光环逐项切换并收到本人状态，观察Prayer扣费治疗、Cleansing／Meditation协同治疗、抗性／防御／命中属性与切换清理。战斗路径观察Sacrifice命中反噬、带Holy Shield的Smite命中／击退、rank4 Zeal连续攻击、Holy Bolt击杀不死与治疗转换单位、Vengeance伤害／冰冻、Blessed Hammer螺旋弹体及命中、FoH十帧延迟电击、Charge直线接近／命中／击退及障碍中止。Conversion rank20成功转换，所属光环及治疗生效，400帧后恢复敌方归属；Holy Fire／Freeze／Shock／Sanctuary取得脉冲伤害，Holy Freeze取得减速状态，Conviction取得目标防御／三抗削减，Thorns取得受击后的反伤，Redemption消耗五具尸体并恢复生命。

Holy Shield状态101及防御9→12；卸盾后防御降至5、施放拒绝，重新装备恢复12。正常保存退出后，新进程读回30项技能、Zeal4／Conversion20及剩余46技能点；临时Holy Shield未写盘，选中光环重建，换区及重新施放通过。首次运行发现MPQ注释式pSrvHitFunc解析失败及失败后局部UI对象重用空指针，已修复并在完整学习／光环、战斗及新进程换区中复验。

单机完成后抽取Blessed Hammer112、Charge107、Holy Shield117，以同一客户端包连接本机PvPGN→原版D2GS（既有`artifacts/d2gs-local/d2gs/D2GS.exe`，TCP4000，非d2x_server），独立普通房间`PalOrigSmoke`。本轮新建PalNetSmoke的一级原档及charinfo先备份，再用已验证native-v96单机档准备99级／30项技能条件；只修改该测试角色的原名字字段并重算原校验，不覆盖旧角色。D2GS日志确认99级Paladin入局。Holy Shield通过UI施放，法力162→128，原状态101含格挡14；Blessed Hammer通过UI施放，客户端missile92取得路径位置及原图截图，法力扣除，并收到不死怪物生命128→0及死亡原包；Charge单位施放取得位置4830,4564→4842,4588、目标生命128→46和击退／受击原包，UI点地施放取得4846,4588→4859,4587。正常退局后D2GS／D2DBS确认CHARSAVE、CHARINFO保存成功及解锁；本轮客户端退出，既有原服进程与其他房间保留。

上述为有限冒烟，不覆盖所有等级／难度／装备组合、随机分支、并发与背压、全部首领／免疫筛选、完整多人队伍与PvP；原服只认证列出的三个代表技能及当前客户端路径，独立d2x_server按用户要求不重复技能冒烟。Holy Shield特殊盾图仍按上节明确暂缓。
