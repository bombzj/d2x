# 首领与专用策略

本页维护专用首领／固定单位策略、原图绘制及任务控制与战斗AI的边界。精英rank与SuperUnique数据归[ELITES](ELITES.md)，普通家族归各幕台账，逐技能程序归[怪物技能](../skills/MONSTERS.md)。专用AI、幕首领和固定金怪不是同一个分类。

## 五幕首领绘制

已静态核对当前`assets/mpq2`的MonStats／MonStats2、六种图形token的49份COF、所引用组件路径和DC6帧头；覆盖安达利尔、都瑞尔、墨菲斯托、迪亚波罗、王座巴尔及战斗巴尔／分身。共208个COF组件引用中207个存在相应原图，王座巴尔DD的TR缺原图。此次范围是资源与源码核对，未构建、运行或进行原版逐帧画面对照。

| 幕／图形 | 核对结果与当前消费 |
| --- | --- |
| Act 1：AN，7份COF | 本体与喷毒／死亡附属组件来自原DCC，透明S1／S2为mode3；未新增常驻光环。原死亡控制器沿既有入口 |
| Act 2：DU，7份COF | 多部件、DT／DD与原DCC均存在。已补原死亡第13帧创建的`durieldead_fwd/rear`覆盖图，按AnimRate推进后保持末帧，DD保留；只对已收到的新鲜死亡动作启动，不给晚入视野的尸体补播 |
| Act 3：MP，7份COF | 本体组件全为DC6。常驻雾气不是身体COF：原`Overlay.mephisto`指向`MephistoOverlay.dc6`，26帧、AnimRate16、Trans3、PreDraw0。已补单位创建时的独立循环覆盖图及DCC不存在时的DC6加载；DC6按原cell基准减帧高定位。DT的S2另为透明mode2，已接PL2 trans[0]，不再画成不透明层 |
| Act 4：DI，14份COF | 普通动作DCC，142帧DT使用DC6；COF的死亡／技能透明S1仍按原mode3与逐帧组件顺序绘制。未新增无依据的常驻效果，完整死亡及技能附属程序仍见下方边界 |
| Act 5：41，3份COF；42，11份COF | 王座与战斗身份分开。已补王座专用`baal_on_throne`护罩（原`expansion/baalshield.dcc`），不套给战斗巴尔或分身。42普通动作与透明技能组件、200帧DC6死亡保留原资源。41的DD引用缺失`41trlitddhth`，明确保留缺口，不借用42或其他动画 |

零售依据为本机1.13c D2Client（MD5 `f5860c629d309b8fc1f96174babcf633`）A7A55→A70C0创建附属表现：class242在A7282以type3挂Overlay122，class543在A729B挂275；名称和行号由当前MPQ解析，Expansion分隔行不计入索引。6B300读取覆盖图偏移／高度及AnimRate，6C130使type3循环、type5保持末帧；4E824–4E847在DD清常驻覆盖图，但保留Duriel血迹。21210在Duriel DT动画第13帧清旧覆盖图并创建52／156；当前接入血迹创建及保持，不认证全部旧覆盖图清理和同帧插入顺序。

`RemoteScene::monsterOverlays`只持展示状态，以原单位身份和assignmentRevision隔离实例，跨动作保留，单位移除／重新指派及换区／换局回收；跟随本体连续显示位置、图形偏移和显隐。雾气不依赖技能或状态包，不生成服务端单位、伤害或碰撞。新增专用挂载只对零售协议启用，未核实的修改版专用回调不套用。

公共合成器保留COF透明开关与CelDraw模式，未启用透明时忽略DrawEffect字节。世界单点取光路径已接mode0／1／2的原PL2透明表、mode3 Screen和mode4 Burn，沿COF原组件顺序取光后混色。光照结果直接传原索引，避免同色调色板项在RGB反查时换索引；背景仍来自RGBA反查。墨菲斯托DD的COF为16方向而DC6为8方向，继续走已有方向映射，未据资源差异修改原图或编造帧。

本次不认证全部Boss像素、阴影、技能／死亡回调及随机交错已一致；边缘视口中附属图沿本体可见性裁剪，原创建首帧／同帧顺序仍待运行对照。当前缺资源项明确保留；全场景与原客户端对照及声音边界见下文和[光照](../world/LIGHTING.md)。

## 已接第一幕专用程序

