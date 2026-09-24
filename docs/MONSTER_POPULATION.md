# MPQ 怪物生成

已实现的第一幕地形使用 MPQ 刷怪数据。当前读取 `assets/mpq2` 的资料片命名字段表；原始五个 MPQ 无需追加下载。未实现的怪物按用户要求使用现有沉沦魔作为替身，保留真实身份。NPC、鸡、牛及其他中立／环境单位不会因此变成敌人。

## 使用

双击 `Play.cmd`，由营地沿野外和洞口探索；Ctrl+F2 为开发目录。也可直接指定区域、难度和刷怪种子：

```powershell
.\build\bin\d2x.exe --level 25 --difficulty normal --population-seed 210
.\build\bin\d2x.exe --level 37 --difficulty hell --population-seed 210
.\build\bin\d2x.exe --level 38
```

普通、噩梦、地狱分别接受 `normal`、`nightmare`、`hell`，目前改变刷怪数据选择，不代表全部难度战斗规则已完成。默认普通难度，刷怪种子为 210（32 位无符号十进制）。原有 `--seed` 仍专用于掉落随机状态，不影响本系统。

鼠标悬停敌人显示原始怪物 ID／固定首领名称、等级类别及 `Fallen substitute` 标识。`fallen1`、`zombie1`、`skeleton1`、`corruptrogue1`、`brute1` 已使用各自原外观；其他变体仍视为替身。普通难度普通怪物及其普通随从的生命与 A1 伤害按真实身份原表计算；骷髅、沉沦魔部分 AI 决策、Brute 受伤加速及 Brute／骷髅／僵尸／沉沦魔 A2 攻击已接，详见[怪物实施计划](MONSTERS.md)。原外观仍不代表完整专属 AI 或抗性已完成。

旅行保留该区域的怪物与尸体，重访不重新刷怪。R 恢复角色并以同一设置重建当前区域的生成计划，分配新的实体 ID。读档恢复保存的难度和刷怪种子，之后进入尚未访问的区域也继续使用该设置。

资源工具可查看完整预设的生成计划，与游戏共用人口生成器；该命令尚未接入生成地图的房间上下文：

```powershell
.\build\bin\d2x_assets.exe assets/mpq2 population 25 normal 210
.\build\bin\d2x_assets.exe assets/mpq2 population 37 hell 210
```

报告包含区域原字段、选中的怪物种类、密度尝试次数、生成数量、固定／随机来源、首领／随从类别、替身和被推迟的规则。它是资源检查功能，不是测试脚本。

## 读取并使用的原数据

| 来源 | 已接入内容 |
| --- | --- |
| `Levels.txt` | `mon1..10`、`nmon1..10`、`umon1..10`；`NumMon`、`rangedspawn`；三个难度的 `MonDen`、`MonUMin/Max`、`MonLvl*Ex`；平方距离 `WarpDist` |
| `LvlPrest.txt` | `Populate` 控制该预设是否进行区域密度刷怪 |
| `MonStats.txt` | `Id/hcIdx`、`BaseId/NextInClass/Level`、`isSpawn`、`Rarity`、`MinGrp/MaxGrp`、`sparsePopulate`；`minion1/2`、`PartyMin/Max`；`placespawn/spawn`；启用、阵营、NPC、可杀及首领标记 |
| `MonStats2.txt` | 通过 `MonStatsEx` 关联并识别 critter／inert，避免把环境单位当作替身敌人 |
| `MonPreset.txt` | 按 Act 分段保留原始顺序和重复槽位；DS1 的怪物编号解析为普通怪物、固定首领或放置标记 |
| `SuperUniques.txt` | 首领实际 `Class`、名字、随从范围，以及保留的 `Mod1..3` 与各难度 `TC` 名称 |
| `MonPlace.txt` | 区分放置标记；支持 unique/champion、bloodraven、fallen/fallenshaman；其他规则明确报告 |
| `MonUMod.txt` | 第 0 条 `constants` 决定随机精英转成勇士组的机会 |
| DS1 | Act、固定怪物坐标、原对象索引、已生成 flag；动态怪物不重复绘制成静态物件 |

