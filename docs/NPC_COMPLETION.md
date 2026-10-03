# NPC 对话与服务实施顺序

资源始终从启动时挂载的 `assets/mpq2` 读取。`resources` 解码文字与 DS1，`content` 解析原始 NPC 记录，`gameplay/session` 执行服务和移动，`presentation` 只显示状态、提交命令。避免把对话、地图路径或物品规则复制成独立数据文件。

当前界面边界：对白／服务菜单／任务提示通过 `INpcClient` 的值投影显示；本地适配读取原目录与本人资格，服务意图仍走原权威校验。菜单／对白手势已抽出独立控制文件，商店货架／报价和佣兵详情查询仍待迁移。入口及本批准确冒烟范围见 [NPC／任务基线](baseline/NPC_QUEST.md)；下方历史构建与验收说明仅覆盖各自批次。

## 第一幕与第二幕通用聊天

2026-10-02五城镇加载冒烟：普通seed210最终包各城镇短帧退出0、stderr空，出生截图已查看；第四幕泰瑞尔TR组件在当前MPQ仅有同名DC6，通用COF合成器按OpenDiablo2 composite.go的DCC／DC6顺序读取，不替换原图。所有城镇复验告警消失。此证据只覆盖启动与出生画面资源，不等同逐NPC聊天、菜单、交易和任务服务验收；不读写角色存档或新增测试程序。

2026-10-02 原数据与固定规则核对：第一幕原DS1中立单位也从MonPreset/MonStats.NameStr与TBL取名，已移除Region按图形token推断Gheed/Akara/Kashya等姓名的表；对象名称优先取Objects.Name。对白、介绍、闲聊和第一幕任务回顾均通过原wave关联Sounds.Sound到同一发言人索引，不再靠Cain/CharsiMain分组姓名别名。原MonPreset.Act为绝大多数初见身份提供幕别；cain5没有预设、tyrael1的初见位归第四幕但预设在第二幕，沿D2MOO PlrIntro保留仅这两处原规则例外。Anya与原MONSTER_DREHYA身份按D2MOO A5Q3对应。首次介绍位号1–34本身来自原引擎npcIndexMap，MPQ没有该位号字段，不能根据原表顺序重编号。治疗仍取原NPC类，不用外观token决定服务；传送点状态只看原Objects.OperateFn=23，不拿英文显示名作为身份。源码已Release链接，本批按要求不运行游戏／测试、不提交；实际互动留给用户验收。

2026-10-02 五幕扩展：原DS1敌对单位保留人口链，非敌对MonPreset读取当前幕索引、MonStats/2的名称、16组件、interact和原路径，不用第二幕专用图形替身。原a1npc–a5npc与Sounds.FileName/Sound按wave绑定MonStats.Id／NameStr（TBL提供显示名）；凯恩的称号与安雅在MonStats中使用的Drehya身份按原D2MOO对应关系处理。NPC显示、Talk、介绍和闲聊沿共用菜单，但未实现的跨幕任务话题不伪造。原初见位序来自D2MOO `PlrIntro.cpp` 而不是MPQ字段；编码维持位1–34及原v96。五幕凯恩鉴定按原身份与现有A1Q4免费条件执行，原六个治疗NPC沿同一治疗消费者；交易、修理及赌博沿现有受限规则。其他幕佣兵和任务专属服务尚未完成，仅加载与通用聊天不代表服务全面可用。最终源码Release编译后更新固定包，本轮按用户要求不运行游戏／测试、不提交，NPC实际交互等待验收。

2026-10-02：鲁高因NPC已移除会话载入后强制禁用交互，按MonStats.interact恢复通用Talk资格。同一解析器读取MPQ的a1npc/a2npc原文本，保留原分组供任务查询，并兼容原文件NAME/Name大小写；介绍、闲聊的动态索引由原文wave匹配Sounds.FileName，Sounds.Sound的NPC身份关联MonStats.Id（幕别数字后缀分离）与NameStr/TBL，幕别由原语音路径读取。Diablerie NpcInteractions同样按MonStats.id查询Sounds语音身份，D2MOO A1Intro/A2Intro提供职业分支和初见生命周期依据；当前文本索引是本项目适配，不冒充原客户端消息ID表。

