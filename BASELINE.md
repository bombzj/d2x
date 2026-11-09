# 项目基线

PD2专项内容：[能力状态](docs/pd2/STATUS.md)。本页只维护当前能力概况、架构边界和主要缺口；实现细节、原版依据与验收条件归[文档目录](docs/README.md)中的负责专题。构建、运行、试听和调试过程在当次交付中说明，不写入本页。

## 总体阶段

战斗重构处于R1–R5交错迁移后的公共底座收尾阶段，尚未完成整体范围验收。内容准备、权威执行和公共计算已分层；非玩家实体、属性／资源、持续效果、动作帧事件及原生接触已有公共消费者，但不代表全部程序、来源与组合完成。

主要缺口：世界休眠重建的组合验收、队列1／2全局交错及逐随机轨迹、全部属性／费用／死亡回调、剩余专用飞弹与世界载体、装备／伙伴组合，以及既有内容迁回后的逐项对齐。德鲁伊／刺客、后三幕专用AI／首领及特殊程序受这些底座门槛约束；不能以历史技能数量或公共函数存在宣称完整复刻。

阶段、优先级和完成条件唯一见[战斗重构进度](docs/architecture/MULTIPLAYER.md#23-战斗重构实际阶段与剩余步骤)；执行入口见[内核子系统](docs/modules/SERVER_SYSTEMS.md)、[技能路径核对](docs/gameplay/skills/COMMON.md#技能执行路径源码核对)，数值入口见[公共规则](docs/architecture/COMBAT_SHARED_RULES.md#重点战斗数值的当前公共入口)，原版证据见[资料来源](docs/resources/THIRD_PARTY.md#原版113c静态与开发对照证据)。剩余工作清单见[原版剩余工作](docs/development/ORIGINAL_GAPS.md)。

## 产品与代码边界

D2X提供单机、TCP/IP和原服连接，共用一套客户端。自研宿主负责权威玩法，客户端读取状态并提交请求；断线不切换权威来源。宿主负责内容准备、协议适配和角色存储，玩法内核不依赖资源加载、网络、文件或显示设备。资源与规则以当前MPQ为依据，未实现行为明确拒绝。模块职责见[架构速览](docs/architecture/OVERVIEW.md#agent速览)，协作约束见[AGENTS.md](AGENTS.md)。

客户端按可见瓦片与动画帧上传场景图像，按预算回收图集页和解码像素；MPQ规则表按挂载和准备模式共享，见[客户端资源与内存](docs/development/CLIENT_RESOURCE_MEMORY.md)。服务端战斗目标以[复刻目标架构](docs/architecture/SERVER_COMBAT_RECONSTRUCTION.md)为准，目标设计不等于当前能力。性能优化必须保持有效输入下的数值、随机消费和执行次序，不能用降频、合帧或近似算法换速度。

## 当前实现范围

已接能力不代表全部组合或原版客户端兼容性完成；支持条件和未覆盖范围由对应专题维护。

| 领域 | 能力概况 | 主要缺口／专题入口 |
| --- | --- | --- |
| 连接／宿主 | 单机、TCP/IP、原服连接及独立游戏服务；按房间难度恢复各角色自己的出生幕和传送点；无窗口原协议诊断入口可组装房主及多个真实客端 | 完整故障恢复；[联网](docs/modules/NETWORK.md)、[独立服务](docs/development/PVPGN_SERVER.md)、[无窗口入口](docs/development/BUILD_AND_RUN.md#无窗口协议客户端) |
| 地图／世界 | 五幕地图生成及客户端房间重建；宿主逐房间物化、缺失输入按房间补载，并已核对原版瓦片／静态碰撞及房间调用链；跨区寻路、常驻碰撞格与逐子格占位、多人出生／旅行／复活寻位、探索缓存及常见环境物件 | 原版混合客户端、多人交错、单位随机／恢复／资源回收及特殊内容；[地图](docs/modules/MAP.md)、[物件](docs/gameplay/world/OBJECTS.md) |
| 物品／成长 | 库存、装备、掉落、商店、合成、角色成长及死亡／尸体；掉落双流、冻结批次、逐操作属性及指定词缀；HC创建、永久死亡、模式隔离及授权回收 | 原版同帧出生／逐件落点时序、全来源／属性组合、复杂争用与多尸体；[物品](docs/gameplay/items/README.md)、[人物](docs/modules/CHARACTER.md)、[对照存档](docs/development/CHARACTER_REFERENCE_FIXTURES.md) |
| 战斗／技能 | 普攻、法术、状态、武器效果及PvP；女巫、亚马逊、圣骑士、死灵、野蛮人技能；刺客蓄气／陷阱／伙伴施法及德鲁伊变形／召唤已有执行入口；尸体技能起手资格与选取 | 全技能与来源组合、完整双持与触发组合、完整PvP规则；[死灵](docs/gameplay/skills/NECROMANCER.md)、[刺客](docs/gameplay/skills/ASSASSIN.md)、[阵营](docs/gameplay/combat/FACTIONS.md) |
| 怪物／伙伴 | 第一、二幕家族及第三至第五幕逐项程序；NPC休眠命令、光环与修饰器重建；零售死灵召唤回调 | 全局交错／逐随机及全难度组合；本机1.13c的Uber默认AI、高级方块门保留空分支，不新增Realm World Event触发或替换规则；[怪物目录](docs/gameplay/monsters/README.md#当前实施范围)、[逐项台账](docs/gameplay/monsters/ACT3.md)、[佣兵](docs/gameplay/characters/HIRELINGS.md) |
| NPC／任务 | 城镇服务、NPC原MapAI移动与动作、交谈暂停；Cain／Anya退场、熔炉、邪恶洞窟、Nihlathak、混沌清场等任务演出；五幕个人任务主流程、组队任务及奖励保存；任务日志已阅回执 | 完整剧情时序／退场表现、机关和特殊多人任务；[NPC交互](docs/gameplay/npc/INTERACTIONS.md#宿主npc移动与地图动作)、[NPC／任务](docs/modules/NPC_QUEST.md)、[任务](docs/gameplay/quests/SYSTEM.md) |
| 多人／表现 | 组队、聊天、交易、敌意及共享收益；物品提示、自动地图、区域标题、消息日志、局内聊天与IME、头顶消息、ESC各级菜单与设置；包裹拿放、快捷键、方块、鉴定、堆叠与金币输入 | 完整像素／状态对齐、特殊任务事件、硬核权限组合、完整本地化／输入法组合；[UI](docs/gameplay/ui/CLASSIC_HUD.md)、[包裹](docs/gameplay/items/INVENTORY_UI.md#操作)、[自动地图](docs/gameplay/world/AUTOMAP.md)、[物品提示](docs/gameplay/items/PRESENTATION.md#物品属性提示) |

## 专项状态

| 专项 | 已接范围 | 主要缺口／专题入口 |
| --- | --- | --- |
| 局前界面 | 战网大厅按原控件表接表单、聊天／频道与帮助页；创建角色按零售控件／动画表、天梯创建与后续入局链（自研GS按普通规则执行，不提供赛季／排名）；单机及TCP/IP Host难度选择 | 成员肖像、天梯／资料服务、中文发送、非资料片及完整音画／多语言对照；[局前核对](docs/modules/NETWORK.md#局前逐界面一致性核对)、[创建页](docs/modules/NETWORK.md#创建页零售依据与边界)、[难度选择](docs/modules/NETWORK.md#tcpip直连与自研宿主入口) |
| 局内面板与输入 | 传送点菜单原字体与状态色；Configure Controls主／副键、16个技能快捷槽、全角色共享偏好；原版技能循环与角色语音键 | 实机组合、重启保存及像素对照；[传送点](docs/gameplay/ui/CLASSIC_HUD.md#传送点与菜单)、[ESC菜单](docs/gameplay/ui/CLASSIC_HUD.md#esc-菜单)、[面板核对](docs/gameplay/ui/CLASSIC_HUD.md#主要面板对照边界)、[客户端偏好](docs/modules/SAVES.md#客户端偏好凭据与探索) |
| 世界选取与标注 | 世界悬停／点击使用原COF组件、cell帧范围及专用矩形；NPC／玩家姓名、门户标题、覆盖图与感叹号沿原高度和字体框定位 | 重叠目标、全部动画与原版运行对照；[世界选取](docs/gameplay/world/OBJECTS.md#世界悬停与点击选取)、[NPC姓名](docs/gameplay/npc/INTERACTIONS.md)、[状态覆盖图](docs/gameplay/skills/COMMON.md)、[玩家姓名](docs/modules/NETWORK.md#组队邀请客户端) |
| 音频 | 音乐／任务曲／对白／雨声独立读取与播放线程，短音效后台准备及混音；局前菜单曲、UI、脚步、物品、NPC、叙事事件声音；场景声音单位绑定与原Falloff距离音量 | 脚步地板次序、变形、语音占用／防重、完整混音、空间声像、过场影片声音及端到端延迟；[音频边界](docs/gameplay/ui/CLASSIC_HUD.md#esc-菜单) |
| 自然生成 | server/population唯一执行自然生成，普通／精英／任务怪物及NPC共用Actor构造、发布与休眠恢复；地面物品JM休眠；整层资源按原11帧检查释放；原生GUID与内部句柄分离 | 特殊AI／光环组合、原生分配与全局交错；[人口](docs/gameplay/monsters/POPULATION.md#自然生成工作总览)、[物件](docs/gameplay/world/OBJECTS.md#自然生成源码边界) |
| 天气 | 客户端雨雪四阶段、风向、水花、雷电及循环雨声；Act 1河流物件；第五幕雪区规则 | **开局阶段按用户授权随机选取，是产品调整，不是原版规则**；没有服务端天气同步，同局各客户端可能不一致；声音空间定位、环境声与原版音画对照；[天气](docs/gameplay/world/LIGHTING.md#天气与河流水面)、[河流](docs/modules/MAP.md#act-1河流与天气) |
| 首领绘制 | 五幕首领原COF及组件资源，墨菲斯托雾气、王座巴尔护罩、都瑞尔死亡血迹与透明混色 | 缺失原图（如41的DD）及完整画面对照；[首领绘制](docs/gameplay/monsters/BOSSES.md#五幕首领绘制) |
| 移动一致性 | 角色本地／权威动作隔离、重复坐标请求、权威范围与超界反馈、出口／门户目标准备、跑步耗体力与零体力转换；5F目标资格、怪物路径与开门碰撞、逐帧击退 | 客户端资源更新与转换时序、动态碰撞历史、IDAStar0／16、专用AI／伙伴路径、技能位移；按角色→NPC／怪物→其他位移顺序推进；[移动一致性审计](docs/modules/MAP.md#移动一致性审计边界) |
| 调试任务AI | 五幕27项任务配方及传送点目标，按实际私人任务条件推进；探索统一使用碰撞边界与单位观察边界上的待探通路，完整碰撞、单位指派范围和小地图图元分别记录；规划叠加临时占位，交互／接敌使用可用站位；唯一默认`debug-ai.json`，按幕／任务面板编号选择，命名配置独立读取 | 任务完成不认证全部地图／角色／恢复组合；自动练级与通用装备选择未实现；[调试AI](docs/development/DEBUG_AI.md)、[待验收清单](docs/development/ORIGINAL_GAPS.md#待验收调试导航) |

## 保存边界

角色持久数据由宿主保存，临时游戏状态不写入角色存档。窗口关闭等待原保存退出链，未确认退局的错误会取消关闭；本地角色租约与原子写盘使用进程持有的文件锁。异常退出释放、旧版目录锁及失败恢复仍待运行对照。格式、校验、拒绝与恢复规则见[存档](docs/modules/SAVES.md)，探索缓存见[自动地图](docs/gameplay/world/AUTOMAP.md)。原版移动、任务通知、NPC／怪物投影及佣兵属性的调用依据见[原版协议与任务依据](docs/resources/NATIVE_PROTOCOL_EVIDENCE.md)；不以历史包数或任务通关推导完整协议一致性。

## 文档维护边界

本页保持简短，只替换能力概况与主要限制。不放协议编号、算法、公式、资源参数、逐项功能清单、构建／打包／运行记录或调试过程。原版依据归负责专题或[资料来源](docs/resources/THIRD_PARTY.md)，未解决缺陷归[原版剩余工作](docs/development/ORIGINAL_GAPS.md)，修改历史由Git追溯。
