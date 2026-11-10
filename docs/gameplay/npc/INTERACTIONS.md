# NPC交互与对白

已有NPC会话的27替换可用对白列表。零售客户端保留正在播放的台词及本次会话已经播放的编号，结束当前台词时再从新列表选下一条。任务确认改变对白后，宿主按最新权威记录重建任务及未确认介绍，可靠发布27／29后再替换交谈revision。背压保留刷新请求，任务只结算一次。初次交谈使用下文的27／29／28确认，刷新只发27／29。依据为零售D2Client 7DE50、4B230及本地D2MOO `QUESTS_NPCActivateSpeeches`。

会话按原textId保存多个待确认任务及独立介绍，不再只容纳一个任务回调。已接取的邪恶洞穴、血乌和罗达门特评论共用当前MPQ准备的`questSpeeches(quest,state)`，选择AfterInit／EarlyReturn并匹配真实NPC；因此Charsi、Warriv等也能提供相关评论，不按NPC名称补固定消息。依据为D2MOO对应NpcActivate文本链与原版Host接取邪恶洞穴后Charsi的menu2／67；其他任务及奖励后评论链仍待逐项对齐。

NPC是所连接服务端的真实type1单位，位置／模式、对白、任务提示和服务资格均由该服务端提供。中立单位不使用敌对替身；组件／原图由公共ActorAnimation／Graphics解析。

剧情关键NPC的出生沿D2MOO `QUESTS_SpawnCriticalMonster(bSpecialInit)`传递原ISRESURRECT保留资格，复用公共Actor的`restoredFromInactive`与既有休眠／恢复；不把城镇SaveMonsters=0当作可丢弃关键NPC。当前MPQ安雅Restore=1、Harrogath SaveMonsters=0，缺该资格会在离城后丢失本人，却留下任务出生位。现spawnStoryNpc仅对原critical分支传入specialInit，非critical及其他默认出生调用保持原资格；无按NPC名称强制重生。81412075实测安雅领取个性化资格、离城后本人285进入109区room12休眠记录（dead=false），不再丢失；同版实际读回完成档后，再以正常旅行离开第五幕至Act1城镇，安雅233进入休眠；同局返回109后恢复同一233并正常交谈。普通出生／退役／恢复通过，容量与提交交错仍未穷举，入口见[验证约定](../../development/TESTING.md)。

原版 LoD 1.13c `D2Client 4B955→46770` 在确认交谈时清除旧 NPC 提示；`479DA` 忽略正在交谈 NPC 的新 8A。客户端已同步这两个条件，后续提醒仍由新 8A 决定。`4BA48→A5600` 的交谈朝向已补入显示层，保留原类537／538／539／527的固定朝向例外；不修改服务端位置或交互距离。交谈前另有待机转向分支。零售 D2Client RVA `4F577–4F60E` 对 MonStats.interact、非 critter／佣兵且处于 NU 的单位，按双方原生尺寸距离 <7、每次客户端游戏帧抽取 <10/100 转向本人；此处分开排除类537／538／539。显示层按该条件使用单位本地表现随机流，实际转身时刻待对照。

## 宿主NPC移动与地图动作

奥玛斯演出是否可点必须分别核对服务端交谈与客户端选取。D2MOO A3Q3／Fn032及MonsterAI交谈入口未见放置期间的专门拒绝；原1.13c D2Client（MD5 `f5860c629d309b8fc1f96174babcf633`）RVA `A68E0`另检查单位可选标志及图形命中，S1动作12／13经`4DC70→4E006／4DED6→2A6C0`进入模式8，所查分支未直接清除目标可选位。当前MPQ `ors1hth.cof`与`ornuhth.cof`的TR层Selectable均为1。上述不能证明演出每阶段都可点；运行中的标志与完整鼠标链仍待核实，不据服务端允许新增客户端全程禁点或宣称全程可点。AI主动等待祭坛放置完成是策略，不能充当原版点击规则。

