# 怪物、首领与精英技能

更新：2026-10-18。本页负责非玩家敌对单位的技能入口与覆盖台账。身份、AI、人口、动作、数据契约、原版依据及缺口由[怪物目录](../monsters/README.md)链接负责页维护。第三至第五幕按[逐项接入](../monsters/ACT3.md#逐项接入)补齐，未完成项目不计为覆盖；下表描述当前源码入口，完整行为仍须按原程序逐项对齐。

## 当前执行家族

| 使用者／能力 | 当前自研入口与边界 |
| --- | --- |
| 沉沦魔萨满 | 本族尸体复活、按BaseId链选择火弹、普通近战；不开放任意尸体复活 |
| 鸟巢／GenericSpawner | 原序列出生、数量／间隔及耗尽死亡；Nest的St49保存实际类型／位置／模式并预约，Do91释放预约后创建无经验／掉落子体。GenericSpawner读取区域已选类型，但有效MonStats.spawn优先，当前evilhut使用imp1；预检与出生偏移分别核对 |
| 蜘蛛 | arach1至7的SpiderLay轨迹、蛛网／地面减速；生命周期与效果分开持有，变体接入及缺口见第三幕 |
| 沙地跳跃者 | Leap（132）的怪物St40／Do77分支：起手原A1接触、落点／飞行和落地后退，UNINTERRUPTABLE／SKILL_MOVE与原A5结束同步；不追加玩家Leap范围击退 |
| 骨Fetish | MonStats.deathDmg的死亡模式回调：原飞弹117承载物理范围伤害，原表等级生命／Ladder、难度倍率及单位随机；与火强化死亡效果分开 |
| Fetish萨满 | FetishInferno（178）沿原St53／Do95：A1事件2、INFERNO起止、calc3续放、MonStats2收尾；原飞弹144以自身行伤害和实际等级执行；Resurrect2沿原序列复活。FetishAura不增加AI施放分支；客户端Clt54连续发射源码与待验边界见怪物台账74项，萨满死亡重建另见第三幕 |
| ThornHulk | MonFrenzy（215）：A2的原攻击事件，独立命中／伤害处理后才叠加monfrenzy；skill_frenzy栈数以技能等级为上限，持续时间及移动／攻击／其他动画加速均读Skills公式，沿公共状态计时到期移除；AI连击与普通攻击冷却见第三幕 |
| 吸血鬼 | vampire1至9准备自身Fireball／FireHead／Firewall／Meteor，Fn028按原表掩码选择；普通法术使用独立可射击目标，火墙／陨石使用主目标；冷却和动作核对见第三幕第07项 |
| 萨卡兰姆祭司／议会 | ZakarumHeal（179）沿Do96；祭司按最后合格单位、议会按严格最低整数生命百分比选择。议会Hydra（62）沿St14／Do144，怪物拥有者不进入PlayerPets或套玩家阵营；三头各自Fn086及截止帧，普通S1另发自身MissS1 |
| 邪恶之母／FingerMage | 邪恶之母Nest（167）的`seq_vileres`使用事件2；St49保存自身加原表偏移，Do91去除预约后搜索幼体。FingerMageSpider（196）沿原Do101构造飞弹177、原循环公式及SrvDo11拥有者目标回退；命中沿既有SrvHit19状态／重应用规则 |
| Regurgitator | RegurgitatorEat（214）沿S1事件1／Do108，删除实际尸体GUID并按其原生命上限和calc1给自身回血；随后普通A2发射自身regurgitatorcorpse |
| AbyssKnight | DoomKnightMissile（335）沿Do148按实际S3组件偏移飞弹；MonBoneArmor（210）沿自己的Do18状态、公式和事件槽，当前MPQ没有玩家Bone Armor的吸收事件，不补造回调；MonBoneSpirit槽在Fn073不主动调用 |
| OblivionKnight | MonBoneSpirit（211）沿Do10传递原目标并初始化SrvDo7；Decrepify（87）与MonCurseCast（212）沿公共诅咒列表。零售Do112抽签表为66／72／82／87／91，但仍按原索引执行跳转分支，状态／公式行与回调分开保留，不能套用参考代码的六项表 |
| 巨型恶魔 | MegademonInferno（319）共用已接St53／Do95，读取自己的飞弹675、两帧密度、S1回绕和结束参数；不复用Fetish飞弹伤害或女巫Inferno；客户端Clt54连续发射源码与待验边界见怪物台账74项 |
| Imp | Imp Teleport（330）沿Do129与attached／sourceunit，逐帧骑乘跟随由ActiveState的Do147执行；Imp Fire Missile（299）沿Do132按类链偏移，Ex（356）保留基础飞弹。骑乘Fireball（283）使用原Do0构造，Inferno（282）沿St59随机期限、Do126三帧续放及十三帧收尾；原技能ID、构造后Range、Clt71／72／74／88与S7定位分别处理。ImpBolt只保留声明。源码／原DLL静态核对 |
| Overseer／SuicideMinion | Cry Help（290）沿Do128复制有期限的真实仆从命令；Healing Vortex（291）发原弹体425并沿已有SrvHit43治疗；Overseer Whip（295）沿Do131的公式／抽签、bloodlust列表和同GUID重建，特殊AI15继续Fn117。自爆是UMod33死亡定时器及客户端动画回调，不能算作Skills施放 |
| MinionSpawner | 技能302沿St62保存出生类／坐标／模式，Do135先原点、失败再四圈搜索；成功后禁经验／掉落、保存生成器owner并播放sumoverlay。与Nest的预约／不可中断流程分开；源码／原DLL静态核对 |
| 安达利尔 | AndyPoisonBolt及AndrialSpray多次释放；毒伤、方向与原Clt／Srv偏移分开处理 |
| 血乌 | Quick Strike序列及zombie2召唤；普通弓箭与专用召唤分别准备 |
| 伯爵夫人 | 真实DS1路径节点上的FirewallMaker／地面火；缺节点不自造落点 |
| GargoyleTrap | 原序列发射、距离／轴向限制；是可击杀单位型陷阱，不是刺客陷阱或普通物件 |
| 精英主动／周期效果 | Amplify Damage、友方属性光环、Holy Fire／Freeze／Shock脉冲、低生命Teleport；只是精英来源入口，不表示对应玩家职业全部可用 |
| 精英受击／死亡效果 | 电强化延时／冷却与充能弹、冰／火死亡事件等由独立效果时钟处理；并非每项都是可施放Skills行 |
| ReanimatedHorde | Self-resurrect由UMod34死亡／定时回调启动；St61复起、Fn114恢复目标标志，34回调恢复奖励资格。Charge用自身序列及三射线预检；源码核对 |
| Succubus／SuccubusWitch | Defense Curse（309）／Blood Mana（310）沿Do30，SuccubusBolt（345）沿St4／原弹体665；两类AI、技能槽和动作各用自身原表。普通Blood Mana费用由[公共机制](COMMON.md)维护；源码核对 |
| Izual／UberIzual | Frost Nova（44）沿Do22原生64方向构造；Uber另接Chilling Armor（60）的Do18／EventFunc01和MonTeleport（184）的Do98。冰甲反击使用实际反击者，无玩家拥有者前提；源码核对 |
| Mephisto | PrimeLightning／MephistoMissile走普通原生飞弹，PrimeBolt走Do17；PrimePoisonNova走Do99及Clt55，MephFrostNova走Do22，Blizzard走Do28。原始弹速、回绕及施放后AI随机继续点见怪物台账 |
| Diablo | Do100、Do102／Clt58、Do152／Clt56、Do024、St54／Do103、Do104、Do124／Active146已接Fn051真实身份；DiabRun客户端St41／Do59保留独立定点移动／序列，骨牢以原房间探测／出生并接受旅行系统门户。Clt56灯光抑制标记贯穿出生、73合并与绘制；源码入口、原版地址和未验收边界统一见怪物台账第73项，未完成战斗／随机／恢复验收 |
| Korlic | Leap Attack独立接怪物St41／Do78和客户端St30／Clt44；落点预约、原Run速度、空中序列保持、落地定时与命中分别处理，未套用SandLeaper普通Leap。完整交错及实机限制见[台账77项](../monsters/ACT3.md#逐项接入) |
| Talic | 旋风斩沿怪物St38／Do76，保留本族序列回退／单位随机命中次数与固定终点碰撞预约；命中复用公共近战、原公式、接收与反射。客户端St31／Clt45独立推进，不套用玩家命中间隔，见[台账76项](../monsters/ACT3.md#逐项接入)；本项未做战斗实机 |
| Madawc | Shout复用公共战吼状态和飞弹命中程序，新增野生怪物Do68入口，64个环形原生飞弹先于自身状态发布；未做本项实机，见[台账75项](../monsters/ACT3.md#逐项接入) |
| Baal Monster Spawn | 原550飞弹SrvHit54读取拥有者当前Param1后走公共任务SuperUnique出生，参数映射动态读取当前MPQ；王座主AI已连接，见[台账94及97项](../monsters/ACT3.md#逐项接入) |
| Baal Corpse Explode | Do141沿原0x3002尸体查询逐具调用公共Do55，保留目标绑定与逐具背压；当前MPQ的CltSt82在1.13c越界而不调用。王座AI入口见[台账93及97项](../monsters/ACT3.md#逐项接入) |
| Baal Nova／Inferno／Cold Missiles | 分别沿Do22、St53／Do95及Do139；原S3／S1／A1事件、Clt25／54／81和Do139首弹后置参数见[台账82至85项](../monsters/ACT3.md#逐项接入)。普通Baal／clone专用AI见92项，完整正常战斗仍待对齐 |
| Baal Taunt | 原284沿Do28目标放置；控制飞弹SrvDo34的剩余帧数及CltSt46／Do56／Hit62／63已接，真实非敌对控制Actor使用Fn136；源码及缺口见[台账98至100项](../monsters/ACT3.md#逐项接入)，完整行为仍待对齐 |
| Catapult五种弹药 | Spotter按Fn126沿A1调用公共Do28，CltSt47目标锚点及原图形轨迹见[台账103至105项](../monsters/ACT3.md#逐项接入)；CltDo43发射端附着表现见122项，CltHit46／47／48／50源码见114至117项，完整弹药组合仍待对齐 |
| HellMeteor | Fn033／原213技能和坐标Do28服务端见[台账101项](../monsters/ACT3.md#逐项接入)；落地CltHit36／起始CltSt44源码见112至113项，完整行为仍待对齐 |
| Submerge／Emerge | St51／52保留原目标资格开关；Do94使用MonSeq显示零帧对应的序列位置停帧，支持触手／触手头倒放；不把触手下潜改成FrogDemon的额外脱离碰撞流程。六种水域身份及Fn056／057见[108项](../monsters/ACT3.md#逐项接入)，尚无该项实机结论 |
| Baal Tentacle | Do140逐次抽数、原房间单点出生及初始A1／Idle15已接源码；普通Baal／UberBaal与clone的BaseId分支分别保留。普通Baal／clone的AI见92项，出生／恢复实机尚未完成，见[台账80至81项](../monsters/ACT3.md#逐项接入) |
| Nihlathak | 五技能及Fn128接入见[台账74项](../monsters/ACT3.md#逐项接入)；Do55复用原尸体资格及公共尸爆，Do129接无拥有者／无单位目标的坐标传送，St62保存实际出生类型和模式。共用Clt54按客户端25Hz发射两束；本批尚无正常战斗实机 |

普通箭矢、尖刺、骷髅法师元素弹、近战与元素附伤已有共享程序，但A1／A2或MissA1／A2不一定对应独立Skills行。SkeletonRaise槽是复活动画，不归普通AI主动；关系见[第一幕](../monsters/ACT1.md)。第三至第五幕程序尚未全面迁入，任务死亡链和身份替身不能认证战斗技能。

## 第二幕技能逐项台账

当前MPQ74个可击杀身份的Skills槽去重30项；下表区分主动技能、动画声明和被AI关闭的槽。等级读取SkNlvl及MonsterSkillBonus，Duriel光环等级另从aip1读取。Skills以外的MissA1／A2和固有回调亦列于表后，不将其混算为技能行。

| ID／Skills原名 | 使用者／执行 |
| --- | --- |
| 10 Jab | 甲虫S1两击、Duriel S1三击；ln34伤害比例、命中加成，各释放帧独立命中 |
| 44 Frost Nova | Summoner；64个原环形方向及冷伤／冰冷时长 |
| 47 Fire Ball | Summoner；原fireball伤害与范围爆炸 |
| 49 Lightning | lightningspire第2槽；原arcanelightningbolt及等级 |
| 55 Glacial Spike | Summoner；原范围爆炸／冻结，难度冻结时长除数 |
| 72 Weaken | Summoner第5槽；目标中心半径、持续时间与damagepercent负值，状态事实同步 |
| 97 Smite | blunderbore／Duriel；A2伤害、命中、眩晕ln12上限250帧、命中类型0x65 |
| 107 Charge | Duriel；A2、ln34比例、额外近战范围、原速度加成、击退和0x70命中类型 |
| 114 Holy Freeze | Duriel；AI等级的原光环、周期脉冲及状态，独立于精英光环 |
| 132 Leap | SandLeaper；先A1后向起跳时选定的2×目标−起点跳跃，落点按原50格边界搜索空位，Run速度及skill_move；怪物落地不加玩家范围击退 |
| 156 Fire Hit | SandRaider；S1物理、命中及S1元素附伤，独立于A1／A2 |
| 157 UnHolyBolt | GreaterMummy／Radament；按BaseId链偏移魔法弹，不与普通A2毒弹混用 |
| 158 SkeletonRaise | 骷髅声明；继续通过实际ResurrectMode显示复活，不添加主动AI技能 |
| 159 MaggotEgg | rank＜5时rank只，否则min(12,5+(rank−5)/3)；原子怪、位置探测及孵化后终止 |
| 161 MagottUp | SandMaggot／Queen声明；检查占位、恢复受击资格及原上钻序列 |
| 162 MagottDown | SandMaggot／Queen声明；开始钻地时取消受击资格，释放时按当前生命10%恢复 |
| 163 MagottLay | SandMaggot／Queen声明；原朝向偏移、spawn子怪及S1出生，只禁经验 |
| 166 Swarm Move | 5步移动／路径回退入口；原Swarm使用Fn019，不主动施放此槽 |
| 167 Nest | crownest1至4：St49固定点预留／不可中断，原SQ事件4清预留并沿房间随机流出生，子怪NOXP／NOTC；成功预检即消耗一次尝试，耗尽NOTC死亡。sarcophagus保留独立AI入口 |
| 169 VampireFireball | vampire1；原vampirefireball弹体、范围爆炸及难度等级 |
| 170 VampireFirewall | vampire1／Summoner；Do024按两侧maker→可选中心地火顺序创建，使用原整数垂线终点和原始构造入口，路径地火由原飞弹程序派生 |
| 171 VampireMeteor | vampire1声明；延迟中心、等级伤害／半径、原18处地火及持续帧；MPQ掩码3不自然调用 |
| 172 GargoyleTrap | 原机关射线、MonSeq释放与轴向资格，复用已接入口 |
| 182 Bestow | GreaterMummy／Radament；支持目标资格及原公式，当前calc1／calc2空值导致0治疗 |
| 203 DesertTurret | firetower；环境控制器原三发火弹及间隔，复用已接入口 |
| 204 ArcaneTower | lightningspire；64方向lightningtowernova及两阶段次数／间隔 |
| 216 QueenDeath | Queen死亡回调／54行死亡序列，八道queenpoisoncloud；不是活体AI施放 |
| 327 Resurrect2 | GreaterMummy复活lUndead；Radament支持两类亡灵及普通难度重要尸体，动态碰撞、原复活模式及满生命，禁重复奖励 |
| 333 VampireMissile | vampire1；原FireHead弹体与来源生命恢复 |
| 352 SerpentCharge | ClawViper；A1伤害、原速度加成、额外范围、击退与0x70命中类型 |

普通A2、骷髅法师元素弹、投矛／投瓶从MissA1／A2准备；绿色瓶子按原八方向产生rancidgascloud及SubLoop时长，橙瓶当前没有有效Srv爆炸半径，不自造范围伤害。甲虫GH／死亡的buglightning与虫后死亡的queenpoisoncloud各为四个基方向×两条ChargedBolt路径；固有计时不覆盖精英电／冰／火词缀。

当前源码已接移动序列回绕、飞行／栖息／集群AI，技能声明与AI启用条件分开保留。逐身份实现、原版依据和缺口见[第二幕](../monsters/ACT2.md)，三难度完整战斗仍待对齐。

## 代码与归档

Mosquito（206）：`mosquito1`至4使用原SQ事件1，起手按双方GUID独立抽取循环预算；每次释放先按物理伤害的calc3回血，再结算自身随机流生成的物理、毒和法力／耐力消耗，离开近战范围终止吸取。序列回绕遵循原Param1和剩余帧计数，未把预算次数直接当作实际命中次数；本轮未专项实机核对。

Submerge（176）／Emerge（180）：青蛙恶魔的隐藏和现身声明。目标资格与地面碰撞分别更新；Submerge在原序列零帧暂停，不阻断AI定时；现身前沿原1C09空位恢复入口处理位置和地面碰撞。远程显示读取自己的原序列与S1图形，本轮未专项实机核对。

MonBow（339）／MonFireArrow（340）／MonExplodingArrow（342）：`cr_archer6`至8按Fn035的槽顺序选择，A1事件2分别释放cr_arrow6、firearrow和explodingarrow，技能ID／等级随原飞弹构造传入公共伤害与碰撞执行器；St4仅检查射击资格，不提前发射。三种箭未专项实机核对。

第五幕持矛罗格的Jab（10）读取自己的seq_crlancerjab及A1物理参数；MonPowerStrike（344）／MonIceSpear（347）沿公共St6起手分配、Do2事件结算，读取各自雷／冰伤和持续时间。Fn036按存在的技能槽依次抽取概率，冲近后清除强制攻击标记；本轮未专项实机核对。

[monster_actions_content.cpp](../../../src/hosting/monster_actions_content.cpp)及[act2_monster_skills.cpp](../../../src/hosting/act2_monster_skills.cpp)按当前MonStats槽、Skills、Missiles、MonSeq及难度准备规则；[skills/monster_actions.cpp](../../../src/server/systems/skills/monster_actions.cpp)持开始／释放队列，monsters执行复活／出生／移动，missiles／effects执行弹体和效果。AI只提交窄请求，不另建技能执行器。精英所有权见[精英](../monsters/ELITES.md)与[首领](../monsters/BOSSES.md)。

已有公共方向／路径计算及原Clt／Srv差异沿[技能COMMON](COMMON.md)和[怪物COMMON](../monsters/COMMON.md)维护。怪物调用职业技能时链接职业页，怪物专用程序才在本页登记算法；与物件、任务死亡演出、伙伴能力的分类见[目录](README.md)。

后续逐项补当前MPQ技能ID／原名、实际使用槽与AI条件、Clt／Srv程序、共享函数、实际执行、原版依据及明确缺口。当前表为代码执行家族摘要，不是全部非玩家Skills行清单；未归档行先列入[SPECIAL](SPECIAL.md)。第一幕类型及程序边界见[第一幕](../monsters/ACT1.md)，不以自研运行记录提升完成度。
