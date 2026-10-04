# MPQ 怪物生成

本页维护人口计划、房间激活与实例生成规则；身份／活动词缀／死亡奖励的代码边界见 [怪物模块](../../modules/MONSTERS.md)，家族行为见 [怪物专题](MONSTERS.md)。最新源码与运行包差异见 [项目基线](../../../BASELINE.md)，旧批次证据不认证之后的源码。

已实现的第一幕地形使用 MPQ 刷怪数据。当前读取 `assets/mpq2` 的资料片命名字段表；原始五个 MPQ 无需追加下载。未实现的怪物按用户要求使用现有沉沦魔作为替身，保留真实身份。NPC、鸡、牛及其他中立／环境单位不会因此变成敌人。

## 使用

双击 `Play.cmd`，由营地沿野外和洞口探索。也可直接指定区域、难度和刷怪种子：

```powershell
.\build\bin\d2x.exe --level 25 --difficulty normal --population-seed 210
.\build\bin\d2x.exe --level 37 --difficulty hell --population-seed 210
.\build\bin\d2x.exe --level 38
```

普通、噩梦、地狱分别接受 `normal`、`nightmare`、`hell`，目前改变刷怪数据选择，不代表全部难度战斗规则已完成。默认普通难度，新游戏从新种子派生人口计划；`--seed <uint32>` 固定整局，`--population-seed <uint32>` 单独覆盖人口计划。见 [随机机制](../combat/RANDOMNESS.md)。

