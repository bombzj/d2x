# 女巫技能

本页维护女巫技能的原表规则、实现及已有证据。通用执行／状态见 [公共技能](COMMON.md)，代码依赖见 [技能模块](../../modules/SKILL_RUNTIME.md)。

## 女巫优先技能

### 本轮冒烟

2026-10-02用户后续授权：使用最终dist/current的普通Sorceress、seed210、关卡8临时新角色，通过既有管道升级到76级并按原前置学习，无存档输入／输出，不新增测试脚本、用例或专用程序。三个实例正常退出0、stderr空。仅为有限冒烟，不代替完整原版认证；下文各项“未运行”是实现时状态，本节补充本次运行证据。

| 范围 | 实际观察 |
| --- | --- |
| 启动／原图 | 管道就绪，heroAppearanceError为空，三头海蛇与护盾原叠层截图已查看。 |
| Fire Wall | 原69火焰生成，独立巨兽20帧内28→23.42；没有把多个同时生效的技能击杀当作单项证据。 |
| Blaze | 建立原状态13，实际移动4子格生成4处原67火焰，状态随后到期。 |
| Enchant | 本人附火伤11–14、命中75→90；最终包精确鼠标对佣兵施法后原状态16／来源52，命中279→334、显示伤害1–4→12–18。 |
| Energy Shield | 施法、耗蓝、原状态30和叠层86确认；独立100帧逐帧现场未观察到有效命中，吸收比例、缺蓝失效及全部伤害通道仍未运行认证。 |
| Thunder Storm | 原状态38，海蛇到期后的独立现场击杀27HP巨兽。 |
| Telekinesis | 真实右键对巨兽电伤及位移；远程回收100金币，玩家保持(153.5,155.5)，没有走近。物件／门、全部拾取类别及拒绝分支未穷举。 |
| Hydra | 三头原图、原247火弹伤害约21–24、无雷云风暴现场巨兽35→0，寿命推进后不再观察到旧海蛇攻击。上限与完整跨区生命周期未穷举。 |

日志在artifacts/sorceress-smoke-20261002.log、sorceress-smoke-final-20261002.log和sorceress-shield-smoke-20261002.log及对应error文件，最终截图在dist/current/artifacts/debug-pipe.png；均不纳入源码提交。现有hireling只读查询增加x/y、displayDamage和effects，便于精确定位及核对来源，不增加玩法旁路。范围外包括剩余三个未实现技能、用户存档往返、所有难度／装备／概率分支及全客户端像素等价。

### 九头海蛇

2026-10-02：Hydra（62）按D2MOO SkillSor::SrvDo144在目标(-1,-1)、(0,0)、(1,-1)创建三个真实身份hydra1/2/3，组件token HX/21/HZ；当前MPQ MonStats生命空、MonStats2碰撞0和不可攻击资格不转换为自造生命值。以独立限时活动状态推进真实S2出生、A1攻击和DT消失，零生命不会误用普通宠物死亡判定。原AnimData三头S2均12帧、A1均12帧且第8帧事件2、DT均10帧，必需资源和时序缺失时明确拒绝。寿命Param1=250帧、petmax=99头、delay=40；PetType.hydra的warp为空，不跟随传送，保持原施放区域。

AI依据AITHINK_Fn086_Hydra：25格内有效敌人、60%攻击概率，否则10帧空闲，寿命严格大于到期帧后进入DT。火弹从真实头部A1事件释放，Missiles.hydra=247的原Firebolt图／速度／寿命／碰撞／fireexplode命中图及声音沿公共导入。Fire Bolt／Fire Ball基础点每点3%协同、召唤时继承火焰支配，伤害快照保存到海蛇，非每次读取玩家当前属性。三头不走骷髅尸体消费、近战、跟随和传送；无原宠物头像不创造头像。

当前静态落点阻挡、近敌搜索和区域休眠采用公共适配，完整原宠物range=1跨房间释放、火焰穿透、死亡／主人生命周期及原音轨空间规则尚未认证；不可攻击海蛇的ReturnFire事件仍受公共寒冰甲目标资格限制。Windows Release最终链接通过并逐项打包；未编写或运行测试、未启动游戏，临时海蛇不入D2S v96。女巫源码27/30项，不是全技能准确完成声明。

### 闪电、连锁闪电与陨石

