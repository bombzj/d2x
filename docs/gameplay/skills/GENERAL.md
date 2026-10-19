# 通用攻击与卷轴／书本技能

更新：2026-10-16。当前MPQ十项为0–5、217–220，均有自研执行入口；弓／弩是Attack的武器分支，不新增技能。本页负责这些原程序、当前实现与缺口，公共规范见[COMMON](COMMON.md)，完整分类见[技能目录](README.md)。其他物品授予／充能／触发与专用程序见[ITEM_SKILLS](ITEM_SKILLS.md)，不扩大本页十项范围。

## 逐项入口

| ID／技能 | 原程序与数据依据 | 当前实现 |
| --- | --- | --- |
| 0 Attack | SrvSt1／SrvDo1、CltSt1／CltDo1；SrcDam128，range=both | 空手／普通近战与弓弩统一进入 skills.weapon；公共武器六通道与命中计算，missiles／combat 执行。弓弩读取物品映射的 arrow／bolt，释放时消耗对应箭袋／弩矢 |
| 1 Kick | 隐藏通用程序；SrvSt2／SrvDo2、KK，SkillDesc.ListRow=-1；Objects.OperateFn5 | 各职业操作普通木桶时自动调用，保持左右键选择；KK使用原AnimData，破坏／掉落仍归objects／loot。单位目标按St02固定伤害及击退执行，当前MinDam空值编译为0；不使用刺客踢腿／靴伤公式 |
| 2 Throw | SrvSt65／SrvDo3、CltSt2／CltDo2；itypea1=thro | 选择当前主武器，必须真实可投掷；弹体和数量来自物品，TH释放时原子扣一件 |
| 3 Unsummon | SrvSt3／SrvDo4、CltSt3；PetType.unsummon | companions 检查本人归属、原PetType许可和当前存活单位；释放时复验，退役不产生敌人经验／掉落，原7A删除归属并保留死亡显示直到移除 |
| 4 Left Hand Throw | SrvSt65／SrvDo5、CltSt2／CltDo2；S4 | 同一投掷管线选择左手真实物品，保持左手动画；是否有此固有技能来自CharStats，不无条件开放给全部职业 |
| 5 Left Hand Swing | SrvSt1／SrvDo1、CltSt4／CltDo1；S3 | 同一普通攻击管线选择左手真实武器，保持左手动画、六通道及目标复验 |
| 217 Scroll of Identify | SrvDo113，Books.pSpell1／SpellIcon，isc | inventory 选择本人背包中的有效卷轴，原3F准备鉴定光标；原27确认有效未鉴定物品后一次提交鉴定与卷轴消耗 |
| 218 Book of Identify | 同一SrvDo113／pSpell1，ibk | 与卷轴共用鉴定事务，消费书本charges中的一页；空书不可施放 |
| 219 Scroll of Townportal | SrvDo113，Books.pSpell2，tsc | inventory 选择来源，travel 执行当前区域资格、落点、本人门户替换与卷轴扣费；沿现有原59／82等门户同步 |
| 220 Book of Townportal | 同一SrvDo113／pSpell2，tbk | 与卷轴共用travel门户事务，消费书本charges中的一页；书本保留，空书不可施放 |

Kick不是其他职业可学习或应展示在选择器中的踢腿技能；刺客的Dragon Talon／Dragon Tail／Dragon Flight属于职业技能。原CharStats给各职业隐藏Kick，原Objects.OperateFn5在破桶时使用它。服务端objects只调用skills的窄objectKick入口，复用KK时序；客户端依据同一原物件操作预测本人的KK，原4C通知其他可见玩家，不额外发送本人强制包。爆炸桶OperateFn7不套用普通桶规则。

服务器内容准备按原函数组合识别六项general技能；从技能栏施放四项物品技能走原SC时序，在释放帧由SrvDo113对应入口重新查找有效来源；背包直接使用仍走物品使用入口。四项物品技能由Books中的Completed、pSpell、物品代码、技能名称动态绑定。原表缺失或未知程序明确拒绝。CharStats准备职业固有技能；普通等级／基础技能点不代替固有资格。卷轴数量与书本页数在入场和库存事务后通过既有原技能数量投影更新，选择／绑定及施放均拒绝空来源。

## 武器与投掷物

