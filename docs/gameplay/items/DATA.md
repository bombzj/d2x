# 原版物品与掉落数据

当前使用资料片 `lod-named-txt-v1` 原表适配，运行时从 MPQ 读取定义与美术。既有本机清单为 659 条物品、734 条怪物、852 条 TreasureClassEx，另从 ItemTypes 生成 160 个自动类别；条数是这套资源的摘要，不是所有条目都能执行的保证。试玩 1.04 数字类型／旧 TC 适配已退出运行路径。

## 导入与执行边界

| 数据 | 原表／职责 | 当前入口 |
| --- | --- | --- |
| 基础物品 | Weapons／Armor／Misc、ItemTypes、Belts、Inventory；代码、占格、数量、耐久、需求、装备位、外观与原属性 | `content/items`、`gameplay/items` |
| 品质／词缀 | ItemRatio、MagicPrefix／Suffix、RarePrefix／Suffix、UniqueItems、SetItems、Sets、QualityItems、LowQualityItems | 原行及掷值进入实例，不由说明文字反推 |
| 属性 | Properties、ItemStatCost | 内容适配提供类型化统计值，装备、说明与定价复用；消费者支持见 [效果清单](SUPPORT.md) |
| 消耗品／初始物品 | Misc 的 pSpell／stat／calc／len、Books 配对、CharStats 的初始物品列 | `item_consumables.*`、书页事务与角色创建 |
| 掉落入口 | MonStats、SuperUniques、Levels 的真实身份与 TC 引用 | `resolveMonsterLoot`；不按敌对外观替身决定掉落 |
| TC | TreasureClassEx 的 Picks、NoDrop、group／level、ItemN／ProbN、品质修正 | `selectTreasure`；原空引用不改造为自定掉落池 |

DataTable 保留列顺序、重复列名和空值，字段名按 ASCII 忽略大小写匹配，调用方可选择同名列的 occurrence；非空整数必须完整解析，不能接受数字后的多余字符。`number()` 对空值／未找到列返回 nullopt，但不少调用方仍使用 `value_or(0/1)`，不能据此声称所有缺列都明确拒绝。关键表头只作各消费者列出的部分校验；未引用的列保留在原表中，不等于已解释语义。

## 物品参数覆盖复核（2026-10-04）

直接用已有资源工具从当前 `assets/mpq2` 挂载顺序提取物品原表及 Properties，结合本地 D2MOO 核对；导出物仅在忽略的 `artifacts/item-audit-*.txt`。镶嵌随后完成 Windows Release 构建和有限实机冒烟，范围见 [支持清单](SUPPORT.md)；未新增测试脚本或打包。

