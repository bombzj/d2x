# PvPGN 独立游戏服务端

PD2专项内容：[运行与部署](../pd2/OPERATIONS.md)。本页保留原版／公共规则及必要的版本边界。

更新：2026-10-10。本页负责独立EXE部署、启停、配置和后端协议边界。客户端连接见[NETWORK](../modules/NETWORK.md)，游戏消息见[MESSAGES](../modules/MESSAGES.md)，领域支持范围见[BASELINE](../../BASELINE.md)。部署目录为`dist/server`；后端契约、当前实现和故障恢复缺口见下文。

## 角色与复用

### PD2 CN原生引擎隔离实验

PD2专项说明见[原生实验：部署与证据边界](../pd2/NATIVE_REALM_LAB.md#原生实验部署与证据边界)。

<a id="联网资料与适用范围"></a>

原生实验的联网来源与适用范围见[PD2实验资料](../pd2/NATIVE_REALM_LAB.md#联网资料与适用范围)。

<a id="d2x原版独立宿主"></a>

### D2X独立宿主

新增控制台程序`d2x_server.exe`，CMake目标为`d2x_pvpgn`（`d2x_server`已是现有权威内核库）。它替换原D2GS游戏进程，不替换bnetd、D2CS、D2DBS，也不是启动原Game.exe的包装器。无需窗口、音频设备或原D2Game DLL，不链接presentation／raylib。

客户端继续Battle.net账号认证→D2CS选角／房间→D2X游戏端口；账号和角色数据库仍由PvPGN拥有。已有完整客户端、GameHost／GameInstance、领域系统、MPQ地图和内容准备、NativeRealmService游戏握手／分派／复制、原D2S codec全部复用，不复制一套玩法。

| 代码入口 | 职责 |
| --- | --- |
| [server_main.cpp](../../src/server_main.cpp) | JSON配置、MPQ加载、控制台日志、信号／停止文件、调度 |
| [pvpgn_server.cpp](../../src/hosting/pvpgn_server.cpp) | D2CS控制、一次性票据、D2DBS锁／保存、TCP游戏会话与停服 |
| [pvpgn.hpp](../../src/network/protocol/pvpgn.hpp) | 独立8字节后端帧，长度／容量校验 |
| [NativeRealmService](../../src/hosting/detail/native_realm_service.hpp) | 外部票据入场与保存回调；本地MCP／文件租约保持原路径 |
| [NativeRealmHost](../../src/hosting/detail/native_realm_host.cpp) | 共用实例推进、异步内容准备、领域事实原包输出 |

当前接受资料片普通／Hardcore、非Ladder及当前codec／内核可准入角色，死亡HC拒绝入局，普通／HC房间隔离；七职业／五幕完整玩法、原版客户端全协议仍未完成。不能把替换进程等同于替换完整原版游戏功能。原服角色中不支持的存档内容会拒绝准入，不删字段降级加载。

## 部署

1. 先备份PvPGN角色数据库。建议使用独立开发Realm／数据库副本，不直接接生产角色。
2. 保留原bnetd、D2CS、D2DBS启动方式。手动停止要替换的原D2GS，并确认4000未被其他进程占用。新脚本不代为停止或修改任何原服进程／启动脚本。
3. D2CS的游戏服务器允许列表、D2DBS的允许地址应包含D2X进程来源IP；D2CS对客户端公布的GS地址／NAT映射必须能到达该机器4000。账号、Realm及D2DBS名称配置一致。
4. 本实现仅无签名后端注册，D2CS需要`d2gs_checksum=0`；`d2gs_version=0`可禁用版本检查，或在新配置中显式设置匹配值。不会自动改PvPGN配置，也不伪造原引擎校验和。签名挑战、非零反作弊版本明确拒绝。
5. 配置当前原MPQ路径与恢复目录，使用以下独立脚本。先启动PvPGN服务，后启动D2X；关闭时反向操作，必须先等D2X保存成功再关D2DBS。

仓库内默认配置：[pvpgn-server.example.json](pvpgn-server.example.json)。所有文件路径相对配置文件目录解析，不相对终端目录。`realm`为空时采用D2CS认证挑战中的Realm名；填写时必须一致。

```powershell
.\scripts\Start-D2XServer.ps1 -Config .\docs\development\pvpgn-server.example.json
.\scripts\Stop-D2XServer.ps1 -Config .\docs\development\pvpgn-server.example.json
```

发行包在`dist/server`，可用包内scripts同名入口；默认读包根`server.json`。包位于仓库内时示例MPQ／恢复路径仍有效，移动包到其他位置后必须编辑路径。也可直接运行`d2x_server.exe --config <绝对路径>`，Ctrl+C请求优雅停止。直接启动不创建脚本PID记录，不能用停止脚本管理该进程。

| 配置 | 默认／含义 |
| --- | --- |
| listen | 0.0.0.0:4000，仅游戏连接；不监听MCP |
| d2cs | 127.0.0.1:6113，出站控制连接 |
| d2dbs | 127.0.0.1:6114，出站锁／存档连接 |
| realm | 空时采用挑战值，否则严格匹配 |
| version | 默认0，可配置D2CS期望的32位版本值；不自动猜原版DLL版本 |
| maximumGames | 默认16，范围1–256；每房最多8人，总TCP会话最多64 |
| mpq | 用户原MPQ目录或单个MPQ，不随包复制 |
| ruleset | `lod113c`（默认）或`pd2-local`；未知值拒绝 |
| pd2Directory | `pd2-local`必填，当前固定PD2资源目录；原版模式不接受此字段 |
| recovery | 带原子写入和.bak的恢复副本目录，不作为正常角色库 |
| stopFile | 本机停止请求文件；每实例必须使用独立路径 |

游戏默认端口已统一恢复4000，包括LAN Host／Join及原服客户端默认值。显式`--game-port`或online配置仍有效；同机LAN Host和独立D2GS不能同时绑定4000。D2CS原加入回复没有自定义GS端口字段，部署改变游戏端口时必须同时配置客户端／NAT，不会在原包插入私有字段。

## 启停与日志

启动脚本后台启动进程，记录PID、启动时间、EXE绝对路径和配置路径，标准输出／错误分别写入`<stopFile>.stdout.log`／`<stopFile>.stderr.log`；每次启动会覆盖这两个日志，需要留存时先复制。它只表示进程已创建，实际连接成功以日志`D2CS registration accepted`／`Game listener ready`为准。停止脚本按UTC时间刻度核对记录，兼容PowerShell将JSON时间自动读成DateTime；取得进程句柄后创建stopFile并等待退出及退出码，不使用Stop-Process、不强杀、不停止bnetd／D2CS／D2DBS／原D2GS。超时保留记录并报错；失败退出需检查恢复目录、后端锁和日志，再人工清理旧控制文件。

控制台输出内容加载、注册、房间编号、角色进入／保存确认、停服与错误。不会输出票据、密码、原包或D2S载荷。运行时仅使用标准C++信号及文件控制，不把Win32引入玩法；PowerShell脚本负责Windows部署。关闭窗口的强制终止、系统关机和崩溃不保证优雅保存，应使用停止脚本或Ctrl+C。

## 后端契约

两种后端均用小端`u16 total,u16 type,u32 sequence,body`，总长含8字节头，最大65535；输入缓冲256 KiB。D2CS连接先发64，D2DBS先发65。DBS请求体的字符串实际顺序为account、character、realm，各带NUL，依据实际处理器而非不完整头文件注释。

| 通道／消息 | 已接范围 |
| --- | --- |
| D2CS 10／11／12 | challenge／注册结果／容量；128字节空签名，拒绝签名要求；旧引擎配置只消费不执行 |
| D2CS 13／14／15／16 | 心跳、停止／重启请求（只优雅退出，不启动新进程）、初始化、旧引擎配置接收 |
| D2CS 20／21 | 建房预留、一次性角色票据；拒绝未支持模式、重复角色、满房 |
| D2CS 22／23 | 进入／定期更新／离开、房间关闭；空预留房60秒回收 |
| D2DBS 31 | 类型1 charsave读取并锁定，类型2 charinfo读取；锁失败拒绝入局 |
| D2DBS 30 | D2S和charinfo分别保存，按序号／类型／角色匹配确认 |
| D2DBS 33／34 | 解锁、心跳；解锁原协议无确认，不宣称获得解锁ACK |

客户端0x68的u32 token和u16 gameId必须匹配D2CS21及姓名，验证1.13c签名与职业后消费票据。票据有效30秒。加载后的D2S姓名、职业、模式、难度进度以及charinfo的magic／版本／三种身份再次验证；准入沿原领域规则，不信任客户端角色属性。

本地PvPGN默认`newbie.save`为130字节v89登记，带INIT位且尚无角色内容。仅接受该长度／版本、原marker 0x82、状态0x21（普通）或0x25（HC）及匹配姓名／职业，charinfo还须为一级、零经验、相同HC身份的非Ladder资料片；复用MPQ CharStats创角，首次保存写完整v96。原始登记先写恢复副本，后续保存保留.bak；不把已有v89角色迁移为新角色。D2CS初始化0x15的checksum与AC说明两个NUL字符串都消费，非零AC版本仍拒绝。

charinfo沿本地PvPGN192字节布局，头部112字节、portrait34、pad30、summary16。保留原创建时间和未知字段，更新最后访问时间、共同外观编码、经验／状态／等级／职业；不实现完整染色肖像或累计在线时间。D2S／charinfo分别确认，DBS原协议没有跨两文件事务，因此中途失败可能只保存一份；恢复文件供人工比对，不自动回写或静默回滚。

加载时保留原始恢复副本，之后约60秒保存一次；主动0x69先等待死亡结算，再保存两份数据、排队解锁及离局通知，之后才回B0。断线也走保存路径。后端失败停止继续入局／执行，不自动重新连接，保存结果未知时保留恢复文件供管理员处理。恢复文件命名为account-character，角色名／账号限制ASCII字母、数字、连字符与下划线，路径不采用客户端任意文件名。

首版DBS事务串行等待，最大15秒，期间只服务D2CS控制连接；内容准备与DBS等待可能阻塞其他游戏会话，不是高负载／长期运行认证实现。没有TLS或后端消息签名，部署在可信内网／回环，不能将6113／6114直接作为不可信公网后端接口。未实现自动重连、热配置、Warden、Ladder更新、全部D2CS扩展和服务管理器安装。

## 构建与打包

| 发行类型 | CMake可执行目标 | EXE | 打包入口 | 目录 |
| --- | --- | --- | --- | --- |
| 原客户端 | `d2x` | `d2x.exe` | [package.ps1](../../scripts/package.ps1) | `dist/current` |
| 独立服务端 | `d2x_pvpgn` | `d2x_server.exe` | [package-server.ps1](../../scripts/package-server.ps1) | `dist/server` |

这是两个构建目标与两份发行脚本，不是两个CPack target；原客户端打包保留。`d2x_server`这个CMake名字仍属于内核库，构建独立EXE须选择`d2x_pvpgn`。

使用现有CMake配置中的`d2x_pvpgn`目标，输出`build/bin/d2x_server.exe`。独立打包入口[scripts/package-server.ps1](../../scripts/package-server.ps1)只复制已构建EXE、可用网络DLL、独立启停脚本、说明及许可到`dist/server`，不编译、不运行、不复制MPQ／数据库、不覆盖已有server.json，不修改`dist/current`或原PvPGN脚本。

```powershell
.\scripts\package-server.ps1
```

独立服务端使用同一`Archives`挂载／解压缓存和`sharedClassicData`内容准备。嵌入宿主与独立宿主共用内核，但后端锁、保存确认和停服属于独立部署边界。

## 未闭合边界

空目录首次房间列表的超时原因仍未明确。主动停服会关闭游戏连接，客户端显示连接失败；重新登录及恢复依赖后端保存状态。原版Game.exe、跨机器／Linux、满房与高负载、后端中断／保存失败／崩溃恢复、全部职业／三难度及长期运行仍缺完整对照；签名／非零AC、Ladder按前述限制拒绝；HC死亡沿共用宿主保存，完整混合客户端与后端故障组合仍待核对。

## 参考依据

本地固定PvPGN参考：common/d2cs_d2gs_protocol.h、common/d2cs_d2gs_character.h、common/field_sizes.h、d2cs/handle_d2gs.cpp／handle_d2cs.cpp／d2charfile.cpp、d2dbs/dbspacket.h／dbspacket.cpp。D2CS校验、票据与DBS锁／实际字符串顺序均按这些入口核对；独立编解码，不将参考仓库或原引擎二进制打包。来源／许可见[THIRD_PARTY](../resources/THIRD_PARTY.md)。