| 程序 | 权威行为及数据 |
| --- | --- |
| Andariel | 四项MPQ AI概率；普通近战、AndyPoisonBolt、AndrialSpray原MonSeq九次释放。公共andarielSprayRay只计算原整数方向／起点；客户端保留subtile中心，服务端执行命中和毒伤 |
| BloodRaven | 原home边界、走跑／撤退／绕行、普通弓箭、Quick Strike独立序列；受击后限制远距离射击，绕行失败继续近距离撤退／射击分支；召唤zombie2采用原位置随机、难度数量上限，巢子NOXP／NOTC。地狱MonProp的knock从表准备，chance空白／0按原规则无条件应用（非地狱运行认证） |
| Griswold／Smith | 原近战／等待、Smith随生命变化速度；The Smith另保留固定SuperUnique身份／词缀 |
| Countess | 独立Countess AI，不改所有CorruptRogue；分别核对本人／目标与home的房间，回位使用原坐标距离；使用真实DS1 path nodes，逐点FirewallMaker／地面火，原700帧周期；没有路径节点不捏造火墙点 |
| GargoyleTrap | 原距离、轴向窗口、概率、射后恢复与等待；静止可击杀单位，原seq_gargoyletrap的event4释放。gargoyleTrapRay显式保留原Clt／Srv轴向差异 |

