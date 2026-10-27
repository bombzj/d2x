# NPC交互与对白

已有NPC会话的27直接替换当前对白列表，不重新等待28或发送2F。任务确认改变对白后，NPC系统按最新权威记录重建任务及未确认介绍，可靠发布27／29后再替换交谈revision；背压保留刷新请求，不重复结算任务。初次交谈仍使用下文的27／29／28确认。原版1.13c Host确认Akara64后的27为menu2／65及尚未确认的menu0／12，随后只发29；本地D2MOO的`QUESTS_NPCActivateSpeeches`用于核对公共刷新调用。

会话按原textId保存多个待确认任务及独立介绍，不再只容纳一个任务回调。已接取的邪恶洞穴、血乌和罗达门特评论共用当前MPQ准备的`questSpeeches(quest,state)`，选择AfterInit／EarlyReturn并匹配真实NPC；因此Charsi、Warriv等也能提供相关评论，不按NPC名称补固定消息。依据为D2MOO对应NpcActivate文本链与原版Host接取邪恶洞穴后Charsi的menu2／67；其他任务及奖励后评论链仍待逐项对齐。

NPC是所连接服务端的真实type1单位，位置／模式、对白、任务提示和服务资格均由该服务端提供。中立单位不使用敌对替身；组件／原图由公共ActorAnimation／Graphics解析。

## 宿主NPC移动与地图动作

`hosting/npc_content.cpp`按当前MPQ的MonStats.AI区分Npc、Towner、NpcStationary、JarJar和NpcOutOfTown，准备Velocity、aidel、MonStats2.mS1／mS2及原AnimData时钟；DS1单位预设的MapAI节点连同动作号完整传入该出生点的MonsterRule，使用全局子格坐标。节点不按自定义半径、当前碰撞或动作号预先删减，不为缺路径的NPC合成随机闲逛。NPC的AI／模式事件和碰撞移动由同一个monsters::Actor执行，城镇门控允许这些非战斗AI；原协议仍由公共monster_replication发布；S1／S2显式朝向经Actor→MonsterSnapshot→原6B／6C动作包传递，不再统一写0。

权威入口为`server/systems/ai/town_npc.cpp`，对照本地D2MOO `AiThink.cpp::AITHINK_Fn032_Npc/Fn041_Towner/Fn054_NpcStationary`、`AITHINK_ExecuteMapAiAction`、`sub_6FCE61E0/6270/6340/64D0/69A0`及AiTactics移动辅助。Npc／Towner首次记录command10原地坐标并等待20帧；MapAI先掷66%再从全部节点抽一个，1／3为走位，2保留20次等待，4／5保留走位、command4等待及command7的S1／S2动作；缺少该模式时沿原NU50分支。动作支持来自当前MPQ，不伪造S1／S2资源。已有路线在结束前继续执行，交谈暂停，NpcStationary保持原位。Npc的任务提醒对象查询在近房间单位顺序中取首个满足FullUnitSize距离15和现有任务提醒资格的玩家，保留接近、问候18、60次冷却及command10距离16分支；Towner不执行该玩家接近分支。休眠保留MapAI及NPC的command10原点，临时command4／7及问候计数在重建AI时重新开始，不写角色档。

定点只读核对本机1.13c D2Game.dll，MD5 `978f6f74cd031bce6ecee602cd5988cc`、ImageBase `6FC20000`：AI表第32／41／54项分别为RVA `1DB40/1BC60/19880`，MapAI选择为`1BBC0`。Npc的交互辅助`19C20`在`19C62–19C67`要求交互结构非空；本地D2MOO `sub_6FCE5EE0`的该判空方向有转录错误，不能照搬为“有交互结构就跳过”。`16A30`读取全局绕行计数，不能照搬D2MOO局部零变量；执行器在实例内保留该计数。预设传递及休眠依据为D2MOO `SUNIT_SpawnPresetUnit`、`SUNITINACTIVE_*`与`DRLGPRESET_CreateCopyOfMapAI/ChangeMapAI`。

专用剧情入口为`server/systems/quests/npc_story.cpp`，状态归quests，Actor位置／模式／出生搜索归monsters，门户归travel；不由UI推导剧情。当前接入范围：

