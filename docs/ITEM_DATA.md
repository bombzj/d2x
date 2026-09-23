# 原版物品与掉落数据

## 完整资源适配（最新）

地图阶段新增 `lod-named-txt-v1` 适配器，用户完整包实际导入 659 条物品、734 条怪物、852 条 TreasureClassEx，并读取 `itemtypes.txt`。物品 type 保留命名类型；腰带容量由 armor.belt 指向 belts.txt 的原始行。TC 保留 Picks、NoDrop、group、level、四种品质修正和 Item1–10/Prob1–10，怪物按名称引用 TC，空引用保持为空。

运行时直接从已挂载 MPQ 读取原表和图形，不依赖抽取后的独立文件。`misc.txt` 中的 pSpell／stat／calc／len 现由 `content/item_consumables.cpp` 适配为药剂和传送卷轴的只读定义；`charstats.txt` 决定初始物品代码、数量与装备位置。原表缺失效果字段时不补写物品数值。

`d2x_assets assets/mpq2 drops fallen1` 可查看实际分支。例如普通沉沦魔 TC1 指向 Act 1 H2H A，原表为 Picks=1、NoDrop=100，四个分支权重依次 21／16／21／2。这些是原表权重，不是完整最终物品掉率。

当前 `d2x_assets assets/mpq2 treasure "Act 1 Champ A" [seed] [monster-level]` 执行单人 TC 选择，包含正负 Picks、NoDrop、递归、四项品质修正继承，以及从 ItemTypes／物品原表生成的 160 个自动类别；指定等级时仅升级根 TC。此查询不创建实例，也不执行品质。`quality` 查询执行原 ItemRatio 品质请求；`special <unique|set> <code> <level> <seed>` 查询原 UniqueItems／SetItems 合格行；`loot-plan` 调用游戏共用物品规划器，在每个叶子判定品质并限制最多六件支持候选。源码现可规划普通消耗品、基础装备与投掷堆叠、魔法／稀有词缀、套装／暗金及优质／劣质展示实例，并按原引擎品质顺序在候选生成失败时尝试低品质；属性目前仅展示，暂不参与战斗。投掷攻击／损耗和未核实使用效果的杂项仍不执行效果。本轮新增源码尚未构建或运行，不能将查询路径当作完整最终掉率或逐随机流原版复现，详见核心 STATUS。

以下保留旧 1.04 试玩适配器的说明。两种结构分别识别，启动使用同一套 MPQ，不混表；读取更多数据不表示全部装备和资料片玩法已经实现。

物品不仅有图像。2026-09-22 起，项目在启动时直接从 MPQ 读取物品定义，移除了编译进 C++ 的 `classic_catalog.cpp`。`content/classic_data.cpp` 是明确的经典 1.04 表适配层；物品服务仍只依赖类型化定义，不接触 MPQ 或窗口。

## 本轮已经使用的数据

| 原表 | 当前读取内容 | 接入范围 |
| --- | --- | --- |
| `misc.txt`、`weapons.txt`、`armor.txt` | 共 361 条物品定义 | 代码、名称、类型 ID、占格、堆叠、耐久、使用／入带标志及图形路径直接供物品服务使用 |
| 同上 | 单手／双手／投掷伤害，防御范围、力量／敏捷／等级需求、基础等级、价格、速度、格挡、孔数、稀有度和可生成标志 | 导入为可选数值；装备提示显示部分基础参数，尚未实现完整装备生效、随机防御或词缀 |
| `belts.txt` | 原版各腰带布局的格数 | 容量取原表；1.04 的装备代码到布局名称对应仍在版本适配器中明确列出 |
| `monstats.txt` | 410 条原始怪物记录，三种难度各四个 TC 引用 | 类型化读取并验证引用；不表示已经实现 410 种怪物 |
| `treasureclass.txt` | 95 条旧式 TC，原始 `NumCodes` 与 `Code1..30` | 保留顺序及重复代码，不去重、不自行加权 |

`resources/DataTable` 保留列顺序、重复列名和空单元格。1.04 护甲表末尾存在重复的 `mindam/maxdam` 列；类型化读取指定第一次出现的字段，完整原列仍可查询。缺失数值使用 `optional`，不伪装成已知的零值。此版本 `type` 是数字类型 ID；未找到 `itemtypes.txt`，不能当作资料片的文字类型代码直接解释。