`Levels.cmon/cpct/camt` 环境生物配置也已读取并在报告中展示，目前暂不生成。不能把这类记录按敌人替身处理。`NameStr` 等字段仍是原始本地化键，不冒充已加载的翻译文本。

`NumMon` 指选取的怪物种类数，不是全图怪物总数。先从区域池无放回选择种类，处理首个远程类型条件；随后按选中种类的 `Rarity` 加权。普通难度的精英使用 `umon`，噩梦／地狱使用对应区域池。

密度按原代码的面积尝试与 `random % 100000 <= MonDen` 门槛执行，密度上限 10000。普通群组使用 `MinGrp/MaxGrp` 和稀疏条件，沉沦魔／甲虫家族使用原本的单个主单位入口，再通过 `PartyMin/Max` 扩展随从，避免把两种群组数量重复相乘。随从交替使用 `minion1/2`，不会递归产生无限随从；普通怪物的 Party 随从保持 Normal，精英随从才使用 Minion。

随机精英读取 `MonUMin/Max` 与原额外出现判定，勇士组为主单位加 1–3 个同类；普通随机金怪为主单位加 3–6 个随从。固定首领使用 `SuperUniques.MinGrp/MaxGrp`，非零范围按难度增加；随从优先使用首领的 `minion1`，否则使用首领自身类型。词缀效果尚未应用。

`place_fallen` / `place_fallenshaman` 使用正常区域名单匹配家族，必要时根据 `NextInClass` 和正常区域等级推进，并执行第一幕黑色荒地／泰摩高地／深坑的原代码覆盖。模板没有所属 Levels 记录时不猜等级或名单。

## 当前地图适配边界

**当前是原表驱动的刷怪系统，不是原版 DRLG 逐种子等价实现。**

- 生成地图按实际房间／户外宏块的范围计算密度，避免把房间间的空洞计入面积。完整预设仍使用整张 DS1（去掉额外边界瓦片）的单房间人口适配。精英压力和随机流尚未与原引擎完全对齐。
- 随机落点遵守现有 DT1 碰撞；固定点只允许局部调整，无法放置则跳过并统计，不能搬到远处房间。替身使用现有小型怪物的占位方式，没有把安达利尔等原始大型碰撞尺寸伪装成已支持。
- 已连接地图使用实际出口到达点执行 `WarpDist` 平方距离保护；独立预览使用场景到达点。固定 DS1 敌人保留原点位，不受随机刷怪保护影响。
- 种子按场景路径、原关卡身份、难度和用户种子派生，跨平台使用明确的无符号运算。访问顺序和掉落不会改变另一地图的计划；这不是 Diablo II 的原始 RNG。
- 固定首领、unique/champion 标记可能使场景精英组数超过区域随机上限；不删除原 DS1 固定记录来凑范围。
- 营地保持安全；密度为 0 不生成随机群组。模板只处理明确可解析的固定记录，不借用其他关卡名单。
- `place_group25/50/75/100`、巢穴、任务条件及未核实的特殊规则暂缓并列出。参考版本的通用放置函数未提供这些 group 标记的完整执行分支，不能仅从名字编造概率和组内容。
- 原表存在重复名称 `cr_lancer8`（不同 hcIdx，属于第五幕）。当前将此名称标为歧义并禁止用字符串猜选，其他第一幕记录照常加载。旧 1.04 MonStats 架构没有本轮专用适配器时关闭人口生成并提示，不换回任意敌人。
- 普通难度普通怪物及其普通随从已读取原生命与 A1 伤害区间；有 A1 数据的远程类型也读取该区间；精英／首领及噩梦／地狱仍沿用现有适配值。普通骷髅的接近／攻击几率和停顿、沉沦魔的追击距离／游走／近身攻击几率、Brute 的受伤行走加速是专属 AI 的首批分支；除 Brute、普通骷髅、僵尸和沉沦魔外的 A2 动作、抗性、远程行为、复活、首领技能、精英词缀和任务进度尚未接入原规则。替身只解决“有可运行敌人”，不冒充这些行为已完成。