`hosting/npc_content.cpp`按当前MPQ的MonStats.AI区分Npc、Towner、NpcStationary、JarJar和NpcOutOfTown，准备Velocity、aidel、MonStats2.mS1／mS2及原AnimData时钟；DS1单位预设的MapAI节点连同动作号完整传入该出生点的MonsterRule，使用全局子格坐标。节点不按自定义半径、当前碰撞或动作号预先删减，不为缺路径的NPC合成随机闲逛。NPC的AI／模式事件和碰撞移动由同一个monsters::Actor执行，城镇门控允许这些非战斗AI；原协议仍由公共monster_replication发布；S1／S2显式朝向经Actor→MonsterSnapshot→原6B／6C动作包传递，不再统一写0。

权威入口为`server/systems/ai/town_npc.cpp`，对照本地D2MOO `AiThink.cpp::AITHINK_Fn032_Npc/Fn041_Towner/Fn054_NpcStationary`、`AITHINK_ExecuteMapAiAction`、`sub_6FCE61E0/6270/6340/64D0/69A0`及AiTactics移动辅助。Npc／Towner首次记录command10原地坐标并等待20帧；MapAI先掷66%再从全部节点抽一个，1／3为走位，2保留20次等待，4／5保留走位、command4等待及command7的S1／S2动作；缺少该模式时沿原NU50分支。动作支持来自当前MPQ，不伪造S1／S2资源。已有路线在结束前继续执行，交谈暂停，NpcStationary保持原位。Fn032 的 `SetVelocity(1,0,0)` 在 AiTactics 内转为路径类型7，零售 D2Game RVA `19D4C` 直接写7；接近按双方原生子格计算短步坐标，保留一步参数。Npc的任务提醒对象查询在近房间单位顺序中取首个满足FullUnitSize距离15和现有任务提醒资格的玩家，保留接近、问候18、60次冷却及command10距离16分支。Fn032／Fn054 在选中有资格的玩家时向该人发8A；A1–A4的Q0欢迎资格取本人当前难度的原任务位。无合格目标时Fn032继续原DS1地图动作。Towner不执行该玩家接近分支。休眠保留MapAI及NPC的command10原点，临时command4／7及问候计数在重建AI时重新开始，不写角色档。

定点只读核对本机1.13c D2Game.dll，MD5 `978f6f74cd031bce6ecee602cd5988cc`、ImageBase `6FC20000`：AI表第32／41／54项分别为RVA `1DB40/1BC60/19880`，MapAI选择为`1BBC0`。Npc的交互辅助`19C20`在`19C62–19C67`要求交互结构非空；本地D2MOO `sub_6FCE5EE0`的该判空方向有转录错误，不能照搬为“有交互结构就跳过”。`16A30`读取全局绕行计数，不能照搬D2MOO局部零变量；执行器在实例内保留该计数。预设传递及休眠依据为D2MOO `SUNIT_SpawnPresetUnit`、`SUNITINACTIVE_*`与`DRLGPRESET_CreateCopyOfMapAI/ChangeMapAI`。

专用剧情入口为`server/systems/quests/npc_story.cpp`，状态归quests，Actor位置／模式／出生搜索归monsters，玩家／红门归travel，Fn031的189号剧情蓝门归objects及任务QUESTFN；不由UI推导剧情。当前接入范围：

