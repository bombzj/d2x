# 怪物实施计划

怪物身份、区域名单、分组和掉落继续以运行时 MPQ 为准；类型替身只用于尚未实现的敌对外观。一个怪物完成外观不表示其 AI、精英词缀或战斗帧已与原版一致。

## 分步边界

1. **普通基础数值：已完成。** `content/monster_combat.*` 从运行时 `MonStats.txt` 的 `minHP/maxHP/A1MinD/A1MaxD/noRatio/Level` 和 `MonLvl.txt` 的 `L-HP/L-DM` 解析普通难度基础区间；`Simulation` 按真实身份在生成时掷生命、攻击时掷 A1 伤害。普通群组随从与有 A1 数据的远程记录也已接入；精英／首领及噩梦／地狱仍使用旧适配值。敌人最大生命随存档保存并校验，血条使用该实例的最大生命。
2. **单个普通怪物外观：已完成。** `brute1` 使用 MPQ `YE` 的 NU/WL/A1/DT COF/DCC；原 `MonStats2` 只有躯干组件，四种动作资源均在本机 MPQ 中。该身份从沉沦魔类型替身切换为原外观，仍沿用公共近战决策。`brute2` 等不同变体继续为替身。
3. **骷髅 AI 决策：已完成基础分支。** `content/monster_ai_data.*` 从挂载 MPQ 的 `MonStats.AI/aip1–aip8` 按难度读取参数；`gameplay/monsters/skeleton_ai.*` 对原 `AI=Skeleton` 的普通骷髅使用接近几率、停顿帧数和近身攻击几率。追击和停顿状态写入 v24 存档并校验。逐帧 AI 调度仍待后续。
4. **Brute 受伤加速：已完成基础分支。** `gameplay/monsters/brute_ai.*` 按本地 D2MOO 的 `AITHINK_Fn007_Brute`，从当前生命百分比计算原 AI 的 40% 下限和最高 60% 行走速度加成；只对 MPQ `AI=Brute` 且已实现外观的普通怪物应用。近身盘绕与停顿仍待完成。
5. **A1 动作与出伤帧：已完成基础分支。** `resources/anim_data.*` 从挂载 MPQ 解码 `AnimData.d2`；`content/monster_animation.*` 按已实现外观的 token／武器类选取原 A1 速度、帧数和事件 1。`gameplay/monsters/monster_melee.cpp` 在原事件帧重新检查玩家距离／通路并结算命中；画面按相同动作时长推进，v25 存档保存动作剩余与待出伤时间。原表无有效记录时明确沿用旧即时攻击适配。
6. **Brute A2：已完成基础分支。** `brute1` 的第二攻击模式由运行时 MPQ 的 A2 COF/DCC 与 `AnimData.d2` 驱动；伤害／命中读取 `MonStats.A2MinD/A2MaxD/A2TH` 与 `MonLvl`，A1／A2 选择使用原 `AI=Brute` 的 `aip4`。攻击模式写入 v26 存档并在恢复时验证。只在 A2 资源、数值和命中资料齐全时选择该动作。
7. **普通骷髅 A2：已完成基础分支。** `skeleton1` 的 A2 使用运行时 MPQ 的 `SKA21HS` COF/DCC、`AnimData.d2` 动作事件及 `MonStats` A2 伤害／命中；`AI=Skeleton` 的 `aip4` 在攻击决策通过后选择 A1／A2。v27 存档保存模式并按当前资源验证。该分支仍只覆盖普通难度、普通级别的已实现外观。
8. **普通僵尸 A2：已完成基础分支。** `zombie1` 的 A2 使用 MPQ 原 `ZMA2HTH` COF/DCC、`AnimData.d2` 动作事件和 `MonStats` A2 伤害／命中；`AI=Zombie` 的 `aip4` 决定近身攻击模式。v28 存档保存模式并按当前资源验证；接近行为见第 10 项。
9. **调试定向刷怪与扣血：已完成。** 命名管道按当前 MPQ `MonStats.Id` 和可走坐标生成单个敌对怪物，沿用正式生命掷骰；扣血、死亡经验与掉落走正式结算。存档使用 Debug 来源区分自然生成，v29 恢复校验身份、位置与唯一键。未实现的敌对类型仍显式标记沉沦魔替身。
10. **僵尸接近／游走：已完成基础分支。** 普通 `AI=Zombie` 按 MPQ `aip1` 接近概率和 `aip2` 警觉距离开始追击，追击采用原规则指定的 100% 速度；未警觉时在自身附近三格内选择可走位置游走。追击和短暂决策等待保存于 v30；邪恶洞窟定向生成后，追击与远处游走、存读档均已冒烟。原 AI 状态 3／19、埋骨之地强制追击与逐帧行动调度仍待实现。
11. **普通群组等级与数值：已修正。** 普通怪物从 MPQ `PartyMin/Max` 生成的随从使用 Normal，保留原数量但不再误用精英 Minion 的通用 100 HP／6 点伤害；Minion 留给精英随从。读取 MPQ A1 数值时不因 `rangedtype` 一概跳过有 A1 字段的类型。血腥荒地种子 210 的 24 个 `fallen1` 现为 1–4 HP，`quillrat1` 为 2 HP；普通难度 `Levels.MonDen=520` 与两个高难度在该区域恰好相同，不能凭难度名称擅改数量。v31 存档按新等级与生命区间校验；噩梦／地狱的完整战斗数值和正式切换仍待后续。
12. **沉沦魔追击／游走／攻击概率：已完成基础分支。** 普通 `AI=Fallen` 按 MPQ `aip2` 的距离门槛追击目标；门槛外参照原规则按 30% 几率在三格内游走，否则等待 10 帧。近身攻击按 `aip3` 选择，失败后短暂等待。普通难度 `fallen1` 的门槛 10、攻击概率 50% 均来自当前 MPQ。游走与等待共用独立怪物 AI 组件，等待／追击状态进入 v32 存档。见同伴尸体逃跑、`aip1` 同伴命令与 Skill2 喊叫暂缓。
13. **普通沉沦魔 A2：已完成基础分支。** `fallen1` 从 MPQ 原 `FAA2HTH` COF/DCC、`AnimData.d2` 动作事件和 `MonStats.A2MinD/A2MaxD/A2TH` 取得第二近战动作与数值；`AI=Fallen` 的 `aip4` 决定 A1／A2。原表 A2 为 15 帧、0.6 秒，命中事件在 0.32 秒；v33 存档可恢复出伤阶段，独立运行目录已截图。仅在原资源与数值齐全时启用。
14. **后续候选：其他专属 AI 与动作。** 逐种核对原 AI 规则和 MPQ 资源，再处理远程弹体与技能、尸体复活、各难度与等级缩放、抗性和元素伤害、精英／首领词缀和 Boss 特性。

当前数值计算依据本地 `reference/d2moo/source/D2Common/src/DataTbls/MonsterTbls.cpp` 的 `DATATBLS_CalculateMonsterStatsByLevel` 和 `reference/d2moo/source/D2Game/src/MONSTER/Monster.cpp` 的生命掷骰；AI 规则核对 `reference/d2moo/source/D2Game/src/AI/AiThink.cpp` 的 Skeleton、Zombie、Fallen 与 Brute 分支，动作帧核对 `reference/d2moo/source/D2Common/src/DataTbls/AnimTbls.cpp` 与 `Units.cpp`。区间、AI 参数和动作时序始终从用户挂载的 MPQ 读取，参考仓库不作为运行时数据源。当前目标选择、AI 调度和随机流仍是项目适配，不能视为原版逐帧复刻。
