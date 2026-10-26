# PD2技能与战斗差异

本页维护当前目标的职业技能、客户端程序及服务端接触／状态规则。版本、地址口径和证据要求见[版本与证据](BUILDS.md)；装备属性、触发与命中效果归[物品](ITEMS.md)，实体身份和伙伴生命周期归[怪物](MONSTERS.md)。以下“已接入”只说明代码执行路径，不代表所有数值或多人组合一致。

## 当前实现入口

较大差异位于`src/content/pd2/{skill_data,aura_data,curse_data,summon_data,corpse_data}.cpp`、`src/server/systems/{skills,combat,missiles,effects}/pd2`及`src/gameplay/effects/pd2`；德鲁伊／刺客复用各自领域执行器并按固定规则选择分支。参数来自当前MPQ，内核不读取MPQ。PD2普通飞弹接触同样经过公共`missiles/motion::collisionContacts`，单位位置到原生子格的转换与动态占位保持一致，规则及未验证边界见[公共飞弹接触](../modules/SERVER_SYSTEMS.md#飞弹数值与接触收尾源码)；这不是PD2专属尺寸或命中范围改动。

| 职业／机制 | 当前执行范围及入口 |
| --- | --- |
| 女巫 | 当前喷流、Blaze移动留火、传送附加状态、Static Field减抗、Combustion、Meteor／Blizzard、强化范围、Hydra／Lesser Hydra独立召唤定义；投射物按当前SrvHit／SrvDmg推进 |
| 亚马逊 | 当前弓／投掷／标枪程序、Exploding Arrow延迟扇形、Lightning Bolt／Fury、Strafe及近战序列；Valkyrie／Decoy读取各自MonStats与隐藏攻击技能 |
| 死灵 | 当前诅咒槽数、Curse Mastery、Dark Pact、Blood Warp、Desecrate、Poison Strike、尸爆与骨墙／骨牢；骷髅、法师、弓手及四类石魔读取当前身份、MonProp、PetType、隐藏技能和数量／生命公式 |
| 圣骑士 | Smite盾牌及Holy Shield伤害、额外Crushing Blow、Joust、Charge移动与临时溅射状态、Holy Light友敌分流、Holy Sword装备门槛、Vengeance延迟传播、Sacrifice反伤与死亡爆炸；光环按当前原表准备 |
| 野蛮人 | Bash主命中清除击退及条件二次无伤害击退、当前Concentrate／Berserk／Frenzy、Double Swing／Double Throw、Split Throw、Leap／Leap Attack、尸体搜索与战吼 |
| 德鲁伊 | 当前变形、Fire Claws／Firestorm、Gust落点范围、Hunger吞尸、Raven每次多召与隐藏攻击、藤蔓／狼／熊／灵体及当前召唤属性 |
| 刺客 | 当前蓄气阶段、元素环／扇形与终结释放、踢击、Dragon Flight、Mind Blast渐进伤害／状态、Blade Fury碰撞与反射、陷阱及影子隐藏技能 |
| 接触与触发 | Do163以主目标为中心并排除主目标；按指定技能Calc取ED，Smite用盾牌伤害、踢击复制已分配伤害；按武器类型读取参数化溅射／Crushing Blow属性；Open Wounds使用PD2伤害、期限与三层上限，层数和期限只在运行态保存 |

### 飞弹命中间隔的运行态事件

当前核心RVA `0x3A6E10／0x3A6EF0`区分玩家与非玩家来源：非玩家共用命中门槛，玩家通常按拥有者／飞弹类区分；标志0x80000另去掉玩家拥有者维度。`src/server/systems/missiles/pd2/hit_delay.cpp`准备对应命中键，`combat/missile_contact.cpp`按实际NextDelay在接收者登记保护，`effects/pd2/hit_delay.cpp`检查并发布期限。`FrameEventType::Pd2MissileHitDelay`是D2X运行态事件15，由`runtime/frame_event_dispatch.cpp`在到期时消费；它不属于原版0–14定时器编号，也不是新增网络包或存档字段。公共`gameplay/events/frame_queue.cpp`的直接插入与准备插入均允许这个显式扩展，仍拒绝超出当前枚举的值，不能沿旧的`>=15`边界拒绝已经接入的PD2命中事件。

## 角色技能树与客户端程序

ProjectDiablo核心的技能CltDo／渐进回调及喷流程序同时用于`pd2-local`与CN连接；`src/presentation/remote/remote_scene_effects.cpp`按当前PD2规则分派，不能只匹配CN协议标识。CN专属网络扩展仍由协议层选择，不作为核心技能表现的开关。

普通`anim=SC`施法在完整动作结束前的移动输入门槛与原版共用，原DLL依据、消费者及序列边界见[客户端普通施法与移动输入](../gameplay/skills/COMMON.md#客户端普通施法与移动输入)。不另加PD2职业／技能ID白名单，也不把允许受击打断等同于允许移动取消待释放的Clt飞弹。

骨甲setfunc6的分阶段浮现、按吸收池减少骨片、Overlay定点帧速率及骨墙出生朝向由公共客户端路径消费当前MPQ；当前PD2骨甲沿用相同状态回调与资源名称。原1.13c DLL证据、代码入口及随机流／逐帧核对边界见[死灵依据与入口](../gameplay/skills/NECROMANCER.md#依据与入口)，不作为PD2独有技能重写。

固定原生D2Client样本（SHA-256 `dd8bc6025de921216a97c17f97cd1a50fbb85926e838ec60e13451448836d906`）公共飞弹推进RVA B18F7–B1959读取Missiles字节字段+0x12C（CltSrcTown）：正常递减CurrentFrame后，拥有者在城镇时将剩余帧封顶到该值；非LoopAnim且AnimLen大于上限时同步设置动画帧为差值左移8。原生找不到拥有者也进入封顶分支；拥有者存在但房间为空则跳过。D2X目前对已观察到的拥有者／房间应用封顶，未知身份／房间不据观察者位置猜测；Meteor静态落地图只限制寿命。当前PD2原表三种Meteor地面火CltSrcTown=12，而Town与服务端SrcTown属于另外的裁决条件；进入城镇不应触发所有客户端效果的清空。上述依据为静态调用链，完整实机表现仍待核对。

已有配套TXT／BIN对照记录：Skills为610条／步长0x23C，SkillDesc为262条／步长0x120；该记录未单列资源摘要，资源更新后仍须按[版本口径](BUILDS.md#目标版本)复核，不能仅凭核心摘要沿用。职业技能的 ID、描述及三项前置技能链接、maxlvl、reqlevel、四项属性需求、CltSt／CltDo 及描述的页／行／列／ListRow／ListPool／IconCel 与 BIN 对应字段一致。前置链接位于0x17E／0x180／0x182，为有符号 WORD；180条非空引用均对应，空引用为-1。Skills 偏移分别为 0、0x194、0x12C、0x174–0x17C、0xF2／0xF4；SkillDesc 的链接序号为 WORD 0，六项布局为字节 2–7。ListRow 的 -1 对应 0xFF。同时对上职业技能的法力字段（0x184／0x186／0x188／0x18A／0x18C）、HitShift（0x1A4）与 EMin／EMax（0x1E0／0x1E4），保留起手／基础／递增法力的有符号 WORD。运行时在 RemoteDisplay 导入前执行相同字段校验；不据此声称 TXT 公式与编译表达式已经全部逐条对齐。女巫三系截图及两个新增技能的英文 TBL／提示对应见[技能描述](SKILL_DISPLAY.md#技能显示技能树与资源覆盖)。

按页／行／列有效且 maxlvl 大于零统计，可见节点为 ama 29、bar 29、ass 31、dru 31、nec／pal／sor 各33。Avoid、Sword／Mace／Spear Mastery、Shattering Arrow 和 Blade Dance 为有等级的隐藏记录。原 DC6 图标仍按当前包读取，布局与技能参数不硬编码为原版30节点。

以下 RVA 的旧版／更新版分别对应 SHA-256 `9c2b4d4627112d7f14ab959e71c56e947b7ceeb6f16424a637364f614d39122f`／`17d5fb1b049d8076226d27295487b49c8b5f2629553c193a01aa1e3d8afaba57`。旧版表写入入口为核心 0x3681A0，目标 D2Client CltDo 表 RVA 0xDEA48，槽位按编号乘4定位；见[程序表](../resources/pd2-core/PROGRAM_TABLES.md)。旧版／更新版均以对应指令核对，不执行核心DLL。

| CltDo | 旧版／更新版 RVA | 已恢复的输入与显示行为 |
| --- | --- | --- |
| 8 | 0x3A65C0／0x3A8A80 | Mind Blast 在目标处生成 CltMissileA，子弹体取 B；基础半径 AuraRangeCalc，加本单位 AuraState 列表中 `max(AuraStat1,1)-1` 乘 Calc1。缺少状态／属性事实时暂缓，不借累计属性猜充能 |
| 83 | 更新版0x3A8BE0 | 仅Skills.CltDo83走专用：单位位置中心、1113固定场地分支、路径种子重置及原40F00螺旋环；渐进83继续原版委托 |
| 94 | 更新版0x3A8CF0 | DoomKnightMissile335从SrvMissileA加怪物S3组件值；968／969／1106／1107分别固定偏移0／1／2／3。读取原appearance位流的组件10，缺事实不猜元素；保留非零目标坐标与100子格距离门槛。消费者为remote_scene_pd2／effects |
| 98 | 0x3A6B20／0x3A8FE0 | Double Throw按两手真实武器的Items.MissileType选择A／B／C，沿序列15两次释放；Calc2为连锁次数，AuraRangeCalc为半径。Common10321读取基础物品+0xFA，不是ItemTypes编号 |
| 100 | 0x3A6C30／0x3A90F0 | Combustion 以本人为圆心，A 为弹体、Calc2 为环形数量。堆栈副本中的字段对应 Skills +0x13C，不能误读为 Calc1 |
| 101 | 0x3A6CE0／0x3A91A0 | Desecrate 在单位目标或瞄准位置以 A 请求8条方向；B、C 均有效时依次在同处生成两者 |
| 102 | 0x3A6E10／0x3A92D0 | Poison Dagger 必须有单位目标，在该位置以 A 请求6条方向 |
| 103 | 0x3A6EA0／0x3A9360 | 空 CltDo 返回成功；不从这个入口另造光环弹体，继续消费服务器状态 |
| 105 | 0x3A6F30／0x3A93F0 | 在施法者位置创建 Skills.SrvMissileA（+0x48）；不误用CltMissileA |
| 109 | 更新版0x3A9710 | 先调用原CltDo25生成毒环，再以技能159、原等级调用CltDo46暂停MonSeq；当前消费者为PoisonEggSpawn453，双端链见下节 |
| 110 | 0x3A7280／0x3A9740 | Fire Claws要求真实单位目标，以A生成Calc2条ChargedBolt路径 |
| 111 | 0x3A7390／0x3A9850 | Inferno／Arctic Blast读取Calc1帧数与Calc3宽度，按施法者视觉随机消费40%分支及有符号余数；8020只覆盖TotalFrames，随后设置CurrentFrame，未置04时不应用创建结构的速度字段 |
| 113 | 0x3A7890／0x3A9D50 | CltMissileA 与 CltCalc1（+0x114）形成本人中心环，数量非正时不生成 |
| 116 | 0x3A7E60／0x3AA320 | 必须有真实单位目标及路径；正ID的CltMissile优先，否则取A，目标指针与坐标传入创建链 |
| 117 | 0x3A7F60／0x3AA420 | Lightning Bolt／Fury 取正 ID 的 CltMissile，空缺时取 A，经原 A1540 目标发射链；保留非零目标坐标及100子格距离门槛 |
| 118 | 0x3A7FC0／0x3AA480 | Split Throw按真实武器MissileType选择弹体及glaive／pilum相邻变体，Calc1决定原整数扇形数量，10820含激活帧1 |
| 119 | 0x3A8220／0x3AA6E0 | Firestorm以A生成Calc1条ChargedBolt路径，与110共用36B9A0准备链 |

环形 helper 旧版 0x369AF0／更新版 0x36BBC0 使用64项原整数方向，数量为0或大于64时取64，否则步进为整数 `64/count`，不是浮点均分圆周。因此请求6实际生成7条。数量、弹体与半径继续来自当前 MPQ／公式；显示公式走公共只读目录，不准备自研宿主的 PD2 规则。以上发射入口尚未完成全部施放／像素验收，弹体自身的 CltDo／CltHit 仍可能未支持，不能将发射入口接入等同于完整技能完成。

当前MPQ的直接CltDo调用者须与程序表区分：83的新增皇家火分支用于568／574；109用于453PoisonEggSpawn；114用于575LucionDash，归特殊首领范围。108／115目前没有Skills直接引用，不据此臆造普通职业入口，也不排除间接程序调用。83的零售渐进委托仍必须保留。普通97已接本人、非本人及怪物序列；112已补地面目标并保留非本人真实属性门槛；94的LobSkill分支是未覆盖边界，不能把本人Leap／Charge预测或线性弹体接入推广为全部客户端组合完成。

当前Clt94唯一直接调用者为335DoomKnightMissile，lob为空。当前3A8CF0在Skills+4的bit1分流：置位委托39F1D0抛物线创建，否则委托39F1B0；现有普通发射分支对应后者，不能将未覆盖的LobSkill写成当前335缺必需弹道。配套Client A1540的坐标距离门槛比较平方距离10000，沿用现有非零目标坐标与平方距离检查。材料为`town-queries/gap-clt94`，其他单位、隐式调用和像素表现仍未认证。

隐藏佣兵技能的客户端输入按原客户端自身副本求值：第四幕佣兵Bone Spear84的CltDo17读取Calc1中的extra_bonespears，该扩展属性没有原版怪物构造器初值，使用零初始化加本单位已收到的MonProp／状态列表。服务端0xAC现按原SCmd发送MonProp基础列表的前16条再筛Send Other；客户端独立解码尾部，未知／损坏列表仍拒绝求值，不借用主人或隐藏服务端装备。原生未发出的材料装备值不能当作他人可见属性；服务端弹数仍由实际佣兵公式独立结算。

### 皇家火CltDo83

当前`3A8BE0`只在Skills.CltDo为83时执行专用分支，否则委托原回调；不能将渐进83一起替换。`37E710`更新单位+CC的路径种子，再将其写到单位+20的随机低DWORD，高DWORD不变。`37BB50`取的是施法者自身路径坐标：原调用ECX=EDI，不能据失真伪代码改为目标坐标。只有当前class1113／LucionControl将中心替换为`(seed+13)%40+0x620F,(seed+19)%40+0x17CD`，按uint32回绕和无符号余数处理；这是DLL明示的特定场地分支。

发射用CltMissileA及Calc1，`3A25C0`经构造器213320、getter319FA0及表4A76A4委托配套D2Client RVA40F00；局部SEED以该路径种子及高字666初始化。按64项原整数方向、Calc1作为下标步长逐枚推进局部随机，原40200回调为螺旋路径且最多77帧；每枚创建后以Skills+14C即Param2加1写TargetX。不是均分成Calc1枚，也不是追踪玩家的散射。

`remote_scene_pd2.cpp::preparePd2RoyalFire`负责专用种子／中心／参数，effects分派复用公共螺旋与飞弹创建；缺身份／资源／公式或步长非正则暂缓。当前直接调用者568／574、981／997 maker及原CltDo6已有表现消费者。已构建；服务器SrvDo143已按下节接入；完整子体、同帧回调、视觉随机起点及场景控制器仍待闭合，不以此宣称完整首领技能完成。

### 皇家火SrvDo143

当前36A270在36A47A将服务端Do表+23C（槽143）写为3AE960；该函数只在Skills.SrvDo==143时执行新分支，否则保留原渐进委托。37E710更新一次路径种子，并只覆盖主人SEED低DWORD；以施法者位置为圆心，仅怪物1113使用与客户端一致的固定场地中心。SrvMissileA及Calc1分别提供弹体和原64方向的下标步长，不是总弹数：当前568／574的步长13／21分别产生5／4枚。

3A2580经全局5DED8C、构造器213310、getter319EF0和版本表4A830C绑定配套Game RVA93D60。原函数以路径种子／高字666初始化局部SEED，每枚调用一次RollRandom交给91040螺旋初始化，寿命上限77。原服务端仅当飞弹ID568（royalstrikechainlightning）才以技能280的Param2+1写TargetX；不能照抄客户端对所有弹体写入当前技能Param2+1。当前皇家火981／997不触发这个分支。静态指令证据见`town-queries/gap-royal-fire`，Game摘要24b293…。

独立`server/systems/missiles/pd2/royal_fire.cpp`准备中心、局部种子及螺旋请求，`skills/pd2/projectiles`走现有权威飞弹／费用事务；内容准入和怪物selfAction接入143。路径种子和主人随机均随同一出生批次复验、提交；容量等待保留已准备批次，身份／种子／ID游标过期则拒绝，不用新的随机重掷替换已准备结果。场地绝对坐标先转换为当前区域局部坐标；客户端保持其原绝对坐标口径。

当前表依赖闭包为981→982（Radament）和997→998→999（Lucion）。SrvDo6在换格时生子体，998的SrvHit48及CltHit53负责延时后子体，后继沿原技能链接、NextDelay及接触程序处理。客户端CltDo6补缺及剩余边界见下一节；此项已构建，尚未逐场景运行对照，未关闭首领AI、特殊控制器和全场景奖励缺口。

### 皇家火子弹与公共CltDo6

配套Client dd8b…的CltDo表DD8A0槽6为B92B0，未被本条当作PD2专用新算法。它在公共运动／碰撞前读取上次路径更新的换格标记；以CltSubMissile1为必需的基础子弹，第二／第三槽均存在时抽0／1／2分别选择第二／第三／第一槽，只存在第二槽时抽0才替换。先读第一槽Range，非零时作为选中变体的固定总帧数，不能让firesmall／firemedium使用各自Range或LevRange；之后独立抽CltParam1，零或抽样非零时添加创建标志0x4000抑制光照。子弹创建后按Common11080／11168取得的父弹精确坐标覆盖子弹坐标，不取整到换格后的新位置。

`presentation/world/client_missile_view.cpp`已补该公共回调及每弹的上一帧换格记忆，复用当前SEED、飞弹创建、固定寿命和suppressLight输入；原先移动后只生第一种子弹的路径已移除。当前981有982／firesmall／firemedium三个候选，CltParam1=4；997仅998，仍抽光照门控，998寿命20后由CltHit53生999。`CelFile=null`早已有imageLessProjectiles消费者，997无需补图或替换原图；空文件和真正缺资源继续不可用。

CltHit53为原Client B5A10，读取CltHitSubMissile1与父DynamicPath目标坐标（+0x10／+0x12；配套Common10983／10764），再把cHitPar1／2／3传入子图形的弹跳／初始轨迹；当前998三项均零。服务端原Do6及Hit48分别见D2MOO MissMode的对应回调，并以配套Game为后续核对入口。指令材料为`town-queries/gap-royal-fire/client-b92b0-instructions.json`及`client-b5a10-instructions.json`；InitSteps在B3535设置初始隐藏标志，B1862按原生经过帧数解除，并非预先移动；客户端已保存绝对路径目标而非用移动后的当前位置加旧方向，避免子体偏移；“首路径点”是原反编译命名误导，实际不是PathPoints[0]。InitSteps初始隐藏只约束绘制，只有激活且原生经过帧严格大于InitSteps才解除；B195C之后的原碰撞分支不读取隐藏位，因此隐藏不能额外屏蔽地形／单位接触。绘制不再以动画年龄重复裁剪，接触独立保留原激活门控。原视觉种子初态、完整同帧交错及逐像素表现仍需原生对照，本轮修改待最终统一构建实测。

### PoisonEggSpawn的双端委托

当前核心`36A270`将CltDo109设为`3A9710`：以原技能／等级先调用原表25，再以技能159／相同等级调用原表46。配套D2Client的25为RVA73CB0，读取CltMissile及CltCalc1，沿原64方向毒环发射；46为RVA56780，只将单位dwSeqSpeed清零并返回成功，不另生子怪／飞弹。`remote_scene_effects.cpp`为109复用环形创建，ClientSend不抑制此原生客户端回调；`remote_scene_units.cpp`在MonSeq首个动作事件保持帧，后续权威动作再更新动画请求。当前卵的UseAttackRate为空；非标准序列倍率的跨帧停留时刻尚未运行核对。

服务端安装`36A560`将SrvDo173设为`3B1090`：原技能／等级调用原Do22，再以技能159／相同等级调用原Do84，并忽略第一次返回值。SrvSt43仍是原版清目标标志／死亡碰撞。当前MPQ的453PoisonEggSpawn使用St43／Do173／Clt109，MonStats880 torajanBossPoisonEgg的AI为GargoyleTrap，Skill1模式为seq_maggotegg；子怪来自该行spawn（torajanBossMaggot）、spawnmode。不能因AI名相同而准备为普通陷阱射线。

`hosting/pd2/monster_actions.cpp`独立准备该动作，`skills/pd2/monster_skills.cpp`只编排Do22→Do84，复用公共毒环和孵化能力；数量用当前Skills159.calc1及本次等级，实际子怪／动画／碰撞从MPQ准备。毒环完成标记与孵化发布状态跨容量重试保留，不重复发环或创建已发布子体；孵化终止本体，沿公共无经验子怪和死亡处理。代码已构建，完整场景入口、出生／死亡随机交错及非本人像素表现仍未认证，不据此解除Torajan场景控制器的拒绝。

### 渐进事件与扇形

渐进事件0xA3现接106／107：旧0x3A6FD0／0x3A7130，更新0x3A9490／0x3A95F0。阶段由原事件与PrgStack重放，按阶段选择CltMissileA/B/C和CltCalc1–3；106从施法者向目标处的整数扇形发射，107以目标为圆心按64项整数方向发射。当前表对应Fists of Fire第二／三阶段及Cobra Strike第一／三阶段，不能只接普通CltDo入口。106保留原do-while至少一次，107的零或大于64数量取64。弹体选择旧39E3B0最终绑定1.13c D2Client RVA40550，读取AuraState/AuraStat1并将弹体槽阶段限至1–3；原事件阶段重放不依赖较新的累计状态。扇形helper旧379710／新37B960以施法者减目标，围绕目标生成端点；平方距离小于4时原指令把deltaY替换为此前平方距离，不能照搬普通扇形。纯几何入口为pd2MissileFanLayout，未改变原版missileFanTargets。

83替换旧3A6720仅在Skills.CltDoFunc本身为83时走专用路径，否则转回原回调；因此Blades of Ice／Royal Strike的渐进83仍沿原路径，不可整体屏蔽。83专用分支已接下述皇家火发射，109已接毒虫卵委托；108／114–115仍未完整恢复，当前MPQ直接调用范围见上段；94已有普通发射消费者，其他变体待核对。97已接本人、其他玩家及怪物；112接本人及其他玩家，怪物原Clt37另有消费者，不能将三者共用同一速度或序列。114属于M04的Lucion专用控制链。

### 尸体血迹与Sacrifice表现

104旧3A6EB0的Find Potion／Find Item委托原38／40及D2Client RVA4C500，已从真实尸体、释放帧及原MonStats2.Bleed恢复初始5条血迹，非零damage_blood覆盖数量；原喷血几何与随机流由同一单位的视觉种子拥有，单位撤出／离局清理。Sacrifice的原CltDo34／D2Client RVA2B530另外生成一条CltMissileA+CltCalc1随机变体，方向为原面对方向反向加三槽抖动，不能替换为尸体血迹程序。

### 跳跃表现

本人Leap未收到4C时使用当前skill_move确认起飞，按序列13第5帧释放。97旧3A6930读取Calc2，速度为max(2560,Calc2×41+2560)；原1.13c D2Client C66A0依AuraRangeCalc及三个旋转候选找落点，C65B0确定距离相关帧数／重力，C7C80设置高度与竖直速度。客户端复用原碰撞、25Hz路径和图形累加器；状态先移除而末端坐标尚未到达时，已确认的飞行仍完成原弧线。非本人玩家已接同一程序，使用4C／4D实际技能、等级、目标和接收时刻及该玩家状态，不读取本人的localCast或私有属性；缺公式／目标输入仍拒绝求值。怪物97已复用独立PD2落点helper及真实MonSeq，速度读MonStats.Run左移8位，不求玩家Calc2；普通飞行保持第10帧，sandleaper／ancientbarb1基础身份及991沿原距离切10／12。原生包装器39F150→5DED84→328D50的当前表4A7B54首项为Client C6AA0，3A8DF0另有991专用分支。此扩展已构建，全部障碍与装备组合仍待运行对照。未确认的预测沿既有15秒预算回收。

### Mind Blast状态输入

Mind Blast只消费解码成功且序号与当前单位一致的状态快照。当前表pvp_disable为属性491，旧核心386A69／39145F及更新388DC9／3937CF只在map状态198列表写1；本人投影复用完整装备、同序号状态及被动输入，未知仍拒绝半径求值。PvP地图中的完整状态输入仍待核对。

### 客户端随机与路径种子

111的随机函数旧36BAA0与原SEED不同：低DWORD乘0x6AC690C5，SHRD移位20，并按INT_MAX减高DWORD的阈值计进位。回调消费施法者流，并非全局流；本地视觉流的初始状态尚无法与原客户端逐随机一致，不用于命中或状态裁定。110／119共用36B9A0将路径帧数限至77；37C400／37C460／37C470从单位类型／GUID建立20位种子，12位计数递增并在0xFFF后回1，低位按0x6AC690C6递推。D2X按该确定算法准备本地路径种子，跨区保留、离局／单位撤出清理，不借权威战斗随机。

### Blaze与命中连锁

PD2 Blaze的当前States行未填原版setfunc3，匹配配置从Skills的Blaze aura关联准备移动留火；根弹体的CltHit12按SHitCalc1（Missiles +0x88）正值覆盖寿命，再在三个有效CltHitSubMissiles中选取图形，不能误读CHitCalc1或继承一帧根弹体寿命。固定1.13c D2Client RVA BB070与核心替换共同确定此链。CltHit16的连锁次数／半径以创建输入传入子体并逐次递减；半径优先Missiles.cHitPar1，否则取技能AuraRangeCalc，依据原D2Client RVA B67D0。原CltDo22／RVA15660从施法者搜索，回调RVA A09F0按目标GUID后继／最小GUID回绕选取；候选包含本次击中的目标，唯一敌人时允许GUID回绕到它。CltDo22在一般连锁选择器排除当前目标之后，独立恢复此回绕分支，其他命中连锁仍排除当前单位。完整目标／伤害组合仍待覆盖。

### 刀刃反射

CltHit6旧3A1DC0／更新3A4140只在无接触单位、当前帧大于1且介于ActivateFrame与TotalFrames内时考虑反弹；TargetX保存反弹次数，cHitPar3为次数上限。它先以原碰撞0x806及路径大小加1检查当前位置，再从当前拥有者到碰撞点的射线依次尝试X／Y反射。HitPar1–3（零值各取1）决定分支数量、64向角度步长和剩余帧损失，cHitPar1／2决定首跳最小剩余帧及终止阈值；子体只改CurrentFrame，保留TotalFrames。37CDF0只检查前方一个半径处的占地，不检查整条反射射线。客户端已接当前bladefragment1这条原表路径，单位命中不能触发墙面反射。当前MPQ的ClientSend=1不能跳过拥有者发射，原版D2Client CltDo6 RVA413A0直接调用B30F0构造飞弹，未以该字段拒绝；原版同入口仍保留普通飞弹创建路径。

服务端HitPar数组在当前TXT中的列名是sHitPar1–3；`client_missile_assets`已修正从这三个实际列准备serverHitParameters，旧HitPar查找会静默得到零。当前bladefragment1实际为3／5／2。服务端description_data已有sHitPar→hitpar归一化；当前1063条Missiles的TXT数组与BIN+0x4C三项逐记录一致，没有新增BIN覆盖器；表身份与对照材料为`p3-current-tables/missiles-identity.json`。

普通预设出生的SrvHit6直接调用者为859／864／909／953／979，当前模式均为NU或S1。979的0x402001E除了NOXP／NOTC，还在出生后设置完整目标位及INITSEEDSET；`monsters/pd2/missile_spawn`已补这组标志，不重新掷出生种子。其他未核实模式、部分目标位组合和专用预设仍拒绝，不能把当前五条数据范围扩大为所有预设出生已完成。

### PD2传送落点与怪物Blink

当前`3AF240／3AF4F0／3AF610／3AFD50→36B570`在实际位移前检查状态214／disable_tele，不能只在技能起手检查。玩家Teleport／Gust／Blood Warp与Joust已补该施放检查；已移动而等后继发布的重入不重复位移。`travel/pd2/teleport.cpp::pd2TeleportLanding`承接区域规则，公共castTeleport只在PD2规则下调用，Joust及怪物Blink共用。

Teleport=2通过原0x804射线检查，按来源尺寸与固定目标尺寸2缩短两端；公共Grid::interactionSegment增加掩码和来源尺寸输入，原调用默认仍为0x804／2。Teleport=3先用来源尺寸／0x80D寻位，失败仍沿原坐标；37E170拼接当前与目标房间的NearRoom数组，按各自单位链为613／614／615选首个最近物件，距离为Common10522的去尺寸后`2*max+min`。613在距离小于25且任一轴没有严格同侧时触发检查；614要求X没有严格同侧且与来源Y差小于25；615交换两轴。触发后同时检查目标3×3／0x80D矩形与0x80D缩短射线。当前对应Levels152／164／171／194，物件名blocker_teleport_xy／x／y，读取实际MPQ位置；不自造墙或禁传区域。

怪物当前直接调用者357Blink与573LucionBlink已由hosting准备为真实Teleport动作，`activatePd2MonsterTeleport`执行上述区域／状态门控，再走0x1C09落点与公共怪物位置发布；573按Calc2=32经36BAA0的64方向整数步进生成落地环。已补同幕邻房跨区域落点：复验目的区域代次、原邻房可达及碰撞，转移单位房间／占位，并同步内外两层技能续办区域及缓存坐标。已移动后不再次位移，落地环在新区域创建。当前两技能没有掌握减益，其他怪物减益组合仍拒绝。

Do155落地环已抽为同领域私有消费者，玩家／怪物共用SrvMissileA及Calc2输入；当前普通玩家Teleport没有SrvMissileA，不能据此编造附加弹。Gust的落地飞弹及范围脉冲分别保留准备／发布阶段。移动／费用一旦提交，续办立即绑定当前区域并保留nativeCallbackStarted，避免最后一次充能移除技能指针、换区或飞弹背压使后继效果被丢弃。该组修正已构建，跨区／末次充能／多人交错待最终验证。

Hell条件下36B570在param5非零时查询5DC81C集合。当前核心`190C50→292A40→292F90／293240`仅初始化空容器，`3D4960`登记析构；当前模块对根地址只见初始化、两处查询及析构引用，对相邻计数地址5DC820没有直接引用。尚未发现填充入口，不为当前目标自造一份禁传区域清单；外部模块／间接配置修改仍不在这条静态结论内，不能称为所有发行版均无禁传集合。

原指令与辅助器材料为`town-queries/gap-blink`、`pd2-area-skill/36B570`和`skill-helpers/36BAA0`；配套Common040692…的10686→315E0、10522→7FBD0分别核定射线与距离。当前States／Levels／Objects表摘要见`p3-current-tables/teleport-identity.json`。已构建，尚未逐场景运行对照，物件休眠／重新安装和全部跨区交错仍需核对。

### Claws of Thunder子体

CltHit67旧3A2330／更新3A46B0要求真实接触单位，在其位置生成CltHitSubMissile1并继承原拥有者及等级；CHitCalc1求初始图形Z，cHitPar1–3直接写图形定点速度，不能作为加速度或寿命。目标D2Client RVA C7E30把初始坐标左移11位；当前clawsofthunderbolt的CHitCalc1=280，cHitPar3=-81920，子体cotthunderstorm的图形及Range来自当前MPQ。客户端已接该分支，当前仅支持可直接读取的常数高度公式；非常数输入保持未支持，不置零。

### 冲锋表现

112旧核心3A7660和CltSt57旧3A8500控制冲锋的接近／攻击两段：玩家速度为CharStats.RunSpeed左移8位，再乘Param1加max(Stat67,50)的百分比；玩家路径沿原初始Straight路径和碰撞。攻击切到序列第8步，剩7帧，速度min(256,有效物品IAS+Stat68+Param6)，有效物品IAS对应D2Common RVA323E0的120递减公式。原PlayerStats／PlrSave2初始化Stat67／68为100，武器速度保留原负Stat68贡献。本人路径使用完整装备／状态输入并接入接近、攻击及末端坐标收敛。其他玩家经`client/pd2/player_rates.cpp`使用自己的0x20基础值、9D公开装备及同序号A8／AA状态求FullStats。当前表真实列名为`Send Other`，67／68／93均为1；此前把不存在的`SendOther`列读作空值的结论已撤销。Send Other不等于另发完整属性包：原PlrMsg::sub_6FC82830只初次发送67／68／12／0／2的BaseStats，物品IAS来自原公开装备／状态列表；D2X宿主已有这条发送链，不增加额外0x20或私有同步。Client191E0对所有玩家初始化67／68／69为100，缺省93是零初始化属性；只有装备／状态解码完整才合并，不借本人localCast或IAS。武器基础速度与真实活动手位独立准备，双持沿原平均速度。地面目标已接原坐标及Straight路径，终止不伪造接触；服务端原有地面到达搜敌／接触链继续负责裁决。新消费者已构建，怪物独立Charge37见下节，特殊首领114另按实际程序处理，完整打断／装备／障碍组合仍待运行对照。

### 怪物冲锋表现

怪物原CltDo37／Client2B6E0与CltSt25／2BF40单独由`remote_scene_pd2.cpp::displayPd2MonsterCharge`消费。客户端4D610初始化67／68／69为100；起手速度读MonStats.Run左移8位、Param1及max(50,Stat67)，路径保持怪物初始Toward2、距离30。序列事件按原函数重复读MonSeq第一条事件字节，移动段回卷、直接目标接触、停路搜敌及未找到目标的剩余帧分别处理；reanimatedhorde1／siegebeast1保留原停路例外。2B680以A283和范围3调用A1860／A1620，按可攻击标志、施法者自身阵营／主人敌对关系及mask4射线筛选，再由A09F0取GUID后继；不按观察者敌对或最近距离选目标。服务端St31近距回退仅ClawViper／Duriel用A2，其余A1。以上为普通Charge37消费者，不接管Lucion114控制器，已纳入双端构建，完整场景尚待实测。

Charge37的非序列模式也按当前MonStats实际声明准备：`RemoteScene::Impl`仅为真实37调用槽缓存对应模式的AnimData／COF时钟及事件字节，包含非序列A2；不以通用序列替代。程序维护原移动／攻击帧，渲染直接读取该帧，避免旧通用Charge裁剪再次覆盖。原0x15重定位会撤销该动作的旧预测，后续新动作才重新准备；完整接触、打断和多人观察仍待实测。

### 技能等级公告与陷阱

原1.13c D2Client RVA A1CF0接收4C／4D时安装远端单位技能，17B0读取Skills.maxlvl，非正取20；公告等级超过上限的部分作为额外等级。D2X给怪物保留本次公告技能的基础等级及实际等级，其他技能仍未知。Charged Bolt Sentry隐藏BoltSentry的Calc1读取blvl，基础等级与额外等级不可合并后再作为该输入。99／9A效果通知不作为技能等级公告。

本人释放事件及Leap落点公式按技能ID和真实来源物品GUID查询CharacterView技能等级，充能技能使用该物品记录的effectiveRank，不能从已学技能表借等级。充能降至零仍保留其已知等级供已接受动作完成；普通非充能技能继续使用自身有效等级。冲锋射程优先真实活动主手武器，只有副手武器时回退副手；盾不构成武器射程。本人速率查询也独立于主手UI时序提示，遵循原初始化及真实装备／状态输入。原0x15重定位撤销旧PD2移动预测，同一动作不从过期目的地重新起飞／冲锋；后续新动作才重新准备，连续远端Leap不复用上一跳的起点时刻。

PD2远端玩家的item_leap_speed／extra_bonespears／extra_holybolts同样从其公开装备、原初始化值和状态求取。怪物AC尾部的16项原属性列表已按当前Send Bits／Send Param Bits及Signed解码缓存，三项扩展属性按该怪物自身原属性列表与状态列表求值；其原零售构造没有这些扩展属性初值，未出现记录时在这个客户端副本中为零，不将主人或服务端隐藏装备填入副本。缺失／损坏的属性尾部、装备或状态仍明确未知。该修正同步覆盖伙伴Bone Spear84等客户端Calc1，数值只用于原客户端表现，不参加权威伤害或存档。


Inferno Sentry的本怪物技能槽须按原NAMETOWORD链接，文本大小写不能改变身份。当前MonStats BIN为1283行、步长0x1A8，infernosentry行415的+0x170为311，下一技能为262；TXT的Mon Inferno Sentry对应Skills中的mon inferno sentry。Sk1mode为seq_infernosentry，当前MonSeq有14步、第9步event1。客户端使用此槽的序列，修复错误回退monanim=xx而使动作及喷流缺失的问题。原1.13c D2Client CltSt52 RVA C4370以Calc2设置期限，CltDo77 RVA C5BA0复用74930创建喷流，再以CltCalc1经C7E30设置图形坐标并置0x20；当前值为-37。完整PvE接收者及逐像素表现仍待核对。

### 发射公式与激活帧

本人客户端发射公式复用RemoteUiClients的只读完整属性投影，含extra_bonespears／extra_holybolts；装备／状态／被动不完整时保留未知，不把缺少属性包当作零。共享Charged Strike／CltDo19现求当前Calc1，而非只接受原版`par1+lvl/par2`文本；目标点与原ChargedBolt路径编号仍保留。喷流、留火、火风暴、双手／分裂投掷及骨矛的客户端入口已接入；额外圣光弹及近战Charged Strike的完整目标／伤害组合仍待核对，不能将客户端入口等同于技能完成。

渐进106的创建标志10820包含绝对目标坐标20及激活帧覆盖800，结构+44写1。当前1.13c D2Client创建器B30F0在B31DB读取绝对目标，在B348F按覆盖值计算Range减Activate；公共推进B17C0在减帧前比较CurrentFrame与ActivateFrame，不满足时跳过碰撞段。D2Common当前10130／RVA6A030读取激活字段+08，不能照搬旧版本ordinal。D2X仅为显式传入activationDelay的发射准备阈值，106／118传1，首次创建和73配对共用计算；门槛只控制碰撞，不隐藏图像或停止移动。其他发射默认路径不变；原客户端逐首帧对照仍待完成。

### 客户端飞弹程序

客户端飞弹程序表另有独立替换，不能由技能发射入口推定子弹体完成。以下旧／新RVA均对上上述两份核心，只有已恢复的显示语义进入SceneAssets／ClientMissileView；不结算命中或状态。

当前新增飞弹对零售CltDo的直接依赖中，已补10／12／15／25的公共消费者。依据为配套Client dd8b…的表DD8A0及RVA BBD40／B2D00／B8BC0／B83D0；不是PD2新增同号程序。10先选择CltSubMissile1–2，再以等级／CltParam3求半径和周期，发射时重置父视觉SEED为X+elapsed、高字666，按0x4检查出生格；B8DF0／B2E00读取子表CltParam1／2设置高度、下落速度及独立Total／CurrentFrames。12在elapsed0按CltParam2的秒数分成三段屏幕震动。25按elapsed周期依次抽方向X／Y、出生X／Y，不能交换四次随机顺序。

15区分CurrentFrame与TargetX有符号计数器：非正计数逐次递减，到负CltParam2结束；正计数才追踪和产子。子创建flags5启用显式零速度，父TotalFrames−1虽写入+0x4C但没有8000标志，原创建器不使用该值；因此子体保持表寿命、TargetX=0并原地淡出，不能覆盖寿命或让它沿零射线对角移动。优先使用已知弹体目标，再取原主人已观察到的路径目标；缺目标事实不另选敌人。当前消费者包括740／1062、785／1010、900／950及684；技能CltDo57已按Client RVA58070补正计数来源：196／441／608的实际技能等级写入新弹TargetX，真实技能ID／等级传入创建结构，单位目标优先否则取原路径目标。Calc1写入循环数，但flags3没有8，不能据此延长寿命。独立73不含这些字段时仍保留未知，待同一创建配对补充。Missiles.Activate已按原创建器设置倒计时阈值，73配对保留已知的显式激活参数。新消费者及65／67技能身份修正已构建，完整视觉种子初态、低帧率交错和双端场景仍待最终核对。

| 飞弹回调 | 旧／新RVA | 当前显示行为 |
| --- | --- | --- |
| CltDo8 | 0x3A18C0／0x3A3C40 | CltParam2小于1取100、大于100截100；CltParam3为正时加其乘CurrentFrame再除4。每回调抽一次百分比，选择原闪电尾迹或普通移动；视觉随机流不宣称与原全局随机轨迹一致 |
| CltDo69 | 0x3A1960／0x3A3CE0 | 先普通推进，CurrentFrame等于TotalFrames−1时，在父弹体推进后位置生成CltSubMissile1，继承已知等级 |
| CltDo70 | 0x3A1A50／0x3A3DD0 | 返回物追踪原主人，距离小于2时结束；缺主人时继续原移动，不选其他单位替代 |
| CltDo71 | 0x3A1AB0／0x3A3E30 | 主人有效时按CurrentFrame对CltParam1取模，同处顺序创建三个有效CltSubMissile；非正周期不生成 |
| CltDo72 | 0x3A1B90／0x3A3F10 | 设置路径移动检测mask=0x100后普通推进。旧0x30A200包装器的1.13c目标为D2Common ordinal11164／RVA348B0（MD5 ee1238806ef6d6d9801d12a09d128fe1），写路径+0x50；不是螺旋轨迹算法。锤子轨迹仍由Skills.CltDo35独立准备 |
| CltHit65 | 0x3A2120／0x3A44A0 | 命中位置创建CltHitSubMissile1，再以cHitPar1的整数64方向步进创建CltHitSubMissile2环；需要真实主人，等级由父弹体继承；子体技能显式取父Missiles.Skill（BIN+0x194有符号WORD，空为-1），不取主人当前技能 |

CltHit5（旧3A1C50／更新3A3FD0）及66的有效子弹体范围回调仍未恢复，明确拒绝，不沿用原版同号效果。66旧3A2250在CltHitSubMissile1无效时直接返回成功，不做范围回调；当前Missiles BIN为1063行、步长0x1A4，holylightdelay行695的+0x2C为FFFF，已接这个空子体分支。本地原生PD2组队对另一账号施放Holy Light，生命150→188；该结果不能认证66的有效子体分支。6／67已接范围与依据见上文。不能把收到73当作完整命中程序已完成。

技能伤害显示还存在独立修改：旧核心 0x39D5F0→0x3A9C30 从当前 SkillDesc +0x18（ddam calc1）求值后加到原伤害；0x39D550→0x3A9B80 修改武器伤害百分比及支配组合。尚未接入这两条完整输入链，不能因当前描述行编号都在1–75范围内而声称全部提示数值一致。1级角色的树页与未学提示可核对布局／文案；这些截图不能替代高等级施放、装备及伤害公式的证据。

远端显示不准备原版武器／投掷药剂宿主程序，不把负stash坐标截成0。职业背包／装备几何已单独从目标Inventory表准备：七职业800px行均为invTop=32、invBottom=492、gridX/Y=10/8、gridLeft/Top=415/249、格距29；相对320宽面板的格子起点为(15,217)。原版仍保留既有布局校验。扩展容器及受限操作的当前范围由兼容设计维护。

`pd2assets.mpq`的`data/global/ui/panel/invchar6.dc6`为304845字节，SHA-256 `1476addbad8ede3560a7409dc3979834ba7b96908ac29ee54b65c1cac44fac83`。8帧左半保持256／176高的两行，右半第4／5帧高256、y=-35，第6／7帧高246、y=35，宽仍为256／64。D2X改以左半行基线和各帧真实高／有符号偏移拼右半图，同时用TXT绝对invTop投影装备与格子；不能将表面板460高误当作DC6总高度。初始法杖、药水与卷轴已经显示，全部图形和原客户端逐像素对照仍未完成。

RemoteDisplay独立准备Gems的武器／头盔护甲／盾牌属性元数据，不开启镶孔或符文之语的本地生成。当前Um Rune的第二项武器属性为`Deep-Wounds`，Properties的`deep-wounds`使用func1=1、stat1=deep_wounds；按原表大小写无关链接规范化后读取固定120，不能因大小写略过。孔内原服务器列表优先，未提供列表时只接受已核实的固定表值；需要随机roll但服务器未指派的值继续未知。远端套装同时准备SetItems的aprop条件层与Sets的部分／全套定义，激活层读取实际装备身份和原保存列表，未知随机奖励不取范围下限冒充实值。完整战斗／镶孔／套装组合仍未认证。



### 资源覆盖图

当前四个TXT覆盖图名称缺失的飞弹，仅在配套missiles.bin的ProgOverlay为FFFF时接受无覆盖图，不生成资源或猜映射；无法据此接受所有未知覆盖图。

## 服务端规则

### 包装器解析

原生模块解析与易误读调用关系统一见[版本与证据](BUILDS.md#包装器解析与反编译纠正)。

### Blizzard与Combustion：发射输入

当前MPQ的Blizzard59使用技能CltDo28／SrvDo28，`CltMissileA`／`SrvMissileA`均为blizzardcenter158。该中心的`Missiles.Skill`为空，客户端CltDo13（原D2Client RVA BBC10／B8DF0）从创建时的技能身份取Calc1半径和Calc2间隔；当前分别为Param1和Param3。`src/presentation/remote/remote_scene.cpp`按技能CltMissileA补足中心的关联，`remote_scene_effects.cpp`按实际拥有者和等级求值，随`ClientMissileInitialization.areaEmission`传入实例；`src/presentation/world/client_missile_view.cpp`用实例半径／间隔创建原表冰块变体。不能依赖仅原版宿主准备的`SkillSpec.blizzard`，否则PD2的无图形中心不会产生落冰。施法重建和原包同步两种创建均经过相同输入准备，配对后保留已准备的实例值。

Combustion376（当前中文表“烈焰爆发”）SrvDo159为当前核心RVA `0x3AF870`：从Skills结构`+0x13C`读取Calc2，经公式helper `0x30AF50`求值后传给环形helper `0x36BAA0`，速度覆盖为-1。栈中副本起点EBP-0x248、读取位置EBP-0x10C，差值为0x13C，不能误读成Calc1。客户端CltDo100同样使用Calc2，见上表。`src/server/systems/skills/pd2/projectiles.cpp`已修正为Calc2；`remote_scene_effects.cpp`在本地PD2连接也执行现有CltDo100火球环。数量、弹体和环形整数步进仍取原表与原程序；本地／远端全部等级和画面组合未逐项核对。

### Do163：实际手位、分配伤害与独立防御

主入口RVA `0x3AFEB0`，范围回调`0x303360`，技能ED选择`0x306090`，Smite盾伤`0x3062A0`，分配后结算`0x305A50`，独立防御`0x3037C0`，参数层筛选`0x37F760`。对应代码为`src/server/systems/skills/pd2/splash.cpp`、`src/server/systems/combat/pd2/splash.cpp`与`src/gameplay/items/pd2_parameterized_stat.hpp`。

- 当前隐藏技能358（proc_SplashDamage）Calc3为`max(6-lvl+stat('inc_splash_radius'.accr)/20,0)`；389（Golem Splash）为4，393（Skeleton Splash）为3。Calc3已含无参数属性478，后续只加匹配武器类型的参数层／20，避免计两次。基础物品`code==normcode`时半径再减1；已核对Items结构code／normCode偏移为0x80／0x84。
- 当前属性359 `item_splashonhit`为damageRelated，`itemevent1=domeleedamage`、func20；layer22913解码为技能358／等级1，概率100。它属于本次所用武器伤害视图，不能只扫描角色根属性，也不能固定取右手。D2X在主接触仍存在时，按`unitStatValues(actor,tick,weapon.item)`注册并同步执行`ItemSkillEvent::MeleeSplash`，让踢击能取原分配伤害。
- 已用技能只对玩家提供特殊元数据；怪物保持普通技能0语义。`0x306090`只对0、10、96、97、107、126、133、144、147、151、152、232、260、378、380取Calc1；30、106、248、255、265、275取Calc2；其余不附加ED。不能把其他技能表示数量的Calc1当增伤。Concentrate144另外按EType及Calc4转换魔法伤害；当前Berserk152没有同样的EType配置。
- Smite97的Param5非零时取得或分配3帧state224，写属性478（当前Param5=20）；复用列表不延长旧期限。伤害来自盾牌基础、Holy Shield及允许的加成，HitFlags2／ResultFlags1、hitclass0x65；范围副目标不继承主Smite近战距离或Calc2眩晕。
- 踢击255／270／275回调先复制原pCombat的0x70字节伤害，再加技能元素和物理掷值；使用完整已分配物理伤害，不能限为主目标剩余生命。原分配HitFlags为0x23，不是Drain局部副本的0x20。分配后毒时长减半，非零至少1帧；击退等字段随副本保留。
- 范围圆心为主目标，排除主目标。`0x305A50`在分配后执行专用防御，成功且不含uninterruptible54时强制GETHIT（4），再走怪物暴击、事件、伤害、受击／死亡及节点释放。
- 专用防御先算Weapon Block，适用武器动画类5（2hs）／13（ht2），读取参数化属性348，封顶75；攻击者怪物1112使概率除3，特殊PvP区域使玩家防御概率减半。随后移动目标先掷Evade，失败仍继续Avoid；Do163传入missile=1，不走普通盾牌格挡。玩家成功防御后4帧内不再掷新的防御。
- `0x37F760`最多读取32层，要求真实当前武器；类型0x72／0x74为2han／1han（TXT物理行号因Expansion行不同）。2han要求另一手为空或同一物品；可双持者的不同另一手影响1han／2han匹配。玩家双持职业为bar／ass，影子原生身份为417／418，不能错位成418／419。

防御／Open Wounds／Crushing Blow使用`pd2UnitEventRandom`，其乘法进位与SHRD移位20的规则不同于普通SEED和CRT；三者不能互换。玩家state233及原生特殊格挡／soft-hit细节、全部PvP组合尚未逐项认证。

### Bash与Charge：额外接触和短期状态

Bash126／Do158为RVA `0x3AF780`，代码在`src/server/systems/{skills,combat}/pd2/bash.cpp`。主接触先清KNOCKBACK（8）再Drain；原主命中成功且目标具有`UNITFLAG_UPGRLIFENHITCLASS=0x8000`，CRT百分比小于50时才分配第二接触。原指令先清零8字节头之后的0x68字节正文，再明确写HitFlags1／ResultFlags8；第二接触没有SUCCESSFULHIT，因此AllocCombat不Fill、不Calculate、不追加生命伤害，仍执行DOMELEEATTACK／ATTACKEDINMELEE和击退。

该门控是单位生命／受击通知位，不能用“发生任何命中”或“已进入受击动画”代替。D2MOO `SUnitDmg.cpp`中玩家非GH通知、怪物soft-hit及与上次发布生命差值有关的分支会置位，GH模式切换的提前返回不经过相同写入；`SUnit.cpp`发布单位时清位。D2X只用当帧锁存及零伤害接触表达此顺序，不保存该位。传输发布时点、全部last-sent百分比和多攻击者交错尚未证明逐位相同。

Charge107的St72／Do180为RVA `0x3B4350`／`0x3B23E0`，对应`src/server/systems/skills/pd2/charge.cpp`。起手以原整数距离存入已用技能Param3；执行时取得3帧state224，将`(Param3/4)*20`写入属性478，随后Param3清零。复用旧列表保留到期时间，零值也必须覆盖，不能只写正数。到达时同帧攻击，使用Calc1增伤、ToHit、击退及hitclass0x70，Drain后给目标overlay147；起手已经过近则退回Attack0。

### Vengeance与Sacrifice：延迟回调的来源

Vengeance111／Do174为RVA `0x3B10E0`，范围回调`0x3043C0`、传播`0x303EA0`，代码在`src/server/systems/skills/pd2/vengeance.cpp`。初接触分支看准确率helper的返回值，不能把所有后续防御失败都算成同一门控。先从装备火／冰／电贡献扣掉状态35+16／43+188／46的光环或强化贡献，再套Calc1／2／3与对应掌握329／331／330；技能EType0的物理基础另掷后按掌握复制到三元素，物理清零。

当前冰时长为ELen80减`15*depth`并钳零；首圈范围7，后续至少2。Calc4决定传播概率，Param5延迟当前4帧，Param6+1保护当前25帧（state86 JustHit）。圆心先写AuraTargetState，接收者伤害后仍存活且无JustHit才写保护；不是施法者当前路径目标且单位随机通过时，登记type7传播。圈后清圆心state107 shatter。延迟触发重新取当前等级和装备，不固定首击快照；D2X定时器标识0x50443256仅属运行态。

Sacrifice96／Do64为RVA `0x3ADAA0`，死亡Hook `0x302930`，回调`0x303C60`，范围`0x303E90→0x304C60`；对应`src/server/systems/effects/pd2/sacrifice.cpp`。主Drain前反伤按当前生命乘Calc2（当前3%）。死亡Hook要求WILLDIE，并检查实际攻击者当前已用技能96，或真实来源飞弹关联技能96；最终归属主人仅参与定时器信用，不可借其已用技能放宽触发。范围伤害携带skill96公式身份也不自动成为“真实飞弹”。

原延迟为总动画帧／动画速度减6，钳3..30，缺省3；D2X用当前规则deathTicks减6表达，改变动画速度后的精确时点尚未认证。过杀量为`max(totalDamage-stat259(previous_hitpoints),0)&0x0fffffff`。先标state223，回调重新取当前技能等级的Calc3范围，缺少时取Param4；尸体生命依据原生生命表的Min／Max平均，伤害为该基础生命的Param5（当前5%）加过杀量的Param6（当前125%），物理伤害。清state223后创建／通知／移除视觉897，按filter8583结算范围；不消费尸体。连爆仍受当时已用技能或真实来源飞弹门控，宠物信用等稀有组合尚待核对。

### Desecrate、召唤和诅咒的数据边界

Desecrate83／Do161为RVA `0x3AFB10`，选池`0x36BE50／0x36BEF0／0x36BFF0`，到期`0x36AF20`；代码为`src/content/pd2/corpse_data.cpp`及`src/server/systems/skills/pd2/corpses.cpp`。普通难度用当前Levels的mon1..25，其余难度用nmon1..25；排除PartyMin／PartyMax为正、Spawn不是-1及原生40项身份排除集。排除初始化RVA `0x190C70`，后备区域初始化`0x190D60`，当前后备为6／66／76／106／132（132池为空）；CRT按后备区域、槽位拒绝采样，不能用均匀合法列表替代消费顺序。

死态怪物创建包装器`0x39F0A0`经表`0x4A710C`解析到D2Game `0x47CF0`，以DD创建、记录主人并修正计数，无经验和掉落。位置为圆心、+2/+2、-2/+2，后续每轴累加`rand%2-2`。碰撞失败保留已生成尸体，全部5具成功才发8条毒云217并支付法力；不能把部分失败当事务回滚。400帧后仍可选的尸体加state104 corpse_nodraw并取消选择，不删除实体。D2X更新实体revision后再准备发布，防止新尸体内部存在而客户端遗漏。

召唤定义、光环和诅咒分别在`src/content/pd2/{summon_data,aura_data,curse_data}.cpp`准备。PetType身份使用原生idx，不能跨Expansion分隔行直接拿TXT行号／Id。fenris的TXT／BIN差异及实体身份规则见[怪物基线](MONSTERS.md#编译字段宽度与monprop)。骷髅MonProp溅射由单位接触事件执行，发布不覆盖当前攻击动作。

Dark Pact按原列表顺序保留最旧的有限时诅咒，移除其余；按移除前数量结算魔法伤害，无诅咒不伤害。Curse Mastery影响可用槽数，不应靠同一状态ID的简单覆盖代替多槽顺序；对应`src/gameplay/effects/pd2/curse_slots.cpp`及`src/server/systems/effects/pd2/{curses,dark_pact}.cpp`。Iron Golem材料／恢复、复活完整动作、伙伴跨区、全部友军治疗及复杂装备组合仍待覆盖。

### 装备命中效果

Open Wounds与Crushing Blow的当前核心规则统一见[物品与属性](ITEMS.md#open-wounds与crushing-blow)。

### Exploding Arrow：命中与延迟出生

Hit63为RVA `0x3A6410`，延迟回调`0x304680`，扇形helper `0x37F3E0`；对应`src/server/systems/missiles/pd2/hit.cpp`，定时器登记与游标由同模块`system.cpp`／`runtime.hpp`持有。命中先创建4个HitSubMissile；HitPar1为正时对子体／受害者调用`0x3063B0`完整MissileCollision，它不是单纯出生helper。当前Fire Arrow／Exploding Arrow飞弹12／41的HitPar1均为空（0），不能据另一个版本强加立即碰撞。

后续扇形公式取飞弹链接的技能：Calc2数量当前至少3，Calc3距离当前10，Param4的一半宽度当前12.5，Calc4为起始延迟，每点再加2帧。每点按原顺序消费3个CRT随机数，用平方根重心、第二权重及平方距离分层，沿源／目标方向的反向三角形插值。延迟回调固定使用技能16，读取当时已分配等级，缺省1，不沿用任意来源技能。

回调的A持久弹体与B／C瞬时弹体分别创建并移除。D2X `prepareNativeBirth`不接受混合transient标志的同批请求，故必须独立提交；`Pd2DelayedSkillMissiles.next`保存已完成位置，背压重试不重复A，碰撞等Unavailable按原生空指针分支继续。不能把拒绝混合批次误判为缺少飞弹程序；B／C完整客户端像素时序仍待核对。


## 待核实与未完成

尚未逐项认证全部装备／技能组合、随机初态、极端数值、碰撞、画面、难度、跨区、PvP及多人。喷射状态中的FCR周期额外施法触发已有独立消费者；怪物SC／SQ施法触发已有消费者，连续伙伴及部分ItemCheckStart组合仍待核对，见[物品触发](ITEMS.md#装备触发与装备自带技能)。Iron Golem材料／恢复、Revive家族动作及伙伴跨区的消费者和边界见[怪物](MONSTERS.md#召唤实体与伙伴)。未知程序明确拒绝，不以图标、扣血或构建成功作为规则依据。

### 特殊场景技能

当前41d49的St76／Do185／Clt114已分别接入monsters/pd2/dash、skills/pd2/dash及remote_scene_pd2_dash，沿当前MonSeq事件、路径9与起步／沿途／到达三槽执行。Srv184局部序列、Srv186 Act5强制73、召唤177／179及485状态写入的实际消费者和限制见[F61](FUNCTION_PARITY.md#f61-专属4145控制器的出生与事件依赖)；Skills.summon按当前MonStats编译身份链接，不按文本转数值。
