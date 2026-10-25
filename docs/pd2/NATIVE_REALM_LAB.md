# 原生实验

CN原生引擎隔离实验的入口、依赖、来源与限制；不等同D2X自研宿主，也不作为生产部署承诺。 目标身份与证据口径见[版本与证据](BUILDS.md)。

<a id="原生实验部署与证据边界"></a>

## 原生实验：部署与证据边界

来源：[docs/development/PVPGN_SERVER.md](../development/PVPGN_SERVER.md)。

用户的`artifacts/d2gs-local/Play-pd2.bat`现转入`artifacts/d2gs-local/pd2-native-realm-lab`的独立实验栈；旧`artifacts/pd2-native-realm-lab`目录仍保留，不能混用默认路径。实验目录包含独立PvPGN源码副本、CN补丁、输入与engine，实际装载ProjectDiablo.dll。当前为旧核心9c2b＋当前数据cc42的组合，完整身份、桥接校验与对照限制见[PD2版本基线](BUILDS.md#本地原生战网对照环境)；原版参考栈不因此变为PD2。

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

已构建本机实验环境可使用[隔离后端管理入口](../../scripts/Invoke-PD2RealmLab.ps1)，默认LabRoot已指向`artifacts/d2gs-local/pd2-native-realm-lab`；Status只检查后端，不据此断言原生引擎未实现：

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