monsters持home、真实DS1技能位置、谱系和死亡连锁；[boss_decision](../../../src/gameplay/monsters/boss_decision.cpp)是服务端策略纯函数，skills持动作与多次释放，missiles／effects执行弹体和效果。A1无MissA1的技能用原event1（安达利尔毒弹／伯爵夫人火墙），远程A1用event2；Nest／Gargoyle及对应序列沿原事件。方向共享但保留Clt／Srv差异，原函数证据见[参考设计](../../architecture/REFERENCE_DESIGN.md#第一幕怪物公共计算依据)。

血乌与安达利尔死亡连锁采用QuestsFX的范围、延时和undead筛选；连锁本身不授予任务奖励或首杀完成。任务资格见[第一幕任务](../quests/ACT1.md)，技能201／164／167／172及Countess的程序和缺口见[第一幕](ACT1.md)与[怪物技能](../skills/MONSTERS.md)。

## 第一幕死亡表现

RemoteScene按原1.13c D2Client RVA20FB0消费新鲜DEATH动作，不从已入场尸体推造历史演出；重复死亡通知只触发一次，原死亡图形在首领进入视野时提前准备。同区资源长帧保留已启动的死亡程序与子效果；换区／换局仍清空，不重播过期死亡。安达利尔52230创建当前MPQ `andycontrol0`，血乌52320创建`brdeathcontrol`（原50帧启动计时）、`brdeathspirits1`与`brdeathsmokes1`。现有公共客户端弹体队列执行烟雾／灵魂原ExplosionMissile链、CltDo21／22的闪电、CltDo27的游走火墙，以及CltDo29的原火柱／落石选择；图形、寿命、链接和Clt参数读取MPQ。Andariel火柱使用显示像素位移，不产生伤害／碰撞或服务端单位。

零售Mephisto死亡链另已按本机1.13c补齐：20FB0→52270依次创建MPQ的mephistodeathcontrol、mephistoholedelay、mephistodonotdraw，首控制器两个Target倒计时均75。Clt26（BB780／B82B0）分别按CltParam1／2续计时，半径取CltParam3，第一子项继承技能／等级并保存方向参数，第二子项在坐标随机之前抽等级1–5，仅创建成功后抽TargetX数量。Clt28（B8010）只在elapsed等于CltParam1时，以实际主人当前整数格按左／上／右／下创建四项，原flag2000消费子项自身随机设置图形朝向，零速保持。Hit33（B5DD0）按实际TargetX数量、半径至少3及原零方向重抽顺序产生碎石；Hit34（B07E0）到期仅隐藏仍存在的相关单位。显示隐藏按实际指派代次撤销，不改变权威生命／碰撞／尸体资格；这些新回调仅零售启用。此批全目标Release构建通过，普通难度自然DS1首领经管理伤害触发的一次实际死亡通知已观察三控制器、四邻爆裂、火墙／碎石、地洞完整链与持久隐藏及尾部清理；版本和准备条件见[缺口验收](../../development/ORIGINAL_GAPS.md#待验收客户端程序)。原版随机／绘制逐发生对照、完整声音及失败／重指派组合仍待验收。

零售Duriel死亡链按20FB0→52620在死亡坐标创建363，实际主人为本地玩家、Skill0／Level1。Clt36（B41F0、常量表D3610）以CurrentFrame严格分档350／300／200／175，分别使用原3,5／2,3／3,2／5,1／100,5周期；325帧启动幅度20、6000毫秒震屏。42爆裂与364落石各从父随机取[-20,19]双坐标偏移，图形朝向来自成功子项自身随机；落石重设Range15、Q11高度150、速度-10及原动画速128。Hit41（BBEE0→A27B0／B9A80）拒绝单位接触，按实际路径目标X重设种子（本机Common10614明确高字666），恰三次[-6,5]双坐标探针，平方距离不足4不补抽；有效365以原距离期限和高度30轨迹创建，最后366烟雾仍要求真实主人存在。资源取当前MPQ；原代码固定类号在可见时预载，不自造贴图或伤害。此批Release全目标构建及弹体定向clang-analyzer通过。普通难度自然DS1 Duriel经管理伤害触发的一次实际死亡通知，已观察363、42／364及365／366，演出结束客户端弹体清空；版本和准备条件见[缺口验收](../../development/ORIGINAL_GAPS.md#待验收客户端程序)。完整声音、震屏／像素与逐随机及失败组合仍待对照。

零售Izual死亡视觉按20FB0→53070的真实DEATH通知创建MPQ379（izual control）。Clt42（BA680／B55C0）每帧先消费父随机产生[-10,9]双坐标雾380，再读取固定379的Range与Param1／2，严格在Range-Param2＜CurrentFrame＜Range-Param1时，查找实际主人并取[-5,4]双坐标雷382；仅创建成功后追加一次父随机选择-3／-4／-5的Q11重力。原C7C80保留非零已有重力，以15帧／高度0计算初速度后清位2，不改写子项Range。380沿MPQ ExplosionMissile进入381，资源和参数不按名称猜测。原53070的4691调用实际进入45570任务音乐切换器，45660–4595E确认250为客户端结束帧。现已接入既有任务音乐单槽，MPQ music_quest_izual在0帧启动、250帧退出、无追加语音，原环境曲从Block0恢复；当前Music Vol0／Fade Out125仍读MPQ，不误作普通附着音效。视觉及音乐分支源码已通过全目标Release构建与定向静态检查；自然DS1单位死亡的基本雾／雷／退场链已在最终版复测，剩余验收见[缺口清单](../../development/ORIGINAL_GAPS.md#待验收客户端程序)。音乐未报告缺失／解码失败，但严格帧、听觉、原后端／完整恢复仍待验收。

控制器只持25Hz显示时钟和独立随机流；权威死亡连锁仍由monsters／effects结算。完整像素粒子随机流、原相机震动、全部落石残片变体及各难度死亡连锁仍待原版对照，其他幕死亡程序尚有缺口。

## 其他幕与特殊首领

| 范围 | 当前实现与明确缺口 |
| --- | --- |
| 第二幕Duriel及任务固定目标 | 已接Duriel冲锋／Jab／Smite／光环、Summoner五槽、Radament支持技能和虫后死亡毒云；AI／动画接线与缺口见[第二幕](ACT2.md) |
| 第三幕Mephisto／议会 | 已接Mephisto专用AI及六技能、议会治疗／Hydra；具体源码边界见[46／72项](ACT3.md#逐项接入)，正常首领战斗与全难度未完整验收 |
| 第四幕Izual／Hephasto／Diablo | 衣卒尔Fn055及新星、Hephasto沿Smith已接；Diablo第73项已接真实身份、Fn051及技能，封印组不等于完整精英验收，见[第四幕](ACT4.md) |
| 第五幕古代人／Baal及五波 | 开战／重置、波次与任务完成控制已接；古代人专用AI、Baal／分身与王座AI已接，全部词缀／召唤清理和恢复仍缺，见[第五幕](ACT5.md) |
| 牛王与高级事件 | Cow King随第一幕常规覆盖复核；Uber／Pandemonium高级事件本轮暂缓，已有接线保留，专用程序／出生缺口不计常规完成门槛，见[SPECIAL](SPECIAL.md) |

第三至第五幕可能使用已授权替身，真实任务身份保留不等于真实首领战斗完成。管理击杀不能作为Boss AI、技能组合、伤害平衡或正常通关依据；任务资格与死亡回调见[第二幕任务](../quests/ACT2.md)和[第三至第五幕任务](../quests/ACT3_5.md)。

后续每个首领登记真实MonStats／SuperUnique、生成条件、专用AI、技能与序列、阶段／召唤／死亡演出、难度、奖励与任务依赖。尚未完成的原技能不回落普通攻击，世界／任务控制器不承担首领战斗规则。
