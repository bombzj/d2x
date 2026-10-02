# 死灵法师技能

当前接入 Raise Skeleton（Skills.Id 70），使用公共战斗单位与阵营接口。Skeleton Mastery（69）和 Summon Resist（89）的有效等级参与新召唤物属性。诅咒逐项实施状态见下节。

## 诅咒逐项实现

2026-10-03：Amplify Damage（66）、Weaken（72）、Decrepify（87）、Lower Resist（91）已逐项Release链接并更新dist/current，不运行测试或游戏。范围、等级期限、费用、原状态和声音从当前MPQ导入，SC出手后按目标中心整数平方范围／阵营和MonStats2.mA1资格施加；附体免诅咒、Attract不可覆盖及诅咒互斥沿公共状态。伤害加深／衰老减物抗、削弱／衰老减物理攻击、衰老移动／攻击速率均有现有消费者；降低抵抗只影响火冰电毒，不降魔抗。减抗按非佣兵怪物原基础抗性免疫独立除5，重施不累积。原状态叠层沿公共Overlay加载，精确客户端节拍和完整邻房过滤仍属现有世界适配，未实机认证。依据D2MOO SkillNec::SrvDo30、sub_6FD0B450／sub_6FD0B2B0／sub_6FD0B3D0及Skills::sub_6FD10360；临时诅咒不入D2S v96。

## 数据和规则

- `content/skills/necromancer_data.*` 读取当前 MPQ 的 Skills、MonStats、MonStats2、MonLvl、PetType、Sounds。校验 srvstfunc=15、srvdofunc=31、原召唤／被动公式及 necroskeleton 的 noRatio；未知公式或缺少资源明确报错，不回退到演示数值。
- 施法使用角色原 SC 的出手帧。目标须已完成 DT、未被消耗、无死亡 udead 禁选标志，并满足原 MonStats2.corpseSel 与非零 Velocity；冻结碎裂目标不会留下可用尸体。尸体不能反复使用，也不能再被沉沦魔巫师复活。获准的敌对怪物替身沿用实际显示的原 DT，尸体资格仍读取真实身份的原表。起身 S1 和 A1 出手数据、可放置地面均可用时才消费尸体与法力。城镇禁止施放。
- 法力读取原 6 点及每级增加 1 点。数量上限为等级小于 4 时等于等级，否则 `2 + 等级 / 3`（整数）。超过上限移除最早的存活同类召唤；装备切换使有效技能等级下降时同步缩减数量。
- 宠物等级为 `技能等级 + 主人等级 × 3 / 4`，上下限为 1 和主人等级。基础生命按难度为 21／30／42，来自当前原表。支配每级增加的生命、伤害，召唤等级的生命／伤害百分比、AR／防御、分段伤害，以及召唤抵抗 dm12 参数全部导入，不以这些文档数值替代原表。
- 基础防御／命中叠加对应宠物等级的 MonLvl L-AC／L-TH。伤害保留 8 位小数；生命恢复、格挡、暴击、碰撞尺寸、基础抗性及移动速度使用原怪物记录。召唤时固定属性快照；之后的有效支配等级变化不追溯重算已有骷髅。

## 战斗和生命周期

召唤物保存独立实体 ID、主人、阵营及 Summon 角色，位于 `WorldState.companions`，使用公共 `CombatUnit`、关系过滤、命中／格挡、伤害、抗性、状态和死亡入口。玩家、罗格、骷髅为盟友；敌方 AI 和弹体可选择骷髅，近战和范围效果均按关系判定，不以怪物容器推断敌我。

召唤物击杀追溯到主人以结算经验、任务及主人的 MF／GF；不会借用佣兵装备的加成。骷髅自身死亡不产生掉落、经验或任务击杀。死亡表现使用通用 UnitDied 通知，与可掉落怪物的 EnemyDied 事件分开。死亡记录在本局保留以维持迟到弹体／持续伤害的来源身份，画面只保留原死亡动画。

跟随参考 NecroPet 的近战分支：远离主人 28 时追赶、50 时重定位，通常扫描 24 范围内的可见敌人，可响应主人攻击目标；近战 80% 选择 A1，其余等待 10 帧。追赶速度加值由原 Run／Velocity 比例计算。地形／对象寻路已按真实怪物身份的原十字／方形体积查询；动态避让使用项目现有世界网格适配，并非移植完整原版宠物 AI；复杂拥挤场景仍须实机查看。

出入口、回城和传送按 PetType.warp 带走存活宠物并取消旧动作。主人死亡时移除骷髅。召唤物、尸体消耗、关系表和施法动作仅在本局存在，不写入 D2S v96，也不增加私有指纹或旧档迁移；保存后重开需重新召唤。

## 原图和使用入口

使用原 `necroskeleton`／SK 组件与 NU、WL、A1、GH、DT、DD、S1 动画。盾牌按 MonStats2 的原组件集合及 SkillNec 的选择规则保留；基础身体与武器按普通召唤的组件级别选取。声音来自 necroskeleton 的 MonSound 与原施法声音。左上角使用 PetType.baseicon 指向的 skeletonicon.dc6 和当前存活数量；位置、缩放及计数排版沿现有宠物栏适配，未宣称原客户端逐像素等价，组生命条和自动地图宠物标记暂未接入。

用户可从 `dist/current/Play.cmd` 新建 Necromancer，保留原初始法杖提供的召唤骷髅等级；在野外击杀敌人，待死亡动画结束，将右键技能选为 Raise Skeleton 后点击尸体。继续召唤可查看数量上限、共同跟随及敌方转而攻击骷髅；没有添加调试装备、技能点或专用测试入口。

## 本地证据和交付边界

- D2MOO `D2Game/src/SKILLS/SkillNec.cpp`：SrvSt15、SrvDo031、SetSummonBaseStats、SetSummonPassiveStats、SetUnitComponent。
- D2MOO `D2Common/src/D2Skills.cpp::D2COMMON_11017`：可消耗尸体；`D2Game/src/MONSTER/Monster.cpp::MONSTER_SetComponents`：初始组件。
- D2MOO `D2Game/src/AI/AiThink.cpp::sub_6FCE3740`、Fn067_NecroPet：近战宠物行为；SUnitDmg／PlayerPets：归属、收益、数量和旅行。

参考仓库与导出表不进入源码提交。按用户要求不编写或运行测试、不启动游戏；编译和交付状态见 [构建](BUILD_AND_SHARE.md)。多人联网、第五幕友军、复活怪物及其他召唤技能尚未实现，战斗关系和伤害入口为后续接入共用。