可用正式资源工具查看所有原始字段及怪物 TC 引用：

```powershell
.\build\bin\d2x_assets.exe assets/mpq/d2x-mvp.mpq item hax
.\build\bin\d2x_assets.exe assets/mpq/d2x-mvp.mpq item cap
.\build\bin\d2x_assets.exe assets/mpq/d2x-mvp.mpq drops Fallen
.\build\bin\d2x_assets.exe assets/mpq/d2x-mvp.mpq drops Zombie
```

这些是资源查询入口，不生成物品，不执行掉落，也不是测试脚本。

## 掉落概率的实际情况

当前 1.04 表中，基础 `Fallen` 与 `Zombie` 在普通难度都引用 **1 / 70 / 90 / 0**：Gold、Act1A-HtH、Act1-junk、Null。Act1A-HtH 有 `clb ssd jav sbw tkf hax spc cap qui buc lea lgl lbt lbl`。Act1-junk 中 `hp1` 出现三次，箭矢、弩矢也有重复槽位。它们是原始选择数据，不能据此直接给出“击杀后掉某件物品的最终百分比”。类别选择、掉落次数、空掉落及品质生成仍涉及对应版本的引擎规则。

**旧代码的“20% 不掉落、另有 20% 再掉一件”和自定怪物权重已删除。** 当前 LoD 已有普通消耗品、箭袋原堆叠数量、金币等级随机数量及 TC mul/256 倍率；本轮接入普通基础武器／护甲、投掷堆叠、普通杂项及各品质展示实例的源码分支，尚待统一测试。钱包拾取不占背包，先前实际击杀、箭袋／金币拾取和保存恢复已有证据，见核心基线。仍未核实的实例会整批暂缓并记录原因，不等同于原 NoDrop。旧 1.04 生成仍关闭。金币经济、投掷攻击／损耗和属性战斗效果未实现。

资料片年代的表结构不同：`monstats.txt`、`superuniques.txt` 等提供入口，`TreasureClassEx.txt` 包含 `Picks`、`NoDrop`、`ItemN/ProbN` 和品质修正，类别还可嵌套。正 Picks 的普通一次选择，在尚未应用人数等修正时，条目权重通常按 `ProbN / (NoDrop + 所有条目权重之和)` 参与选择；这是该层的选择权重，不是最终暗金掉率。负 Picks、递归类别、品质、MF、难度及人数必须按对应版本分别处理。[OpenDiablo2 表读取实现](https://github.com/OpenDiablo2/OpenDiablo2/blob/master/d2core/d2records/treasure_class_loader.go)、[D2MOO 物品生成实现](https://github.com/ThePhrozenKeep/D2MOO/blob/master/source/D2Game/src/ITEMS/Items.cpp)

上述资料用于确认字段和执行链的区别，本轮没有移植或冒充完成这些算法，也没有把资料片算法套到旧式 1.04 槽位表上。

## 后续数据需求

若继续 1.04，需要核实该版本实际类别选择及物品生成算法；仅增大 MPQ 不会补上引擎代码。若转向经典完整版／LoD，应一次确定版本，准备同一安装中的 `d2data.mpq`、`d2exp.mpq`、`patch_d2.mpq`；角色和声音另用 `d2char.mpq`、`d2sfx.mpq`。不需要重制版、音乐或过场包。

优先检查 `weapons/armor/misc/itemtypes`、`monstats/superuniques/treasureclassex`、`itemratio`，再检查词缀、暗金／套装、`properties/itemstatcost` 等表。数据定义、选择算法、实例生成、图形覆盖应分别核对。当前适配器会拒绝不支持的表结构，尚不支持直接换一套 LoD MPQ 即运行；寻找方法见 [MPQ 资源](MPQ_RESOURCES.md)。

本轮从已有原始试玩 MPQ 补入数据即可，无须另下更大的资源包。新精简包含 285 个资源，10,512,784 字节，SHA-256：`b0b6d7d3189533906affa548f07b01d58f10deda0e00359f5d0905e1d1f0dce9`。图形覆盖仍为之前的 34 种物品，数据条数不代表图形或玩法全部实现。
