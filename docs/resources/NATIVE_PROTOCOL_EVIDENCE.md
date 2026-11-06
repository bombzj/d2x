# 原版 1.13c 协议与任务依据

本页保留原版调用链、字段与版本冲突证据，不维护 D2X 对比计数、修复尝试或构建／运行流水。基础来源为本地 `reference/d2moo`；转录与零售代码矛盾时以注明身份的 DLL 定点核对为准。源文件、版本及工具入口见[资料来源](THIRD_PARTY.md)。未核实边界不因删除历史报告而视为已解决。

## 移动、可见性与入局

| 原版入口 | 可确认的语义 |
| --- | --- |
| `PlrMsg::Rcv0x04_RunToEntity` | 跑向单位保留逐轴50子格请求资格；客户端预测位置不代替服务端检查 |
| `MonsterMsg`／`PATH_GetLastPoint` | 普通NPC坐标移动消息取实际路径末点，不将不可达的意图目标直接作为路径末点 |
| `MonsterMsg::sub_6FC659E0` | 单位目标消息取决于接收者是否已知目标；目标可为玩家、怪物、物件、物品、出口或尸体，不能只查怪物表。67／68的零售分支与部分D2MOO转录条件相反，具体证据见[怪物表现](../gameplay/monsters/PRESENTATION.md) |
| `PlrMsg::sub_6FC81D20` | 4C／4D仅在技能flags4或旁观者条件下发送；目标在接收者房间内使用4C，否则4D。`sub_6FC81E10`对普通0F／10排除本人，不应无条件回显普通本人动作 |
| `SUnitDmg`／`PlrModes` | BL／GH／KB／死亡有独立模式分支；软生命action19由独立0x8000标志控制，不能对每个动作受击再无条件合成19。原玩家模式19沿`sub_6FC81F60`发送wire20同步本人击退 |
| `Clients::sub_6FC31EF0` | 首次真实玩家加入房间后，按顺序发送本人15(flag1)和7E；不能把普通移动按距离猜成传送 |
| `Game.cpp`的PLAYER_SPAWNED／CHANGING_ACT分支 | 真实加载完成04之后执行`UpdateInventoryItems(bRefresh=1)`，存在独立48刷新；04前库存初始化及佣兵INVMODE_First是不同来源 |
| `PLRSAVE2_ReadSkills` | 设置基础技能后发送94，再经`sub_6FD14C30`刷新被动技能；没有无条件为94中全部技能再发送21的循环。`SKILLS_GetBonusSkillLevel`包含装备加成 |
| `Clients`快捷键初始化 | 仅发送`0 <= skill < Skills行数`的有效项；空槽初始化与后续主动清槽不是同一操作 |
| `SUnitMsg`／`Objects` Init08、Init14 | 首次51包含物件mode；0E由真实更新标志和物件生命周期产生，不能因每次51都机械补0E，也不能仅凭mode数值未变删除合法更新 |
| `MonsterSpawn`／`MonsterMsg`／`SUnit` | 57携带怪物身份信息，包括名字种子、前三修饰器和冠军字段；出生／祭坛更新与空间AC重新指派需区分。完整flags解释仍需按消费者核实 |
| `Arena` | 65属于初始分数及dirty-score更新，不能以固定零值或包数量代替实际计分规则 |

## 任务标志、物品与声音

