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
- 当前激活是原版房间状态模型的适配，尚非完整 DRLG 客户端／服务器四级调度。

## 物品与存档

- LoD 掉落数据含 852 个原表 TC 和 160 个 ItemTypes 自动类别；怪物 TC 引用在生成类别后重新按名称解析，不把运行时数组下标存入存档。自动类别使用原 weapons/armor/misc 行顺序和主类型 rarity，排除任务物品及不允许生成的条目，投掷药剂不混入其他类别。
- `selectTreasure` 消费类型化 TC 与调用方种子，可在叶子回调中执行品质判定并停止遍历；纯查询不改变会话状态。单人 NoDrop 未做人数缩放，旧 1.04 数据不套用 LoD 算法。`planConsumableLoot` 最多规划六件支持消耗品；遇未知实例整批暂缓并丢弃候选，保留已消费随机状态而不重抽。入口本身无法解析时不抽取。死亡 ID 仍只结算一次，暂缓批次不会补发。
- ItemRatio 在 LoD 源存在时读取并纳入内容指纹；缺表时游戏显示暂缓原因，不补造比例。品质请求规则独立于存档 ItemQuality，超强／劣质等尚不能创建的品质不会被编码成普通。`createItem` 接受可信物品等级参数，掉落实例使用解析后的等级，初始物品仍为等级 1。
- `resolveMonsterLoot` 只读真实身份、MonStats、SuperUniques 和 Levels，返回 Ready／Empty／Deferred、基础 TC 名称、物品等级及 TC 升级等级；不读取替身 MonsterKind。普通难度升级等级固定为零，噩梦／地狱仅非 boss／非 noRatio 怪物可升级。任务条件缺失和随从等级归属不明明确暂缓，不当作 NoDrop；死亡日志不编码到存档，已结算 ID 仍按原语义保存。
- `ItemDefinition` 是只读原表定义；`ItemInstance` 是带稳定 ID 的实例。
- 位置由地面区域或容器格子唯一确定；转移通过 `InventoryService`，禁止 UI 直接改归属。
- 私人箱访问权是临时交互状态，不能通过存档恢复远程访问。
- `.d2xsave` v8 在玩家字段末尾追加 gold 钱包，其余物品数量、等级、ID、掉落随机状态及已结算 ID 沿用显式编码。金币地面物品使用 quantity，禁止放入普通容器；拾取最多补到角色等级乘 10000，余额留地面，满额拒绝。当前等级仍为 1，钱包上限 10000。
- 读取先完整校验，再提交状态。地图配置、内容／规则指纹不匹配则拒绝；旧 v1–v6 不迁移。防具实例防御必须落在原基础范围，非防具不得带防御值；装备派生结果在恢复提交前重算验证，不单独编码。
- 当前规则标识为 `d2x-session-rules-v26-gold-stack-loot`，格式 v8，旧 v1–v7 不迁移。金币基础数量为物品等级加 [0,5 倍等级) 随机值，支持 TC 的 gld,mul=N 按 N/256 整数缩放；当前超过原 gld.maxstack 的金币堆明确暂缓，不截断金额。箭袋按 minstack/maxstack，其他已支持普通消耗品按 minstack/spawnstack（无效 spawnstack 回退 maxstack）生成，随机上界不含。数量使用会话掉落随机流，是项目适配，不宣称原物品独立种子流一致。
- 装备 ContainerLocation 的 cell.x 表示部位而非背包矩形，cell.y 固定零；腰带仍使用 BeltEquipment。读档校验钱包上限、金币位置、数量、部位、普通品质、需求、职业与手部组合。普通野外配方身份 outdoor-v6、牛场 cow-v1 不变。金币加成、银行、死亡扣金和投掷武器实例仍暂缓。
- LoD ItemTypes 导入 BodyLoc、Class、Equiv1/2、Shoots/Quiver 与双手标记；StaffMods 不作为职业限制。旧数字 ItemTypes 格式未适配完整装备规则时明确拒绝穿戴。需求当前取原 Barbarian 初始力量／敏捷和等级 1，不包含成长或装备加成；复杂品质与投掷药剂不进入普通穿戴事务。
- 普通武器消费原基础伤害与 StrBonus／DexBonus，盾牌消费 block 及 CharStats.BlockFactor。普通难度普通怪物 A1 准确率按 noRatio 直接取 A1TH，否则取 MonLvl 的 L-TH 乘 A1TH 百分比；缺原数据时不猜值。当前仅接命中率，不改变已有 MVP 怪物生命、伤害、速度及技能。
- 非堆叠普通装备耐久变化增加实例版本并发布 DurabilityChanged，立即重算派生属性。玩家随机流负责伤害、格挡和耐久，怪物随机流负责自身命中；种子分配是本项目适配，不保证原版全局随机顺序一致。
- `--load` 启动时使用存档地图种子与难度重建地图；自选 `--preset/--variant` 仍需保持同一配置。
- 临时输入、拖拽、出口／拾取／交互请求不保存；对应的自动寻路在快照中清除。
