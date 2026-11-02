# 调试 AI 五幕任务验收记录

本页保留本轮结果、仍有证据缺口的现场及原始日志入口。已修复并运行通过的旧问题不再逐次追加过程；功能说明归[调试AI](DEBUG_AI.md)，待验收清单唯一见[原版缺口](ORIGINAL_GAPS.md#本批验收清单)。原版权威规则和协议差异仍归对应模块，不能因AI通关删除。

最近一次 Windows Release 客户端构建完成，编译与链接零警告。战后回收任务掉落、等门期间战斗走位和开门后恢复通行尚未实机复验。

## 2026-11-02 五幕实机记录（含换版与人工恢复）

| 条件 | 实际值 |
| --- | --- |
| 原版Host | 本机4000／PID226184，普通难度；用户再次重开后seed1529334111／secondary3262289062。此前native-01／02房间seed103542261，不与新房间混作同一次连续测试 |
| 构建与版本 | 黑暗森林依赖及近点返城修正后，Release全目标构建通过，日志build-waypoint-return-retry.log。native-04／05使用冻结客户端`DABB4D0935A49AAA9A1FC9BCEC66AC0489718C8C7C62976D8AA8AB1F350ADD9D`；测试进程与构建不并行，运行中未继续改代码 |
| 初始角色 | 99级女巫ActOneSorceress，原始空任务档DF3CA9BA…F671，初次入局私人任务字与status全部0；不是通关档。准备期间普通NPC接触开启洞窟介绍，不算已完成任务 |
| 任务完成定义 | 完成目标、交付并取得奖励资格即可；恰西灌注、拉苏克镶孔、安雅个性化默认保留，三项目标useServiceRewards=false。不改原版REWARDPENDING为已消费；只有显式true才执行材料服务。任务完成不要求购买帽子 |
| 计划与控制 | 冻结配置debug-ai-campaign.json／1F2D0EA4…EF451，最初66目标（26任务＋39点＋巴尔），useServiceRewards=false。安雅失败后按实际进度重提；额外补点目标32循环后取消补点，另提仅巴尔的计划，最终25点。全程仅debug pipe d2x-rebuild-campaign，未用聊天、管理旅行或授予任务物品 |
| 运行与恢复 | native-03于09:17:43从空档完成洞窟／血鸟，因开点节点误省略卷轴前置而正常保存换版；D2S6BA18D3C…AD4FE。native-04于09:25:26以此档重入同Host，PID233136，完成其余任务，含安雅处普通移动人工恢复及取消额外补点。不是单一构建／同一计划无人干预通关 |
| 最终存档 | native-04-resume-waypoint-return/saves/ActOneSorceress.d2s，1747字节，SHA256 `841BC8B56B7510683022F68115A7680EF9E464EF5729A14DD93748787C89D8F3`。原生个人事实27项目标满足，含三项服务待使用；正常保存后资源工具重读成功，最终审计见final-quest-facts.json、final-save-audit.json |
| 专项复验 | native-05于10:16:23用上述通关档副本正常重入，PID218908，同EXE／Host。普通travel5→return-town完成：C2S49 seq374选5，seq490选1，实际到1，无town-portal-submitted。验证眼前已解锁点返城使用传送点，不重跑任务 |
| 证据根 | artifacts/ai-campaign-rebuild-20261102-085141/。各native-*/identity.json固定版本／输入；client.jsonl为原包、frames.jsonl为长帧与内存、checkpoint为AI状态。主轮证据归native-04-resume-waypoint-return，近点返城归native-05-near-waypoint-return |

### 分幕结果

| 幕 | 当前结果 | 边界 |
| --- | --- | --- |
| I | 六项目标完成，已通过瓦瑞夫到40 | native-03完成洞窟／血鸟；native-04取到真实bks、翻译／救出凯恩、女伯爵、马勒斯及安达利尔完成。已亮黑暗森林点没有跳过卷轴；马勒斯槽3仍8270／0x204E、status10，goal4完成且灌注保留。不是同一构建从零完成第一幕 |
| II | 六项目标完成，已通过马席夫到75 | native-04自动合杖／插杖、等待并实际进入73，击杀都瑞尔及Tyrael／Jerhyn／Meshif后续均完成；未人工传送到洞口。staff-093554-world.json等保存实际73及双方位置，act2-town-*保留返城交谈。旧洞口卡点本局未复现，不能据不同种子成功断言旧根因已定位 |
| III | 六项目标完成，经墨菲斯托通路到103 | native-04实际背包曾持j34／GUID769及qey／GUID977，金鸟交付使用后槽20为8193／0x2001、status13，j34／g34／xyz均已不在库存；golden-bird-completed-*保存状态及goal16完成。古书、合成和破珠正常推进，未重现此前连枷停点 |
| IV | 三项目标完成，已到109 | 衣卒尔、真实熔炉放石／锤击／装备恢复、混沌封印及Diablo完成；act4-completed-*保存阶段事实 |
| V | 六项目标完成，实际终局及正常保存通过 | 拉苏克0x2022／安雅0x201E保留服务；野蛮人、冰药／奖励、尼拉塞克及三古人完成。五波、实际入132、巴尔、Tyrael20175、红门、6107、Victory均完成。安雅交谈含人工恢复；额外39点计划未完成，不能混作全开验收 |

### 第五幕与终局

native-04的final-ai.json为completed，final-world.json为Victory／area109，final-victory.png显示Slayer。final-quest-facts.json逐项核对27个个人记录，其中24项有原奖励位、3项保留材料服务资格：恰西0x204E、拉苏克0x2022、安雅0x201E。保留次数不是未完成目标，也不记成已经消费。

原包seq166211为6107。普通Enter确认后seq166477发69，166478–166484收到完整7段B3／1747字节，166485/B0、166486/05、166487/06；after-victory-enter-online.json为CharacterSelection／error=null，final-save.json为实际保存重读。随后正常退出客户端，未关闭用户Host。B3普通完整保存通过，不替代下方半档被新first抢占的特殊时序验收。

## 当前缺陷与待验收

| 范围 | 当前结论与证据入口 |
| --- | --- |
| 安雅交谈／位置分歧 | native-04目标24在尼拉塞克死亡后返城，NPC8118／class512在5105,5114；本地5103,5114，最近Host5112,5078，三次子导航恢复及一次完整计划重提仍失败。stop-anya-dialogue-*、anya-dialogue-packet-window.json保留现场：跟随04、开窗13、关闭30已发送，无27。随后人工普通online-move到最近已观察Host点5112,5078，再重提完整计划后交谈成功，目标24完成。anya-manual-native-move-*／resume-after-anya-manual-native-move.json记录人工恢复；根因未定，不记无人干预通过，不因后来成功删除此缺陷 |
| 军营到监牢补点循环 | native-04完成三古人后，额外补点目标32／area29在28区反复接近14858,4958与14862,4963，玩家约14906,4958、Host14907,4957，数分钟无实质推进；子导航反复无路径／重建，夹有known-target-teleport但无成功位移。jail-route-*-walk.json及stop-jail-waypoint-*保留现场，根因未定位。按用户任务优先要求取消额外补点，另提仅eve-of-destruction的普通任务计划；不授点／不传管理坐标，本轮39点未通过 |
| 黑暗森林强制开点范围 | native-03顺路开启第四点、native-04已亮点不漏取卷轴均有实际证据；因换版时已亮点，本轮尚未覆盖“取卷轴后点仍未亮，再搜寻／开启”的分支。此前遗漏独立卷轴依赖的失败已修复并通过续跑，不再列未解决缺陷；现场留native-03的pause-waypoint-dependency-* |
| 071422都瑞尔入口卡住 | 两次计划及其有界重试都在洞口3045／class100处超时。原站位客户端及子导航Host采样均22847,14208，洞口22845,14206；普通move到22845,14208未实际移动。正常Teleport请求洞口中心后实际落在22847,14206，再原13点击成功到73。尚不能确定交互范围、碰撞或位置反馈中的根因，不仅凭重启成功归为等待不足；未改代码 |
| 第五幕重入三项修复 | BBF61CD7版本已构建，尚待对应运行。短跳改为已确认距离／绕障路线进度；实际563按Operate70及已收任务条件识别；547本次开门收到本人声音19后执行本局祭坛／三古人。普通全新任务通关不能替代特定重入条件验收，见R06／R08 |
| 移动NPC接近／位置分歧 | Alkor／Jerhyn曾出现客户端已在身边、Host脚点仍较远且不开窗；后续换种子正常通过不能证明根因修复。原失败仍保留，细节见下方专项及R07 |
| B3替换未完成快照 | 已按原版first语义修复，补丁后正常终局和完整保存通过；尚未再次撞到半档替换时序。失败为旧native-04 seq53390开始1797字节，至53421仅1020字节，53427新first触发旧代码协议错误。原现场及修复边界见[联网](../modules/NETWORK.md#tcpip直连与自研宿主入口) |
| NPC与演出组合 | 原版Drognan交谈期间清旧提示、后续合法新提示已有证据；全部NPC资格、D2X宿主Fn032跟随、固定朝向例外未逐一运行。石柱演出及逐帧／随机一致性仍待核对；旧轮staff-073037.png已拍到完整洞口，不再列作缺失画面 |

## 角色准备与可复用基准

空任务基准位于本地`artifacts/debug-ai-vitality-20261031-231300/fresh-campaign/ActOneSorceress.d2s`，摘要见本轮条件。使用独立目录，正常补给／交易，不覆盖基准或用户角色。既有资源工具save-json用于核对存档，save-recipes及[人物配方](fixtures/debug-act1.json)可准备新的独立角色；新档须重新记录摘要，不能混作同一基准。

本轮为能力足够的99级女巫，初始不保证腰带已满／当前血蓝满，由正常城镇治疗和采购完成出发准备；不认证自然练级、装备成长或低等级适用性。此前实见基础Static Field5级、装备后14级，Frozen Armor基础1级及真实状态消失后补放；不能据此认证所有技能与装备状态组合。

普通任务无需预留或购买帽子。仅显式useServiceRewards=true测试材料服务时才准备合格物品；灌注可能产出带孔稀有帽，不保证还能镶孔。不得降低原资格、使用当前装备或改任务位制造成功。用户本轮已明确保留这些服务，旧轮强制消费及补帽的做法不再作为任务完成标准。

## 公共策略的专项观测

已通过的常见功能归DEBUG_AI，不再保留重复问题行；下列是结果适用范围。

- 普通难度高等级女巫在原版Host已分段完成27任务、39点、实际终局／保存。冰甲初次及失效补放、普通采购／饮药、金鸟真实掉落、峡谷红门先行及开点、庇护所74→40→74回溯、真墓线索引导均有原包／状态证据；其他职业、难度和策略组合未随之完成。
- Diablo危险弹体多时实见evading，前后较少时clear及正常攻击；未认证所有密度、来源和低血组合。无伤害不等于免疫。长期资源压力和备用技能边界见R11。
- 寻路已实见缓存命中、预算让出及路径继续；单次Grid搜索仍同步，2ms是查询间软预算。一次查询可超预算，未作同条件提速认证。
- 本轮仅用debug pipe。历史暂停／任务面板及物品取回有局部结果；剪贴板、IME／编码边界、事务期间阻塞／重载和迟到回包仍见R04／R10，不由普通任务通关删除。

## 停点与验收边界

原版Host结果只验证D2X客户端；D2X宿主的NPC行为、原任务字／状态投影、入口资格及其他消息差异须分别验证。已核实的原版消息差异归[第二幕](../gameplay/quests/ACT2.md#已确认的原版消息差异)及[后三幕](../gameplay/quests/ACT3_5.md)，不因原版Host任务完成删除。

单一固定最终EXE从空任务档完整运行、普通恢复、特定重入、异常事务和视觉逐帧分别记录。每次人工输入、短恢复计划或正常保存换版单列，不拼成无人干预整链。动态GUID、游戏状态和当前房间资格不能从旧档或旧图片补猜。

## 证据索引

运行产物、截图、存档、参考仓库均留忽略目录。已关闭旧问题的详细过程已从源码文档清理；必要的版本与原始证据保留如下。

071422轮进洞停点：stop-01-duriel-entry-{ai,walk,world}.json、staff-watch.jsonl、staff-072954.png（开墙演出）及staff-073037.png（可见洞口）保留首次位置／画面；原包seq57453／57673已对3045发C2S13而未换区。full-campaign-resume-after-duriel-timeout.json及duriel-retry-*保留用户要求的第二轮，仍失败。duriel-manual-closer-*、duriel-manual-teleport-*、duriel-manual-entry-result-world.json及full-campaign-resume-after-portal-position.json保留人工普通移动／传送／交互和真实73抵达。D2MOO PlrMsg的物件分支先检查UNITS_IsObjectInInteractRange及COLLIDE_MASK_PLAYER_FLYING，Operate43本身直接寻找73落点；MPQ class100的OperateFn为43，不能沿用普通开门34／66的模式等待解释全部现象。

| 轮次 | 结果与本地入口 |
| --- | --- |
| 071422旧轮 | artifacts/ai-campaign-clean-acceptance-20261102-071422/。BBF61CD7从空任务档完成前12任务，含误滚轮取消及都瑞尔人工接近恢复；金鸟搜索阶段用户暂停，未完成后三幕 |
| 11-02 005640，01:20至06:23 | `artifacts/ai-campaign-acceptance-20261102-005640/`。七个进程／六份构建分段完成，含人工恢复。native-05确认39点并正常重入；native-07确认27任务、实际终局与保存。不是同一最终档同时拥有两组验证 |
| 11-01 235051 | `artifacts/ai-campaign-record-20261101-235051/`。第一、第二幕12任务完成后按用户指示暂停，含NPC恢复。native-02-rejoin存档8463ACA9…C8414；其后三幕未测试 |
| 11-01 131857 | `artifacts/ai-campaign-retest-20261101-131857/`。历史分段五幕及终局完成，最终程序59503D80…EFA0，D2S427C4E9C…CAC77；不能认证后续源码 |
| 11-01 195533专项 | `artifacts/ai-resume-routes-20261101-195533/`。single-03正常箱内马勒斯取回／灌注通过；原版native-01-fresh最终因Alkor停住，只有前12任务完成。不能记全链通过 |

005640轮的最终通过证据为native-07-object-reading-fixed：EXE `14700404049C58620A36BEE0667E00C298A1054926443168A3677D90E2A806AB`，新Host seed1768906280；seq9587为6107，final-ai.json为completed，terminal-062230.png为Slayer界面。Enter确认后seq9775发69、9776–9783完整B3、9784/B0、9785/05、9786/06，D2S `0AEA33782175C566A4D0CF466A7B2FA3A7CAF4BD292150BB81198749A8274EAB`重读27任务完成，已确认27点。39点独立档为native-05的`F15F8C61396C3EE268D551CFCB054015A0939C32209054AB26FA907F43150B10`。各进程identity.json固定输入身份；save-and-finale-packet-index.json说明当次没有半档替换。

## 任意进度、路线与客户端长帧专项（实施中）

### 仍未定位根因的NPC现场

| 现场 | 已确认事实与限制 |
| --- | --- |
| 195533轮Alkor | native-01-fresh的alkor-stop-*及alkor-opening-packets.json，seq70227–70370：6次C2S04、24次C2S13、42条该NPC的6D，无27开窗；含父导航恢复，不能全部算一次child。停点本地5085,5011、最近Host5084,5029；后来NPC移动后的截图不能倒推此前已满足范围。未证明碰撞、预测或NPC运动哪个环节是根因 |
| 235051轮Cain／Jerhyn | native-01的stop-01-cain-*：本地5133,5086、Host5136,5049，三次交互仍不开窗；重提后续行。stop-02-act2-return-*／stop-03-jerhyn-repeat-*：本地约5084,5155、Host5122,5119，普通点击亦无效，非AI专属。manual-input-packet-window.json保存普通C2S03，表明输入已发、世界仍更新，不能证明服务器已执行移动 |
| 正常重入恢复 | 235051轮native-02-rejoin同EXE同Host正常保存重入，GUID变化、双方入场脚点一致，随后完成Jerhyn／Meshif；证明实际存档能续行，不单独证明根因已修复 |

### 长帧与内存

frame-profile记录阶段墙钟、进程私有提交／工作集／缺页／CPU、系统可用内存及图形逻辑计数，与原包epoch／单调时间对齐。图集字节不是驱动显存，缺页不区分软硬，采样可能漏临时分配。永久卡死还需线程栈；历史WPR启动因权限拒绝，未运行或停止其他采样。

| 范围 | 观测 |
| --- | --- |
| 本轮native-03／04／05 | current-host-frame-summaries.json汇总：三进程均无≥3秒完整帧。native-04约2908秒，65个≥100ms帧，最长1452.13ms／area1，client750.69ms＋scene698.72ms，AI0.0008ms；私有提交采样峰值1428.01MiB，工作集峰值1010.76MiB。native-03最长1275.36ms／923.36MiB私有提交；近点返城专项native-05最长1478.71ms／812.51MiB。采样和不同运行范围不能认证旧卡帧根因已解决 |
| 195533原版轮 | frame886约7276.75ms，其中debug7255.26ms，对应旧版启动计划；frame33105／area37约4154.85ms，其中scene4146.90ms；frame62530／area73约5718.81ms，其中scene5678.22ms。阶段不同，不能都归AI或网络。原JSON及CLIENT_RESOURCE_MEMORY保留定位 |
| 235051两进程 | 原版前两幕最大2426.70ms／私有提交采样1168MiB；重入对话最大1380.92ms／818MiB；均无≥3秒完整帧 |
| 005640七进程 | 各frame-summary.json及根frame-summaries.json；各段均无≥3秒完整帧，最大2021.41ms，私有提交采样峰值1348.22MiB。分段、不同种子不能代替本轮单进程全链对照 |

旧轮巴尔死亡到泰瑞尔出现约25秒是实际指派等待，不能记成25秒卡帧；本轮也保留了原地等待NPC指派的AI快照，没有≥3秒完整帧。当前源码的资源归属、按需帧及计量口径归[客户端资源与内存](CLIENT_RESOURCE_MEMORY.md)。

### 端口查询

已定位并复现“表格空行而实际查询非空”：同一PowerShell输出流先Select进程Id／ProcessName，再Select连接LocalAddress／LocalPort／OwningProcess，自动格式化沿用首个列，真实连接显示为空行。005640根目录port-query-format-analysis.json同时保存Count=1、0.0.0.0:4000／PID226184和空白表格；235051轮port-query-first-call.json亦为firstCallCount=1、errors为空。独立输出JSON或分别显式格式化，必要时交叉netstat／实际连接；最早未保存计数的调用不能逐次追认，不归咎权限或CIM。