| 原版入口 | 可确认的语义 |
| --- | --- |
| `QUESTS_StatusCyclerEx` | 计算指定任务，按幕、角色状态过滤并携带任务专用计数；不等价于每次进度变化重发全部任务 |
| `QUESTS_SendCurrentFlags`／`QUESTS_StatusCallback` | 公共29、私人28、完整查询52和指定任务5D有独立来源；普通状态基线不能吞掉flags2／16等显式通知 |
| `QUESTS_UseClueItem`／`ACT1Q4_SendStoneOrderToClient` | 50/4来自实际读取译文bkd；一般任务快照或查询不能按历史阶段隐式重播或提前消费石柱排序随机流 |
| `ItemMode`任务消耗品 | 技能书ass、生命药剂xyz、抗性卷轴tr2成功授予并更新任务位后调用`QUESTS_SendLogUpdate`，再删除物品；失败、重复使用及洗点令牌不是同一通知来源 |
| `ACT2Q3_UnitIterate_UpdateClient` | 对符合第二幕条件的接收者，变暗53(5,0,1)后发送5D(quest10,flags16,state0,counter0)。结束日蚀与普通昼夜同步另有来源 |
| `ItemMode`书本 | 施法扣次数使用3E/stat70；装填、合书、卷轴删除、TARGETING清除和7C法术结束有各自顺序，不能将所有QuantityChanged统一编码为同一动作 |
| `ItemMode`鉴定／回城 | 合法来源与目标下空鉴定书仍有7C通知；回城pSpell的通知位置与外层扣费不同。完整TARGETING集合、失败pSpell、非背包／腰带来源需逐分支核实 |
| `ItemMode`普通／光标拾取 | 普通拾取先附着声音1再执行任务拾取回调；光标拾取先任务回调再附着1；共享金币只向实际拾取者附着 |
| `SkillItem` EventFunc17／28 | 击杀回蓝／回血在属性非零且资源未满时更新资源并设置MPQ stealmana／steallife覆盖图，零售编号152／151；满资源不发。伤害转法力EventFunc13不能由此推导 |
| `SUNIT_AttachSound` | 单位附着声音槽、单位更新阶段的2C发布及客户端播放是不同时间点；同槽覆盖与接收者资格必须保留，不能统一挪到领奖时补声 |
| `QUESTS_ActChange_HirelingChangeAct` | 瓦瑞夫／马席夫首次跨幕受幕完成位和busy控制，61/2、61/3与私人任务更新及实际旅行有顺序；C2S48解除busy，重复往返不重播首次电影 |

### 任务旁白调用位置

| 调用链 | 原版依据 |
| --- | --- |
| A1Q2 MonsterKilled | 奖励待领／队伍资格／CompletionFlag遍历后附着34，之后创建状态计时器及公共标志 |
| A1Q6 MonsterKilled | 击杀者资格、额外宝石及其余资格遍历之后，建立计时器再附着33 |
| A1Q3 ItemPickedUp | 马勒斯成功拾取的36以CUSTOM2防重；v96任务槽3的CUSTOM2为0x40，历史位独立于当前任务阶段 |
| A1Q4树操作／出生 | Operate12生成bks前附着45；`SpawnCainInTristram`出生成功后向先前开笼玩家附着48；出生失败不等价于播放成功 |
| A1Q4石柱操作 | 无实际bkd时按本局尝试计数的模64条件附着39；提示声音不表示成功激活石柱 |
| A1Q5 Callback08 | 女伯爵资格／声音37在`SpawnTowerChestMissiles`之前；原GUID历史名单与当前区域接收者资格不能用通用完成标志替代 |
| A2Q1 MonsterKilled | 公共标志及资格后，在`DetermineSkillBookDropCount`迭代中附着50，随后建立计时器并掉技能书 |
| A2Q3祭坛操作 | 物件模式、环境53及FX6之后计算护符数、赋资格并附着52，随后公共标志和掉落 |
| A2Q5 Callback08／Timer_StatusCycler | 击杀触发FX7；51来自任务计时器，对当时仍在74区的合资格角色附着，后续计时器推进状态4。`QUESTS_CreateTimer`的timeout3与20帧任务时钟相位有关，不按现实秒数猜测 |
| A3Q1 ScrollMessage／NpcDeactivate | 交书记录角色身份，关闭艾柯交谈时核对该身份再附着67；刺客／德鲁伊被排除，不属于拾取旁白 |
| A3Q3／A3Q4 ItemPickedUp | 吉德宾65与玉质小人72分别使用CUSTOM5(0x200)／CUSTOM2(0x40)历史位，不能由任务阶段推导已经播放 |
| A3Q5／A3Q6／A5Q1／A5Q2 | 议会64、墨菲斯托66、申克80、救人81由各任务资格／回调产生；申克另有FX15，救援声音与状态3有顺序 |
| A4Q2／A5Q6 AttachCompletionSound | 合资格本人的5D flags2、state0、counter0先于声音75／83附着 |
| A5Q6 Callback08 | FX19在bNotIntro判断之前，之后才进行状态4、资格、5D flags2／声音83及baalfx control出生；旧完成局也不能因此省略该FX |
| AiThink的泰瑞尔／衣卒尔幽灵分支 | 54／74由距离、交谈状态及退场资格触发，不是击杀或飞弹结束；原单位距离阈值为12／5，退场进入模式0，不等于凯恩的直接DD |

