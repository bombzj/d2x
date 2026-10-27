# PD2版本与证据边界

本页限定各分册的目标身份及静态证据解释方式。相同文件名、启动器模式名或相近地址都不能替代摘要和实际调用链；本目录不在产品中加载或Hook原DLL。

<a id="目标版本"></a>

## 规则分析基线

下表的“当前”指各分册RVA和规则结论使用的分析基线，不是用户安装目录始终不变的最新版本，也不是运行时资源白名单。更新后的安装身份单独列于后文；旧结论不能仅因新包可以启动就改标为新核心结论。

| 标识／文件 | SHA-256 | 适用边界 |
| --- | --- | --- |
| 当前核心／ProjectDiablo.dll | `17d5fb1b049d8076226d27295487b49c8b5f2629553c193a01aa1e3d8afaba57` | 5,243,904字节；当前宿主技能、物品、价格的定点分析目标；各条目未另注明的核心RVA使用此版本 |
| 当前pd2data.mpq | `cc42f5c71d61c9a38d5b948243ed2fe6a9a2bc0124fb3eaaff0f1943830e15cf` | 当前隔离目录实际资源摘要；表字段及记录身份须与此输入核对，历史解析结果有独立版本时保留其范围 |
| 当前pd2maps.mpq | `fb9e21ae5d797caa7ae8051ed15752a71a35ac317b44c8cdde3c443ad62b25eb` | 当前扩展地图DS1／DT1输入，不能用旧地图包替代后宣称布局相同 |
| 当前pd2assets.mpq | `2ceb83ca8e9f482fa46a94cda95d2ab80f8415a0d2c1047a44f64d4a95cf56af` | 当前配套图形资源，与地图包和数据表独立标识 |
| 当前pd2monchars.mpq | `ae2abade85c5f393db5d8089cb4b6566b2cfed2adea0f573bc0f4220ef733217` | 当前怪物图形包；自研宿主资源配置与其余四个MPQ一起核对 |
| 当前patch_d2.mpq | `b976e7847dda696f54198a9477819f96579c4894f54381c72afb4937e919e0a9` | 当前PD2目录补丁包；覆盖原目录后再挂载data／assets／maps／monchars，不作为原版补丁身份 |
| 配套D2Common.dll | `0406922db7b9959484a08f83bcc6a0fe9d11a157cb2322a061033e8d37480217` | 当前包装器负序号的实际解析模块，不直接套D2MOO序号 |
| 配套D2Game.dll | `24b293b86f95bea0228826c502ea04737ba21b91ca9832e52aac4b11fef14f53` | 当前服务端RVA的实际解析模块 |
| 旧核心／ProjectDiablo.dll | `9c2b4d4627112d7f14ab959e71c56e947b7ceeb6f16424a637364f614d39122f` | 5,230,080字节；[完整函数目录](../resources/pd2-core/README.md)、旧补丁／程序表及未另标版的历史取证 |
| 旧pd2data.mpq | `6ce1d885dfcb16ac0ff7a183d56c56baeaad591d8779ce9e843e16dfc2756002` | [首批样本摘要](BUILDS.md#已取得的-cn-目标样本)对应的资源，不能与当前资源混称同一输入 |

分析样本原取自隔离目录`D:/Downloads/Diablo II V1.13C-PD2-Analysis/pd2/`；该目录可被启动器更新，复核时必须另核对同名文件的实际摘要，路径不能作为版本身份。核心和MPQ可以分别更新，因此同一核心摘要不自动证明资源相同。其他资源、BH、mdll1、PD2_EXT、Bnclient和原客户端等模块按[样本取证](../resources/PD2_CN_DLL_ANALYSIS.md)各负责段落的摘要解释，不套用核心的版本标识。

上述两份ProjectDiablo.dll的PE文件属性均未提供FileVersion／ProductVersion。代码中的`Normal20261020`／`Normal20261021`／`Normal20261026`分别标识9c2b…／17d5…／41d49…样本，属于本项目的构建枚举，不是DLL自带的产品版本号；`pd2-cn-normal-20261020`是兼容协议配置名，不能仅按名称判断实际文件版本。核心摘要仅选择已核实的计数元数据，不作为其他认证／玩法的版本白名单。

CN Normal／bnet／TCP是已有目标范围；旧快照主文件与`.sn`摘要相同，`.ss`不同。不能把此事实推广到其他发布、赛季、官方版或KCP／QUIC。CN附加模块提供的存储、认证和控制扩展须注明模块，不一概归为ProjectDiablo核心功能。

### 安装更新与运行边界

2026-10-26更新后的上述安装目录身份如下；F57–F61与各分册明确标注41d49的条目已按其实际函数体复核，其余旧RVA不自动改认此版本。单机和TCP/IP读取实际MPQ，资源包摘要变化不再导致启动失败；具体挂载及校验见[资源加载](RESOURCE_LOADING.md#更新与兼容性检查)。

| 文件 | SHA-256 | 当前边界 |
| --- | --- | --- |
| ProjectDiablo.dll | `41d49d63d531050bb0c89db9efd191fefd322378683159be5dc059498ae1e424` | 5,252,096字节；核心计数选择表及地图代码已逐项核实，接入Normal20261026；认证按实际文件与服务端挑战检查，不因该摘要拒绝登录，本地玩法不加载该DLL |
| pd2data.mpq | `f1e0bd44111ffbabbe6548fcf249bba5bab808de4bb6bb9750a3019fae534162` | 运行时读取当前表，规则差异须逐条核实 |
| pd2assets.mpq | `6945dc862f2360df61ce9d21652a5587b4637b1251ae51d0d1e2ce1713588cd9` | 更新后的图形资源 |
| pd2maps.mpq | `a1cbc2bdc899b679cf396607d14b52647d95697b957bca1c06432ac892197a5b` | 更新后的地图资源，旧DS1结论仅适用于各自摘要 |

patch_d2.mpq与pd2monchars.mpq摘要仍与规则分析基线相同。启动不再要求更新资源白名单；真正改变表结构、存档格式或协议的版本仍需对应适配，不能承诺所有未来版本自动兼容。

已按41d49实际函数体核定的本地规则对应包括：17d5的3452D0→345430（充能回调）、3451E0→345340（首次登记）、37F760→381720（参数层）、3810A0→383060（开图）、38A000→38BFC0（预置）、377580→3792C0（Outer Void阶段）、3A8270→3AA650（Zhar操作）、383060→385020（地图Gheed报价）、37A6F0→37C510（double缩放）、37A740→37C560（地图范围）。对应只覆盖各分册已读分支，不作全库地址平移或全部算法相同声明。分析工程为忽略目录`town-queries/current-41d/current41d`，原始41d49样本另按完整摘要保留；持久结论仍记录实际模块／RVA及调用链。

属性通知查询2FAE70→2FAE80、主人查询2FAA20→2FAA30及Vengeance Do174的3B10E0→3B3550另经实际函数体复核。计数选择器41d49的37E630使用37EE60字节表／37EC40跳转表及434DB0地图代码；300项普通选择与60项地图代码均与17d5逐项相同，详见[F63](parity/ITEMS.md#f63-41d49核心计数映射)。这些局部对应不代表新版全部玩法等价。

### 本地原生战网对照环境

用户的`artifacts/d2gs-local/Play-pd2.bat`转入其下的`pd2-native-realm-lab`；另一个`artifacts/pd2-native-realm-lab`目录仍存在，二者不是同一路径，也不是链接。选择环境时显式传LabRoot并核对配置／进程路径，不能只按目录名认定目标。

该启动链的engine／ProjectDiablo.dll摘要为上表旧核心`9c2b…`，engine／pd2data.mpq为当前`cc42…`，D2Game为上表配套`24b2…`。D2Server.dll另为`2b70b83e8324e27b186e87dbf1fcc9e9ef7ff861ccf276a6c47312b2d0eb05f3`；实验桥接`source/native-d2gs/src/d2gs/pd2_bootstrap.c::InstallPd2Bootstrap`要求这两份DLL的完整摘要及原指令字节，随后在预初始化和表加载之间装载核心。它没有接受新核心的声明，不能仅复制`17d5…`覆盖文件或放宽摘要校验。

这是“旧核心＋当前表”的独立对照组合。可用它采样已分别确认的共有规则、客户端原包及原生保存；结果必须标记该组合，不能直接认证当前`17d5…`核心的全部行为。自有宿主的Pd2Local与实验Realm的CN方言也不同，按命令含义、原字段及可观察结果对照，不要求两套信封逐字节相同。部署及入口见[原生引擎实验](NATIVE_REALM_LAB.md#原生实验部署与证据边界)。

## 地址与证据

ProjectDiablo.dll静态ImageBase为0x10000000。文档使用所属模块内RVA；VA转RVA须减去该模块实际ImageBase，不能混用核心、D2Game、D2Common或D2Client地址。

恢复规则时优先沿D2MOO原调用链及配套MPQ理解，再按目标补丁、程序表和真实指令确认差异。`FUN_地址`、自动函数名、导出器的语义候选、发现callers或成功反编译都只是定位证据。ECX／EDX、栈清理、SSE参数和原生有符号／无符号宽度须在指令层核对；查询内部地址可能落到相邻函数。

已恢复条件的条目保留版本、模块／RVA、输入输出、顺序与副作用；推断和观察注明适用条件，缺失部分保留未知。数值／结构来自MPQ时记录表、记录身份与字段；TXT物理行、编译记录、hcIdx、PetType idx及程序编号不能混用。

## 分析材料与长期文档

`reference/pd2-cn-analysis/`保存目标摘要、提取表、反汇编和Ghidra定点查询等辅助材料，按仓库规则忽略，不进入构建。当前定点工程与旧全量目录的版本分开；[旧核心函数目录](../resources/pd2-core/README.md)是地址索引，不是当前全部语义已恢复的证明。

本目录的结论必须在辅助缓存清理后仍能凭摘要、模块地址、表字段及源码入口重新定位；不只引用临时截图／日志或某次冒烟。玩法规则归负责分册，协议具体字段留在[DLL与位流取证](../resources/PD2_CN_DLL_ANALYSIS.md)，代码能力以当前消费者为准。

## 包装器解析与反编译纠正

当前Ghidra工程为忽略目录`reference/pd2-cn-analysis/town-queries/current.gpr`，程序名为`17d5fb1b049d8076226d27295487b49c8b5f2629553c193a01aa1e3d8afaba57-ProjectDiablo.dll`。`core-catalog`完整目录仍是旧SHA，不能从中直接移植函数地址。现有`core-catalog/scripts/AnalyzePd2Paths.java`支持用`stack_arguments`修正包装器栈清理后再导出；`create_unowned`只辅助建立遗漏的函数边界，不证明自动命名正确。

核心解析器RVA `0x3A3B30`的ECX模块号1为D2Common、6为D2Game；Common的EDX为负导出序号，Game为正RVA。当前版本索引为0。复核链为包装器全局指针→初始化构造器→getter→版本表第0项→实际模块函数；查全局指针在代码段的绝对引用时，已核对的构造器CALL位于该引用前8字节，不能统一按前7字节截取。Common序号随版本变化，D2MOO旧序号或包装器自动名称均不足以确定语义。

| 当前核心包装器RVA | 版本表／实际配套模块入口 | 已核对身份与纠正 |
| --- | --- | --- |
| `0x39F2B0` | getter `0x310CC0`，表`0x4A8AA4`；D2Game `0xDDDE0` | `SUNITDMG_AllocCombat`，不能当成`FillDamageValues` |
| `0x3A23C0` | getter `0x308A60`，表`0x4A8B94`；D2Game `0x770E0` | `ApplyCriticalDamage`，不能当成`CalculateTotalDamage` |
| `0x3A38C0` | getter `0x311A90`，表`0x4A7A1C`；D2Game `0xDA6D0` | 释放匹配的combat节点 |
| `0x3A0200` | getter `0x311140`，表`0x4A7444`；D2Game `0xDA540` | 参数化Weapon Block概率，最多取32个属性层 |
| `0x30DE50` | 表`0x4A6BA0`；D2Common ordinal10689／`0x32C10` | `UNITS_SetOverlay`，Charge在Drain后给目标overlay147 |
| `0x30DE80` | 按调用处及返回结构复核 | `UNITS_GetUsedSkill`，不是路径／速度辅助器 |
| `0x3337C0` | 表`0x4993F8`；D2Common ordinal10080／`0x7F5E0` | 到坐标的整数距离`max(dx,dy)+min(dx,dy)/2` |
| `0x309470` | D2Common ordinal10179／`0x4DFD0` | 已用技能Param3（结构偏移0x20），不是路径速度 |

`AllocCombat`在ResultFlags命中`0x8380`或没有SUCCESSFULHIT（1）时，跳过Fill及Calculate但仍分配combat节点；HitFlags含2也跳过Fill。普通分支调用D2Game `0xDD450`填充、`0xDC0B0`计算，之后可能设置WILLDIE（2），最后经`0xDAB90`分配。不能由“分配了combat”推断“又扣了一次生命”。

D2MOO `D2Game/src/UNIT/SUnitDmg.cpp`的`SUNITDMG_ExecuteEvents`在将物理伤害限至目标当前生命之前执行DOMELEEDMG／DAMAGEDINMELEE。Drain内部使用HitFlags加`0x20`的局部伤害副本，原pCombat仍保留原分配数据。原`SKILLITEM_EventFunc20_SkillOnAttackHitKill`在概率非正或有pDamage但缺少`0x20`时拒绝，除此按单位原随机数取百分比；它不要求伤害总量大于零。因此Vengeance零物理接触仍可能触发近战溅射。

另有两处应按指令纠正：`0x37AA30→0x37A9C0`判断区域157／159／166，`0x37AA60→0x37A9F0`只判断166，均不是城镇或移动判断；Bash的memset之后仍显式写伤害头，不能因反编译省略写入而认定标志全零。

## 已取得的 CN 目标样本

用户授权在原版的独立副本中运行启动器。目标文件位于副本的 `pd2` 子目录；启动器创建 `Game.exe` 子进程，游戏日志中的 Application Path 也指向该副本。本轮仅据此确认程序启动，不认证选角、入局或完整游玩。后续D2X已使用用户提供的联调账号核对账号大厅及MCP票据；未保存原始认证载荷，未完成游戏场景。

配置快照为 `seasonMode=Normal`、`gamemode=bnet`、`udpMode=0`、`noUpdate=0`。界面另有单机、TCP／UDP(KCP)／UDP(QUIC)、直连／中转等选项；这些只能说明启动器提供相应入口，尚不能推出隧道内部报文格式或所有模式使用相同资源。主 `pd2data.mpq`／`ProjectDiablo.dll` 与各自 `.sn` 文件的大小、SHA-256 一致；`.ss` 文件不同。切换模式后必须重新核对配套文件，不能混用。

| 文件 | 字节数 | SHA-256 |
| --- | ---: | --- |
| ProjectDiablo.dll | 5,230,080 | `9c2b4d4627112d7f14ab959e71c56e947b7ceeb6f16424a637364f614d39122f` |
| PD2_EXT.dll | 83,456 | `c253dec5a9fab068f2cc64c816392ddc3ed97dfa9822aa5725ace3e64a02d6ab` |
| pd2data.mpq | 2,259,046 | `6ce1d885dfcb16ac0ff7a183d56c56baeaad591d8779ce9e843e16dfc2756002` |
| pd2assets.mpq | 143,872,381 | `70baa8e5334b38dd2c4e3cb8f6517eab0a1cc46e49f667e111af687f764b7ac7` |
| pd2maps.mpq | 5,240,058 | `ad704b94aab4fa4142fb4fe37b38bd3fe7ae43a94c14518baf0d6cb0ca00d33a` |
| pd2monchars.mpq | 373,591,568 | `ae2abade85c5f393db5d8089cb4b6566b2cfed2adea0f573bc0f4220ef733217` |
| patch_d2.mpq | 2,108,703 | `b976e7847dda696f54198a9477819f96579c4894f54381c72afb4937e919e0a9` |
| Game.exe | 61,440 | `065aedaa998adcd337b93ccf33457e3bac589bab3bfd894351b6fa834399985d` |
| D2Common.dll | 679,936 | `0406922db7b9959484a08f83bcc6a0fe9d11a157cb2322a061033e8d37480217` |
| D2Win.dll | 147,456 | `6015e0b67834a8f3c657b32ea440cb04dad42e01e8bcf5782f90b14323769e0c` |
| Storm.dll | 372,736 | `cc848eb2fc29068fb61cb258c1c8b9e8a191c5543805939762ed835b50fb194d` |

另外包含 D2MultiRes、SGD2FreeRes、CheckRevision 相关包及 loose `Data` 目录，不能将目录内所有 MPQ 都当作游戏数据扩展。更新时出现的 `zylpd2update.tmp` 是文件名和十进制校验值的临时清单，更新完成后已移除；本页仅采用此前读到的字段，不依赖该缓存仍存在。这些数值的算法未在本轮确认，不能当成 SHA-256 或发布者签名。

分析产物在忽略的 `reference/pd2-cn-analysis/`：`TARGET_REFERENCE.json` 记录 60 个输入文件、28 个提取表文件的大小与摘要，另有 PE 元数据、静态反汇编；提表使用已有资源工具的只读 list／extract 路径。提取资源、目标 DLL 和分析缓存不纳入源码提交。