不再在dialogueKey写Greiz→Griez、WarrivAct2、CainAct2等名称别名，也不按显示姓名列职业介绍特例；职业缩写与_intro/_gossip编号从原Sounds消费。菜单、重播Introduction、Gossip、首次交互查询共用对象幕别，第一／二幕同名NPC不串对白，第二幕不套第一幕任务话题。原文中无独立Intro的NPC仍可通过实际Gossip进入Talk；未实现任务不会编造菜单内容。

D2S初见位不是MPQ数据：D2MOO PLAYER/PlrIntro.cpp固定npcIndexMap规定原怪物身份与位号。当前内容层保留位1–34的原hcIdx编号顺序，具体身份名从MonStats/原TBL获取，NPC所属幕优先读取MonPreset.Act；唯独cain5没有原预设行、tyrael1的初见位属于第四幕而其预设在第二幕，按原引擎规则保留这两处例外。存档编码只读取内容层的位号关联，不维护npcKeys显示姓名表，也不根据DS1载入顺序重新编号。第一幕原键保持，其他幕键带幕别；格式v96不变，不写未知位、不迁移旧档。声音只用于对白关联，原有字幕音频播放仍未接。

本批Windows Release链接完成并更新dist/current，按用户要求不测试、不运行游戏、不提交。第二幕菜单／对白及初见保存实际操作仍待验收；第二幕任务、佣兵和未实现的服务不在聊天修复完成声明中。下文历史第一幕记录不覆盖本批通用化状态。

头顶任务感叹号已接入共用 `npcQuestAlert` 查询和 `npc_alert_view.cpp`。当前 MPQ `Overlay.txt/npcalert` 提供原 DCC、帧数、速率、偏移和透明方式，`MonStats2.OverlayHeight` 选择高度。依据 D2MOO `A1Intro`、`A1Q0` 和任务 `ActiveFilterCallback` 区分初见、任务分配、携带任务物品、待领奖和未读完成反应；初见提示仅 Warriv／Akara，交谈中隐藏。介绍分难度持久保存；凯恩致谢及安达利尔完成反应只在本局保存，已读后不重复自动触发。完整客户端播放／重播间隔仍待核实。

1. **MPQ 数据与对话。** 读取 UTF-16 的 `data/local/docs/eng/a1npc.txt`，保留 `NAME`、`SPEED`、`wave` 和多段原文。首次接触使用对应职业的原介绍；有自动任务消息时接在介绍后。之后无新消息才显示服务菜单。Talk 列出有原文的 Introduction、当前可用任务话题和 Gossip；选择 Introduction 可重播职业分支，回顾不会再次推进初见状态。当前 MPQ 的营地 Warriv、Akara、Kashya、Charsi、Gheed 有独立 Intro；Cain 没有 `CainIntro`，其营救对白归任务消息。任务标题取 TBL；Gossip 轮换通用原文；长对白连续滚动，也可手动滚轮。音频仅保留引用，不播放。
2. **NPC 服务。** DS1 中立单位是否可点击取当前 MPQ `MonPreset.Place → MonStats.interact`，不再按外观名称猜测：营地 `rogue1`／`rogue3` 虽有 `npc=1`，但 `interact=0`，只保留人物外观；Gheed、Akara、Kashya、Warriv、Charsi、Cain 等 `interact=1` 继续可交谈。独立生成的 Flavie 也校验 `navi.interact`。悬停高亮、名字提示、点击与小地图标签共用交互状态。D2MOO 的 `MONSTATSFLAGINDEX_INTERACT` 和 `NPC_HandleDialogMessage` 提供规则交叉依据。治疗按原 NPC 身份在交谈时执行；鉴定共用物品状态、费用和存档校验。菜单以 MonStats 身份选择服务。赌博商名单（第一幕为 Gheed）、Trade / Repair、单件修理、出售、魔法货架与共同属性报价见 [NPC 交易](NPC_TRADE.md)。Kashya Hire 列表、O 面板、佣兵装备与成长已按用户截图、MPQ 和 D2MOO 接入，详见 [佣兵](HIRELINGS.md)。回购、佣兵复活及其他幕完整服务仍缺。上一版 v91／规则 v125 已构建并在城镇检查 NPC 菜单和商店布局；本轮交互资格修改尚未构建或运行验收。
3. **NPC 移动。** 解码 DS1 v14+ 的 NPC 路径记录，使用 `monstats.txt` 的 AI/速度决定哪些单位能走；只有原路径动作 1／3、距该 NPC 出生点不超过 8 子格且可从 NPC 出生点寻路的节点才移动；每次路径最多 12 节点，行走结束后等待 120 帧。交互在 NPC 附近选择可达站位，NPC 交谈时停步；位置和路线只在本局保留，载入角色重新建立。原 AI 的 66% 选点概率、12 次路径动作预算、25 FPS 等待节拍和 `Velocity / 16` 子格／帧的固定点转换来自引擎代码；8 子格活动半径及 120 帧完整停顿是本项目收紧范围与频率的适配；运行随机种子是本项目确定性种子，完整原 AI 其它动作暂缓。
4. **当前交接。** 上一版已构建并打包五个原始 MPQ 的运行目录，仅做营地 UI 冒烟；本轮按交接约定不再构建或运行，NPC 路线、装备出售和其他服务操作待验收。未新增测试脚本或用例。

