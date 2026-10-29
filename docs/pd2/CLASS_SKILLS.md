# 职业技能

七职业客户端程序的PD2差异与有明确条件的原生观察；权威技能规则见[技能与战斗](SKILLS.md)。 目标身份与证据口径见[版本与证据](BUILDS.md)。

<a id="职业技能亚马逊"></a>

## 职业技能：亚马逊

来源：[docs/gameplay/skills/AMAZON.md](../gameplay/skills/AMAZON.md)。

PD2远端使用当前MPQ的独立职业布局。本地原生PD2已观察Fire Arrow／Cold Arrow击杀、Multiple Shot目标生命下降、Poison Javelin根弹体14及毒云141；Power Strike的CltDo22生成原表692，不能用原版同名技能的弹体代替。该初始目标选择允许唯一敌人GUID回绕到本次目标，其他命中连锁仍排除当前单位；原版资源隔离宿主已核对Lightning Strike单目标232及后续99。上述事实不等于全部箭矛伤害、毒持续、穿透／连锁和装备组合完成，范围见[PD2技能边界](../gameplay/skills/README.md#pd2远端技能范围)。

<a id="职业技能女巫"></a>

## 职业技能：女巫

来源：[docs/gameplay/skills/SORCERESS.md](../gameplay/skills/SORCERESS.md)。

PD2的Blaze和Inferno是独立差异：当前Blaze状态未填原版setfunc3，RemoteScene在匹配配置按Skills的aura关联恢复移动留火；根弹体命中后的火焰按SHitCalc1决定寿命。Inferno的CltDo111不走原版24，Calc1／Calc3控制当前帧与宽度，使用已恢复的PD2视觉随机／整数扇形链。两者已在本地原生PD2服务观察到移动火焰与喷流；不代表原客户端逐像素、伤害／耗蓝及全部等级已认证。当前本地原生服务还核对了冷／火焰附魔、两种寒冰护甲、雷云风暴与能量护盾的原生状态；传送到指定坐标及返回，静态力场两次降低同一目标生命、霜之新星119及新星90命中／击杀，Hydra351／Lesser Hydra738的原生头部和火墙68／69、陨石103及后续弹体链均有事实。各伤害组合与PvP仍待覆盖。核心版本、RVA和剩余限制见[核心分析](../resources/PD2_CN_DLL_ANALYSIS.md#角色技能树与客户端程序)。

<a id="职业技能圣骑士"></a>

## 职业技能：圣骑士

来源：[docs/gameplay/skills/PALADIN.md](../gameplay/skills/PALADIN.md)。

PD2远端Holy Bolt的Calc1还读取extra_holybolts，客户端发射与描述共用本人已核实的装备／状态／被动属性投影，不能从缺少8位属性包推断为零。本地原生PD2服务已核对基础等级1的扣蓝与missile55、真实双手剑上的holysword状态、Sacrifice／Smite／Zeal／Vengeance击杀及Joust位移／伤害。Sacrifice共用原CltDo34血迹入口；本地原服已逐项观察20种光环的基础状态，双客户端组队核对Holy Light将队友生命150恢复至188；当前holylightdelay的CltHit66子体为FFFF，原程序直接返回成功，该无子体分支已接。有效子体的区域回调、额外弹数装备及完整治疗／伤害组合仍待覆盖。Charge虽有原服击杀事实，其替换CltDo112路径表现尚未恢复，不能据此认证位移显示。当前PD2范围见[技能目录](../gameplay/skills/README.md#pd2远端技能范围)，版本与入口见[核心分析](../resources/PD2_CN_DLL_ANALYSIS.md#角色技能树与客户端程序)。

<a id="职业技能死灵"></a>

## 职业技能：死灵

来源：[docs/gameplay/skills/NECROMANCER.md](../gameplay/skills/NECROMANCER.md)。

PD2本地原服已用真实匕首核对Poison Dagger的poison_strike状态、714毒弹及后续目标死亡；当前技能使用SrvDo164／CltDo102，不按原版名称猜测毒伤过程。Weaken／Attract已收到目标状态，Bone Prison观察到八段原骨墙单位，Bone Wall另观察到五段class344；金属短剑成功成为Iron Golem材料，Dark Pact消除已有Amplify Damage并造成目标死亡。Blood Warp已核对二十子格位移、客户端末端坐标和生命368→336。当前States的skel_mastery／golem_mastery带nosend，不能以网络未收到被动状态认定失效；69／79的召唤数值贡献尚未独立测量。完整数值、目标资格与组合仍待覆盖。

<a id="职业技能刺客"></a>

## 职业技能：刺客

来源：[docs/gameplay/skills/ASSASSIN.md](../gameplay/skills/ASSASSIN.md)。

PD2远端的Fists of Fire使用SrvDo170、PrgStack=1与CltPrg9／107／106；原服0xA3事件给出当前1–3阶段，聚气攻击本身会触发对应效果。当前MPQ的AuraLenCalc=par5、Param5=7500，即25Hz下配置5分钟；不能沿用原版的聚气观感判断。D2X按原服渐进事件与状态显示，不在本地改聚气层数或提前清除。本地原服已核对三层Tiger／Cobra／Claws of Thunder／Blades of Ice／Royal Strike、Dragon Flight位移／伤害，以及Dragon Talon消耗皇家三层后的击杀和Dragon Tail击杀；Fists of Fire已在受控本机PvP靶上收到1→2→3→1循环并显示第三阶段弹体链；此前普通区域两层聚气已核对7500帧到期清除。Claws of Thunder的落雷子体已补CltHit67并观察到原弹体；Blade Fury的CltHit6墙面反弹已观察到反向子体及首跳10帧期限。CltDo6按原版RVA413A0直接创建拥有者飞弹，不受ClientSend字段抑制，已核对PD2本地原服与原版资源下的持续发射；完整边界与多人／装备组合继续保留待覆盖。

<a id="职业技能德鲁伊"></a>

## 职业技能：德鲁伊

来源：[docs/gameplay/skills/DRUID.md](../gameplay/skills/DRUID.md)。

PD2远端差异：Arctic Blast的CltDo111使用独立喷流程序；Fire Claws／Firestorm的110／119按当前Calc2／Calc1生成ChargedBolt路径，单位种子递推及77帧上限来自目标核心。火风暴、极地风暴及火爪已在本地原生PD2服务观察实际弹体，火爪同时核对目标死亡。当前PD2 Hunger的TargetCorpse=1、SrvDo165，只允许熊形消耗真实尸体，原服已返回hunger状态及corpse_nodraw；不能沿用原版对活怪物的近战用法。乌鸦、三种藤蔓、三种灵体、灵狼／狂狼／熊已逐项收到当前MPQ对应的原生召唤单位；飓风装甲已收到151状态及132／133吸收值。狼形139、熊形140与再次施放回人形均已核对；1级Shape Shifting的命中50、最大生命加成2分别出现在两种变形状态中，熊形另含当前表基础10。狼形三项攻击、Maul命中／状态及人形Gust传送已有本地原服事实；Twister已观察近身目标死亡，Tornado另行观察原弹体478持续飞行；熊形Shock Wave的511及人形Molten Boulder的453／452也已观察，召唤物同时作战，未据此独立认定眩晕或伤害。毒传播、全部阶段／装备与多人组合仍待覆盖。版本及原入口见[核心分析](../resources/PD2_CN_DLL_ANALYSIS.md#角色技能树与客户端程序)，其余PD2缺口见[技能目录](../gameplay/skills/README.md#pd2远端技能范围)。

<a id="职业技能野蛮人"></a>

## 职业技能：野蛮人

来源：[docs/gameplay/skills/BARBARIAN.md](../gameplay/skills/BARBARIAN.md)。

PD2远端Double Throw走替换CltDo98，按两手真实Items.MissileType选择A／B／C及两次动画释放；Split Throw走118及原整数扇形，连锁次数与半径由当前公式／弹体输入准备。本人跳跃在服务器省略4C时消费最新skill_move状态结束飞行、进入落地动画；未确认预测沿既有15秒预算回收，新技能可替换失效预测。104中Find Potion／Find Item已接原38／40的尸体血迹，本地原服确认搜索后corpse_noselect及原血迹飞弹；原版模式的Find Potion也已核对。CltDo97已恢复本人经skill_move确认的飞行：按当前AuraRangeCalc准备落点、Calc2*41+2560取速度，原序列第5帧起飞，复用原整数路径步进及距离重力。移动状态提前撤出时仍完成已确认弧线，避免画面停在最终坐标之前。本地原服核对Leap／Leap Attack二十子格位移、落地及随后正常走路；其他玩家／怪物的97、全部碰撞及装备速度组合仍待覆盖。当前消费者已与零售43／44归到共用玩家飞行与图形积分路径，97保留独立Calc2*41+2560速度，不套用零售RunVelocity；该次共用调整已构建，行为仍待实测，旧实测不扩大为本次通过。依据与范围见[核心分析](../resources/PD2_CN_DLL_ANALYSIS.md#角色技能树与客户端程序)。

<a id="职业技能刺客渐进与陷阱表现"></a>

## 职业技能：刺客渐进与陷阱表现

来源：[docs/gameplay/skills/ASSASSIN.md](../gameplay/skills/ASSASSIN.md)。

PD2的A3渐进106／107已按原阶段接整数扇形／环形，覆盖火焰拳二／三阶段和眼镜蛇攻击一／三阶段；弹体及公式仍读当前MPQ，缺位置／公式／弹体时记录表现限制。107无目标时以已同步施法者为圆心，不把A3附加DWORD猜成坐标；106无目标仍缺原路径目标，不生成猜测方向。火焰拳延迟弹体的命中53、后续火球程序19以及眼镜蛇毒云程序4已有消费者，但未实机认证。Mind Blast只使用序号一致的蓄气状态快照；pvp_disable由原生map状态198提供，已接完整本人属性投影，本地原服观察到中心576及散射551。陷阱4C／4D施法包的等级用于当前技能的客户端等级，基础部分按Skills.maxlvl限制；Charged Bolt Sentry（261）已观察到PD2四枚495，原版模式回归仍为五枚。Inferno Sentry按本怪物MonStats技能槽选择动作，并按原NAMETOWORD进行不区分大小写的链接，修复Mon／mon名称差异导致的序列与喷流缺失；原版资源隔离宿主及本地原生PD2固定靶均已观察311／523，PD2另见524。原Clt77保留CltCalc1图形偏移及0x20标志。Lightning／Chain Lightning／Wake of Fire Sentry分别收到392／313／281及原弹体；PvE伤害、死亡守卫尸爆和全部组合仍待覆盖。双核心地址、83的原版回退条件及剩余限制见[客户端程序证据](../resources/PD2_CN_DLL_ANALYSIS.md#角色技能树与客户端程序)；全部阶段及子弹体仍未完成认证。原版斗篷的照明证据见[刺客专题](../gameplay/skills/ASSASSIN.md)。

<a id="职业技能骷髅触发通知"></a>

## 职业技能：骷髅触发通知

来源：[docs/gameplay/skills/NECROMANCER.md](../gameplay/skills/NECROMANCER.md)。

PD2骷髅战士攻击后的`Skeleton Splash`（当前Skills ID393、srvdofunc163、monanim=xx、无客户端程序／飞弹／音效／覆盖图）是触发效果，不是新的施法动作。已有CN收包记录中，骷髅先收到0x6C／action10攻击，约0.46–0.51秒后收到0x99／skill393，再约0.25–0.31秒收到0x6D待机。旧客户端副本把0x99覆盖为actionSkill并清mode，导致表现查找不存在的SK／xx动画、整只骷髅停止绘制直到待机。现仅0x4C／0x4D切换技能动作；0x99／0x9A保留当前模式、目标、朝向与动作时间，通知仍进入效果事件队列，不打断本人施法预测；无客户端表现的表行不请求动画。原版也走相同协议语义，不按PD2技能ID补帧或替换资源。

<a id="职业技能骷髅通知观察边界"></a>

## 职业技能：骷髅通知观察边界

来源：[docs/gameplay/skills/NECROMANCER.md](../gameplay/skills/NECROMANCER.md)。

本地原生PD2服务已捕获骷髅攻击后的0x99／393并核对动作与绘制保留。全部触发技能的ItemCheckStart、ItemCltEffect及独立效果时序仍待完整对齐，该结果不代表CN公网长期认证。

<a id="职业技能尸体搜索距离差异"></a>

## 职业技能：尸体搜索距离差异

来源：[docs/gameplay/skills/NECROMANCER.md](../gameplay/skills/NECROMANCER.md)。

当前PD2 CN ProjectDiablo.dll（MD5 `828ae42e54913ee016a751c8dcd4d48e`）补丁记录RVA49FBDC将D2Client RVA517EF的初值改为7，保留半径9；51CBA调用由3A1150→2EF990包装，排除157／159／166后回调原51650，死灵尸体技能继续走原搜索。客户端仅对匹配`pd2-cn-normal-20261020`配置采用严格小于7的门槛，不把PD2范围带入原版。

<a id="职业技能尸体选择观察边界"></a>

## 职业技能：尸体选择观察边界

来源：[docs/gameplay/skills/NECROMANCER.md](../gameplay/skills/NECROMANCER.md)。

本地原生PD2服务已用真实击杀尸体核对战士／法师／弓箭手、重生和尸爆，实际召唤数量与范围仍由远端权威裁定；Desecrate隐藏且带udead的尸体不据此放宽资格。

<a id="职业技能战吼观察边界"></a>

## 职业技能：战吼观察边界

来源：[docs/gameplay/skills/BARBARIAN.md](../gameplay/skills/BARBARIAN.md)。

本地原生PD2已核对Howl的terror56、Battle Cry的89状态及36／171减值；Taunt靠近目标后收到27状态及25／119各−5，先前目标已逃远的请求不算通过。完整AI行为、精英与玩家目标组合仍待核对。

<a id="职业技能陨石城镇寿命"></a>

## 职业技能：陨石城镇寿命

来源：[docs/gameplay/skills/SORCERESS.md](../gameplay/skills/SORCERESS.md)。

当前PD2的meteorfire／firesmallmeteor／firemediummeteor为12帧（约0.48秒），数值从MPQ准备。

<a id="职业技能召唤尸体选择"></a>

## 职业技能：召唤尸体选择

来源：[docs/gameplay/skills/NECROMANCER.md](../gameplay/skills/NECROMANCER.md)。

PD2骷髅、法师和弓箭手由各自表行进入同一SelectProc2，不写死原版技能ID。

零售新增Clt30／Clt32／Clt45／Clt46／Clt54／Clt57／Clt68与原Hit37／51客户端消费者仅在Original规则分支启用；当前PD2核心对应程序／不同字段来源未在此批核对，保持PD2不套用这些新零售回调。共同路径头坐标语义为DynamicPath.tTargetCoord，不能将PATH_GetFirstPoint误当队列头；此次不据此改写PD2专用程序。新增零售路径已构建，行为仍待实测。
