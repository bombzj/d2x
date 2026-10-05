# 联网入口与底层基线

更新：2026-10-06。局前主流程、服务器角色加载与保存退局、五幕共用纯C++地图、移动、地图对象交互、传送点旅行及自动地图已接入。五幕1–136共用离线生成核心，第一幕此前122组、第二至第五幕364组原版DLL地形对照通过；五幕传送点往返及奥术圣殿／皇宫地下层门户往返已实际连服验证。战斗、拾取、完整库存尚未接入，地图通过不等于完整联网游玩。

RemoteTown逐条重放原服有序房间／玩家位置事件，精确检查房间锚点，输出实际DT1与活动碰撞。五幕事件丢失或生成失败会停止显示／移动并给出 `nativeMapReason`，要求重新入局；不回退近似预设。用户明确拒绝原DLL地图运行依赖；开发对照方法见[实施计划](../architecture/MULTIPLAYER.md#原版-dll-对照方法)。

## 流程与入口

普通启动：主菜单 → Battle.net → 登录或注册 → Realm → 服务器角色 → 创建或加入游戏 → D2GS 协议加载 → 重建当前幕活动地图 → 服务端世界显示。Single Player 保留离线入口。联网不读取本地 D2S，也不启动本地 GameSession。

| 页面／操作 | 当前源码 |
| --- | --- |
| 登录／注册 | 原 MPQ 图形、字体和文案；校验两次密码一致，SID 0x3D 成功后自动 SID 0x3A 登录；拒绝／超时后允许重新输入 |
| Realm | 首次自动选择配置项；未找到则显示服务器列表；Change Realm 关闭 MCP、重新取列表，选择后取新票据 |
| 角色 | 服务器列表分页；七职业创建、资料片固定开启、非 Ladder；Hardcore 先显示原警告；删除先确认，成功刷新；死亡专家与未知状态角色不能进入 |
| Create | 名称／密码／说明、人数 1–8、等级差 0–99；普通／噩梦／地狱依据服务器 native 进度解锁；成功自动取票加入 |
| Join | 真实房间列表、人数、选中说明；滚轮浏览、按名与密码加入；再次点 Join 刷新，列表等待可取消 |
| 等待／返回 | 持续 tick，建局队列显示位置；取消列表返回大厅，其他等待取消关闭会话；大厅 Quit、正常退局重新取票返回选角 |

`ProtocolReady`仅表示协议初始化，场景另须 `nativeMapReady`、`playerDisplayed` 与活动碰撞准备成功。左键提交移动、R切换跑／走、Esc打开退出菜单；楼梯底层先通过 `online-use-exit` 调用，鼠标入口留待UI阶段。新闻、广告、频道／聊天、账号设置、Ladder、转换角色和影片等非主流程入口暂缓。

角色名 2–15 字符，首字符英文字母，其余英文字母、连字符或下划线；classId 为 0 Amazon、1 Sorceress、2 Necromancer、3 Paladin、4 Barbarian、5 Druid、6 Assassin。初始属性、装备和 D2S 由原服生成；客户端仅提交 MCP 0x02 的职业／状态与名字。删角用 MCP 0x0A，command 要求 confirmName 完全匹配。注册账号／密码为 2–15 可打印 ASCII 字符，更细名字限制由服务器拒绝码说明。

肖像仅绘制已保存 native 1.13c、无组件染色且原 COF／DCC 完整的外观；legacy 新角、染色或未核实组合保留真实身份，肖像暂不绘制。建角使用当前 MPQ 七职业前端动画。

## 配置与职责

复制 [配置模板](../development/online.example.json) 到 online.local.json，或使用 `--online-config <路径>`。originalClientDirectory 相对配置文件解析，含同一 1.13c 的 Game.exe、Bnclient.dll、D2Client.dll，只读计算版本和 CheckRevision，不执行原 DLL。默认 authentication=pvpgn，SID_AUTH_CHECK 零 key，本机服无需 CD-key；严格服可显式设 keys。密码不持久化，打包只复制 PvPGN 连接设置，私有配置不提交。

本机账号入口 127.0.0.1:6112，Realm D2X-Local，游戏端口 4000；Realm 地址由 SID 返回。原版目录 D:/Downloads/Diablo II V1.13C。测试账号 **bomb / 1qaz2wsx**、**bomb2 / 1qaz2wsx** 经用户授权仅用于此测试服。参数见 [command](../development/DEBUG_PIPE.md#联网命令)，服务启动见 [D2GS 计划](../architecture/MULTIPLAYER.md#本机部署与验证入口)。

| 代码 | 职责 |
| --- | --- |
| [contracts/online.hpp](../../src/contracts/online.hpp)、[online_world.hpp](../../src/contracts/online_world.hpp) | 只读会话与服务端单位／房间／位置／装备外观前缀；未知字段 optional |
| [app/frontend.cpp](../../src/app/frontend.cpp) | 会话所有权、配置、tick、页面路由与命令提交 |
| [RealmFrontend](../../src/presentation/frontend/realm_frontend.hpp)、[RemoteScene](../../src/presentation/remote/remote_scene.hpp) | 原图、字体、局前／世界显示与输入，只读副本并返回意图 |
| [remote_world.cpp](../../src/client/remote_world.cpp) | 有序回包归并，无 MPQ、GPU、存档或本地模拟 |
| [RemoteTown](../../src/client/remote_town.hpp) | 五幕有序房间事件重放、锚点校验及活动地形快照，复用 DS1／DT1，不生成本地单位 |
| [native_act_layout.hpp](../../src/world/outdoor/native_act_layout.hpp)、[native_map.hpp](../../src/world/native_map.hpp) | 共用五幕布局及房间生成会话；GS 有序重放和活动地形快照已接源码，完整规则及跨区未验收 |
| [online_commands.cpp](../../src/app/debug/online_commands.cpp) | 同一会话的菜单管道；accepted 与异步成功分开 |
| [RealmPortraitCatalog](../../src/content/character/realm_portrait.hpp) | MPQ 原表动态重建外观编号 |
| [RealmSession](../../src/network/realm_session.hpp) | SID／MCP／D2GS 状态机、认证、角色／房间、取票、加载、心跳与退局 |
| [TcpStream](../../src/network/tcp_stream.hpp) | Asio DNS／TCP、期限、有限队列、拥有线程 poll；关闭后旧回调不交付 |
| [protocol/](../../src/network/protocol/wire.hpp) | 边界检查、framing、认证、D2GS 压缩与拆包 |
| [Networking.cmake](../../cmake/Networking.cmake) | 固定 Asio 1.30.2／BNCSutil 快照，独立网络／协议／远端客户端目标；[来源／许可](../resources/THIRD_PARTY.md) |

网络公开头仅有项目值类型和标准库；Asio／WinSock／BNCSutil 留在实现，网络不依赖玩法、世界、存档、MPQ 或 GPU，保留 C++20／CMake Windows／Linux 路径。

## 会话与协议边界

所有方法在拥有线程调用并持续 tick。Idle／Failed／Cancelled 登录或注册；RealmSelection 选 Realm；CharacterSelection 创建／删除／选择角色或切换 Realm；Lobby 列表／建局／加入。列表取消保留 MCP，迟到回复不污染后续请求；其他等待取消关闭连接，不自动重发角色消费操作。UI／command 不把 accepted 当作服务器成功。

read() 借用有效至下次修改；connectionGeneration 管登录生命周期，gameGeneration 管游戏连接，消费者按当前代次丢弃旧包。RealmSession 已将支持的回包归并至 read().world；take_game_packets() 保留有界原包供后续消费者，应用仍持续排空。world.areaGeneration 在 LOADACT／UNLOADACT 时递增，仅清理区域实体／房间，保留本人身份、装备和属性；1.13c 首次入局在 LOADACT 前已发送这些角色数据，不能一并清空。跨幕撤销旧位置，首个 LOADACT 可保留已收到的出生位置。断线／取消／退局／新局清空全部副本，显示绑定按代次失效。退局、返回选角和 Realm 切换重新取票；保存成功须由重入结果或服务日志确认。

正常关闭窗口或 command `quit` 时，已进入游戏的会话先提交 0x69，持续 tick 至退局响应／关闭或阶段期限，再注销并结束进程；不把 quit 的 accepted 当作保存回执。online-cancel／online-logout 仍属于显式关闭连接。客户端不写 Realm D2S，服务器负责保存。

- profile 为本机 PvPGN／D2CS／D2GS＋LoD 1.13c，旧式 SID 登录与 IX86ver0..7.mpq。CheckRevision 公式先检查再交解析器；原文件路径暂要求 ASCII。NLS／Warden／扩展反作弊未实现，本机 Warden 禁用，严格版本校验未验证。
- SID／MCP 独立累积半包／粘包；D2GS 压缩模式 0／1、长度头与 Huffman 解压，未知长度明确失败。长度表核对本机 D2Net.dll 0xA900、D2MOO／OpenD2；0x7A 为 13 字节，不沿 JS 示例的 3 字节。
- 加载 0x02 后发 0x6B；0x03 保留幕／种子／townArea／secondarySeed，0x04 完成、0x0B 本人单位绑定。townArea 不代表玩家当前区域，secondarySeed 尚未驱动 DRLG。
- 累积各限 256 KiB，单次解压 64 KiB，世界队列 4096 包／2 MiB；默认阶段期限 15 秒、心跳 5 秒。建局 MCP 0x14 记录队列位置并刷新期限；取消关闭会话，不宣称撤销已完成的服务端操作。
- 参考服空列表不发终止包时，超时返回 Lobby、清除未完成列表并保留错误，gameListComplete=false，不能把无响应当作已确认空列表。列表按角色 Hardcore 请求，Ladder 房间不展示。
- 33 字节预览接受 version 4／10／13；native 1.13c 的 14-bit client flags 给出职业／等级／状态／进度，byte 30 是公会徽章颜色。外观保留类别核对 D2Common.dll 0x9D888，具体 item code 动态读 MPQ。
- 密码、key、哈希和票据不进入视图／日志；各层清理自身副本，调用方负责输入副本。离线存档语义未改。

证据为本地 PvPGN common/bnet_protocol.h、common/d2cs_protocol.h、bnetd/handle_bnet.cpp、d2cs/handle_d2cs.cpp／handle_bnetd.cpp；布局及动画参考本地 reference 和既有离线选择器，图形文案读取当前 MPQ；参考仓库不提交。

## 远端营地入局边界

该标题保留供既有链接使用；五幕1–136现已接入共同生成入口。

- 联网仅创建只读服务器单位／房间副本及地形会话，不创建本地 Region、人口、AI、任务或角色存档。属性、装备和持久保存仍由原服决定。
- 五幕种子、难度和当前MPQ驱动共用 `NativeMapGenerator`；0x07／0x08及玩家换房顺序驱动活动网格，不按最终房间集补猜。历史最多4096条，不连续或生成失败需重新入局。五幕使用同一严格路径，旧唯一预设匹配与碰撞变体回退已删除。
- 地形快照保留当前连续组件的活动房间、选定DT1、完整碰撞及原出口链；允许营地和野外连续跨区。场景位置为服务端全局subtile，快照原点可随活动房间变化，不是固定城镇偏移。
- UI／`online-move`复用当前地图、代次和目标可走检查，向原服发0x01 walk／0x03 run，最多每100ms一次。accepted仅表示请求入队，实际位置由后续回包确认；不把移动命令当传送。
- `online-use-exit`及`online-interact`要求原生地图可用、角色存活及当前场景真实type5出口或type2地图对象ID，发送原0x13交互；Objects.OperateFn分类门、门户、传送台、传送点和对象楼梯，接触／资格及最终区域由原服校验，不能传LvlWarp类型编号冒充单位ID。
- 服务端对象模式按当前MPQ的SizeX/Y、HasCollision、BlocksLight、IsDoor及碰撞标志刷新对象阻挡层；原地形碰撞保持。非循环物件动画从已确认模式变化起播放，不提前开门或清墙。
- 原0x63读取对象GUID和8个WORD历史、校验0x102头，Levels.Waypoint映射目的地，未解锁点拒绝。原0x49提交旅行或level=0关闭；LOADACT／房间及玩家回包确认地图／落点。原DC6／TBL／PL2面板与单机共用显示函数，不依赖本地任务或存档。残留移动回包不误关菜单；新移动／交互先发关闭请求，避免服务端保留忙碌状态。
- 自动地图复用AutomapCatalog和当前MPQ图块，记实际可见DT1实例；关闭地图仍积累。按局代次／种子／幕／区域／全局格记忆，换区／换幕返回保留，新局清空；连续步行邻区同层、楼梯／门户隔层。联网不写离线.d2xmap或服务器D2S。Tab开关、V切小图左右、方向键平移、Home居中；尺寸另有online-automap命令，完整选项／名称／队伍显示未接入。
- RemoteScene只读权威位置／模式／装备，显示原图；缺图单位明确计数，未接入攻击／技能／物品回包消费者。地形已选变体、Pops与亮面Warp显示不参与本地伤害或碰撞模拟。

包长度核对本机1.13c D2Net，协议语义交叉参考本地D2MOO；完整客户端动作、颜色、自动地图及逐帧视觉仍有边界。原800ctrlpnl7.dc6有七帧，场景与既有HUD仅绘制前六块。

## 地图交互与探索冒烟

本批Windows Release实际包通过既有command连本机原服；独立临时角色`bomb2/MapNavSor`，证据在`artifacts/online-map-navigation-20261006/`。仅该角色的原D2S用于夹具：普通难度39个传送点位／出生幕及校验和，不改aaa／bbb／ActMapSor，不把夹具解锁当作任务流程通过。传送台复验另用既有单机命令生成99级／分配体力／原补给神殿恢复的独立D2S，匹配本轮临时角色名称并重算校验和，未解锁任务。检查后正常退局并通过MCP删除MapNavSor，原角色保留。

- 0x63实际历史头258、初始只有第零点，39条目的地按原表读取。未解锁目的地、关闭后的旅行、未分配对象和旧局代次请求均拒绝，原传送点面板截图已查看。
- 最终包以种子1685319291通过真实0x49连续完成1→40→75→103→109→1→74，全部nativeMapReady／playerDisplayed成立，返回保留历史和探索。此前种子1773422220另覆盖48下水道二层、113水晶通道、3冰冷之原及107火焰之河；后者使用生成器实际调整后的原点。
- 地下墓穴二层门199实际模式0→2；奥术圣殿74的原门户激活后，经0x13往返皇宫地下三层54，地图、区域与落点回包正常。
- 奥术圣殿传送台实际往返：种子1538180684，服务端台23交互后25501,5448→25516,5447，另一台返回25503,5447，区域74和原生重建保持正常。首轮低等级角色在接近传送台途中死亡，客户端禁止后续移动；该轮未计为传送台通过。离线独立99级D2S同进程保存／新进程加载正常，未用它认证正式联网成长或战斗。
- 鲁高因完整原城镇图跳过红叉诊断帧，单机／联机共用修正。表交叉核对本地libd2与原1.13c D2Client文件偏移0xD2DB8，实际大小图／面板截图保留。
- 参考服第三次建局曾停在LoadingGame，地图包之前没有角色取档；另一次watchdog明确关闭游戏并退出。失败快照／服务日志保留，重启仅本任务D2GS后最终往返通过。长时间稳定性仍未解决，服务器终止或角色死亡中断不计为通过。

地形生成核心本批未修改，原122／364组对照证据继续适用，未冒充重新穷举。完整彩光、原客户端逐像素自动地图锚点／色表、全部任务门户资格、所有种子／激活时序及Linux仍未认证。

## 第一幕地图与出口冒烟

2026-10-06使用实际 `dist/current/d2x.exe`、既有command及本机参考服，独立非Ladder测试法师 `bomb2/ActMapSor`；未改动 aaa／bbb。证据集中在忽略目录 `artifacts/map-oracle-20261005/`。

- Windows Release构建／打包通过，包内EXE及网络DLL与构建产物SHA256相同；正式地图不依赖原D2Common DLL。
- 第一幕原版1.13c对照122组全部通过；共享离线路径五种子各136区域加载／出口关联通过，包内第一幕39区显示、噩梦／地狱地下墓穴代表启动及临时D2S同进程／新进程重载通过，详见[地图验证](../gameplay/world/MAPS.md#本批验证)。
- 连续参考服检查以真实移动完成营地1→血腥荒地2，真实type5单位交互进入邪恶洞穴8并返回2；最终种子665010272，原生地图、人物显示和活动碰撞保持，无mapErrors。补齐原0x09出口分配回包，0x13使用真实服务器GUID。出口附近仍按服务器寻路／碰撞接近，command不是传送。
- 正常退局返回CharacterSelection；原服00:25:15日志确认独立角色CHARSAVE／CHARINFO保存成功。此项不认证联网战斗、拾取或任务变化保存。
- 先前长时间停留时参考D2GS watchdog关闭游戏；最终重新连接后连续完成往返及正常退局。服务长期稳定性问题仍未解决，断开时需检查服务日志；不将被服务端终止的检查记作通过。

## 其余幕地图冒烟

2026-10-06使用本批实际 `dist/current/d2x.exe`、现有command及本机参考服。第二至第五幕364组原版开发对照、五种子各136区加载／出口关联、包内97区显示及第五幕独立D2S重载通过，详细范围见[地图验证](../gameplay/world/MAPS.md#本批验证)。证据集中于 `artifacts/maps-acts2-5-20261006/`。

- 仅本轮临时角色 `bomb2/MapActsSor` 调整原D2S出生幕字节及校验和，逐幕建普通、非Ladder游戏。第一幕营地回归、40／75／103／109实际原生地图及人物显示通过，全部 `nativeMapReady/layoutMatched/playerDisplayed=true`、无mapErrors；第二至第五幕均收到真实移动后的位置变化。
- 地图种子依次为2031870738、536379678、1078075686、235020106、851939196；截图及入局／移动快照保留。各次正常退局回到服务器选角，原服日志确认CHARINFO保存回复。本轮未以持久任务／物品变化认证CHARSAVE差异。
- 检查后通过MCP删除本轮临时角色，原aaa／bbb／ActMapSor保留。出生幕夹具不表示原版任务门槛或正式跨幕旅行已通过；本批未实际连服走完第二至第五幕野外／室内全部区域，地形等价由独立原版事件对照认证。
- 参考服第三次建局时两次在地图包之前握手超时；重启D2GS后第三／第五幕分别检查成功。保留失败日志，服务长期稳定性仍有问题，不将失败轮次记为通过。第三至第五幕更多NPC／怪物外观仍计入unavailableUnits，缺图不替换中立单位。

## 营地入局与存档冒烟

2026-10-05，使用实际 EXE、既有 command 与参考服，未新增测试脚本或专用测试程序。证据在忽略目录 `artifacts/online-world-smoke-20261005/`：

- Release 构建通过；修复真实回包顺序导致 LOADACT 清掉已加载人物／装备／状态的问题，以及面板七帧误判资源缺失的问题。临时包编号诊断已移除。
- `bomb/aaa` 加载一级法师，生命40／法力35／体力74、装备 sst；`bomb2/bbb` 加载一级圣骑士及 ssd／buc。原东向营地以17个固定物件及服务器房间唯一匹配，最终包另通过24个固定物件匹配原南向营地；碰撞检查、本人图形、双人显示和持续心跳通过。
- 跑／走请求后，两端收到同一玩家的新位置；目标附近实际落点由服务器决定，不能要求命令坐标必然原样达到。截图查看了原营地、两个人物和原控制面板。
- 正常退局返回服务器选角，同进程及新进程再次登录／加入后，生命／法力、原属性、装备和本人图形恢复；单位 ID／gameGeneration 更新，出生位置回到营地，不保存上局站位。正常 quit 的服务端日志确认离开与 CHARINFO 保存。
- 最终包经正式 MCP 新建临时非 Ladder 法师 `NetEntrySor`，正常入局／退局后 D2DBS 确认 CHARSAVE／CHARINFO 写入；保存文件为原 D2S v96，含 sst 和原角色属性，重入重新从数据库加载。仅清理本轮创建的临时角色，保留 aaa／bbb。
- 营地移动没有改变角色的持久字段。本轮没有用联网战斗／物品变化认证 CHARSAVE 差异保存；原服会对未变化角色去重，参考 PlrSave 的数据库保存比较，不能将没有新 CHARSAVE 写入等同于保存失败。
- 离线独立临时原生 D2S 的保存、同进程 load、新进程 --load 和移动通过：经验500／等级2、金币123、已分配体力1点、Fire Bolt 等级1均保持。最终包使用正式 Fire Bolt 击杀原 fallen1，生命由3降至0、经验500→518，保存再加载仍为518。未写入用户原单机存档；该项属于离线回归，不认证联网技能或物品流程。
- 参考服曾再次出现取票后握手超时及启动 watchdog 退出，重启参考服后入局恢复；同房间连续退局重进成功。长期稳定性、人工鼠标完整流程、其他地图、复杂外观及完整玩法仍待验收。

## 本机有限冒烟

此前 2026-10-05 使用实际 `dist/current/d2x.exe` 与既有 command 管道，没有新增测试脚本或测试程序：

- `bomb` 返回已保存的一级法师 `aaa`；`bomb2` 返回一级圣骑士 `bbb`。无 key SID_AUTH_CHECK 均成功，bnetd 日志确认真实计算结果匹配 `D2XP_113C`；错误密码返回 SID 0x3A／code 2，随后正确密码可重试。
- `aaa` 建普通难度非 Ladder 房间，`bbb` 从真实游戏列表取得该房间并加入。两边达到 ProtocolReady，mapSeed／secondarySeed 相同，本人单位 ID 分别为 1／2；持续心跳后没有协议或解压错误。
- 两边正常退局，重新取得 Realm 票据并返回 CharacterSelection；D2GS／D2DBS 日志确认两人的 CHARSAVE／CHARINFO 保存成功。`aaa` 再建一局成功；最终包再次用 `bbb` 完成建局与初始化。这里只核实服务器保存回执与读取原角色，没有验证移动、经验或装备变更的保存。
- 单机独立新建法师的直入入口、status 和正常退出冒烟通过，没有写入原角色存档；stderr 仅有已知 Trees.ds1 兼容诊断。
- 主菜单、登录、已保存角色页、大厅及协议交接页已截图查看；修正选角按钮换行与大厅控件调色板／原有文字重叠。人工鼠标／键盘完整操作、新闻和 Banner、完整颜色映射及逐像素一致性仍待验收。
- 当前参考 D2CS 在没有可列房间时不会发送列表结束包（`on_client_gamelistreq` 只在 count 非零时发终止记录）；客户端 15 秒超时返回 Lobby 并保留错误，不能把无响应当成已确认的空列表。非空列表已验证。
- 参考 D2GS 曾被 watchdog 退出，另一次出现建局／取票成功但游戏握手超时；重新启动 D2GS 后最终包入局成功。服务端长期稳定性未解决，不据此宣称联机已可长期运行。遇到该现象先检查 `artifacts/d2gs-local/d2gs/d2gs.log`；确认 D2GS 已激活后重新登录，不选择 Ladder。

上述验证状态快照、构建日志与截图在忽略目录 `artifacts/online-smoke-20261005/`；服务端日志在 `artifacts/d2gs-local/pvpgn/var/` 和 `artifacts/d2gs-local/d2gs/`。最终网络 DLL 保持独立可替换，MinGW 运行库静态链接，包不依赖开发环境中的 libwinpthread DLL；原版 EXE／DLL／MPQ 不复制进包。
