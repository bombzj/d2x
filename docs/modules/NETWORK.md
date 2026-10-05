# 联网入口与底层基线

更新：2026-10-05。当前源码与 `dist/current` 已包含局前主流程、服务端世界副本和第一幕营地入局链，Windows Release 构建及有限实机冒烟通过。已确认服务器角色加载、双人显示／移动、正常退局及同进程／新进程重入；出城、战斗、拾取和完整库存尚未接入，不能据此认证完整联网游玩。

## 流程与入口

普通启动：主菜单 → Battle.net → 登录或注册 → Realm → 服务器角色 → 创建或加入游戏 → D2GS 协议加载 → 条件匹配原版营地 → 服务端世界显示。Single Player 保留离线入口。联网不读取本地 D2S，也不启动本地 GameSession。

| 页面／操作 | 当前源码 |
| --- | --- |
| 登录／注册 | 原 MPQ 图形、字体和文案；校验两次密码一致，SID 0x3D 成功后自动 SID 0x3A 登录；拒绝／超时后允许重新输入 |
| Realm | 首次自动选择配置项；未找到则显示服务器列表；Change Realm 关闭 MCP、重新取列表，选择后取新票据 |
| 角色 | 服务器列表分页；七职业创建、资料片固定开启、非 Ladder；Hardcore 先显示原警告；删除先确认，成功刷新；死亡专家与未知状态角色不能进入 |
| Create | 名称／密码／说明、人数 1–8、等级差 0–99；普通／噩梦／地狱依据服务器 native 进度解锁；成功自动取票加入 |
| Join | 真实房间列表、人数、选中说明；滚轮浏览、按名与密码加入；再次点 Join 刷新，列表等待可取消 |
| 等待／返回 | 持续 tick，建局队列显示位置；取消列表返回大厅，其他等待取消关闭会话；大厅 Quit、正常退局重新取票返回选角 |

`ProtocolReady` 仍仅表示协议初始化。达到它后，第一幕营地只有在服务器位置／房间／固定物件与当前 MPQ 预设唯一匹配、DT1 碰撞变体一致时才切入远端场景；否则保留说明及退出入口。进入场景后左键提交移动、R 切换请求跑／走，Esc 打开原图退出／返回菜单。战斗与物品操作尚未接入。新闻、广告、频道／聊天、账号设置、Ladder、转换角色和影片等非主流程入口暂缓。完整人工操作、调色与逐像素一致性待验收。

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
| [RemoteTown](../../src/client/remote_town.hpp) | 独立 MPQ 营地绑定，复用 DS1／DT1，不生成本地单位 |
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

- 单位以原 type／ID 归并：0x59 玩家、0x51 物件、0xAC NPC；0x0B 绑定本人，0x0A／0x5C 移除。0x07／0x08 只给出区域与 tile 锚点，不能声称收到房间尺寸；单位位置为 subtile，每 tile 五格。
- 0x0D／0x0F／0x10／0x15／0x16、0x6B／0x6C／0x6D 归并已知绝对坐标；0x67／0x68 只保留目标。本人 0x18／0x95／0x96 解出状态与位置，尾字节是到路径首点的偏移，不是小数坐标。0x1D／0x1E／0x1F 保留原属性编号／值，不当作本地派生属性；单位 lifePercent 保留原回包比值字节，未换算到 0–100。
- 原营地四种 DS1 从 LvlPrest／Levels／LvlTypes 动态读取；至少两类固定物件、所有相关可见物件和城镇 8-tile 房间锚点须一致，候选必须唯一。DS1 物件编号复用既有原版映射。玩家位置不用于猜出生点，也不沿用未认证的离线 DRLG 布局。
- 复用 Map 只解码营地地形，未创建 Region／人口／AI／任务／离线存档。未复制零售版房间随机流；仅允许所有可选 DT1 变体碰撞一致的场景入局，图像变体选择不承诺与原客户端逐像素一致。其他幕、出城及完整种子重建暂缓。
- 0x9C／0x9D 只解出装备外观前缀，按 ID／主人／身体槽更新；不构造库存、物品属性或交易。角色原表／原 COF 外观复用 RealmPortraitCatalog；染色、自动词缀、ethereal、未知组合暂不绘制。NPC 组件选择由 0xAC 位流与当前 MonStats／MonStats2 解出；独特／佣兵染色及缺资源暂不绘制，绝不把中立单位替换为敌人。
- 显示使用原 DT1、DCC／DC6／COF、调色板、AnimData 与 Objects 原表；摄像机跟随服务端本人坐标。没有本地寻路推进、位置预测或 AI；当前以服务器坐标更新显示，NPC 路径和动作／帧节拍不宣称零售版一致。HUD 暂只显示服务器当前生命／法力数值，未猜最大值或填充比例；背包、技能、NPC 服务、战斗和拾取暂缓。
- UI／online-move 共用当前地图／代次／边界／目标地形校验，向原服发 0x01 walk／0x03 run，最多每 100ms 一次。accepted 仅代表入发送队列，必须观察之后的 playerPosition 判断移动结果；动态单位碰撞与最终路径由服务器决定。只允许营地内目标，不承诺原服寻路不会绕出边界；实际位置出营地或匹配失效即撤下场景、停止新请求，允许正常退局。
- 副本限制 8192 单位、4096 房间、2048 装备条目；未知已定长消息计 ignoredPackets，未猜字段。长度错误／位流越界交会话失败处理。online-world 返回副本与匹配诊断；scene.available、movementAvailable、playerDisplayed 和 unavailableUnits 分别说明地图、移动入口、本人图形与未绘制单位，不能仅凭 ProtocolReady 认定可玩。

包长度核对本机 1.13c D2Net；字段交叉参考本地 D2MOO 的 D2PacketDef／SCmd／SUnitMsg／PlrMsg／MonsterMsg／Items 序列化及 1.13 协议表；营地分房参考 DRLGPRESET_BuildArea(false)。1.10f 参考提供语义证据，不代替实际 1.13c 验收。当前 MPQ 四种营地所用瓦片候选未发现碰撞 flags 差异，运行时仍逐项检查；本次实际连服验证 towne1／towns1，北／西向未实机验收。原 800ctrlpnl7.dc6 有七帧，场景与既有 HUD 一致仅绘制前六块面板。

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