| 原数据 | 已解释／消费者 | 仍缺或受限 |
| --- | --- | --- |
| Weapons／Armor／Misc | code／namestr、type／type2、invwidth／invheight、invfile／flippyfile、stackable／maxstack、durability、需求、三组伤害、minac／maxac、StrBonus／DexBonus、speed／rangeadder／hit class、block、gemsockets、gemapplytype、quest；保留 sourceTable／sourceRow | 自动词缀 auto prefix、天然 staffmods、gemoffset 图标叠层、天然无形生成与完整视觉；并非所有美术、声音或效果列均消费 |
| ItemTypes | Equiv1／2 递归继承、BodyLoc、职业、Shoots／Quiver、Throwable／Repair／AutoStack、品质标志、MaxSock1／25／40、VarInvGfx／InvGfx、TC 类别；sock 继承链用于填充物校验 | StaffMods 等未形成天然技能生成规则 |
| Gems | 运行时加载 68 个带 code 条目，含 35 种宝石／骷髅及 33 种符文；按三组 ModCode／Param／Min／Max 准备类型化属性，宿主 gemapplytype 选择组 | transform 染色与孔位叠层未消费。当前 scalar 掷值固定；若修改表出现 compact D2S 无字段保存的变量标量，则明确拒绝加载，不静默重掷 |
| Runes | 加载 78 条 complete=1 原行的 server、itype／etype、Rune1…6、T1Code／Param／Min／Max；检查品质／类型／满孔／顺序，属性一次掷值、独立保存；名称及 D2S 身份取原 TBL | server 为元数据，当前没有天梯／联机模式过滤；触发施法、充能施法、装备光环等消费者仍有限，不能把全部原行解析等同全部效果生效 |
| Cubemain | 原表有 146 条 enabled=1；绑定法杖／克林姆、8 条加孔相关配方及 1 条去镶嵌，孔配方读取类型／品质／qty、特殊暗金身份、输出等级及孔数原范围 | 其余语法、op、ladder／class、任意属性与多输出仍无通用解释器；不支持的孔配方限定明确报错，详见 [方块](CUBE_AND_GOLD.md) |
| UniqueItems／SetItems／Sets | 原行、等级、需求、权重、属性指令／掷值、图形覆盖、nolimit、carry1、套装件数与部分分级加成 | 生成目录筛掉 disabled、version>100 和 lvl>99；carry1 另从完整 enabled 原行建立，避免事件行遗漏。ladder 虽进入记录，当前没有天梯模式，不能称为完整模式过滤 |
| MagicPrefix／Suffix、RarePrefix／Suffix、QualityItems | 等级／类型／职业／组／frequency、原前后缀与稀有名称、优质／劣质选择及掷值 | 部分被过滤原行不会进入实例目录；失败暗金／套装额外耐久、完整原生成顺序及所有属性指令未完成 |
| Properties | 保存最多七组 func／stat／set／val；解析 func 1–11、14–17、19–24；func14 写实际孔数，触发／充能编码技能层，符文之语 func23 设置无形物理标志 | `set` 没有通用解释；其余未知 func 仍会跳过，不能声称整件属性都有效。原技能属性可编码／保存不等于技能已执行；触发／充能的完整报价仍暂缓 |
| ItemStatCost | ID、说明优先级／函数／文本、部分 op、原 Save Bits／Param Bits／Add，用于说明、按等级效果和 D2S | 当前只换算以 level 为基准的 op 2／4／5 与 op stat1；op stat2／3 和其余变换没有通用执行。说明没有覆盖所有 descfunc／组合说明；保存已知 stat 不代表玩法消费该 stat |
| Misc.pSpell／stat／calc／len、Books、Belts、Inventory、CharStats | 已支持药效／卷轴／书页、腰带容量、箱子／方块布局、初始物品，运行时取原表；药剂基础值在消费时应用原职业倍率 | 任意 calc 表达式、全部 pSpell、玩家药剂暴击／恢复叠加平均、完整原图行为仍未完成；以 [消耗品](BELT_AND_CONSUMABLES.md) 为准 |

这些条数按当前挂载原表的字段筛选统计，未宣称所有行可生成或可执行。当前不能得出“MPQ 物品参数已全都准确识别”的结论；应逐层区分原字节读取、字段绑定、规则求值、玩法消费者、提示及存档。

## 选择、品质与实例

TC 支持单人正 Picks 权重／NoDrop、负 Picks 顺序展开、嵌套、四项品质修正继承及根 TC 升级。人数缩放尚未接入。纯查询不创建实例，品质查询使用 ItemRatio；游戏共用规划器在叶子判定品质、生成已支持实例，一批最多六件。

暗金候选按原行排重，`nolimit` 行不加入已用集合；这与角色 `carry1` 是两条独立规则。已有品质失败／无候选分支会降级；不支持的叶子在 `planItemLoot` 记 deferred、跳过自身并继续后续选择，保留已生成物品和已消耗随机状态，不将暂缓当 NoDrop。当前词缀／等级生成器返回 deferred 时仍有降级分支，不能把所有暂缓都称为原版品质失败或完整“不重抽”保证。死亡实体 ID 只结算一次。结算资格与执行顺序见 [奖励模块](../../modules/REWARDS.md)，实例／位置规则见 [物品模型](MODEL.md)。

原表分支权重不是最终物品概率：递归类别、掉落次数、品质、MF、难度和资格会继续影响结果。旧自定“不掉落／额外掉一件”概率已删除，不用缺省参数填补原规则。

## 查询入口

在项目根使用已有资源工具；这些命令读取资源，不作为新的游戏验证记录：

```powershell
.\build\bin\d2x_assets.exe assets/mpq2 item hax
.\build\bin\d2x_assets.exe assets/mpq2 drops fallen1
.\build\bin\d2x_assets.exe assets/mpq2 treasure "Act 1 Champ A" 210
```

其他查询包括 quality、special 和 loot-plan；是否有数据／图形／实例生成／战斗效果分别核对。原包来源及摘要见 [MPQ](../../resources/MPQ.md)，算法借鉴与许可见 [资料来源](../../resources/THIRD_PARTY.md)。
