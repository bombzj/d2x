# 数据与生命周期

## 原始资源

- 当前来源：`assets/mpq2/{d2data,d2char,d2exp,d2sfx,Patch_D2}.mpq`。
- `Archives` 统一路径与包优先级；完整源与试玩不混用。运行不需要原版 EXE。
- `lod-named-txt-v1`、`classic-1.04-txt-v1` 是表结构适配标识，不代表逐补丁等价。
- 原始素材不受项目 GPL 许可覆盖，不提交 Git。

## 世界

`Levels/LvlPrest/LvlTypes/LvlMaze/LvlSub/LvlWarp → WorldCatalog → WorldPlan → MapRecipe → Map → Region`。

- `Levels` 决定关卡身份；户外邻接另需原引擎连接规则，不能只读 Vis。
- DS1 保存布局、原生单位和标记；DT1 保存像素与子格碰撞。没有地图 JSON。
- DS1 替换方法与分组独立保留；版本 12 起读取分组边界，13 起读取变体数，18 起跳过分组前额外字段。边界模板用墙序列编码匹配／替换宏格，不作为地形图片叠加。
- DT1 的无图像记录仍保留原瓦片键、权重及碰撞，不因没有像素而从查找表删除；兵营物件库的柱体 9/25/12、9/26/12 使用该语义，不生成替代图片。
- `MapRecipe.pieces` 表达原模板及平移，拼接时共享 DS1 的额外边行，不拉伸图像。
- `MapRecipe.blankAreas` 清除基础草地而不删除相邻 DS1 边缘；`floorOverrides` 在原预设合并前写入道路地板。外部空白不创建空地刷怪房间；这些是重建配方，不增加存档编码字段。
- `outdoor_layout` 管区域坐标；`outdoor` 管矩形边界和原预设；`maze` 管 Cave/Crypt/Barracks/Jail/Catacombs 房间图。兵营连接房使用原表固定变体，即使 `Files=0` 也不能遗漏其三个定向资源。
- `MapRecipe::Boundary::plane` 为可选的局部连接平面；缺省仍使用地图外边缘。兵营与外侧回廊按原版偏移对齐，两侧均可行走的位置才作为跨区入口，不移除原图碰撞。
- `exits` 解析隐藏与可见出口，按 `Vis/Warp` 关联；`session_exits` 管走近、跨区和返回。
- 地图种子默认 210；`--map-seed` 与刷怪种子、掉落种子独立。难度影响迷宫房间数。
- 地形目前启动时整体装载。房间激活只管理怪物，不是完整地形流式加载。

## 怪物

`原表与房间 → PopulationPlan → AreaState.pendingSpawns → 附近房间成组实例化 → Enemy`。

- 先计算计划；待生成单位没有实体 ID、AI 或小地图标记。
- `RoomLayout` 以所在房间及相接房间判断附近范围；不是屏幕像素距离。
- 原生群组成组创建一次。远处已创建单位保留状态、停止 AI 和动画计时。
- 切区保存整个 `AreaState`；回来不重刷。`R` 才主动重置当前区。
- 真实怪物／首领身份与运行替身类型分离；掉落请求使用真实身份。
- 基础行走速度读取真实身份的 MonStats.Velocity，按 `(Velocity * 256 * 75 / 100) * 25 / 4096` 转换为子格／秒；保留整数阶段截断。怪物独立奔跑、精英加速及原冰冷修正尚未接入，不能把此项当作完整原移动模拟。
- 普通怪物及其普通随从的三难度生命、A1／A2 伤害与命中、护甲、暴击、再生和六项抗性由运行时 `MonStats`／`MonLvl` 共用解析；缺少 A1 等列时仍使用该行其余原值。`MonStats.AI/aip1–aip8` 按难度读取，已实现普通骷髅、僵尸、沉沦魔和 Brute 的基础决策，以及 Zombie 受击／埋骨之地追击；完整逐帧 AI、远程技能、元素出招和特殊行为仍待实现。已实现外观的 A1、Brute／骷髅／僵尸／沉沦魔 A2 时长与命中帧从 `AnimData.d2` 读取。Brute、Zombie 与 Skeleton 另有 NU/WL/GH/DT/DD 原动作和各自 `MonSounds` 所列音频。
- 弗拉维为原引擎补充预设单位，不是普通 DS1 列表内的单位：仅血腥荒地边界预设 4–7、变体 3 的预设中心生成；保留中立身份及原 RG 外观，不使用敌对替身。
- 当前激活是原版房间状态模型的适配，尚非完整 DRLG 客户端／服务器四级调度。

