# 女巫技能

更新：2026-10-09。自研服务端执行26项主动、4项被动，原服与自研共用原协议客户端。此页维护30项职责、女巫专属Clt／Srv差异、原版依据和缺口；公共规范与整体状态见[COMMON](COMMON.md)。

女巫持续状态现从公共原公式环境准备AuraLen／AuraStat／Passive槽及modifierlist元数据，使用独立状态位与原有符号移除事件。冰甲按原状态组先撤出再投递，附魔保持同级复用及后续非零写入；Blaze、Thunder Storm及Energy Shield采用对应原始列表生命周期。能量盾百分比／耗蓝因子读取calc1／calc2，魔法耗尽撤出状态并随同一人物候选发布事实，骨甲池写入／撤出也在资源观察边界执行。单位事件登记次序已独立保存；骨甲与能量盾按登记先后处理，能量盾耗尽在接收流程内撤出，冻结反应读取当前原calc整数帧。完整伤害前派发及所有跨职业组合仍需迁移／对齐。

## 30项公共职责核对

按当前MPQ的Skills／Missiles函数号定位本地D2MOO。空SrvDo经通用 `D2GAME_SKILLS_Handler_6FD12BA0` 创建srvmissile，不是未实现；被动不走施法入口。C＝公共法力／等级公式，D＝公共技能元素伤害／协同／支配，L＝公共元素长度，P＝公共线性被动；通用取整与权限见[跨职业计算](COMMON.md#跨职业公共计算)。表中已接入口不代表全部参数或表现已认证。

| ID／技能 | 原SrvSt／SrvDo及执行证据 | 公共计算／定义 | 两端执行边界 |
| --- | --- | --- | --- |
| 36 Fire Bolt | 通用Handler；MissMode SrvDo1 | C、D、速度／碰撞几何、SC时钟 | 服务端单体命中；客户端原图飞行 |
| 37 Warmth | D2Common RefreshSkill/passivecalc | P：skillRankBonus | 服务端attributes安装恢复贡献；客户端提示 |
| 38 Charged Bolt | SkillSor SrvDo17／MissileInit_ChargedBolt | C、D、数量曲线、chargedBoltPath | 独立权威／视觉弹体，索引和77步上限相同 |
| 39 Ice Bolt | 通用Handler；MissMode SrvDo1 | C、D、L、速度／碰撞、SC | 真实减速归服务端；客户端接状态 |
| 40 Frozen Armor | SkillSor SrvDo18、EventFunc02 | C、等级防御／期限／冻结公式 | effects互斥安装、近战伤害事件；客户端状态覆盖层 |
| 41 Inferno | SkillSor SrvSt11／SrvDo19／DoInferno | C、D、线性射流长度、seq6步序／固定时钟 | 服务器持续释放／扣蓝；客户端24选择两种火图 |
| 42 Static Field | SkillSor SrvDo20／AuraCallback_StaticField | C、比例／下限／范围准备值 | 真实生命、难度下限和抗性仅服务端；客户端效果 |
| 43 Telekinesis | SkillSor SrvSt12／SrvDo21 | C、D、距离／击退几何 | 服务端资格、拾取／物件事务；客户端原目标／确认 |
| 44 Frost Nova | SkillSor SrvDo22 | C、D、L、missileRingBurst／Direction | 原CltDo25视觉环；服务器弹体及冷伤 |
| 45 Ice Blast | 通用Handler；MissMode SrvDmg4 | C、D、L、速度／碰撞 | 服务端难度冻结长度；客户端接原状态／冰图 |
| 46 Blaze | SkillSor SrvDo23／CreateBlazeMissile；MissMode SrvDo5／Dmg3 | C、D、期限；整数跨格规则 | 服务端状态移动留火／逐帧伤；客户端state setfunc3及CltDo5 |
| 47 Fire Ball | 通用Handler；MissMode SrvHit1 | C、D、速度／碰撞／范围几何 | 服务端范围目标与伤害；客户端CltHit1爆炸 |
| 48 Nova | SkillSor SrvDo22 | C、D、missileRingBurst／Direction | 原CltDo25视觉环；服务器雷伤 |
| 49 Lightning | 通用Handler；MissMode SrvDo1 | C、D最小值门槛、seq12、速度／碰撞 | 服务端穿透／NextDelay；客户端CltDo8链段图 |
| 50 Shiver Armor | SkillSor SrvDo18、EventFunc03 | C、D、L、等级防御／期限 | 服务端近战尝试反击；客户端状态／命中 |
| 51 Fire Wall | SkillSor SrvDo24；MissMode SrvDo6／Dmg3 | C、D、missileWallDirection、跨格、速度 | 两侧maker生成各端执行，服务器真实火段伤害 |
| 52 Enchant | SkillSor SrvDo25 | C、D、线性期限／命中率 | 服务端友方资格、来源及成组事务；客户端原状态 |
| 53 Chain Lightning | SkillSor SrvDo26；MissMode SrvHit12 | C、D最小值门槛、seq12、跳数、GUID successor | 真实候选／再次命中归服务端；客户端CltHit16可见链段 |
| 54 Teleport | SkillSor SrvDo27 | C、位置／碰撞几何 | 服务端资格／位移；客户端原位置校正 |
| 55 Glacial Spike | 通用Handler；MissMode SrvHit13 | C、D、L、范围／冻结等级曲线 | 服务端接触范围冷伤／冻结；客户端CltHit14冰图 |
| 56 Meteor | SkillSor SrvDo28；MissMode SrvHit14 | C、D；地面火evaluateMissileDamageFixed；范围／期限 | 18个权威火偏移与客户端CltDo9／Hit18图层分开 |
| 57 Thunder Storm | SkillSor SrvSt13／SrvDo29 | C、D、linear期限、dm56递减周期 | 服务端定时选敌／落雷；客户端原A3瞬时图 |
| 58 Energy Shield | SkillSor SrvDo23、EventFunc24 | C、吸收上限／TK协同、dm12期限、absorbSkillShield | 服务端受击扣蓝／生命／状态事务；客户端原状态 |
| 59 Blizzard | SkillSor SrvDo28；MissMode SrvDo10／CreateMissileWithCollisionCheck | C、D、missileEmissionDue／blizzardOffset | 独立随机、客户端反向偏移／四变体，实际碎片碰撞归服务端 |
| 60 Chilling Armor | SkillSor SrvDo18、EventFunc01 | C、D、L、防御／期限、速度 | 服务端ReturnFire资格／反击弹体；客户端原状态／动作 |
| 61 Fire Mastery | D2Common RefreshSkill/passivecalc | P：skillRankBonus，D中支配阶段 | 服务端有效等级加伤，客户端已知值提示 |
| 62 Hydra | SkillSor SrvSt14／SrvDo144；MonsterAI Hydra | C、主人原Passive／Aura／Calc／SumSkill、PetMax、原整数期限；自身337等级与飞弹关联62伤害公式 | 服务端三头原始属性／归属／AI，攻击接怪物动作队列；客户端原指派与动作，完整Hydra行为仍待对齐 |
| 63 Lightning Mastery | D2Common RefreshSkill/passivecalc | P：skillRankBonus，D中支配阶段 | 服务端有效等级加伤，客户端提示 |
| 64 Frozen Orb | 通用Handler；MissMode SrvDo15／16／SrvHit29 | C、D、L、remaining调度／环形／转向／速度 | 独立Clt19／20／Hit30程序，服务端子弹重新求伤害 |
| 65 Cold Mastery | D2Common RefreshSkill/passivecalc | P：skillRankBonus | 服务端抗性穿透且不破免疫，客户端提示；不是冷伤百分比 |

## 公共层及原版证据

迁入起点为master的 `session_skills`、`projectile_launch`、`frozen_orb`、`blizzard`、`arc`、`glacial_spike`、`meteor`、`firewall`、`elemental_runtime`、`applied_effects`、`reactions`、`shield`及伙伴代码。当前MPQ决定参数、图形与身份；原分派查 [D2Skills.cpp](../../../reference/d2moo/source/D2Common/src/D2Skills.cpp)、[Units/Missile.cpp](../../../reference/d2moo/source/D2Common/src/Units/Missile.cpp)、[SkillSor.cpp](../../../reference/d2moo/source/D2Game/src/SKILLS/SkillSor.cpp) 和 [MissMode.cpp](../../../reference/d2moo/source/D2Game/src/MISSILES/MissMode.cpp)。固定版本与DLL摘要见[资料来源](../../resources/THIRD_PARTY.md)；模块导入／序列及数值取整证据只维护在[参考设计](../../architecture/REFERENCE_DESIGN.md#女巫公共层的原版核对)。

`resolveSkill`供服务端求值与客户端提示，`rank_bonus`处理四被动。弹体复用 `projectile_path`／combat几何；具体专项函数已列在逐项表，不维护第二份家族清单。客户端只消费已知值，不重新安装支配、护盾或伤害。

### 时序与弹体差异

- `prepareCastAnimationTiming`／`normalCastTiming`／`playerCastSequenceTiming`两端共用SC／seq12时钟，seq6固定速率。本人FCR优先取收到的属性；必要时从完整装备／套装／状态列表汇总，缺输入保持未知，远端未知FCR仍使用基础显示速率。
- 冰封球用原GetRemainingFrames的TotalFrames−CurrentFrame差值判散射与转向，保留Clt／Srv参数及零帧发射。0x73带剩余寿命时，客户端时钟已有已过帧前缀，不能重复扣除。子弹出生时服务端重新求当前支配／协同；撞墙不补自然到期环射。
- `advanceMissileVelocity`共用五帧加速／限速，`missileChangedCell`共用跨格条件；两端固定点与cells/second表示仍各自适配。Blaze留火、火墙maker、Meteor落地及Blizzard周期持有各端创建队列，尚未共用完整调度／路径状态。
- Blizzard共用整数落点，但客户端按remaining／globalX重种并保留反向偏移／图像变体，服务端使用自己的随机与真实碰撞。Meteor服务端18个火偏移与客户端分层图像分别执行，独立地面火使用Missiles伤害曲线。

本地1.13c D2Client静态补证：Inferno CltDo24经RVA74D50／74930选择两种火图；Blizzard CltMissile13格点生成与序列导入证据见参考设计。Fire Wall CltDo26经74770生成两侧maker及中心火；Meteor／Blizzard CltDo28经73DF0→A1540→AFF10在目标点创建中心弹体，这些Clt程序不以ClientSend阻止本地创建。

### 领域和原包行为

`actions`持有玩家／怪物待释放动作，`skills`解释施放、冷却、释放及射流；`missiles`持有权威飞行／子弹，`combat/spell_damage`处理逐目标伤害／NextDelay，`effects`持有装甲反击、吸收及周期，`companions`持有Hydra归属／期限／AI，单位属性／生命／模式仍归monsters。Hydra不再由伙伴另发AttackFact及玩家发射请求，改为请求337怪物技能，沿相同动作／释放入口发射。心灵传动复用inventory／objects／travel各自资格和事务；客户端开仓库／传送点等待原77／63，地面目标来自原物品副本，失败拾取音效为原2C的0x13。

未释放动作随移动／死亡／换区取消；已释放弹体可在施法者死亡后完成，玩家离开或区域代次变化时清理。持续Inferno不能将每帧续射的结束刻误投影为站立，原0D只在动作真实结束后发出。受控Hydra不发敌怪经验／掉落；三头生命周期、337弹体及强化沿各自领域。

hosting独立prepareHydraRules从当前MPQ准备三头／三难度基础、初始技能槽、生命及模式表、普通／L-AC比例、LoD区域等级和PetType；服务端不读取原表。原单位初始化后先赋Hydra自身技能，再安装主人原Passive／Aura／Calc1／SumSkill，因此火焰精通为召唤时的基础贡献。共用prepareSummonStatSources，关闭SetSummonBaseStats和召唤抵抗；初始生命按生命表投掷并保留raw单位。当前三头生命表为零；maxhp的fMin／MinAccr／ValShift不代表缺项查询最小生命，keepzero关闭时SetStat(0)移除字段，缺项仍返回零。实体通过modeAlive保持活动，死亡模式清除该标记；参与者查询、动作入口、死亡／到期、拥有者清理和复制开始区分原生命与存活，不人为安装256。

原AI用自身单位种子以60%请求337；首次AI按SummonPet的25帧安排，期限由Param1／Param2原signed32计算，Hydra仅在AI到达时检查严格大于到期帧。普通AI／动作中立返回仍沿现有调度，容量失败保留该次随机输入。原Missiles关联技能62与发射技能337分开保存在MissileSkillDamage：发射候选从本头的当前有效等级和原始属性求物理、元素、支配及长度；不再在发射时重新取玩家当前精通。固定原1.13c D2Game.dll（MD5 `978f6f74cd031bce6ecee602cd5988cc`）Do144表项为RVA43D40，43F19–43F30确认自身技能赋值在公共召唤贡献之前。完整出生／碰撞失败、控制节点、原Missile程序及roll时点、同帧／跨帧事件顺序和逐版本公式差异仍需后续阶段闭合。

当前源码将Hydra三头按原偏移依次执行公共怪物出生−1／4搜索，房间随机贷款与游戏种子随人物事务复验、接受后共同发布；全失败只保留实际房间搜索消费，不提交单位种子。三头从当前MPQ准备SizeX=0、TotalPieces=2及躯干组件，单位流先消费组件单选，再投掷生命；首个Actor AI事件直接登记出生后25帧，不先触发下一帧空回调。完整出生初始化AI、逐头容量回调、非玩家施法者及全局交错仍未闭合。

三种装甲互斥；Frozen Armor响应近战伤害，Shiver Armor响应近战尝试；Chilling Armor在HitByMissile阶段按实际飞弹的ReturnFire资格反击，准确率／格挡／回避及基础扣血之前即可触发，落空仍可反击。反击弹体的创建随机流及公共事件边界见[公共持续效果入口](COMMON.md#当前持续效果入口)。Energy Shield先吸收再减免，以基础Telekinesis协同求耗蓝；生命／蓝／清盾成组提交。Enchant接受同区友方玩家、已接伙伴及真实NPC，赋予状态不表示NPC攻击AI已实现。

原服省略普通本人4C／4D；上述可重建地面程序及其火段也不按每次施法广播73。自研同步遵守该行为，其他可见玩家仍收到动作；客户端保留0x73视野同步及有界不同来源配对，不合并两次独立施法。原表CelFile=null且有Light的弹体仅产生光源，不制造替代贴图。

## 边界

- 同区Teleport资格／碰撞及位置事务已接；同行者传送、跨区弹体及完整路径定点量化未完成。
- Chilling Armor的完整远程AI、ReturnFire来源组合及客户端接触流程仍待对齐。
- 碎冰、爆炸密度、连锁在延迟／可见性变化下的时序、循环声音、三难度／全装备／协同等级／免疫和背压仍没有完整认证。
- 充能、装备触发、武器通用限制由[库存](../../modules/INVENTORY.md)及[GENERAL](GENERAL.md)维护；保存字段与校验边界由[SAVES](../../modules/SAVES.md)维护。晚入视野和客户端权限沿COMMON，不以本页30项入口扩张其他职业或宣称完整原版对齐。

## 原服代表技能与发包收敛

本机1.13c D2GS／PvPGN参考栈（本地RElesgoe适配构建），D2X客户端使用普通难度99级、30项基础等级1的SkillTestSor，角色来自D2DBS，未上传自研D2S。服务端消息与状态可作为所列原服行为参考；画面由D2X重建，不等于原版客户端画面，适配参考栈也不代表全部纯原版规则。

| 实际路径 | 观察与限制 |
| --- | --- |
| 直接与弹体技能 | 心灵传动真实仓库／传送点77／63开窗；Teleport位置／扣蓝；Blizzard怪物死亡；Chain Lightning目标生命比例128→0。Frozen Orb散射、Meteor标记／落地火、Fire Wall两侧展开及Blaze移动留火是该连接中的D2X画面观察，原版逐帧图形仍待核对 |
| Hydra／Inferno | 三头原351／352／353及4C技能337；Inferno开始、转向及停止显示曾被观察，但点施法复查未捕获持续火焰，原服连续伤害／扣蓝节奏仍待核对 |
| 原包对照 | 常规本人4C／4D及所列地面程序重复73未出现；按PlrMsg::sub_6FC81D20、SUnitMsg::FirstFn／MISSILES_SyncToClient及CltDo26／28解释服务端通知与客户端重建职责 |

参考连接仍有未消费包；一次握手超时在重启D2GS后恢复，原因未确定。全30项原服参数、跨机器LAN、Linux、长期稳定性、PvP、完整充能／触发、极限召唤／背压、断线及像素／人工听音仍待对齐。
