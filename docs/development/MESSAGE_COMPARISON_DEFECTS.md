# 原版消息对比缺陷

本页跟踪原版1.13c Host与D2X单机宿主的配对消息差异。全部条目处理并完成必要验证后清空本文件，保留入口供后续使用。本轮运行已恢复，使用用户开启的原版Host与D2X单机，先不使用D2X TCP/IP Host测试；计划及逐项结果见[MESSAGE_COMPARISON_PLAN.md](MESSAGE_COMPARISON_PLAN.md)。接收成功、静态核对或自研单机通过均不单独构成原版一致性认证。规则结论同步回负责专题，不把本页变成长期运行日志。

## 对照条件与证据

首轮为普通难度、隔离的1级女巫Hero，两种模式使用同一准备存档；仅为控制范围预先解锁冰冷之原传送点。执行私人箱子开关、鉴定卷轴背包→箱子→背包、阿卡拉任务64及介绍12确认、营地传送点→冰冷之原、向空地释放一次短杖授予的1级火弹，再正常退出。

原版Host在127.0.0.1:4000，房主abab与客人Hero未组队；D2X单机只有Hero。地图种子分别为1942025962与536192050，不按怪物数量、坐标、GUID或全局包量判定一致性。五个基础MPQ逐文件SHA256一致。原D2Client MD5为`f5860c629d309b8fc1f96174babcf633`，D2Game为`978f6f74cd031bce6ecee602cd5988cc`。

首次采集EXE为`build/bin/d2x.exe`，SHA256为`340ee796435229fa3c16bd8d4d7ec71c6e4834c3c732a9c6f478a72020c62376`；开始时源码HEAD为`e1bf18060c8d0f9ec22cc364ef2c861f71f47964`，不由此认定EXE等于该源码构建。

本轮临时材料在`artifacts/original-message-round1-20261025/`：`run.json`、`comparison.json`、`item-comparison.json`、MPQ与DLL摘要，以及`single/client.jsonl`、`single/host.jsonl`、`native/client.jsonl`和各步骤快照。以下sequence均指对应客户端JSONL；临时材料不纳入提交，关键原包及参考入口在本页独立记录。

## 待解决条目

| 编号 | 问题 | 当前状态 | 关闭条件 |
| --- | --- | --- | --- |
| Q1 | 接取任务向同局其他角色发布Quest Entry | 未组队双客户端已确认：关闭Akara后双方收到任务1状态1；其他资格组合未验完 | 按D2MOO恢复本局任务状态传播，双客户端核对未组队／组队、已接／未接／已完成资格及回包时机 |
| Q4 | 传送离城后未传播／保存LEAVETOWN | 单机保存000C并在新进程重入通过；最新多人保存及ENTERAREA受本轮禁用D2X Host限制未复验 | 同局成员接取、离城、重入后分别保留真实STARTED／LEAVETOWN／ENTERAREA，不由阶段补造缺失位 |
| Q5 | 普通共享进度触发奖励副作用 | 罗达门特共享曾附带技能书标志并导致保存拒绝；公共队列已分离，单机Atma接取保存／恢复通过，双人仍待重验 | 同局人物接取后只保存STARTED，不授予技能书；双方正常退出和新进程重入通过 |
| T1 | 同幕传送点本人15末字节为0而原版为1 | 单机／TCP营地→冰冷之原已确认flag=1；跨幕及旁观者组合未验完 | 按原旅行／定位调用恢复标志并验证同幕与跨幕、本人／旁观者；不把普通纠正／技能定位一律改为1 |

当前复验材料为同一临时目录中的`recheck-single`、`recheck-host`、`recheck-guest`和`recheck-skills`。单机`10-reopened.json`记录新进程恢复；多人双方关闭Akara后的`05-after-close.json`及客户端原包记录`5D0100010000`，客人`quest-entry.png`记录提示。Atma扩展仅依据D2MOO，未采集新的原版对照；其跨幕状态过滤符合预期，但退出捕获保存失败，见`recheck-host/save-failure.json`，不能宣称多人保存通过。上述多人结果来自前一构建；最新Q5仍待双人保存复验，禁止D2X Host期间不以单机替代；本轮管理准备到第二幕后的单机Atma304→310、STARTED=0004、无技能书及正常保存／新进程恢复已通过。普通资源、骨甲元数据、公共属性派生、徒手资格及陷阱公共时钟／最后帧回调已完成本轮针对性复验，对应条目已移除。

