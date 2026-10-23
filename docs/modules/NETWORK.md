# 联网模块

更新：2026-10-22。本页维护原协议、客户端副本、原服参考及明确缺口；[基线](../../BASELINE.md)维护实现范围，[总计划](../architecture/MULTIPLAYER.md#21-统一状态口径)维护状态口径与功能顺序。原服与自研宿主共用RealmSession／Remote*客户端，传输为TCP或内存字节队列。原服常规施法不回送本人4C／4D，也不以73重复创建所列地面程序；具体技能、原服条件及边界见[女巫技能](../gameplay/skills/SORCERESS.md)。亚马逊30项、原Clt程序及7A／9D怪物装备已有接入，多重箭／导引箭／诱饵／女武神／闪电之怒的原服参考与缺口见[亚马逊](../gameplay/skills/AMAZON.md)。既有未消费包和引擎启动限制仍需核对，不宣称全协议或全技能已对齐，也不按构建／自研运行划分完成等级。

PD2区分CN远端与自有城镇宿主；`--pd2-local`的资源、协议和存档边界见[PD2城镇宿主](../architecture/PD2_COMPATIBILITY.md#自有pd2城镇宿主)。CN远端的PlayPD2／--client-profile在挂载前绑定固定CN构建、资源和认证文件，由共用frontend组合RealmSession和Remote*，不实例化EmbeddedRealm或本地角色存储。当前CN服务已接受版本认证、账号登录、Realm启动和既有Ladder角色选择，可显示角色原图、进入大厅、取列表并返回选角；列表与游戏环境按角色Ladder状态核对。CN建房在原字段后追加GS／lootAllocation两字节；已获MCP建房和加入成功回复，按BH证据改用4001后，已完成游戏握手／初始同步并实际显示罗格营地、本人、NPC和HUD，正常退局返回选角；已核对营地与鲜血荒地往返，其他区域和持续战斗未认证。CN证明采用合法原MPQ的key／owner来源及独立编码，原配置保留旧认证语义。新账号注册尚未重新联调；建角按原MCP 0x02（可选专家级0x04、天梯0x40）、删角按原MCP 0x0A编码开放、成败以该服务回复为准，房间详情仍关闭，PD2库存、城镇交互及基础战斗输入按当前确认的来源／上下文开放；属性加点已按CN控制分界开放，学习和属性分配均等待目标值增加与余点减少。未接入操作继续拒绝，已知分帧但未接入语义的世界消息记录未消费诊断并继续会话。范围见[兼容设计](../architecture/PD2_COMPATIBILITY.md#当前代码接入范围)，算法、MPQ来源限制及原客户端大厅崩溃证据见[DLL取证](../resources/PD2_CN_DLL_ANALYSIS.md)。

## TCP/IP直连与自研宿主入口

TCP/IP Game现在使用原版直连D2GS生命周期，默认只有4000端口，不先连接BNCS／MCP。Host／Join各自从本机`--host-saves`目录选择角色；局前选角复用内存MCP和同一原图角色页，MCP不对远端监听。入局后的RealmSession／Remote*、协议副本和权威内核与Single Player／原服共用。

- Host：首页TCP/IP Game → Host Game → 本地角色 → 已解锁难度（只有普通时自动选择） → 建房／加载。监听`0.0.0.0:4000`，单房容量8、无密码或等级限制；本机角色仍使用内存游戏连接。宿主退出房间先发送客人的保存和结束包，再移除宿主；客人不继承房间。
- Join：首页TCP/IP Game → Join Game → 目标IP → 本地角色 → 游戏端口直连。默认IP为`127.0.0.1`；不同机器填宿主实际IPv4，不能填`0.0.0.0`。不展示宿主角色或MCP大厅。退局／失败重新打开本机选择器，不请求远端Realm票据。
- `--host-lan <本机IPv4>`预先监听该接口；UI首次Host默认全部接口。`--lan <目标IPv4>`打开本机角色页。双方用`--host-saves <本机目录>`隔离角色，用`--game-port`覆盖游戏端口；`--realm-port`只保留内部MCP端点标识，不产生TCP监听。与原版互通使用4000；同机已有D2GS占用4000时必须选择空闲端口做D2X隔离验证，不停止已有服务。
- 客户端收到AF后发送6A查询，B2游戏信息以FFFF结束；取得gameId后发送68。收到02后先上传本地v96存档（6C），再发送6B进入游戏。服务端B3回传完整存档，应用交给hosting的本地CharacterStore校验身份、校验和及文件版本后原子替换；network与UI不解码／写盘。传输严格限制小于8192字节，不新增私有包或存档段。
- 自研宿主检查容量、同名角色、职业／姓名及难度资格。客人断线或正常关闭后宿主保留独立`tcpip-recovery/character-<时间标识>.d2s`恢复副本，不自动回写客户端角色。完整接收前不会替换本机D2S；本机写盘失败保留字节和租约，用户关闭错误提示后重试，不按帧反复写盘。

共享房间按25Hz推进，客人失焦／ESC不暂停其他人；Single Player一人内存实例保留暂停。独立PvPGN入口继续使用D2CS／D2DBS账号、票据和保存，不使用TCP/IP上传，部署见[PvPGN服务端](../development/PVPGN_SERVER.md)。现有内容范围仍见[基线](../../BASELINE.md)。

依据：本地D2MOO的CCmd系统消息、Game的JOINLIST／OPENCHAR／保存回传及D2PacketDef布局用于解释流程；包号／长度以当前1.13c DLL为准：D2Client RVA 0x14630发6A，0x14D90发6C（partSize+7，含忽略的尾字节）；D2Game RVA 0x2BA73发B2／53字节，0x2D1E2发B3／partSize+7，0x8C4B0发B4／5字节入局错误，0x2E3CE同样回送角色加载错误。D2Net长度表0xA900的B4项为5，当前目录补齐该项；RealmSession立即停止入局并保留u32原错误码，TCP/IP预检码6／15／16分别指游戏／姓名资格、满员、版本不符（D2Game RVA 0xCAD60实际检查版本13）。D2Net的实际端口为4000。参考代码不加入源码或替代MPQ内容规则。

局前资源仍为当前MPQ的TCPIPscreen、3WideButtonBlank、PopUpOkCancel2、CancelButtonBlank和textbox2，文字TBL 5116–5124；难度选择复用PopUpLarge、MediumButton及TBL 5156–5154，不声称该弹窗布局已逐像素对照原版。七职业／资料片角色沿原存档支持范围，支持普通／HC并隔离房间，死亡HC及Ladder仍拒绝入场；原版互通的观察条件及缺口见下节。

## TCP/IP原版互通参考与缺口

待核实：2026-10-19用户以原版1.13c加入D2X Host后，报告两名女巫在城镇入场时完全重叠。需要核对入场落点搜索、已有玩家占位及碰撞登记／发布顺序；尚未与原版Host的相同入场条件配对验证，不据此认定原版绝不允许重叠。本项先记录，未修复。

原版Join从第一幕洞窟9管理旅行到第二幕下水道47时，曾在加载完成包04处Halt（返回点`6FB5E061`）。实际安装的1.13c D2Client MD5为`f5860c629d309b8fc1f96174babcf633`；RVA `ADFE0`消费者要求本地玩家已挂入房间，`AE057`为断言分支。旧投影在05／03后直接发04，玩家定位15仍在后续TravelFact中；同时只清空宿主可见缓存，没有向客户端撤销旧单位和房间。首次只提前定位仍发生D2Net清理竞态，不能据此将所有故障都归因于04的断言。

换幕投影现先发送旧可见单位0A／房间08，再按`05 → 03 → 53 → 新房间07／出口09 → 本人15(flag=1) → 04`推进；TravelFact不重复定位，房间引用去重。04由初次入局／跨幕共用的发布入口等待完整原近邻物化及本次房间人口／恢复完成，不再紧接03／15发送或预制进入入局包；只读就绪检查不推进生成／随机。03的末尾物件种子来自该局ObjectControl初值，整个实例稳定，不能固定0或随物件控制流变化。本人单位沿用原GUID，不重新发送59／0B。依据是D2MOO的`LEVEL_ChangeAct`、`CLIENTS_SetRoomInClient`、`sub_6FC81650`／`sub_6FC81CA0`，并以2026-10-19原版1.13c Host／D2X Join经真实传送点从第一幕到鲁高因的消息独立核对：旧08／0A先于05，新03／53／07后发送本人15(flag=1)，最后04；原版还在加载阶段发送单位动作／物件等消息，不要求它们都位于04之后。D2Client RVA `AC3D0`、`ADB50`、`AF640`及D2Common ordinal11099／RVA `3CBE0`、ordinal10868／RVA `3C990`分别核对撤销房间、加载幕和定位消费者。原1.13c就绪门控与客户端04消费的定点补证见[资料来源](../resources/THIRD_PARTY.md#原版113c静态与开发对照证据)；复活／死亡换幕、八人交错及异常断线的完整组合仍待核对。

用户手动运行原版1.13c Host／D2X Join：首次选择aaa收到B4，旧目录缺少该包，显示Undefined packet ID 180；补齐5字节长度与拒绝码提示，不修改原版准入规则、版本字段或D2S编码。用户随后换角色确认Join成功。该现象符合原版禁止同一房间中角色重名的检查（忽略大小写）；首次响应的u32错误码未采集，不将推断写成已捕获码6。该次观察只确认加入；反向互通及退出的原版现场见下文，完整游玩／保存组合仍有缺口。

2026-10-16用户运行D2X Host／原版1.13c Join，ShotSorceress在场景显示前崩溃。原版当日日志三次均为D2Common RVA `0x4E1DD`读取技能表空指针；22:47两次现场对应宿主诊断连接3／5。最后一次栈中保留的`9D0620006A000000006000000010008000652402301606078280F0000605FF01`与宿主sequence855的装备包一致，其前sequence816发送`210001600000000000000100`，即Attack的基础等级0、偏移2移除标志1。原1.13c D2Client RVA `0xAC910`把偏移2传给D2Common ordinal10302／RVA `0x52BC0`的AssignSkill；基础等级0且标志非零会删除节点。左右手仍引用被释放的默认Attack，装备回调经D2Client RVA `0x81B90`读取当前技能时崩溃。装备包是触发后续读取的位置，本次证据没有证明其物品位流损坏。

产生该消息的源码原因是`attributes/calculation.cpp`将`rules.innateSkills`并入有效技能输出，`rank_sources.cpp`为未学通用技能提供原生1级，而发包器只取持久已学等级，组合成基础0、bonus1及移除标志1。原版客户端现场因此暴露默认Attack节点被删除的问题；自有客户端曾忽略移除标志并直接合成base+bonus，不能作为原版消费者的参考。

`native_character_wire.cpp`源码已将普通21更新的移除标志改为0（与本地D2MOO的Skills／ItemMode／SUnitMsg调用一致），并复用由当前MPQ的`loadInnateSkillIds`准备的`commonSkills`，在94快照、21初始及增量投影中补齐Attack／CharStats通用技能的原生基础1级；持久角色的已学技能表不变。原1.13c D2Common ordinal10506／RVA `0x529E0`先添加Attack再遍历CharStats的十个基础技能，AddSkill在RVA `0x52812`写基础等级1；94消费者按绝对基础等级调用AssignSkill，不会重复累加这些初始化节点。已学职业等级维持原值，物品授予的未学技能仍可保持基础0，等级变化继续来自装备／状态。原DLL身份见[资料来源](../resources/THIRD_PARTY.md)；原版重入及完整装备／技能组合仍待核对。

用户随后确认原版Join已进入场景，但2026-10-16房主关闭后原版在23:48:41崩溃。宿主消息sequence1129–1133为客人1194字节存档的五个B3分片，1134及1138为两次06，中间没有B0／05；故障为重定位D2Net的RVA `0x7443`，接收线程访问已为空的全局收包缓冲区。两次06分别来自`NativeRealmService::close`结束房主客人和随后`LanRealm::close`，均遗漏原结束序列。这是原版客户端接收D2X宿主消息的现场，不是原版服务端的结束消息依据。

退出顺序依据取自2026-10-12原版1.13c Host／D2X客户端的实际消息：sequence1042–1049为`B3… → B0 → 05 → 06`，随后新进程重入成功。原1.13c D2Game RVA `0x2DD00`把05加入游戏缓冲，调用`0x2D790`发送存档，再在`0x2DD18`缓冲06、`0x2DD2C`直接发送系统B0；实际线上顺序不能按这几次函数调用顺序直译。D2Net在RVA `0x6F26`清空接收缓冲指针，再在`0x6F30`置退出标志，接收线程的故障访问与结束后再次到达数据的清理竞态吻合；仅据现场不能宣称原版所有断线竞态均已排除。

源码将TCP/IP结束投影统一到`NativeRealmService::endGame`：保存／退役成功后按B0、05、06发送，只有原阶段为Entered的连接发送一次；房主结束客人、主动69和LAN关闭复用此入口。后续LAN关闭见连接已Closed，不再追加06；应用shutdown的TCP客人由LAN统一保存，避免先全局checkpoint、再LAN checkpoint重复回传。本地内存连接保持原05、06、B0顺序，异常断线恢复副本与原存档编码不变。原版客户端的完整关闭／异常断线组合仍待核对。

未覆盖高难度弹窗、损坏／大存档、磁盘故障恢复、异常断线、八人、跨机器或Linux；也未重新认证PvPGN后端。没有新增测试脚本或专用程序。

## 流程与入口

普通启动：主菜单 → Battle.net → 登录或注册 → Realm → 服务器角色 → 大厅公告 → 点击创建或加入游戏 → D2GS 协议加载 → 重建当前幕活动地图 → 服务端世界显示。Single Player在组装入口连接预认证MCP，使用同一角色列表及GS客户端；自研宿主读写D2S，客户端不读取存档，也不启动旧GameSession。

| 页面／操作 | 当前源码 |
| --- | --- |
| 登录／注册 | 原 MPQ 图形、字体和文案；校验两次密码一致，SID 0x3D 成功后自动 SID 0x3A 登录；提交后记忆账号密码，下次进入登录页可直接Login；修改用户名立即清空当前和已记忆的密码；拒绝／超时后保留输入以便修改 |
| Realm | 原版首次自动选择匹配的配置项，未找到则显示列表；PD2 CN默认选返回的第一个Realm，直接显示角色；Change Realm重新取列表和票据 |
| 角色 | 服务器列表分页；七职业创建、资料片固定开启、非 Ladder；Hardcore 先显示原警告；删除先确认，成功刷新；死亡专家与未知状态角色不能进入；存活HC名称与Hardcore标签为红色，死亡HC名称仍为红色、标签置灰，状态来自已解析的服务器预览。仅原服显示右上角Realm控件，Single Player及LAN Host／Join露出原MPQ角色页背景 |
| 大厅 | 默认原黑色公告区域，Create／Join 才切换表单，Cancel／表单 ESC 返回公告；SID 0x0A 后提交 0x0C 初始频道请求，0x0F 的 info／error 只读公告最多保留32条；不伪造服务器 MOTD，不提供频道聊天操作 |
| Create | 名称／密码／说明、人数 1–8（默认8）、等级差 0–99；等级限制默认关闭，关闭时提交99，手动开启沿当前等级差；普通／噩梦／地狱依据服务器 native 进度解锁；成功自动取票加入；CN另显示CN副本原中文TBL的GS1–GS9原按钮列表，初选取配置默认值，本次选择随建房提交，原版表单不显示该列表 |
| Join | 点击 Join 请求真实房间列表，查询期间逐条显示名称／人数；选中通过 MCP 0x06 查询详情。同一房间可双击加入，右侧按原版截图显示经过时间、等级范围、金色姓名及灰色等级／职业，玩家列表可滚轮翻阅。再次点击 Join 可替换进行中的列表查询；可取消或按名／密码加入，网络复验输入后退役未完成列表。详情独立超时，迟到回复不覆盖新选择、不阻塞加入 |
| 等待／返回 | 独立 worker 持续收包／心跳，建局队列显示位置；取消列表返回大厅；创建／加入／连接游戏／握手等待取消时关闭旧 MCP／GS，保留账号连接并重新取服务器角色列表；已登录游戏的加载取消提交原0x69并有界等待退局；大厅 Quit、正常退局重新取票返回选角 |

`ProtocolReady`须原服加载完成、幕／难度／种子和本人GUID可用、本人单位名称／职业与所选服务器角色一致且坐标已分配；场景另须 `nativeMapReady`、`playerDisplayed` 与活动碰撞准备成功。左键提交移动或点击真实地图对象／NPC，R或底栏按钮切换跑／走；跑走偏好在换区／换幕后保留。Esc先结束对白或关闭交谈／传送点面板，再打开退出菜单。NPC对白和菜单复用既有字体／菜单绘制；回城门创建沿现有物品技能请求入口。新闻、广告、频道／聊天、账号设置、Ladder、转换角色和影片等非主流程入口暂缓。

本人移动不应依赖每步S2C坐标。`publishMotion`沿Game外层10／20帧资源检查条件，PlrMsg计四次检查而非四帧；资源变化沿18／95／96，平稳移动经过四次资源检查再按坐标差决定是否发96。客户端自动地图、房间裁剪、界面投影和相邻步行地图切换使用已有逐帧碰撞路径，服务器坐标保留为权威校验。原版回包频率及对照条件见[连续移动](../gameplay/world/AUTOMAP.md#原版host下的连续移动)。

角色名 2–15 字符，首字符英文字母，其余英文字母、连字符或下划线；classId 为 0 Amazon、1 Sorceress、2 Necromancer、3 Paladin、4 Barbarian、5 Druid、6 Assassin。初始属性、装备和 D2S 由原服生成；客户端仅提交 MCP 0x02 的职业／状态与名字：资料片固定0x20，专家级加0x04、天梯加0x40，资格由所连Realm裁定（原版旗标见[DLL取证](../resources/PD2_CN_DLL_ANALYSIS.md#最小闭环认证注册选角与入局)）。删角用 MCP 0x0A，command 要求 confirmName 完全匹配。注册账号／密码为 2–15 可打印 ASCII 字符，更细名字限制由服务器拒绝码说明。

肖像仅绘制已保存 native 1.13c、无组件染色且原 COF／DCC 完整的外观；legacy 新角、染色或未核实组合保留真实身份，肖像暂不绘制。建角使用当前 MPQ 七职业前端动画。

选角产品页只走RealmFrontend；旧app/character_frontend未编译／调用，保留作功能参考，不维护第二条本地选角执行链。沿旧选择器和OpenDiablo2 character_select核对双击同一角色1.25秒进入、滚轮／PageUp／PageDown翻页和两列四行布局；卡片步距读取当前MPQ charselectbox的两段总宽及原高度（当前272×92），标签及肖像位置对齐原布局。翻页使用既有MPQ numberarrows及页码；完整原Scrollbar仍未还原。Realm空白底图的标题／名称没有烘焙文字；Current Realm、Realm名称及Change Realm按钮统一使用已有原font16。字体字宽来自当前MPQ，角色列表、选择、创建和删除仍由MCP原协议驱动；Ladder／经典角色转换仍不可用。槽位角色名颜色经原D2Launch CharSel反汇编认证：名字经D2Win!10022写入，颜色参数=hardcore?1:4，绘制路径把4归一化为0，死亡位不参与颜色且原版该字段无灰色；RealmFrontend据此用当前MPQ font16的PL2文字色1渲染HC名字、其余白色，可选性仍由既有playable门控（realm_frontend.cpp）。死亡位另经原D2Launch认证写入肖像格子控件flags的0x10位（hardcore写0x20，经D2Win!10194），原版据此把死亡HC角色肖像画成半透明幽灵；RealmFrontend在portrait里对dead角色以半透明alpha绘制该槽位肖像（realm_frontend.cpp:407），颜色与幽灵都不进可选性判定。选中角色大名字与槽位同规则（hardcore红、死亡位不参与），仅无权限的Ladder等其他不可选状态显示灰色；Ladder／描述行颜色及其余文字效果仍待原版对照。

创建／加入／握手超时同样重新获取服务器角色列表，不自动重发建房或加入。加载超时先请求原保存退局；退局等待超时后关闭旧游戏连接并重新取角色列表，错误明确标记保存结果未知。协议损坏、账号连接失败等仍结束会话，失败提示关闭后回登录页。创建／加入／握手取消和其余恢复分支尚未逐项核对原服行为。

游戏传输失败源码保留目标IP／端口及底层TCP原因，在原MPQ错误文案后显示；不修改MCP加入回包的保留字段。2026-10-08本机D2GS在22:07:49启动时记录`QSNTInit failed on bind() - err=10013`，D2CS随后可成功建房／发加入票据，但D2GS未监听4000，客户端入局失败；绑定失败的外部诱因尚未确定。旧进程22:18:38被watchdog退出；按用户授权仅重新启动D2GS后，22:20恢复`0.0.0.0:4000`监听并经D2CS认证启用8个槽位，未启动客户端或建房复验。该环境恢复不作为客户端回归通过。

大厅按钮底纹：当前原Create Game帧大量使用251–254号索引，Units全为黑色，Act1保留原色阶。项目MPQ与原1.13c安装的按钮及Act1表哈希一致；原CelFileNormalize仅处理指针／缓存，不转换像素索引。当前六类大厅控件独立以Act1原表上传，背景及其它UI不改变；原Create Game帧有烘焙文字，不额外叠字。大厅按D2Win BUTTON_Draw高度规则使用高按钮Exocet10、紧凑按钮Ridiculous，动态文案黑字，禁用状态保留原控件底图处理。原版精确亮度、所有状态及窗口尺寸仍未对齐。800×600原生像素居中、局内画布及8人／默认关闭等级限制不变。

EXIT按钮：当前MPQ MediumButtonBlank原帧为128×35，按本地D2MOO D2WinButton::BUTTON_Draw高度规则仍使用Exocet10。原Exocet10的EXIT字形使用15–32号灰阶索引，含亮灰像素；公共UiPainter在原字形可用时不追加描边，POINT采样也不额外插值。当前动态按钮文案按原非透明字形遮罩统一BLACK，保留笔画轮廓、不扩张／侵蚀字形；默认字体的高度分支用于共用入口，≥35使用Exocet10、其余使用Ridiculous，Realm显式font16保留。具体视觉效果仍待原版对照。

Join双击复用现有选角的1.25秒窗口，绑定房间名称／MCP索引及连接／游戏代次；改输入、滚轮、点击其他位置、切页／刷新、模态及失焦清理点击记录。第二击沿原Join／密码提交链路，不重复查询、不要求详情Ready。右侧布局依据用户原版Join截图和当前joingamebckg.dc6，font8／Units PL2金色4与灰色5，字宽沿原TBL。MCP 0x06提供uptimeSeconds、creatorLevel／levelDifference、玩家姓名／等级／职业；时间显示服务器查询时的快照，等级范围取PvPGN game.cpp原限制与当前experience.txt MaxLvl（现表99）的可玩范围交集，职业读charstats.txt，缺等级依据显示未知。Elapsed Time及连接词to在当前三份英文TBL未找到，显示文案取用户原版截图，Level格式沿TBL 5017。三个可见玩家槽各显示姓名及等级／职业两行，其余用滚轮翻阅。

## 配置与职责

复制 [配置模板](../development/online.example.json) 到 online.local.json，或使用 `--online-config <路径>`。originalClientDirectory 相对配置文件解析，含同一 1.13c 的 Game.exe、Bnclient.dll、D2Client.dll，只读计算版本和 CheckRevision，不执行原 DLL。默认 authentication=pvpgn，SID_AUTH_CHECK 零 key，本机服无需 CD-key；严格服可显式设 keys。打包只复制 PvPGN 连接设置，私有配置不提交。

UI登录记忆由应用层[OnlineLoginMemory](../../src/app/online_login_memory.hpp)管理，按配置文件路径隔离。Windows使用当前用户系统凭据管理器，Linux使用配置旁权限0600的`.credentials.local`文件；密码不写入连接JSON、会话只读视图、日志或D2S，记忆文件已排除Git。修改用户名时保存新账号及空密码；返回菜单／取消登录保留输入，注册页返回恢复已记忆信息。`online-login`命令不改变UI登录记忆。保存失败会显示提示，但仍提交登录请求。

| 代码 | 职责 |
| --- | --- |
| [contracts/online.hpp](../../src/contracts/online.hpp)、[online_world.hpp](../../src/contracts/online_world.hpp) | 只读会话与服务端单位／房间／位置／装备外观前缀；未知字段 optional |
| [app/frontend.cpp](../../src/app/frontend.cpp) | 会话所有权、配置、tick、页面路由与命令提交 |
| [RealmFrontend](../../src/presentation/frontend/realm_frontend.hpp)、[RemoteScene](../../src/presentation/remote/remote_scene.hpp) | 原图、字体、局前／世界显示与输入，只读副本并返回意图 |
| [remote_world.cpp](../../src/client/remote_world.cpp) | 有序回包归并，无 MPQ、GPU、存档或本地模拟 |
| [RemoteTown](../../src/client/remote_town.hpp) | 五幕有序房间事件重放、锚点校验及活动地形快照，复用 DS1／DT1，不生成本地单位 |
| [RemoteControl](../../src/client/remote_control.hpp) | 当前MPQ／活动地图目标资格、移动范围、NPC靠近后交谈及回城门技能选择；UI与command共用，不模拟玩家位置或NPC规则 |
| [online_items.hpp](../../src/contracts/online_items.hpp)、[RemoteInventory](../../src/client/remote_inventory.hpp) | 原服物品与请求值契约；客户端依当前MPQ解码品质／属性与布局，并校验物品command；不调用离线库存或保存 |
| [RemoteCombat](../../src/client/remote_combat.hpp) | MPQ主动技能／学习／目标资格及原状态位流只读投影，提交原服战斗／成长请求；不运行离线技能、伤害、AI或保存 |
| [native_act_layout.hpp](../../src/world/outdoor/native_act_layout.hpp)、[native_map.hpp](../../src/world/native_map.hpp) | 共用五幕布局及房间生成会话；GS 有序重放和活动地形快照已接源码，生成及活动地图入口；完整时序仍未认证 |
| [online_commands.cpp](../../src/app/debug/online_commands.cpp) | 同一会话的菜单管道；accepted 与异步成功分开 |
| [RealmPortraitCatalog](../../src/content/character/realm_portrait.hpp) | MPQ 原表动态重建外观编号 |
| [RealmSession](../../src/network/realm_session.hpp) | SID／MCP／D2GS 状态机、认证、角色／房间、取票、加载、心跳与退局 |
| [chat_command.hpp](../../src/network/chat_command.hpp) | 游戏内输入语法与本地／房间／账号命令分类；不执行玩法或网络发送 |
| [TcpStream](../../src/network/tcp_stream.hpp) | Asio DNS／TCP、期限、有限队列、拥有线程 poll；关闭后旧回调不交付 |
| [protocol/](../../src/network/protocol/wire.hpp) | 边界检查、framing、认证、D2GS 压缩与拆包 |
| [Networking.cmake](../../cmake/Networking.cmake) | 固定 Asio 1.30.2／BNCSutil 快照，独立网络／协议／远端客户端目标；[来源／许可](../resources/THIRD_PARTY.md) |

网络公开头仅有项目值类型和标准库；Asio／WinSock／BNCSutil 留在实现，网络不依赖玩法、世界、存档、MPQ 或 GPU，保留 C++20／CMake Windows／Linux 路径。

## 会话与协议边界

### 游戏内命令分流

输入先由`network/chat_command`识别客户端命令与聊天语法，再由应用／RealmSession选择目标；不在游戏服务端解析通用斜杠命令，不增加私有D2GS包。名称不区分ASCII大小写；正文仍只支持255字节内可打印ASCII。已识别命令的参数或模式错误直接提示，不降级为广播或其他服务命令。

| 输入 | 执行位置与范围 |
| --- | --- |
| `/nopickup` | 本进程客户端输入开关；开启后地面拾取需要Alt，不修改服务端物品资格，也不写角色存档 |
| `/players N` | N为1–8，仅Single Player或本机TCP/IP Host；类型化宿主调用设置本实例虚拟人数，并通过原房间聊天发布确认。客人及账号Realm客户端明确拒绝 |
| 普通文字、`!文字` | 当前房间D2GS 15广播、14头顶文字；头顶图形气泡仍未新增 |
| `@角色名 文字` | 显式房间私聊，D2GS 15带接收角色；目标不在房间由服务端报告，不回退BNCS |
| `/w`／`/m`／`/msg`／`/whisper` | 当前名册命中角色名则房间私聊，否则按`/w 接收者 正文`交BNCS；`*账号`明确走战网私聊 |
| 剩余`/命令` | 通过现有账号连接发SID_CHATCOMMAND 0E，不穷举好友／查询／屏蔽等服务端命令；无BNCS连接明确拒绝 |

`/fps`、`/framerate`、`soundchaosdebug`暂不实现，识别后明确提示。BNCS的04私聊、0A发送确认、12信息和13错误进入独立`accountChat`有界记录；游戏界面与房间消息合并显示，房间副本不持有账号聊天，退局不丢账号记录。账号重连清理账号记录；其他频道事件暂不消费，转发入队不代表命令执行成功。

原版依据：D2MOO PlrMsg 14／15只消费已解析聊天，PLAYER_SetVirtualPlayerCount／GetPlayerCount与MONSTER_GetHpBonus／GetExperienceBonus提供虚拟人数及出生时人数规则。另核对本机1.13c D2Client RVA707F0的输入分流、70500的房间名册／BNCS私聊分支、6EC30的`/`／`!`／`@`前缀表；RVA70A94经D2Game ordinal10049设置本进程人数。原版允许范围之外的N会钳制，d2x输入目前明确要求1–8。D2MOO没有D2Client解析器，不能仅凭Storm声明推断转发规则。

本实例虚拟人数初始1，不保存；出生人数取存活已入场人数和虚拟人数的较大值（1–8）。新敌怪生命与原始经验每额外人数加50%，先缩放原始经验再计算MPQ等级比例；梦魇／地狱原攻击的伤害、命中和元素时长沿D2MOO MonsterMode每额外人数加8/128；既有怪物不追溯修改，邪恶祭坛升级保留出生人数加成。掉落沿原NoDrop有效人数计算纳入虚拟人数，并保留怪物出生人数上限与真实队员贡献；宝箱使用当前有效人数。D2S仍为v96。

原版D2Game虚拟人数是进程全局值；d2x改为实例状态，避免一个宿主同时存在的房间互相影响。

随机掉落分布、全部难度／怪物组合、头顶气泡、跨机器及Linux仍缺完整原版对照；NoDrop和梦魇／地狱攻击倍率的规则依据不替代这些组合的核对。

### 会话生命周期

公共方法在同一个客户端线程调用，底层持锁串行提交。私有 worker 每 10 毫秒服务传输、心跳、超时及有序副本更新；`tick()` 在客户端线程发布快照，也可立即服务一次。UI 停止绘制不停止网络。Idle／Failed／Cancelled 登录或注册；RealmSelection 选 Realm；CharacterSelection 创建／删除／选择角色或切换 Realm；Lobby 列表／建局／加入。列表取消保留 MCP，迟到回复不污染后续请求；游戏请求取消退役旧 MCP／GS 并重新取角色列表，已登录游戏则先原生退局；其他账号／角色等待取消结束会话。不自动重发角色消费操作。UI／command 不把 accepted 当作服务器成功；失败阶段的迟到输入不覆盖原连接错误。

read() 借用客户端快照，有效至下一次 tick 或公共修改／随后 read；后台回包不改写该快照。connectionGeneration 管登录生命周期，gameGeneration 管游戏连接，消费者按当前代次丢弃旧包。RealmSession 已将支持的回包归并至 read().world；仅 `RealmSession(true)` 显式保留有界原包供额外消费者；默认 UI 使用已归并快照，不积累无消费者的原包。启用者仍必须持续排空。world.areaGeneration 在 LOADACT／UNLOADACT 时递增，仅清理区域实体／房间，保留本人身份、装备和属性；1.13c 首次入局在 LOADACT 前已发送这些角色数据，不能一并清空。跨幕撤销旧位置，首个 LOADACT 可保留已收到的出生位置。断线／取消／退局／新局清空全部副本，显示绑定按代次失效。所有原服动作提交复验已发布快照与后台当前的连接／游戏／区域代次及本人GUID；加载期间换区的旧输入被拒绝，调试输入队列也清除。退局和新局统一清除门户请求、NPC初始化、限流／等待计时器与心跳；新局首个pong前延迟为null，不能显示为已测零延迟。NPC明确拒绝不被后续相关更新标记覆盖。错误具有独立本地sequence，界面关闭错误后不因世界／心跳revision变化重新弹出同一错误。退局、返回选角和 Realm 切换重新取票；保存成功须由重入结果或服务日志确认。

04另进入world.mapEvents，保留相对于07／08及本人位置的顺序；RemoteTown的4ms分帧重建必须先消费前序事件，并在该04处确认本人活动房间，才置本地地图完成。网络层的serverLoadComplete只表示已收到服务端04，不能跳过本地消费进度。05／03清理区域代次时同时撤销本地完成；本人身份／库存仍按上述首次入局和跨幕边界保留。无窗口协议入口不准备客户端地图，其ProtocolReady不能证明可见地图已经就绪。

正常关闭窗口或 command `quit` 时，已进入游戏的会话先提交 0x69，持续 tick 至退局响应／关闭或阶段期限，再注销并结束进程；不把 quit 的 accepted 当作保存回执。online-cancel／online-logout 仍属于显式关闭连接。客户端不写 Realm D2S，服务器负责保存。

- profile 为本机 PvPGN／D2CS／D2GS＋LoD 1.13c，旧式 SID 登录与 IX86ver0..7.mpq。CheckRevision 公式先检查再交解析器；原文件路径暂要求 ASCII。NLS／Warden／扩展反作弊未实现，本机 Warden 禁用，严格版本校验未验证。
- SID／MCP 独立累积半包／粘包；D2GS 压缩模式 0／1、长度头与 Huffman 解压，未知长度明确失败。长度表核对本机 D2Net.dll 0xA900、D2MOO／OpenD2；0x7A 为 13 字节，不沿 JS 示例的 3 字节。
- 加载 0x02 后发 0x6B；0x03 保留幕／种子／townArea／secondarySeed，0x04 完成、0x0B 本人单位绑定。townArea 不代表玩家当前区域，secondarySeed 尚未驱动 DRLG。
- 累积各限 256 KiB，单次解压 64 KiB，可选原包队列 4096 包／2 MiB；默认阶段期限 15 秒、心跳 5 秒。建局 MCP 0x14 记录队列位置并刷新期限；取消关闭会话，不宣称撤销已完成的服务端操作。
- 参考服空列表不发终止包时，超时返回 Lobby、保留已收到的列表及 packetId=0x05 超时诊断，gameListComplete=false；Join 页内提示不完整，不用等待／超时弹窗遮挡列表，不能把无响应当作已确认空列表。列表按角色 Hardcore 请求，Ladder 房间不展示。
- 33 字节预览接受 version 4／10／13；native 1.13c 的 14-bit client flags 给出职业／等级／状态／进度，byte 30 是公会徽章颜色。外观保留类别核对 D2Common.dll 0x9D888，具体 item code 动态读 MPQ。
- 密码、key、哈希和票据不进入视图／日志；各层清理自身副本，调用方负责输入副本。独立D2S编码不变。

证据为本地 PvPGN common/bnet_protocol.h、common/d2cs_protocol.h、bnetd/handle_bnet.cpp、d2cs/handle_d2cs.cpp／handle_bnetd.cpp；布局及动画参考本地 reference 和既有离线选择器，图形文案读取当前 MPQ；参考仓库不提交。

## 协议消费边界（1.13c 当前源码）

消息ID、长度、参数与预留状态统一维护在[消息速查](MESSAGES.md)。下表仅记录客户端消费范围和缺口，不作为第二份编码规范；不会以收发成功计为玩法成功。`online-status.protocol`按SID／MCP／game分别返回包ID、received／sent／unconsumed、协议逻辑字节数和lastReceived，不保存原始认证载荷。已按当前profile完整分帧的未实现消息累计world.ignoredPackets及game.unconsumed，保留会话，不当作操作成功或虚构状态；未知流长度和损坏消息仍按协议错误处理。共享分帧位于network/protocol，客户端不依赖宿主实现目录，原服同样适用。

`online-status.clientProfile`标识本会话固定的协议定义；`versionChallenge`为空或仅包含`logonType／revisionModule`，重试登录时清空，不包含公式／token／proof。`RealmSession(ClientProfile, retainGamePackets)`是底层版本入口，现有默认构造及GUI／无窗口组合继续使用原版。CN定义仅为固定2026-10-20 Normal样本的准备，不能凭该枚举认定资源匹配或允许实际入局。

| 方向／包 | 长度／分帧 | 当前消费 | 字段／关键性与缺口 |
| --- | --- | --- | --- |
| SID 双向 | FF／ID／16-bit 总长；独立 256 KiB 累积 | 0x50／51 版本认证、3D 注册、3A 登录、40 Realm、3E 取票、0A 选角聊天环境、0C 初始频道请求、0F info／error 公告、25 ping | PvPGN 头与 handle_bnet／message；密码／key／票据不入视图；4C 认证模块拒绝；0F 其他事件仍记为未消费，频道聊天与账号服务未完整实现 |
| MCP 双向 | 16-bit 总长／ID；独立 256 KiB 累积 | 01 启动、19 角色、07 选角、02 建角、0A 删角、03 建房、04 加入、05 列表、06 详情、14 队列 | PvPGN d2cs_protocol／handle_d2cs；详情固定16槽职业及等级，不采用JS示例的变长数组；真实请求 ID 与拒绝码；Ladder、完整分页／过期服务未完成 |
| D2GS S→C 0xAF／01–06／8F／B0 | 原 1.13c 长度表；AF 动态压缩模式 0／1 | 握手、难度／资料片、幕／种子、加载、心跳及退局 | D2Net.dll 0xA900＋RealmSession；校验幕0–4、难度0–2和标志范围；Ladder 仍明确拒绝，secondarySeed 含义保留未知 |
| D2GS S→C 07／08／09 | 固定6／6／11字节 | 有序房间进入／离开、对象指派 | room／tile／level、对象GUID与真实身份；地图历史4096条，丢连续性拒绝重建 |
| D2GS S→C 0A–11／15／16／18／95／96 | 原表固定；16为word长度；18／95／96资源位流 | 单位增删、玩家／怪物动作、坐标／不连续移动、资源与本人身份 | 可见单位8192上限；身份与动态字段保留 optional；完整特殊移动及其他玩家私有字段未覆盖 |
| D2GS S→C 19–1F／21–23／7B／94 | 固定与原技能列表 count 长度 | 本人绝对属性、技能选择／基础／加成、技能列表 | 状态更新／成长确认；不是通用事务ACK，完整派生显示未完成 |
| D2GS S→C 27–2A／77 | 固定40／103／97／15／2字节 | 原NPC对白、私有／公共任务字、商店结果和容器打开 | 本人私有／公共任务字和日志状态分开；个人主流程与分任务组队资格已接，完整房间邻接／特殊事件／剧情未完成；商店报价已接，完整逐整数认证仍有限 |
| D2GS S→C 3E／42／97／9C／9D | 3E／9C／9D byte长度；42固定6，97固定1 | 原物品位流／基础属性增量、光标清理、武器组及异步库存 | 参数宽度／物品布局读MPQ；完整染色／名称／全部属性消费者未完成 |
| D2GS S→C 4C／4D／99／9A／73 | 固定16／17／16／17／32字节 | 4C／4D切换技能动作；99／9A只发布触发效果，保留当前动作与目标；73为原飞弹通知 | 本地 monotonic 收到时间不是线上字段；全部触发／技能客户端程序未完成，动作覆盖修正依据见[死灵技能](../gameplay/skills/NECROMANCER.md#状态归属与保存) |
| D2GS S→C 51／59／60／63／82／8E | 固定14／26／7／21／29／10字节 | 对象／玩家指派、移出视野、旅行／传送点、门户、尸体归属 | 0x0A仅撤销空间指派、0x5C才移除名册；完整多人门户与尸体权限待接 |
| D2GS S→C 67–6D | 固定16／21／12／12／16／16／10字节 | 原怪物路径、动作、生命比例 | 速度／尺寸读MPQ；原动作12／13映射SKILL1；真正序列消费技能通知，保留wireAction |
| D2GS S→C A3／A7–AA／AC | 原表；A8／AA／AC byte长度 | 状态快照／开关、位流、NPC身份与外观前缀 | 位宽由MPQ消费者解析，状态历史64条；不代表全部状态效果可画 |
| D2GS C→S 15 | ID／type=1／language=0、text NUL、receiver NUL、零扩展长度；普通广播最大261字节，私聊最大276字节 | 局内ASCII广播／私聊，RealmSession::send_chat、Enter聊天UI及online-send-chat；14为头顶消息 | Clt15／PlrMsg及D2Net SERVER_GetClientPacketSize核对；仅入队、无本地回显；独立服已接广播／私聊／屏蔽与头顶投影，其他编码暂缓 |
| D2GS S→C 26／5B／5C／75／7F／8B–8D／90 | 26双NUL字符串，发送者≤15字节、文本≤255字节；5B word长度；其余原1.13c固定表 | remote_social归并聊天原字节、名册、队伍／关系原值和公开位置；普通消息投影至左上及M日志 | 组队邀请状态已解释；7F非玩家分支更新已登记宠物的生命／区域；不创建视野外单位；完整逐包核对仍缺 |
| D2GS S→C 5E／5F | 38／5 | 保存37项任务初始化及本人门户访问位；诊断见world.quests.availability／world.playerPortalFlags | 不将初始化顺序当任务日志槽位，不用访问位推断永久旅行资格 |
| D2GS S→C 7D／91 | 18／26字节 | 物品损坏／修复标志；按幕保存NPC初次交谈标记原报告 | 7D只更新已存在且归属匹配的物品／装备；91未联动对白选择，不改变任务奖励 |
| D2GS S→C 5A／7E／81／A4–A5 等 | 原表固定 | **能分帧但未消费完整语义**，unconsumed 按 ID 暴露 | 系统事件、任务／宠物等关键后续状态；M4／M7／M8／M9仍未完成，不能归为无关消息或宣布M1全覆盖 |
| D2GS S→C 其他有长度包 | 原始固定表或已核实变长程序 | 未消费ID公开计数 | 不把能跳过当成功；未知长度／未核实变长／Warden明确失败 |
| D2GS C→S 行走／交互／旅行 | 原01–04／13／49等请求编码 | 当前地图资格＋完整坐标终点／原GUID靠近、传送点／退出 | 位置信息始终以服确认；取消只结束客户端未发意图 |
| D2GS C→S 库存／城镇／战斗／成长 | 原物品27类及技能／属性请求 | 远端领域协调器资格、限流、关联更新、拒绝／超时 | 无通用ACK／事务号；非幂等操作不自动重发；统一全部领域协调尚未完成 |

任务／提示另消费0x50／52／5D日志及进度、0x8A NPC提示；0x2C事件2仅产生公共升级表现事件。字段到达不等于完整剧情／升级验收。

## 共用游戏界面

底栏、背包／装备／腰带、人物／技能树／技能选择、箱子／方块／金币、NPC对白／菜单、普通商店、传送点、任务日志、佣兵和Esc／选项面板共用既有SceneView／SceneController及原MPQ资源加载入口，地面物品原图／品质色标签／拾取热区、鼠标光标和敌人血条同样共用；联网独立HUD已移除。联机通过RemoteUiClients实现同样的IActor／IInventory／ICharacter／INpc／IQuest／IMap端口，不构造离线GameSession；资源、几何、面板状态和命中处理不另写联网版本。网络 worker 在资源加载、窗口等待和界面绘制期间持续收包、心跳和更新权威副本；资源回调不再重入会话。客户端线程通过 tick 发布快照，后台线程不修改表现正在借用的容器。

已绑定底层命令的界面操作：左右技能选择与基本PvE施放、属性／技能加点、物品拖放／装备／武器组／腰带／容器转移／交换／堆叠／装书／镶嵌／鉴定、金币、合成、普通NPC交易／维修／凯恩鉴定及传送点。组合库存操作逐步等待服务器光标赋值；每步保留 UI 投影时的 OnlineIntentContext（连接／游戏／区域／本人／交互代次及 NPC），发送端在锁内复验；关闭后重新交谈同一 GUID 也使旧队列失效；快照保留原GUID／revision，失败或超时中止，未确认的位置、金币、属性和技能不写入副本。UI关闭消费原手势，侧栏变化同时调整世界视口，换区保留同一套面板对象，退局后释放角色快照和待处理命令。

未知原服属性和攻击面板计算标为`?`，不按单机规则补造。普通／赌博报价共用原表纯函数，只对完整已知物品值报价，canRequestSale另表达是否能提交出售；最终价格和结果由服务端决定。两种服务端的原买卖／赌博／批量界面与请求共用，范围见[库存](INVENTORY.md)。任务日志只投影原生私有记录；五幕个人主流程与分任务组队资格已接，完整房间邻接／特殊事件／剧情、佣兵服务、玩家交易与部分显示消费者仍有缺口，不因入口或原表存在而成为完成。F1–F8绑定见本页流程，球体状态变色和完整物品染色仍待接。

地面金币取原数量，落地图读取当前Levels.Pal；敌人名字读MonStats.NameStr／TBL，生命按原0–128刻度并区分0x0C／69 GH的电强化触发位。物品Take组合等待真实光标回包及既有请求间隔，缺回包按会话超时结束、不自动重试。回城卷轴／书右键通过RemoteControl创建门户。联机Esc菜单不暂停服务端，Save and Exit提交退局；死亡回城与尸体取回接口已接，确认与验证边界见本页死亡章节。

## 战斗与成长底层

普通攻击、左右技能选择、坐标／单位施放、单次Hold请求和原0x12停止接口由command调用；共用游戏UI已接基本PvE施放、选择和成长。技能、目标、职业／前置／等级／属性／最大等级读取当前MPQ，服务端仍检查最终资格。单位资格按原技能区分：普通PvE敌怪／合格尸体，Enchant友方，Unsummon本人原7A及PetType许可的召唤物，Telekinesis物件／物品。其他尚未接目标显式拒绝，不因对象可见就允许任意施法。普通Attack依据D2Common原初始化规则识别，innate独立标记，不伪造技能回包等级。

- 0x21分开保存基础与装备加成，有效等级为二者之和；0x94更新基础等级但保留已收到的加成。0x23分别维护左右选择及原owner。玩家分配时按当前MPQ解析的Attack身份初始化空技能槽；后续本人0x23引用不存在、不属该玩家或未装备的物品来源时保留已有选择，不把owner=0一律转成普通来源。新角色初始装备发来的有效技能选择因此不会被零初始化的保存槽覆盖；其他玩家的私有物品来源不作同样校验。普通技能0x3C使用owner=-1，充能选择仍须真实已装备物品及已解码技能／次数；不按职业写死初始技能或自动补发选技。
- 0x3B学习／0x3A属性加点记录提交前目标值与余点；基础等级／绝对属性增加且对应余点减少才进入Confirmed，两类回包允许任意到达顺序。CN单点学习追加数量BYTE=1，普通属性0–3仍用原3A布局，与低字节0x10起的扩展控制分开。Pending互斥物品、回城门及其他成长请求；100ms限流、死亡／换幕中断、超时结果未知，不自动重试。
- 施放／停止没有通用原生ACK，记录SentNoAck。客户端不扣生命／法力、不算伤害、不按施放意图生成战斗单位。Hold仅提交一次，后续调用方决定是否持续提交；0x12原处理仅清地狱火状态，不能声称通用撤销技能。
- 接原0x0C命中、原玩家动作／死亡、0x67–6D怪物原动作及权威位置、0x11叠层、0x4C／4D／99／9A／A3技能和0x73飞弹消息。原怪物动作字节转换为MONMODE，12／13歧义保留wireAction；0x67是目标位置，0x68是当前位置和目标GUID。怪物切换非移动动作时，若无新的自身坐标样本，保留连续移动后的显示位置；4C／4D技能通知和仅含目标的死亡动作不能触发旧坐标回退，后续真实位置样本仍正常校正。依据为D2MOO `MonsterMsg::sub_6FC65C70`及`SCmd::sub_6FC3F060`／`sub_6FC3F0C0`的目标与自身坐标分界。生命比例保留原字节尺度。
- 战斗事件队列最多256条，按sequence有序；0x73无权威飞弹GUID，保留类型、所有者、当前位置／首路径目标和pierce，不伪造实体；首路径点改名为missileDestination，避免误当出生点。已接范围与表现限制见本页“联机游玩表现与输入”。
- A7／A8／A9／AA状态消息按单位有序保留；RemoteCombat依当前States／ItemStatCost的Send Bits、Send Param Bits、Signed解码，不套D2S Save Add或ValShift。状态队列最多64条，缺前缀／未知参数明确decoded=false，等待完整AA恢复，不猜状态。
- 1A／1B经验正增量、1C绝对经验；绝对等级与可用点仍读原1D–1F。全部职业技能、PvP、宠物／佣兵目标和充能／特殊物品技能仍有缺口；死亡／回城／尸体的实现与限制见下节。

双手初始化与无效来源保留依据为D2MOO `SKILLS_InitSkillList`及`SKILLS_SetLeftActiveSkill`／`SKILLS_SetRightActiveSkill`；新建角色的`PlrSave2`先按CharStats.StartSkill分配实际可用的右手技能，`Clients::CLIENTS_AddPlayerToGame`随后发送PlayerData保存槽，其来源仍可能为零。已有PD2新角色收包中，右手技能70／owner=FFFFFFFF先于左右保存槽的owner=0到达；客户端不能仅因后到就把不存在的物品来源认作当前技能。此修正共用于原版和PD2，两种模式的新建／重入／充能组合仍待实机回归。

## 原服物品与请求

0x7C为6字节单位类型／GUID通知：固定1.13c D2Client包表DDE60的槽124指向AD2A0，再由81AA0移除该单位uninterruptable（54）列表；D2Common当前ordinal10871／RVA38520读取指定状态。SkillItem／ItemMode在卷轴完成及pSpell失败时都可能发送，不能当作物品删除或开门成功。客户端接收后只撤已存在单位的对应状态；物品没有独立状态投影，数量、删除和目标Cursor仍各走原消息。该通知在PD2接收路径也必须识别，不能因普通回城卷轴而中断会话。

- 0x9C／0x9D保留真实GUID、所有者、服务端位置／模式及原位流。客户端序列无JM头、物品种子或Realm尾；地面／掉落模式用16-bit全局坐标，其余用原body／格子／page。0x9C本人库存允许先于0x0B到达，绑定后补本人所有者；镶嵌子物品保留type4宿主GUID。
- 解码compact、耳朵、金币、任务难度、quality 1–9、未鉴定字段省略、图形变体、词缀编号、个性化名字、数量、耐久、防御、孔数及主／套装／符文之语属性列表。尺寸、部位、属性宽度／Save Add／参数、书本配对均读当前MPQ。协议name字段为基础名，原名称／属性经共用提示投影，完整特殊名称／属性消费者仍有限；属性列表值为序列化单位，尚未应用ValShift，0x3E基础属性更新另列baseStats并保留服务器单位。
- 0x3E按1.13c实际变长包读取变宽GUID／数值、基础属性标志和参数，数量／耐久更新与请求关联均使用实际长度。0x0A删除物品；0x42按所属玩家清理Cursor。9C／9D移出容器／腰带按实际模式处理，合并在同包的Cursor转移保留，旧容器记录删除；IFLAG_DELETED本身不直接删除。0x97确认武器组。换幕保留本人库存及孔内子项，新局／断线清空。
- command支持拾取至背包或Cursor、拿起、放入／交换背包格、Cursor丢弃、装备／卸装及两手武器交换、腰带放入／拿起／交换／使用、堆叠、装书、卷轴／书鉴定、镶嵌及切换武器组。原拾取由服务端寻路接近，沿原50单位距离限制；不自动发放物品或改变客户端位置。需求／职业、同类堆叠细则、效果、扣减、最终装备和保存由原服决定。
- 操作前复验当前代次、GUID／revision、本人所有权、Cursor及MPQ格子／腰带／部位；未知或截断数据拒绝。只允许一个Pending物品请求，100ms限制、不自动重试；storage-close可取消等待。accepted仅入队；Updated需复查位置／数量，TimedOut结果未知，死亡／换幕／退局标Interrupted。NPC交易等待0x2A明确回执，原错误码标Rejected；关闭存储无原生确认，标SentNoAck。
- Pickup等待允许新有效移动／交互或施法替换旧意图，并标Interrupted；相关物品更新及超时清理对应拾取显示目标，迟到回包正常消费。其他库存Pending仍互斥。局内错误提示退出原游戏后退役，不在选角页重新弹旧导航拒绝；真实新错误仍显示。
- page快照为原InvPage+1：1背包、4方块、5私人箱；place使用native page=0／3／4。箱子由真实Objects.OperateFn=32单位交互，方块由原物品使用请求打开；仅0x77确认后开放对应格子。布局读当前Inventory.txt：资料片箱子6×8、方块3×4。关闭／死亡／换幕清理上下文，禁止把已打开的方块放进自身。
- 0x4F提交存取金币、关闭箱子／方块和合成，金额高WORD在前；0x50丢金币。0x19为金币正增量，0x1D／1E／1F为绝对属性，0x2A保留交易金额但不重复写钱包，避免出售双计。合成配方、产物和金币上限完全由原服决定，不接本地方块事务。
- 当前NPC交谈提交0x38 action=1生成普通货架、action=2请求原赌博货架；9C action=11／12维护服务器物品。单件购买0x32（赌博交易类型2）、出售0x33、单件／全部维修0x35、凯恩批量鉴定0x34由原服判资格及结算，0x2A保存结果／物品GUID／金币。共用报价覆盖悬停、购买确认、全部维修合计及online-item-quote，UI／debug买卖发送前重算并绑定revision，网络锁内复验货架／服务模式。维修限原五个铁匠，鉴定限原五幕凯恩；赌博NPC依原Dialog处理器。Shift多买已发送原标志，玩家交易权威已接；佣兵完整装备组合及完整原服回归仍有缺口，本人尸体取回消费原服库存结果。
- 回城卷轴／书创建门户仍用online-town-portal；通用use效果与目标选择由回包决定。库存／商店UI及原属性提示已接共用面板；地面原图／品质色标签及拾取命令也已接共用实现；孔内完整显示及未识别的特殊名称仍有缺口。

## 联机死亡与尸体

- 死亡依据原0x0D动作8／9和0x0E的PLRMODE_DEATH=0／DEAD=17，保留Dying／Dead。资源回包只以正生命到零的实际变化补足死亡开始；死亡存档重入时的初始零生命不能覆盖原服站立动作、锁住尸体取回。正生命回包不能自行解除既有死亡。D2MOO `sub_6FC82360`可省略生命零值通知，故不能只用life==0驱动界面；动作19仍是校正。只读`world.dead/deathPhase/deathRevision`共用于UI、移动、战斗、物品和交互资格。
- 共用死亡文字、原字体、死亡动作／DD缺图时DT末帧及UI关闭逻辑。死亡结束持有手势、分段移动、待发施法与物品组合，关闭所有玩法面板／对白并屏蔽输入；原服仍持续运行，怪物和其他玩家照常显示。地图暂时不可用时仍显示死亡提示并接收Esc。
- Esc／`online-resurrect`只建立回城请求，先等待服务端DEAD，再单次发送原0x41；收到原角色重新分配／存活站立模式及位置，或0x41之后的三项绝对资源恢复与0x15位置校正组合，才解除死亡。旧资源样本清掉后读取原服绝对属性／后续0x95，不把生命、法力或耐力设为本地最大值。`world.respawnRequest`给出WaitingForDeath／Sent／Confirmed／TimedOut及sent；重复Esc不重复发包，超时保留死亡状态、不自动重试，迟到的原服确认仍可消费。Hardcore继续操作走正常退局，不能复活；角色死亡位与尸体持久化由原服保存。
- 原0x8E记录／解除corpse GUID与owner的关系，独立于本人死亡状态和空间单位分配，跨区保留归属、重新加载真实可见单位。尸体使用同一职业原DD／DT末帧、名字与MPQ `corpse`文字、高亮；本人仍在死亡位置时避免自身与新尸体重叠绘制。`world.corpses`给出GUID、owner、owned与当前可见坐标；只有本人的真实可见尸体成为`scene.mapTargets`的type0／corpse目标。
- 点击尸体或`online-recover-corpse`（unitId）复用共同绕障／分段靠近，原服距离≤8后发送0x13 type0交互。accepted不等于装备已取回；库存、装备、经验、金币及0x8E解除／单位删除都等待原服回包。空间单位暂时隐藏不擅自删除归属；部分回收后原服未解除的尸体继续保留。未开放其它玩家尸体的loot权限或交易入口。

## 多人只读副本

玩家空间显示使用原 0x59 身份／坐标、0x0F／10 移动、0x0D 动作／生命及0x4C／4D技能；每位玩家各自保有公共动画和连续显示路径。0x59 的新指派清除同GUID旧显示缓存，原空装备流允许裸装，不因未收到物品而隐去人物；0x0B 只回填本人坐标，不把其他玩家放到本人位置。0x20 按当前1.13c十字节包保留公开属性（byte属性ID／有符号32位值），不使用D2MOO扩展word ID，不覆盖本人私有属性。0x74／8E消费原尸体归属，死亡单位停止移动／声音，原freeze状态也可冻结玩家表现。0x0A／5C移除远端玩家时一并清理其装备、物品及孔内项；本人临时空间移除仍保留服务器库存。

原图绘制只读实际职业／装备与当前MPQ，鼠标悬停活的其他玩家显示真实角色名及轮廓，交易请求沿独立邀请入口，PvP请求仍未开放。`online-status.scene.players`区分本人与他人、本帧可见／不可见原因、移动／死亡及显示坐标，名册身份不能作为空间可见证据。参考入口为D2MOO SUnitMsg::FirstFn、PLAYER_SynchronizeItemsToClient、PlrMsg动作表／sub_6FC82830、SCmd::0x59／74／8E，以及PvPGN MCP 0x06处理器；MPQ及参考源码不进入提交。

`contracts/online_social.hpp` 与 `client/remote_social.*` 单独维护名册、聊天和关系，原包字段以当前1.13c长度表及本地D2MOO的D2PacketDef／SCmd交叉核对。0x5B确认名册身份，0x75／8B／8C／8D维护原队伍／关系值，0x7F玩家分支维护公开区域／生命比例，0x90维护公开32位坐标；先到更新保留未知身份。原生命比例及flags保持原值；0x75的关系flags与0x5B的partyFlags分开。0x75末字与0x8B同取PlayerList.field_C，按到达顺序更新同一邀请状态，不能优先保留旧8B。0x7F非玩家分支更新独立于空间单位的宠物名册，未登记GUID不创建实体；头像范围见[佣兵](../gameplay/characters/HIRELINGS.md#召唤物与佣兵头像)。组队邀请客户端见下节；完整系统事件及敌意发送仍未实现，聊天屏蔽关系已接原5D。

空间单位与名册分离：0x0A不退出名册，0x5C移除成员及其关系；离开视野不会生成幽灵角色，也不清除未收到原删除的尸体归属。本人换幕保留本局名册／关系／聊天，退局／断线／新局清空。名册暂存上限64（缓冲保护，不是允许64人游戏），关系4096，聊天256条。诊断以nameBytes／textBytes保留原语言字节，避免将原编码误当UTF-8；组队按钮仅使用已核实的邀请状态，未授予敌对资格，不算M4完成。

聊天发送源码：`RealmSession::send_chat`要求ProtocolReady及当前连接／游戏／本人身份，跨区、死亡、库存／NPC面板不作为聊天禁用资格；拒绝空白、非可打印ASCII及超过255字节的文本。`protocol::game_chat`编码0x15广播／具名私聊或0x14头顶消息：type=1／language=0、文本NUL、收件人NUL、扩展长度0；末尾额外长度字节依据`reference/d2moo/source/D2Net/src/Server.cpp::SERVER_GetClientPacketSize`，不能仅发两个字符串。D2Game `PLAYER/PlrMsg.cpp::Rcv0x15_HandleChatMessage`由服务端真实角色名生成0x26，并按原关系／交互限制转发，发送者也等真实回显；不插本地消息或伪造ACK，不自动重试，不停止原服移动／施法。接收分帧限制姓名15／文本255字节，支持跨包与同帧后续包。原0x26的unitType=2／GUID=0不代表发送者GUID；`nNameColor`在普通玩家广播中是发送者等级，不直接作为字体色。原类型1／2／3／4／5／6／7按原字节保留。独立服务social按不区分大小写角色名路由私聊，接收者type2、发送者type6真实回包；找不到角色／被屏蔽分别回原5A/type4／13。原5D/action2(ignore)与3(squelch)更新方向关系，75／8C投影；广播沿原PlrMsg的聊天屏蔽及交易双方限制。14头顶消息按D2Chat原公式125+8×长度(最多254)帧保留，26/type5发送给可见且未屏蔽玩家，晚入可见也投影，到期76清除。客户端保留world.social.hover；头顶气泡的图形绘制未新增。BNCS频道命令、表情与中文编码仍未实现。

聊天界面依据与源码：用户提供原版底部输入、左上回显及M消息日志三张截图，历史最新消息在上、真实角色名深金色／普通文字白色、标题Message Log、底部Close、右侧滚动条。OpenDiablo2 `d2game/d2player/key_map.go`核对Enter／M，`d2common/d2resource/resource_paths.go`及`d2core/d2gui/box.go`／`layout_scrollbar.go`核对boxpieces／textslid原帧用途。当前MPQ：boxpieces为22帧14×15，textslid为17帧12×13；TBL的strMsgLog／strClose存在；对照截图字形和尺寸选font8消息、font16输入／标题，字宽沿原TBL。`presentation/hud/chat_view.*`独立维护只读消息、编辑草稿、光标及滚动；边框步距从原帧尺寸读取，位置按截图800像素基准适配当前HUD尺度。Sky原PL2十三种字体索引变换按需上传，白色保留原索引；messageColor控制正文，姓名色独立于等级字段，支持原FF c0–c9颜色串（本地d2bs Print用例补证）。标题／Close动态读当前MPQ。

用户原版入房截图中的三条消息为参考服公告：参考D2GS的`handle_s2s.c`中`D2GSCBEnterGame`将角色加入MOTD队列，`d2gamelist.c`／`d2ge.c`的`D2GSSendMOTD`按顺序发送商业用途提示、配置MOTD、World Event提示。2026-10-07本机原服向D2X客户端发送的消息均为0x26 type4、language0、messageColor1、姓名字节[administrator]；第一条含FF c8橙色，Hello world!沿包的红色，第三条含FF c9黄色且本服key item为空。当前聊天源码支持type4无姓名前缀和这些颜色串，同样进入顶部与M日志；文案、颜色及顺序来自原服，不在客户端写入固定欢迎消息。此记录只说明该参考服配置下的消息，不代表原版所有服务器具有相同公告。

Enter打开底部输入，再次Enter提交普通广播；空白Enter关闭，Esc丢弃草稿。Backspace／Delete、左右／Home／End编辑光标，255字节限制，非ASCII输入明确提示；提交拒绝保留草稿。M／底栏Message Log打开历史，M／Esc／Close关闭；滚轮、上下、PageUp／PageDown、Home／End及原箭头／滚动条拖拽翻阅，新消息到达时保留正在查看的旧行锚点。输入／日志占用整个开启、编辑和关闭帧，隔离走跑、自动地图、背包／技能、药剂／换装及鼠标玩法输入；持有按钮需重新释放，服务器／网络不暂停。关闭与新局不重放消息或玩法意图，换区保留本局历史。

当前显示限language=0的普通type1／私聊type2和6／server type3／noname type4及ASCII正文，其余真实原包保留但计chatUi.unavailableMessages，不猜表情／线索格式。用户补充原版实测：每条显示10秒、新消息在下、最多15条，左侧角色等面板打开时右移。顶部源码取最新15条可显示消息，按收到顺序向下排列，以原消息receivedMilliseconds的单调时钟独立过期；新消息、打开／关闭日志及面板不重置寿命。绘制使用SceneView::worldViewport的左边界和可用宽度，重新换行并裁剪在世界区域内；过期只隐藏顶部消息，不清除本局最多256条历史，M日志继续沿截图最新在上。font8正文／font16输入及标题保留原像素大小，字宽、换行、光标和行距同步。中文原编码／输入法、频道、头顶气泡绘制及全部窗口／字体精确一致性仍暂缓。

`online-social`／`online-chat`是完整只读快照别名，均返回world.social；操作仍以服务端回包为准。以上包尚未逐包运行验证，JS参考中的错误长度／位宽不采用（例如0x7F区域按原包16位），不将参考表纳入源码。

## 联机游玩表现与输入

右手选择尸体技能时，怪物尸体的悬停轮廓和名称按该技能的当前MPQ目标资格显示，不要求先按住右键；悬停与实际按键目标分开，普通左键仍按左手技能和交互决定行为。右键施放保留真实怪物GUID／type=1请求，消耗尸体、扣蓝和召唤物均由服务器确认。箱子、木桶等仍从当前Objects的OperateFn／Selectable及原TARGETABLE标志准备交互；Draw=0的隐形机关控制器不生成可点击替身，开启箱子触发的陷阱仍由服务端执行。PD2专用陷阱的全部效果与实服尸体组合尚未认证。

### 组队邀请客户端

`presentation/hud/party_view.*`只读原名册，P／底栏Party Screen开关左侧面板，Esc／原关闭按钮关闭。原图来自当前MPQ的menu/party、partybuttons、partyboxes、partybar及panel/buysellbtn；font8、职业名、Level、Invite／cancel／Accept／Leave／Yourparty及Levels.LevelName读原字体、PL2与TBL。本人姓名单列顶部，其余最多七名他人不依赖空间可见性；本地原1.13c D2Client的Party.cpp与用户2026-10-10截图核对38像素行距、按partyId分组及原分隔条。相同且非FFFF的本人／对方partyId显示绿色姓名／职业／等级、“In your party”及原0x7F区域。顶部Leave提交本人GUID。左侧Hostile及右侧Ignore／Squelch提交原5D/action4／2／3；Ignore阻止对方听见自己，Squelch屏蔽对方发来的聊天。图标与提示取MPQ，Hostile原帧4／6、Ignore0／2、Squelch8／10，按下取相邻帧；只在75／8C确认后改变状态。Hostile要求城镇内、双方基础等级至少9，禁用提示取原TBL；当前partyboxes.dc6为18帧，红色禁用直接使用原16／17帧，不额外生成调色替身。敌意行的姓名／职业／等级使用Sky PL2原红色font8，优先于同队绿色，邀请按钮与本人标题保持各自原字体。HC增加原5D/action1尸体许可，按钮取原12–15帧与strParty11–14，存活时可授予／撤销，75／8C确认后改变状态；其他关系行色仍未完整实现。

D2MOO `PartyScreen::ReceivedInvitation／InvitationCancelled／Command8／PlayerLeftParty`及`PlayerList::sub_6FCBAA20`核对相对状态：0提供Invite、2提供Accept、4提供Cancel，同队不再提供邀请按钮。RealmSession.party_action发送原六字节0x5E＋action6／7／8／9＋目标GUID；发送前复验ProtocolReady、连接／游戏代次、真实名册身份、当前邀请状态或本人非FFFF队伍ID。鼠标同项按下／释放匹配GUID、动作和本局代次；失焦、模态输入、重入清除按下状态。发送不改按钮／partyId，入队、离队及Cancel都等真实75／8B／8D；无自动接受、重试或本地成功消息。

左上角同队头像读取MPQ Hireables下七职业原Icon、font6及姓名；本地原1.13c 0x5BA90／0x5B5A0核对56像素列距、TRANS50原PL2混色及46像素生命条，0x7F生命百分比在50／25阈值转绿／黄／红，未知生命不猜满值。本人不重复显示，首列x=15、姓名底线72／62交替；左侧面板或模态占用时隐藏，与佣兵／召唤图标避让。Z沿原Show Portraits切换整排显示，范围与缺口见[佣兵专题](../gameplay/characters/HIRELINGS.md#召唤物与佣兵头像)。自动地图本人／NPC／队友／门户的原线形、颜色、公开位置及Show Party入口见[自动地图](../gameplay/world/AUTOMAP.md)。

右上角敌意提示使用当前MPQ `panel/hostilepic.dc6`，只由另一名仍在名册中的玩家→本人关系位8驱动；本人单向对别人宣战不代替该方向。原1.13c D2Client RVA285C0调用9DB90，按W−58、底部基线45（自动地图开启为115）及原TRANS50绘制；D2X沿当前HUD尺度与活动幕PL2显示，右侧面板遮住角落时隐藏，左侧Party等面板仍可显示，不从点击意图推断敌意。

玩家悬停姓名使用独立drawPlayerLabel入口：按用户原版截图，无论关系都绘制深红色半透明背景；红色取当前Sky PL2的字体RGB，背景走活动幕原矩形混合表。已确认本人→目标敌意时用原红色font16，其他关系保持原普通字体；姓名沿收到的真实type0单位，友方交易目标与敌方战斗目标都走该入口；NPC／物件／尸体仍用各自标签。背景颜色依据截图适配，并未静态恢复原版玩家标签全部布局／颜色分支；其他幕、全部面板遮挡及窗口尺度仍需对照。

客户端消费原5A的Party3–9及type16敌意冷却通知，按当前MPQ文案、颜色和收到时刻进入聊天顶部及消息日志。type2／3按sysmsg3／4（有账号名时3a／4a）显示入退局和怪物强弱提示，type6玩家击杀使用sysmsg7；只消费真实事件，不从名册变化生成通知。type2角色名与本人选中角色名相同时在公共客户端收包路径跳过，不进入聊天顶部或日志；单机／房主独自开局不显示本人入场，其他玩家加入时已有玩家看到提示，新加入者不显示自己的提示。依据本地D2MOO Game::入场分支、PlrMsg::sub_6FC84D40与GAME_ForEachIngameClient：服务端向包括入场者在内的INGAME客户端广播，没有第二人起发包的判断；同一原1.13c D2Client RVA 0x7E148–0x7E18B在type2分支比较本人姓名，相同跳过格式化，0x9D850取得角色姓名。宿主仅在真实入场／退场提交时广播，晚入名册不会重播历史提示；“minions weaken”不触发对已生成怪物重新缩放生命，人数系数仍在生成准入时捕获。

自研服务端的`server/systems/social/party.cpp`唯一维护实例内队伍和有向邀请；原5E动作6–9经连接绑定解析真实GUID后提交权威命令。接受须有来自目标的有效邀请且本人尚未入队；新组从原ID3开始，加入已有组复用ID，清理不能继续接受的邀请；离队／断线撤销邀请，余一人解散。邀请不受空间距离限制，仍要求同局真实参与者。75／8B投影观察者相对状态及关系，8D同步本人和他人队号；7F玩家分支只向同队他人发送生命百分比／区域，宠物分支只向主人发送，90发送远处公开全局位置，不创建空间单位／揭示房间，晚加入与重新入局从当前权威投影恢复。关系更新保留队伍ID，不以聊天屏蔽重置组队。

同队回城门在接受交互与目的地准备完成时复验队伍资格，队友穿门不关闭主人的门；只有主人从城镇返回野外才关门。death按同区域、活人、距死亡怪物80格以内筛队员，沿SUnitDmg的原89/256额外经验及等级比例分配原始经验，再套每位队员MPQ经验比例和装备加成，背压重试不重复发奖；loot的NoDrop人数系数采用同区域存活队员数，仍受怪物准入人数上限。友方圣骑士光环按原MPQ滤镜、半径和碰撞作用于同队玩家／随从。任务按各事件冻结资格和共享范围，规则及缺口见[任务系统](../gameplay/quests/SYSTEM.md#资格准备与提交)。HC尸体loot权限及原通知已接，规则与保存见[玩家死亡](../gameplay/characters/PLAYER_DEATH.md#hardcore永久死亡与尸体许可)；耳朵战利品、完整房间邻接／特殊任务事件及其余技能的组队例外仍未完成。

服务端敌意由social唯一持有，开启时双向设置位8并传播给目标队伍中等级合格成员；关闭只清除发起者到目标的方向。开启关闭本人的回城门，同队时退出队伍；同一对象重新开启需60秒，发起者回城门／传送点菜单分别有5／10秒限制，沿原真实时间而非暂停的游戏帧计时。75为接收者到行玩家的关系，8C包含所有已入场玩家对，晚加入也补齐。伤害与技能取攻击者方向，非队友不会自动成为敌人；PvP结算范围见[武器与PvP](../gameplay/combat/WEAPON_EFFECTS.md#pvp伤害)。

NoDrop分布、同队光环／随从动态标记、八人竞争与背压、跨机器和Linux仍缺完整核对。

### 玩家交易邀请

`OnlineWorldView::playerTrade`与NPC交易状态分开。城镇左键点击活着的其他玩家，经RemoteTown／RemoteControl的原距离≤8靠近与0x13玩家交互发起；0x77 action0等待、1邀请、6打开／清除双方同意、5对方同意、14禁用同意／15恢复、9／10无空间、12关闭、13完成。邀请没有身份字段；真实对方姓名和GUID只取0x78，不从0x76或邻近玩家猜测。死亡、换区、对方移除、退局、断线清理。

邀请用0x4F button3接受、button2拒绝／取消；已打开的报价用button4同意、7撤销双方同意、8报价金币。金币高16位先于低16位，0x79 flag1更新本人、flag0更新对方；钱包和物品由原服最终处理。本人绿勾表示同意请求已发出，没有自己的独立同意ACK；只在原13确认完成。报价变化、原6清除勾选；原14／15决定取回物品后的锁定，不自造解锁计时。金币报价若对方已同意，按TCP顺序先发7撤销再发8；等待本人0x79期间不能同意。未确认响应超时保留状态，可显式取消，无自动接受或重试。

当前MPQ的trade.dc6四帧、buysellbtn.dc6第16／17勾选帧及第10／11取消帧、800borderframe、font16／Sky、TBL用于完整左右面板；第12–15帧是左右箭头，不用于灰色勾选。绿色勾选保留原勾与边框，内区按当前Sky PL2绿色变换生成，原版专用变换尚未认证。用户提供接受后及放入手套／绿勾截图作为布局依据：左侧上方对方报价，下方本人报价，右侧本人库存；格子坐标／容量读取inventory.txt的Trade Page 1-2／2-2。点击本人Gold字段用原dialogbackground和strTradeGoldHowMuch打开数字报价，Enter提交／Esc取消编辑；该弹窗布局参考本地OpenDiablo2 MoveGoldPanel，尚未与原版交易金币弹窗截图认证。物品格底色及全部像素配对仍未认证。

NPC服务／Talk菜单和发起／接收交易邀请共用OriginalMenu（presentation/graphics/original_menu.*）：当前MPQ boxpieces、font16／原TBL字宽及Sky PL2金色／蓝色变换；按用户Warriv／Akara／等待交易截图统一金色NPC标题、白色居中选项、蓝色悬停。收到交易邀请时使用当前MPQ的strTradeAgreeTo作为标题，确认行读取TBL的ok（英文显示OK），取消行读取strUiMenu1，与用户原版交易邀请截图一致；标题和选项共同参与菜单尺寸及布局，发起方等待菜单不显示该标题。无悬停时邀请选项保持白色，键盘导航可显示选中项。菜单边框与文字／命中区域共用，业务动作及资格由调用方处理；没有另画NPC边框或按字符数猜宽度。boxpieces底边帧的墨迹位于顶端，拼接按当前原图的九像素差与侧边偏移修正，消息日志也复用该边框入口。精确菜单锚点及原版完整状态仍待对照。

服务器实际本人报价为原INVPAGE_TRADE=2（位流page3）；对方报价是原服复制到本人库存的INVPAGE_EQUIP=1（位流page2）临时副本，独立只读容器，不能取出／交换／装备。原服action5／mode4／page2撤销临时副本时直接清理，不将副本当作真实Cursor（D2MOO PlrTrade::sub_6FC931D0与ItemMode::sub_6FC446B0）。本人背包↔报价使用既有Take／Place／Swap和原0x19／18／1F；拖拽、Shift快捷放入／取回及悬停共用库存投影。发送前核对游戏／区域／交互、物品revision与原格子尺寸／占用；同意另外绑定交易revision，物品增删／属性更新及金币回包使旧意图失效。待处理库存队列、光标物品、本人已勾选及未齐响应阻止再次同意或编辑。关闭只消费原服实际回包，不本地交换或修改保存格式。

TradeInviteView／SceneView只读状态并提交意图；交易关闭旧交互面板，打开本人库存，输入继续被隔离，网络不暂停。邀请Enter接受、Esc拒绝；报价面板勾选／取消鼠标同项按下释放，Esc取消整个交易（金币编辑时先关闭编辑）。报价期间禁用移动、战斗、消耗／装备等普通操作，保留本人背包／报价物品移动及取消。capturesWorldInput只在库存绘制时允许已打开报价面板，保持其他模态状态及玩法输入隔离；本人Cursor图标与落点高亮沿公共库存投影。聊天草稿／历史保留并继续接收。原服成交观察见[物件与交易参考](#原服物件与交易观察)，完整原版界面仍待对照。

世界绘制、动作／原图、手势、人物／技能／任务提示、自动地图和声音只有公共消费入口。文件归属见[架构](../architecture/OVERVIEW.md)、[客户端](CLIENT.md)及相关模块；女巫与亚马逊原表、公共纯计算及显示公式不重复实现；新增原Clt程序、本人已知IAS／Pierce、宠物归属与怪物装备／人物伪装沿同一客户端消费者，范围见对应职业与[客户端](CLIENT.md)。

- SceneController 负责首次按下、按住目标锁定、Hold、松开、失焦与 UI 消费；RemoteControl／Combat 提交原请求。0x12仅有原地狱火停止语义，不是任意技能撤销。长按行走在原服到达后继续，未移动指针时固定世界目标。
- 位置副本与连续显示位置分开。路径目标只在与当前意图一致时用于显示衔接；无原生请求号，回包代次不是 ACK。15秒无原服位置进展才超时；0x15明确校正、受击、死亡和传送独立处理。移动→施法保留当前连续动作交接位置，清移动请求不撤销显示进度。
- ActorAnimationCatalog／State 统一组件、模式、AnimData和MonSeq；空变体且原索引0省略组件，不丢整个NPC。固定Utrans和当前MPQ调色读取共用路径。嵌套序列／非零方向覆盖等不足证据时不可用。
- 本人原服通常省略技能动作通知，因此已发送请求驱动本人显示衔接；强制同步去重。动画／弹体不扣资源、不判命中、不生成战斗单位。
- ClientSend只消费0x73原坐标、首路径目标、剩余帧、等级与穿透；无弹体GUID。已有程序1／5／6／8／9／18／19／20、原25Hz运动、充能弹／扇形／新星、部分拖尾与冰封球显示；接触只控制画面，未收到穿透次数不掷装备概率。
- 原任务／NPC事件、技能、角色受伤／死亡和怪物动作转换成公共声音事件；SoundCatalog解释当前MPQ的Sounds／MonSounds／技能声音，SceneAudio消费选择、概率、延迟、脚步和待机间隔。SoundBank处理原声组、Compound及Stop／Defer；换区／打断／暂停恢复清过期事件。不维护两套MonSounds解释。

限制：骨魂追踪、其余未接程序、完整速度状态、其他幕首领死亡程序仍有缺口；精英姓名／调色及第一幕死亡回调已接，有限范围见怪物专题。技能与精英回调范围分别见职业文档及怪物模块。完整光照、空间衰减、循环音和非零FsOff缺少足够原客户端证据。scene.effectLimitations只记录遇到的限制，空数组不认证全部表现。职业及原版证据见[公共技能](../gameplay/skills/COMMON.md)、[怪物](../gameplay/monsters/PRESENTATION.md)与[资料来源](../resources/THIRD_PARTY.md)。

## 原服物件与交易观察

以下观察来自2026-10-07本机1.13c参考服与D2X客户端，消息和结算可用作原服参考；D2X截图不作为原版客户端像素依据。物件规则、资源与表现缺口见[物件](../gameplay/world/OBJECTS.md)。

经验祭坛class81／code15领取后收到原服state137和50%经验属性。生命祭坛class2从mode0进入mode1，生命恢复。客户端只应把玩家／怪物的0x4D解释为施法动作，不能覆盖物件的0x0E模式。全部祭坛形态与Overlay节拍仍待核对。

两名角色交易hp2药水与10金币：放入报价后对端收到只读page2副本，取回收到14／15并撤销副本；报价金币收到0x79，成交收到0x77 action13。原服重新分配药水GUID，钱包112／0变为102／10；反向成交恢复112／0。退局重入后原物品位置与余额保留，零金币角色未收到属性14，缺字段不能当作已知零。具体实现与容量、取消、保存边界见[玩家交易](../gameplay/items/PLAYER_TRADE.md)。全部装备／镶嵌交易及原版金币弹窗、物品格底色、绿色专用变换仍缺对照。

MCP加入时levelDifference=0导致不同等级角色被拒绝，返回0x2C；关闭限制提交99可加入。不存在房间返回0x2A，错误密码返回0x29。

公共UI调色依据：当前MPQ clickbox有两张15×16原帧，本地OpenDiablo2 d2ui/checkbox.go使用Fechar；共用checkboxControls据此读取Fechar，覆盖建房与创建角色。14组当前MPQ DC6按实际非透明索引比较，invchar6、tradestash、supertransmogrifier、skltree_s_back、800ctrlpnl7、buysellbtn、goldcoinbtn和skillpoints在Act1／Sky／Units下没有像素颜色差异。numberarrows、radiobutton、realmselectbuttonthin、3widebuttonblank及PopUpLarge分别有56／55／64／91／5个像素随Act1与Units或Sky变化；这些是需核对原版页面配对的候选，不能仅凭有差异统一改表。全部资源、PL2变换、停用／混合效果及窗口尺寸仍未核对完整。

## 本批源码修正

传送点中性启动按原0x0E/mode1结束激活等待，不再错误等待原版不会发送的0x63而超时；后续真实菜单仍必须由0x63授权。同一交互代次保留多人先后激活的回包关联，已关闭／移动后的旧回包不重开菜单。此入口用于原D2GS与自研宿主；服务端规则与依据见[传送点](../gameplay/world/OBJECTS.md#自研传送点)。

物品报价两端共用itemTradePrice／itemGamblePrice，原字段、整数依据和拒绝边界只维护在[物品报价](../gameplay/items/ECONOMY.md#公共报价与原版依据)。AutoMagic索引及multibuy的完整原服组合仍待核对。

无形图标／装备更新：原物品flags=0x00400000经InventoryItemView进入公共drawItemIcon，原PL2变换范围及缺口见[物品表现](../gameplay/items/PRESENTATION.md)。穿戴外观保留真实MPQ身体组件，不因无形隐藏人物；RemoteWorld除显式移除／卸装动作外按位流实际mode=1及身体槽重建，避免仍穿戴的物品更新丢失外观。依据D2MOO D2Constants的物品动作与SCmd的原位置序列化；世界逐组件透明／染色仍有缺口。

近距离交互：OnlineMovementRequest区分普通行走与交互靠近；RemoteScene在连续显示位置已合格时清除本帧行走路线，不伪造原服站立模式、位置或成功回执。依据D2MOO PlrMsg::sub_6FC828D0，物品使用D2Common_10399距离≤4并检查原0x0804交互射线；物件复用UNITS_IsObjectInInteractRange对应几何和射线，尺寸来自当前MPQ；本人尸体距离≤8、出口距离≤4。RemoteTown同步准备出口靠近资格。远处／有阻挡时保留靠近预测，普通移动不提前停止；库存、对白、旅行及回收仍只消费原服结果。

NPC点击／靠近：D2MOO PlrMsg `sub_6FC828D0`只在距离≤6且玩家非busy时交谈，距离7／8执行靠近，距离≥9不会靠近或回复对白。当前客户端普通02／04单位路径与交互靠近共用原尺寸距离停止条件，按当前显示路径到达提交13，不等待稀疏96采样；显示位置只授权请求，菜单与奖励仍等原27／28确认。具体原版观察、请求取消和跨边界限制见[NPC交互](../gameplay/npc/INTERACTIONS.md)。

传送点／怪物尸体消费：当前MPQ Objects的OperateFn=23条目均为Selectable1=0，但D2MOO `OBJECTS_OperateFunction23_Waypoint`接受mode=1／2开菜单。RemoteTown对这两个模式保留点击资格，普通物件继续核对Selectable及TARGETABLE；解锁历史、菜单和旅行仍等待原服。0x51物件分配记录动作接收时间；公共ActorAnimationCatalog按原FrameCnt1＋1个25Hz服务帧投影启动结束后的mode=2，复用原ON循环动画，并将同一显示模式交给公共光照。仅限Waypoint的表现衔接，不修改副本模式、碰撞或解锁位；缺少必要MPQ定义不推导完成状态。

MonsterMsg的0x69动作9使用当前坐标，RemoteWorld将它写入position／positionRevision并清除旧destination，事件仍保留原坐标与方向；其它动作的目标坐标语义保持原样。RemoteScene在死亡动作尚无新当前位置时停止路径并保留当前连续显示位置，收到尸体当前位置后由原位置校正收敛，不再先回退到旧行走采样。原包依据与缺口见[怪物表现](../gameplay/monsters/PRESENTATION.md)；第二幕传送点鼠标／发光仍需原版对照。

技能／怪物只读资格：人物投影和 RemoteCombat 共用 skill_eligibility 的当前 MPQ 条件导入、Attack／CharStats 固有清单及学习／选择资格。未收到基础等级不按零授权学习，界面补齐原 reqstr／reqdex／reqvit／reqint 门槛；当前法力／装备提示仍使用已有只读输入，下一等级预览不写当前资格。RemoteCombat 集中 MonStats 敌我、原状态 alignment 及 corpseSel／hide／udead 目标校验，RemoteScene 的命中与锁定复用 monsterTargetEligible；声音、弹体、光照与 NPC 读取同一 onlineMonsterCorpse，不再各自判断死亡模式或漏剥离生命触发位。场景、owner、范围及地图绑定保留原协议校验，伤害、消耗和学习结果仍由 D2GS 决定。

物品品质映射、原图、flippy及掉落事件使用公共客户端；当前支持和限制只维护在[物品表现](../gameplay/items/PRESENTATION.md)。

掉落声音与数量反馈使用公共SoundCatalog／SceneAudio和原物品变化；参数与拒绝边界只维护在[物品表现](../gameplay/items/PRESENTATION.md)，完整音频时序仍待原版对照。

世界角色装备：按D2MOO D2Inventory的INVENTORY_GetCompositItem和当前MPQ准备真实身体组件。例如qui绗缝甲四个主体组件ArmType为0、肩部为1，女巫对应原DCC及城镇COF存在。真实装备组件进入公共合成器，未知组件染色／无形透明只作诊断，不因品质或效果标志隐去人物；真实组件／动画缺失保持明确不可用，不替换裸装。选角肖像染色与全部世界组件组合仍有缺口。

## 最终依赖清理

2026-10-07：删除已被替代的 Local 入口及 GameSession、Simulation、SkillRuntime、InventoryService、本地任务／AI／战斗／奖励执行源码，移除 d2x_session 目标。共用 gameplay／items 只保留联机显示、几何、原资源报告和独立存档工具的纯函数及值类型；混放的元素伤害显示、技能等级和库存错误文案独立提取，内容加载移除依赖库存服务的装备计算入口。人物／任务投影位于 d2x_client；presentation 链接 client／content／world／remote_scene，客户端不链接 persistence。

删除无原服生产者的旧法杖插入面板、状态和资源加载，缺失原服插杖流程仍明确未实现；删除旧本地调试输入、旧 CLI 别名、旧地图／动画／音效重复缓存及单机切换分支。LocalCast 名称表示本人施法显示衔接，保留原服预测／校正用途，不是本地结算。测试 pause/resume、在线 command 和独立资源／存档工具保留。

CharacterSaveData 定义移到 persistence/character_save.hpp，字段与 D2S v96 编码不变，不更改格式或语义。原 MPQ、reference、旧压缩包、mvp 和用户文件保留；参考源码／原表／资源不纳入提交。完整依赖图见[架构](../architecture/OVERVIEW.md)。

## 既有有限证据

本节只保留原服消息、规则和原版生成结果。2026-10-06至07的联机观察使用本机PvPGN／D2CS／1.13c D2GS和D2X客户端；部分独立角色的等级与物品由临时档准备，不能据此推断自然获得过程或原版客户端表现。

| 记录 | 实际观察 | 边界 |
| --- | --- | --- |
| 战斗、死亡与回复 | 一级女巫Fire Bolt消耗法力35→32；原服怪物受伤／死亡并反击扣血；死亡回城恢复资源，死亡后直接退局的新进程入场为1／40生命、35／35法力，可取回法杖与护身符并由Akara恢复 | 只覆盖这些普通单人条件；离开期间死亡精确时序、专家、多尸体及完整伤害分布仍缺 |
| 选择与成长 | 新角色初始Fire Bolt owner=0，重新选择收到owner=0xFFFFFFFF，退局重入保留；原服回应属性加3、Fire Bolt学习、Frozen Armor状态和Teleport位置 | owner未确认时不能猜普通施法资格；全部升级边界、技能及充能来源仍缺 |
| 物品与任务消息 | 观察到Cursor／腰带／装备／箱子／金币消息、3宝石合成、普通买卖／维修／鉴定、卷轴鉴定、0x7B快捷键恢复及邪恶洞穴STARTED记录和说明 | 全部组合、服务资格、逐整数价格、自然掉落及任务奖励仍缺；具体原字段参考见[物品依据](../gameplay/items/EVIDENCE.md) |
| 旅行 | 原服五幕传送点、奥术／皇宫地下层门户与传送台往返 | 完整特殊资格、多人旅行及原版客户端地图表现仍缺 |
| 地形开发对照 | 第一幕122组、第二至第五幕364组原1.13c事件序列／最终DT1／完整碰撞对照 | 方法、样本与排除项统一见[原版对照](../architecture/MULTIPLAYER.md#原版-dll-对照方法)；不认证全种子／全激活时序或玩法 |

参考服曾出现握手后不加载、watchdog可能死锁及保存后断连，确认无人在线后重启可恢复；失败样本不计通过。Ladder仍server down，严格版本校验、跨机器和长期稳定性未认证。本机组件、端口及启停只维护在[部署入口](../architecture/MULTIPLAYER.md#本机部署与验证入口)。