| 原版入口 | 当前消费者 |
|---|---|
| A2Q4 Init18／19、GetAndUpdatePalaceNpcState、Fn081 JarJar | 按欢迎／离城及任务状态撤出起点杰海因并准入宫殿身份；消费实际宫殿／阻挡物标记，等待合资格玩家接近、走向阻挡物、守卫后退、原碰撞搜索重定位及command10原点平移；守卫保留200帧决策门控与开门后站位 |
| A3Q3／A3Q4 NPCDeactivate、Fn032 | 交付吉德宾并结束交谈后，奥玛斯走向Init39祭坛并执行S1，祭坛沿ENDANIM转为开启；艾柯收鸟后结束交谈执行S1，再提供成功对白 |
| A1Q4笼子MONUMOD、Fn031／Fn032 | 笼子17帧事件准入真实cain1，按原半径3后备搜索；凯恩先离开出生点、生成蓝门、走向门并退场，随后准入城镇凯恩并向城镇门靠近；出生失败沿返城后备分支 |
| A5Q3解冻定时器、Fn031、Fn032 AnyaOpenPortal | 原任务20帧时钟分两次开启冰封物件、准入真实drehyaiced并移除冰封物件；安雅走位／蓝门退场后才准入城镇身份，马拉20132领奖回调可清理活跃／休眠野外身份并请求返城；红门起点来自安雅坐标+10,+5，重入依原痛苦之厅传送点资格请求重开 |
| A3Q0 Init49／50、Natalya Init52、A5Q3／Q4／Q1 | 赫拉铁力欢迎后待起点房间失活再转至终点，保留交谈；墨菲斯托死亡撤出娜塔亚，解冻撤出城镇尼拉塞克；拉苏克使用原出生搜索 |
| QUESTS_MapPresetSpawn／DRLGPRESET_ChangeMapAI | 只接原物件459／461／543的安雅／尼拉塞克／拉苏克路径缓冲；标记与NPC两种加载先后均可转交一次，安雅野外出生可消费已有缓冲；路径写回Actor恢复规则，房间重入不恢复旧路径或已撤出的NPC |

MapAI三类分派另定点核对上述1.13c DLL的RVA `35E50`（目标`59E10/59D30/5A5C0`）。Hratli／Natalya虽有任务缓冲字段，本地D2MOO及该分派均没有对应提供者，当前不自造MapAI来源。剧情状态仅属于本局，不改角色存档格式；已完成档按既有个人任务位恢复城镇状态。当前双端任务门户准备先激活目标落点房间，再读取碰撞选择落点；已准备的城镇坐标不代表房间仍驻留，等待期间不分配门户身份或提交退场。原版Cain门户在A1Q4_SpawnCainPortalOutsideTown／SpawnCainPortalInTown分别基于实际房间创建；当前双端准备仍不代表原版两端演出交错完全一致。

剩余边界包括完整ActiveFilter／PLAYER_IsBusy组合、原房间客户端列表顺序、返城门户两端生成的精确交错、退场死模式表现、完整休眠AI命令恢复及全部多人／逐随机轨迹。普通MapAI和上述专用分支已接入，原版完整演出及上述边界仍待逐项对照。救援囚犯独立逃离程序的限制见[后三幕任务](../quests/ACT3_5.md)。

## 交谈与对白

SceneController统一悬停、高亮、姓名、点击／目标锁定和释放；RemoteControl按真实GUID靠近后0x13交谈，原0x27对白列表→0x29公共标志→带NPC身份的0x28交互确认→0x2F准备及公共对白UI继续消费。D2X先保存27待确认列表，收到匹配的28才打开会话并发送2F；2F确认已有权威会话，不重发27／28。一般私人任务刷新使用28/type6，不把玩家GUID编码成NPC身份。0x31确认实际原stringId，已确认话题不重复提交；0x30关闭，旅行0x38 action=0等原幕／位置结果。

当前TBL对白首行数字按a1npc SPEED元数据移除，通用字符串查询仍保留原文。hosting比对当前MPQ的NPC文本与TBL时同时处理UTF-8及原单字节省略号／弯引号，避免遗漏阿卡拉女巫介绍；对白编号仍由实际TBL匹配得到。原0x27消息的menu=0自动对白依次显示未确认集合；menu=2为可选任务评论，通过Talk打开，不在每次点击NPC时抢先弹出。任务原表中的menu=1经QUESTS_InitScrollTextChain转为回包menu=0，不能直接发送为原版的紧急交互菜单。普通闲谈由客户端读取当前MPQ，不占27的任务／介绍槽，也不发31任务确认；D2X通过Talk里的Gossip轮播。首次介绍和任务自动对白由服务端消息及PlrIntro旗标决定，不增加客户端“已问候”状态。NPC0x8A提示用原npcalert与MonStats2高度；它不是本地任务资格。原字幕时序、重播间隔及提示精确像素定位尚未完整认证。

