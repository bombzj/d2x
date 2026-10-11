# 第二幕怪物执行边界

更新：2026-10-12。本批完成第二幕逐身份AI、动画、普通攻击与技能接线；源码完成与有限冒烟分别登记。当前MPQ的第二幕Levels三难度候选、Act 2 MonPreset及SuperUniques固定记录，再递归spawn／minion，得到74个enabled、killable、非中立MonStats身份、33类AI。技能槽去重30项，逐项关系见[怪物技能](../skills/MONSTERS.md#第二幕技能逐项台账)。规范见[COMMON](COMMON.md)。

## 三难度重新核对

2026-10-12重建当前MPQ各难度集合，普通／噩梦／地狱各74类，集合相同，全部有真实实现入口。33类AI逐难度读取aip、aidist、aidel；数值、技能等级、人数倍率、冷／冻结、Drain、ToBlock、MonProp、精英和TC分别准备。技能加成为0／3／7；Duriel Holy Freeze使用aip1的5／5／6，不再叠加技能加成。等级规则沿[第一幕三难度表](ACT1.md#三难度重新核对)。逐身份矩阵位于忽略目录`artifacts/act12-reaudit-20261012`，全集见下表。

| 已补偏差 | 当前执行 |
| --- | --- |
| GreaterMummy目标／走位 | A1／A2用主目标，复活／Bestow用合格亡灵，UnHolyBolt独立选择可射击目标，低速接近取原steps−1距离 |
| SandMaggot目标覆盖 | A1用主目标，A2毒液用可射击目标；上／下钻分别保留主目标存在条件与射击回调距离 |
| PantherJavelin目标覆盖 | 距离判断用射击回调，逃离／接近相对主目标，投掷指向可射击目标 |
| Summoner目标／游荡 | 元素偏好、新星距离、削弱、火墙用主目标，冰尖柱／火球用可射击目标；不施法时在自身附近游荡4格 |
| 鸟巢／石棺出生 | 初始间隔从AI初始化计，区分成功施放次数与实际子怪数；鸟巢堵塞仍消耗间隔，石棺堵塞不消耗间隔／次数；按各自上限耗尽并NOTC |
| 绕行／持续追击 | 原方向偏移±2、逐格碰撞与路程上限；保持已接受移动意图，目标移动时重规划，不截成几格短路 |

修正继续输出原移动／动作／技能包；自有客户端已有同一消费者，不增加私有协议或客户端AI。MPQ未开启的Swarm Move／VampireMeteor不启用，Bestow空公式不补造治疗量。

跨区域完整活动房间、Vision缓存／强制察觉、全动态占位和全随机顺序仍有适配；投瓶S5血迹缺图继续略过装饰层。自然通关、全技能／词缀／难度组合和原版逐帧画面未认证，证据不足不补造规则或图片。

本次最小运行范围统一见[三难度核对后的最小冒烟](ACT1.md#三难度核对后的最小冒烟)，本页其余运行表为对应历史版本的证据。

## 当前身份与程序入口

本页逐身份登记AI、动画、普通攻击及所需技能接入，包含出生／复活、移动技能、固有效果与固定首领。已补入Baboon逃跑／再生、Vulture起降／目标资格、BatDemon栖息／离巢、Panther集合、SandRaider受伤伙伴／颜色、ClawViper颜色清理及移动序列续行。实现后统一构建、打包并复用现有游戏／调试管道完成有限冒烟，运行结果见本页末尾。

以下是当前MPQ身份，不按族名推断其他幕变体已经实现。共享家族沿用第一幕执行，新增家族的技能值由hosting准备；DesertTurret及GargoyleTrap保留各自机关入口。

| AI | 当前第二幕身份 | 本批范围 |
| --- | --- | --- |
| SandRaider | sandraider1、2、3 | Fire Hit、S1物理／火伤、颜色蓄力及受伤伙伴集合 |
| SandMaggot | sandmaggot1、2、3 | 上／下钻、当前生命百分比恢复、产卵、A2毒液 |
| SandMaggotQueen | maggotqueen1 | NOXP子怪、出生S1、QueenDeath序列与毒云回调 |
| MaggotEgg | maggotegg1、2、3 | 按等级孵化，随后终止自身 |
| MaggotLarva | maggotbaby1、2、3 | 普通攻击，S1出生与NOXP |
| ClawViper | clawviper2、3 | SerpentCharge、击退及A1／A2 |
| SandLeaper | sandleaper1、2、3 | 起跳前A1、越过目标落点、Run速度及skill_move |
| Swarm | swarm1、2 | 普通攻击；Swarm Move有入口，Fn019不主动施放 |
| Scarab | scarab1、2、3、4 | S1 Jab两击，GH／死亡固有八道闪电 |
| GreaterMummy | unraveler1、2、3、radament | 复活、Bestow、UnHolyBolt；Radament扩大亡灵资格 |
| Mummy | mummy1、2、3、4 | A1／A2及MPQ元素附伤 |
| PantherWoman | pantherwoman1、2、3、4 | 普通攻击、同族集合，无主动Skills槽 |
| PantherJavelin | slinger1、2、4、5、6 | 远程攻击、同族集合、绿瓶毒云与橙瓶碰撞 |
| Baboon | baboon1 | A1／A2选择、逃跑与阶段再生，无主动Skills槽 |
| Vulture | vulture1、2 | 普通攻击、空中绕行、伤员观察及安全落地，无主动Skills槽 |
| BatDemon | batdemon1 | S3入巢／S4栖息／S2离巢、再生与脱离，无主动Skills槽 |
| PinHead | blunderbore1、2 | A2伤害的Smite及眩晕 |
| Summoner | summoner | 冰尖柱、霜之新星、火球、火墙、第5槽削弱 |
| Duriel | duriel | Charge、三击Jab、Smite及AI等级的Holy Freeze |
| Sarcophagus | sarcophagus | Nest、数量上限及耗尽NOTC |
| ArcaneTower | lightningspire | 环形闪电、连续A1闪电及两阶段间隔 |
| Skeleton | skeleton2、3、4、5 | 共享普通攻击／复活模式 |
| SkeletonBow | sk_archer4、5 | 共享箭矢入口 |
| SkeletonMage | skmage_pois3、4；skmage_cold4；skmage_fire3、4；skmage_ltng3、4 | 共享组件与元素弹入口 |
| Zombie | zombie5 | 共享普通攻击 |
| Brute | brute4 | 共享A1／A2 |
| Goatman | goatman4 | 共享普通攻击 |
| Wraith | wraith3、4 | 共享普通攻击与碰撞规则 |
| BloodHawk | foulcrow3 | 共享攻击／飞行入口 |
| FoulCrowNest | crownest3 | 共享Nest序列与NOXP／NOTC子怪 |
| Vampire | vampire1 | 四槽准备；MPQ掩码3自然开启火球／FireHead和火墙，未开启陨石 |
| GargoyleTrap | gargoyletrap | 共享原序列、射线及轴向资格 |
| DesertTurret | firetower | 环境控制器三发原火弹 |

MonPreset固定身份包括Radament、Bloodwitch the Wild、Fangskin、Beetleburst、Leatherarm、Coldworm the Burrower、Fire Eye、Dark Elder及Ancient Kaa the Soulless，保留实际SuperUnique及MonStats。Duriel、Summoner任务身份与战斗规则分开持有。不可击杀的横／竖陷阱控制器由环境模块负责，不加入74个身份；飞行弯刀由物件产生，不属于上述候选或主动Skills槽。

## 技能、生命周期与原资源

`hosting/act2_monster_skills.cpp`按MonStats最多8槽、Skills、Missiles、MonSeq、AnimData及难度加成准备纯值。MonSeq显示帧可倒放／重复、混用A1／A2；释放时钟按序列行累计，Jab保留多次命中事件。技能使用自己的伤害／命中快照，不把SQ当普通A1。SC维持施法时钟，移动技能另持目的地与中断代次。Charge使用Run基速、最低50%的原速度修正，跟踪目标位置，结束移动后复验额外近战范围及邻近新目标；接近时回落当前怪物自己的A2。原skill_move沿状态事实和快照同步，0x16位置更新保留施放身份，显示随移动阶段回绕／切入攻击段。

`monsters/act2_skills.cpp`负责钻地、移动、出生及木乃伊支持目标；`skills/monster_actions.cpp`持释放队列；`missiles/monster.cpp`持冰尖柱爆炸／冻结、延迟陨石／地火及毒瓶子弹体。甲虫／虫后固有回调与精英词缀分开计时，本身光环与精英光环各自执行。客户端消费真实MonSeq、钻地末帧、甲虫闪电及虫后死亡毒云，不判伤害或生成战斗子怪。Vulture空中使用无碰撞移动、不可攻击／选择，S1沿原移动包持续显示，落地S2复验原碰撞及动态占位。BatDemon的S3／S4／S2使用各自AnimData时钟与原26／28动作包。蓝／红及其他颜色状态按States.colorpri选择当前幕PL2的colorshift，不自造RGB替身。

虫卵与石棺虽有MonStats2.inert，仍按enabled／killable／Align及自己的AI准入；inert不作为这两类敌怪的拒绝条件。ArcaneTower保留原表1000毒抗，抗性校验采用原有符号16位字段范围；其MissA1的CelFile=null使用原CltDo8程序，不要求不存在的DCC。MaggotEgg的原calc1先按公式字段去除外围引号，再核对及按原等级求值；出生先探原点，再按原搜索参数1–4展开3／6／9／12格周界，每次完整失败才提高参数，追加调用最多2×目标数量。冲锋的回绕／落地区间只作用于完整MonSeq，内部COF仍使用全部原帧，MonSeq.dir不作为拒绝条件。豹族投矛手的1HT DT／DD COF引用S5 bld，但当前MPQ只含该组件A1图；保留原身体并略过缺失的血迹装饰层，不借用其他身体或自制像素。

沙虫／虫后子怪只禁经验，保留MPQ掉落资格；Nest和复活同时禁重复经验／掉落。Bestow当前calc1／calc2均为空，治疗量为0，不自造比例。SkeletonRaise是复活显示声明，不添加AI主动施放。Swarm Move及VampireMeteor保留声明与原AI开启条件，不因有槽擅自启用。

依据为当前MPQ，以及本地D2MOO AiThink Fn008／011／014／015／016／017／018／020／021／022／023／028／029／038／040／044／045／053／066／093／095、SkillMonst、SkillAma、SkillPal、SkillBar、SkillSor、MissMode、MonsterUnique。参考代码不进入源码产物。

## 交付与未验证边界

本批D2S v96；临时技能、钻地、飞行、栖息、颜色、再生加成、移动、召唤、复活和效果时钟不写盘。本节下述2026-10-11历史批次已完成Windows Release构建、客户端／独立服务端打包及有限冒烟；2026-10-12两幕重新核对的包身份与最小运行结果见[第一幕核对证据](ACT1.md#三难度核对后的最小冒烟)，后续当前包由[运行包记录](../../development/BUILD_AND_RUN.md#运行包记录)维护。历史结果不认证后续源码。

全部74身份使用自己的当前MPQ身份、组件、AI参数、普通动作及声明槽；共享家族继续调用第一幕程序，新增分支见上表和源码入口。所有三难度数值与技能等级从当前表准备，未人为扩展未启用技能。有限管理准备与单机观察不能认证三难度自然出生、所有技能分支／精英组合、全部碰撞／背压／中断、原版逐帧随机、联网及自然通关。任务死亡链历史管理验证只维护在[第二幕任务](../quests/ACT2.md#有限运行证据)。

## 有限运行证据

2026-10-11使用当前MPQ、隔离角色及本批Windows Release客户端，在原第二幕地图执行现有管理准备和单机权威AI推进；没有新建测试脚本、用例或专用程序。管理出生／伤害／升级用于准备条件，技能释放与阶段变化从权威快照、事件和原资源场景读取。74身份及9个固定SuperUnique在最终二进制上重新逐项核对，均保持真实MonStats／SuperUnique身份，场景不可用计数为0，没有沉沦魔替身。

| 范围 | 实际观察 |
| --- | --- |
| 钻地／飞行 | 沙虫下钻后拒绝伤害、上钻并按当前生命恢复；秃鹫起飞／移动／落地，空中拒绝伤害及攻击选择，显示原S1／S2 |
| 栖息／出生 | BatDemon受伤逃离后进入S3／S4并持再生加成；石棺生出mummy1、虫后生出sandmaggot1，子怪原身份及场景可用 |
| 虫卵 | 最终包按原calc1及出生搜索孵出两只maggotbaby1，随后耗尽自身；两只子怪场景可用 |
| 固有死亡／复活 | 虫后死亡生成八道原queenpoisoncloud；GreaterMummy自然使用Resurrect2复活skeleton2，保留已完成奖励标记 |
| 召唤者 | 自然AI攻击事件观察到170／47／55／44／72，即火墙、火球、冰尖柱、霜之新星、削弱；场景不可用计数为0 |
| 保存重入 | 隔离角色正常保存、退出并重启载入；等级91、经验1800000131、生命730、四项基础属性、基础技能及剩余属性／技能点一致，进入第二幕城镇且无角色恢复问题 |
| 独立服务端 | 最终包加载并显示Usage，参数入口按现有契约退出2；未启动PvPGN后端会话 |

本地逐身份CSV、代表快照及截图、构建和启动日志保留在忽略目录`artifacts/act2-complete-20261011/`，不进入源码或交付资源。最终客户端标准错误为空；场景仍提示既有wraith_attack_1原声音中断淡出限制。豹族投矛手缺失的S5死亡血迹装饰按上文保留资源限制，身体仍使用原MPQ；场景可用计数不能认证该缺失装饰或所有动作的逐像素完整性。本批未覆盖完整三难度自然出生／通关、所有技能分支及多人／跨机器／Linux运行。
