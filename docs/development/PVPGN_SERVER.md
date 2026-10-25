# PvPGN 独立游戏服务端

更新：2026-10-10。本页负责独立EXE部署、启停、配置和后端协议边界。客户端连接见[NETWORK](../modules/NETWORK.md)，游戏消息见[MESSAGES](../modules/MESSAGES.md)，领域支持范围见[BASELINE](../../BASELINE.md)。部署目录为`dist/server`；后端契约、当前实现和故障恢复缺口见下文。

## 角色与复用

### PD2 CN原生引擎隔离实验

用户的`artifacts/d2gs-local/Play-pd2.bat`现转入`artifacts/d2gs-local/pd2-native-realm-lab`的独立实验栈；旧`artifacts/pd2-native-realm-lab`目录仍保留，不能混用默认路径。实验目录包含独立PvPGN源码副本、CN补丁、输入与engine，实际装载ProjectDiablo.dll。当前为旧核心9c2b＋当前数据cc42的组合，完整身份、桥接校验与对照限制见[PD2版本基线](../pd2/BUILDS.md#本地原生战网对照环境)；原版参考栈不因此变为PD2。

实验后端以`D2X_PD2_CN_LAB_WIRE=ON`构建Win32 Debug的bnetd、d2cs、d2dbs，产物在`candidate-backend`，不覆盖`backend`内旧发布包。Realm为`D2X-PD2-Lab`，BNCS／MCP／DBS分别仅监听回环16112／16113／16114，附带w3route为16200。配置与数据库在`backend`，输入、参考源码和二进制不纳入源码提交。

| 接入面 | 当前实现与限制 |
| --- | --- |
| 认证 | CN SID 0x51使用完整帧偏移26的两个长度字段，前面为计数及三个WORD掩码；原候选误从24读取已修复。只检查两条64位十六进制证明的形状，不验证产品密钥所有权，不是完整反作弊服务 |
| 版本 | D2XP 0x0d挑战使用`ver-IX86-1.mpq`，固定公式下的实际版本为1.0.13.0、校验和0x80f59bf0，独立标签`D2XP_PD2_CN_LAB`；禁用未知／错误版本放行。该校验不证明整个PD2核心一致，资源摘要仍由D2X固定配置核对 |
| Realm／角色 | CN MCP建房／列表扩展沿既有候选补丁，仅GS1与公共掉落。空列表明确终止；没有可用GS时返回原MCP建房0x32，不进入无限候选队列。普通角色登记与账号可跨后端／客户端重启恢复，不等于游戏存档已验证 |
| 客户端资源 | `config/pd2-client.local.json`使用用户完整PD2安装，包含`Data/local`中文资源；旧冻结客户端缺该目录，不能作为当前默认语言启动入口。正式CN配置和凭据保持不变 |
| 游戏引擎 | Win32隔离宿主在D2Server预初始化后、数据表加载前加载固定PD2核心；已经完成建房、角色进入、原生城镇同步、移动、正常保存与同角色重入。仅实验构建，不是完整战网或生产部署认证；共享仓库、专用规则和异常恢复未验证 |

已接入本地注册／登录、Realm认证、普通非天梯角色创建／选角、大厅与列表、严格版本检查及名册恢复。原生引擎已有普通建房、城镇同步与移动、正常CHARSAVE／CHARINFO／UNLOCKED及同角色重入的消费者；无可用GS时MCP建房返回0x32。独立服务的正常保存／重入与异常退出恢复须分别核对；多客户端、长期战斗、全部技能／地图和复杂存档组合尚未认证。实验服务的成功不能替代正式CN或新版核心兼容证据。

固定构建桥接位于实验源码`src/d2gs/pd2_bootstrap.c`。核心加载要求工作目录有真实Game.exe版本资源；基础MPQ位于引擎上级目录，PD2 MPQ位于引擎目录。使用原版Storm，不加载会写Game.exe固定地址的CN代理PD2_EXT。已证实并规避两处原生补丁冲突：D2Game 0x9D247的EB覆盖PD2 NOP块导致非法回跳；0x2E2E0的旧跳转略过原版游戏锁释放，导致递归计数11触发Fog断言。只调整摘要和字节校验后的进程内宿主补丁，不改输入DLL。数据库角色交付暂时调度到引擎线程，15秒超时采取结果未知的fail-stop，仍需生产化评审。

已构建本机实验环境可使用[隔离后端管理入口](../../scripts/Invoke-PD2RealmLab.ps1)：

```powershell
.\scripts\Invoke-PD2RealmLab.ps1 -Action Start -LabRoot .\artifacts\d2gs-local\pd2-native-realm-lab
.\scripts\Invoke-PD2RealmLab.ps1 -Action Status -LabRoot .\artifacts\d2gs-local\pd2-native-realm-lab
.\scripts\play.ps1 -ClientProfile .\artifacts\d2gs-local\pd2-native-realm-lab\config\pd2-client.local.json -PipeName d2x-pd2-lab -Diagnostic
# 先退出客户端；没有活动客户端或GS连接时才停止实验后端。
.\scripts\Invoke-PD2RealmLab.ps1 -Action Stop -LabRoot .\artifacts\d2gs-local\pd2-native-realm-lab
```

入口不构建、不下载、不启动GS；检查候选EXE、端口与PID／启动时间，不接管其他实例。Start只表示创建进程，须再查Status监听和`backend/var/d2cs.log`的Realm认证。Stop在无外部连接时终止拥有的后端进程，不承诺游戏服优雅保存；未来接入GS须先完成独立保存停服流程。构建、源码补丁和后续步骤维护于实验`NEXT-RUN.md`与`patches/pvpgn.patch`，这些本地文件不随源码交付。

GS通过独立停止事件正常退出后，未使用调试器的新进程再次加载NativeTownFive进入TownRestart，原生地图、玩家显示、移动和心跳均就绪；退出时有三条Storm未释放内存警告。手动启动与停止步骤见实验NEXT-RUN.md。后续重点是GS启停管理、异常断线恢复、并发和共享存储验证；基线INI里的可选原版规则也尚未逐项认证。实验服务器固定使用核心摘要9c2b4d46…，客户端完整资源目录包含更新核心17d5fb1b…，城镇互通不代表全部规则版本一致；更新服务器核心必须独立恢复地址和补丁契约，不用全局偏移迁移。来源限制见[自建战网与开源范围](../architecture/PD2_COMPATIBILITY.md#自建战网与开源范围)。

#### 联网资料与适用范围

2026-10-22联网核对，未找到可直接复现当前CN构建的完整PD2＋PvPGN部署教程或已验证游戏核心；这仅是本轮公开检索结果，不证明不存在私人部署。Google及部分DuckDuckGo／论坛请求受验证限制。

| 来源 | 可借鉴内容与边界 |
| --- | --- |
| [PvPGN D2GS安装指南](https://pvpgn.pro/d2gs_installation.html)／[Magic Builder](https://github.com/pvpgn/pvpgn-magic-builder) | 四服务连接、Realm、地址转换、角色目录、Windows兼容模式和建房排队排查；主要是原版1.13c，不包含PD2核心加载。旧教程网络／安全建议须按实际实现复核 |
| [RElesgoe/d2gs](https://github.com/RElesgoe/d2gs) | 明确D2Server QueryInterface启动链及1.13c适配来源，是本地原生宿主的已有依据；不是PD2兼容证明 |
| [weili0677/d2gs_113c](https://github.com/weili0677/d2gs_113c) | 页面显示继承tesseract2048/d2gs，README限定1.13c、多D2GE与LibD2Server；不能因本地目录含pd2字样而当成PD2服务器 |
| [jaenster/d2-dedicated-server](https://github.com/jaenster/d2-dedicated-server/tree/b661cace6e861b16211310312b55273ccfa753d2) | 新发现的原DLL宿主参考，README宣称包含1.13c引擎及额外DLL加载；Linux优先，Realm使用Redis／Postgres，不是现有PvPGN的直接替换组件，也未确认当前PD2 CN支持 |
| 上述项目的[DLL宿主](https://github.com/jaenster/d2-dedicated-server/blob/b661cace6e861b16211310312b55273ccfa753d2/docs/dll-host.md)、[Mod接入](https://github.com/jaenster/d2-dedicated-server/blob/b661cace6e861b16211310312b55273ccfa753d2/docs/MODDING.md)、[状态](https://github.com/jaenster/d2-dedicated-server/blob/b661cace6e861b16211310312b55273ccfa753d2/docs/STATUS.md) | 可定点核对模块／数据表初始化、回调生命周期与调用约定、网络泵及任务重挂；文档混有历史修正，1.10f导出序号不可套到1.13c。状态页明确pre-1.14角色回写未完成；Mod示例的Game.exe注入路径也不能直接等同于1.13c DLL宿主 |
| [Project-Diablo-2公开仓库](https://github.com/Project-Diablo-2) | 当前公开列表为BH、启动器、安装器、图形等项目，未见完整游戏服务核心。公开客户端组件不等于服务器构建链 |
| [docker-pd2](https://github.com/bethington/docker-pd2) | Wine＋虚拟显示＋VNC运行客户端，所谓MCP是AI接口，不是Diablo Realm协议；不能作为PD2 D2GS部署方案 |
| [暗黑2纯Linux服务器构建](https://blog.csdn.net/qq_34404284/article/details/122766293) | 中文原版PvPGN＋D2GS-113c-build03＋Wine部署参考，不是PD2专用教程 |

后续优先把新DLL宿主参考与当前本机1.13c／PD2二进制逐入口交叉核对，不迁移整套Realm架构、不修改D2X客户端逻辑，也不执行未审查的第三方加载器。当前目标仍是由本地游戏服务提供真实城镇同步，D2X只修改服务器连接目标。

### D2X原版独立宿主

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
