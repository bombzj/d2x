# 项目基线

PD2专项内容：[能力状态](docs/pd2/STATUS.md)。本页保留原版／公共规则及必要的版本边界。

本页只维护当前能力概况、架构边界和主要缺口。具体实现、原版依据及适用范围见[文档目录](docs/README.md)。

战斗重构处于R1–R5交错迁移后的公共底座收尾阶段，尚未完成整体范围验收。内容准备、权威执行和公共计算已分层；非玩家实体、属性／资源、持续效果、动作帧事件及原生接触已有公共消费者，但不代表全部程序、来源与组合完成。

主要缺口是世界休眠重建的组合验收、队列1／2全局交错及逐随机轨迹验收、全部属性／费用／死亡回调、剩余专用飞弹与世界载体、装备／伙伴组合，以及既有内容迁回后的逐项对齐。德鲁伊／刺客、后三幕专用AI／首领及特殊程序仍受这些底座门槛约束；不能以历史技能数量或公共函数存在宣称完整复刻。

实际阶段、优先级和完成条件唯一见[战斗重构进度与剩余步骤](docs/architecture/MULTIPLAYER.md#23-战斗重构实际阶段与剩余步骤)。执行入口和迁移边界见[内核子系统](docs/modules/SERVER_SYSTEMS.md)、[技能路径核对](docs/gameplay/skills/COMMON.md#技能执行路径源码核对)及[持续效果公共机制](docs/gameplay/skills/COMMON.md)；数值入口见[公共规则](docs/architecture/COMBAT_SHARED_RULES.md#重点战斗数值的当前公共入口)，原版证据见[资料来源](docs/resources/THIRD_PARTY.md#原版113c静态与开发对照证据)。本页不复制逐程序迁移清单。

## 产品与代码边界

D2X提供单机、TCP/IP和原服连接，共用一套客户端。自研宿主负责权威玩法，客户端读取状态并提交请求；断线不切换权威来源。

宿主负责内容准备、协议适配和角色存储，玩法内核不依赖资源加载、网络、文件或显示设备。资源与规则以当前MPQ为依据，未实现行为明确拒绝。模块职责见[架构速览](docs/architecture/OVERVIEW.md#agent速览)，协作约束见[AGENTS.md](AGENTS.md)。

服务端战斗目标以[复刻目标架构](docs/architecture/SERVER_COMBAT_RECONSTRUCTION.md)为准；阶段准入和后续顺序由上述总计划维护，目标设计不能当作当前能力。性能优化必须保持有效输入下的数值、随机消费和执行次序，不能用降频、合帧或近似算法换速度。

## 当前实现范围

| 领域 | 能力概况 | 主要缺口／专题入口 |
| --- | --- | --- |
| 连接／宿主 | 单机、TCP/IP、原服连接及独立游戏服务；显式无窗口原协议诊断入口可组装房主及多个真实客端 | 完整故障恢复；[联网](docs/modules/NETWORK.md)、[独立服务](docs/development/PVPGN_SERVER.md)、[无窗口入口](docs/development/BUILD_AND_RUN.md#无窗口协议客户端) |
| 地图／世界 | 五幕地图生成及客户端房间重建；宿主按每幕逐房间物化，已核对原版瓦片／静态碰撞及房间调用链；连续跨区寻路、服务端原生常驻碰撞格及逐子格占位维护、多人出生／旅行／复活寻位、按路径结束切换走跑动画、探索缓存及常见环境物件 | 原版混合客户端、更广泛多人交错及单位随机／恢复／资源回收仍待验收，特殊内容仍有缺口；[地图](docs/modules/MAP.md)、[物件](docs/gameplay/world/OBJECTS.md) |
| 物品／成长 | 库存、装备、掉落、商店、合成、角色成长及死亡／尸体；掉落双流、冻结批次、逐操作属性及指定词缀已有实现；原版人物27场景的已采面板／权威属性及七个数值提示对照范围见[对照存档](docs/development/CHARACTER_REFERENCE_FIXTURES.md) | 原版同帧出生／逐件落点时序、全来源／属性组合仍未精确认证，复杂争用与多尸体；HC创建、永久死亡、模式隔离及授权回收已接，完整组合待核对；[物品](docs/gameplay/items/README.md)、[人物](docs/modules/CHARACTER.md) |
| 战斗／技能 | 普攻、法术、状态、武器效果及PvP；女巫、亚马逊、圣骑士、死灵、野蛮人技能；刺客蓄气／陷阱／独立伙伴施法及德鲁伊变形／召唤等已有执行入口；尸体技能已补起手资格、SelectProc选择、SearchEnemyNear落点附近搜索、鼠标范围及尸体名称；触发效果通知保留当前动作 | 全技能与来源组合验收、完整双持与触发组合、完整PvP规则；见[死灵](docs/gameplay/skills/NECROMANCER.md)；[刺客进度](docs/gameplay/skills/ASSASSIN.md)、[战斗](docs/gameplay/combat/FACTIONS.md) |
| 怪物／伙伴 | 第一、第二幕家族及第三至第五幕逐项程序已有接入，含NpcBarb／囚犯Fn131、HellMeteor／投石机客户端程序、王座Baal波次／Diablo命令恢复及迷乱随机借用；零售死灵已接Do31先删尸体继续点、Fn067／PetMax原回调及Do58初始化／主牵引／UMod21（Windows Release已构建，运行组合待验收） | NPC休眠命令、光环与修饰器重建已接，组合待验证；全局交错／逐随机及全难度组合尚未验收；本机1.13c的Uber默认AI／高级方块门保留空分支，本机没有已核实的Realm World Event自然触发消费者，不新增SoJ阈值或替换规则。范围见[怪物目录](docs/gameplay/monsters/README.md#当前实施范围)，具体覆盖见[逐项台账](docs/gameplay/monsters/ACT3.md)、[典型行为消息边界](docs/gameplay/monsters/PRESENTATION.md#普通移动攻击与复活通知)；[佣兵](docs/gameplay/characters/HIRELINGS.md) |
| NPC／任务 | 城镇服务、原DS1 MapAI移动与动作、固定站位及交谈暂停；Cain／Anya正常退场蓝门与直接DD、熔炉逐批掉落及邪恶洞窟清场FX／客户端30帧光束与底光衰减、熔炉碎片／灵魂、Nihlathak原FX17／82、混沌清场DT／FX12与任务计时及附着单位声音槽已有新源码；杰海因／守卫、奥玛斯／艾柯、凯恩／安雅返城专用分支及459／461／543路径转交；五幕个人任务主流程、组队任务及奖励保存；原任务日志已阅回执及完成动画消费 | 完整剧情时序／退场表现、机关和特殊多人任务仍待对照。范围见[NPC交互](docs/gameplay/npc/INTERACTIONS.md#宿主npc移动与地图动作)、[NPC／任务](docs/modules/NPC_QUEST.md)、[任务](docs/gameplay/quests/SYSTEM.md) |
| 多人／表现 | 组队、聊天、交易、敌意及共享收益；物品提示、库存原调色／镶孔叠层／无形混色、自动地图大小／定位／平移及PL2 Fade；大面板原资源、M键消息日志、局内聊天原代码页／语言与Unicode编辑／Windows IME、头顶消息原字体／底板、Holy Shield的hsh组件、零售跳跃定点高度、组队按钮、人物／技能按压及原表背包布局（新增表现路径已构建，行为仍待实测）；ESC各级菜单按所选语言加载原图、Sound／Music／NPC Speech、Lighting On／Off设置与未支持设置置灰；包裹即时拿起、放置、原快捷键／使用、方块收纳、鉴定准备、堆叠与购买／金币输入已有实现，移除拆分及额外快速转移／右键装备，见[包裹界面](docs/gameplay/items/INVENTORY_UI.md#操作) | 完整像素／状态对齐、特殊任务事件、硬核权限完整组合、完整本地化／输入法组合与部分原版表现；[联网](docs/modules/NETWORK.md)、[UI](docs/gameplay/ui/CLASSIC_HUD.md)、[自动地图](docs/gameplay/world/AUTOMAP.md)、[物品提示](docs/gameplay/items/PRESENTATION.md#物品属性提示) |


已接能力不代表全部组合或原版客户端兼容性完成；具体支持条件和未覆盖范围由对应专题维护。

客户端Configure Controls已接主／副键编辑、原PL2状态色、草稿取消／默认／应用及全角色共享偏好保存，技能输入投影扩为16槽；技能循环按原版右手快捷槽顺序接入，原版角色语音键已接请求与原声音消费，实机组合与重启保存仍待验证，见[UI](docs/gameplay/ui/CLASSIC_HUD.md#esc-菜单)及[客户端偏好](docs/modules/SAVES.md#客户端偏好凭据与探索)。

世界悬停／点击使用原COF可选组件、原cell帧范围及专用表矩形，覆盖普通物件、NPC／怪物／尸体与地面掉落；NPC姓名读取原pixHeight与font16基线，玩家／玩家尸体姓名沿原72像素高度及同一字体框。祭坛领取标记、诅咒等覆盖图与NPC感叹号读取OverlayHeight及原Overlay偏移，跟随单位图形／抬升；头顶聊天沿地面锚点。此前选取、姓名及覆盖图定位修正已通过Windows Release构建；后续静态物件屏幕锚点及Q任务面板原字体／正文基线修正尚未构建／运行，详见[任务日志](docs/gameplay/quests/SYSTEM.md)。此前EXE仅实际检查营地恰西本体交谈、私人箱本体开启及地面卷轴本体拾取，不认证新源码。原版运行对照、重叠目标及全部动画仍待验收，依据与边界见[世界选取](docs/gameplay/world/OBJECTS.md#世界悬停与点击选取)、[NPC姓名](docs/gameplay/npc/INTERACTIONS.md)、[状态覆盖图](docs/gameplay/skills/COMMON.md)与[玩家姓名](docs/modules/NETWORK.md#组队邀请客户端)。

自然生成由server/population唯一执行，hosting只准备当前MPQ规则、原型和预设描述；旧人口规划器与预掷出生队列已移除。普通怪物、精英／仆从、任务怪物及NPC共用Actor构造、身份／房间发布与休眠恢复继续点，任务清场共用实时生成覆盖查询。地面物品已接JM休眠及房间登记，怪物死亡模式与HP分离；混合房间链逐单位准备、发布与退役，背压保留同帧继续点，活动瓦片按房间实际释放。内部唯一句柄与按类型原生GUID分离，目标排序／种子消费者使用原生身份，回收保留效果、技能、弹体及休眠引用；区域准备包在同一挂载内共享不可变DS1／碰撞库。真实类型碰撞／替身、中立预设、囚犯激活、随机物件和共享生成流范围见[人口](docs/gameplay/monsters/POPULATION.md#自然生成工作总览)及[物件](docs/gameplay/world/OBJECTS.md#自然生成源码边界)。整层资源按原11帧检查与引用边界释放，重入以新票据复验并保留原人口进度；精英双次光环指定、普通修饰器追加及掉落失败身份消耗已接。完整特殊AI／光环组合、原生分配与全局交错仍待运行对照；源码补缺与待测边界见[原版剩余工作](docs/development/ORIGINAL_GAPS.md)。

## 保存边界

角色持久数据由宿主保存，临时游戏状态不写入角色存档。窗口关闭等待原保存退出链，未确认退局的错误会取消关闭；本地角色租约与原子写盘已改用进程持有的文件锁，名册检查实际占用。关闭等待、异常退出释放、旧版目录锁及失败恢复仍待运行对照。格式、校验、拒绝与恢复规则见[存档](docs/modules/SAVES.md)，探索缓存独立见[自动地图](docs/gameplay/world/AUTOMAP.md)。

## 文档维护边界

本页保持简短，只替换能力概况与主要限制。不放协议编号、算法、公式、资源参数、逐项功能清单或构建／打包／自研运行记录。详细内容归负责专题，操作方法归[开发指南](docs/development/BUILD_AND_RUN.md)，修改历史由Git追溯。