以下为旧 NPC 阶段的历史验证记录，不代表当前任务与菜单改动已验证。Windows Release 构建通过；新游戏和 v13 读档短帧通过。最终规则 v37 下，人物在 96 帧内走近正在移动的 Akara 并打开原文对话；Gossip 切换为另一条原文。原掉落稀有手套正常拾取时未鉴定，与凯恩交谈后鉴定 1 件、扣 100 金币，保存恢复后仍为已鉴定且钱包为 400。Warriv、Kashya 和 Gheed 在 250 帧内移动，交谈中的 Akara 停步；四者保存恢复后坐标一致。长对话滚动界面截图人工核对通过，存于不提交的 `artifacts/`。

已核实：`npc.txt` 主要是交易价格字段；`a1npc.txt` 包含长篇原文与声音引用；营地 NPC 的速度来自 `monstats.txt`。DS1 路径记录位于单位与替换组之后。`townw1.ds1` v18 有 5 个单位各自 3–6 个节点，动作 1–4；`townn1.ds1` 亦有 5 个带路径单位。六项任务按难度保存，凯恩出现及鉴定收费由营救状态决定。对白及推进共用 `GameSession::npcQuestDialogue`；初见与未读反应有各自生命周期。Talk 话题列表沿 D2MOO 的逐任务消息链适配；声音与逐段确认仍缺，边界见 [第一幕任务](ACT1_QUESTS.md#当前核对结果与边界)。

当前界面：NPC 附近显示按服务生成的菜单，悬停项蓝色；Talk 可重播有独立原记录的 Introduction；进入字幕／商店关闭菜单，退出也不重开，重新点击 NPC 才发起新交谈。正文用当前 MPQ `FontFormal12`，按原版截图缩至面板基准 12 像素；第一行打开时已在顶部半透明框内，随后连续上滚，手动滚轮停止自动滚动，点击／Esc 结束交谈。字体资源还与 OpenD2 `D2Client.cpp` 和 OpenDiablo2 `resource_paths.go` 核对；这两份参考不含原版客户端的字幕入场时序，首行位置依据用户提供的原版截图调整。初见行为还参考[暴雪原版手册](https://ftp.blizzard.com/pub/misc/Diablo%20II%20Manual.pdf)。菜单边框、字幕框及约 2.2 秒一行的速度为近似；声音同步、字幕自然结束和多消息逐段确认尚未实现。本轮排版修改尚未构建或运行；此前 v87 Windows 构建通过，运行已确认自动滚动、多个任务话题及关闭商店后不重开菜单，不覆盖 v89 新增物品和服务。