物品掉落执行仍按前一阶段的边界暂停。死亡事件和 `LootRequest` 现在携带原怪物、首领身份、等级类别、地图和难度，不再由替身的 `MonsterKind` 选择掉落池；因此后续不会让女伯爵错误使用沉沦魔掉落。

## 代码边界与存档

首次进入区域生成位置计划，计划不分配实体 ID。玩家进入房间或其相邻房间时，按群组一次性创建敌人并移出待生成队列。远处已创建敌人暂停 AI、受击／死亡计时，不绘制，也不参与命中；返回时继续原状态。小地图不提前展示待生成或休眠敌人。

`RoomLayout` 使用生成配方的实际范围；完整预设按 8×8 瓦片分块激活。这是附近房间策略，尚未移植原版完整激活调度；地形与人口计划也尚未按距离延迟生成。

- `content/monster_catalog.*`：原表解析、命名引用、原生预设解析，不创建游戏单位。
- `content/world_catalog.*`：区域人口配置和预设 Populate，与瓦片配方保持同一原始来源。
- `world/population.*` / `d2x_population`：只读内容和碰撞网格进入，值类型生成计划离开；独立于会话 ID 分配和渲染器。后续 DRLG 接入时替换空间／随机流适配，不在 AI 中读取 MPQ。
- `gameplay/monster_spawn.*`：原始身份与实现注册表。新怪物实现只需逐项注册，生成器不需要复制每一种怪物的逻辑。
- `GameSession`：首次进入或 R 时请求计划；`Simulation` 在本局保存待生成记录，`monster_activation.cpp` 按附近房间创建单位；死亡事件保留原身份。

当前角色存档 v82 保存地图配置和角色进度，不保存待生成记录、怪物实体或实例生命。载入后从城镇开启新的一局，区域人口按运行时 MPQ 重新规划；本局内仍保留原怪物／首领 ID、来源、组号和生成键。旧版本明确拒绝，详见 [存档](SAVES.md)。

## 参考

依据固定提交 `5596f5cb6c5251a0a07c6637d26458b06099d516` 的 D2MOO 实现核对：

- [MonsterRegion.cpp](https://github.com/ThePhrozenKeep/D2MOO/blob/5596f5cb6c5251a0a07c6637d26458b06099d516/source/D2Game/src/MONSTER/MonsterRegion.cpp)：区域池、密度、固定放置。
- [MonsterChoose.cpp](https://github.com/ThePhrozenKeep/D2MOO/blob/5596f5cb6c5251a0a07c6637d26458b06099d516/source/D2Game/src/MONSTER/MonsterChoose.cpp)：Rarity、umon、精英选择。
- [MonsterSpawn.cpp](https://github.com/ThePhrozenKeep/D2MOO/blob/5596f5cb6c5251a0a07c6637d26458b06099d516/source/D2Game/src/MONSTER/MonsterSpawn.cpp) 与 [MonsterUnique.cpp](https://github.com/ThePhrozenKeep/D2MOO/blob/5596f5cb6c5251a0a07c6637d26458b06099d516/source/D2Game/src/MONSTER/MonsterUnique.cpp)：随从、不递归标志、固定首领和勇士机会。
- D2Common 的 `DrlgPreset.cpp`、`Monsters.cpp::D2Common_11063` 和 `MonsterTbls.cpp`：DS1 预设关联、等级链和表链接。

版权及 MIT 许可见 [D2MOO.txt](licenses/D2MOO.txt)。这些是规则适配的来源，不代表用户这套 MPQ 已被鉴定为该参考版本。

本次交接的检查范围和未验证项见 [开发与交接](baseline/DEVELOPMENT.md)。人口数量是当前适配器的结果，不能作为原版逐种子参考值。
