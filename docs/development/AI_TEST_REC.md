# 调试 AI 五幕任务验收记录

本页保留最近复测、当前缺陷、验收条件和证据入口。功能说明归[调试 AI](DEBUG_AI.md)，跨模块验收清单归[原版缺口](ORIGINAL_GAPS.md#本批验收清单)。源码修复与运行通过分别记录；原版 Host 结果不认证 D2X 宿主。

## 原版Host Act4–5续测：2026-11-04 14:07–14:32（修复后9／9完成）

沿用74684FD0固定构建、19D41991配置及前三幕18项实际终档D8C75FA7…A68C1，复制匹配探索缓存到独立目录；入局未报缓存恢复失败。原版Host仍PID268332／4000，同seed1111012732／secondary1506396018，本人GUID4。act4-5共9目标，Frozen Orb／Teleport48／250ms、只顺路开点、保留服务。证据`artifacts/ai-native-act4-5-20261104-140734/`，identity、initial-world及原包／行为／帧记录。

伊卒尔、熔炉自主完成后，混沌避难所寻混沌大臣时在7771,5200与7792,5160反复传送，两点各46次，约110秒；stop-01-vizier-*及vizier-oscillation-audit.json保留现场。S2C AC seq10881实际指派混沌大臣GUID4943／class306／superUnique38，位置7667,5321、life128；错误提示7792,5160属于普通同类尸体4886，vizier-assignment-evidence.json留原包。当前单位匹配已核对超级暗金身份，但approachKnown误用仅按class保存的坐标。现按GUID敌人记忆保存／筛选超级暗金与精英身份，缺精确线索则正常探索；不以普通同种怪或尸体代替。修改ai_impl.hpp私有敌人记录、ai_events.cpp及ai_navigation.cpp，不改原版怪物规则或持久化动态GUID。

暂停后正常保存退出0；native-02-vizier-fixed仅复制实际存档21786578…9D62D3及匹配缓存。修复版EXE SHA256 `611EE0677BDA091A340E903E92F2B71743016B7C2D6D50F3C9C283EAB9D897B6`，原配置不变；vizier-fix-build.log共24步成功、零编译／链接诊断、输入稳定。14:20同一Host／同种子重入，本人GUID5，重提完整act4-5后按私人状态跳过前两项。29.196秒实际选中4943，30.007秒已切换其他待清首领；随后完成迪亚波罗及泰瑞尔红门进入109区。原先已开的封印状态保留，不称从未开封印独立重跑。

修复后同一进程完成第五幕全部六项，无进一步人工接管／计划重提：囚犯、安雅解冻及红门、尼拉塞克、三古人、王座五波、巴尔和终局出口。瓦特之厅实见terrain-clue-confirmed vaught direction=1,0；五波等待中按实际存活敌人切换战斗，终局泰瑞尔延迟期间保持每秒复查。final-ai为completed／9，S2C6107 seq43227，final-victory.png实际显示Slayer。该结果为换版分段续测，不是同版空档连续通关，也不认证D2X宿主、其他职业／难度。

| 目标 | 本次成功段耗时 |
| --- | ---: |
| 伊卒尔 | 54.210s |
| 熔炉 | 46.386s |
| 迪亚波罗／第五幕到达 | 60.161s（修复后续接） |
| 山克 | 35.015s |
| 救囚犯 | 87.572s |
| 安雅 | 74.735s |
| 尼拉塞克 | 49.639s |
| 三古人 | 46.284s |
| 王座／巴尔／终局 | 230.167s |

耗时含各目标赶路／交谈并共享已探索地形，不含构建、停点等待或已完成跳过；逐项证据为quest-timings.json。修复后计划总583.672秒，前两项仅跳过，不把跳过计作任务速度。

正常Enter退出通关画面、返回选角，14:32:46退出0，exit.json留证；原版Host保留。终档1756字节，SHA256 `BA1117711E3C8C990F6C5488DA4438FAC63863AB825E30F60FA15AF2CAFAD63A`，既有save-info／save-json成功解码、lastRegion109；final-save-*留证。该终档未另行重入验收。

## 原版Host Act1–3复测：2026-11-04 11:14–13:58（分段18／18条件确认，导航停点已复验）

固定0E0FF20F构建，普通难度、99级女巫空任务档DF3CA9BA…F671，不带地图缓存；原版Game.exe PID268332／本机4000，seed1341169885、secondary2991359366。initial-world确认本人GUID2、私人任务48字全0。完整默认配置19D41991…B6264选择act1-3共18目标，Teleport48／250ms、只顺路开点、保留服务。证据`artifacts/ai-native-act1-3-20261104-111421/`，identity.json及原包／帧／行为记录。

第一幕6项自主完成。第二幕49区接近拉达门特（7528,8190）时，步行朝目标绕墙，传送反复回7553,8158；178.676秒内同落点106次传送，Boss仍满血，无战斗目标。stop-01／stop-02、radament-watch、radament-oscillation-audit.json留证。重提完整计划仍复现；一次正常客户端Teleport请求7528,8188后实际到7527,8188，恢复AI可击杀／完成拉达门特，并继续日蚀任务。该辅助及一次重提计划均计入干预，不算无人干预通关。用户要求先修复再继续，已在第8目标暂停，正常保存退出0，原版Host保留。

修复：teleportLanding终点仅6格微调，被目标附近密集占位／落点避险耗尽后退用旧中继；现按攻击距离扩大合法终点附近搜索（6–18格），仍验证真实碰撞、原表传送限制及拥挤。路线节点与实际可落脚点分别记忆，只有真实传送回包确认到达才消费对应中继；父／子目标可共享这次到达，已耗尽路线离开终点后从当前脚点重算，不重放旧前缀。另为同期公共接口新增的两项doorOrientation补默认空值，消除初始化警告，未改其语义。

新Release构建及复验：并发公共移动改动曾触发输入稳定性保护，后续重编译并修正上述警告；radament-fix-build-clean.log最终54步成功、零编译／链接诊断、输入稳定。固定EXE SHA256 `16881D54AC831B836EE183E61A667E31FF35AAE43CF893069682085F46E0030D`。用户正常重建原版TCP/IP房间后，native-02-radament-fixed／PID263848在12:27入局，新seed1111012732／secondary1506396018；仅复制实际辅助前自动保存0E2ABADF…DE768，第一幕已完成、拉达门特nativeRecord12未完成，不带地图缓存。AI单独执行拉达门特目标，60.908秒自主完成接近、击杀、拾取／使用技能书、交谈及卷轴获取；43次传送落点各一次，无手动辅助、管理操作或重提目标。radament-result-*、radament-fix-result.json和ai-1793766441094留证。新局种子不同，不称旧坐标逐帧复现。

同一新构建／进程继续完整act1-3的18目标计划，按私人保存状态跳过已完成任务；第二幕剩余目标自主完成并经马席夫进入第三幕，continuation-act123-start.json及act2-completed-*留证。第一幕来自修复前构建，不算同版空档单次无人干预通关。

第三幕原停点：78区寻找吉德宾祭坛时，在5190,3310持续explore-wait超过2分钟，提示无可用探索点；本地／最近权威脚点一致。仍有连通、未访问且observedTiles=0的房间612／616／620，离线原资源同种子完整地图确认祭坛252在房间612；该离线坐标仅用于诊断，未输入AI。候选房间27但候选视点／覆盖／边界均0，不记作服务端位置偏差。gidbinn-stopped-*、gidbinn-resource-map.json及gidbinn-stop-proof保留现场。先按“记录问题”范围重排独立任务，再按用户补充要求修复；重排计入干预，act3-reordered-*留证。

古书、克林姆、议会、黄金鸟均完成。取脑途中的正常房间加载使AI记住祭坛位置5192,3602，但返回仍在5176,3329报partial／no-useful-route，再次进入explore-wait；gidbinn-recheck-*留证。取消该计划后单独执行guardian，25.844秒完成击杀墨菲斯托及红门换场到103区；门出现等待初值1000ms、实际复试1次，再提交进门并观察换场，未人工走桥／点门。guardian-only-*及原包留证。此段正常保存退出0，1623字节17项进度档SHA256 `DEC50C6B404458385394F9599B63A80676F55237D1BE7A8837B82FB499BDCDBD`，用于下述续测。

吉德宾修复：房间连通性已逐格检查，但探索中继／已知目标返回仍只探中心与四分点，窄河岸存在采样盲区。原停点同种子静态资源分析：房间557的11个可站位在48格范围内，其中10个不在旧九点各8格搜索方框内，gidbinn-static-probe-analysis.json留证。ai_navigation.cpp现共用roomStandingCells逐格枚举当前加载地形；步行用可达区域筛选，不为每格另算路径；传送保留实际距离／邻房／占用／原表限制，不以房间矩形假定可落脚。修改未扩展公共头。

修复版EXE SHA256 `74684FD03155FBF982D3FE74CD5FCB59AC7F59819B47CC77D77F57F60457E155`，配置仍19D41991…B6264。两次152／154步构建因输入变化被保护拒绝，未用于运行；gidbinn-fix-build-final.log最终3步重编／链接通过、零诊断、输入稳定。native-03-gidbinn-fixed-131831／PID266032于13:49加入同一原版房间／同seed1111012732，本人GUID3，载入上述实际17项进度档。尝试恢复早期探索缓存因幕种子校验不匹配被客户端拒绝，保留原文件，实际从未恢复探索缓存状态运行；不称原缓存逐帧重现。initial-world、preparation及input-manifest留证。

修复版自主完成祭坛、击杀、拾取、交付、奥玛斯演出及后续奖励交谈，275.608秒／127次传送，同落点最多2次，无人工接管或目标重提。随后正常从城镇执行find gidbinn返回已知祭坛，45.955秒／17次传送且落点各一次，最终本地／最近权威脚点均5194,3600，验证已知目标接回。gidbinn-result-*、known-return-result-*、navigation-result-audit.json及原包留证。再提交完整act1-3，final-ai确认18项均completed，私人吉德宾槽19=13301；是分段修复后条件核对，不是单版本空档连续通关。非传送角色及其他窄地形组合仍待覆盖。

固定修复版各段行为耗时（含该目标赶路／交谈，受前置探索共享影响，不是独立基准）：拉达门特60.908s、日蚀68.297s、庇护所184.176s（含召唤者）、赫拉迪克之杖39.715s、七墓54.767s、古书54.795s、克林姆177.682s（含议会）、黄金鸟31.976s、墨菲斯托25.844s。跳过／已被前项满足的目标不计作任务速度；完整逐项见quest-timings-current.json。

13:58:22正常保存离局、退选角并退出0，native-03的exit.json留证，原版Host保留。18项终档1657字节，SHA256 `D8C75FA785EAFCAD1818322BF78C1CF7F279BE78F08ABE569FE337F5414A68C1`；既有save-info／save-json成功只读解码，lastRegion75，final-save-*留证。该18项终档未另行重入验收。

## 城镇步行／蓝门专项：2026-11-04 11:03–11:10（通过）

Windows Release d2x构建通过，编译／链接零警告、零错误，build.ps1输入稳定性检查通过。8路构建因本机可用内存不足主动停止，改2路完成剩余59步；不把中止的构建作为失败诊断或最终产物。固定EXE SHA256 `0E0FF20FDF319FC0A8481C96AD491A91F0A7A354C82E58481478F89C53C5347E`，证据`artifacts/walk-recovery-20261104-105154/`的identity.json、build-two-jobs.log及原包／快照／行为日志。

普通单机、99级女巫，沿用上一现场实际保存进度与探索缓存的独立副本，未改位置／任务位或管理旅行。先完成艾柯→奥玛斯→艾柯→凯恩→艾柯5次交谈（72.262秒），再在100区正常开蓝门，做5轮“返75城镇→艾柯交谈→步行进自有蓝门回100区”。共10次NPC会话、10次实际蓝门换场；所有城镇门端均为5158,5068，经过用户所述艾柯回门方向，未手动纠正／接管。portal-packet-audit.json列实际S2C51物件59及10次C2S13，换场另由场景确认；behavior-summary.json列各目标耗时。

连续4轮计划的第2轮，在进门请求前的步行阶段自然触发walking-recovery：35.863秒本地5129,5053、最近位置包5093,5041，退回接回点约3.044秒后继续原目标；服务端快照确认恢复移动正在执行。43.341秒提交门265，44.085秒观察100区，随后余下两轮自主完成，整组79.241秒。证明本次通用步行恢复已实际执行并可完成后续进门；不据此认定旧75→83停点的初始偏差已完全复现或根因已解决。最终正常保存退到选角并退出0，exit.json留证；原现场及源档保留。

## 当前单机复测：2026-11-04 09:14开始（第三幕换版续测进行中）

按用户要求最新Release all构建24步、零编译／链接警告和错误，构建期间输入稳定。固定EXE SHA256 `FBA91E46AD8CE2C2AB93079E5CA5C549FFFF790BAA52F68F5F44C3B4865E3A7E`；独立空任务女巫DF3CA9BA…F671，仅复制D2S、不带地图／导航缓存，入局宿主三难度任务记录均全0。PID264628，正常单机入口，09:14提交默认配置的act1-5完整27目标；activateAllWaypoints=false、useServiceRewards=false，只顺路开点、保留三项服务。首段完成前三项后停在塔入口，第二段已复验女伯爵通过，完整主线继续；不将旧版结果作为本轮验收。

证据根目录`artifacts/ai-single-campaign-20261104-090819/`，build-proof/build.log及binary-identities.json、candidate固定程序／配置；single-01-fresh下identity.json、initial-{status,world,server}.json、full-campaign-start.json、client.jsonl／host.jsonl、frames.jsonl、ai-*/events.log及真实退出码监视。本轮采用新的完整默认debug-ai.json，SHA256 `19D41991179FB4E8890F9781789C9CE0D59AFD7D9BC19A72CD26BA096CDB6264`，27任务／女巫Frozen Orb、Teleport48、250ms，未覆盖用户既有运行配置。独立debug pipe控制，不通过聊天执行命令；重点验收上方最新反馈中的出城锁定技能、通行等待／重试及静电场穿插。

本轮首停点：09:21左右goal3 forgotten-tower，在24区向25区旅行时子导航失败`Server did not open the requested interaction`。目标type5／29，客户端和最近宿主脚点均12509,9523；导航提交交互3次、接近重试2次／移动8次，目标落点12506,9521。`stop-01-tower-interaction-{ai,walk,world,server}.json`、截图及双方原包留证。不能据此认定女伯爵未出生，也不把这个停点写成图形主线程卡死。

用户要求使用当前进度换版重测女伯爵。PID264628于09:25:34正常保存退出0，实际1598字节D2S SHA256 `96DC54594BF7C083054466C85145355DF30E73E20DD8B221CFE290C282AE5E90`；原生私人槽Den／BloodRaven／Cain均12289，Tower槽5=28，未获奖励。已将这份实际存档及匹配探索缓存复制到single-02-countess-retest并保留input-proof，不改任务／人物位置。countess-rebuild-proof保留本次重新构建日志。Release all共7步、零警告／错误、输入稳定；EXE SHA256 `D13CA4138894113E86A3865E20D18C9B0D381C09A7719DFE76E78FB3CFA2E813`，配置仍19D41991…B6264。新PID241220正常读回原存档与缓存，先单独提交女伯爵目标；09:29 completed／1，实际25区，私人Tower槽5=12289（REWARDGRANTED），countess-completed-{ai,world,server}.json、截图及原包／行为留证。自主通过24→25并击杀／完成任务，无手工开入口或管理操作。删除普通进入第五层／女伯爵击杀待验；旧24区交互三次失败的精确原因尚未由重入成功证明已定位，保留原现场作为专项。随后同一进程按原五幕要求重提完整27目标，自动跳过已完成条件，full-campaign-continuation-start.json留证；不是同一进程从空档或单次计划完成。09:38已自主完成前两幕12项并进入第三幕，goal-12-running-{ai,world,server}.json留证；插杖洞口、都瑞尔击杀、泰瑞尔交谈与正常马席夫跨幕通过，未再人工恢复。

新增非阻塞路线疑点：吉德宾交付后去94区取古书，09:41–09:44从75城镇经76／77到78；此前已解锁75／78点，watch-02-kurast-exit-{ai,walk,world}.json保留解锁记忆及旅行诊断。76区规划ordinaryRoute=[76,77,78,79,80,94]、ordinaryCost=1403.84、selectedWaypoint=null；这是离城后的采样，尚不能单独解释城镇起始为何未选78点，需补对照当时可用性／成本与规划顺序。海港接近阶段耗时偏长但自行出城，77→78也已自主通过，无人工恢复；不把路线低效误写为整帧卡死或完全不动。本轮先记录，未改路线代码。

single-02完成前14项目标，在goal14的`khalim-introduction`失败。AI要求凯恩543及私人STARTED位，槽18实际为0。S2C27 seq95279的GUID3534菜单无话题，29／私人28随后到达，重开一次仍无543。原包与快照见`stop-03-khalim-npc-packets.json`及`stop-03-khalim-topic-*.json`。

同局行为日志514113ms已记录进入77区大沼泽。原版A3Q2_Callback03在此开放本局开场，Callback00按本局状态与个人STARTED决定543。旧宿主回城后只查金鸟及个人阶段，漏掉大沼泽触发。现于成功跨区后保留开场资格。旧AI还把543–548当作合成、装备的前置，现按实际持物推进。待分别验收未做金鸟时经77回城可见543，以及凯恩无话题时AI仍能取材、合成、装备、破珠并获得私人完成位。旧PID241220已按用户新要求于10:04:15正常保存退出0。实际1678字节D2S SHA256 `2E9D45F0B1CA6690C3529A81CD27263FF26DD2060030C021A2FAD8967A3519B1`及匹配探索缓存已复制至single-03-act3-retest/input-proof与独立saves；吉德宾／古书已完成，Khalim私人槽18仍0，未改进度。act3-rebuild-proof保留本次构建及输入校验：八路编译因内存压力主动中止；两路继续114步编译／链接零诊断，但1727项SHA256核对发现client_missile_view.cpp期间变化，未运行该产物。已刷新该文件时间并以build-final-03.log重新编译／链接3步，零警告／错误、脚本稳定性校验通过。新固定EXE SHA256 `D28FE87B2BDB7C6DEE544E3DBA644B12199341418159205EA680574DB4FB63F5`，配置仍19D41991…B6264。single-03-act3-retest／PID266996正常加载原保存进度，已提交act3六目标，只顺路开点／保留服务，initial-{world,server}.json、act3-start.json、双方原包／帧／行为记录及真实退出监视留证。未改任务位、授物或手工绕过。第三幕新运行结果待更新。

交谈策略待验。克林姆日志状态7才指名凯恩，四件齐全并已听连枷说明后应切到状态5提示合成。第二幕法杖状态1／4分别指示出示卷轴／遗物。复测记录日志阶段、同一NPC的0x8A、27菜单和关窗后私人更新。覆盖迟到提示、同一NPC连续两条不同台词、重复相同台词及无提示继续取材。交物／领取奖励另核实物品和私人位。

两段截至停点的帧统计见frame-summary-through-khalim-stop.json，无≥3秒完整帧；第二段截至汇总1492条采样、最高1495.266ms（1区加载）。这是任务话题失败停点，不据此关闭历史长帧／堆损坏缺陷。



## 最新反馈：出城技能与第四幕红门（2026-11-04，源码修复待验）

凯恩蓝门核对（既有日志）：正常189是不可选演出门，当前MPQ OperateFn0／Selectable全0，现有两端代码已正确区分；更正第一幕文档误写59。090819轮single-01救笼后547ms已指派真实cain1，645ms玩家选择220并施放，955ms指派自己的59／608，1614ms点击608回城；城镇端为59／609，4493,4657。全段仅记录一条189指派679于Tristram的25140,5142，没有城镇189可见指派，不支持两扇可见城镇门重叠或玩家进入189。证据single-01-fresh/cain-portal-audit.json；没有依据调整门户位置。未新增构建／运行，正常189／异常出生失败后备59及全部加载顺序仍需运行组合对照。

控制解除（源码修复待验）：AI控制期间，鼠标、施法／移动、技能／装备切换、饮药、其他按键及普通调试操作保留功能，不自动取消计划；仅游戏主画面的Esc解除普通输入控制，聊天／Q面板／对话／菜单内的Esc只处理当前界面。实体输入、无场景回退和ui-input共用处理，Esc在AI决策前停止计划，原面板／菜单功能继续。显式debug-ai-pause／cancel及任务生命周期收尾保留，独立自动行走的接管规则不变。待验非Esc输入后计划仍在、观察面板关闭后继续、Q／聊天／对话内Esc正常关闭界面且保留计划，回主画面再按Esc解除、排队ui-input按Esc实际帧解除、独立行走仍可手动接管。未构建／运行。

- 冰封球只用一次便换备用：81412075原版Host行为日志仍有`combat-strategy-next: normal character projection reports unusable skill`，此前仅豁免识别到的冷却不等于问题已解决。现出城前按主／备用顺序，仅凭有效等级、minLevel、主动／手别资格选定一个攻击技能；整次出城不因冷却、临时状态、缺蓝、换目标／野外区域或无伤害改用备用，回城再选。冷却等待不计局部无进展时间；主／备用锁定不限制特殊技能；静电场恢复为首领战主攻冷却期间穿插，传送／冰甲／光环仍独立。日志`combat-excursion-skill`及状态`departureSkill`记录选择。
- 第四幕红门：`native-01-fresh/client.jsonl`中7068门首次C2S13为seq110741；5.019秒后选冰甲、5.143秒施放；15.179秒后第二次C2S13（手动接管），再过约40毫秒收到6105。原来的出现等待使用events预算，但点击后另走固定30秒，提前恢复依赖新的本人位置纠正，且未保护移动／技能输入。现等待出现与点击换场共用`events.waitSeconds`（默认90秒）；通行初等结束后每1秒通过共用恢复入口重新接近／交互，不要求新位置纠正包，门户退订时重新发现真实指派，不续期；先推进通行，再利用停稳空档攻击，不补冰甲或以追怪替换接近。新增泰瑞尔X+5见证点沿D2MOO A4Q2 Callback11，门未指派时不全城乱找。现有原包不能证明首次未换场仅由开门延迟造成。
- 下方NPC：实际为贾梅拉，泰瑞尔20000之后触发自动补给，日志有`supply-detour-started`、`town-healing-submitted`及`tsc +1`；不是凯恩鉴定或额外任务交付。旧日志未区分生命未满、药不足或回城书不足的触发分支，不能把补买1张卷轴等同于启动原因；已补heal／hp／mp／tp原因字段。现城镇→城镇任务门户先完成跨幕，补给留到下一次出城。泰瑞尔20000本身是原版生成566门的必要交谈，保留。

通行等待已统一为PassageWait：普通门初等250ms、蓝门创建／普通换场1000ms、插杖入口15000ms；之后每1秒复试，实际结果提前到达立即推进，普通等待总上限90秒、插杖入口仍30秒。卷轴复试只查真实门户／继续进门，未确认的消费不重复。代码与config/debug-ai.json示例已更新，未改服务端规则。入口：`ai_combat.cpp／ai.cpp`选技能，`ai_events.cpp／ai_quests.cpp／ai_execution.cpp`通用门户预算／重试／动作保护，`ai_act_four.cpp`出生见证，`ai_supplies.cpp`跨城补给顺序。本轮FBA91E46／D13CA413已构建并运行前两幕及第三幕部分流程，普通冷却后继续使用冰封球、蓝门／换层与插杖入口等待已覆盖；删除这些普通待验。剩余：长冷却不降级、主技能等级不足选备用后整次出城固定、回城重新选；103→109延迟开门／首点无结果可自主重试，期间不补甲／绕访贾梅拉；真实换场不再点击旧门，到期明确失败；普通门、蓝门创建／返城／补给回程、点击式入口、步行边界复试一致；长演出可防御、入口接近不被攻击取消、计时不续期；普通补给和王座必须清怪的波次不退化。passage-wait／passage-retry及status.passageWaits留证。

通行接近回归（源码修复待验）：用户观察野外远处洞口改为长距离步行。servicePassage提前返回漏掉主循环mobility尾部，且等待防御遗留combatTarget可能继续阻止职业移动；现在通行分支复用既有mobility，并在接近时挂起战斗记忆。待验洞口／楼梯／任务红门／已有蓝门远距离优先传送，跨幕前无多余步行；近距离正常点击、城镇步行、传送不可用后备及已提交换场保护保持正常。未构建／运行。

通用步行卡住／城镇进门停住（海港专项通过，其余待验）：single-03-act3-retest固定D28FE87B构建在75→83自有蓝门2/1663处失败，goal-5-failed-{ai,world,server}.json及ai-1793759017674/events.log留证。画面脚点5158,5068，客户端最近位置包及服务端快照均5117,5048、服务端无移动路径；PassageWait已有52次复试，但未产生有效重新走位。通行复试仅在本地未到位时开启forceApproach、resyncApproach只允许NPC，普通导航又以本地走动刷新无进展计时。现恢复下沉auto_walk：新的本人校验确认所报终点已停、脚点持续无进展且明显分歧时，普通赶路／探索／步行跨区也可在交互前恢复；无宿主进展窗口可跨子目标保留，连续交互失败共用此入口。保留原目标，以碰撞校验短段退回拐角附近再走，优先双方可达指针，父任务／战斗／职业位移不抢占；原纠正与饮药仍处理，不续总期限，单子导航最多两轮。status的movementRecoveries／recoveryAnchor及walking-recovery阶段、interaction-approach-retry留证。

本页上方海港专项已覆盖NPC往返、自有蓝门及一次进门前行走恢复。剩余：野外／地下拐角、探索／步行跨区／传送点／红门、连续子目标保留停步观察、正常回包滞后与真实纠正的组合。最初位置偏差的生成原因尚未由此证据确定；本项不认证客户端／宿主移动已全面复刻。

静电场修正（同日，待本次构建）：保留staticField开关及原首领／当前安全射程策略，先用一次主攻，冷却期间穿插静电场，主攻就绪优先于静电场和可选绕圈；静电场不修改主攻计时／备用选择、不再限制自动三次。停止比例只取原DifficultyLevels.StaticFieldMin（经典版为0），不加任意百分比门槛。当前客户端没有怪物可靠绝对HP，绝对血量停用条件明确暂缓，诊断标为未知，不用基础表生命或宿主数据替代。待验冰封球→静电场→冰封球顺序、切技不缩短／续期主攻冷却、原难度下限、未知HP和射程外不强行贴近。未运行测试。

## 上一轮原版Host复测：2026-11-04 06:30–07:08（27/27及保存重入通过）

从独立空任务档开始Act1–5完整27任务，`activateAllWaypoints=false`，只顺路开点；`useServiceRewards=false`，保留三项物品服务。遇停点先保留现场并正常重试，仍失败则暂停AI诊断修复；未通过的条目保留，实际验收通过后删除对应旧待验条件。

| 条件 | 实际值与证据 |
| --- | --- |
| 证据根目录 | `artifacts/ai-native-campaign-20261104-062931/`；`native-01-fresh`保存原包、帧记录、AI行为、隔离角色及真实进程退出结果 |
| 程序／配置 | 固定EXE SHA256 `814120756FE5E7D72F3D3468F3D1C6965F1C86757D6D5D5098257EDC2CB32682`，EXE旁配置`6D941EA2…C0C3`；Release d2x增量构建无工作、无编译／链接诊断，不记作本轮重新编译；原稳定构建依据仍为上轮critical-npc-build-proof |
| Host／角色 | 原版Game.exe PID264668，本机4000，普通难度seed2041311693／secondary2263795417，本人GUID2；客户端PID264968。99级女巫空档`DF3CA9BA…F671`，仅复制D2S、不带地图／导航缓存；initial-status.json确认私人／本局任务字全0 |
| 输入与边界 | 独立管道`d2x-native-campaign`，完整27任务，Frozen Orb／Teleport48／250ms；仅正常客户端操作，不用聊天调试、管理旅行、授物或改任务位 |
| 任务／终局 | `final-debug-ai-status.json`为completed／27，私人任务固定段核对27条件；S2C6107为seq150950，`final-screen.png`实际显示Slayer。正常Enter返回Characters后quit，07:06:06真实退出0 |
| 首次终档 | 1747字节，SHA256 `5709895BCB04910C673598FF221DB7197EE72DC41541A1D90646846DF758A577`，原生校验3454942130；现有d2x_assets的save-info／save-json均成功解码，27条件／29点，三项服务未消费。证据`final-save-native-audit.json`及`final-save-export.json` |
| 重入／再次保存 | `native-02-completed-rejoin`只复制上述D2S，用同EXE／同Host新进程重入，27目标全跳过；正常找传送点收到实际63，确认29点。正常离局回Characters，07:08:15退出0；再次保存1747字节，SHA256 `362B268247B6997BD3634288EA8E8189060973674C93A5B57F3010E4A1CF9383`、原生校验200014980，现有工具解码通过，仍27条件／29点 |

同一固定构建、同一通关进程完成五幕。首计划在迪亚波罗击杀后、103→109门户结果等待阶段以`Manual input took control`取消（行为时间1558013ms）；随后实际快照确认109区、S2C61程序5已收到。`stop-01-manual-input-*`保留现场；同进程重新提交完整27任务（`recovery-01-full-plan-start.json`、`ai-1793746614112`），按实际私人条件跳过前21项，继续第五幕直至完成。**有一次手动输入接管后的计划恢复，不算单次计划无人干预通关**；未发生重试仍失败的任务阻塞，本轮未改代码，未提交git。原版Host保留，两测试客户端均已退出。

本轮复验通过：阿卡拉译卷／五石柱、德罗南日蚀交谈、皇宫／召唤者日志红门、插杖后等待16.138秒、都瑞尔／泰瑞尔、奥玛斯交付后等待5.113秒再继续、取眼、墨菲斯托红门、熔炉、五封印／迪亚波罗、安雅解冻／交付及尼拉塞克、三古人／五波／巴尔／终局保存。群魔堡垒103→104曾暂报已知连接不可达，AI自行恢复并完成后续旅行，未重提计划；`watch-01-act4-exit-*`及首行为日志留证。只关闭实际覆盖的普通条件，旧指定种子、退订及异常恢复边界保留。

非阻塞资源诊断仍待核对：首进程stderr有`data/global/monsters/0a/tr/0atrmeddd1hs.dcc`缺失6次、`0atrhvydd1hs.dcc`缺失3次，另有`Original audio unavailable`。`stderr-diagnostic-summary.json`保存逐路径计数；未确定是当前MPQ资源缺口还是组件／路径选择问题，不据通关判为资源完整。

## 冰封球施法间隔与技能变红（源码修复待验）

用户观察同一D2X客户端由AI控制时，单机冰封球明显快于连接原版Host。当前MPQ delay=25逻辑帧，D2MOO确认独立于FCR的共享skilldelay状态。旧AI已有独立状态检查：原版Host发状态便等待，D2X宿主只有截止帧、未同步状态，AI无法据此等待。AI本地nextCast从提交请求起算1秒，且换目标会重置，不能替代从成功释放起算的权威冷却；因此可以出现单机更频繁请求／本地预测。此前“AI未判断状态”的表述不成立，缺失的是D2X宿主发布及公共客户端入口／图标判断。尚未逐帧确认观察到的频率差值，也不能断言服务端实际伤害频率超限。现补状态发布／定时移除及公共客户端施法资格、图标红色，参数使用表公式，未改AI固定间隔。实现及原版证据见[技能公共规范](../gameplay/skills/COMMON.md#原版施法延迟)。

已纳入下述522B1248稳定构建，运行专项待验：普通操作和AI连续冰封球都遵守25帧延迟；高FCR不缩短该延迟；冷却中其他有delay技能也变红／不可施放，传送不被此状态阻止；移动、切技能、回避后截止帧不被清空；到期恢复，实际飞弹／扣蓝与画面对应。此前固定EXE测试结果不认证本项。

## 上一轮单机分段复测：2026-11-03 20:16至11-04 00:54（27/27及保存重入通过）

从空任务档开始的Act1–5分段续测已完成。最后固定修复版81412075自主完成安雅剩余资格、三古人、王座五波、巴尔及终局红门，实际收到S2C 61/07，显示Victory／Slayer ActOneSorceress。正常Enter返回选角并保存退出0；同版读回终档，完整27目标均正确跳过。这里是十段修复后续测加一次终档重入，**不是单版本／单进程从空档无人干预通关**，也不认证原版Host、其他职业／难度及39点全开。

| 条件 | 实际值与证据 |
| --- | --- |
| 证据根目录 | `artifacts/ai-single-campaign-20261103-201541/`；每段single-*保留identity.json、client.jsonl、host.jsonl、frames.jsonl、ai-*/events.log、正常保存与真实process-exit.json |
| 角色／房间 | 99级女巫空任务DF3CA9BA…F671，首段私人任务字全0；D2X嵌入单机、普通难度，seed557488535／secondary3628852854，本人GUID1。换版仅复制实际正常保存的D2S与同种子缓存，未改任务位、授物、管理传送或手工杀怪 |
| 策略／配置 | 完整27任务，activateAllWaypoints=false、useServiceRewards=false，仅顺路开点，保留灌注／镶孔／个性化。EXE旁配置SHA256 `6D941EA2809F020FD8685695DA71E4C2AAA0E4424664C191A7C1FCA7A30FC0C3`，Frozen Orb／Teleport48／250ms；十一段客户端原包C2S 0x15聊天均0 |
| 最终程序／构建 | 固定EXE SHA256 `814120756FE5E7D72F3D3468F3D1C6965F1C86757D6D5D5098257EDC2CB32682`。critical-npc-build-proof/{build.log,binary-identities.json}：Release all80步、零警告／错误，构建输入稳定。此前各段使用各自固定产物，不由终版倒推认证旧段 |
| 任务／终局 | single-10-critical-npc-resume/final-{ai,world,status,server}.json及final-quest-audit.json确认27条件；S2C6107为client seq23205，final-screen.png为实际Slayer画面；Victory Enter后回Characters，PID231588于00:41:07正常退出0 |
| 首次终档 | single-10的final-save-native-audit.json：1753字节，SHA256 `BC6074816E5C16D6E3B14A24022AEC22E1A2C7ABC72009475D6EB6074CA03697`，原生校验4183347751一致，27条件／27点。三项服务均未消费，不将资格已取得误判为尚需使用 |
| 重入／再次保存 | single-11-completed-save-rejoin读取上述实际终档，正常选择Normal；completed-rejoin-{ai,world}.json确认27目标全跳过。后续安雅专项完成，PID262204于00:54:15退出0；saved-rejoin-native-audit.json再次核对27条件／27点、校验3646319270一致，1753字节SHA256 `4274D576C38C816580649C3D081A7D7445AEEBE42D5F78A31C8B97F5CE6D5453` |

已验收的普通停点从待验清单删除，保留以下结果和证据索引；原现场、失败构建及旧包仍在artifacts，不重复堆叠过程记录：

| 分段／固定EXE前缀 | 问题、已通过结果与证据 |
| --- | --- |
| single-01-fresh／0B8CB8EE | 邪恶洞窟、血乌、译卷／五石／凯恩通过。女伯爵普通身份→移除重指派后RemoteCombat沿用旧外观缓存；stop-01-countess-*留证 |
| single-02-countess-resume／1E97AA57 | 外观修订改用世界收包修订后，自主击杀女伯爵、锤子交付保留灌注、安达利尔／罗达门特／日蚀通过。Countess普通出生后再升级与原版同步出生的时序差异、原缓存内部值未采集仍是边界 |
| single-03-arcane-resume／CE7D4177 | 已有物件模式改走0E，保留51首次／真实重订阅；54→74皇宫入口、召唤者、法杖通过。stop-02-arcane-*、arcane-build-proof及逐目标快照留证 |
| single-04-duriel-resume／7468754C | 按原Operate43取消虚设ENDANIM门槛，正常进入73并击杀都瑞尔；Tyrael后续回调丢失停点留stop-04-*。普通入口已通过，不代替多人及全部站位验收 |
| single-05-tyrael-resume／D62FFAA4 | 保留NPC回调继续点，完成第二幕跨幕及金鸟；蓝门本人坐标分歧停点留证，不宣称全部首次失步根因已定位 |
| single-06-movement-resume／2163E6BF | 原移动纠正路径与AI恢复调整后吉德宾通过；77→78河岸局部传送循环保留stop-06-*与地图／导航证据 |
| single-07-detour-resume／522B1248 | 部分落点不冒充完整可达路线，77→78河岸绕行及古书通过。蜘蛛洞qey拾取被潜地沙虫危险分类／出土占位及冷却识别阻塞；stop-07-spider-chest-*、spider-chest-analysis.json留证 |
| single-08-spider-resume／25DD365D | 沙虫合法出土、暂不可选危险分类和公共冷却资格调整后，正常开箱取眼、脑／心／连枷、合成／破珠／墨菲斯托、衣卒尔／熔炉通过；eye-acquired-world.json、act3-completed-world.json。用户要求地图导出时曾暂停并重提计划，无手工任务推进，analysis-08-map-export-*拆分客户端探索、宿主已准备地图和AI诊断，不向AI灌入隐藏地图 |
| single-09-seal-resume／13D375C8 | 下述封印范围修复后，五封印、三首领、迪亚波罗及跨第五幕通过；act4-completed-{world,server}.json确认deathMask7及diabloSlain。随后山克、15囚犯、安雅、尼拉塞克击杀通过，返城交付资格时安雅丢失停点留stop-09-anya-entitlement-* |
| single-10-critical-npc-resume／81412075 | 下述关键NPC保留修复后，安雅个性化资格、三古人、五波／巴尔、终局27/27及正常保存通过。anya-entitlement-world.json及anya-retention-after-departure-server.json记录本人285正常进入109区room12休眠，dead=false |
| single-11-completed-save-rejoin／81412075 | 终档全27条件跳过；正常找安雅→出城110→回城交谈完成4/4。再用正常旅行到Act1城镇1，anya-cross-act-away-server.json确认同一安雅233休眠；正常返回109并交谈完成2/2，anya-cross-act-return-final-{ai,world,server}.json确认恢复的是同一233。未强制生成NPC、消费服务或改旗标 |

### 本会话直接修复并验收的两个阻塞

**封印394无法开启**：旧25DD365D两次正常计划均等quest-object-result超时，GUID2991／class394始终mode0；用户手动点击也无效，manual-seal-result-*留证，不能归因只在AI。封印7773,5155，Lord De Seis偏移-39/+33后的基点7734,5188，在同seed／MPQ离线静态图周围14格没有合法3×3中心。旧D2X误将D2MOO调用参数13／14／15当有效搜索半径；该公共函数实际忽略参数并用100。本机1.13c D2Game.dll MD5 978f6f74cd031bce6ecee602cd5988cc，RVA BC050助手在BC0A2压入100，BC510偏移仍为-39/+33。仅修正三种封印的搜索范围100，保留size3、0x3F11、偏移和实际房间／占位验证。证据single-08-spider-resume/seal394-map下original-113c-seal-helper.txt、original-113c-seal-evidence.json、static-marker-analysis.json及完整离线地图；离线图不代替动态现场。seal-placement-build-proof构建4步零错误／警告；single-09实际开封印／生成三首领／迪亚波罗通过，新版无人工开门。删除普通封印待验，保留动态占位、输出背压及异常房间时序边界。

**关键安雅离城后丢失**：旧single-09的anyaSpawned／anyaThawed已true，但109区无活动安雅，休眠怪物也为0，个人槽38=8194，未交付个性化资格。当前MPQ drehya的MonStats2.restore=1、109区SaveMonsters=0；spawnStoryNpc原critical未传到admitNpc，Actor缺原ISRESURRECT对应资格，退役被丢弃且任务出生位阻止重生。按D2MOO QUESTS_SpawnCriticalMonster(bSpecialInit)的ISRESURRECT／ISINIT分支，把已有critical经world.admitQuestNpc的默认false specialInit传至现有admitNpc保留资格，复用正常休眠／恢复；不按NPC名强制补生，不扩大非critical消费者。MPQ摘录与stop-09-*留证。single-10交付未用资格、离城休眠，single-11同局跨幕退役／同一实体返回交谈均通过，删除普通出生／退役／恢复待验；容量和提交交错仍未穷举。

## 历史原版Host复测：2026-11-03 17:44，88EBF1E8（当轮用户暂停）

按用户要求重新构建、空档从Act1跑至Act5；最初要求开齐39点，18:07用户改为只顺路开启、不再强制补点。本会话只记录和正常恢复，不改代码、不提交git。Windows Release all构建成功，4步、零警告／错误，1707个源码／配置／脚本摘要在构建前后无变化；d2x目标已有产物无须重编，实际4步为资产工具链接、两项服务端编译与链接，不把它写成全量重编译。完整日志与摘要见本轮build.log、build-source-stability.json。

| 条件 | 实际值 |
| --- | --- |
| 程序／配置 | 固定EXE `88EBF1E8F40C4365E62089B55D8E8915DD05548BFDA8DD6BD4F82FAB3327AFFA`；沿用EXE目录既有配置6D941EA2…C0C3（Frozen Orb、Teleport48、250ms决策）；未覆盖用户配置 |
| 房间／角色 | 本机4000已实际TCP连接成功；原版Game.exe PID11992；普通难度seed258554026／secondary2998408510，本人GUID2；独立空档DF3CA9BA…F671，只复制D2S、不带地图／导航缓存；初始私人／本局任务字均0 |
| 计划 | 17:44:19启动完整66目标，activateAllWaypoints=true；18:07:33按用户新要求取消补点，重新提交仅27任务、activateAllWaypoints=false，requireBeforeDeparture=false已核实。普通activateWaypoint仍由tick调用，保留顺路开点；useServiceRewards=false，保留灌注／镶孔／个性化；仅debug pipe提交正常客户端操作，不使用聊天发调试命令 |
| 暂停结果 | 18:23:07.721按用户要求debug-ai-pause，state=paused、active=false；实际area131，双方15101,5022一致。27任务计划完成26项，私人任务字核对26项满足，三项服务保留；毁灭前夕停在throne-waves-and-baal-departure，未击杀巴尔／未收到6107。AI解锁记忆35点，不再以39点为门槛；未验证终局保存或重入，不能写成全链路完成 |
| 证据 | `artifacts/ai-campaign-retest-20261103-174256/`；candidate固定程序及配置、source-identities.json、host-connect-probe.json；native-01-fresh的identity.json、initial-{world,status}.json、full-campaign-start.json、client.jsonl、frames.jsonl、stdout.log、stderr.log、ai-1793699059755/events.log；有真实退出码监视 |

重点复验：已见任务红门的模式与退订恢复、世界事件等待、凯恩私人状态刷新、奥玛斯交付祭坛演出及领奖、鲁高因活板门、普通蓝门旅行、终局保存／退出。当前构建包含的修复不能由上一轮4FFE结果认证，以下旧问题在实际通过后才清理。

阶段结果：第一、第二幕12任务及18点已完成，17:55进入第三幕；这一阶段单进程／单次完整计划，无人工恢复。之后的补点与熔炉停点及干预见下文；前四幕及第五幕前五任务均完成，王座阶段由用户暂停。石柱、日志门户及插杖入口实际通过；插杖入口world-wait-started为588115ms，world-result-observed为604338ms，等待16.223秒后进入都瑞尔层。相关原包及行为日志持续保存，不把逻辑等待计作完整帧卡顿。

本轮已覆盖：奥玛斯交付后留在实际祭坛附近，772405→781396ms等待8.991秒后才继续戒指话题；艾席拉自主找到。墨菲斯托红门class342／GUID6243，seq105569指派mode0、seq105843更新mode1，无额外mode2；AI等待1.210秒后认为可选并实际到103，没有人工跨幕，普通静默ENDANIM场景通过。安雅标记等待、解冻往返蓝门及领奖通过；Malah正常治疗后购买tsc+5，415020ms接回任务。普通私人状态查询沿公共入口运行，不能据此关闭并行／乱序组合。

截至暂停共2447条帧记录，JSON错误0，无≥3秒完整帧；最长1750.09ms／area1（client956.57ms、scene792.15ms），私有提交采样峰1417.31MiB、工作集859.28MiB。frame-summary-through-user-pause.json按暂停epoch截断；不关闭历史长帧或堆损坏根因问题。原包审计C2S15聊天0、6107为0，paused-progress-audit.json与paused-protocol-summary.json留证。user-pause-command.json、paused-{ai,walk,world,status}.json、user-paused.png保存现场；客户端与原版Host留在游戏，Host不会随AI暂停，暂停后的世界变化不计作测试推进。本会话未改代码，未提交git。开跑时1707项源码／配置／脚本摘要中，暂停审计有18项变化，来自其他会话；固定candidate仍为88EBF1E8，本轮不认证测试过程中后来修改的源码，详见source-stability-at-pause.json。

本轮路线停点：goal37补大沼泽77传送点时，在76区约5170–5290,4090–4280反复传送／回走。前三分钟238次frontier-attempt、22次walk-failed、22次backtrack-no-discovery；AI已自行回城并经76传送点换起点，仍回到这一带。诊断间歇为no-useful-reachable-frontier（0候选落点），visited房间仍缓慢增加，不能写成完全不动；未到77、点数停在25。watch-01-great-marsh-*、stop-01-great-marsh-*、行为日志与截图保留现场，种子258554026。随后取消主计划，仅安排正常travel78→77，尝试从剥皮丛林侧寻找；首次辅助请求带activateAllWaypoints，被正常展开为41目标，随后取消改为两目标（recovery-02-*）。辅助已到78，但再去77仍回到76；用户随后要求停止强制补点，18:07:33取消辅助并恢复27任务主线，未取得77点。recovery-01/02/03-*记录干预，不改代码、不解锁传送点。本轮不算无人干预通关，后续不以39点为完成门槛。

## 第三幕往返与导航路线复用（普通河岸续测通过，剩余地形待验）

174256轮stop-01快照：当前76已解锁，目标为去77补点，并非重找76的传送点。search为connection，回溯目标房间76/5280/4280，后续approach却改追中心5300,4300，实际落点5218,4140。辅助计划ai-1793700375381也出现同类回溯；两份日志均见5218,4140→5213,4146附近反复选点。此处证据指向AI中转衔接，不据旧Host坐标断言客户端失步。

此前中转修复保留图搜索选中的坐标，不改追远处房间中心；抵达已知中转点不要求新增开图。该修复已纳入79B727A0，运行结论由上方复测维护。

以下算法已纳入本轮522B1248及后续固定产物；77→78普通河岸绕行已实际通过，不据此宣称全部地形或性能收益已验证：

- 传送按目的地保留完整多跳路线和已走进度，接回可达的后续点；不再每跳重算第一跳。后台A*优先减少施法次数，再比较路程；合法绕路可暂时远离目标。
- 已加载地形分块检查，混合块细分到子格，保留窄岸／小平台的落点；复用未变房间样本及同一图内的跳跃连接。高层房间边改查实际落脚单元，不以九个样本否定连通，也不把房间整体当作可穿越空间。
- 步行保留按原生移动边验证的连通区域，区域内查询复用；传送不用步行连通条件。怪物只影响当前落点／输入，不进入长期地形图。普通房间更新不直接丢弃传送后续路线，失效段、地图坐标域或游戏／区域切换按各自边界更新。

已通过本轮seed557488535的77→78河岸绕行及后续跨幕旅行；旧原版Host seed258554026的76→77及78侧回访、全部窄岸／房间加载释放组合、战斗位移后续接与性能对照仍待专项。不能用另一个种子的普通通过关闭旧现场，也不能宣称全局最优。

## 熔炉阶段回退与缺话题收尾（本轮普通熔炉通过，异常恢复待验）

174256轮ai-1793700453810：121557ms选择soulstone-placed，122065ms退回soulstone-owned，随后回城找凯恩679并因缺话题失败。stop-02-forge-topic-*与hellforge-object-mode-packets.json保留证据；不是鉴定请求。D2MOO A4Q3 Operate49在0→1时立即消耗mss，ENDANIM才变2；旧条件仅认开启，放置动画期间材料消失便触发错误补领。

熔炉放置条件现认可真实非中立阶段，并保留已观察到的放置事实，供材料前置条件复用；退订／离区不因物品已消耗而重新索取。敲击仍等待原ENDANIM开启，不以放置等同砸碎或领奖。缺话题通用恢复先关闭交谈，经客户端刷新私人任务快照，重评条件后至多重开一次；期限不无限续期。失败收尾只关闭本局AI当前单位动作所打开的NPC会话，记录failed-dialogue-closed或拒绝原因。正常移动原已通过close_interaction关闭菜单，此次停点未提交移动，不归为该客户端链路退化。

single-08已完成D2X单机普通流程；当前81412075原版Host亦自主完成。`native-01-fresh/hellforge-object-mode-packets.json`确认class376／GUID6719，seq104439为mode1，之后seq104515为mode3、104535起为mode4，没有额外mode2。首行为日志1439378ms选择soulstone-placed、1441576ms选择soulstone-smashed，未退回soulstone-owned；普通静默ENDANIM条件从待验删除。仍待：短暂退订／回城续办、真正丢失未放置灵魂石的补领；缺话题刷新后条件改变续行，无变化／无回包有界失败收菜单，以及手动接管。旧88EBF1E8的辅助恢复不算新版自主验收，详情仅保留于该轮artifacts。

## 上一轮复测：2026-11-03 15:10，4FFE14D0

用户已完成构建，本轮直接固定已有程序，不重复构建或修改代码。原版Host重开房间，使用独立空任务女巫，只走debug pipe正常客户端输入。

| 条件 | 实际值 |
| --- | --- |
| 程序／配置 | EXE `4FFE14D0007DB965263C5799627A17C865FBA8787AD09D6457BCE6F1D11D3F9A`；沿用EXE目录既有女巫Frozen Orb／Teleport48配置，摘要6D941EA2…C0C3。源码示例06B3A637…7501另存留证，未覆盖用户EXE配置 |
| 房间／角色 | 本机4000／原版Game.exe PID23228；普通难度，seed489357658／secondary2794395303，本人GUID2；空档DF3CA9BA…F671；初始私人／本局任务字均0 |
| 计划与结果 | 15:10:18启动27任务＋39点共66目标，activateAllWaypoints=true、useServiceRewards=false；15:54:51最终completed／66，实际6107、Victory／Slayer及进入红门均确认。24项奖励已领取，灌注／镶孔／个性化三项服务保留；39点均保存，34个非城镇点首次离区前激活。墨菲斯托后用户协助跨幕，另有101→102辅助旅行及第四幕蓝门正常移动／交互、计划重提，**不算无人干预通关** |
| 保存与重入 | native-01最终D2S为1703字节、98D05E41…9943，原生校验正确；Victory Enter实际返回选角，再quit退出码0。native-02只复制此D2S，不带导航／地图缓存，同EXE／同Host重入，完整66目标26.552秒内正确跳过；原版63确认39点，再次保存1731字节、ABD8E01C…2585，仍27条件／3保留服务／39点，退出码0 |
| 证据 | `artifacts/ai-campaign-retest-20261103-150955/`；source-identities.json、post-run-source-stability.json、plan-outcomes.json、frame-summaries.json、protocol-save-and-victory-audit.json、waypoint-departure-audit.json；各native-*的identity.json、client.jsonl、frames.jsonl、quest-packets.json、ai-*/events.log、final-*.json、saved-*-audit.json及process-exit.json；native-01另有victory.png、stop-*／recovery-* |

版本边界：开跑时记录的1704个源码／配置摘要中，结束审计有16项变化，包含AI事件等待、红门表现与绘制模块，来自其他会话；固定candidate程序摘要始终未变。本轮不认证这些未编入固定程序的改动。首计划至最终完成墙钟44分32.547秒，含人工观察与恢复，不作纯AI速度基准；两进程C2S15聊天均0。未操作原版Host退出，两个测试客户端均已退出。

运行提示另记：stderr共59种`Original audio unavailable`路径，例如`data/local/sfx/act1\charsi\cha_act1_q3_init.wav`、`cha_act1_q5_successful.wav`、`cha_act1_q3_successful.wav`。尚未核定是当前挂载资源、语言或路径问题，未修改资源／代码；完整原文留stderr.log，去重列表见unavailable-audio-warnings.json，不把提示当作任务阻塞。

本轮已实见：初始Akara采购完成；Drognan处两次补书（+10／+2页）及Ormus治疗后补书（+2页）均正常接回。艾席拉从iron-wolf-reward选择至goal-completed为23.442秒，主题589、脚点5036,5089，无重启／辅助行走；未复现旧两点往返，不作同种子速度比较。安雅标记class460于seq128860指派10181,6556，1.189秒后本体class558于seq128950指派；AI实际接近、等待、解冻并领奖，无人工恢复，证据frozen-anya-marker-body-packets.json及ai-1793691838710/events.log的319020／319363ms。瓦特之厅本轮壁画方向0,1（+Y）已通过；第五幕至实际通关未再人工恢复。

终局原包：6107为seq152800；Enter后C2S69 seq152934，B3 seq152935–152941共1703字节，随后B0／05／06。native-01最后一条63仍是35点的旧菜单快照，不代表最终未开齐；native-02只带D2S重入后的63实际为39点，与两次D2S核对一致。客户端普通返回选角、退出与重入已实际通过，但不据一次正常退出关闭旧堆损坏根因问题。

本轮首个停点：guardian的act-four-arrival在102区探索耗尽，15:29至15:34仍等待“actual assigned quest portal”；双方17702,8077一致，个人guardian字10353。停点当前世界只见装饰物／箱子等、没有门户342；但原包seq93387／93725确实曾指派class342、GUID6032于17602,8069，模式0→1，不能写成服务端从未开门。已观察位置退订后是否仍参与导航尚未定位，不按任务字已奖励推断跨幕已完成。native-01-fresh/stop-01-mephisto-portal-{ai,walk,world,status}.json及截图、mephisto-portal-assignment-packets.json保留现场。15:34:54取消主计划，正常travel101→102的行为日志耗时2.335秒，15:35:55检查时已完成，仍无当前门户342；随后用户接管并报告已到Act4。15:41:04确认实际scene.area=103，重新提交完整66目标，正常跳过旧条件并接到Izual；recovery-01-*、user-recovery-act4-*及recovery-02-after-user-act4-full-campaign-start.json留证。用户具体操作序列未额外推断，不修改代码。

本轮第二个停点：第四幕衣卒尔奖励完成后，熔炉条件hellforge-hammer-source经本人蓝门6327（class59，目标105，城镇脚点5048,5043）旅行，15秒内未见目的地区域，15:42:12计划failed（Submitted travel's destination was not observed）。提交时本地5044,5041、最近Host5022,5029；停点仍相差约25格，不把旧采样直接视为当前权威位置。stop-02-act4-travel-*含原包、快照与截图。随后用正常online-move到5044,5041并交互当前真实蓝门6327，最近Host更新至5045,5041；recovery-03-*记录命令与完整计划重启；之后实际进入105→106→107，熔炉／封印／Diablo及第四幕奖励均完成，15:46进入第五幕，act4-completed-ai.json留证。该恢复使用正常输入，未改代码、未管理旅行。

## 上一轮复测：2026-11-03，578F99D3

证据根目录：`artifacts/ai-campaign-retest-20261103-125203/`。Windows Release all 构建 180 步、零错误／警告，1704 个源码及配置摘要在构建期间未变化。测试使用固定 EXE、独立空任务档，仅通过 debug pipe 发正常客户端输入。

| 条件 | 结果 |
| --- | --- |
| 身份 | EXE `578F99D3213F745F6D8817DA0B1146CCB847550587AF491C330AE41364C1D932`；原版 Host 普通难度，seed1341701607／secondary1119464808；99级女巫空档 DF3CA9BA…F671 |
| 配置 | 配置摘要 6D941EA2…C0C3；Frozen Orb＋三项已掌握备用技能，250ms 决策；27任务＋39点，activateAllWaypoints=true、useServiceRewards=false，正常补给，保留灌注／镶孔／个性化 |
| 最终任务结果 | native-03 的 final-ai.json 为 completed、goalIndex=66，实际6107及Victory／Slayer已确认；最终D2S核对27项条件：24项REWARDGRANTED、三项服务保留，39个传送点已保存。过程中有多次取消重提、正常保存重入、回城和辅助分段行走，**不算无人干预通关** |
| 退出异常与重入 | Victory Enter后收完整1747字节B3及B0／05／06，随后堆损坏退出，不能写成正常返回选角。native-04只复制该D2S、不带导航／地图缓存，以同EXE／同Host实际重入；完整66目标20.817秒内正确跳过，原版63确认39点，正常退出码0，再次保存仍为27条件／39点 |
| 主要文件 | 根目录build.log、build-source-stability.json、source-identities.json、plan-outcomes.json、frame-summaries.json、protocol-save-and-victory-audit.json、waypoint-departure-audit.json；各native-*的identity.json、client.jsonl、frames.jsonl、ai-*/events.log、stop-*与recovery-*；native-03/saved-campaign-audit.json、native-04/saved-rejoin-audit.json和process-exit.json |

首计划至14:23:02完成，墙钟70分12.303秒，包含人工恢复、观察及停留，不作纯AI提速基准。34个非城镇点在行为／保存对齐中均为首次离区前激活；四进程C2S15聊天均0。关闭菜单时scene.waypoints可能仍是旧菜单快照，最终以原版63及D2S原固定段核对。

native-03的6107为seq105533；14:24:38 Enter后C2S69 seq105794，B3 seq105795–105801共1747字节。保存摘要`AB2467F71A817EF8EE76EF5C4BFDFF464057C7B662E19ABC05F17319798E064F`。native-04重入63为seq403（39点），再次保存1747字节，摘要`2D0250B2942CEB76FFD98A83E789EA7F941180EA38D33590CDD62DD0661A7288`，语义仍为27条件／39点。原版Host保留，测试客户端均已退出。

## 通用事件等待与墨菲斯托红门修复（已纳入88EBF1E8，剩余条件待验）

150955轮固定4FFE14D0：红门342／GUID6032先在seq93387以mode0指派，seq93725重新指派为mode1，之后没有mode2更新，seq94682撤销；实际位置17602,8069。AI在known-target-not-reassigned后改搜整层，最后17702,8077等待超过两分钟。停点截图没有门不等于此前从未指派。原包摘录：native-01-fresh/mephisto-portal-mode-evidence.json。

客户端objectEndAnimation补Operate46，按D2MOO A3Q6击杀回调的FrameCnt1／25处理静默ENDANIM，共用显示／选取／碰撞投影，不改服务端副本。

AI统一由WorldWait／waitAtWorldEvent保存事件位置、接近距离和超时，接近受阻／防御打断后继续同一事件，不转全图搜索；入口真实可用后才交互，实际换区后确认完成。已见门户、见证物与安雅出生标记复用此流程，覆盖石柱／日志／插杖后入口、三古人门和墨菲斯托红门；已提交激活仍保留原pending，不能重放操作。巴尔五波保留现场战斗及A5Q6 status3资格，563初始mode1不算波次完成。

步行升桥：从当前MPQ InitFn45解析桥身份与SizeX，仅使用实际指派／已见位置。在桥所在房间的可达岸边选择D2Common_10399距离小于18的点，等待服务端桥mode2，再走向红门；传送可用时不额外等桥。原版Quests.cpp每24帧检查，要求红门已打开，桥按0→1→2推进后解除碰撞；距离不是欧氏18格，不把桥动画结束推断成通路已开。

普通红门mode1后静默结束／传送跨幕、插杖入口及本局首次三古人门已通过，删除这些普通待验。剩余：红门进入前短暂退订、等待中防御后续行；步行触发升桥／站错房间／桥短暂退订；旧档古代人完成而新局试炼未清。王座五波、实际到132、巴尔与终局红门已在single-10通过，删除普通终局待验。上述特殊条件未触发的继续保留，旧辅助恢复不算新版自主验收。

## 都瑞尔通道、任务提示与鼠标（已构建，剩余专项待验）

- 都瑞尔战后通道：当前MPQ区域73的Teleport=2，原SrvDo027要求通过0x0804射线。客户端功能层与AI候选共用此资格；受阻目标保留步行，通过后再传送。本轮单机战后至泰瑞尔及正常跨幕已通过；反向及全部受限射线／跨墙组合仍待专项。
- 任务更新提示：按钮文字按原版1.13c读取CfgQuestLog（Quest Log），修正误用New Entry；当前原版Host的act1-progress.png已实见正确文字，删除此项普通待验。完成任务后AI请求正常Q入口；有界面占用则延后／合并，无提示不额外开窗。两秒计时由AI负责，打开与关闭均调用界面功能函数，不单独处理已阅；自动展示不暂停AI。仍待行走／连续传送中清提示、最后任务后关闭及手动接管不被误关。
- 鼠标手形：AI控制期间隐藏；暂停、结束、人工接管或手动面板观察时恢复，物品Cursor保留。待验控制权切换及菜单组合。

## 交谈刷新、吉德宾演出与活板门（已纳入88EBF1E8，剩余条件待验）

- 凯恩往返不是装备鉴定：150955轮首计划原包未见C2S34鉴定请求，实际是任务说明和私人标志刷新。AI现关闭交谈一次后调用客户端refreshQuests，与Q面板共用缓存／刷新入口，不直接发送40或为刷新重开NPC。客户端记录私人快照接收修订，有更新则复用，未返回查询合并；D2MOO Rcv0x40→QUESTS_StatusCallback返回私人28及52。实际任务说明、下一项服务和缺话题的一次重开仍保留；查询受理后按原期限等待实际结果，不无限续期。对白、旅行、传送点和插杖操作也已收回UI／AI共用功能层。
- 奥玛斯：同轮587交付后54ms即开始重开刷新，约2.1秒后提交593戒指话题。现刷新不点NPC，交付与领奖间增加公共事件等待，以真实Init39祭坛及原ENDANIM开启确认放置；位置来自实际指派，放置观察本局保留。依据A3Q3 NPCDEACTIVATE→Fn032→SetAltarMode。服务端未见专门拒绝交谈分支；另核原版客户端选取入口、S1动作分派及当前MPQ可选图层，但未闭合演出全程的客户端选取条件，不能由服务端允许推出客户端可点，暂不添加推测的全程禁点规则。证据边界见[NPC交互](../gameplay/npc/INTERACTIONS.md)。
- 鲁高因活板门：原Operate16第一次0→2，第二次才warp；客户端此前同时暴露盖子及同房间底下的type5入口，AI可直接选后者。现唯一同房间入口绑定盖子，隐藏独立底层点击目标，AI先提交开盖、观察开启，再提交进入；不改海边裸入口，房间入口绑定不唯一时不猜关联。

普通凯恩说明／私人查询、奥玛斯交付→等待放置→戒指领取、实际旅行／传送点／插杖入口本轮已运行通过，不再重复列为普通待验。剩余：Q面板／AI并行刷新、已有私人快照复用、普通状态不能冒充私人更新、换局及交谈后重新查询；消费灌注／第五幕服务结果与交错；奥玛斯演出重入及原客户端分阶段鼠标选取；上方活板门首次关闭、已打开重入及海边入口分开验收，本轮未专门确认选择哪一入口。以上已纳入88EBF1E8；本轮实际覆盖以上方结果为准，未触发条件继续保留。

## 本次修复与仍未解决的问题

本节由另一会话在578F99D3测试后补充修正；本测试会话仅记录和正常恢复，未修改代码、未提交git。后续Windows Release all已构建通过：22步后补编2步，零错误／警告，1714个源码及配置摘要在最终构建期间未变化；EXE `4FFE14D0`。4FFE14D0实机结果以上方当前轮为准，未覆盖条件继续保留，不能由578F99D3结果认证。完整日志、源码摘要和产物身份：`artifacts/ai-fixes-20261103-build/`；本轮版本差异见post-run-source-stability.json。

| 问题 | 证据、修复与验收 |
| --- | --- |
| NPC 开窗与采购剩余组合 | Akara首次自动对白后采购、Drognan／Ormus治疗后补书已有实机通过；88EBF1E8本轮Malah治疗后实际补书tsc+5也正常接回，删除这项普通待验。剩余服务主题、金币不足／库存不足、连续请求交错；不由单次采购通过认证全部组合。 |
| 德罗南开窗失败：客户端位置分歧，根因未定 | native-01 goal16，NPC5092,5032；本地5094,5032、最近 Host5117,5052。child9移动／3交互／2局部重试，未收27。与“成功开窗后采购不续行”分开；本次不宣称修复。现场：stop-02-drognan-{ai,walk,world,status}.json。需对照普通点击和 AI，关联03／04、本人96／15／5F、NPC位置及27，区分延迟旧采样和实际走不过去。 |
| Victory 后堆损坏：客户端崩溃，根因未定 | native-03 PID37000，14:24:39.526 Application Error1000：ntdll.dll+0xFF489、0xc0000374；WER为APPCRASH。堆检出点不是最初损坏位置，未取到mdmp，不能归因 AI、天气或保存。保留 application-events-all-run.json、post-victory-application-events.json、crash-evidence/Report.wer。4FFE14D0本轮相同Victory Enter已正常返回选角并退出0，终档重入也退出0，但根因未定位，仍保留缺陷。需异常转储／堆调用栈后定点修复；正常保存数据不代表退出安全。 |

第一幕入口开点／修道院定向／传送时跳过普通门，以及七职业技能配置、瓦特之厅壁画策略沿用当前源码，不用本次编译结果替代实机验收。

## 下一轮重点

| 范围 | 验收条件 |
| --- | --- |
| 指令与完整配置（源码已改，未构建／运行） | 唯一默认debug-ai.json合并完整27任务及策略；核对无参完整计划、act1／act1-5／act2-4、act1 quest3→A1Q3马勒斯／quest4→凯恩、act3 quest1→古书／quest4→金鸟、act4 quest2→迪亚波罗／quest3→熔炉。按配置顺序筛选，编号不取队列下标；campaign／其他名称各读取独立debug-ai-名称.json，缺文件明确失败；完整文件只重载安全数值，append继承当前策略；旧EXE旁仅策略文件需用户更新。此前固定EXE的27任务／保存／重入结果不认证新解析 |
| 补给与交谈 | 普通Akara／Drognan／Ormus／Malah实际采购已通过；剩余服务主题与采购交错、金币／库存不足。任务保留三项服务，不以未消费奖励判失败；需消费服务的专门流程另验 |
| 搜索与扫描 | 普通港口自主找到艾席拉、安雅实际标记→本体等待已通过，删除旧普通待验。剩余宽窄视口、室外内部漏带、地下独立未观察区域、未稳定便退订重入、标记短暂退订／长延迟／超时。通用事件等待普通主线已随本轮通过，未触发的专项仍按上节保留；不由普通出生认证所有恢复分支 |
| 地形线索 | 瓦特之厅-X、+Y及当前原版Host的-Y均实际通过；本轮ai-1793746614112在228896ms记录vaught direction=0,-1，后续尼拉塞克与任务交付完成，删除-Y普通待验。剩余+X、步行、半幅／冲突壁画及末端晚指派；庇护所零落点回中心及旧边界地形仍待验 |
| 译卷／石柱特殊恢复 | 重复确认与容量恢复、回调等待中保存／离局、短暂退订、石柱控制物件模式2至6；普通译卷与五石柱已通过，旧缺陷过程条目删除，原证据保留在183631／190613轮artifacts |
| 第一幕策略 | 冰冷之原入口不同方向及随机后备、晚指派／已开点；泰摩→修道院大门→外侧回廊双向，地穴任务不受影响；传送可用时不额外开普通门，缺蓝／城镇门／任务机关仍照常 |
| 七职业配置 | 当前仅女巫有通关证据。验收主技能未学／minLevel不足、装备变化、备用资格及全部不可用，命令覆盖职业配置、旧格式及重载；不据99级女巫认证自然练级或其他职业 |
| 后台寻路与战斗 | 34→35旧10.335秒等待、长路线计算时近敌攻击／脱险、战斗位移后接回路线；移动怪物不使长路径反复重算，城外传送不为目标额外求步行路线。首次109→110已通过，门短暂退订及已开门重入仍待验 |
| 旧特殊地形 | 旧蛇殿seed1935880086、黑色荒地seed448733414、都瑞尔入口站位、蜘蛛森林取眼球后76→78侧跳、庇护所零落点回中心、桥头／帐篷转弯；新种子通过不关闭指定旧现场 |
| 全开点与重入 | 81762E77、578F99D3及本轮4FFE14D0均已验证39点保存／重入、34非城镇点首次离区前开启，不再列为待验收；剩余斜向长跳掠点／邻区归属／旧63交错、石块旧补点只步行、第五幕563及个人古代人完成而本局未清、材料容器跨进程组合见R06／R08 |
| 客户端／宿主 | 德罗南等玩家位置分歧独立保留；居民、NPC、佣兵、怪物的sample／path／display按同GUID对照67／68／6D／15。原版Host不能认证D2X宿主NPC接近／转身、石柱／插杖演出和完整剧情消息 |

## 历史证据索引

旧详情保留在对应忽略目录和 `artifacts/ai-fixes-20261103-build/AI_TEST_REC-before-fixes.md`，不在本页重复过程流水。

| 轮次／目录（artifacts/下） | 有效结论与边界 |
| --- | --- |
| ai-campaign-retest-20261102-234535 | 固定81762E77，单进程／单计划从空档自主完成27任务＋39点，34分10.752秒，34非城镇点离区前激活、Slayer、1740字节保存及重入。仍有4次城镇接近各约15秒零移动后自恢复；不能认证后续源码。[逐任务／步骤／区域耗时](AI_CAMPAIGN_TIMINGS.md) |
| ai-campaign-retest-20261103-064025 | FD79空档前两幕，含下水道入口61失败后重入改走64；78搜88无落点后人工安排回城。F119以已有心脏／连枷档完成第三幕六任务及含第四幕城镇总28点、1650字节保存8437918D…F3AC0；未测后两幕。剥皮丛林孤立房间的一跳检查／穷尽回点已有源码，尚待原停点自主恢复 |
| ai-campaign-retest-20261102-205356 | 分段27任务／39点，含换版和人工恢复；184956／151151等早轮同理，不拼成单版本无人干预结果。151151保留德罗南位置分歧、Jerhyn续算、马拉接近和石块补点只步行现场；184956保留马拉复现及普通单位移动对照 |
| ai-campaign-acceptance-20261102-005640 | 七进程／六构建分段完成，native-07终局及正常保存，24奖励＋3保留服务；39点独立档来自native-05，不拼作同一最终档两组验证 |
| ai-campaign-clean-acceptance-20261102-071422 | 前12任务，含滚轮取消及都瑞尔辅助接近恢复；金鸟阶段暂停，未测后三幕 |
| ai-campaign-record-20261101-235051 / ai-resume-routes-20261101-195533 | 前两幕及NPC停点；Alkor／Cain／Jerhyn本地与Host分歧、普通点击无菜单、正常重入后恢复。195533的single-03马勒斯取回／灌注通过，不认证原服整链 |
| ai-campaign-retest-20261101-131857 | 历史分段五幕终局，程序59503D80…EFA0；不认证后续源码 |

## 计时、长帧与资源

当前原版Host两进程共2271条帧记录，JSON错误0、无≥3秒完整帧；通关进程最长1343.481ms、私有提交采样峰1501.58MiB／工作集889.25MiB，重入进程最长1498.517ms、峰787.69／532.03MiB。两进程C2S15聊天均0、退出码均0；完整阶段及最大帧对象见`ai-native-campaign-20261104-062931/frame-and-exit-summaries.json`。普通终局退出本轮通过，不据此关闭旧堆损坏根因或全部长帧缺口。

本轮单机十一段统计见`ai-single-campaign-20261103-201541/completed-segments-frame-summary.json`，原JSON无损坏。single-06有两条≥3秒记录，详见single-06-long-frames.json：21:54:10进入75区时3993.597ms（scene3052.386／client911.187ms，AI未运行；私有提交较上一采样增加256.695MiB）；22:39:23位于77区且表现暂停／AI未运行时3888.348ms（present3887.061ms，私有提交无增长）。分别指向场景加载与渲染提交阶段，现有粒度不能定位具体资源或驱动等待根因，不能全归因AI或内存增长。其余十段无≥3秒采样；single-10最长1278.425ms、私有提交峰1298.578MiB，single-11最长1285.146ms、峰959.871MiB，均正常退出0。保留长帧根因及旧Host堆损坏缺口，不以本轮正常终局关闭。

81762E77后台导航：3738查询／2788缓存命中／73次componentSearches／22次routeJoins，后台单次最长5.642ms。原包 `teleport-feedback-timings.json` 中2838次传送有2次未匹配，不混入有效样本；计时含战斗／交互／事件等待，不等于纯规划。

| 非城镇范围 | 样本 | 请求→落地中位数 | 落地→下一移动／施法P95 | 最大间隔／≥1秒数 |
| --- | --- | --- | --- | --- |
| Act3 | 827 | 211.13ms | 276.39ms | 6710.68ms／6 |
| Act4 | 369 | 211.17ms | 333.46ms | 16535.13ms／8 |

长间隔逐项包号及阶段在JSON：第三幕最长为吉德宾怪物事件，第四幕最长为Diablo出生；不能记作整帧卡顿。234535两进程最长帧1314.83／1560.45ms，私有提交采样峰1463.55／793.12MiB；064025的F119第三幕续跑最长1513.57ms，client849.84ms＋scene663.03ms，私有提交974.68MiB。其他轮frame-summaries.json保留原统计；这些结果不关闭195533轮debug7.276秒、scene4.147／5.718秒的旧长帧缺口。逻辑RGBA不是驱动显存，采样可能漏峰值；资源口径见[客户端资源与内存](CLIENT_RESOURCE_MEMORY.md)。

4FFE14D0本轮两进程亦无≥3秒完整帧，原始帧记录JSON错误0；汇总与完整阶段耗时见150955轮frame-summaries.json。以下峰值来自所有记录的内存采样，不只最长帧。两进程退出码均0，仍保留历史长帧及堆损坏根因缺口。

| 进程 | 最长帧／区域 | 私有提交峰值 | 工作集峰值 |
| --- | --- | --- | --- |
| native-01-fresh | 2118.18ms／1 | 1398.20MiB | 966.28MiB |
| native-02-final-save-rejoin | 2314.80ms／109 | 819.29MiB | 556.02MiB |

578F99D3本轮四进程均无≥3秒完整帧，原始帧记录无JSON截断；统计见本轮frame-summaries.json。最长为终档重入area109：client1140.09ms、scene788.74ms、AI0.0012ms；另有通关确认后堆损坏，不能据帧统计认证退出安全。

| 进程 | 最长帧 | 私有提交峰值 | 工作集峰值 |
| --- | --- | --- | --- |
| native-01-fresh | 1332.64ms | 1066.87MiB | 748.11MiB |
| native-02-drognan-rejoin | 1386.49ms | 1090.53MiB | 761.77MiB |
| native-03-asheara-rejoin | 1547.58ms | 1321.92MiB | 912.97MiB |
| native-04-final-save-rejoin | 1929.82ms | 789.02MiB | 532.92MiB |

## 角色与取证约定

空任务基准：`artifacts/debug-ai-vitality-20261031-231300/fresh-campaign/ActOneSorceress.d2s`。新轮使用独立角色目录、固定EXE／配置摘要及Host种子；正常治疗／采购，不改任务位、不授予材料、不覆盖基准或用户角色。普通任务保留三项可选物品服务，只有useServiceRewards=true才准备合格材料，不降低原资格。

子导航completed、输入accepted、地图seen、实际单位指派、任务完成、保存及正常退出分别判定。游戏测试默认不自动执行；管道和原包入口见[测试指南](TESTING.md)与[调试管道](DEBUG_PIPE.md)。