| 原版入口 | 当前消费者 |
|---|---|
| A2Q4 Init18／19、GetAndUpdatePalaceNpcState、Fn081 JarJar | 按欢迎／离城及任务状态撤出起点杰海因并准入宫殿身份；消费实际宫殿／阻挡物标记，等待合资格玩家接近、走向阻挡物、守卫后退、原碰撞搜索重定位及command10原点平移；守卫保留200帧决策门控与开门后站位 |
| A3Q3／A3Q4 NPCDeactivate、Fn032 | 交付吉德宾并结束交谈后，奥玛斯走向Init39祭坛并执行S1，祭坛沿ENDANIM转为开启；艾柯收鸟后结束交谈执行S1，再提供成功对白 |
| A1Q4笼子MONUMOD、Fn031／Fn032 | 笼子17帧事件准入真实cain1，按原半径3后备搜索；凯恩先离开出生点、生成蓝门、走向门并退场，随后准入城镇凯恩并向城镇门靠近；出生失败沿返城后备分支 |
| A5Q3解冻定时器、Fn031、Fn032 AnyaOpenPortal | 原任务20帧时钟分两次开启冰封物件、准入真实drehyaiced并移除冰封物件；安雅走位／蓝门退场后才准入城镇身份，马拉20132领奖回调可清理活跃／休眠野外身份并请求返城；红门起点来自安雅坐标+10,+5，重入依原痛苦之厅传送点资格请求重开 |
| A3Q0 Init49／50、Natalya Init52、A5Q3／Q4／Q1 | 赫拉铁力欢迎后待起点房间失活再转至终点，保留交谈；墨菲斯托死亡撤出娜塔亚，解冻撤出城镇尼拉塞克；拉苏克使用原出生搜索 |
| QUESTS_MapPresetSpawn／DRLGPRESET_ChangeMapAI | 只接原物件459／461／543的安雅／尼拉塞克／拉苏克路径缓冲；标记与NPC两种加载先后均可转交一次，安雅野外出生可消费已有缓冲；路径写回Actor恢复规则，房间重入不恢复旧路径或已撤出的NPC |

MapAI三类分派另定点核对上述1.13c DLL的RVA `35E50`（目标`59E10/59D30/5A5C0`）。Hratli／Natalya虽有任务缓冲字段，本地D2MOO及该分派均没有对应提供者，当前不自造MapAI来源。剧情状态仅属于本局，不改角色存档格式；已完成档按既有个人任务位恢复城镇状态。Fn031正常退场已拆除双端玩家门户准备：A1Q4／A5Q3在野外和城镇分别准入Objects189，使用各自25帧QUESTFN，城镇第六次ON更新或Cain到位后关闭，野外随返城标志关闭，随后3→4。两端不互相钉住房间、不提前请求城镇地图、不登记可供玩家旅行的双向门。当前MPQ的189为Dummy／Init61／OperateFn0，Selectable0–7全0；客户端不列入选取或AI通行目标，玩家不可进入。返城回调遇到已激活的54／66标记时先出生城镇NPC，再在实际NPC坐标创建城镇蓝门；标记晚到时只沿原Init分支出生NPC，不补造蓝门。Cain出生失败仍保留A1Q4_SpawnCainInTristram明确创建的玩家回城门，该异常分支不与正常退场混用。命令3在Actor重新准入后重建，Cain坐标已初始化时沿原返城分支，Anya重新捕获坐标；容量／内容等待保存本次出生阶段，原分配失败不无限重造城镇门。Fn031末尾沿MonMode直接DD分支清零生命、切到12，撤出目标／非死亡状态／碰撞并取消原生事件；不触发DT杀敌奖励、UMod死亡伤害或房间死亡GUID，尸体保留至正常房间生命周期撤出。依据D2MOO MonMode::sub_6FC642C0／sub_6FC63E80及Fn031；退场阶段与房间卸载的交错仍待实机对照。

剩余边界包括完整ActiveFilter／PLAYER_IsBusy组合、原房间客户端列表顺序、返城门户两端及标记／NPC加载顺序的运行对照、直接DD的协议／表现与状态回调、完整休眠AI命令恢复及全部多人／逐随机轨迹。普通MapAI和上述专用分支已接入，原版完整演出及上述边界仍待逐项对照。救援囚犯独立逃离程序的限制见[后三幕任务](../quests/ACT3_5.md)。

## 世界点击的公共边界