`common_actions.hpp::commonAttackWeapon`迁用master的装备选择逻辑；`commonProjectileSkill`把物品弹体规则投影为同一施放规格，避免普通攻击恢复第二套伤害执行器。`weapon_damage`已有六通道、目标加成、物理转换、致命／双倍与显式随机；普通攻击和职业武器技能使用同一实现。

通用攻击的服务端释放tick取当前MPQ AnimData动作帧；客户端预测动作不证明服务端接受或命中。`loadSkillAnimations`按CharacterDefinition.appearance读取角色COF／AnimData，不使用技能树图标token；刺客ai／as、德鲁伊dz／dr不可混用。原依据为D2MOO D2Composit::COMPOSIT_GetCof使用PlrType类token，以及Units::UNITS_SetAnimActionFrame／UNITS_GetEventFrameInfo消费动画事件；全部职业／武器动作组合仍待原版对齐。

Weapons／ItemTypes／Missiles的原映射分别提供标枪、飞斧／飞刀、弓／弩和六种投掷药瓶；不依物品名字猜等级。普通投掷按真实武器计算，弓弩按shoots匹配活动组的箭袋。开始时检查可用弹药，释放时重新选择并复验同一物品／手位／武器类型及数量；最后一发药瓶删除物品，普通投掷武器保留零数量以供补充。最后一件删除后不再读取失效物品；扣费与背压的公共要求见COMMON。

药瓶遵循SrcDamage=0与原SrvHit2／3：火瓶采用原Missiles固定物理／火通道及范围；毒瓶生成原毒云子弹体并执行毒伤／期限。`rollPotionDamage`不继承装备元素、武器增强伤害或致命概率。地面目标不在沿途撞到单位时提前触发，地形或寿命结束时执行一次原命中程序。

`groundThrowFrames`是客户端和服务器共用的纯计算，依据MISSILES_CreateMissileFromParams的0x400距离标记，改变剩余帧而不改变原速度。客户端用原CltDo2的物品弹体映射和CltHit2／3显示，服务器持有数量、接触、范围伤害及子弹体状态。它不等于药瓶完整抛物线高度／阴影的原版认证。

## 原协议及客户端修复

通用动作的本人通知省略及ClientSend原则沿COMMON；普通箭矢／投掷由原Clt程序创建，原73仍用于合格同步。原3F固定8字节：cursor、source GUID、skill；取消为cursor FF、skill FFFF。服务端输出真实来源／技能，并在鉴定事务后清除目标光标，不发通用成功ACK。

公共客户端按原表修复：CltDo2不再排除药瓶，左手投掷按手位选择可见装备弹体；Unsummon目标按原7A本人归属及PetType.unsummon过滤；3F驱动鉴定准备视图。光标准备使用事件revision，支持Escape后再次选择同一来源、入局代次重置及来源物品迟到；界面只读取状态、提交原27，不自行消费或鉴定。原服和自研宿主走相同代码。普通桶另补本人KK预测；只读取真实物件身份、原OperateFn5和已发送的0x13交互，复用已有动作表现，不决定木桶破坏或掉落。

按住近战的客户端追击以当前待执行攻击、本地连续人物／目标位置和原近战范围为准，自行启动路径／决定起手，不等本人96或服务端移动确认。localUnitPosition读取同一单位本地路径的位置；首次指派／真实位置不连续时从原指派坐标开始，不能把稀疏位置样本当作连续追击目标。旧CastHandoff只抑制先前动作的稀疏原走路目标，不能抑制敌人逃出范围后的下一次攻击靠近；合成的本地靠近意图使用LocalCast自己的revision，不借最新世界／其他战斗请求的revision。起手距离使用MonStats2原SizeX与playerMeleeReach，保持moving增量及武器rangeadder，目标换格重算按floor(position+0.5)的原生子格，客户端与权威分别执行；Shift站定和已经开始的攻击动画仍沿原语义。依据为PlrMsg::Rcv0x06／09→sub_6FC836D0与UNITS_IsInMeleeRange：范围外保存单位目标并改RUN，范围内才改技能动作。

客户端起手与自动靠近共用`castMeleeRange`，只对单位目标的`h2h`或持近战武器的`both`检查近战距离；`rng`和`none`直接在当前位置起手，点目标与Shift站定不创建追击路径。当前MPQ的Throw／Left Hand Throw／Double Throw／Poison Javelin均为`rng`，不因手持标枪或飞刀而走近目标。依据为D2MOO `D2Common_11016`对`both`的弓弩分支及PlrMsg `sub_6FC836D0`的范围分类；服务端`skills::System::weaponCast`已排除投掷靠近并在起手后停止移动。

