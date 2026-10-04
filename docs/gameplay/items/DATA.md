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

DataTable 保留列顺序、重复列名和空值；字段按适配器语义读取。缺失原值不伪装成已知零值，缺少规则消费者不等于资源缺失。

## 选择、品质与实例

TC 支持单人正 Picks 权重／NoDrop、负 Picks 顺序展开、嵌套、四项品质修正继承及根 TC 升级。人数缩放尚未接入。纯查询不创建实例，品质查询使用 ItemRatio；游戏共用规划器在叶子判定品质、生成已支持实例，一批最多六件。

同批暗金候选按已选原行排重；品质失败按已核实顺序降级。未知实例导致整批暂缓并保留已消耗随机状态，不重抽、不将暂缓当 NoDrop；死亡实体 ID 只结算一次。结算资格与执行顺序见 [奖励模块](../../modules/REWARDS.md)，实例／位置规则见 [物品模型](MODEL.md)。

原表分支权重不是最终物品概率：递归类别、掉落次数、品质、MF、难度和资格会继续影响结果。旧自定“不掉落／额外掉一件”概率已删除，不用缺省参数填补原规则。

## 查询入口

在项目根使用已有资源工具；这些命令读取资源，不作为新的游戏验证记录：

```powershell
.\build\bin\d2x_assets.exe assets/mpq2 item hax
.\build\bin\d2x_assets.exe assets/mpq2 drops fallen1
.\build\bin\d2x_assets.exe assets/mpq2 treasure "Act 1 Champ A" 210
```

其他查询包括 quality、special 和 loot-plan；是否有数据／图形／实例生成／战斗效果分别核对。原包来源及摘要见 [MPQ](../../resources/MPQ.md)，算法借鉴与许可见 [资料来源](../../resources/THIRD_PARTY.md)。