| 目标 | 原版判断与当前入口 |
| --- | --- |
| NPC | LoD 首击单位距离 2，npc＋interact 先发 59；保留交谈动作在走近途中按通用近战范围判断。服务端 13 独立检查距离 6／7–8。见下文 |
| 门、箱子、可破坏物、祭坛、可操作机关 | type2 共用物件矩形范围及 0x0804 射线遮挡，随后按 MPQ 模式和 OperateFn 分派。RemoteTown → interactionClear → objects；OperateRange 不作统一中心半径 |
| 陷阱 | 先按真实类型区分可操作物件、箱子触发效果和可攻击怪物；沿相应公共入口，不另设“陷阱点击距离” |
| 地面物品 | type4 单位距离 ≤4 且无 0x0804 遮挡。nativePickupReach 与服务端拾取校验共用几何 |
| 地图出口 | type5 单位距离 ≤4，再由服务端决定旅行 |
| 玩家尸体／交易 | type0 首击距离 8，服务端核验尸体权限或交易资格 |
| 敌对单位 | 按 Skills.range 分 melee／ranged／both／none；近战用武器范围、目标尺寸、移动加成和碰撞。原 INVENTORY_GetEquippedWeapon 优先可用右手、再左手，排除 BROKEN／NOEQUIP；客户端接近预测已按此选择 |

物件与拾取依据为零售 D2Client RVA `1AB50–1AC83／1AC9C–1AD7A`，出口为 `1AED0`，服务端依据为 D2MOO PlrMsg。近战依据为 D2MOO Units::UNITS_IsInMeleeRange、D2Inventory::INVENTORY_GetEquippedWeapon、D2Skills::D2Common_11016。公共入口的源码核对不代表每种机关操作、技能、双持或变形组合均已实测。精确物件接近路径、触手近战特例和变形武器分类仍须独立核对。

原版 1.13c Host／D2X Join 的鼠标对照覆盖艾柯交谈、传送点、木桶和门。隔桥 NPC 与隔花坛木桶有远点接近停步、近处操作成功的样本，尚缺同条件原版客户端对照。

## 交谈与对白

SceneController统一悬停、高亮、姓名、点击／目标锁定和释放；RemoteControl按真实GUID靠近后0x13交谈，原0x27对白列表→0x29公共标志→带NPC身份的0x28交互确认→0x2F准备及公共对白UI继续消费。D2X先保存27待确认列表，收到匹配的28才打开会话并发送2F；2F确认已有权威会话，不重发27／28。一般私人任务刷新使用28/type6，不把玩家GUID编码成NPC身份。0x31确认实际原stringId，已确认话题不重复提交；0x30关闭，旅行0x38 action=0等原幕／位置结果。