2026-10-02：按用户授权补齐缺完整经典客户端依据的可用显示。规则及数值读取当前MPQ，服务器算法查本地D2MOO SkillSor／Missiles；公开[DataGuide](https://locbones.github.io/D2R_DataGuide/)仅补充客户端函数与字段语义，不用D2R数值替代当前原表。

Lightning（49）使用服务器98，直线扫掠接触及LastCollide，穿透敌人但受地形阻挡；Chain Lightning（53）使用服务器93，命中后按原20整数子格范围与公共视线筛选，大于当前ID的最小ID优先，无后继回到最小ID。当前MPQ跳数为(26+有效等级-1)/5，NextDelay=4，继承来源等级并逐次解析伤害，前一目标不被子弹立即重复命中。两项共用原SQ12／SC、19个序列项及第7项出手；0FCR实例动作0.76秒。Lightning不可见服务器弹体不强求CelFile，表现沿CltDo08语义生成原99 LightningStrike三段尾迹；链主体保留原93 DCC。电弧折线及完整房间GUID／随机调度仍为适配。

Meteor（56）使用服务器中心101，60帧后一次半径6爆炸；爆炸伤害取Fire Bolt／Fire Ball基础点协同和火焰支配。原18个偏移分别放置240地火，阻挡格不生成；地火独立EMin／EMax及等级档、Inferno基础点协同、持续30+15×(等级-1)帧，命中沿公共软受击链。SC出手／法力及30帧技能延迟读取原表，落点起手与出手复验。表现读取原100落石、102尾迹、103爆炸、105／104火焰及154光源；下落高度按CltParam1=59／CltParam2=25和显示时钟适配，爆炸环按原密度字段生成。CltParam3横向滑移、ProgSound提前触发、完整爆炸随机形态与经典客户端精确混合未认证，不把适配称为逐帧复刻。

联合普通Sorceress／seed210冒烟：闪电穿透击杀两只巨兽；移除支配后连锁击杀32HP目标并使下一只27→24.10；陨石中心施法后30帧仍有1.44秒寿命，70帧时生成17处地火（一个原偏移阻挡），后续目标死亡。电弧与下落截图已查看，临时实例退出0、stderr空。未新增测试程序、未读写用户存档；精确爆炸伤害／地火持续伤害分离、全部等级／装备组合及原版逐像素对照未穷举。D2S v96不变。

### 心灵传动

2026-10-02：Telekinesis（43）使用原SC、light_cast_2和sorceress_telekinesis声音，固定7法力。依据SkillSor::SrvSt12／SrvDo21，以25子格整数平方范围验证目标，出手再验证；敌方单位只能在非城镇施放，原闪电伤害1–2、每等级两端各+1，应用闪电支配，35%分支调用既有击退路径，HitClass=109。不创建虚构伤害弹体。右键支持敌方单位、地面物品和非NPC原物件目标。

地面拾取仅允许原类型scro/gold/tpot/misl/poti/key，普通武器／护甲等不远程收集。通过已有库存collect／consume处理堆叠、容量和金币上限，不改玩家坐标；原物件调用现有completeInteraction，保留锁箱钥匙、状态和任务资格。独立回城门／凯恩门不是当前原物件目标，尚未接入；物件仍受已有各操作消费者限制，完整原拒拾声音／地面刷新、击退资格及路径调度尚未认证。Windows Release最终链接通过并逐项打包；未编写或运行测试、未启动游戏，女巫源码26/30项，D2S v96不变。

### 雷云风暴

2026-10-02：Thunder Storm（57）使用原thunderstorm状态38，持续800+200×(有效等级-1)帧。周期为(100-dm56)*Param4/100+Param3，当前Param3=25、Param4=100、Param5=0、Param6=100，下限5帧；按D2MOO SKILL_ComputePeriodicRate对齐世界帧。施法只建立状态，首周期再打击，不额外扣蓝；城镇不打击，到期／死亡停止。按17子格整数平方范围、敌对可攻击／存活和公共阻弹视线筛选，按原sub_6FD10880思路选择大于上次ID的最小ID，无后继时回到最小ID。周期读取原施放等级及当前装备／支配，掷一次定点闪电伤害，按原SrvDo29直接调用目标命中，不把落击图作为第二份服务器伤害；直接弹体接触共用寒冰甲事件。

原thunderstorm1（166）、LightningboltBig DCC及TravelSound沿MPQ加载，原Range=9作落击显示；没有原持续状态Overlay，不创造常驻雷云图。原房间链搜索／GUID与当前实体顺序有差异，完整客户端落击随机变体及音频空间表现尚未核实。Windows Release最终链接通过并逐项打包；未编写或运行测试、未启动游戏，实际战斗／画面留待人工验收，临时周期状态不入D2S v96。当前女巫源码25/30项。

### 强化

2026-10-02：Enchant（52）沿原SC施法建立enchant状态16，持续3600+600×(有效等级-1)帧，附火伤使用原enma/exma分段结果转换到整数属性，命中百分比20+9×(有效等级-1)。Warmth基础点每点9%协同，随后施法者火焰支配；玩家近战时附火伤再应用攻击者当前火焰支配，远程沿既有弹体创建快照。右键鼠标可选当前佣兵和召唤物；正式UseSkill可指定友军，无效／死亡／敌对目标回退施法者，来源仍记施法者。重施替换，不叠加；玩家／佣兵／召唤物共用状态属性合并、到期和死亡移除。

依据D2MOO SkillSor::SrvDo025_Enchant、D2Common元素伤害公式及SUnitDmg附元素伤害规则；当前原表aurastat1–3、auralencalc、声音均读取MPQ。未实现完整客户端武器换色／状态闪光，远程支配、跨房间目标和UI属性显示仍受公共实现边界限制。Windows Release最终链接通过并逐项打包；未编写或运行测试、未启动游戏，女巫源码24/30项，临时强化不入D2S v96。

### 能量护盾

2026-10-02：Energy Shield（58）按SrvDo23创建原energyshield状态30，持续3600+1500×(有效等级-1)帧，重施替换、到期／死亡清理。吸收率按原EMin=20及五段5/2/1/1/1递增，最高95%；法力倍率=max(1,Param5=32-Telekinesis基础点)/16，装备加技能不计该协同。依据SkillSor::EventFunc24、SUnitDmg::ABSORBDAMAGE事件位置，先计算阵营倍率，再按物理／火／闪电／冰／魔法顺序以1/256定点生命计算吸收，受当前法力上限约束；剩余伤害再走抗性／平减／元素吸收。毒伤、已有毒／开放伤口持续伤害不进入护盾，缺蓝时只吸收可支付部分，法力归零移除状态。原ManaShieldBall持续DCC和sorceress_energyshield启动声音接入现有表现。

原EventFunc24还处理怪物life/mana/stamina吸取字段及prgoverlay八方向命中闪光；当前公共伤害请求没有这些资源吸取通道，原PATH方向量化／命中闪光尚未接入，不伪造该行为，也不把持续叠层作为命中闪光。Windows Release最终链接通过；未编写或运行测试、未启动游戏，实际战斗／画面待人工验收，临时状态不入D2S v96。女巫源码23/30项，不是全技能准确完成声明。

### 炽烈之径

2026-10-02：Blaze（46）接入SC施法、原blaze状态13、重施替换与到期／死亡清理。持续时间按原dm12：min(500,50+(500-50)*110*等级/(等级+6)/100)帧；仅实际移动跨整数子格且不在城镇时创建blaze（67），站立或传送不制造火路。新火焰使用状态记录的施法等级、创建时装备／基础协同，Warmth每基础点4%、Fire Wall每基础点1%，随后火焰支配；原HitShift=4与五段伤害增量共用解析。火焰独立持续90帧，Size=1，SrvDmg03以19/128随机软命中，无普通硬受击；旧火焰不随状态到期删除。原groundFireBig DCC／原声音沿现有资源加载。

依据D2MOO SkillSor::SrvDo023／SKILLS_CreateBlazeMissile及D2Common::D2Common_11033，MPQ Skills／Missiles／States。现有每模拟步比较前后子格，不是原完整动态路径的逐子格事件队列；高速跨多格、区域边界和客户端火焰精确节拍仍是公共适配边界，未做实机验收。Windows Release最终链接通过并逐项打包；未编写或运行测试、未启动游戏，临时状态／火焰不入D2S v96，女巫源码22/30项。

### 玩家火墙

2026-10-02：Fire Wall（51）进入现有学习／选择／SC施法入口，当前女巫源码21/30项。当前MPQ为SrvDo24、firewallmaker（68）→firewall（69），以目标整数位置为中心，按施法者至目标的垂线发出两个maker及一份中心火焰；maker Range=7+2×有效等级，速度沿公共原换算，火焰持续90帧。伤害按Skills原五段增量和HitShift=4逐帧派生，Warmth基础点×Param8=4与Inferno基础点×Param7=1分别协同，随后应用火焰支配和装备增伤；共享技能延迟35帧，费用来自原mana/lvlmana/manashift。火焰碰撞读取原Size=1，不附加普通硬受击；SrvDmg03按dParam1=19与随机数低7位决定软命中。敌方直接弹体接触共用寒冰甲事件，伤害／收益归属玩家。

依据本地D2MOO SkillSor::SKILLS_SrvDo024_FireWall、MissMode::SrvDo05／06／SrvDmg03；原groundFireBig DCC、叠层和声音复用MPQ加载。现有连续坐标maker、格变更创建火焰、创建时伤害快照及客户端火焰节拍仍沿既有执行器适配，不宣称完整原定点路径和逐帧客户端等价。Windows Release最终链接通过；未编写或运行测试、未启动游戏，行为／画面待人工验收，临时火焰不写D2S v96。Lightning（49）因本地reference无完整CltDo08电弧生成实现暂缓；原图和参数存在不等于客户端算法已经核实，未保留该技能试改。

### 逐项补全

以下保留此前独立技能包的验收证据；这些记录不覆盖后续源码修订。2026-09-27 的 Windows Release 曾编译并更新固定 `dist/current`；2026-09-30 冰封球与全局照明批次随后完成 Windows Release 编译并更新 `dist/current`，未运行验收；2026-10-01 暴风雪、冰尖柱、碎冰甲与寒冰甲仅修改源码和文档，按用户要求不编写测试，不构建、运行检查、启动游戏或打包。

地狱之火（41）已按当前 MPQ 与本地参考完成可用适配，不再等待原版截图。冰封球（64）、暴风雪（59）与冰尖柱（55）已接入下述弹体链，冰系十个节点均有玩法定义。2026-10-02补齐后，当前源码30/30项有玩法入口，依据与限制见上文。原版客户端精确重入、图层组合与声音循环仍列为待对照项，不将当前适配声称为逐帧复刻。`artifacts/sorceress-inferno-20260926/` 保留为旧原型，当时验收包为 `artifacts/sorceress-inferno-release-20260926/`；当前分发入口为 `dist/current`，历史包不作为本次源码验收证据。

| 技能 | 实现与包内验证 |
| --- | --- |
| 寒冰甲（60） | 原 SC、三冰甲互斥、防御／持续时间、ReturnFire 弹体接触反击箭、冰伤／减速、原 DCC／声音已接源码；未构建、运行或打包；客户端状态闪光为显示适配，见下文。 |
| 碎冰甲（50） | 原 SC、护甲互斥、防御／持续时间、近战攻击尝试反击冰伤／减速与原叠层声音已接源码；未构建、运行或打包；附加闪点客户端规则暂缓，见下文。 |
| 冰尖柱（55） | 原飞行弹体、碰撞／到期范围冰伤与冻结、协同、原 DCC 三段显示、光源和三种循环／撞击声音已接源码；未构建、运行或打包，经典版碎冰数量／偏移及公共状态规则缺口见下文。 |
| 地狱之火（41） | 起手扣一次费用，SQ 准备后持续喷焰；暖气基础等级协同、火焰支配、原火伤、速度与等级寿命。支持右键持续瞄准，松键、移动、受击、死亡、换技能、换区域或法力不足停止。独立包已验证起手 47→46.4375 法力、前 10 帧无火焰、第 11 帧出现首焰、两帧一次扣费、目标生命 11→0.82421875、转向不重扣起手费用、停止后的余焰到期、移动中断、法力耗尽停止与低于 6 点门槛拒绝。未做 D2S 战斗状态检查；真实鼠标持续输入及原版逐像素对照尚未人工验收。 |
| 冰封装甲（40） | 读取原防御百分比、持续时间、冻结时间、两种高级冰甲基础等级协同、互斥状态组与原叠层／声音。近战实际物理伤害后冻结攻击者，不造成反击伤害；首领仅减速。包 `artifacts/sorceress-frozen-armor-20260926/` 已验证 1 级防御 6→7、消耗 7 法力、120 秒持续、重施不叠加、到期恢复、近战冻结且怪物生命不变，以及 D2S 等级重载。冰甲临时效果从未写入 D2S，载入使用新角色状态，不存在从存档清除冰甲的步骤。当前三冰甲已按原 group=1 接入互斥；本批未运行交叉验收。 |
| 充能弹（38） | 原 `min(24,ln12)` 弹数、闪电协同和支配、D2MOO `PATH_ComputePathChargedBolt` 的方向选择与随机偏转序列、两格路径点及 77 帧寿命上限；逐段碰撞，命中消失并播放原 lightning 叠层。使用原 DCC、Units 调色板、中段动画循环和声音。包 `artifacts/sorceress-charged-bolt-20260926/` 已验证第 7 帧释放 3 颗、耗蓝、分叉轨迹、到期清除、真实怪物生命 12→8.2421875，以及 D2S 等级重载。当前源码另按原表核实的模式 3／单子格尺寸分离地形阻弹，详见下文；此修复已随本次构建进入 `dist/current`，尚未运行，不包含在旧包验收中。动态单位碰撞、动态光照和逐像素对照仍未完整覆盖。 |

`content/skills/sorceress_data.cpp` 从当前 MPQ 的 `Skills.txt`、`Missiles.txt`、`Overlay.txt`、`Sounds.txt` 和 `AnimData.d2` 生成类型化定义。玩法不读取 MPQ 或 GPU 资源。等级伤害、法力定点数、基础等级协同依据 D2MOO `D2Common/src/D2Skills.cpp`；武器授予等级与已分配等级叠加，装备加技能不计入基础等级协同。

### 通用弹体与地形碰撞

`ClassicData::missileCollisions` 从当前 MPQ 的全部 `Missiles.txt` 记录读取 `CollideType` 和 `Size`，按固定本地 D2MOO `MissMode.cpp::stru_6FD2E5F8` 映射模式 0–8。`Missiles.cpp::MISSILES_CreateMissileFromParams` 将此掩码交给路径；`Step.cpp` 只以命中的 `WALL | MISSILE_BARRIER` 停止地形运动，单位标志另用于寻找目标。因此模式 1/2/3/5/6 检查阻弹位，模式 8 还检查不可行走墙位；模式 0/4/7 没有这两种地形阻挡。`D2Collision.cpp::COLLISION_CheckMaskWithSize` 的尺寸 0/1 为单格、2 为中心与上下左右五格、3 为 3×3；不再把所有弹体当作行走点，也不把行走的对角侧格约束附加到单格弹体。

充能弹原表为 `Id=56、CollideType=3、Size=1、CollideKill=1`，现在与女巫其他法术、弓弩／投掷物、佣兵及怪物弹体共用 `Simulation::missilePathClear`／`Grid::missileSegment`，不再保留充能弹专用网格或固定模式校验。玩家和佣兵远程选目标同步使用各自弹体规则；怪物 AI 的普通视线使用原阻弹位，实际移动仍走寻路。

`Grid::terrainCollision` 合并当前 DT1 原始位及 DS1 `bFillLOS`（第 16 位→阻弹 `0x04`）、`bUnwalkable`（第 17 位→墙 `0x01`）。地板、墙、屋顶及方向 3/4 墙角配片按位 OR，避免后层地板清掉前层阻挡；角色还检查 DT1 `NOPLAYER=0x08`。原对象尺寸、各动画状态碰撞和 `BlockMissile` 进入独立对象层，见 [物体](../world/OBJECTS.md)。当前 MPQ 的 loose rock/boulder（174/175）有行走占位、`BlockMissile=0`，不能按外观把它们统一设成阻弹墙。

本次核实并接入通用地形／对象阻挡与原弹体尺寸；物理弹体、移动毒云及直线单体法术已按原单位占位扫掠求交；新星、充能弹、地狱之火等仍有旧距离判断，原引擎定点逐帧路径未完整复刻。区域外／拼接房间空隙和现有区域封边保留，不表示实现了跨区弹体。2026-09-27 本次技能重构的 Windows Release 已包含该碰撞源码，但未测试或运行游戏；用户所见具体石头尚无坐标／瓦片定位，实机效果待查看。

### 地狱之火依据与待对照项

- 原 MPQ：`srvstfunc=11`、`srvdofunc=19`、`seqnum=6`、`seqinput=10`、`startmana=6`，`usemanaondo` 为空。`startmana` 是启动门槛，不额外扣 6 点；当前起手费用使用原定点公式，1 级 `(36 << 2)/256=0.5625`。D2MOO `Skills.cpp::sub_6FD12950` 在启动成功且不使用 `usemanaondo` 时调用通用扣费；`SkillSor.cpp::SKILLS_SrvSt11_Inferno_ArcticBlast` 只以 `startmana` 判资格。
- `SequenceTbls.cpp::gPlayerSequenceInferno` 有 15 项，索引 10 为动作事件，使用 SC 图帧 9。当前按 25 Hz 播放准备动作并停留在喷火姿势，前 10 个模拟步不生成火焰，第 11 步起每步发射一次。弹体寿命使用 `calc1=ln12/2`，1 级 10 帧、每级 `Param2=3` 进入整数公式；伤害用原每帧数值，面板显示每秒值。
- 持续扣费目前每两帧一次，与 D2Common `D2Skills.cpp` 公式参数 22 的 `/2` 换算及 1 级约 7 法力/秒相符。该显示公式对等级增量的括号与通用单次费用不同，不能单凭它证明所有等级的客户端重入频率；两帧节拍是本次采用的适配，后续有录屏或更完整证据再修正。起手只扣一次，更新瞄准不重启准备或重复扣起手费用。
- 当前只渲染一条连续火焰流，使用 `srvmissilea=infernoflame1` 的原 `Flamethrower.dcc`，不凭资源槽数量创造第二种火。原 `cltmissileb=infernoflame2` 引用 `Flamethrower2.dcc`，具体叠加／交替规则未核实，暂未加入。原 `sorceress_inferno` 声音在首次喷焰时播放，持续循环与停火尾音尚未对齐客户端。用户后续截图用于核对火焰外形、数量和动作；扣费节拍需要连续画面或运行证据。
- 包内短程检查使用现有程序和命名管道，没有新增测试脚本、用例或专用程序；精确阻弹掩码、碰撞尺寸及多目标密集重叠沿用共享实现，未声称原引擎逐像素等价。

### 寒冰甲依据与当前实现

Chilling Armor（60）在碎冰甲完成并提交后实现。当前 MPQ 规定 24 级学习、Shiver Armor 前置、城镇可用、固定 17 法力、无技能延迟；SC 出手应用 States.chillingarmor=20。与 Frozen Armor（10）、Shiver Armor（88）同属 group=1，三种冰甲互斥、重施刷新，替换／到期／死亡后不再反击；均为临时状态，不写入 D2S。

防御为 `45+5*(等级-1)`%，持续时间为 `3600+150*(等级-1)+250*(Frozen Armor.blvl+Shiver Armor.blvl)` 帧，即无协同 1 级 144 秒，每级 +6 秒，每点基础协同 +10 秒。冰弹伤害 `HitShift=7`、EMin/EMax=8/12，1 级 4–6 冰伤；五段最小增量 2/4/6/8/10、最大 3/5/7/9/11，Frozen Armor／Shiver Armor 每点基础等级 +7% 伤害，随后加装备冰技能伤害。ELen=100，三个等级段增量为 0/25/25 帧；1–8 级基础冰冷持续 4 秒，之后每级 +1 秒，只减速，不冻结。

原 auraevent 为 hitbymissile／EventFunc01。D2MOO `MISSMODE_SrvDmgHitHandler` 在弹体接触有效目标时触发，ToHit 未命中也触发，发生在服务器命中特殊函数和伤害结算之前。因此不要求正生命损失，也不因为格挡、抗性或吸收而取消；它本身不提供抵消原弹体的概率。仅对当前仍存在的敌对弹体所有者，且来袭 Missiles.ReturnFire=1 时生成反击。近战、环境伤害、范围内被波及但没有直接弹体接触的目标、墙体碰撞和到期不触发，ReturnFire=0 的弹体不触发。公共内容层导入所有原 ReturnFire 标记，玩法不读取 MPQ。

EventFunc01 按护甲的施放等级及所有者当时的装备／基础协同创建 `chillingarmorbolt=264`，从护甲主人的整数子格射向来袭弹体所有者当时的整数位置，不瞄准来袭弹体，也不跟踪目标后续移动。每次有效接触生成一次，无额外耗蓝、冷却或 SC 重播；无固定周期自动发射。新箭在本轮弹体更新结束后加入，避免反击生成使父弹体引用失效。原 Vel=MaxVel=18、Range=25，公共原换算为每帧 0.84375 子格、1 秒寿命；CollideType=3／Size=1，首次敌对单位碰撞即消失，可被途中其他敌人截住。没有服务器命中特殊函数、爆炸或 AlwaysExplode，到期／撞墙没有范围伤害；减寿命后到期步不再查询单位。伤害范围在生成时确定，命中时按该箭随机流掷 1/256 生命半开区间冰伤，再走冷抗／冰冷穿透、减时和公共受击、死亡与收益。没有物理伤害，原 `MISSMODE_SetDamageFlags` 不启用普通盾牌格挡；公共被动闪避／双爪格挡仍未实现。

反击箭原 ReturnFire=0，防止冰甲互相无限反击。普通／怪物弹体的直接接触、冰封球子弹、暴风雪服务器冰块、冰尖柱的直接碰撞以及现有移动毒云的单位接触均使用同一入口并受各自原 ReturnFire 标记控制；弹体原 NextHit／LastCollide 控制重复接触，不给寒冰甲额外发明一套命中间隔。

展示使用原 `IceBolt.dcc` 的 16 方向图及 Missiles 的 AnimLen=6／LoopAnim／animrate=1024 元数据，实际 DCC 帧数由原资源解码，不补造图帧；Light=4、RGB=81/81/255、Trans=1 进入公共照明和 PL2 pScreen 混色。护甲为原 `FrozenArmor.dcc`，chillarmor Overlay 指定 24 帧、AnimRate=16、Trans=3；施法用原 ice_cast_3。原 `chillarmor_hit/AuraResistColdCast.dcc` 的 11 帧、AnimRate=24、PreDraw=1 与高度随附着单位读取。States.clteventfunc=1 完整经典客户端实现仍缺证据，当前把关联闪光附在反击的护甲主人上作显示适配；精确客户端触发帧、额外图层组合明确暂缓，不用自造粒子替代。

启动声音为原 shivers.wav，施法声 Volume=210。冰箭使用原 icebolt1/2/3.wav（单声道 22050 Hz／16 bit），smpl 循环区间分别为 31512–54456、28611–51139、31347–53875（含末采样），与原 Sounds.Block 1 一致；Loop=1、Compound=4、Fade Out=6 跟随每枚反击箭。撞击使用原 coldimpact1/2/3.wav，Group Size=3、Volume=255、非循环、Compound=4、三行 Stop Inst=1；声音分组选图与四帧入声间隔共用独立音频随机流，换区清理间隔记录。SoundBank 按各变体读取 Stop Inst 布尔值，通过 Compound 后停止所选 WAV 的旧实例再播放；被间隔拒绝的请求不会截断正在播放的声音。此前把 Stop Inst=1 当作不支持的字段，导致用户新编译 EXE 在声音初始化时报错、命名管道尚未创建；现已修正源码，未重新构建或运行验证。字段为布尔值的本地证据见 OpenDiablo2 SoundDetailRecord／Diablerie SoundInfo；替换实例的含义另参照 [D2R Data Guide 的 Sounds.txt 说明](https://locbones.github.io/D2R_DataGuide/#soundstxt)。该说明不证明经典版跨变体／单位的实例调度；精确原声音实例调度、空间衰减／定位与共享音轨关系仍暂缓。

当前冰系 10 个原节点（39、40、44、45、50、55、59、60、64、65）均已有玩法定义：九项主动及冰冷支配；这不是全客户端逐帧复刻或实机验收声明。两种新增冰甲本批仅源码和文档；未编写测试、未构建、运行检查、启动游戏或打包，现有 `dist/current` 不含它们。D2S v96、技能等级编码与规则指纹机制不变。原表／DCC／WAV 只用已有资源工具读取当前 `assets/mpq2`，导出证据保留在忽略的 `artifacts/ice-armors-20261001/`；全局照明、原定点路径、冷状态调色与跨区弹体缺口仍按公共文档保留。

### 碎冰甲依据与当前实现

Shiver Armor（50）是本批先完成的技能。当前 MPQ 规定 12 级学习，前置 Ice Blast／Frozen Armor，城镇可用，11 法力固定费用，无技能延迟；SC 动作和 `ice_cast_2` 随公共出手帧播放。SrvDo18 应用 `States.shiverarmor=88`、group=1，与冰封装甲互斥，重施刷新而不叠加；状态、反击注册及显示随替换、到期和死亡统一移除，不写入 D2S。

防御为 `45+6*(等级-1)`%，持续时间为 `3000+300*(等级-1)+250*(Frozen Armor.blvl+Chilling Armor.blvl)` 帧，即无协同 1 级 120 秒、每级 +12 秒、每点基础协同 +10 秒。防御参与公共属性和命中判定，装备加技能只增加本技能等级，不计基础协同。

原 `auraevent1=attackedinmelee`／EventFunc03 由近战攻击尝试触发，包括未命中和盾牌格挡；远程弹体不触发，不能复用 Frozen Armor 的实际物理受伤事件。现有怪物／召唤物和玩家近战入口在准确率／格挡结果之后、承伤之前触发一次。反击不消耗额外法力，没有范围攻击或反射原物理伤害；按护甲施放等级和所有者当前协同／装备属性解析，使用所有者随机流在 1/256 生命半开区间掷一次冰伤。原反击 ResultFlags=0x4021 不含 GetHit，不额外触发攻击者的受击硬直，避免把每次挥击取消成反击击晕。

`HitShift=7`、EMin/EMax=12/16，1 级 6–8 冰伤；五段最小增量 4/6/8/10/12、最大 5/7/9/11/13，原分段沿用公共计算。Frozen Armor／Chilling Armor 的基础等级每点 +9% 反击伤害，随后计算装备冰伤。ELen=100，ELevLen1 空为 0、第二段 25、第三段 50；1–8 级基础冰冷持续 4 秒，9–16 级每级 +1 秒，17 级起每级 +2 秒。反击走公共冷抗、非免疫怪物的冰冷穿透、无法冻结、半冻结时长、ColdEffect、MonsterColdDivisor 与受击／死亡／收益；只减速，不冻结。原命中 Overlay 也会在免疫目标上显示。

原护甲叠层 `FrozenArmor.dcc` 为 24 帧，shiverarmor Overlay 的 AnimRate=16／Trans=3；反击 `AuraResistColdCast.dcc` 为 11 帧、AnimRate=24、PreDraw=1、Trans=3，附着于攻击者，按其 MonStats2.OverlayHeight 选择原 Height1–4，玩家固定 Height2。播放原 DCC 及 Act1 PL2 混色，不绘制自造护甲圈；施法声原 Volume=210，启动声为原 `shivers.wav`，不是每次反击重复启动音。原 Overlay 光源仍走公共照明，其动态半径／衰减限制见 [照明](../world/LIGHTING.md)。原表帧率与高度字段另核对 [D2R Data Guide 的 Overlay 说明](https://locbones.github.io/D2R_DataGuide/#overlaytxt)，不把 Diablerie 的 1.5 倍经验速度视为经典版精确时序。

显示限制：States.cltactivefunc=87 的 sparkle 附加闪点没有在本地 reference 找到完整经典客户端实现；当前原 `sparkle/Gleam.dcc` 与 cltcalc=10/5/3 已核对并加载，但精确生成相位、方向、偏移和高度明确暂缓，不把该客户端模板的毒伤字段接入玩法。原定点动作与事件分配时序、冰冷状态调色、跨区弹体和空间音频仍受公共系统既有边界约束。已用现有资源工具读取当前 `assets/mpq2` 原表与原图，证据在忽略的 `artifacts/ice-armors-20261001/`；未编写测试、未构建、运行检查、启动游戏或打包。

### 冰尖柱依据与当前实现

原技能为 Glacial Spike（55），18 级学习、Ice Blast 前置、左右键选择、城镇禁用及 SC 出手沿用当前 MPQ 的技能树和公共施法入口。`content/skills/glacial_spike_data.*` 导入并校验 Skills／Missiles 原关联与公式；`gameplay/skills/glacial_spike.cpp` 执行独立的 25 Hz 飞行及范围冻结，不借用冰风暴的单体命中算法。

| 原表项 | 当前 MPQ 与执行规则 |
| --- | --- |
| 费用与伤害 | `manashift=7`，1 级 10 法力，每级 +0.5；没有技能延迟。`HitShift=7`，EMin/EMax=32/48，1 级 16–24 冰伤；最小五段增量 14/26/28/30/32，最大 15/27/29/31/33，使用公共原等级分段。Ice Bolt、Ice Blast、Frozen Orb 的基础等级每级 +5% 伤害，装备加技能不计协同，装备冰技能伤害随后计算。 |
| 飞行 | `glacialspike=96`，SrvDo1／SrvHit13，Vel=MaxVel=16，无加速／等级速度或寿命增量；按公共原速度换算为每帧 0.75 子格，Range=40（1.6 秒）。从施法者整数子格发射，CollideType=3／Size=1、CollideKill=1；动态单位命中共用尺寸扫掠，墙体共用原阻弹掩码。 |
| 爆裂与攻击判定 | D2MOO `HandleMissileCollision` 先移动再扣剩余帧；寿命归零也调用 SrvHit13，不能直接删除。首次碰撞／到期只爆裂一次，中心是弹体当前整数坐标，不吸附目标中心、不再附加单体伤害。原 `sHitPar1=0` 回退到 `aurarangecalc=ln12`，本表半径固定 4 子格；整数中心距离平方不超过 16 的存活敌对可攻击目标参与结算。原 aura filter=0x8583 没有额外障碍视线位，范围效果不补墙体射线。 |
| 掷伤害与冻结公式 | 飞行弹体保存创建时的 1/256 生命伤害范围。爆裂仅掷一次半开随机伤害区间，再对每个目标分别抗性结算。`sHitPar2=0` 使用当前所有者属性及发射时等级计算 `auralencalc=ln34*(100+Blizzard.blvl*par7)/100`，即 `(50+3*(等级-1))*(100+3*暴风雪基础等级)/100`，整数帧截断；1 级无协同为 50 帧／2 秒。不能套用缺失的 ELen，不能重复掷每个目标的范围伤害。 |
| 冻结与困难度 | 冻结时长先处理无法冻结／半冻结时长，再按冷抗与非免疫怪物的冰冷穿透取整。普通可冻结怪物仅除以 MonsterFreezeDivisor，整数截断，不叠加 MonsterColdDivisor；重施取较晚到期、停止行动和清空路线。普通怪物原 ColdEffect>=0 不冻结，也不退回冰冷。首领／冠军／独特怪物、玩家及佣兵退回公共冰冷减速，沿用 ColdEffect／MonsterColdDivisor。 |

展示加载原 `GlacialSpike.dcc`（16 方向、6 帧循环）、`FreezeExplodeCenter.dcc`（单方向、15 帧）及 `FreezeExplodeEjecta.dcc`（8 方向、15 帧）；原 animrate=1024 按 25 Hz，中心／碎冰 Range=16 各自计时。冻结沿用现有怪物停止动画及冰色状态显示。飞行 InitSteps=1、原方向量化、帧偏移、Trans=1 进入公共 Act1 PL2 pScreen 混色；弹体 Light=5、中心 Light=11，原 RGB=81/81/255 共用全局照明，碎冰没有新造光源。原图已有碎冰的像素位移，子弹体 Vel 为空，不再人为叠加飞行速度。

CltHit14 的关联与随机方向核对 [D2R Data Guide](https://locbones.github.io/D2R_DataGuide/#missilestxt)。经典版完整函数未在 D2MOO 找到；Diablerie 显示 3–4 份并随机偏移，但不能证明其数量／偏移等同原版。因此当前使用一份原中心和一份随机 8 方向原碎冰显示适配；经典版碎冰密度、随机序列与精确创建帧明确暂缓，不宣称逐帧全面复刻。全局光照的原衰减／遮挡缺口仍见 [照明](../world/LIGHTING.md)。冻结入口已检查原 `STATE_UNINTERRUPTABLE` 活跃状态；该状态的其他技能消费者、亚马逊闪避／刺客双爪格挡及跨房间搜索顺序尚未接入公共模型；原范围入口 `ApplyBlockOrDodge(1,0)` 不做普通盾牌格挡，本技能也不附加。怪物冰色仍沿用已有显示，原状态调色映射未完整复刻。

声音使用原 `sorceress_cast_cold`（Volume=210）、`icespike1/2/3.wav` 飞行组和 `blastimpact1/2/3.wav` 撞击组（Volume=255）。飞行三种 WAV 均为单声道 22050 Hz／16 bit，smpl 循环区间分别为 24220–51867、30578–59890、28407–55903（包含末采样），与原 Sounds.Block 1 对齐；Loop=1、Compound=4、Fade Out=6，跟随弹体生命周期，暂停／换区域清理共用音轨。撞击 Group Size=3，非循环随机选择一次；展示／声音随机流不改玩法掷伤害。精确空间衰减、立体声定位与原声音实例调度仍沿用公共音频的既有边界。

本批只用已有资源工具读取当前 `assets/mpq2` 原表、DCC 原图和 WAV 元数据；摘录位于忽略的 `artifacts/glacial-spike-20261001/`，不纳入源码提交。按用户要求未编写测试、未构建、运行检查、启动游戏或打包，后续源码与包状态统一见 [项目基线](../../../BASELINE.md)。D2S v96、技能等级编码和规则指纹机制不变；飞行、冻结、显示与声音均为临时战斗状态。

### 暴风雪依据与当前实现

原技能为 Blizzard（59）。`content/skills/blizzard_data.*` 导入当前 MPQ 参数并核对函数／伤害来源；`gameplay/skills/blizzard.cpp` 在公共 25 Hz 模拟中执行，不在玩法读取 MPQ。等级、24 级学习门槛、Frost Nova／Glacial Spike 前置、城镇限制、右键资格、SC 动作及 `ice_cast_3` 叠层沿用原技能树和施法入口。

| 原表项 | 当前 MPQ 与执行规则 |
| --- | --- |
| 技能函数 | `srvdofunc=28`，在出手帧按目标单位当时的整数位置或地面目标创建 `blizzardcenter`；目标以尺寸 1、掩码 5 检查墙／阻弹位。原整数距离 `max(dx,dy)+min(dx,dy)/2` 不超过 100。扣蓝前复核目标。 |
| 法力／技能延迟 | 1 级 23 点，随后每级 +1；`manashift=8`、`minmana=1`；`delay=45`，共享延迟 1.8 秒，从释放成功开始。快速施法只影响角色 SC 动作，不缩短风暴或落冰节拍。 |
| 中心弹体 | `blizzardcenter=158`，`SrvDo10/CltDo13`，Range=100（4 秒），零速度／碰撞模式 0，`CelFile=null`，没有中心伤害或可见中心球；原 Light=9、RGB=81/81/255 进入公共照明。 |
| 生成节拍与范围 | `calc1=par1=7`、`calc2=par3=4`。D2MOO `MISSMODE_CreateMissileWithCollisionCheck` 在扣减前的剩余帧模 4 为零时尝试一次；当前表共 25 次。每次以原 DRLG 世界整数 X + 剩余帧重新初始化原随机流，X／Y 分别取 `random(12)-6`，落点是两个整数轴上的半开范围 [-6,5]。落点检查单格掩码 5，阻挡时放弃该次，不补生成、不向敌人吸附。 |
| 伤害源 | 固定服务器子弹 `blizzard1=159`，SrvDo3，零速度，Range=9，CollideType=3、Size=2；地面五格十字碰撞与原单位尺寸走公共判定。减剩余帧后查询单位，到期步不再伤害；无 CollideKill／NextHit／NextDelay，命中不销毁，LastCollide 只记录上一个命中的单位，不能改成统一命中冷却或全目标集合。 |
| 冰伤与协同 | `HitShift=8`、EMin/EMax=45/75；五段最小增量 15/30/45/55/65，最大增量 16/31/46/56/66。1 级无协同／装备时每次 45–75 冰伤；Ice Bolt、Ice Blast、Glacial Spike 的基础等级每级 +5%，装备加技能不计协同；装备冰技能伤害在协同之后计算。子弹创建时按原施法等级读取当前所有者的伤害属性，每次命中用该弹体的随机流在 1/256 生命单位半开区间掷伤害。 |
| 冰冷 | ELen=100，等级增量空字段按原表为 0；不冻结。与冰封球共用冷抗、冰冷穿透、无法冻结、半冻结时长、ColdEffect 和困难度 MonsterColdDivisor；伤害走公共受击、死亡、经验、掉落与归属。 |

显示加载原 `icestormfallvar01/02.dcc`、`blizzard.dcc` 及 `icestormimpactvar01/02.dcc`；按原 AnimLen=6/8、LoopAnim、animrate、方向、帧偏移和 Trans=1 使用 Act1 PL2 pScreen 混色。当前展示采用服务器子弹对应的 blizzard1 与首个关联碎裂图 blizzardexplode1，不增加自绘圆圈或自造粒子。下落按原子弹 CltParam1=120、CltParam2=5 作显示适配：每 25 Hz 步降低 5 像素，24 步落地后播放 Range=6 的原碎裂图；它与 9 帧服务器伤害源独立，伤害不会等待动画落地。展示阶段不分配玩法实体、不调用玩法随机流，暂停／换区域沿用已有清理路径。

客户端复刻边界：本地 D2MOO 没有 CltDo13／CltHit19 完整实现，Diablerie 也未实现暴风雪专用函数。[D2R Data Guide 的函数说明](https://locbones.github.io/D2R_DataGuide/#missilestxt) 只佐证关联、范围／频率与下落参数含义，不能证明经典版客户端的初始化、变体选择、落点随机同步或碎裂触发帧。四种原落冰记录、三种碎裂记录均校验并备妥资源，但随机选图、额外客户端落冰、精确下落／碎裂时序明确暂缓；当前显示适配不声明逐帧全面复刻。密集单位的搜索顺序、跨区域边缘足迹仍受公共模拟／碰撞的现有边界限制。

声音按中心 TravelSound 使用原 `blizzloop.wav`；当前 WAV 无 smpl 段且 Sounds.Block 1–3 全为 -1，按原 Loop=1 循环整段 PCM，Volume=180、Fade In/Out=18，由中心实体的生命周期控制，暂停和换区域清理沿用公共音轨。原 `sorceress_blizzard_impact_1/2/3` 的 Volume 均为 0，当前不额外生成非零撞击声。子冰块音轨的共享方式、Compound=-1 的经典版含义及精确空间音频仍暂缓。

本次只使用已有资源工具读取当前 MPQ 原表、DCC 原图和 WAV 元数据；摘录位于忽略的 `artifacts/blizzard-20261001/`。2026-10-01 仅修改源码和文档，未编写测试，未构建、运行检查、启动游戏或打包；后续源码与包状态统一见 [项目基线](../../../BASELINE.md)。D2S v96、技能等级编码和规则指纹机制不变；中心、落冰、延迟及展示音轨均为临时战斗状态，不写入存档。

### 冰封球依据与当前实现

入口为原技能 64，学习等级 30、前置暴风雪、左右键可选与城镇禁用均取原 `Skills/SkillDesc`。`content/skills/frozen_orb_data.*` 核对父／子弹体函数和关联并导入类型化参数，`gameplay/skills/frozen_orb.cpp` 在 25 Hz 模拟中执行；缺原资源、伤害来源或函数不支持时明确拒绝，不退回单颗普通冰弹。

| 当前 MPQ 弹体 | 原函数与运动 | 原图与序列 |
| --- | --- | --- |
| `frozenorb`（260） | SrvDo15／SrvHit29、CltDo19／CltHit30；Vel=10、Range=30；每帧发射一枚沿途冰弹，方向索引从 0 起每次加 19，模 64 | `IceOrb.dcc` 单方向 16 帧循环；InitSteps=1 |
| `frozenorbbolt`（261） | SrvDo1／CltDo1；Vel=18、Range=25；命中或撞墙消失 | `IceBolt.dcc` 16 方向、每方向 6 帧循环 |
| `frozenorbnova`（262） | SrvDo16／CltDo20；Vel=24、Range=25；以间隔 4 的原 64 方向表产生 16 枚，到期前按相同单位碰撞结算 | 同一 `IceBolt.dcc`；经过帧 0、2、4 将偏移转为 `((x-y)/2,(x+y)/2)`，保留原整数截断，并以当前整数子格中心重算航向 |

- 运动及命中依据本地 D2MOO `MissMode.cpp::MISSMODE_SrvDo15_FrozenOrb`、`MISSMODE_SrvDo16_FrozenOrbNova`、`MISSMODE_SrvHit29_FrozenOrb` 和 `MISSMODE_HandleMissileCollision`。沿途冰弹在球体移动前生成，球体移动后递减寿命，自然到期在终点环射。球体自身未绑定 Skill／MissileSkill、没有原表伤害，穿过单位不结算球体伤害；提前撞墙结束，不触发到期环射。冰弹按原 CollideType=3／Size=1、目标 MonStats2 占位及阵营查询，逐枚命中消失；没有准确率掷骰、NextHit 延迟或额外范围伤害。最后一个寿命帧先到期，不再查单位。速度采用原 8.8 数值与 75% 系数换算。
- 每枚冰弹的冰伤来自原 Frozen Orb 技能，HitShift=7；基础 EMin/EMax=80/90，五段增量为 20/24/28/29/30 与 21/25/29/30/31。1 级无协同／装备时每枚 40–45；冰弹基础等级每级提供 2% 伤害协同，装备加技能不计入协同。先按 1/256 生命单位算协同，再应用装备 `passive_cold_mastery`；命中时用该枚弹体的原随机流在最小值至最大值的半开区间掷伤害，随后走公共冷抗、受击、死亡、经验和掉落。
- 原法力定点公式为 `max(1,(50+等级-1)/2)`，1 级 25 点、每级加 0.5；到 SC 出手帧扣除。原 delay=25 从成功出手帧起计时，与其他有延迟的技能共用门槛，FCR 不缩短这 1 秒；没有延迟的技能仍按各自动作许可施放。HUD 显示每枚冰伤、基础减速时长与延迟，冷却时图标不可施放。
- 基础减速为 `200+25*(等级-1)` 帧，1 级 8 秒。目标免冰冻、半冻结时间、有效冷抗及冰冷支配／装备 `passive_cold_pierce` 参与时长；先半时长，再按抗性截断。冰免不被支配击破，最低有效抗性为 -100%。MonStats 的 ColdEffect=0 拒绝减速，负值怪物再除 DifficultyLevels 的 MonsterColdDivisor（普通／噩梦／地狱为 1/2/4），仍有效时最低保留一帧。此路径减速而不冻结。
- 施法使用原 SC 动作／出手帧、`IceCastNew03` 15 帧叠层及冰系施法音。弹体按自身时间、原帧偏移和世界方向绘制；当前原 animrate=1024 与 AnimSpeed=16 的两种定点速率均对应每模拟帧一图帧。原弹体及施法叠层使用 Units 调色板；图形／方向交叉核对 Diablerie 的 `Engine/Entities/Missile.cs` 与 OpenDiablo2 `d2mapentity/factory.go`，散射节拍与数值以 D2MOO 和当前 MPQ 为准。
- `palette_blend_view.*` 读取当前 MPQ `Act1/pal.pl2` 的 `pScreen` 表（偏移 `0x33500`，256×256 索引），接 `Missiles.Trans=1` 与 `Overlay.Trans=3`。格式交叉核对本地 OpenD2 `Engine/Palette.hpp` 与 OpenDiablo2 `d2pl2`，模式核对 OpenD2 `Renderer_GL.cpp` 和 Diablerie 材质。保留原 DCC 索引，按单位／弹体绘制顺序在 GPU 内复制对应背景矩形并查原混色表，不再用 RGB 公式代替它。已有世界缓冲仍为 RGBA：原调色板颜色恢复索引后查表，已着色／半透明混合的背景按最近颜色量化，相同 RGB 的重复索引无法完全恢复；这仍不等于全场景原索引合成。
- `LightingView` 接球体 Light=6、冰弹 Light=4、RGB=81/81/255，光源跟随实际弹体、InitSteps 与生命周期，邻接区域使用共享偏移；当前全部已接入弹体及命中特效共用原表光源，已移除冰封球限定。全局标量／灰光接当前 Act1 PL2 的 Shadows 32 行，按强度右移 3 位查原色，彩光暂留既有 RGB 乘色；未染色且 RGB 唯一的像素可恢复原索引。距离曲线仍在 `unverifiedFalloff` 明确暂留，彩光合并／遮挡、RGBA 量化及世界后处理顺序仍为适配，不代表冰封球或全项目照明已与原版等价，见 [场景照明](../world/LIGHTING.md)。
- `audio_emitters.cpp` 将 `frozenorbbolt.TravelSound` 的 `sorceress_glacialspike_1` 作为三种原 WAV 的分组，按原 Compound=4 的帧门槛选取创建音轨；各弹体拥有独立播放游标，不再重启同一个 Sound。原 22050 Hz 单声道 PCM 的 `smpl` 循环起点分别为 24220／30578／28407，均与 Sounds.Block 1 一致；含尾点的循环终点分别为 51867／59890／55903。先播放开头，随后无缝循环原采样区间。原 Volume 参与音轨音量；弹体消失后淡出，暂停暂停音轨，静音继续推进游标，换区／读档清理展示实例；声音随机流不进入玩法。分组和 Compound 单位参照 Diablerie `SoundInfo.cs/AudioManager.cs`，Fade Out=6 暂按该经典版参考的 6/25 秒适配，未把 D2R 文档的音频 tick 单位直接视作经典客户端证据。原距离衰减、声像、混响和声道优先级仍未复刻，声音准入沿用现有画面距离范围。
- 原 `frozenorbexplode`（263）的 `IceOrbExplode.dcc` 已查阅，但当前 MPQ 的技能／父子链没有引用它。Diablerie `Game/MissileFunctions.cs::OnMissileLifetimeEnd` 仅创建 16 枚关联冰弹；[D2R Data Guide 的 Missiles.txt](https://locbones.github.io/D2R_DataGuide/#missilestxt) 的 CltHit30 说明也只有关联弹体圆盘，不能证明另有硬编码的 263 播放分支，因此仍暂缓该触发。`InitSteps` 隐藏期已接通用绘制，完整客户端叠层时序仍需原版对照。当前路径仍为本项目连续坐标与扫掠适配，未移植原定点路径／单位调度，不声明逐像素或逐帧等价。

本次只使用已有资源工具读取 MPQ 表、DCC 头、原图、PL2 与 WAV 采样循环元数据；摘录／预览保留在忽略的 `artifacts/frozen-orb-20260930/`，不是运行验收。随后按用户要求完成 Windows Release 编译并更新 `dist/current`；未新写或运行测试、运行游戏或做画面验收，待用户查看。D2S 字段及保存语义不变，飞行阶段、技能延迟、减速和展示音轨状态不写磁盘。

### 共同施法与显示

- 待施法和引导由各自 `PlayerState` 的 `pendingCast`／`channel` 拥有；引导技能 ID 与经过时间直接从这份状态读取，不维护第二份显示状态。开始、推进、停止和释放的内部函数显式接收角色；替换角色值对象即结束其旧施法，不依赖全局施法槽。当前世界与命令入口仍为单玩家，这次归属重构不等于多人联机已实现。2026-09-26 Release 及包 `artifacts/character-start-town-release-20260926/` 已验证充能弹第 7 帧释放、地狱之火持续／转向／停止及 D2S 场景加载；此前按技能独立包保留不覆盖。
- 城镇施法许可从原 `Skills.InTown` 导入，`UseSkill` 在扣蓝、动作和效果生成前拒绝不允许的技能；普通攻击也不得绕过。HUD 当前不可施放图标使用红色着色，技能仍可选择和绑定；精确原版调色板未对照，技能树未学习节点不受影响。此前包内逐项验证火弹、充能弹、冰弹、地狱之火、静电力场、冰霜新星、冰风暴、火球、闪电新星和传送在城镇均拒绝，法力 83 不变、无弹体；冰封装甲正常生效、防御 6→7。原表还允许心灵传动、高级冰甲、强化和能量护盾，许可不代表这些技能效果已实现。
- 原 `SC` 动作按当前武器查 `AnimData`；当前 MPQ 的女巫记录均为 14 帧、速度 256、索引 7 的动作标志 1。快速施法按 D2MOO `UNITS_UpdateCastAnimRateAndVelocity` 的递减收益及 175% 上限改变速率，不再统一使用 0.32 秒。普通 SC 法术到出手帧才扣蓝和生效；死亡、受击中断或换区域取消未释放动作。地狱之火使用上文 SQ 路径。载入建立新角色运行时状态，不恢复上一局待施法动作。
- `Skills.castoverlay` 绑定火、冰、闪电或传送原叠层，附着施法者；新星的 `Missiles.ProgOverlay` 附着受击目标。叠层读取原帧数、偏移及 `AnimRate`，使用 Units 调色板。叠层客户端精确时间仍待画面对照，当前没有采用 Diablerie 的经验性 1.5 倍速。
- 弹体速度按 D2MOO `Missiles.cpp` 的 8.8 路径速度和 75% 系数换算；新星每 5 个游戏帧应用 `Accel`，受 `MaxVel` 限制。寿命读取 `Range/LevRange`。图形按弹体自身时间、`animrate/1024`、`AnimLen`、`LoopAnim` 播放，不再使用全局时钟或对非循环动画取模。
- 移除按亮度生成透明度的处理。`Missiles.Trans=1` 与技能叠层 `Trans=3` 已接原 PL2 的 `pScreen` 混色表，详情及 RGBA 背景量化边界见上文；其余非零弹体 Trans 仍沿用 Diablerie 的柔和叠加材质适配。此共享弹体绘制路径也影响原怪物／武器弹体，未做实机回归。
- `stsound` 在施法开始播放；普通主弹体 `TravelSound` 在出手时每次施法播放一次，新星不会叠播 64 次；冰封球沿途子冰弹按原分组及 Compound 门槛创建独立循环音轨。直射弹体碰撞时播放 `HitSound`，并生成独立计时的原命中动画。无 `AlwaysExplode` 的弹体寿命到期不再额外结算范围伤害。

### 逐项状态

| 技能 | 当前源码行为 |
| --- | --- |
| 冰霜新星 | 移除约 24 个图块的装饰环和立即全范围伤害；与原参考一样使用 64 个整数方向偏移生成真实冰弹，沿路径结算，原 `NextDelay=4` 帧限制重复命中，保留最后碰撞目标，命中显示 `ice_explode`。 |
| 闪电新星 | 使用相同原方向及运动规则，闪电伤害、4 帧命中间隔和 `lightning` 受击叠层；不使用冰冷时长。 |
| 火弹 | 原 `firebolt` 飞行图、`fireexplode` 命中图和原释放／命中声音，单体火焰伤害。 |
| 火球 | 原 `fireball` 飞行图、`explodingarrowexp` 主爆裂图和声音；服务端命中函数 1 的半径读取 `sHitPar1`，不再画旧渐变圆。 |
| 冰弹 | 原 `icebolt` 与 `iceexplode`，单体冰伤和减速，不冻结。 |
| 冰风暴 | 原 `iceblast` 与 `freezingarrowexp1`；函数 4 将冰冷时长转为冻结，并补入 `ELenSymPerCalc` 所指冰尖柱基础等级的时长协同。普通怪物按 `ColdEffect`／`MonsterFreezeDivisor` 冻结，首领等只减速，免疫不被击破。 |
| 冰封球 | 原球体、逐帧散射与到期 16 枚转向冰弹；逐枚冰伤、冰弹基础等级协同、冰冷支配／装备加成与 25 帧延迟。主体穿过单位，无球体伤害；提前撞墙不环射。原图／动作及未核实客户端细节见上文；仅源码，未运行验收。 |
| 暴风雪 | 原中心 100 帧、每 4 帧随机整数落点；独立 9 帧地面冰伤源、十字尺寸与 LastCollide，三技能基础等级协同、冰冷支配／装备加成、SC 出手和 45 帧延迟。原下落／碎裂图、PL2 混色和中心 WAV 循环已接；客户端精确规则边界见上文，仅源码，未运行验收。 |
| 传送 | 出手帧移位；目标仍要求当前区域可站立且 `Levels.Teleport` 允许。原 `teleport` 叠层随角色移动，不再在起点和终点各生成一份地面动画；没有 MVP 的距离上限或额外冷却。 |
| 静电力场 | 原 `light_cast_1` 施法叠层；范围和百分比来自原技能表，`StaticFieldMin` 先判定目标资格，再按整数当前生命求伤害。正闪电抗性减伤，负抗性不放大，闪电支配不加成；不是把伤害强行夹到难度阈值。 |
| 暖气 | `ln12` 的 30% 基础、每级 12% 与装备回蓝相加；恢复按原每帧 1/256 法力截断和最小基础量计算，升级、重置、装备及读档重算，无假施法动画。 |
| 火焰支配 | `ln12` 的 30% 基础、每级 7%，在基础等级协同之后加成火弹／火球；此顺序核对后保留。 |
| 闪电支配 | `ln12` 的 50% 基础、每级 12%，加成闪电新星而不加成静电力场；此规则核对后保留。 |
| 冰冷支配 | `ln12` 的 20% 基础、每级 5%，降低非冰免目标有效冰抗，最低按 -100% 结算；冰冷／冻结时长使用对应抗性，不破冰免。此规则核对后保留。 |

### 未核实边界

此前八项主动技能修订时只核对源码、当前 MPQ 和编辑器诊断，后续物品审查已构建，但不能据此声称所有技能画面或逐帧行为已与原版等价。新增技能的逐项包内验证见上表。火球 `CltHitSubMissile1=fireexplosion2` 的额外散布图块、静电力场目标专属命中表现、弹体灯光的精确衰减／遮挡／闪烁、完整单位命中尺寸、叠层完整客户端时序仍待核实；通用地形／对象阻弹及弹体尺寸已按原表接入，尚未运行验收，边界见上文。不凭空生成这些细节。

当前仅保存原版 D2S v96 的角色技能等级、选择和八个快捷键；弹体、叠层、未完成施法和怪物临时状态不写入磁盘。本次重构未改变保存语义，效果实例／模拟帧不写入 D2S，内部行为枚举不再承担原技能身份。格式固定，支持度变化不升级格式，详见[存档说明](../../modules/SAVES.md)。此前的技能树截图与存读档记录不代表本轮战斗效果已验收。
