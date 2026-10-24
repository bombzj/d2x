# PD2版本与证据边界

本页限定各分册的目标身份及静态证据解释方式。相同文件名、启动器模式名或相近地址都不能替代摘要和实际调用链；本目录不在产品中加载或Hook原DLL。

## 目标版本

| 标识／文件 | SHA-256 | 适用边界 |
| --- | --- | --- |
| 当前核心／ProjectDiablo.dll | `17d5fb1b049d8076226d27295487b49c8b5f2629553c193a01aa1e3d8afaba57` | 5,243,904字节；当前宿主技能、物品、价格的定点分析目标；各条目未另注明的核心RVA使用此版本 |
| 当前pd2data.mpq | `cc42f5c71d61c9a38d5b948243ed2fe6a9a2bc0124fb3eaaff0f1943830e15cf` | 当前隔离目录实际资源摘要；表字段及记录身份须与此输入核对，历史解析结果有独立版本时保留其范围 |
| 当前pd2maps.mpq | `fb9e21ae5d797caa7ae8051ed15752a71a35ac317b44c8cdde3c443ad62b25eb` | 当前扩展地图DS1／DT1输入，不能用旧地图包替代后宣称布局相同 |
| 当前pd2assets.mpq | `2ceb83ca8e9f482fa46a94cda95d2ab80f8415a0d2c1047a44f64d4a95cf56af` | 当前配套图形资源，与地图包和数据表独立标识 |
| 配套D2Common.dll | `0406922db7b9959484a08f83bcc6a0fe9d11a157cb2322a061033e8d37480217` | 当前包装器负序号的实际解析模块，不直接套D2MOO序号 |
| 配套D2Game.dll | `24b293b86f95bea0228826c502ea04737ba21b91ca9832e52aac4b11fef14f53` | 当前服务端RVA的实际解析模块 |
| 旧核心／ProjectDiablo.dll | `9c2b4d4627112d7f14ab959e71c56e947b7ceeb6f16424a637364f614d39122f` | 5,230,080字节；[完整函数目录](../resources/pd2-core/README.md)、旧补丁／程序表及未另标版的历史取证 |
| 旧pd2data.mpq | `6ce1d885dfcb16ac0ff7a183d56c56baeaad591d8779ce9e843e16dfc2756002` | [首批样本摘要](../architecture/PD2_COMPATIBILITY.md#已取得的-cn-目标样本)对应的资源，不能与当前资源混称同一输入 |

当前摘要的可复核输入为隔离目录`D:/Downloads/Diablo II V1.13C-PD2-Analysis/pd2/`的同名文件；路径只供定位，不能作为版本身份。核心和MPQ可以分别更新，因此同一核心摘要不自动证明资源相同。其他资源、BH、mdll1、PD2_EXT、Bnclient和原客户端等模块按[样本取证](../resources/PD2_CN_DLL_ANALYSIS.md)各负责段落的摘要解释，不套用核心的版本标识。

CN Normal／bnet／TCP是已有目标范围；旧快照主文件与`.sn`摘要相同，`.ss`不同。不能把此事实推广到其他发布、赛季、官方版或KCP／QUIC。CN附加模块提供的存储、认证和控制扩展须注明模块，不一概归为ProjectDiablo核心功能。

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
