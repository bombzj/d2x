# 怪物实施计划

怪物身份、区域名单、分组和掉落继续以运行时 MPQ 为准；类型替身只用于尚未实现的敌对外观。普通级别怪物共用 `MonStats`／`MonLvl` 的三难度生命、A1／A2、命中、防御、暴击、再生和抗性解析；某行没有 A1 近战列时仍读取其余字段。共享数值只需集中核对，不逐个怪物重复验算。按原 `MonStats.Id` 逐一收尾专属动作、可触发 AI 分支、技能或元素攻击、声音、死亡经验与掉落、存读档和画面，再打完整 MPQ 运行目录、冒烟并提交。一个 ID 完成后才开始下一个；精英、固定首领与 Boss 单列。通用地图寻路、目标调度和画面帧时钟仍是项目适配，验收不声称逐帧等同原引擎。

## 共用与专属边界

本地 `reference/d2moo` 的 `DATATBLS_CalculateMonsterStatsByLevel` 对所有怪物按同一公式和模式标志换算生命、护甲、经验、A1／A2／S1 伤害与命中；`MonsterMode.cpp` 同一路径还按 `El1–3` 与当前动作模式附加元素伤害。`AITHINK_GetAiTableRecord` 则按 `MonStats.AI` 选择 AI 函数；Skeleton、Zombie、Fallen、Brute 等是不同函数，同一 AI 类型的不同 `MonStats.Id` 共用行为代码，`aip1–8` 取各自难度的原值。故不能把所有行为合并为一种通用追击，也不应为每个变体复制一份 AI。

本项目共用解析放在 `content/monster_difficulty_combat.*`，生成、命中、受击、抗性、经验、掉落与存档走通用会话／模拟流程。`content/monster_animation.*` 按 token／动作／武器类读取 `AnimData.d2`，展示层的 `monster_audio.cpp` 按真实怪物身份读取 `MonSounds`／`Sounds`。后续应将元素模式、原技能／弹体与其施放动作接入共用攻击流程；仅在 `gameplay/monsters/` 为不同 AI 家族实现决策和必要状态。怪物 ID 负责挑选原图形、声音、AI 家族及数据行；同一家族变体不重复实现数值或战斗规则。精英词缀、特殊死亡、复活、召唤和 Boss 阶段另设模块。

## 分步边界

