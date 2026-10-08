# 联网模块

更新：2026-10-08。本页维护原协议、客户端副本及既有原服证据；[基线](../../BASELINE.md)区分源码与包，[总计划](../architecture/MULTIPLAYER.md)维护功能顺序。当前原服与自研宿主共用RealmSession／Remote*完整客户端，传输为TCP或内存字节队列。本轮协议统一和D2S接入已构建打包并完成有限单机冒烟，证据与限制见基线；下文原服交付记录不认证新宿主，也不代表本次进行了原服游戏回归。

## 局域网自研宿主入口

源码已提供原MCP／D2GS的TCP接受连接，与Single Player内存队列共用服务和完整客户端。当前宿主运行于D2X应用进程，未交付独立无图形EXE，也未提供BNCS账号。LAN角色来自宿主目录；客户端本地D2S不参与上传。各机器使用同一当前MPQ版本；本批已构建打包并完成同机TCP双客户端的有限冒烟及嵌入宿主两个房间的基础观察；跨机器尚未认证，范围见[基线](../../BASELINE.md#当前运行包与有限冒烟)。

- 界面宿主：首页TCP/IP Game → Host Game → 共用角色页／大厅，创建人数1–8的房间。首次Host监听全部IPv4接口，后续Host复用监听；返回菜单只退出本机连接，不停止远端房间。Single Player独立保持自动建单人房。
- 界面加入：首页TCP/IP Game → Join Game → 原IP弹窗，默认填入并选中`127.0.0.1`；可直接输入宿主IPv4替换，OK／Enter连接，Cancel／Esc返回。失败显示现有错误弹窗，关闭后返回IP输入并保留地址。
- 页面地址来自本机实际启用的IPv4接口（含回环），多网卡列出多个地址，不做外部网络探测。UI仅提交连接意图；应用选择TCP／内存传输，角色和入局仍用同一RealmSession。原MCP加入回包使用该连接实际到达的宿主接口地址，因此同机127.0.0.1与不同网卡上的局域网客户不会共用错误的GS地址。
- 命令行宿主：`d2x.exe --host-lan 192.168.1.10 --host-saves saves`，预先监听指定本机接口；随后从TCP/IP Game的Host进入。显式绑定某网卡时只接收该接口连接，本机加入也需输入该地址。
- 其他客户端：`d2x.exe --lan 192.168.1.10`，直接使用相同角色页／大厅，不读取原服账号配置。每位参与者选择不同的宿主角色；房间列表／详情、密码／难度／等级差／容量使用原MCP消息。
- 默认MCP 6113、D2GS 4000。两端均可用`--realm-port`／`--game-port`指定端口；本轮未配置系统防火墙。宿主可用`--debug-pipe <名称>`查看rooms／participants和管理明确目标，不向远程客户端开放管理命令。

共享房间持续按25Hz推进，某客户端失焦、ESC或退出不暂停其他人；私有一人内存实例保留单机暂停。正常退出应用保存所有参与者并结束监听；仅退本机角色不销毁其他人所在实例。当前同步玩家／穿戴／移动／自然边界及UNIT_TILE换区和同局聊天；已接普通怪物及有限战斗切片；精细房间兴趣、传送点／门户／任务旅行、全部非玩家实体／战斗、队伍和交易仍待实现。独立无图形服务端及其多实例验收留到后续，本次两个房间的观察只说明嵌入宿主的基础隔离。

局前界面资源为当前MPQ的`TCPIPscreen.dc6`、`3WideButtonBlank.dc6`、`PopUpOkCancel2.dc6`、`CancelButtonBlank.dc6`和`textbox2.dc6`，文案读取TBL 5116–5124；布局参考用户截图及本地OpenD2／OpenDiablo2，面板和按钮使用原生像素。原说明中的本地角色不表示新增上传能力：此实现角色仍全部保存在宿主。本次已实际查看首页TCP/IP、Host／Join页面与Join默认127.0.0.1。

## 流程与入口

普通启动：主菜单 → Battle.net → 登录或注册 → Realm → 服务器角色 → 大厅公告 → 点击创建或加入游戏 → D2GS 协议加载 → 重建当前幕活动地图 → 服务端世界显示。Single Player在组装入口连接预认证MCP，使用同一角色列表及GS客户端；自研宿主读写D2S，客户端不读取存档，也不启动旧GameSession。

| 页面／操作 | 当前源码 |
| --- | --- |
| 登录／注册 | 原 MPQ 图形、字体和文案；校验两次密码一致，SID 0x3D 成功后自动 SID 0x3A 登录；提交后记忆账号密码，下次进入登录页可直接Login；修改用户名立即清空当前和已记忆的密码；拒绝／超时后保留输入以便修改 |
| Realm | 首次自动选择配置项；未找到则显示服务器列表；Change Realm 关闭 MCP、重新取列表，选择后取新票据 |
| 角色 | 服务器列表分页；七职业创建、资料片固定开启、非 Ladder；Hardcore 先显示原警告；删除先确认，成功刷新；死亡专家与未知状态角色不能进入 |
| 大厅 | 默认原黑色公告区域，Create／Join 才切换表单，Cancel／表单 ESC 返回公告；SID 0x0A 后提交 0x0C 初始频道请求，0x0F 的 info／error 只读公告最多保留32条；不伪造服务器 MOTD，不提供频道聊天操作 |
| Create | 名称／密码／说明、人数 1–8（默认8）、等级差 0–99；等级限制默认关闭，关闭时提交99，手动开启沿当前等级差；普通／噩梦／地狱依据服务器 native 进度解锁；成功自动取票加入 |
| Join | 点击 Join 请求真实房间列表，查询期间逐条显示名称／人数；选中通过 MCP 0x06 查询详情。当前源码支持同一房间双击加入，右侧按原版截图显示经过时间、等级范围、金色姓名及灰色等级／职业，玩家列表可滚轮翻阅。再次点击 Join 可替换进行中的列表查询；可取消或按名／密码加入，网络复验输入后退役未完成列表。详情独立超时，迟到回复不覆盖新选择、不阻塞加入；本次双击／详情样式修正未构建或运行 |
| 等待／返回 | 独立 worker 持续收包／心跳，建局队列显示位置；取消列表返回大厅；创建／加入／连接游戏／握手等待取消时关闭旧 MCP／GS，保留账号连接并重新取服务器角色列表；已登录游戏的加载取消提交原0x69并有界等待退局；大厅 Quit、正常退局重新取票返回选角 |

`ProtocolReady`须原服加载完成、幕／难度／种子和本人GUID可用、本人单位名称／职业与所选服务器角色一致且坐标已分配；场景另须 `nativeMapReady`、`playerDisplayed` 与活动碰撞准备成功。左键提交移动或点击真实地图对象／NPC，R或底栏按钮切换跑／走；跑走偏好在换区／换幕后保留。Esc先结束对白或关闭交谈／传送点面板，再打开退出菜单。NPC对白和菜单复用既有字体／菜单绘制；回城门创建沿现有物品技能请求入口。新闻、广告、频道／聊天、账号设置、Ladder、转换角色和影片等非主流程入口暂缓。

角色名 2–15 字符，首字符英文字母，其余英文字母、连字符或下划线；classId 为 0 Amazon、1 Sorceress、2 Necromancer、3 Paladin、4 Barbarian、5 Druid、6 Assassin。初始属性、装备和 D2S 由原服生成；客户端仅提交 MCP 0x02 的职业／状态与名字。删角用 MCP 0x0A，command 要求 confirmName 完全匹配。注册账号／密码为 2–15 可打印 ASCII 字符，更细名字限制由服务器拒绝码说明。

肖像仅绘制已保存 native 1.13c、无组件染色且原 COF／DCC 完整的外观；legacy 新角、染色或未核实组合保留真实身份，肖像暂不绘制。建角使用当前 MPQ 七职业前端动画。

选角产品页只走RealmFrontend；旧app/character_frontend未编译／调用，保留作功能参考，不维护第二条本地选角执行链。最新源码沿旧选择器和OpenDiablo2 character_select核对双击同一角色1.25秒进入、滚轮／PageUp／PageDown翻页和两列四行布局；卡片步距读取当前MPQ charselectbox的两段总宽及原高度（当前272×92），标签及肖像位置对齐原布局。删除覆盖第四行的自绘文字翻页按钮，改用既有MPQ numberarrows放在列表右侧，显示页码；未声称完整还原原Scrollbar。Realm空白底图已只读查看，标题／名称没有烘焙文字；Current Realm、Realm名称及Change Realm按钮统一使用已有原font16，避免小fontexocet10在当前前端缩放下呈现细碎字形。字体字宽来自当前MPQ，角色列表、选择、创建和删除仍由MCP原协议驱动；Ladder／经典角色转换等既有不可用范围不变。本批只改源码／文档，未构建、运行或打包，文字效果和交互仍待运行验收。

创建／加入／握手超时同样重新获取服务器角色列表，不自动重发建房或加入。加载超时先请求原保存退局；退局等待超时后关闭旧游戏连接并重新取角色列表，错误明确标记保存结果未知。协议损坏、账号连接失败等仍结束会话，失败提示关闭后回登录页。既有有限观察覆盖加载超时后原生退局等待及再次超时返回角色页、保存结果未知；创建／加入／握手取消和其余恢复分支尚未逐项连服验证。

大厅按钮底纹黑斑来自错误的Units颜色映射：当前原Create Game帧大量使用251–254号索引，Units全为黑色，Act1保留原色阶。项目MPQ与原1.13c安装的按钮及Act1表哈希一致；原CelFileNormalize仅处理指针／缓存，不转换像素索引。当前六类大厅控件独立以Act1原表上传，背景及其它UI不改变；原Create Game帧有烘焙文字，不额外叠字。取消统一font16替换，大厅按D2Win BUTTON_Draw高度规则使用高按钮Exocet10、紧凑按钮Ridiculous，动态文案黑字，禁用状态保留原控件底图处理。此前把小字体当成根因及整体截图判定通过的结论撤回。证据在artifacts/lobby-legibility-20261007的create-verified、controls-verified和join-verified；构建无warning／error，诊断实例退出码0、stderr为空。原版精确亮度、所有状态及窗口尺寸仍未认证。800×600原生像素居中、局内画布及8人／默认关闭等级限制不变。

EXIT白边源码修正：当前MPQ MediumButtonBlank原帧为128×35，按本地D2MOO D2WinButton::BUTTON_Draw高度规则仍使用Exocet10。原Exocet10的EXIT字形使用15–32号灰阶索引，含亮灰像素；公共UiPainter在原字形可用时不追加描边，POINT采样也不额外插值。共用RealmFrontend::button此前只有lobbyText文案使用BLACK，其余按钮沿WHITE／GRAY乘色，原字形的亮色边缘直接显示。当前动态按钮文案按原非透明字形遮罩统一BLACK，保留笔画轮廓、不扩张／侵蚀字形；默认字体的高度分支同时推广到共用入口，≥35使用Exocet10、其余使用Ridiculous，Realm显式font16保留。未更换EXIT字体或按钮底图调色板，未改公共UiPainter的普通正文／物品文字。原资源只读提取位于忽略目录artifacts/button-font-20261007；已随多人批Windows Release构建及打包，EXIT具体视觉效果仍未单独运行验收。

Join双击复用现有选角的1.25秒窗口，绑定房间名称／MCP索引及连接／游戏代次；改输入、滚轮、点击其他位置、切页／刷新、模态及失焦清理点击记录。第二击沿原Join／密码提交链路，不重复查询、不要求详情Ready。右侧布局依据用户最新原版Join截图和当前joingamebckg.dc6，font8／Units PL2金色4与灰色5，字宽沿原TBL。MCP 0x06提供uptimeSeconds、creatorLevel／levelDifference、玩家姓名／等级／职业；时间显示服务器查询时的快照，等级范围取PvPGN game.cpp原限制与当前experience.txt MaxLvl（现表99）的可玩范围交集，职业读charstats.txt，缺等级依据显示未知。Elapsed Time及连接词to在当前三份英文TBL未找到，显示文案取用户原版截图，Level格式沿TBL 5017。三个可见玩家槽各显示姓名及等级／职业两行，其余用滚轮翻阅；本次源码尚未构建、运行或打包。

## 配置与职责

复制 [配置模板](../development/online.example.json) 到 online.local.json，或使用 `--online-config <路径>`。originalClientDirectory 相对配置文件解析，含同一 1.13c 的 Game.exe、Bnclient.dll、D2Client.dll，只读计算版本和 CheckRevision，不执行原 DLL。默认 authentication=pvpgn，SID_AUTH_CHECK 零 key，本机服无需 CD-key；严格服可显式设 keys。打包只复制 PvPGN 连接设置，私有配置不提交。

UI登录记忆由应用层[OnlineLoginMemory](../../src/app/online_login_memory.hpp)管理，按配置文件路径隔离。Windows使用当前用户系统凭据管理器，Linux使用配置旁权限0600的`.credentials.local`文件；密码不写入连接JSON、会话只读视图、日志或D2S，记忆文件已排除Git。修改用户名时保存新账号及空密码；返回菜单／取消登录保留输入，注册页返回恢复已记忆信息。`online-login`命令不改变UI登录记忆。保存失败会显示提示，但仍提交登录请求。UI记忆登录及快捷入局有有限历史证据，范围见本页验证记录。

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
| [TcpStream](../../src/network/tcp_stream.hpp) | Asio DNS／TCP、期限、有限队列、拥有线程 poll；关闭后旧回调不交付 |
| [protocol/](../../src/network/protocol/wire.hpp) | 边界检查、framing、认证、D2GS 压缩与拆包 |
| [Networking.cmake](../../cmake/Networking.cmake) | 固定 Asio 1.30.2／BNCSutil 快照，独立网络／协议／远端客户端目标；[来源／许可](../resources/THIRD_PARTY.md) |

网络公开头仅有项目值类型和标准库；Asio／WinSock／BNCSutil 留在实现，网络不依赖玩法、世界、存档、MPQ 或 GPU，保留 C++20／CMake Windows／Linux 路径。

## 会话与协议边界

公共方法在同一个客户端线程调用，底层持锁串行提交。私有 worker 每 10 毫秒服务传输、心跳、超时及有序副本更新；`tick()` 在客户端线程发布快照，也可立即服务一次。UI 停止绘制不停止网络。Idle／Failed／Cancelled 登录或注册；RealmSelection 选 Realm；CharacterSelection 创建／删除／选择角色或切换 Realm；Lobby 列表／建局／加入。列表取消保留 MCP，迟到回复不污染后续请求；游戏请求取消退役旧 MCP／GS 并重新取角色列表，已登录游戏则先原生退局；其他账号／角色等待取消结束会话。不自动重发角色消费操作。UI／command 不把 accepted 当作服务器成功；失败阶段的迟到输入不覆盖原连接错误。

read() 借用客户端快照，有效至下一次 tick 或公共修改／随后 read；后台回包不改写该快照。connectionGeneration 管登录生命周期，gameGeneration 管游戏连接，消费者按当前代次丢弃旧包。RealmSession 已将支持的回包归并至 read().world；仅 `RealmSession(true)` 显式保留有界原包供额外消费者；默认 UI 使用已归并快照，不积累无消费者的原包。启用者仍必须持续排空。world.areaGeneration 在 LOADACT／UNLOADACT 时递增，仅清理区域实体／房间，保留本人身份、装备和属性；1.13c 首次入局在 LOADACT 前已发送这些角色数据，不能一并清空。跨幕撤销旧位置，首个 LOADACT 可保留已收到的出生位置。断线／取消／退局／新局清空全部副本，显示绑定按代次失效。所有原服动作提交复验已发布快照与后台当前的连接／游戏／区域代次及本人GUID；加载期间换区的旧输入被拒绝，调试输入队列也清除。退局和新局统一清除门户请求、NPC初始化、限流／等待计时器与心跳；新局首个pong前延迟为null，不能显示为已测零延迟。NPC明确拒绝不被后续相关更新标记覆盖。错误具有独立本地sequence，界面关闭错误后不因世界／心跳revision变化重新弹出同一错误。退局、返回选角和 Realm 切换重新取票；保存成功须由重入结果或服务日志确认。

正常关闭窗口或 command `quit` 时，已进入游戏的会话先提交 0x69，持续 tick 至退局响应／关闭或阶段期限，再注销并结束进程；不把 quit 的 accepted 当作保存回执。online-cancel／online-logout 仍属于显式关闭连接。客户端不写 Realm D2S，服务器负责保存。

- profile 为本机 PvPGN／D2CS／D2GS＋LoD 1.13c，旧式 SID 登录与 IX86ver0..7.mpq。CheckRevision 公式先检查再交解析器；原文件路径暂要求 ASCII。NLS／Warden／扩展反作弊未实现，本机 Warden 禁用，严格版本校验未验证。
- SID／MCP 独立累积半包／粘包；D2GS 压缩模式 0／1、长度头与 Huffman 解压，未知长度明确失败。长度表核对本机 D2Net.dll 0xA900、D2MOO／OpenD2；0x7A 为 13 字节，不沿 JS 示例的 3 字节。
- 加载 0x02 后发 0x6B；0x03 保留幕／种子／townArea／secondarySeed，0x04 完成、0x0B 本人单位绑定。townArea 不代表玩家当前区域，secondarySeed 尚未驱动 DRLG。
- 累积各限 256 KiB，单次解压 64 KiB，可选原包队列 4096 包／2 MiB；默认阶段期限 15 秒、心跳 5 秒。建局 MCP 0x14 记录队列位置并刷新期限；取消关闭会话，不宣称撤销已完成的服务端操作。
- 参考服空列表不发终止包时，超时返回 Lobby、保留已收到的列表及 packetId=0x05 超时诊断，gameListComplete=false；Join 页内提示不完整，不用等待／超时弹窗遮挡列表，不能把无响应当作已确认空列表。列表按角色 Hardcore 请求，Ladder 房间不展示。
- 33 字节预览接受 version 4／10／13；native 1.13c 的 14-bit client flags 给出职业／等级／状态／进度，byte 30 是公会徽章颜色。外观保留类别核对 D2Common.dll 0x9D888，具体 item code 动态读 MPQ。
- 密码、key、哈希和票据不进入视图／日志；各层清理自身副本，调用方负责输入副本。独立D2S编码不变。

证据为本地 PvPGN common/bnet_protocol.h、common/d2cs_protocol.h、bnetd/handle_bnet.cpp、d2cs/handle_d2cs.cpp／handle_bnetd.cpp；布局及动画参考本地 reference 和既有离线选择器，图形文案读取当前 MPQ；参考仓库不提交。

## 协议覆盖目录（1.13c 当前源码）

下表区分能分帧、字段已消费和语义仍未知；没有以收发成功计为玩法成功。动态字段定义与取证入口见相应源码及[资料来源](../resources/THIRD_PARTY.md)。`online-status.protocol` 按 SID／MCP／game 分别返回包 ID、received／sent／unconsumed、协议逻辑字节数和 lastReceived，不保存或输出原始认证载荷。错误回执的 packetId 保留 dispatch 来源；未知流长度仍立即失败。

| 方向／包 | 长度／分帧 | 当前消费 | 字段／关键性与缺口 |
| --- | --- | --- | --- |
| SID 双向 | FF／ID／16-bit 总长；独立 256 KiB 累积 | 0x50／51 版本认证、3D 注册、3A 登录、40 Realm、3E 取票、0A 选角聊天环境、0C 初始频道请求、0F info／error 公告、25 ping | PvPGN 头与 handle_bnet／message；密码／key／票据不入视图；4C 认证模块拒绝；0F 其他事件仍记为未消费，频道聊天与账号服务未完整实现 |
| MCP 双向 | 16-bit 总长／ID；独立 256 KiB 累积 | 01 启动、19 角色、07 选角、02 建角、0A 删角、03 建房、04 加入、05 列表、06 详情、14 队列 | PvPGN d2cs_protocol／handle_d2cs；详情固定16槽职业及等级，不采用JS示例的变长数组；真实请求 ID 与拒绝码；Ladder、完整分页／过期服务未完成 |
| D2GS S→C 0xAF／01–06／8F／B0 | 原 1.13c 长度表；AF 动态压缩模式 0／1 | 握手、难度／资料片、幕／种子、加载、心跳及退局 | D2Net.dll 0xA900＋RealmSession；校验幕0–4、难度0–2和标志范围；Ladder 仍明确拒绝，secondarySeed 含义保留未知 |
| D2GS S→C 07／08／09 | 固定6／6／11字节 | 有序房间进入／离开、对象指派 | room／tile／level、对象GUID与真实身份；地图历史4096条，丢连续性拒绝重建 |
| D2GS S→C 0A–11／15／16／18／95／96 | 原表固定；16为word长度；18／95／96资源位流 | 单位增删、玩家／怪物动作、坐标／不连续移动、资源与本人身份 | 可见单位8192上限；身份与动态字段保留 optional；完整特殊移动及其他玩家私有字段未覆盖 |
| D2GS S→C 19–1F／21–23／7B／94 | 固定与原技能列表 count 长度 | 本人绝对属性、技能选择／基础／加成、技能列表 | 状态更新／成长确认；不是通用事务ACK，完整派生显示未完成 |
| D2GS S→C 27–2A／77 | 固定40／103／97／15／2字节 | 原NPC对白、私有／公共任务字、商店结果和容器打开 | 本人私有／公共任务字和日志状态分开；完整五幕资格／奖励未完成；商店精确报价待接 |
| D2GS S→C 3E／42／97／9C／9D | 3E／9C／9D byte长度；42固定6，97固定1 | 原物品位流／基础属性增量、光标清理、武器组及异步库存 | 参数宽度／物品布局读MPQ；完整染色／名称／全部属性消费者未完成 |
| D2GS S→C 4C／4D／99／9A／73 | 固定16／17／16／17／32字节 | 技能动作、目标、原飞弹通知和有界事件 | 本地 monotonic 收到时间仅作诊断，不是线上字段；全部技能客户端程序未完成 |
| D2GS S→C 51／59／60／63／82／8E | 固定14／26／7／21／29／10字节 | 对象／玩家指派、移出视野、旅行／传送点、门户、尸体归属 | 0x0A仅撤销空间指派、0x5C才移除名册；完整多人门户与尸体权限待接 |
| D2GS S→C 67–6D | 固定16／21／12／12／16／16／10字节 | 原怪物路径、动作、生命比例 | 速度／尺寸读MPQ；原动作12／13映射SKILL1；真正序列消费技能通知，保留wireAction |
| D2GS S→C A3／A7–AA／AC | 原表；A8／AA／AC byte长度 | 状态快照／开关、位流、NPC身份与外观前缀 | 位宽由MPQ消费者解析，状态历史64条；不代表全部状态效果可画 |
| D2GS C→S 15 | ID／type=1／language=0、text NUL、空receiver NUL、零扩展长度；最大261字节 | 局内普通ASCII广播，RealmSession::send_chat、Enter聊天UI及online-send-chat | Clt15／PlrMsg及D2Net SERVER_GetClientPacketSize核对；仅入队、无本地回显；未运行验证，其他编码暂缓 |
| D2GS S→C 26／5B／5C／75／7F／8B–8D／90 | 26双NUL字符串，发送者≤15字节、文本≤255字节；5B word长度；其余原1.13c固定表 | remote_social归并聊天原字节、名册、队伍／关系原值和公开位置；普通消息投影至左上及M日志 | 无关系资格解释；7F非玩家分支仍计unconsumed；不创建视野外单位；聊天新UI未运行验收，逐包认证仍有限 |
| D2GS S→C 5A／5E–5F／7D–7E／81／A4–A5 等 | 原表固定 | **能分帧但未消费完整语义**，unconsumed 按 ID 暴露 | 系统事件、任务／宠物等关键后续状态；M4／M7／M8／M9仍未完成，不能归为无关消息或宣布M1全覆盖 |
| D2GS S→C 其他有长度包 | 原始固定表或已核实变长程序 | 未消费ID公开计数 | 不把能跳过当成功；未知长度／未核实变长／Warden明确失败 |
| D2GS C→S 行走／交互／旅行 | 原01–04／13／49等请求编码 | 当前地图资格＋完整坐标终点／原GUID靠近、传送点／退出 | 位置信息始终以服确认；取消只结束客户端未发意图 |
| D2GS C→S 库存／城镇／战斗／成长 | 原物品27类及技能／属性请求 | 远端领域协调器资格、限流、关联更新、拒绝／超时 | 无通用ACK／事务号；非幂等操作不自动重发；统一全部领域协调尚未完成 |

任务／提示另消费0x50／52／5D日志及进度、0x8A NPC提示；0x2C事件2仅产生公共升级表现事件。字段到达不等于完整剧情／升级验收。

## 共用游戏界面

底栏、背包／装备／腰带、人物／技能树／技能选择、箱子／方块／金币、NPC对白／菜单、普通商店、传送点、任务日志、佣兵和Esc／选项面板共用既有SceneView／SceneController及原MPQ资源加载入口，地面物品原图／品质色标签／拾取热区、鼠标光标和敌人血条同样共用；联网独立HUD已移除。联机通过RemoteUiClients实现同样的IActor／IInventory／ICharacter／INpc／IQuest／IMap端口，不构造离线GameSession；资源、几何、面板状态和命中处理不另写联网版本。网络 worker 在资源加载、窗口等待和界面绘制期间持续收包、心跳和更新权威副本；资源回调不再重入会话。客户端线程通过 tick 发布快照，后台线程不修改表现正在借用的容器。

已绑定底层命令的界面操作：左右技能选择与基本PvE施放、属性／技能加点、物品拖放／装备／武器组／腰带／容器转移／交换／堆叠／装书／镶嵌／鉴定、金币、合成、普通NPC交易／维修／凯恩鉴定及传送点。组合库存操作逐步等待服务器光标赋值；每步保留 UI 投影时的 OnlineIntentContext（连接／游戏／区域／本人／交互代次及 NPC），发送端在锁内复验；关闭后重新交谈同一 GUID 也使旧队列失效；快照保留原GUID／revision，失败或超时中止，未确认的位置、金币、属性和技能不写入副本。UI关闭消费原手势，侧栏变化同时调整世界视口，换区保留同一套面板对象，退局后释放角色快照和待处理命令。

未知原服属性和攻击面板计算标为`?`，不按单机规则补造。商店价格暂交原服决定并明确提示，quote 返回未知，canRequestSale 单独表达能否提交出售请求；UI不显示伪造的零金币价格。任务日志投影原生私有记录／日志状态，细分文字和完整资格仍有缺口。佣兵资料／服务、交易／赌博、任务物品特殊服务和分堆协议未接，入口禁用或反馈限制；原界面实现仍共用，后续只补适配器。F1–F8原生绑定及重入恢复见本页流程入口。完整技能效果、球体状态变色和物品染色仍待接。

地面金币取原数量，落地图读取当前Levels.Pal；敌人名字读MonStats.NameStr／TBL，生命按原0–128刻度并区分0x0C的暗金标志。物品Take组合等待真实光标回包及既有请求间隔，缺回包按会话超时结束、不自动重试。回城卷轴／书右键通过RemoteControl创建门户。联机Esc菜单不暂停服务端，Save and Exit提交退局；死亡回城与尸体取回接口已接，确认与验证边界见本页死亡章节。

## 战斗与成长底层

普通攻击、左右技能选择、坐标／单位施放、单次Hold请求和原0x12停止接口由command调用；共用游戏UI已接基本PvE施放、选择和成长。技能、目标、职业／前置／等级／属性／最大等级读取当前MPQ，服务端仍检查最终资格。单位目标首批限PvE敌对怪物／合格尸体，NPC、中立／友方、玩家与佣兵目标拒绝。普通Attack依据D2Common原初始化规则识别，innate独立标记，不伪造技能回包等级。

- 0x21分开保存基础与装备加成，有效等级为二者之和；0x94更新基础等级但保留已收到的加成。0x23分别维护左右选择及原owner。普通技能0x3C使用owner=-1；初始选择未确认正常owner时先显式选择，不支持充能物品owner。
- 0x3B学习／0x3A属性加点等待基础等级／绝对属性变化；Confirmed表示观察到对应服务端结果。Pending互斥物品、回城门及其他成长请求；100ms限流、死亡／换幕中断、超时结果未知，不自动重试。
- 施放／停止没有通用原生ACK，记录SentNoAck。客户端不扣生命／法力、不算伤害、不按施放意图生成战斗单位。Hold仅提交一次，后续调用方决定是否持续提交；0x12原处理仅清地狱火状态，不能声称通用撤销技能。
- 接原0x0C命中、原玩家动作／死亡、0x67–6D怪物原动作及权威位置、0x11叠层、0x4C／4D／99／9A／A3技能和0x73飞弹消息。原怪物动作字节转换为MONMODE，12／13歧义保留wireAction；0x67是目标位置，0x68是当前位置和目标GUID。生命比例保留原字节尺度。
- 战斗事件队列最多256条，按sequence有序；0x73无权威飞弹GUID，保留类型、所有者、当前位置／首路径目标和pierce，不伪造实体；首路径点改名为missileDestination，避免误当出生点。已接范围与表现限制见本页“联机游玩表现与输入”。
- A7／A8／A9／AA状态消息按单位有序保留；RemoteCombat依当前States／ItemStatCost的Send Bits、Send Param Bits、Signed解码，不套D2S Save Add或ValShift。状态队列最多64条，缺前缀／未知参数明确decoded=false，等待完整AA恢复，不猜状态。
- 1A／1B经验正增量、1C绝对经验；绝对等级与可用点仍读原1D–1F。全部职业技能、PvP、宠物／佣兵目标和充能／特殊物品技能仍有限；死亡／回城／尸体的有限证据与未复验修正见上节。

## 原服物品与请求

- 0x9C／0x9D保留真实GUID、所有者、服务端位置／模式及原位流。客户端序列无JM头、物品种子或Realm尾；地面／掉落模式用16-bit全局坐标，其余用原body／格子／page。0x9C本人库存允许先于0x0B到达，绑定后补本人所有者；镶嵌子物品保留type4宿主GUID。
- 解码compact、耳朵、金币、任务难度、quality 1–9、未鉴定字段省略、图形变体、词缀编号、个性化名字、数量、耐久、防御、孔数及主／套装／符文之语属性列表。尺寸、部位、属性宽度／Save Add／参数、书本配对均读当前MPQ。协议name字段为基础名，原名称／属性经共用提示投影，完整特殊名称／属性消费者仍有限；属性列表值为序列化单位，尚未应用ValShift，0x3E基础属性更新另列baseStats并保留服务器单位。
- 0x3E按1.13c实际变长包读取变宽GUID／数值、基础属性标志和参数，数量／耐久更新与请求关联均使用实际长度。0x0A删除物品；0x42按所属玩家清理Cursor。9C／9D移出容器／腰带按实际模式处理，合并在同包的Cursor转移保留，旧容器记录删除；IFLAG_DELETED本身不直接删除。0x97确认武器组。换幕保留本人库存及孔内子项，新局／断线清空。
- command支持拾取至背包或Cursor、拿起、放入／交换背包格、Cursor丢弃、装备／卸装及两手武器交换、腰带放入／拿起／交换／使用、堆叠、装书、卷轴／书鉴定、镶嵌及切换武器组。原拾取由服务端寻路接近，沿原50单位距离限制；不自动发放物品或改变客户端位置。需求／职业、同类堆叠细则、效果、扣减、最终装备和保存由原服决定。
- 操作前复验当前代次、GUID／revision、本人所有权、Cursor及MPQ格子／腰带／部位；未知或截断数据拒绝。只允许一个Pending物品请求，100ms限制、不自动重试；storage-close可取消等待。accepted仅入队；Updated需复查位置／数量，TimedOut结果未知，死亡／换幕／退局标Interrupted。NPC交易等待0x2A明确回执，原错误码标Rejected；关闭存储无原生确认，标SentNoAck。
- Pickup等待允许新有效移动／交互或施法替换旧意图，并标Interrupted；相关物品更新及超时清理对应拾取显示目标，迟到回包正常消费。其他库存Pending仍互斥。局内错误提示退出原游戏后退役，不在选角页重新弹旧导航拒绝；真实新错误仍显示。本批只修源码，具体衣服未运行复现。
- page快照为原InvPage+1：1背包、4方块、5私人箱；place使用native page=0／3／4。箱子由真实Objects.OperateFn=32单位交互，方块由原物品使用请求打开；仅0x77确认后开放对应格子。布局读当前Inventory.txt：资料片箱子6×8、方块3×4。关闭／死亡／换幕清理上下文，禁止把已打开的方块放进自身。
- 0x4F提交存取金币、关闭箱子／方块和合成，金额高WORD在前；0x50丢金币。0x19为金币正增量，0x1D／1E／1F为绝对属性，0x2A保留交易金额但不重复写钱包，避免出售双计。合成配方、产物和金币上限完全由原服决定，不接本地方块事务。
- 当前NPC交谈提交0x38 action=1生成普通货架、action=2请求原赌博货架；9C action=11／12维护服务器物品。单件购买0x32（赌博交易类型2）、出售0x33、单件／全部维修0x35、凯恩批量鉴定0x34由原服判资格及结算，0x2A保存结果／物品GUID／金币。共用报价覆盖悬停、购买确认、全部维修合计及online-item-quote，UI／debug买卖发送前重算并绑定revision，网络锁内复验货架／服务模式。维修限原五个铁匠，鉴定限原五幕凯恩；赌博NPC依原Dialog处理器。以上源码未运行验收，多买、玩家交易、佣兵装备仍未完成；本人尸体取回消费原服库存结果。
- 回城卷轴／书创建门户仍用online-town-portal；通用use效果与目标选择由回包决定。库存／商店UI及原属性提示已接共用面板；地面原图／品质色标签及拾取命令也已接共用实现；孔内完整显示及未识别的特殊名称仍有缺口。

## 联机死亡与尸体

- 死亡依据原0x0D动作8／9和0x0E的PLRMODE_DEATH=0／DEAD=17，保留Dying／Dead。资源回包只以正生命到零的实际变化补足死亡开始；死亡存档重入时的初始零生命不能覆盖原服站立动作、锁住尸体取回。正生命回包不能自行解除既有死亡。D2MOO `sub_6FC82360`可省略生命零值通知，故不能只用life==0驱动界面；动作19仍是校正。只读`world.dead/deathPhase/deathRevision`共用于UI、移动、战斗、物品和交互资格。
- 共用死亡文字、原字体、死亡动作／DD缺图时DT末帧及UI关闭逻辑。死亡结束持有手势、分段移动、待发施法与物品组合，关闭所有玩法面板／对白并屏蔽输入；原服仍持续运行，怪物和其他玩家照常显示。地图暂时不可用时仍显示死亡提示并接收Esc。
- Esc／`online-resurrect`只建立回城请求，先等待服务端DEAD，再单次发送原0x41；收到原角色重新分配／存活站立模式及位置，或0x41之后的三项绝对资源恢复与0x15位置校正组合，才解除死亡。旧资源样本清掉后读取原服绝对属性／后续0x95，不把生命、法力或耐力设为本地最大值。`world.respawnRequest`给出WaitingForDeath／Sent／Confirmed／TimedOut及sent；重复Esc不重复发包，超时保留死亡状态、不自动重试，迟到的原服确认仍可消费。Hardcore继续操作走正常退局，不能复活；角色死亡位与尸体持久化由原服保存。
- 原0x8E记录／解除corpse GUID与owner的关系，独立于本人死亡状态和空间单位分配，跨区保留归属、重新加载真实可见单位。尸体使用同一职业原DD／DT末帧、名字与MPQ `corpse`文字、高亮；本人仍在死亡位置时避免自身与新尸体重叠绘制。`world.corpses`给出GUID、owner、owned与当前可见坐标；只有本人的真实可见尸体成为`scene.mapTargets`的type0／corpse目标。
- 点击尸体或`online-recover-corpse`（unitId）复用共同绕障／分段靠近，原服距离≤8后发送0x13 type0交互。accepted不等于装备已取回；库存、装备、经验、金币及0x8E解除／单位删除都等待原服回包。空间单位暂时隐藏不擅自删除归属；部分回收后原服未解除的尸体继续保留。未开放其它玩家尸体的loot权限或交易入口。

## 多人只读副本

玩家空间显示使用原 0x59 身份／坐标、0x0F／10 移动、0x0D 动作／生命及0x4C／4D技能；每位玩家各自保有公共动画和连续显示路径。0x59 的新指派清除同GUID旧显示缓存，原空装备流允许裸装，不因未收到物品而隐去人物；0x0B 只回填本人坐标，不把其他玩家放到本人位置。0x20 按当前1.13c十字节包保留公开属性（byte属性ID／有符号32位值），不使用D2MOO扩展word ID，不覆盖本人私有属性。0x74／8E消费原尸体归属，死亡单位停止移动／声音，原freeze状态也可冻结玩家表现。0x0A／5C移除远端玩家时一并清理其装备、物品及孔内项；本人临时空间移除仍保留服务器库存。

原图绘制只读实际职业／装备与当前MPQ，鼠标悬停活的其他玩家显示真实角色名及轮廓，不授权交易／PvP请求。`online-status.scene.players`区分本人与他人、本帧可见／不可见原因、移动／死亡及显示坐标，名册身份不能作为空间可见证据。参考入口为D2MOO SUnitMsg::FirstFn、PLAYER_SynchronizeItemsToClient、PlrMsg动作表／sub_6FC82830、SCmd::0x59／74／8E，以及PvPGN MCP 0x06处理器；MPQ及参考源码不进入提交。

`contracts/online_social.hpp` 与 `client/remote_social.*` 单独维护名册、聊天和关系，原包字段以当前1.13c长度表及本地D2MOO的D2PacketDef／SCmd交叉核对。0x5B确认名册身份，0x75／8B／8C／8D维护原队伍／关系值，0x7F玩家分支维护公开区域／生命比例，0x90维护公开32位坐标；先到更新保留未知身份。原生命比例、队伍ID及flags保持原值，尚未解释为UI资格；0x75的关系flags与0x5B的partyFlags分开。0x7F非玩家分支、系统事件及队伍／关系发送仍未实现。

空间单位与名册分离：0x0A不退出名册，0x5C移除成员及其关系；离开视野不会生成幽灵角色，也不清除未收到原删除的尸体归属。本人换幕保留本局名册／关系／聊天，退局／断线／新局清空。名册暂存上限64（缓冲保护，不是允许64人游戏），关系4096，聊天256条。诊断以nameBytes／textBytes保留原语言字节，避免将原编码误当UTF-8；界面仅投影已核实的普通消息，未接组队按钮或敌对资格，不算M4完成。

聊天发送源码：`RealmSession::send_chat`要求ProtocolReady及当前连接／游戏／本人身份，跨区、死亡、库存／NPC面板不作为聊天禁用资格；拒绝空白、非可打印ASCII及超过255字节的文本。`protocol::game_chat`编码普通0x15：type=1／language=0、文本NUL、空收件人NUL、扩展长度0；末尾额外长度字节依据`reference/d2moo/source/D2Net/src/Server.cpp::SERVER_GetClientPacketSize`，不能仅发两个字符串。D2Game `PLAYER/PlrMsg.cpp::Rcv0x15_HandleChatMessage`由服务端真实角色名生成0x26，并按原关系／交互限制转发，发送者也等真实回显；不插本地消息或伪造ACK，不自动重试，不停止原服移动／施法。接收分帧限制姓名15／文本255字节，支持跨包与同帧后续包。原0x26的unitType=2／GUID=0不代表发送者GUID；`nNameColor`在普通玩家广播中是发送者等级，不直接作为字体色。原类型1／2／3／4／5／6／7仍按原字节保留，私聊／表情／头顶消息、BNCS频道命令与中文编码未接发送。

聊天界面依据与源码：用户提供原版底部输入、左上回显及M消息日志三张截图，历史最新消息在上、真实角色名深金色／普通文字白色、标题Message Log、底部Close、右侧滚动条。OpenDiablo2 `d2game/d2player/key_map.go`核对Enter／M，`d2common/d2resource/resource_paths.go`及`d2core/d2gui/box.go`／`layout_scrollbar.go`核对boxpieces／textslid原帧用途。已只读导出当前MPQ：boxpieces为22帧14×15，textslid为17帧12×13；TBL的strMsgLog／strClose存在；对照截图字形和尺寸选font8消息、font16输入／标题，字宽沿原TBL。`presentation/hud/chat_view.*`独立维护只读消息、编辑草稿、光标及滚动；边框步距从原帧尺寸读取，位置按截图800像素基准适配当前HUD尺度。Sky原PL2十三种字体索引变换按需上传，白色保留原索引；messageColor控制正文，姓名色独立于等级字段，支持原FF c0–c9颜色串（本地d2bs Print用例补证）。标题／Close动态读当前MPQ，不把截图或参考资源导入产品。原截图及原资源核对仅保留在忽略目录artifacts/chat-ui-20261007。

用户补充原版入房截图中的三条消息已核实为当前参考服公告：D2GS `D2GSCBEnterGame`将角色加入MOTD队列，`D2GSSendMOTD`按顺序发送商业用途提示、配置MOTD、World Event提示（本地参考服源码见artifacts/d2gs-local/relesgoe-source/src/d2gs/handle_s2s.c／d2gamelist.c／d2ge.c）。此前实际多人包快照artifacts/multiplayer-20261007/package-ready-a.json的world.social.chat已收到同样三条：均为0x26 type4、language0、messageColor1、姓名字节[administrator]；第一条含FF c8橙色，Hello world!沿包的红色，第三条含FF c9黄色且本服key item为空。当前聊天源码已支持type4无姓名前缀和这些颜色串，同样进入顶部与M日志；文案、颜色及顺序来自原服，不在客户端写入固定欢迎消息。旧包接收证据不代表本轮聊天UI已运行验收。

Enter打开底部输入，再次Enter提交普通广播；空白Enter关闭，Esc丢弃草稿。Backspace／Delete、左右／Home／End编辑光标，255字节限制，非ASCII输入明确提示；提交拒绝保留草稿。M／底栏Message Log打开历史，M／Esc／Close关闭；滚轮、上下、PageUp／PageDown、Home／End及原箭头／滚动条拖拽翻阅，新消息到达时保留正在查看的旧行锚点。输入／日志占用整个开启、编辑和关闭帧，隔离走跑、自动地图、背包／技能、药剂／换装及鼠标玩法输入；持有按钮需重新释放，服务器／网络不暂停。关闭与新局不重放消息或玩法意图，换区保留本局历史。

当前显示限language=0的普通type1／server type3／noname type4及ASCII正文，其余真实原包保留但计chatUi.unavailableMessages，不猜私聊／表情／线索格式。用户补充原版实测：每条显示10秒、新消息在下、最多15条，左侧角色等面板打开时右移。顶部源码取最新15条可显示消息，按收到顺序向下排列，以原消息receivedMilliseconds的单调时钟独立过期；新消息、打开／关闭日志及面板不重置寿命。绘制使用SceneView::worldViewport的左边界和可用宽度，重新换行并裁剪在世界区域内；过期只隐藏顶部消息，不清除本局最多256条历史，M日志继续沿截图最新在上。中文原编码／输入法、频道、私聊及全部窗口／字体精确一致性仍暂缓。聊天协议和上述基本UI已随此前聊天／交易批构建及有限冒烟；当前源码另取消文字的额外约1.33倍HUD放大，font8正文／font16输入及标题保留原像素大小，字宽、换行、光标和行距同步，边框／面板布局不变。此文字尺度修正未构建、运行或入包。

`online-social`／`online-chat`是完整只读快照别名，均返回world.social；操作仍以服务端回包为准。以上包尚未逐包运行验证，JS参考中的错误长度／位宽不采用（例如0x7F区域按原包16位），不将参考表纳入源码。

## 联机游玩表现与输入

### 玩家交易邀请

`OnlineWorldView::playerTrade`与NPC交易状态分开。城镇左键点击活着的其他玩家，经RemoteTown／RemoteControl的原距离≤8靠近与0x13玩家交互发起；0x77 action0等待、1邀请、6打开／清除双方同意、5对方同意、14禁用同意／15恢复、9／10无空间、12关闭、13完成。邀请没有身份字段；真实对方姓名和GUID只取0x78，不从0x76或邻近玩家猜测。死亡、换区、对方移除、退局、断线清理。

邀请用0x4F button3接受、button2拒绝／取消；已打开的报价用button4同意、7撤销双方同意、8报价金币。金币高16位先于低16位，0x79 flag1更新本人、flag0更新对方；钱包和物品由原服最终处理。本人绿勾表示同意请求已发出，没有自己的独立同意ACK；只在原13确认完成。报价变化、原6清除勾选；原14／15决定取回物品后的锁定，不自造解锁计时。金币报价若对方已同意，按TCP顺序先发7撤销再发8；等待本人0x79期间不能同意。未确认响应超时保留状态，可显式取消，无自动接受或重试。

当前MPQ的trade.dc6四帧、buysellbtn.dc6第16／17勾选帧及第10／11取消帧、800borderframe、font16／Sky、TBL用于完整左右面板；第12–15帧是左右箭头，不用于灰色勾选。绿色勾选保留原勾与边框，内区按当前Sky PL2绿色变换生成，原版专用变换尚未认证。用户提供接受后及放入手套／绿勾截图作为布局依据：左侧上方对方报价，下方本人报价，右侧本人库存；格子坐标／容量读取inventory.txt的Trade Page 1-2／2-2。点击本人Gold字段用原dialogbackground和strTradeGoldHowMuch打开数字报价，Enter提交／Esc取消编辑；该弹窗布局参考本地OpenDiablo2 MoveGoldPanel，尚未与原版交易金币弹窗截图认证。物品格底色及全部像素配对仍未认证。

NPC服务／Talk菜单和发起／接收交易邀请共用OriginalMenu（presentation/graphics/original_menu.*）：当前MPQ boxpieces、font16／原TBL字宽及Sky PL2金色／蓝色变换；按用户Warriv／Akara／等待交易截图统一金色NPC标题、白色居中选项、蓝色悬停。无悬停时邀请选项保持白色，键盘导航可显示选中项。菜单边框与文字／命中区域共用，业务动作及资格由调用方处理；没有另画NPC边框或按字符数猜宽度。boxpieces底边帧的墨迹位于顶端，拼接按当前原图的九像素差与侧边偏移修正，消息日志也复用该边框入口。精确菜单锚点及原版完整状态仍待对照。

服务器实际本人报价为原INVPAGE_TRADE=2（位流page3）；对方报价是原服复制到本人库存的INVPAGE_EQUIP=1（位流page2）临时副本，独立只读容器，不能取出／交换／装备。原服action5／mode4／page2撤销临时副本时直接清理，不将副本当作真实Cursor（D2MOO PlrTrade::sub_6FC931D0与ItemMode::sub_6FC446B0）。本人背包↔报价使用既有Take／Place／Swap和原0x19／18／1F；拖拽、Shift快捷放入／取回及悬停共用库存投影。发送前核对游戏／区域／交互、物品revision与原格子尺寸／占用；同意另外绑定交易revision，物品增删／属性更新及金币回包使旧意图失效。待处理库存队列、光标物品、本人已勾选及未齐响应阻止再次同意或编辑。关闭只消费原服实际回包，不本地交换或修改保存格式。

TradeInviteView／SceneView只读状态并提交意图；交易关闭旧交互面板，打开本人库存，输入继续被隔离，网络不暂停。邀请Enter接受、Esc拒绝；报价面板勾选／取消鼠标同项按下释放，Esc取消整个交易（金币编辑时先关闭编辑）。报价期间禁用移动、战斗、消耗／装备等普通操作，保留本人背包／报价物品移动及取消。当前源码修正capturesWorldInput的交易标记误遮挡本人Cursor图标／落点高亮，只在库存绘制时允许已打开报价面板，保持其他模态状态及玩法输入隔离；此显示修正未构建、运行或入包。聊天草稿／历史保留并继续接收。

本轮构建、包身份及实际验收以[当前批交付](#当前批交付)为准；源码支持不等于物品／金币实际成交和完整原版界面认证。

世界绘制、动作／原图、手势、人物／技能／任务提示、自动地图和声音只有公共消费入口。文件归属见[架构](../architecture/OVERVIEW.md)、[客户端](CLIENT.md)及相关模块；女巫原表与显示公式不重复实现。

- SceneController 负责首次按下、按住目标锁定、Hold、松开、失焦与 UI 消费；RemoteControl／Combat 提交原请求。0x12仅有原地狱火停止语义，不是任意技能撤销。长按行走在原服到达后继续，未移动指针时固定世界目标。
- 位置副本与连续显示位置分开。路径目标只在与当前意图一致时用于显示衔接；无原生请求号，回包代次不是 ACK。15秒无原服位置进展才超时；0x15明确校正、受击、死亡和传送独立处理。移动→施法保留当前连续动作交接位置，清移动请求不撤销显示进度。
- ActorAnimationCatalog／State 统一组件、模式、AnimData和MonSeq；空变体且原索引0省略组件，不丢整个NPC。固定Utrans和当前MPQ调色读取共用路径。嵌套序列／非零方向覆盖等不足证据时不可用。
- 本人原服通常省略技能动作通知，因此已发送请求驱动本人显示衔接；强制同步去重。动画／弹体不扣资源、不判命中、不生成战斗单位。
- ClientSend只消费0x73原坐标、首路径目标、剩余帧、等级与穿透；无弹体GUID。已有程序1／5／6／8／9／18／19／20、原25Hz运动、充能弹／扇形／新星、部分拖尾与冰封球显示；接触只控制画面，未收到穿透次数不掷装备概率。
- 原任务／NPC事件、技能、角色受伤／死亡和怪物动作转换成公共声音事件；SoundCatalog解释当前MPQ的Sounds／MonSounds／技能声音，SceneAudio消费选择、概率、延迟、脚步和待机间隔。SoundBank处理原声组、Compound及Stop／Defer；换区／打断／暂停恢复清过期事件。不维护两套MonSounds解释。

限制：引导箭／骨魂追踪、连锁跳转、炮轰序列、持续射流、部分毒云／药瓶客户端程序、完整速度状态、精英染色／特殊死亡演出仍未完成。完整光照、空间衰减、循环音和非零FsOff缺少足够原客户端证据。scene.effectLimitations只记录遇到的限制，空数组不认证全部表现。职业及原版证据见[公共技能](../gameplay/skills/COMMON.md)、[怪物](../gameplay/world/MONSTERS.md)与[资料来源](../resources/THIRD_PARTY.md)。

## 当前批交付

当前野外物件批（2026-10-07）：用户授权全部源码完成后打包，后续明确实际操作验证交由用户。Windows Release首次58步及后续8步增量构建成功；dist/current已更新，EXE SHA256为`8445EBDB3A745F166F7A03F91CA766FBC57D3BECE16C08228E21F7F939DE467C`，与build/bin一致；协议DLL仍为`E96C38DE1911BF24292EE726BB1CF862E7D1CF0D0DF816EDE565A213667B0CBD`。对象族清单、静默ENDANIM共享模式投影、原模式声与钥匙／陷阱声、新星／雕像／毒云表现及未核实缺口见[物件](../gameplay/world/OBJECTS.md)。没有新增测试脚本、用例或专用程序，不改MPQ、原服掉落／概率、存档编码或规则指纹；本批按用户授权提交源码及文档，运行包与冒烟材料不纳入提交。

有限冒烟使用包内EXE及既有管道：bomb／SkillTestSor创建ObjectSmoke，bomb2／NetCombatSor加入，关闭等级差限制（99）后两端ProtocolReady、nativeMapReady=true。SkillTestSor经原传送点进入Cold Plains，NetCombatSor在营地打开传送点菜单；最终采样均unavailableUnits=0、effectLimitations为空。这些采样未操作实际箱子、木桶或陷阱，不能记作物件效果通过。用户接手后正常请求quit，两客户端进程已结束，原服记录保存／角色解锁，一个stderr为空，另一个仅有既有选角肖像sohdbrstnhth.dcc缺组件报告，实际场景采样无不可用单位；本批没有可靠退出码记录。构建、只读MPQ审阅及有限运行材料在忽略目录artifacts/object-audit-20261007；后续手动观察步骤见物件文档。

以下界面／祭坛及更早批为历史交付记录，旧哈希不代表当前包。

此前界面／祭坛修正批（2026-10-07）：用户授权打包、简单冒烟并提交源码。Windows Release首次51步及后续修正增量构建均成功，无编译warning或error；dist/current已更新。最终EXE SHA256为`D1CDCC65190491878E39AFCB498D2F9B1131CE90E4BF80ECFD953F6163EEB82C`，与build/bin一致；协议DLL仍为`E96C38DE1911BF24292EE726BB1CF862E7D1CF0D0DF816EDE565A213667B0CBD`。包括角色面板、HUD字体、交易持物、双击Join／详情、技能提示及树、祭坛图层／模式修正。页签改当前MPQ StrSklTree分片、font16白字及原分行，补Skill Choices Remaining；数字整体右移4原生像素；关闭恢复原圆圈斜杠，按SkillDesc末行空列动态选择预留格，MPQ没有独立关闭像素坐标字段，锚点仍取原UI布局。未新增测试程序、改D2S／规则指纹或复制MPQ，参考仓库、凭据、截图、保存和包不进入源码提交。

使用包内EXE及既有管道／正常UI：UiShrineRetry中bomb／SkillTestSor与bomb2／bbb完成房间详情Ready（经过时间、1–99等级、姓名／职业）及UI同一列表项双击加入，两端ProtocolReady、原地图就绪、对方visible=true；聊天服务器回显及三条入局消息已查看。角色面板白色姓名／数值、千位分隔及常驻灰底已查看。最终哈希包另入UiFinalReady，女巫三页分行、字号、白字、数字小格及两处预留关闭按钮已截图查看，实际点击圆圈斜杠关闭成功；两端再次ProtocolReady，同房两方向ASCII聊天实际收到0x26。原有效技能仍为1级，本轮未为检查双位数而消费技能点；加成／双位数全部状态仍待实机覆盖。最终两端正常quit、退出码均0，原服确认两角色CHARINFO保存及解锁；一个stderr为空，另一个仅有既有选角肖像sohdbrstnhth.dcc缺组件报告，实际场景没有不可用单位。

祭坛：此前包在UiShrineRetry真实AF经验祭坛（class81／code15）看到未领取类型图／闪光及COF阴影，领取后原服state137和50%经验属性真实到达，但本体消失。追查本地ObjMode／SCmd发现0x4D物件操作通知被当技能动作，清除0x0E模式；现仅玩家／怪物生成该施法动作。最终包在UiFinalReady真实class2生命祭坛领取前mode0、领取后mode1／nativeMode=true、actionSkill为空、targetable=false，本体保留且原服生命恢复，scene.unavailableUnits=0。OP到ON和叠层沿当前MPQ及原服时间投影，未修改结算；全部祭坛形态／精确Overlay节拍未认证。

失败样本保留：首包前端字体色查找不存在的Units/pal.pl2导致启动失败，改读当前MPQ实际Sky/pal.pl2后成功。参考服两次watchdog报告D2GS死锁并保存关闭游戏，相关客户端断线截图不计通过；确认没有游戏连接后复用既有Start.ps1恢复参考服，最终有限入局／操作成功。记录及截图在忽略目录artifacts/ui-shrine-smoke-20261007（join-details、joined、character-retry、tree-page1／2-final、tree-ui-final-cold、tree-closed-final、shrine-before、final-shrine-before／after及客户端日志）。此前交易成交证据见下段；本轮未重新验收完整交易、所有HUD悬停、技能加成状态、七职业、八人、跨机器、参考服长期稳定性或Linux。

以下聊天／交易及多人基础批为历史记录，旧哈希不代表当前包。
此前聊天／交易及共用菜单批（2026-10-07）：Windows Release构建成功，公共SceneView头依赖的41个目标重新编译／链接无编译warning或error，随后Ninja无待构建项。dist/current的EXE SHA256为`E22E7F70A5737618EA67914972AE895D4F289795788A237DCD806C3D691CA2C6`，与build/bin一致；协议DLL仍为`E96C38DE1911BF24292EE726BB1CF862E7D1CF0D0DF816EDE565A213667B0CBD`。包内程序、脚本及文档已更新，没有新增测试脚本／用例／专用程序、不复制MPQ、不改D2S编码或规则指纹、未提交Git。

本批使用包内EXE、既有调试管道／正常UI和本机原服，bomb／SkillTestSor与bomb2／bbb在trade1007g、trade1007h入局及互见，两端ProtocolReady／nativeMapReady／玩家visible=true。发起／接收菜单、蓝色悬停、NPC Warriv服务菜单及蓝色Talk已截图查看；NPC Cancel、邀请拒绝／取消和交易取消实际发送原请求。Shift放入hp2药水后对端收到只读page2副本；Shift取回后双方收到14／15、副本消失且对端Cursor为空。Gold原弹窗输入10，两端收到对应0x79；灰色／绿色按钮是对勾。双方点击同意后均收到0x77 action13，原服将药水放入bbb实际背包（新GUID37），钱包由112／0变为102／10。反向交易同药水和10金币，两端再次13，余额恢复112／0，药水回SkillTestSor（新GUID39），再经原Take／Place放回原格(3,2)。不恢复旧GUID或写入角色保存。正常退局重入trade1007h，双方仍互见，SkillTestSor钱包112、药水原格和bbb无该药水再次观察；bbb零金币在重入时原服不发送属性14，不把缺字段认作已知零。

正常Enter输入／发送`trade smoke a`和`trade smoke b`两端均收到服务器0x26，无本地消息回显；M日志实际包含这两条及入局三条原服消息，原字体颜色和共用边框已查看。证据在忽略目录artifacts/trade-ui-20261007（invite-outgoing／incoming-final、对应hover、npc-warriv-final／hover、trade-agreed-final、message-log-final、withdraw／settled／returned／rejoined快照及client-a6／b6日志）。最终实例正常quit，原服记录CHARINFO保存／解除角色锁；stderr一个为空，另一个仅有既有选角肖像sohdbrstnhth.dcc缺组件报告，实际世界双方人物未报告不可用。

失败样本：首个不同等级双账号房间以诊断levelDifference=0创建，原服按相同等级限制拒绝Join（0x2C）；正常产品关闭限制本就发送99，最终诊断同样使用99后加入成功。原版再次登录bomb2曾顶掉冒烟连接，账号空闲后继续。共用菜单首包在编译过程中修改公共头导致部分目标类布局不一致，两端在RemoteScene::draw出现0xc0000005；源码固定后重编全部公共头依赖，最终两局及全部上述操作无再崩溃，失败报告保留在dist/current/artifacts/crashes。返还交易的一次点击遇到报价revision变化被拒绝，等真实回包稳定后重新点击成交，没有自动重发。完整八人、跨机器、异常断线／容量不足、全部装备／镶嵌交易、原版金币弹窗及物品格底色／绿色专用变换、七职业及Linux仍未认证。

以下多人基础批及更早包为历史交付记录，哈希不代表当前包。

当前多人批（2026-10-07）：scripts/build.ps1 Windows Release成功（19步增量），远端物品生命周期补齐后另3步编译／链接成功，均无编译warning／error。dist/current的EXE SHA256为`694AD1CDFD6526A408C59E1D6E657ED9999FCA30D770E680F4EF149DA518443A`，与build/bin一致；协议DLL为`E96C38DE1911BF24292EE726BB1CF862E7D1CF0D0DF816EDE565A213667B0CBD`。包内程序、脚本与文档已更新，不复制MPQ、不改D2S编码或规则指纹、未提交Git。

使用既有调试管道操作正常UI与原服，bomb的SkillTestSor和bomb2的bbb完成：UI创建MultiOctGood、真实列表选中及MCP 0x06详情（普通／1–8人／真实角色和等级）后Join；两端ProtocolReady／nativeMapReady／playerDisplayed和scene.players对方visible=true，走／跑连续显示及最终原服位置、姓名悬停、远端Frozen Armor动作／效果已观察。原服卸装两件初始武器／盾牌后退局重入，无装备bbb仍可见；随后恢复原body4／5装备。最终包另以MultiOctPass验证名称不存在0x2A／错误密码0x29提示、修改后UI入局、双方走跑／施法、装备更新、退出清理全部远端物品、再加入重新互见与恢复装备；不存在房间的详情查询TimedOut后仍可查询现存房间Ready并加入。最终两个客户端正常退出码均0，D2GS记录双方解除角色锁及CHARINFO保存成功。没有新增测试脚本／用例／专用程序，证据入口为忽略目录artifacts/multiplayer-20261007（package-ready、package-walk／run／cast、naked-rejoin、package-peer-left／rejoin、detail-timeout、package-exitcodes和d2gs-final）。

限制：首轮MultiOctFinalm1仅收到握手、参考服没有加载角色回调，客户端有界退局后返回选角并报告保存结果未知；initial-load-failure保留样本。无4000端口已连接客户端时使用既有关闭事件正常重启D2GS，未修改服务器规则／角色保存。最终包一个实例的stderr为空，另一个仅有既有选角肖像组件`sohdbrstnhth.dcc`不可用报告，缺组件肖像继续暂缓、不生成替代资源；实际双方世界人物及本次技能无不可用报告。完整七职业／全部速度状态、受击／死亡及跨幕视野时序、八人、队伍／聊天UI、交易／PvP、跨机器、参考服长期稳定性和Linux尚未认证，不能从本次基础冒烟推断全多人功能完成。

此前2026-10-07小修改包：scripts/build.ps1的Windows Release配置与构建成功，首次Ninja已无待构建项；复选框修正后3步增量编译／链接成功，未见编译warning／error。当时dist/current包含大厅控件调色、背包／stash／方块悬停及复选框；历史EXE SHA256为`C7DD1E3439D1F5A7A09FB324BEDFC63C5CE9F3BFEC72E2DA6073B3A07DFCD1E5`，协议DLL同上。此前大厅调色包E3E4630F及下段71233b2哈希均为历史包身份。

有限冒烟使用包内EXE、既有调试入口及bomb测试账号的SkillTestSor，没有新增测试程序。因包配置未记忆凭据，使用online-login正常认证，非记忆快捷登录复验。正常UI创建SmallOctSmoke，ProtocolReady、nativeMapReady、movementAvailable、playerDisplayed均true，26件物品全decoded；查看背包左右悬停及原服确认打开的stash提示，正常退局返回CharacterSelection，D2GS记录解除角色锁及D2DBS CHARINFO保存成功，退出码0、stderr为空。复选框修正后的最终包已查看建房两种状态及创建角色页共用控件；其首次SmallFinalSmoke入局加载超时，客户端返回选角并明确保存结果未知，参考服日志停在建房／加入请求，未见角色进入回调。确认4000端口无已连接客户端后，仅通过既有停止事件正常重启D2GS，复用其它服务；SmallRetrySmoke成功进入，地图／移动／人物状态齐全、26件物品全decoded，背包／stash提示再次查看，正常退局及CHARINFO回执成功。最终客户端退出码0、stderr为空。日志／快照／原资源核对和截图在忽略目录artifacts/release-smoke-20261007-small。没有买卖、改装备或注入存档；角色无方块，方块悬停未运行覆盖。参考服加载稳定性、全部玩法及Linux仍未认证。

公共UI调色核对：当前MPQ clickbox有两张15×16原帧；本地OpenDiablo2 d2ui/checkbox.go明确使用Fechar，原实现误用Units，现共用checkboxControls读取Fechar，覆盖建房与创建角色。14组当前MPQ DC6按解码后的实际非透明索引比较，invchar6、tradestash、supertransmogrifier、skltree_s_back、800ctrlpnl7、buysellbtn、goldcoinbtn和skillpoints在Act1／Sky／Units下没有像素颜色差异；同一buysellbtn经两个入口加载也没有本次调色差异。numberarrows、radiobutton、realmselectbuttonthin、3widebuttonblank及PopUpLarge分别有56／55／64／91／5个像素随Act1与Units或Sky变化；这些属于需进一步核对原版页面配对的候选，不能仅凭有差异判错或统一改为Act1。该核对不覆盖全部资源、PL2颜色映射、停用／混合效果或全部窗口尺寸，数据见palette-audit.json。

2026-10-07 Windows Release经CMake Tools构建成功，103个增量步骤，项目开启-Wall／-Wextra，编译输出无warning／error，CMake诊断为空。dist/current已更新本批源码、协议DLL、运行脚本和文档；未复制MPQ或提交构建产物。EXE SHA256为`C6AC572EBA70FC9A39A7F01BEFEF095F27CDD6278E360440E7B2B0105FDBB830`，协议DLL为`E96C38DE1911BF24292EE726BB1CF862E7D1CF0D0DF816EDE565A213667B0CBD`。调试脚本补online-item-quote白名单，已实际调用，只读且不排队库存命令。

有限冒烟使用包内EXE、既有命令及本机参考服，没有新增测试程序。忽略目录artifacts/release-smoke-20261007保留日志和选角／大厅／Create／营地截图。记忆登录及SkillTestSor选角成功，截图已查看Change Realm与大厅按钮文字；Create表单默认8人、等级限制未勾选，正常UI仅填房间名创建Smoke104209并入局。ProtocolReady、nativeMapReady、movementAvailable、playerDisplayed均为true，26件物品全部decoded=true，stderr为空。正常online-leave-game返回CharacterSelection，D2GS确认离局、解除角色锁及D2DBS CHARINFO保存成功；随后quit，客户端退出码0。没有买卖、改装备或注入存档，不将CHARINFO回执等同于重新验收全部物品持久化。

首次UI建房因参考服D2GS进程缺失而超时，客户端恢复选角；原日志此前记录watchdog死锁重启。仅按已有WIN8RTM兼容层恢复D2GS，未改服务配置，确认D2CS激活后第二次建房成功。向Charsi靠近仅观察到位置从(5183,6133)到(5168,6128)，未取得交谈，商店报价和结算不计通过。参考服长局稳定性、全部报价逐整数一致性、赌博／全部维修、近距离鼠标交互、音效听验及Linux仍未认证；下节逐项的“未构建／运行／打包”是最初源码核对记录，本批构建与有限验收以上述事实为准。

## 本批源码修正

联机报价接入内容层itemTradePrice／itemGamblePrice共用纯计算，当前MPQ、NPC、私有任务奖励位、难度和原物品属性提供输入；普通／赌博买入、出售、单件／全部维修合计及debug询价共用。补自动词缀、未鉴定、完整孔内属性／符文之语、触发／充能／按时间编码、身体部件／耳朵、自修复、投掷售价和品质9，原1.13c只读指令补充D2MOO整数顺序疑点，证据见库存模块。0x3E充能及属性在唯一归属列表原位更新，成组伤害按原位流不读参数；孔内首次接收顺序沿更新保留，提示／减价与报价使用完整实例。买卖发送前重算并绑定revision，网络锁内核对普通／赌博服务和货架来源；未知数据不授权金额，实际交易及金币仍等原服。全部维修按钮核对原第18帧；独立D2S工具保留且不参与产品。未发送的可变值无法重建，分堆原0x22为空、佣兵身份／库存、任务及多人交易等独立协议能力仍未完成，不能据报价接入宣称物品系统完整。本批未构建、运行、测试或打包，未认证全部物品或逐整数一致性。

无形图标／装备更新修正：原物品flags=0x00400000经已有InventoryItemView进入公共drawItemIcon，图标应用50% alpha，覆盖背包、装备槽、存储、拖拽及复用同一投影的货架。穿戴外观继续保留真实MPQ身体组件，不因无形隐藏人物；RemoteWorld原先先删除旧外观再仅接受少数装备动作，现除显式移除／卸装动作外按位流实际mode=1及身体槽重建，避免仍穿戴的物品更新丢失外观。依据D2MOO D2Constants的物品动作与SCmd的原位置序列化；未认证无形原客户端PL2选择，世界逐组件透明／染色仍未实现，限制见库存模块。本批未构建、运行、测试或打包，用户具体衣服可见性尚待运行验收。

近距离交互源码修正：拾取0x16及地图交互0x13原先无条件记录为向目标中心行走的显示请求，原服可直接交互时仍会预测多走一步。OnlineMovementRequest现区分普通行走与交互靠近；RemoteScene在连续显示位置已合格时清除本帧行走路线，不伪造原服站立模式、位置或成功回执。依据D2MOO PlrMsg::sub_6FC828D0，物品使用D2Common_10399距离≤4并检查原0x0804交互射线；物件复用UNITS_IsObjectInInteractRange对应几何和射线，尺寸来自当前MPQ；本人尸体距离≤8、出口距离≤4。RemoteTown同步补出口靠近资格，NPC仍沿既有≤6及新原服位置保护。远处／有阻挡时保留靠近预测，普通移动不提前停止；库存、对白、旅行及回收仍只消费原服结果。本批仅源码和差异审阅，未构建、测试、运行或打包，具体鼠标场景尚待用户验收。

NPC点击／靠近源码修正：原`interactionReady`只读最近原服坐标，角色已经离开但采样仍在NPC身边时会直接发0x13。D2MOO PlrMsg `sub_6FC828D0`只在距离≤6且玩家非busy时交谈，距离7／8执行靠近，距离≥9不会靠近或回复对白。SceneController现将当前世界显示观察点随交互意图传给RemoteControl；原服距离合格但显示距离不合格，或尚未采样的移动目标指向别处时，改为原0x02／04向真实GUID靠近。这类靠近必须取得请求之后的原服玩家坐标并满足原距离才发送0x13，不能靠原来的近距离采样立即重复判定到达；接收代次只排除未更新样本，不当作原协议ACK。已在有效范围且没有上述陈旧移动的NPC仍可直接交谈；显示坐标只作否决提示，不替代权威坐标或生成对白。0x27→0x2F准备及公共对白UI仍沿原链路。本批未构建、运行或打包，具体NPC超时未复现，尚不能认证截图问题全部排除。

传送点／怪物尸体源码修正（2026-10-07，未构建、运行或打包）：当前MPQ Objects的OperateFn=23条目均为Selectable1=0，但D2MOO `OBJECTS_OperateFunction23_Waypoint`接受mode=1／2开菜单。RemoteTown对这两个模式保留点击资格，普通物件继续核对Selectable及TARGETABLE；解锁历史、菜单和旅行仍等待原服。0x51物件分配记录动作接收时间；公共ActorAnimationCatalog按原FrameCnt1＋1个25Hz服务帧投影启动结束后的mode=2，复用原ON循环动画，并将同一显示模式交给公共光照。仅限Waypoint的表现衔接，不修改副本模式、碰撞或解锁位；缺少必要MPQ定义不推导完成状态。

MonsterMsg的0x69动作9使用当前坐标，RemoteWorld将它写入position／positionRevision并清除旧destination，事件仍保留原坐标与方向；其它动作的目标坐标语义保持原样。RemoteScene在死亡动作尚无新当前位置时停止路径并保留当前连续显示位置，收到尸体当前位置后由原位置校正收敛，不再先回退到旧行走采样。D2S与规则指纹不变；当前包不包含本轮修正，第二幕传送点的鼠标／发光及怪物死亡位置仍待运行验收。

技能／怪物只读资格收拢（2026-10-07，未构建、运行或打包）：人物投影和 RemoteCombat 共用 skill_eligibility 的当前 MPQ 条件导入、Attack／CharStats 固有清单及学习／选择资格。未收到基础等级不按零授权学习，界面补齐原 reqstr／reqdex／reqvit／reqint 门槛；当前法力／装备提示仍使用已有只读输入，下一等级预览不写当前资格。RemoteCombat 集中 MonStats 敌我、原状态 alignment 及 corpseSel／hide／udead 目标校验，RemoteScene 的命中与锁定复用 monsterTargetEligible；声音、弹体、光照与 NPC 读取同一 onlineMonsterCorpse，不再各自判断死亡模式或漏剥离生命 rank 位。场景、owner、范围及地图绑定保留原协议校验，伤害、消耗和学习结果仍由 D2GS 决定。

掉落显示迁移补齐（2026-10-07，未构建、运行或打包）：对照旧单机loot_view／SceneAssets／ItemChange消费者及独立D2S映射，发现原品质数字被直接转换为内部枚举，导致普通物品误标稀有、魔法误标暗金等颜色／名称／图形错误。原编号表提取到共用quality.hpp，地面、面板和商店消费同一转换结果，独立D2S映射和编码不变。已鉴定暗金／套装沿用原invfile／flippyfile覆盖；未鉴定原位流缺少fileIndex，保留基础原图，不假定第0行。DROPTOGROUND或新DROPPING记录表现事件版本／接收时间，公共SceneView恢复旧25Hz flippy与同帧命中／高亮；mode=5纳入地面显示但仍禁拾取，普通同步／属性增量不重播，切区／退局／离地清理。ALT标签、品质色、带孔／无形灰色、符文之语标题、避让布局、底边锚点继续复用既有代码。普通拾取提示等待同一地面GUID进入本人容器／Cursor，原掉落／消耗／拾取结果仍由D2GS裁决。

所有物品的掉落声音与数量反馈均走公共表现入口（2026-10-07，未构建、运行或打包）：对照 Diablerie SoundSystem.OnLootFlipped／Item.dropSoundDelay 和当前 MPQ，起始 item_flippy 与物品指定帧落地声分别消费一次；SoundCatalog 读取 Weapons／Armor／Misc 及已知暗金／套装覆盖，SceneAudio 使用已有 Sounds 声组规则，普通快照／属性更新不重播。D2MOO ItemMode 的 PickupGold、sub_6FC437F0、sub_6FC43BF0、sub_6FC49AE0 分别核对金币堆移除／钱包增加、自动合堆及书／卷轴转移。RemoteUiClients 用请求前 GUID／revision、已知钱包及堆叠数量基线核对原服结果，含普通拾取、金币、自动／手动合堆、卷轴入书和部分合入；数量分包未对齐、单独移除或 Updated 均不授权成功。通知有界排队，跟踪不写库存或请求结果；缺基线、过期、中断或多人分金仍保留未知。音效与数量反馈的其余边界见[物品支持](../gameplay/items/SUPPORT.md)，当前包不含本轮修正。

世界角色装备显示修正（2026-10-07，未构建、运行或打包）：只读 bomb 的 SkillTestSor 99级女巫原服保存，当前躯干为 qui 绗缝甲，原品质4（魔法）、flags=0x00c00010（含无形0x00400000）。原 RealmPortraitCatalog 世界装备入口对品质>3或无形直接返回空，RemoteScene 因而放弃整个人物。对照 D2MOO D2Inventory 的 INVENTORY_GetCompositItem、旧 hero_assets 身体组件组装与当前 MPQ：qui 的四个主体组件为 ArmType 0、肩部为1，女巫对应原 DCC及城镇 COF存在。现在真实装备组件继续进入已有公共合成器，未知的组件染色／无形透明只作诊断，不再因品质或效果标志隐去人物；真实组件／动画缺失仍保持明确不可用，不替换裸装。选角肖像染色限制未改变，不改装备、存档、原服状态或 D2S 编码；该具体角色尚未运行复现或验收，当前包不含修正。

## 最终依赖清理

2026-10-07：删除已被替代的 Local 入口及 GameSession、Simulation、SkillRuntime、InventoryService、本地任务／AI／战斗／奖励执行源码，移除 d2x_session 目标。共用 gameplay／items 只保留联机显示、几何、原资源报告和独立存档工具的纯函数及值类型；混放的元素伤害显示、技能等级和库存错误文案独立提取，内容加载移除依赖库存服务的装备计算入口。人物／任务投影位于 d2x_client；presentation 链接 client／content／world／remote_scene，客户端不链接 persistence。

删除无原服生产者的旧法杖插入面板、状态和资源加载，缺失原服插杖流程仍明确未实现；删除旧本地调试输入、旧 CLI 别名、旧地图／动画／音效重复缓存及单机切换分支。LocalCast 名称表示本人施法显示衔接，保留原服预测／校正用途，不是本地结算。测试 pause/resume、在线 command 和独立资源／存档工具保留。

CharacterSaveData 定义移到 persistence/character_save.hpp，字段与 D2S v96 编码不变，不更改格式、语义或规则指纹。原 MPQ、reference、旧压缩包、mvp 和用户文件保留；参考源码／原表／资源不纳入提交。完整依赖图见[架构](../architecture/OVERVIEW.md)。

Windows Release 构建及 d2x／d2x_assets 链接成功；构建图无 d2x_session，两个可执行文件均未发现 GameSession／Simulation／SkillRuntime／InventoryService 符号。2026-10-07 已更新 dist/current，实际包内 EXE SHA256 为 `A7B767F8AFE9E0195F64EFC132BB0A3B8604CDA0CE86985A3B879520AE58B3E2`，协议 DLL 为 `E96C38DE1911BF24292EE726BB1CF862E7D1CF0D0DF816EDE565A213667B0CBD`。证据在忽略的 artifacts/trash/dependency-cleanup-20261007，使用既有程序／调试管道／本机参考服，不编写测试脚本、用例或专用程序。

有限冒烟只操作新建独立账号 Clr010757／一级女巫 CleanOctSor，未读写用户测试角色：

| 观察 | 本轮证据与实际边界 |
| --- | --- |
| 入局与世界 | 注册、建角、非 Ladder 单人建局成功；营地本人可见，Warriv 原图可见，鼠标点击收到原服对白；城镇大地图、人物／技能树／任务／库存与 ESC 菜单截图已查看。初始营地 renderedUnits=9、unavailableUnits=2，不能认证全部城镇单位／完整自动地图选项；野外该采样 unavailableUnits=0 |
| 原服移动 | 共用鼠标输入从 `(4393,4548)` 移动到 `(4400,4548)`，原服确认；经出口分段行走到 Blood Moor `(4528,4565)`，地图／人物正常显示。不是完整寻路验收 |
| 施法与交接 | 重新选择 Fire Bolt 后原服 owner 确认为 0xFFFFFFFF；command 和鼠标右键均耗蓝35→32，施法与原弹体截图已查看。同次鼠标移动→施法显示位置 `(4538.186,4557.405)` 保留，650ms 后未回旧位置，原服位置为 `(4538,4557)`；未覆盖延迟／丢包、全部技能或怪物击杀 |
| 普通菜单／测试暂停 | ESC 菜单时 presentationPaused=false，原服收包8297→8761字节；显式 pause 期间收包8761→9564，resume 成功。原服未暂停 |
| 原服保存重入 | 通过 ESC 原菜单 Save and Exit 返回角色页；再次建局成功，一级／40血／35蓝／7件物品和已确认 Fire Bolt 选择保留。仅同进程重入，未追加新进程认证 |
| 独立存档工具 | 首次保存前原服建角文件的无效头／校验被拒绝；原服保存后的 save-info 正常解码 D2S v96。读前／读后 SHA256 均为 `3AAD8DF3FAB4BD3F499276F922AF7BCF86F31FB36751F78D34D2DA637DA74D28`，文件未修改；CharacterSaveData 定义与搬迁前相同 |
| 收尾 | 正常退局返回 CharacterSelection，客户端退出码0、stderr为空；本轮启动的参考服已通过已有 Stop.ps1 正常停止。没有改原资源、reference、旧包、mvp 或用户文件 |

本轮观察到两项限制：`(4469,4579) → (4506,4580)` 的长距离绕墙请求停在 `(4478,4576)`，随后报原服位置无进展超时；从门口 `(4476,4565)` 分段通过桥梁可出城，前一条路径不计作通过。新角色原服初始 Fire Bolt 选择 owner=0，普通施法资格暂拒绝；重新选择后收到 owner=0xFFFFFFFF 可正常施放，重入保留该确认。没有为这些情况恢复本地执行器或猜测来源。声音设备与资源路径随普通窗口运行，无错误限制采样；未人工听音，不认证完整音效／时序、多人、Linux、任务领奖或升级。

## 既有有限证据

下表仅保留对后续开发仍有用的历史观察；这些不是最新未入包源码的复验。旧离线技能／任务冒烟不再列作联机完成度，原始材料仍保留于忽略目录及归档。

| 记录 | 实际观察 | 边界 |
| --- | --- | --- |
| 2026-10-06 online-pve | 一级女巫火弹耗蓝；command击杀僵尸、鼠标右键击杀沉沦魔；两类怪物反击扣血／死亡；ESC满血回城；死亡直接退局新进程存活HUD1／40血、35／35蓝，鼠标取回法杖与护身符，Akara恢复及原服保存 | 声音未人工听验、闪电序列未实战；离开期间服务端死亡精确时序、专家／多尸体未覆盖 |
| 2026-10-06 online-progression | 连续长按／释放、慢走超过16秒、营地／野外切区；原NPC提示／多段对白；火弹击杀、经验84→117→210→276→348及金币0→1；法力神坛、死亡回城和保存重入 | 实际升级、点数分配、任务完成动画／领奖及完整五幕任务未认证 |
| 2026-10-06 online-combat／online-play-smoke | 独立19级女巫属性加3、Fire Bolt学习、Frozen Armor状态、Teleport原位置、原服掉落／药剂／回城门及新进程保存重入 | 部分准备物品／等级来自独立临时档；不认证自然取得；地狱火停止与复杂技能未认证 |
| 2026-10-06 online-items／online-solo-followup | 原物品位流、部分Cursor／腰带／装备／箱子／金币；3宝石合成、普通买卖／维修／鉴定；UI卷轴鉴定、快捷键原0x7B重入、邪恶洞穴STARTED及原说明 | 部分材料由独立临时档准备；27类接口不等于全部组合；完整服务资格、价格、自然掉落及任务奖励未认证 |
| 2026-10-06 online-map-navigation | 真实五幕传送点、奥术／皇宫地下层门户与传送台往返，同局探索返回保留 | 最新传送点点击／循环动画没有复验；完整鼠标／特殊资格与多人旅行未覆盖 |
| 地形开发对照 | 第一幕122组、第二至第五幕364组原1.13c事件序列／最终DT1／完整碰撞对照 | 方法、样本与排除项统一见[原版对照](../architecture/MULTIPLAYER.md#原版-dll-对照方法)；不认证全种子／全激活时序或玩法 |
| 早期双账号营地 | 双账号互见、移动与正常退局 | 不是当前完整多人UI、战斗／队伍／交易认证 |

参考服曾出现握手后不加载、watchdog可能死锁及保存后断连，确认无人在线后重启可恢复；失败样本不计通过。Ladder仍server down，严格版本校验、跨机器和长期稳定性未认证。本机组件、端口及启停只维护在[部署入口](../architecture/MULTIPLAYER.md#本机部署与验证入口)。