## 物品与存档

- 当前格式 v59，规则 `d2x-session-rules-v87-corrupt-rogue4`，拒绝 v1–v58，亦拒绝不同内容／规则指纹。玩家保存职业、四维已分配点及未用点数，必须等于按该职业运行时 MPQ `CharStats.StatPerLevel` 计算的等级总点数；等级与经验按该职业 `Experience.txt` 列校验。技能等级与未用点数总和等于升级次数，按运行时 MPQ `Skills.txt`／`SkillDesc.txt` 校验职业、门槛、最大等级及前置。八个快捷键保存技能 ID 和左右键，读取时校验当前职业身份及左键限制。第一件原初始装备的授予技能按 `CharStats.StartSkill` 校验；角色冰冷剩余时间、毒素剩余时间与每秒伤害及普攻动作时长按当前格式保存；Fallen 逃离状态／路线、同组命令及按 MPQ `AnimData.d2` 校验的 S2 动作时间，以及 Brute 绕行及 Corrupt Rogue 跑步状态／路线一同校验；飞行中的普通与女巫技能弹体保存原 Missile ID、伤害、作用半径和冰冷时长。派生属性、装备词缀和资源上限在恢复前从 MPQ 定义与快照库存重算；私人箱尺寸按 `inventory.txt` 当前模式校验。物品保存品质原行、等级要求、展示属性值与词缀行，掉落状态保存限量暗金记录。WorldState 包含 TownPortalState 和 waypoints；临时走近请求不保存。
- 调试 Travel 不修改解锁记录；普通 WaypointTravel 重新校验源点与目标，不相信 UI 已禁用按钮。传送点 NU／OP（Operating）／ON（Opened）的帧数、速率、循环和起帧来自 Objects.FrameCnt／FrameDelta／CycleAnim／Start；当前难度与角色都固定在同一 SessionSnapshot 中，不提供跨难度迁移。
- 野外传送点固定分组通过 MapPiece.substitutionGroup 裁剪原图层和分组内部原对象，保持每格 DT1 资源作用域；非完整通用 LvlSub。回城蓝门使用原 TP 资源，不写入静态地图对象列表，不分配临时地图对象 ID。