鼠标悬停敌人显示原表本地化名称、等级类别及替身标识。当前第一幕普通／高难度名单的 55 个身份及巢的两种子鸟已有原外观和基础 AI；普通怪物与普通 Party 随从的三难度基础数值按真实身份共用解析。自然精英、固定首领和 Boss 的完整初始化仍未接入，原外观不代表完整行为。当前共用偏差、基型与专属首领计划见[怪物复审](MONSTERS.md#act-1-复审与下一步)。

旅行保留该区域的怪物与尸体，重访不重新刷怪。R 恢复角色并以同一设置重建当前区域的生成计划，分配新的实体 ID。读档恢复保存的难度和刷怪种子，之后进入尚未访问的区域也继续使用该设置。

资源工具可查看完整预设的生成计划，与游戏共用人口生成器；该命令尚未接入生成地图的房间上下文：

```powershell
.\build\bin\d2x_assets.exe assets/mpq2 population 25 normal 210
.\build\bin\d2x_assets.exe assets/mpq2 population 37 hell 210
```

报告包含区域原字段、选中的怪物种类、密度尝试次数、生成数量、固定／随机来源、首领／随从类别、替身和被推迟的规则。它是资源检查功能，不是测试脚本。

出生阻挡（2026-09-30 源码）：按真实 MonStats2.spawnCol 选择原掩码，并以 SizeX 查询点／十字／3×3 方形；人口与巢穴子单位共用内容层规则。飞行／Wraith 的移动路径掩码只用于移动，不用于出生。依据 D2MOO MonsterSpawn 与 D2Collision；选位半径、单位间距、房间／随机流仍是既有适配。本批未构建、运行检查、测试、游戏或打包，动态单位完整占位与拥挤边界见 [地图](MAPS.md#坐标绘制与阻挡)。

## 读取并使用的原数据

| 来源 | 已接入内容 |
| --- | --- |
| `Levels.txt` | `mon1..10`、`nmon1..10`、`umon1..10`；`NumMon`、`rangedspawn`；三个难度的 `MonDen`、`MonUMin/Max`、`MonLvl*Ex`；平方距离 `WarpDist` |
| `LvlPrest.txt` | `Populate` 控制该预设是否进行区域密度刷怪 |
| `MonStats.txt` | `Id/hcIdx`、`BaseId/NextInClass/Level`、`isSpawn`、`Rarity`、`MinGrp/MaxGrp`、`sparsePopulate`；`minion1/2`、`PartyMin/Max`；`placespawn/spawn`；启用、阵营、NPC、可杀及首领标记 |
| `MonStats2.txt` | 通过 `MonStatsEx` 关联并识别 critter／inert，避免把环境单位当作替身敌人 |
| `MonPreset.txt` | 按 Act 分段保留原始顺序和重复槽位；DS1 的怪物编号解析为普通怪物、固定首领或放置标记 |
| `SuperUniques.txt` | 首领实际 `Class`、名字、随从范围、`Mod1..3`、各难度 `TC/Utrans`、`Stacks` 去重；`AutoPos` 读取但原房间自动选位未移植 |
| `MonPlace.txt` | 区分放置标记；支持 unique/champion、bloodraven、fallen/fallenshaman；其他规则明确报告 |
| `MonUMod.txt` | 第 0 条 `constants` 决定随机精英转成勇士组的机会 |
| DS1 | Act、固定怪物坐标、原对象索引、已生成 flag；动态怪物不重复绘制成静态物件 |

`Levels.cmon/cpct/camt` 环境生物配置也已读取并在报告中展示，目前暂不生成。不能把这类记录按敌人替身处理。`NameStr` 等字段仍是原始本地化键，不冒充已加载的翻译文本。

`NumMon` 指选取的怪物种类数，不是全图怪物总数。先从区域池无放回选择种类，处理首个远程类型条件；随后按选中种类的 `Rarity` 加权。普通难度的精英使用 `umon`，噩梦／地狱使用对应区域池。

密度按原代码的面积尝试与 `random % 100000 <= MonDen` 门槛执行，密度上限 10000。普通群组使用 `MinGrp/MaxGrp` 和稀疏条件，沉沦魔／甲虫家族使用原本的单个主单位入口，再通过 `PartyMin/Max` 扩展随从，避免把两种群组数量重复相乘。随从交替使用 `minion1/2`，不会递归产生无限随从；普通怪物的 Party 随从保持 Normal，精英随从才使用 Minion。

随机精英读取 `MonUMin/Max` 与原额外出现判定，勇士组为主单位加 1–3 个同类；普通随机金怪为主单位加 3–6 个随从。固定首领使用 `SuperUniques.MinGrp/MaxGrp`，非零范围按难度增加；随从优先使用首领的 `minion1`，否则使用首领自身类型。elite 随从记录 ownerSpawnKey，勇士同伴不误作直属随从。2026-10-01 第二阶段基础接入：自然 Champion／Unique 先掷普通基础生命，再初始化原随机词缀／勇士变体；同批主人完成后准确继承直属随从的数值和收益，保留人口既定类别而不重新掷勇士概率。光环等技能型能力按用户要求暂缓、保留原 ID 不重掷；姓名／精英调色缺完整客户端依据仍暂缓。固定首领／Boss 没有因此开放，详见[第二阶段交接](MONSTERS.md#第二阶段基础接入与暂缓)。

`place_fallen` / `place_fallenshaman` 使用正常区域名单匹配家族，必要时根据 `NextInClass` 和正常区域等级推进，并执行第一幕黑色荒地／泰摩高地／深坑的原代码覆盖。模板没有所属 Levels 记录时不猜等级或名单。

## 当前地图适配边界

2026-10-01：八个已有基型固定金怪现已接入实例初始化、词缀、直属数值及收益，范围和普通难度包内冒烟见 [固定金怪](MONSTERS.md#已有基型固定金怪)。本文此前“固定首领未初始化／随从暂缓”的描述仅适用于未开放项；指定八项不再受该限制。Stacks=0 在同一区域计划内去重，AutoPos 仍采用原 DS1 点位和局部调整；牛场任务入口不因启动参数能查看地形而完成。

**当前是原表驱动的刷怪系统，不是原版 DRLG 逐种子等价实现。**

- 生成地图按实际房间／户外宏块的范围计算密度，避免把房间间的空洞计入面积。完整预设仍使用整张 DS1（去掉额外边界瓦片）的单房间人口适配。精英压力和随机流尚未与原引擎完全对齐。
- 随机落点遵守现有 DT1 碰撞；固定点只允许局部调整，无法放置则跳过并统计，不能搬到远处房间。替身使用现有小型怪物的占位方式，没有把安达利尔等原始大型碰撞尺寸伪装成已支持。
- 已连接地图使用实际出口到达点执行 `WarpDist` 平方距离保护；独立预览使用场景到达点。固定 DS1 敌人保留原点位，不受随机刷怪保护影响。
- 人口计划使用 D2Seed 的无符号低／高位递推；区域种子按 DRLG 的父流掷值加关卡编号方式派生，移除路径 FNV 和 SplitMix64。访问顺序和掉落不会改变另一地图的计划；房间划分和消费顺序仍为项目适配，不能按同种子逐单位重现原引擎。
- 固定首领、unique/champion 标记可能使场景精英组数超过区域随机上限；不删除原 DS1 固定记录来凑范围。
- 营地保持安全；密度为 0 不生成随机群组。模板只处理明确可解析的固定记录，不借用其他关卡名单。
- `place_group25/50/75/100`、`place_fallennest` 等特殊放置、任务条件及未核实的特殊规则暂缓并列出；已接入的 crownest 普通身份和孵鸟技能不是这些放置标记的替代。参考版本的通用放置函数未提供 group 标记的完整执行分支，不能仅从名字编造概率和组内容。
- 原表存在重复名称 `cr_lancer8`（不同 hcIdx，属于第五幕）。当前将此名称标为歧义并禁止用字符串猜选，其他第一幕记录照常加载。旧 1.04 MonStats 架构没有本轮专用适配器时关闭人口生成并提示，不换回任意敌人。
- 普通怪物与普通 Party 随从三难度数值共用解析，远程／复活／孵化按家族接入；自然精英及准确直属随从现有基础强化和原家族 AI。特殊技能、精英姓名／调色暂缓，固定首领／Boss 尚未接完整初始化；目标获取、路径、房间与随机流仍为项目适配，不能以名单覆盖或本次基础接入宣称全面原版等价，见[怪物复审](MONSTERS.md#act-1-复审与下一步)。

物品掉落已有原 TC 执行入口。死亡事件和 `LootRequest` 携带原怪物、首领身份、等级类别、地图和难度，不由替身的 `MonsterKind` 选择掉落池。自然 Champion／Unique 使用原勇士／金怪 TC，已初始化的直属 Minion 使用其强化等级及普通 TC，经验读取原强化倍数；未初始化的固定首领随从等仍明确暂缓。巢达到原上限后保留经验结算但不执行 TC，召唤幼鸟保持无经验／掉落。固定首领特殊初始化／任务掉落须随对应阶段核对，见[物品完成度](../items/SUPPORT.md)与[怪物复审](MONSTERS.md#act-1-复审与下一步)。

## 代码边界与存档

首次进入区域生成位置计划，计划不分配实体 ID。玩家进入房间或其相邻房间时，按群组一次性创建敌人并移出待生成队列。远处已创建敌人暂停 AI、受击／死亡计时，不绘制，也不参与命中；返回时继续原状态。连续步行边界外、相邻房间中的已创建怪物与尸体现在仍绘制原实体及悬停信息，不再因为当前关卡 ID 改变而整批消失；跨区域 AI、命中和首次预激活仍待完整调度接入，详见 [地图](MAPS.md)。小地图不提前展示待生成或休眠敌人。

`RoomLayout` 使用生成配方的实际范围；完整预设按 8×8 瓦片分块激活。这是附近房间策略，尚未移植原版完整激活调度；地形与人口计划也尚未按距离延迟生成。

- `content/monsters/monster_catalog.*`：原表解析、命名引用、原生预设解析，不创建游戏单位。
- `content/world/world_catalog.*`：区域人口配置和预设 Populate，与瓦片配方保持同一原始来源。
- `world/population.*` / `d2x_population`：只读内容和碰撞网格进入，值类型生成计划离开；独立于会话 ID 分配和渲染器。后续 DRLG 接入时替换空间／随机流适配，不在 AI 中读取 MPQ。
- `gameplay/monsters/monster_spawn.*`：原始身份与实现注册表。新怪物实现只需逐项注册，生成器不需要复制每一种怪物的逻辑。
- `GameSession`：首次进入或 R 时请求计划；`Simulation` 在本局保存待生成记录，`monster_activation.cpp` 按附近房间创建单位；死亡事件保留原身份。

当前角色存档为原生 D2S v96，不保存待生成记录、怪物实体、实例生命或临时 AI 状态。载入后从城镇开启新的一局，区域人口按运行时 MPQ 重新规划；本局内仍保留原怪物／首领 ID、来源、组号和生成键。准确磁盘字段与旧格式边界见 [存档](../../modules/SAVES.md)。

## 参考

依据固定提交 `5596f5cb6c5251a0a07c6637d26458b06099d516` 的 D2MOO 实现核对：

- [MonsterRegion.cpp](https://github.com/ThePhrozenKeep/D2MOO/blob/5596f5cb6c5251a0a07c6637d26458b06099d516/source/D2Game/src/MONSTER/MonsterRegion.cpp)：区域池、密度、固定放置。
- [MonsterChoose.cpp](https://github.com/ThePhrozenKeep/D2MOO/blob/5596f5cb6c5251a0a07c6637d26458b06099d516/source/D2Game/src/MONSTER/MonsterChoose.cpp)：Rarity、umon、精英选择。
- [MonsterSpawn.cpp](https://github.com/ThePhrozenKeep/D2MOO/blob/5596f5cb6c5251a0a07c6637d26458b06099d516/source/D2Game/src/MONSTER/MonsterSpawn.cpp) 与 [MonsterUnique.cpp](https://github.com/ThePhrozenKeep/D2MOO/blob/5596f5cb6c5251a0a07c6637d26458b06099d516/source/D2Game/src/MONSTER/MonsterUnique.cpp)：随从、不递归标志、固定首领和勇士机会。
- D2Common 的 `DrlgPreset.cpp`、`Monsters.cpp::D2Common_11063` 和 `MonsterTbls.cpp`：DS1 预设关联、等级链和表链接。

版权及 MIT 许可见 [D2MOO.txt](../../licenses/D2MOO.txt)。这些是规则适配的来源，不代表用户这套 MPQ 已被鉴定为该参考版本。

本次交接的检查范围和未验证项见 [开发与交接](../../development/BUILD_AND_RUN.md)。人口数量是当前适配器的结果，不能作为原版逐种子参考值。
