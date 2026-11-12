# 项目基线

本页概述 D2X 的当前能力、架构边界和主要缺口。实现、原版依据及验收条件见[文档目录](docs/README.md)，PD2 专项见[PD2 能力状态](docs/pd2/STATUS.md)。

## 总体阶段

战斗重构处于 R1–R5 迁移后的公共底座收尾阶段。内容准备、权威执行和公共计算已分层，非玩家实体、属性与资源、持续效果、动作帧事件及原生接触已有公共执行路径。

待完成的公共底座包括世界休眠重建、队列 1／2 全局交错及逐次随机轨迹、属性／费用／死亡回调、专用飞弹与世界载体，以及装备／伙伴组合。德鲁伊、刺客和后三幕的专用 AI 与首领仍依赖这些工作和逐项对齐。

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
| 战斗／技能 | 普攻、法术、状态、武器效果及PvP；女巫、亚马逊、圣骑士、死灵、野蛮人技能；刺客蓄气／陷阱／伙伴施法及德鲁伊变形／召唤已有执行入口；尸体技能起手资格与选取 | 全技能与来源组合、完整双持与触发组合、完整PvP规则；[女巫](docs/gameplay/skills/SORCERESS.md)、[死灵](docs/gameplay/skills/NECROMANCER.md)、[刺客](docs/gameplay/skills/ASSASSIN.md)、[阵营](docs/gameplay/combat/FACTIONS.md) |
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
| 移动一致性 | 角色本地／权威动作隔离、落地位置修正保留动作时钟、重复坐标请求、权威范围与超界反馈、出口／门户目标准备、跑步耗体力与零体力转换；5F目标资格、通用受阻目标准备、客户端怪物碰撞续算、MPQ开门寻路掩码、IDAStar0／16公共算法与逐帧击退；在线地图快照真实房间输入、客户端常驻碰撞与全局逐tick推进、宿主房间／事件／释放顺序；伙伴跨区攻击和零售闲逛中心修正 | 本轮源码尚未构建／运行；逐条收包历史、专用技能／飞弹交错、IDAStar房间／随机组合、全伙伴AI与跨区运行对照；[移动一致性审计](docs/modules/MAP.md#移动一致性审计边界) |
| 调试任务 AI | 五幕 27 项任务配方与传送点目标，按私人任务状态恢复；AI及子寻路只持只读观察和客户端功能接口，不持会话或副本更新权限；目标搜索按本轮单位核查与配置的等待／范围推进，找树、延迟首领及红门共用；延迟目标一轮未找到后继续有界观察，仅重访已退订的核查范围；长程路线与传送目标只用已取得地形／物件，动态单位占位用于城镇／野外原生局部步行绕行，步行出口按实时可通开口连续穿越，出城无额外等待、首次传送失败先走一格再试，入口及坐标中继停滞跨重规划有界，近距单位交互按原生路径终点资格交接手动点击的接近／寻路，修正落点从实际位置复验后续接；步行路线纳入实际确认的传送台连接，传送角色排除传送台；旅行按各区域步行／传送资格和入口位置估价，补给后在城内重新判断任务目的地，蓝门与传送点共用旅行选择；远程等待时按小范围与攻击射线主动防御，任务方块合成先回城；局部执行区分步行清路、传送途经和空挡入战，清路只将当前阻挡列为攻击目标；父子导航使用类型契约，导航／战斗／补给／任务状态分属唯一对象，选敌、攻击、辅助和站位分开实现；使用地图图元、经验方向及城镇服务地标辅助探索 | 自动练级、通用装备选择待补；通用范围搜索、传送占位分层、修正落点续接、等待防御／城镇合成及旅行／补给调整的运行组合尚待验收，局部执行及跨地图、职业与恢复组合待实机验收；[配置与策略](docs/development/DEBUG_AI.md)、[待验收清单](docs/development/DEBUG_AI.md#待验收调试导航) |

## 保存边界

宿主持久化角色，游戏临时状态随实例结束。正常关闭等待保存退出确认，本地角色租约由进程文件锁持有。失败恢复边界见[存档](docs/modules/SAVES.md)，客户端探索缓存见[自动地图](docs/gameplay/world/AUTOMAP.md)。

## 文档维护边界

详细规则、原版依据和已知限制由[负责专题](docs/README.md)维护，原版一致性缺口汇总于[原版剩余工作](docs/development/ORIGINAL_GAPS.md)；未定位的通用故障见[运行问题](docs/development/RUNTIME_ISSUES.md)，调试工具缺陷归[调试AI](docs/development/DEBUG_AI.md#已知运行问题)。修改历史由 Git 追溯。