## 原版依据

卷轴／书本的公共客户端资格由skill_eligibility统一计算，MPQ的scroll标记与SrvDo113识别物品技能；CharStats固有资格不代替原0x22数量。选择器、热键和施放读取同一已知数量，零数量／尚未知不授权，数量不写入有效等级，也不与装备充能混用。依据D2Common D2Skills.cpp的SKILLS_GetUseState（scroll且nQuantity<=0返回NOQUANTITY）及ItemMode.cpp的数量维护／0x22发送。原表四项range=none，SrvDo113调用物品程序时使用本人位置；公共RemoteCombat不获取悬停单位，施放前将点目标归一本人坐标，再发送原0x0C／0x0F。这样鼠标位于地图外沿时不会因无关目标边界拒绝自身物品技能，原服与自研宿主代码一致；完整资格与落点组合仍待原版对照。

- master：`session/session_skills.cpp`的BasicSkillAction、`units/actions.cpp::selectAttackWeapon`、`combat/attacking.cpp`、`combat/physical_projectiles.cpp`及`items/books.cpp`。
- 本地D2MOO：D2Game `SKILLS/Skills.cpp`的SrvSt01／02／03／65、SrvDo001／002／003／004／005；`SKILLS/SkillItem.cpp`的SrvDo113及pSpell01／02；`ITEMS/ItemMode.cpp`的书本数量处理；`MISSILES/Missiles.cpp`的创建标记、`MissMode.cpp`的SrvHit02／03；`PLAYER/PlayerPets.cpp`的归属删除及退役；`OBJECTS/ObjMode.cpp::OBJECTS_OperateFunction05_Barrel`及PlrModes的KK动作／PlrMsg的本人通知省略；`GAME/SCmd.cpp`的3F编码。原表参数仍以当前MPQ为准。

## 当前边界

- 人形／狼熊普通武器动作和普通Kick使用当前MPQ完整AnimData标记，经实例帧队列释放／结束；玩家普通近战使用原生联系与单位种子。ExecuteEvents负责吸取、扣血及状态，随后是耐久、近战事件、反伤和末尾模式；普通攻击未命中仍填写伤害。可达专用武器、连击、怪物／佣兵／伙伴近战均已迁入，旧武器伤害队列和随机副本已删除，详见[内核对照](../../modules/SERVER_SYSTEMS.md#旧近战链与随机迁移)；完整组合仍待对齐。
- 普通武器管线已接lifeLeech／manaLeech／crushingBlow／openWounds；来源、难度／目标系数、特殊技能资格及提交顺序见[武器命中效果](../combat/WEAPON_EFFECTS.md)。完整双持组合、PvP、未实现职业及专用触发仍有缺口。
- 普通近战武器／防具耐久、Impale磨损、投掷补充／自修复及充能扣费／维修已经接入；具体规则和装备六类事件框架由[库存](../../modules/INVENTORY.md)维护，不能笼统写成未实现。专用触发程序及ItemCltEffect仍有明确暂缓项。
- Unsummon只对当前已实现且PetType允许的本人召唤物有效，不补齐其他职业召唤或完整佣兵；同行者与雇佣范围见[佣兵](../characters/HIRELINGS.md)。普通门户资格由travel负责，任务／队友门户范围由[物件](../world/OBJECTS.md)和[任务系统](../quests/SYSTEM.md)维护。
- 药瓶地面寿命与伤害已接，完整抛物线高度／阴影仍有缺口。普通桶KK、Left Hand Throw／Swing的完整组合仍待核对；左手弓／相反手弹药规则见[装备](../items/EQUIPMENT.md#手位与双手剑)，与两项左手固有技能分别记录。

普通桶KK（包括城镇自动动作）、左手两项、弩及其他投掷武器、全部药瓶等级、空书／取消／背压／多人、原服和Linux的完整边界仍待核对，不以代表路径宣称全部十项已对齐原版。

技能选择、热键、弹药和书页沿已有D2S字段；固有资格和可用物品技能数量重新推导，动作／弹体／宠物／门户／目标光标不保存。编码及校验边界仅维护在[存档](../../modules/SAVES.md)。