声音33／34／35／36／37／39／45／48／50／51／52／54／64／65／66／67／72／74／75／80／81／82／83有上述原服务端来源；38／40／41／42／43／44／46／47／55／56／76／77／78／79沿客户端首次入区路径，不能按缺少S2C补发。未列出的编号不由此推定存在或不存在生产路径。完整受众、计时器相位及多人交错仍须对应原调用链确认。

## 关键NPC与场景

- 凯恩：D2MOO `MonsterIds.h`区分野外cain1/class146与营地cain5/class265。Fn031沿命令3接近蓝门，调用`ACT1Q4_SpawnCainPortalInTown`建立城镇出生请求后才置野外凯恩DD。`SpawnCainInTown`仅在出生成功后设置spawned并清请求；ChangedLevel入营地及Init54均能消费请求。`AITACTICS_WalkToTargetCoordinates`经默认路径101转为TOWARD_FINISH／13，不是固定Straight／7。
- 关键NPC：`QUESTS_SpawnCriticalMonster(bSpecialInit)`传递原ISRESURRECT保留资格；城镇SaveMonsters=0不代表所有剧情NPC都可丢弃。当前MPQ安雅Restore=1、Harrogath SaveMonsters=0，完整依据见[NPC交互](../gameplay/npc/INTERACTIONS.md)。
- 奥玛斯：A3Q3的NpcDeactivate → Fn032 → SetAltarMode驱动放置演出。服务端允许交谈不能直接证明原客户端在全部演出阶段允许选取。
- 墨菲斯托桥：当前MPQ InitFn45及SizeX识别桥；Quests每24帧检查红门已开、同房间及D2Common_10399距离小于18，推动桥0→1→2。该距离不是欧氏距离，动画结束不能单独代替通路确认。
- 混沌首领落点：本机1.13c D2Game MD5 `978f6f74cd031bce6ecee602cd5988cc`，RVA BC050／BC0A2确认搜索范围100、size3、mask0x3F11；D2MOO Operate54／55／56所传13／14／15不是其助手实际使用的搜索范围。详见[后三幕任务](../gameplay/quests/ACT3_5.md)。

## 佣兵属性及版本冲突

`SCmd`绝对属性按值小于255使用9E、小于65535使用9F，其余A0；负值按uint32表示仍为宽值。`MONSTERAI_SendMercStats`由载入、雇用／复活、升级等调用，不是普通每帧生命再生入口。7F生命比例与A0的8.8生命属性不同；空间0A／重新AC和拥有者面板81／属性基线不能混为同一生命周期。9B死亡报价及复活清除也是事件来源，不是活佣兵入局必发空值。

经验增量存在定点核对冲突，必须保留版本边界：所查D2Game.dll SHA256为`24B293B86F95BEA0228826C502EA04737BA21B91CA9832E52AAC4B11FEF14F53`。RVA8BB10内8BB3E计算EDX-EDI；8BB40比较65535后的sbb／neg符合增量小于255用A1、小于65535用A2、其余A0的阈值。但8BBFE写EDI而不是差值EDX。经验调用处DDD1D读取旧经验到EBX，DDD24算新值、DDD47写回，DDD69把新值给EDX、DDD6B把旧值给EDI，DDD73调用8BB10。该大增量分支与D2MOO转录及预期消费存在冲突；文件补丁身份和实际大增量执行未闭合，不据此猜改协议。