当前TBL对白首行数字按a1npc SPEED元数据移除，通用字符串查询仍保留原文。hosting比对当前MPQ的NPC文本与TBL时同时处理UTF-8及原单字节省略号／弯引号，避免遗漏阿卡拉女巫介绍；对白编号仍由实际TBL匹配得到。原0x27消息的menu=0自动对白依次显示未确认集合；menu=2为可选任务评论，通过Talk打开，不在每次点击NPC时抢先弹出。任务原表中的menu=1经QUESTS_InitScrollTextChain转为回包menu=0，不能直接发送为原版的紧急交互菜单。普通闲谈由客户端读取当前MPQ，不占27的任务／介绍槽，也不发31任务确认；D2X通过Talk里的Gossip轮播。首次介绍和任务自动对白由服务端消息及PlrIntro旗标决定，不增加客户端推测的个人任务资格。独立91只设置待播放的返程问候，客户端打开NPC菜单时按原声音选择程序播放并发4D清标志；不在普通Gossip时发送4D，不代替31任务对白确认。宿主91生产及完整音频时序边界见[消息速查](../../modules/MESSAGES.md#d2x客户端对应入口与消费)。NPC0x8A感叹号读取原npcalert，按MonStats2.OverlayHeight-1选取Overlay.Height1..4，定位复用[公共覆盖图](../skills/COMMON.md)的地面加8、原X／Y偏移及单位图形／抬升偏移，保留DCC锚点；AnimRate沿原8.8增量换算为25Hz帧时钟。它不是本地任务资格。最近定位与动画速率修正已通过Windows Release构建，运行仍待验收；原字幕时序、重播间隔及提示全部像素／时钟组合尚未完整认证。

原0x27有8个物理消息槽，1.13c D2Common重建链表时要求count小于8。宿主和客户端均按0..7校验。原版Host对初见阿卡拉的女巫发送64（邪恶洞窟任务）和12（女巫职业介绍），两条均为menu0。零售D2Client RVA 7DEA4／7DEAF调用D2Common #10485／#10151，按包中顺序逐条插入链表头，再按无符号stringId升序排列。RVA 4BA39调用D2Common #10778取第一条menu0，后续由45FF0选取剩余台词。因此先显示12，再显示64。D2X在零售NPC会话的公共副本中复现该次序，手动和AI共用。编号来自服务端及当前MPQ，D2MOO的D2Text和d2decomped的 `npc-talk.md`／`quests.md`用于核对文本链与身份。

NPC服务／Talk菜单与交易邀请共用[OriginalMenu](../../../src/presentation/graphics/original_menu.hpp)，读取当前MPQ boxpieces、font16与Sky PL2：金色标题、蓝色悬停、测宽／居中文字及行命中区域。1.13c D2Client RVA 46880、48BF3、A3580、A3930 表明，服务菜单从 NPC 屏幕坐标上移 150，纵向锚点至少为 20，再减去整框高度的三分之一并按可视区限位。标题基线为框顶加 21，选项基线每行再加 15，宽度为最宽原文加 20。绘制与命中使用同一布局。原服交易消息与结算参考见[联网](../../modules/NETWORK.md#原服物件与交易观察)。

普通02／04单位靠近与交互靠近共用Path.cpp初始化的nStepNum=0及PathMisc::sub_6FD5DB70停止判据，使用当前MPQ双方原始SizeX和D2Common_10399，逐步停止预测路径；距离尺寸与碰撞pattern分开，不用小单位统一cross的size2替代真实size1。服务端普通敌对单位／其他玩家的04目标也保留同一距离0到达条件。这个单位距离0包含足迹大小，并非中心重合。13菜单资格仍为≤6，7–8由服务端走近后重试；不把6当作普通04路径终点。

原版 LoD 1.13c 普通点击以 D2Client.dll 的友善单位分支为准。RVA `1AFEA–1B00F` 对 MonStats.interact 目标取单位距离 2，`1B0E9–1B0FA` 调 D2Common #10407 计入双方尺寸。首击 ≤2 直接发 13，>2 先按 GUID 走／跑并保留交谈意图；这个判定没有读取武器。兼有 npc 标志时，`1B080–1B0BC` 先停目标的本地路径，再发送 59（类型、GUID、目标 X／Y）。服务端的开菜单距离 6 是独立校验。

保留的 13 经 D2Client RVA `2B090 → 2AEA0 → 29E80` 调用 `UNITS_IsInMeleeRange`，使用可用装备的 rangeadder、目标移动加成及碰撞；未到达时继续单位移动。D2X 已让这一阶段复用近战几何与装备选择，首击仍为距离 2。普通单位路径本身的停止距离 0、保留动作的执行条件和服务端菜单资格各自独立。

D2X 的普通点击和调试交互共用上述首击距离及 59 → 02／04 或 13 发包顺序。客户端点击停步立即记录已结束的动作修订并清路径，后续同指令的位置样本不能重新启用旧路线；正式新移动包仍可接管，公共动作边界见[怪物表现](../monsters/PRESENTATION.md#客户端动作与停步边界)。宿主按实际单位、标志及距离 ≤50 接受 59，停止 NPC 路径并把 AI 决策排在下一帧。Fn032 读计数 40 和所报坐标，前四次决策可寻路纠正超过 2 的位置差，此后按 8 帧间隔待机；剧情回调仍先执行。依据为 D2MOO PlrMsg 的 59／13 分支与零售 D2Game RVA `19DF3–19E65`。59 本身不创建会话，仍由匹配的 27／28 确认。

悬停和小地图共用当前MPQ NameStr→TBL名称；Flavie的NameStr键为navi，不能直接把键作为悬停姓名。NameStr指向TBL占位键（dummy／Dummy／not used／unused／Not used）的行视为无名：悬停标签、小地图姓名与交互标签均不绘制，服务端对白speaker匹配仍用原始键不受影响；唯一暗金仍按SuperUniques.Name显示。NPC悬停名称在remote_scene_draw→object_hint::drawNpcLabel读取MonStats→MonStatsEx→MonStats2.pixHeight，以显示位置的地面锚点定位，不跟随组件绘图偏移；原版框底为地面屏幕Y加10减pixHeight。font16的原TBL高度／逐字宽度、所选语言行高与基线、DC6底部偏移、8像素总横向留白及PL2 TRANS75底板共同决定名称布局，不再共用固定70像素／24像素高标签或TTF替换。依据为零售1.13c D2Client RVA C0D30→C0850（C0CA0读取pixHeight）→D2Win ordinal10085，以及D2MOO D2WinFont::FONT／DrawFramedText／10124；画面仍待原版对照。宿主查NPC GUID时允许同幕、已准备且步行相连的邻区，距离计算先转换为本人区域坐标，仍保持交谈6、走近7–8、9以上不回复的门槛，不通过放宽距离补偿跨区错误。

未收到27／28确认的NPC请求允许新的移动／攻击／其他交互通过原30关闭等待。服务和 Talk 菜单打开时，左击菜单外的地图在同一按键内先结束交谈，再按实际落点处理世界输入；菜单标题、边框、选项及明确的 Cancel 均消费点击。正在播放对白时，点击只结束当前台词，按住鼠标也不会因此起步。远点靠近在本地原单位路径到达后发13，资格拒绝或无回复仍可取消等待。

零售 1.13c 自动对白在每条开始时发31。初次打开按2F→31（4B9AA／4BB0F），结束当前台词时用4B230推进，再由4B32E确认新台词。4B37D–4B3C6按NPC服务菜单表和45C50的排除类决定回菜单，退出经47150→47B20发30。Akara的菜单记录允许自然结束后返回，女巫初见为2F→31(12)→31(64)→服务菜单。手动与调试AI共用会话的台词结束入口。d2decomped的`Town::take_npc_quest`也在播放开始时发31。

- **Talk 可选话题。** 零售 D2Client 49D80–49FC8 按menu2生成任务评论，标题取E1400的任务名映射及当前MPQ的数字TBL索引，缺映射用0xE8C。164在1.13c落入表的后半段，仍取3716。473B0开始本地播放，46E60结束后经4A270回Talk子菜单。Akara的Introduction经49130／48590走同一回调，普通角色用11，女巫用12。45FF0的固定表指定Cain的123／125接126，本地评论链保持本地播放，自动对白链在新句开始时提交31。d2decomped的逆向笔记同样区分本地闲谈与服务器提供的任务评论，其`apps/d2d/town/update.cpp`重实现给可选任务评论发送`QuestMessage`，零售D2X以1.13c的本地分支为准。
- **普通跳过与强制结束。** 输入表E1388和7E840有100毫秒防误触窗口。Esc／左键由7ED30进入强制回调并检查当前选中单位，右键按下由7B220消费。普通键进入7E840，Page Up／Down没有手动暂停滚动的分支。4B230先检查后续自动对白，强制结束只在没有下一句时跳过服务菜单并发30。46E60的本地话题回调不读取强制参数。D2X在图形端处理输入窗口和命中，向共享客户端动作提交结束原因。死亡、移除单位和换区沿会话失效清理对白。特殊键绑定与NPC模式12的原生中断时序仍待实机对照。
- **专用话题。** 44C00按NPC类取F37A8中首个匹配记录。D2X恢复有文本且`no_intro`清零的Introduction，49130按职业选择默认／专用台词，并处理类513／514／515的三个扩展职业特例。49F2D的商人话题属于Jerhyn（类201），47360播放254。49F64的方块条目检查凯恩类244／245／246／265／520及玩家库存中真实的`box `，47310播放338。两条均经46E60回Talk，沿当前MPQ取标题、正文和声音。d2decomped的1.14d笔记将商人条目标为Greiz，零售1.13c实现以实际类号为准。
- **物件阅读。** D2Client 7DF2F–7DF88按type2的首个槽进入7D200。menu3进入独立的头顶文字分支，D2X已排除其进入滚动框。物件读文按与NPC共用的滚动时钟结束，原7D318临时开启读文绘制，Audio Only也可见。7D389–7D3B1为127／396／20002／20131／20169设置结束确认，7CF6B在自然结束或跳过后发`31 FFFFFFFF <stringId>`。其它读文只结束本地显示。调试AI与手动输入共用读文结束入口。宿主按操作者的当前阅读记录解析哨兵，分别处理古书、Horazon日志、古代人祭坛、冰封安雅和隐形古代人提示。物件GUID保留在S2C 27和宿主阅读记录中。d2decomped的d2d重实现会使用实际物件GUID发送确认。
- **字幕。** 7BFB0读取TBL首行速度和换行，7BB40用`timeGetTime`毫秒差右移2位后乘速度，以1/1024像素为单位。同毫秒重复调用用测得的间隔插值。7BE6F以整数像素严格超过`112 + 18 × max(1, 行数 - 1)`结束。底板325×122，正文横向加16，首行基线124，行距18，字体8为FontFormal11。7BD50／7BDB5／7BE43经D2Win #10162、D2Gfx #10074和软件渲染D2CMP #10080给出各字形自底部跳过的行数和绘制行数。D2X按DC6字形源行绘制上缘、下缘和只剩末行的分支。实际像素、内嵌色码及异常／非ASCII速度首行仍待对照。
- **音频。** 7D570按DF398的数字对白ID查声音，Akara的11／12／64对应Sounds行3499／3500／3503。物件和两个专用话题的声音也按原数字索引解析当前MPQ。7E970经884D0和D2Sound #10054查询是否开始播放，音量非零且有句柄时先等播放启动，再启动滚动时钟。Text Only禁用语音。7C9F0调用88230，淡出长度取`max(4, Sounds.Fade Out)`，单位为25 Hz声音tick。890CB–8910E由当前整数增益插值至0。D2X零售对白流按40毫秒量化此斜坡，未启动的流直接取消。原生后端更新相位、最终混音与音画对齐待实机核对。

原版46910／46E00在首次本地介绍或闲谈时为全局闲谈表抽取本局台词，最多10次，按职业和私人任务位筛选，失败回退到索引2。D2X为Akara实现该筛选和本局复用，行13仅女巫参与抽取，行23要求私人任务槽6的bit0为0，保底仍用13。D2X在首次打开Akara的介绍或闲谈时为她单独抽取，使用独立随机源。其他NPC的普通闲谈仍沿当前MPQ轮播，可选话题播放期间新到的menu0和物件menu3头顶文字尚未接入。上述零售调整已静态对照本机1.13c和d2decomped。D2X普通难度单机调试女巫的首轮Akara对白按12、64显示，各发送一次31，第一幕任务流程的14条31在客户端和宿主录包中逐条匹配。

原版 1.13c Join D2X Host 的阿卡拉样本中，原客户端发送 13 时，最近一条 96 仍为距 NPC 更远的旧位置。27／28 由 D2X 宿主返回。普通单位接近按本地路径完成提交，02／04 请求资格按 `PlrMsg::sub_6FC83C60` 逐轴 ≤50 核对，13 交谈仍按原 6／7–8／9 分支。

任务提示依据为D2MOO A1Q1／A1Q4消息表、PlayerStartedGame和A1Q2_SeqCallback：介绍12、邪恶洞穴64、寻找凯恩97及menu2／65评论分别消费各自确认和进度。感叹号可能对应下一项任务，应核对实际消息和任务字，不修改存档位来隐藏正常提示。

本地D2MOO PlrMsg::sub_6FC828D0、MonsterAI、SUnitNpc／PlrIntro与任务ActiveFilter核对原流程，Diablerie鼠标消费仅作表现参考。所有文本／图形／声音读当前MPQ；服务边界见[交易](TRADE.md)与[NPC模块](../../modules/NPC_QUEST.md)。