- LoD 掉落数据含 852 个原表 TC 和 160 个 ItemTypes 自动类别；怪物 TC 引用在生成类别后重新按名称解析，不把运行时数组下标存入存档。自动类别使用原 weapons/armor/misc 行顺序和主类型 rarity，排除任务物品及不允许生成的条目，投掷药剂不混入其他类别。
- `selectTreasure` 消费类型化 TC 与调用方种子，原表 `Picks=0` 不抽取，可在叶子回调中执行品质判定并停止遍历；纯查询不改变会话状态。单人 NoDrop 未做人数缩放，旧 1.04 数据不套用 LoD 算法。`planItemLoot` 最多规划六件有原表定义和原图的物品实例，同批次选择限量暗金时登记已选原行，品质候选失败后按已核实顺序降级；遇未知实例整批暂缓并丢弃候选，保留已消费随机状态而不重抽。入口本身无法解析时不抽取。死亡 ID 仍只结算一次，暂缓批次不会补发。
- ItemRatio 在 LoD 源存在时读取并纳入内容指纹；缺表时游戏显示暂缓原因，不补造比例。品质请求规则独立于存档 ItemQuality，超强／劣质等从 QualityItems／LowQualityItems 原行生成展示实例。`createItem` 接受可信物品等级参数，掉落实例使用解析后的等级，初始物品仍为等级 1。
- `resolveMonsterLoot` 只读真实身份、MonStats、SuperUniques 和 Levels，返回 Ready／Empty／Deferred、基础 TC 名称、物品等级及 TC 升级等级；不读取替身 MonsterKind。普通难度升级等级固定为零，噩梦／地狱仅非 boss／非 noRatio 怪物可升级。任务条件缺失和随从等级归属不明明确暂缓，不当作 NoDrop；死亡日志不编码到存档，已结算 ID 仍按原语义保存。
- `ItemDefinition` 是只读原表定义；`ItemInstance` 是带稳定 ID 的实例。
- 位置由地面区域或容器格子唯一确定；转移通过 `InventoryService`，禁止 UI 直接改归属。
- 私人箱访问权是临时交互状态，不能通过存档恢复远程访问。
- `.d2xsave` 当前 v59 显式编码玩家职业、经验、等级、已分配属性／技能与未用点数、F1–F8 绑定、钱包、物品数量／等级／品质原行／已鉴定标志／展示属性、初始装备技能、普通与技能弹体、角色冰冷及毒素剩余时间／毒素每秒伤害／当前普攻时长、怪物最大生命、骷髅、僵尸与沉沦魔 AI 停顿／追击及 Fallen 逃离／命令及 S2 动作状态、Brute 绕行和 Corrupt Rogue 跑步状态、A1／Brute／骷髅／僵尸／沉沦魔 A2 攻击过程、NPC 原路径运动、商人随机货品已售 slot、ID、掉落随机状态及已结算 ID。命名管道定向生成的怪物保存 Debug 来源、原身份、分组和生成键，并沿用敌对身份、生命范围与地图位置校验。金币地面物品使用 quantity，禁止放入普通容器；拾取最多补到角色等级乘 10000，余额留地面，满额拒绝。各职业等级阈值来自运行时 MPQ `Experience.txt`，成长来自 `CharStats.txt`；击杀经验按 `MonStats`／`MonLvl`／`Experience` 及原引擎等级差规则结算。
- 读取先完整校验，再提交状态。地图配置、内容／规则指纹不匹配则拒绝；旧 v1–v24 不迁移。防具实例防御必须落在原基础范围，非防具不得带防御值；装备派生结果在恢复提交前重算验证，不单独编码。
- 金币基础数量为物品等级加 [0,5 倍等级) 随机值，支持 TC 的 gld,mul=N 按 N/256 整数缩放；当前超过原 gld.maxstack 的金币堆明确暂缓，不截断金额。箭袋按 minstack/maxstack，其他已支持普通消耗品按 minstack/spawnstack（无效 spawnstack 回退 maxstack）生成，随机上界不含。数量使用会话掉落随机流，是项目适配，不宣称原物品独立种子流一致。
- 装备 ContainerLocation 的 cell.x 表示部位而非背包矩形，cell.y 固定零；腰带仍使用 BeltEquipment。读档校验钱包上限、金币位置、数量、部位、品质原行及属性展示值、需求、职业与手部组合。普通野外配方身份 outdoor-v6、牛场 cow-v1 不变。金币加成、银行和死亡扣金仍暂缓；投掷攻击和数量损耗已接源码，待运行验收。
- LoD ItemTypes 导入 BodyLoc、Class、Equiv1/2、Shoots/Quiver 与双手标记；StaffMods 不作为职业限制。旧数字 ItemTypes 格式未适配完整装备规则时明确拒绝穿戴。需求取当前职业由 `CharStats` 与已分配点派生的实际力量／敏捷和等级，受支持的直接装备加值进入需求闭包；有原装备部位的品质物品可穿戴，投掷药剂不进入普通穿戴事务。
- 普通武器消费原基础伤害与 StrBonus／DexBonus，盾牌消费 block 及 CharStats.BlockFactor。普通怪物各难度的现有战斗字段由共用内容解析输入命中、出伤、受击、再生与存档校验；缺原数据时不猜值。已实现外观的 A1 与四种 A2 时序按 `AnimData.d2` 读取；未接入的远程技能和元素出招仍是后续工作。
- 非堆叠普通装备耐久变化增加实例版本并发布 DurabilityChanged，立即重算派生属性。玩家随机流负责伤害、格挡和耐久，怪物随机流负责自身命中；种子分配是本项目适配，不保证原版全局随机顺序一致。
- `--load` 启动时使用存档地图种子与难度重建地图；自选 `--preset/--variant` 仍需保持同一配置。
- 临时输入、拖拽、出口／拾取／交互请求不保存；对应的自动寻路在快照中清除。