## 使用边界

### 其他原版调用依据

- C2S目录：零售1.13c长度表RVA ABD8与回调表FA078用于核对合法消息；39／45已禁用，2C／64／65无有效长度。3D门高亮为固定5字节，D2MOO按同幕、每轴10格及原矩形占位处理已开门2／5状态，不等于开门操作。详细目录见[消息速查](../modules/MESSAGES.md#原版c2s补齐与版本边界)。
- 初始角色：`PLRSAVE2_ReadSaveHeader`及`PLRSAVE2ERROR_NEWBIE_SAVE`允许带INIT位的仅头部v96存档，按CharStats准备新人物，不能先要求完整固定段。存档入口见[存档](../modules/SAVES.md)。
- 玩家交易：`PlrTrade::sub_6FC90AE0/sub_6FC90BE0`取消开放交易时发送92/type0／玩家身份，并以新GUID重建个人库存。原件IFLAG_ITEM、撤销镜像9D/action5／mode4/page2／QUANTITY及77/6→77/14顺序分别有来源；成交复制根物品与孔内项，不能只移交旧身份。详见[玩家交易](../gameplay/items/PLAYER_TRADE.md#原版参考与缺口)。
- 敌意：`PARTYSCREEN_ToggleHostile`／PlayerList位8，冷却拒绝按本人5A/type16→双方8C→本人声音的顺序；重复开启也可能进入拒绝分支，60秒冷却按真实时间，不用游戏帧替代。名册75、队号8D和相对关系8B／8C是不同发布来源。
- 玩家死亡：`PLAYER_ApplyDeathPenalty`之后还有`CORPSE_Handler`；玩家归功死亡的DT罚金与DEAD阶段钱包掉落、尸体及耳朵必须分别核对。金币与耳朵的9C动作不同；金币寻位D2MOO的x/y笔误已由本机D2Game RVA11D3F定点纠正。见[玩家死亡](../gameplay/characters/PLAYER_DEATH.md#自研宿主pvp死亡)。
- 怪物模式：本机1.13c RVA F36C4–F3756确认普通追击68、环绕67；普通攻击6B／6C方向字段在对应模式保持0，不从atan2合成。萨满复活沿4C/181、同GUID零点S1、6D，不应因复活额外制造软命中0C／生命比例AB或重新AC。`AiTactics::sub_6FCD0E80`将实际环绕半径交给SetVelocityParams，Fn013传3；并非所有AI都固定3。
- 物件动画：箱子／祭坛的操作模式必须在操作发生时冻结；原ENDANIM不等于再次操作，迟到观察者收到当前51而非重播历史0E／4D。原Operate16活板门第一次0→2，后续操作才warp；同房间底层出口不能绕过盖子操作。
- 第三幕任务：A3Q2_Callback03进入大沼泽开放本局开场，Callback00按本局状态与个人STARTED决定543；543–548是说明，不等于交付三器官。A4Q3 Operate49在熔炉0→1时立即消耗mss，ENDANIM才变2；材料消失不能独立判定为丢失未放置。
- 凯恩蓝门：当前MPQ class189的OperateFn0／Selectable全0，是不可选演出门；玩家回城门class59是另一对象，不因画面相近混用身份。

原包类型／次数相同不是逐事件一致性证明；不同地图、人数、路线和观察者不能按总数补包或删包。需核对触发、字段、受众、次数及因果顺序。单机本地保存不走TCP的B3角色回传，其他Host玩家的20／队伍关系也不能当作单机本人漏包。未知编号、未触发分支及未闭合版本冲突不因当前代码存在消费者而视为认证通过。