1. **普通基础数值：已完成。** 最初由 `content/monster_combat.*` 解析普通难度生命与 A1；当前已由共用三难度解析接管普通怪物。`Simulation` 按真实身份在生成时掷生命、攻击时读取当前模式的原伤害；缺 A1 的远程或特殊记录仍保留其余数值。精英／首领仍沿用旧适配值。敌人最大生命随存档保存并校验，血条使用该实例的最大生命。
2. **单个普通怪物外观：已完成。** `brute1` 使用 MPQ `YE` 的 NU/WL/A1/DT COF/DCC；原 `MonStats2` 只有躯干组件，四种动作资源均在本机 MPQ 中。该身份从沉沦魔类型替身切换为原外观，仍沿用公共近战决策。`brute2` 等不同变体继续为替身。
3. **骷髅 AI 决策：已完成基础分支。** `content/monster_ai_data.*` 从挂载 MPQ 的 `MonStats.AI/aip1–aip8` 按难度读取参数；`gameplay/monsters/skeleton_ai.*` 对原 `AI=Skeleton` 的普通骷髅使用接近几率、停顿帧数和近身攻击几率。追击和停顿状态写入 v24 存档并校验。逐帧 AI 调度仍待后续。
4. **Brute 受伤加速：已完成基础分支。** `gameplay/monsters/brute_ai.*` 按本地 D2MOO 的 `AITHINK_Fn007_Brute`，从当前生命百分比计算原 AI 的 40% 下限和最高 60% 行走速度加成；只对 MPQ `AI=Brute` 且已实现外观的普通怪物应用。近身盘绕与停顿仍待完成。
5. **A1 动作与出伤帧：已完成基础分支。** `resources/anim_data.*` 从挂载 MPQ 解码 `AnimData.d2`；`content/monster_animation.*` 按已实现外观的 token／武器类选取原 A1 速度、帧数和事件 1。`gameplay/monsters/monster_melee.cpp` 在原事件帧重新检查玩家距离／通路并结算命中；画面按相同动作时长推进，v25 存档保存动作剩余与待出伤时间。原表无有效记录时明确沿用旧即时攻击适配。
6. **Brute A2：已完成基础分支。** `brute1` 的第二攻击模式由运行时 MPQ 的 A2 COF/DCC 与 `AnimData.d2` 驱动；伤害／命中读取 `MonStats.A2MinD/A2MaxD/A2TH` 与 `MonLvl`，A1／A2 选择使用原 `AI=Brute` 的 `aip4`。攻击模式写入 v26 存档并在恢复时验证。只在 A2 资源、数值和命中资料齐全时选择该动作。
7. **普通骷髅 A2：已完成基础分支。** `skeleton1` 的 A2 使用运行时 MPQ 的 `SKA21HS` COF/DCC、`AnimData.d2` 动作事件及 `MonStats` A2 伤害／命中；`AI=Skeleton` 的 `aip4` 在攻击决策通过后选择 A1／A2。v27 存档保存模式并按当前资源验证。该分支只覆盖普通级别的已实现外观；数值现由三难度共用解析提供。
8. **普通僵尸 A2：已完成基础分支。** `zombie1` 的 A2 使用 MPQ 原 `ZMA2HTH` COF/DCC、`AnimData.d2` 动作事件和 `MonStats` A2 伤害／命中；`AI=Zombie` 的 `aip4` 决定近身攻击模式。v28 存档保存模式并按当前资源验证；接近行为见第 10 项。
9. **调试定向刷怪与扣血：已完成。** 命名管道按当前 MPQ `MonStats.Id` 和可走坐标生成单个敌对怪物，沿用正式生命掷骰；扣血、死亡经验与掉落走正式结算。存档使用 Debug 来源区分自然生成，v29 恢复校验身份、位置与唯一键。未实现的敌对类型仍显式标记沉沦魔替身。
10. **僵尸接近／游走：已完成基础分支。** 普通 `AI=Zombie` 按 MPQ `aip1` 接近概率和 `aip2` 警觉距离开始追击，追击采用原规则指定的 100% 速度；未警觉时在自身附近三格内选择可走位置游走。追击和短暂决策等待保存于 v30；邪恶洞窟定向生成后，追击与远处游走、存读档均已冒烟。原 AI 状态 3／19、埋骨之地强制追击与逐帧行动调度仍待实现。
11. **普通群组等级与数值：已修正。** 普通怪物从 MPQ `PartyMin/Max` 生成的随从使用 Normal，保留原数量但不再误用精英 Minion 的通用 100 HP／6 点伤害；Minion 留给精英随从。读取 MPQ A1 数值时不因 `rangedtype` 一概跳过有 A1 字段的类型。血腥荒地种子 210 的 24 个 `fallen1` 现为 1–4 HP，`quillrat1` 为 2 HP；普通难度 `Levels.MonDen=520` 与两个高难度在该区域恰好相同，不能凭难度名称擅改数量。v31 存档按新等级与生命区间校验；噩梦／地狱的普通怪物基础战斗数值现由共用解析提供；精英修正仍待后续。
12. **沉沦魔追击／游走／攻击概率：已完成基础分支。** 普通 `AI=Fallen` 按 MPQ `aip2` 的距离门槛追击目标；门槛外参照原规则按 30% 几率在三格内游走，否则等待 10 帧。近身攻击按 `aip3` 选择，失败后短暂等待。普通难度 `fallen1` 的门槛 10、攻击概率 50% 均来自当前 MPQ。游走与等待共用独立怪物 AI 组件，等待／追击状态进入 v32 存档。见同伴尸体逃跑、`aip1` 同伴命令与 Skill2 喊叫暂缓。
13. **普通沉沦魔 A2：已完成基础分支。** `fallen1` 从 MPQ 原 `FAA2HTH` COF/DCC、`AnimData.d2` 动作事件和 `MonStats.A2MinD/A2MaxD/A2TH` 取得第二近战动作与数值；`AI=Fallen` 的 `aip4` 决定 A1／A2。原表 A2 为 15 帧、0.6 秒，命中事件在 0.32 秒；v33 存档可恢复出伤阶段，独立运行目录已截图。仅在原资源与数值齐全时启用。
14. **Brute 近身攻击机会：已完成基础分支。** `AI=Brute` 在近身时先按 MPQ `aip3` 掷攻击机会；掷骰失败则等待 15 帧，等待状态沿用现有存档字段。原 AI 在失败后还会再次掷骰，并可能执行侧移；侧移所需移动参数尚未核实，暂缓接入。`brute1` 普通难度原表攻击机会为 100%，因此这一步不会改变其近身攻击频率；其他 Brute 变体仍使用类型替身。v34 改变规则指纹，不读取旧档。
15. **`brute1` 普通级别三难度收尾：已接源码。** 原 `YE` NU/WL/A1/A2/GH/DT/DD 动作和 `AnimData.d2` 时序，`MonSounds` 与 `Sounds` 的脚步、待机、攻击、受击、死亡音频已按此身份接入；三难度生命、A1/A2 伤害／命中、防御、暴击、生命再生与抗性由上述共用解析读取。地狱原表物理抗性 50%、冰冷抗性 100%；没有原技能或元素攻击。Brute AI 在三难度的 `aip3` 均为 100%，因此失败后的侧移／停顿不会在此 ID 触发；A1/A2 按 `aip4`。自然生成、经验、掉落走既有身份链。独立打包与冒烟结果见开发基线。
16. **后续候选：其他普通怪物。** 下一种须按上述清单独立完成；远程弹体与技能、尸体复活、元素伤害等随相应身份逐项实现。精英／首领词缀和 Boss 特性另列阶段。

当前数值计算依据本地 `reference/d2moo/source/D2Common/src/DataTbls/MonsterTbls.cpp` 的 `DATATBLS_CalculateMonsterStatsByLevel` 和 `reference/d2moo/source/D2Game/src/MONSTER/Monster.cpp` 的生命掷骰；AI 规则核对 `reference/d2moo/source/D2Game/src/AI/AiThink.cpp` 的 Skeleton、Zombie、Fallen 与 Brute 分支，动作帧核对 `reference/d2moo/source/D2Common/src/DataTbls/AnimTbls.cpp` 与 `Units.cpp`。区间、AI 参数和动作时序始终从用户挂载的 MPQ 读取，参考仓库不作为运行时数据源。当前目标选择、AI 调度和随机流仍是项目适配，不能视为原版逐帧复刻。