原0x27有8个物理消息槽，但当前1.13c D2Common `TEXT_CreateTextListFromMessageList`要求count小于8；hosting拒绝超过7条的事实，D2X接收端也按0..7校验，避免掩盖原版断言。自研NPC按原任务回调集合及未确认的首次介绍组织列表，分别接受31确认，不附加闲谈。依据为当前DLL的line274断言、本地D2MOO `D2Common/src/D2Text.cpp`及原版Host阿卡拉先64任务、后12介绍且均为menu0的录包；已有任务进度的角色也可只有12介绍。

NPC服务／Talk菜单与交易邀请共用[OriginalMenu](../../../src/presentation/graphics/original_menu.hpp)，读取当前MPQ boxpieces、font16与Sky PL2：金色标题、蓝色悬停、测宽／居中文字及行命中区域。样式依据用户提供的Warriv、Akara与等待交易原版截图；菜单锚点及全部状态仍待逐像素对照。原服交易消息与结算参考见[联网](../../modules/NETWORK.md#原服物件与交易观察)。

普通02／04单位靠近与交互靠近共用Path.cpp初始化的nStepNum=0及PathMisc::sub_6FD5DB70停止判据，使用当前MPQ双方原始SizeX和D2Common_10399，逐步停止预测路径；距离尺寸与碰撞pattern分开，不用小单位统一cross的size2替代真实size1。服务端普通敌对单位／其他玩家的04目标也保留同一距离0到达条件。这个单位距离0包含足迹大小，并非中心重合。13菜单资格仍为≤6，7–8由服务端走近后重试；不把6当作普通04路径终点。

客户端在自己的当前路径到达后提交13，直接点击已有交谈范围也用当前显示位置判定请求；不等稀疏18／95／96位置采样。显示位置只授权发送意图，服务端按真实位置／资格决定27／28、菜单与奖励。请求绑定、目标GUID和新移动revision仍用于取消旧靠近；没有显示路径位置的调用使用已有协议采样。移除requirePositionUpdate历史等待状态，收到匹配28才打开会话的流程不变。

悬停和小地图共用当前MPQ NameStr→TBL名称；Flavie的NameStr键为navi，不能直接把键作为悬停姓名。NameStr指向TBL占位键（dummy／Dummy／not used／unused／Not used）的行视为无名：悬停标签、小地图姓名与交互标签均不绘制，服务端对白speaker匹配仍用原始键不受影响；唯一暗金仍按SuperUniques.Name显示。宿主查NPC GUID时允许同幕、已准备且步行相连的邻区，距离计算先转换为本人区域坐标，仍保持交谈6、走近7–8、9以上不回复的门槛，不通过放宽距离补偿跨区错误。

未收到27／28确认的NPC请求不封锁整个世界手势；新的移动／攻击／其他交互可通过原30关闭等待并提交新的请求。已确认会话及实际菜单仍消费输入。远点靠近在本地原单位路径到达后发13，服务端资格拒绝或无回复时仍可用新手势取消等待；等待超时保留。点击取消、跨边界NPC靠近及姓名画面仍需完整原版对照。

2026-10-12用户对照中，原版1.13c客人连接D2X宿主，Akara GUID40／class148，位置(5533,4687)。原版客人362.950秒发13，宿主362.952秒发27／28，当次最后位置为(5531,4689)；原版提交13前最近96为362.601秒、位置(5528,4692)。该现场说明原版客户端不以最后96到达NPC作为发13的门槛；27／28来自D2X宿主，不能当作原版服务端时序。当前源码统一普通单位路径停止并移除旧96等待。原02／04请求资格按PlrMsg::sub_6FC83C60逐轴≤50检查，不能误用10399的放大单位距离；13交谈仍按原6／7–8／9分支。

任务提示依据为D2MOO A1Q1／A1Q4消息表、PlayerStartedGame和A1Q2_SeqCallback：介绍12、邪恶洞穴64、寻找凯恩97及menu2／65评论分别消费各自确认和进度。感叹号可能对应下一项任务，应核对实际消息和任务字，不修改存档位来隐藏正常提示。

本地D2MOO PlrMsg::sub_6FC828D0、MonsterAI、SUnitNpc／PlrIntro与任务ActiveFilter核对原流程，Diablerie鼠标消费仅作表现参考。所有文本／图形／声音读当前MPQ；服务边界见[交易](TRADE.md)与[NPC模块](../../modules/NPC_QUEST.md)。
