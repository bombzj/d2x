# NPC 对话与服务实施顺序

资源始终从启动时挂载的 `assets/mpq2` 读取。`resources` 解码文字与 DS1，`content` 解析原始 NPC 记录，`gameplay/session` 执行服务和移动，`presentation` 只显示状态、提交命令。避免把对话、地图路径或物品规则复制成独立数据文件。

头顶任务感叹号已接入共用 `npcQuestAlert` 查询和 `npc_alert_view.cpp`。当前 MPQ `Overlay.txt/npcalert` 提供原 DCC、帧数、速率、偏移和透明方式，`MonStats2.OverlayHeight` 选择高度。依据 D2MOO `A1Intro`、`A1Q0` 和任务 `ActiveFilterCallback` 区分初见、任务分配、携带任务物品、待领奖和未读完成反应；初见提示仅 Warriv／Akara，交谈中隐藏。介绍分难度持久保存；凯恩致谢及安达利尔完成反应只在本局保存，已读后不重复自动触发。完整客户端播放／重播间隔仍待核实。

1. **MPQ 数据与对话。** 读取 UTF-16 的 `data/local/docs/eng/a1npc.txt`，保留 `NAME`、`SPEED`、`wave` 和多段原文。首次接触使用对应职业的原介绍；有自动任务消息时接在介绍后。之后无新消息才显示服务菜单。Talk 列出有原文的 Introduction、当前可用任务话题和 Gossip；选择 Introduction 可重播职业分支，回顾不会再次推进初见状态。当前 MPQ 的营地 Warriv、Akara、Kashya、Charsi、Gheed 有独立 Intro；Cain 没有 `CainIntro`，其营救对白归任务消息。任务标题取 TBL；Gossip 轮换通用原文；长对白连续滚动，也可手动滚轮。音频仅保留引用，不播放。
2. **NPC 服务。** DS1 中立单位是否可点击取当前 MPQ `MonPreset.Place → MonStats.interact`，不再按外观名称猜测：营地 `rogue1`／`rogue3` 虽有 `npc=1`，但 `interact=0`，只保留人物外观；Gheed、Akara、Kashya、Warriv、Charsi、Cain 等 `interact=1` 继续可交谈。独立生成的 Flavie 也校验 `navi.interact`。悬停高亮、名字提示、点击与小地图标签共用交互状态。D2MOO 的 `MONSTATSFLAGINDEX_INTERACT` 和 `NPC_HandleDialogMessage` 提供规则交叉依据。治疗按原 NPC 身份在交谈时执行；鉴定共用物品状态、费用和存档校验。菜单以 MonStats 身份选择服务。赌博商名单（第一幕为 Gheed）、Trade / Repair、单件修理、出售、魔法货架与共同属性报价见 [NPC 交易](NPC_TRADE.md)。Kashya Hire 列表、O 面板、佣兵装备与成长已按用户截图、MPQ 和 D2MOO 接入，详见 [佣兵](HIRELINGS.md)。回购、佣兵复活及其他幕完整服务仍缺。上一版 v91／规则 v125 已构建并在城镇检查 NPC 菜单和商店布局；本轮交互资格修改尚未构建或运行验收。
3. **NPC 移动。** 解码 DS1 v14+ 的 NPC 路径记录，使用 `monstats.txt` 的 AI/速度决定哪些单位能走；只有原路径动作 1／3、距该 NPC 出生点不超过 8 子格且可从 NPC 出生点寻路的节点才移动；每次路径最多 12 节点，行走结束后等待 120 帧。交互在 NPC 附近选择可达站位，NPC 交谈时停步；位置和路线只在本局保留，载入角色重新建立。原 AI 的 66% 选点概率、12 次路径动作预算、25 FPS 等待节拍和 `Velocity / 16` 子格／帧的固定点转换来自引擎代码；8 子格活动半径及 120 帧完整停顿是本项目收紧范围与频率的适配；运行随机种子是本项目确定性种子，完整原 AI 其它动作暂缓。
4. **当前交接。** 上一版已构建并打包五个原始 MPQ 的运行目录，仅做营地 UI 冒烟；本轮按交接约定不再构建或运行，NPC 路线、装备出售和其他服务操作待验收。未新增测试脚本或用例。

以下为旧 NPC 阶段的历史验证记录，不代表当前任务与菜单改动已验证。Windows Release 构建通过；新游戏和 v13 读档短帧通过。最终规则 v37 下，人物在 96 帧内走近正在移动的 Akara 并打开原文对话；Gossip 切换为另一条原文。原掉落稀有手套正常拾取时未鉴定，与凯恩交谈后鉴定 1 件、扣 100 金币，保存恢复后仍为已鉴定且钱包为 400。Warriv、Kashya 和 Gheed 在 250 帧内移动，交谈中的 Akara 停步；四者保存恢复后坐标一致。长对话滚动界面截图人工核对通过，存于不提交的 `artifacts/`。

已核实：`npc.txt` 主要是交易价格字段；`a1npc.txt` 包含长篇原文与声音引用；营地 NPC 的速度来自 `monstats.txt`。DS1 路径记录位于单位与替换组之后。`townw1.ds1` v18 有 5 个单位各自 3–6 个节点，动作 1–4；`townn1.ds1` 亦有 5 个带路径单位。六项任务按难度保存，凯恩出现及鉴定收费由营救状态决定。对白及推进共用 `GameSession::npcQuestDialogue`；初见与未读反应有各自生命周期。Talk 话题列表沿 D2MOO 的逐任务消息链适配；声音与逐段确认仍缺，边界见 [第一幕任务](ACT1_QUESTS.md#当前核对结果与边界)。

当前界面：NPC 附近显示按服务生成的菜单，悬停项蓝色；Talk 可重播有独立原记录的 Introduction；进入字幕／商店关闭菜单，退出也不重开，重新点击 NPC 才发起新交谈。正文用当前 MPQ `FontFormal12`，按原版截图缩至面板基准 12 像素；第一行打开时已在顶部半透明框内，随后连续上滚，手动滚轮停止自动滚动，点击／Esc 结束交谈。字体资源还与 OpenD2 `D2Client.cpp` 和 OpenDiablo2 `resource_paths.go` 核对；这两份参考不含原版客户端的字幕入场时序，首行位置依据用户提供的原版截图调整。初见行为还参考[暴雪原版手册](https://ftp.blizzard.com/pub/misc/Diablo%20II%20Manual.pdf)。菜单边框、字幕框及约 2.2 秒一行的速度为近似；声音同步、字幕自然结束和多消息逐段确认尚未实现。本轮排版修改尚未构建或运行；此前 v87 Windows 构建通过，运行已确认自动滚动、多个任务话题及关闭商店后不重开菜单，不覆盖 v89 新增物品和服务。