### Q1 同局任务通知

用户在原版Host端观察：本轮Hero与Akara交谈时，另一角色也收到Quest Entry。当前客端日志只记录Hero接收，不能由它证明房主收到的确切包号或任务位。后续以原版任务全局状态、ActiveFilter／StatusCycler及NpcDeactivate回调为主，分清本局状态推进、个人STARTED、UI日志提示与组队奖励资格，不能把任务奖励扩散当作同一件事。

入口：`reference/d2moo/source/D2Game/src/QUESTS/ACT1/A1Q1.cpp`的`ACT1Q1_Callback11_ScrollMessage`、`ACT1Q1_Callback02_NpcDeactivate`及状态循环；当前D2X入口为`src/server/systems/quests/system.cpp`的`System::execute`／`commit`、`src/server/systems/quests/act_one.cpp`和`src/hosting/native_quest_wire.cpp`。

源码已确认这不是Akara专属机制。公共玩家遍历／状态循环由任务回调调用，A1Q1／A1Q2／A2Q1确认64／81／304均遍历本局人物、刷新NPC对白，关闭对应NPC时再做状态循环；当前接入共同的`startQuest`、既有可靠人物队列和公共`queueStatus`／`flushStatuses`。查西146在确认与NpcDeactivate之间分开推进，不能套用立即写STARTED的时序。尚未认证全部任务的调用组合。

### Q4 个人中间位与本局状态

首轮同一准备存档的邪恶洞穴任务字为0000；完成接取、营地→冰冷之原及正常退出后，原版Hero为000C，自研Hero为0004。用户原版房主abab的退出存档也为000C。对照D2MOO的A1Q1 ChangedLevel及UpdateQuestStateFlags：本局从已接取状态离开城镇会遍历人物设置LEAVETOWN，进入邪恶洞穴另有ENTERAREA与状态循环。

当前增加公共Goal旗标并集提交，接取与离城按相应原回调筛选人物；D2S导入／导出分别保留三个原中间位，不再仅从阶段合成它们。格式及校验边界同步[SAVES](../modules/SAVES.md#任务槽与奖励语义)。

### T1 同幕旅行定位标志

源传送菜单63除GUID外相同：`63 <source u32> 020103000000000000000000000000`。两边49请求均为level3；目的地真实class119物件均以mode1发布，且均到达冰冷之原。

原版S2C723为`1500020000003C12621401`；自研465为`1500010000007C13471600`。末字节差异不能归于坐标／GUID。`NativeRealmService::changeArea`在非步行、同幕分支固定`out.u8(0)`，跨幕分支已有1；`nativeReposition`也固定0，须按原调用来源分辨，不能全局替换。原D2MOO PlrMsg的传送分支与`sub_6FC81CA0`用于核对。

## 已相符及本轮限制

M1按非本轮独有差异结案：D2MOO `D2Common/src/Path/PathMisc.cpp`的`PATH_Straight_Compute`先取Toward路径，只在目标距离平方不超过18²时回退AStar；当前`src/world/navigation.cpp`的`Grid::nativeUnitPath`使用相同范围。结合两种服务端在对应墙体路段都需手动绕行，不将此现场作为扩大寻路范围的依据；其他客户端预测问题仍需独立证据。

四次卷轴转移对应自研186／193／202／210与原版239／262／290／343，将物品GUID和9D的拥有者GUID归一后逐字节一致；物品最后回背包(1,0)，Cursor为空。两边选择火弹均收到23确认。正常退出回到选角，原版B3×4→B0→05→06；单机内存05→06，不把本机存储省略网络存档回传当作缺陷。

本轮已补七职业代表技能、敌人命中／击杀、商店及保存重入；完整任务完成、原版客户端Join修复后D2X Host、多难度与完整多人交错仍未验证。原版既有47／48等未消费目录不因本轮可分帧而视为已实现；本页范围为上表条目。
