# 第五幕怪物执行边界

更新：2026-10-18。实际身份、原版依据和逐项限制统一见[第三至第五幕接入台账](ACT3.md#逐项接入)，本页不重复逐变体记录。公共边界见[COMMON](COMMON.md)。

| 范围 | 当前源码与缺口 |
| --- | --- |
| 跨幕客串家族 | 蜘蛛、ThornHulk、Fetish／吹箭／萨满／骨Fetish、吸血鬼、狒狒、蝙蝠、跳跃者、蛆虫、血鹰及其他已登记变体按自身MPQ行准备；不能扩展为所有客串身份已验收 |
| 第五幕专用家族 | 已接ReanimatedHorde、FrozenHorror、BloodLord、DeathMauler、BaalMinion、PutridDefiler、SiegeBeast、Imp、GenericSpawner、Minion／SuicideMinion、Overseer、MinionSpawner及Succubus／Witch；技能、骑乘／转换／出生和明确缺口见台账55至69项 |
| Shenk／Nihlathak | Shenk沿Overseer并保留任务激活；Nihlathak五技能及Fn128源码接入见74项，Windows Release客户端／独立服务端构建通过，尚无本批正常战斗实机 |
| 古代人／Baal及五波 | 开战／任务组重置、波次和任务完成已有控制；Madawc投斧／Shout、Talic旋风斩、Korlic跳跃攻击及专用AI源码接入见75至77项，雕像独立生成／失败重试／死亡恢复见78项，原文确认／死亡云见79项，五种Baal触手AI及Do140出生见80至81项；普通Baal／clone的技能、主AI及出生依赖见80至92项。王座五波技能、原地换类／走向楼梯及任务计时循环替换见93至97项；Taunt的AI及客户端控制见98至100项；全部领队／词缀／召唤清理尚未完整迁入 |
| 投石机／环境 | Catapult本体AI及真实四类身份见103项，弹药控制Spotter服务端见104项，客户端CltSt47见105项，inert战斗家族准入补正在106项；CltHit46／47／48／50专用落点源码见114至117项（两包构建通过，专项运行未验收）；CltDo43发射端附着表现及有限实机见122项；EvilHole／demonhole生成控制见111项；其他环境控制仍有缺口 |
| 高级钥匙／红门事件（暂缓） | Lilith／UberDuriel／UberIzual及Uber三首领、专属召唤与事件流程不列入本轮常规战役完成条件；已有代码保留，范围与依据见[SPECIAL](SPECIAL.md) |
| 客户端／恢复 | FrozenHorror及跨幕喷火家族的Clt54连续发射已补源码，随机／动画实机轨迹待核对；王座Baal波次压缩／恢复已补源码（119项，两包构建通过，专项运行未验收）；其余完整休眠重建、原全局交错／随机及全部多人组合未认证 |
| 牢门／囚犯 | 牢门及window1／2已接自身属性／外观与空Idle，不追击／攻击，见107项；当前MPQ启用的act5barb1／act5barb2共用NpcBarb战场友军AI、原A1及第8组目标注册已接源码，见118项（两包构建通过，act5barb1已有限观察选敌／A1／受伤死亡）；救援囚犯act5pow沿既有任务逃跑流程；act5barb3为城镇守卫（Npc），完整通用NPC／MapAI仍未接 |

构建与有限运行范围见[运行包记录](../../development/BUILD_AND_RUN.md#运行包记录)。管理生成、补资源及击杀仅准备隔离条件，不能认证正常战役；任务／囚犯／古代人及五波限制见[第三至第五幕任务](../quests/ACT3_5.md)。野蛮人佣兵独立见[佣兵](../characters/HIRELINGS.md)，不混入敌对怪物完成数。其他首领与精英边界见[BOSSES](BOSSES.md)、[ELITES](ELITES.md)。
