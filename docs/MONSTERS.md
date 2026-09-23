# 怪物实施计划

怪物身份、区域名单、分组和掉落继续以运行时 MPQ 为准；类型替身只用于尚未实现的敌对外观。一个怪物完成外观不表示其 AI、精英词缀或战斗帧已与原版一致。

## 分步边界

1. **普通近战基础数值：已完成。** `content/monster_combat.*` 从运行时 `MonStats.txt` 的 `minHP/maxHP/A1MinD/A1MaxD/noRatio/Level` 和 `MonLvl.txt` 的 `L-HP/L-DM` 解析普通难度基础区间；`Simulation` 按真实身份在生成时掷生命、攻击时掷 A1 伤害。只支持普通级别、非 Boss、非远程记录；其他类型保持明确的既有适配值。敌人最大生命随 v23 存档保存并校验，血条使用该实例的最大生命。
2. **单个普通怪物外观：已完成。** `brute1` 使用 MPQ `YE` 的 NU/WL/A1/DT COF/DCC；原 `MonStats2` 只有躯干组件，四种动作资源均在本机 MPQ 中。该身份从沉沦魔类型替身切换为原外观，仍沿用公共近战决策。`brute2` 等不同变体继续为替身。
3. **骷髅 AI 决策：已完成基础分支。** `content/monster_ai_data.*` 从挂载 MPQ 的 `MonStats.AI/aip1–aip8` 按难度读取参数；`gameplay/monsters/skeleton_ai.*` 对原 `AI=Skeleton` 的普通骷髅使用接近几率、停顿帧数和近身攻击几率。追击和停顿状态写入 v24 存档并校验。逐帧 AI 调度仍待后续。
4. **Brute 受伤加速：已完成基础分支。** `gameplay/monsters/brute_ai.*` 按本地 D2MOO 的 `AITHINK_Fn007_Brute`，从当前生命百分比计算原 AI 的 40% 下限和最高 60% 行走速度加成；只对 MPQ `AI=Brute` 且已实现外观的普通怪物应用。近身盘绕与停顿仍待完成。
5. **A1 动作与出伤帧：已完成基础分支。** `resources/anim_data.*` 从挂载 MPQ 解码 `AnimData.d2`；`content/monster_animation.*` 按已实现外观的 token／武器类选取原 A1 速度、帧数和事件 1。`gameplay/monsters/monster_melee.cpp` 在原事件帧重新检查玩家距离／通路并结算命中；画面按相同动作时长推进，v25 存档保存动作剩余与待出伤时间。原表无有效记录时明确沿用旧即时攻击适配。
6. **Brute A2：已完成基础分支。** `brute1` 的第二攻击模式由运行时 MPQ 的 A2 COF/DCC 与 `AnimData.d2` 驱动；伤害／命中读取 `MonStats.A2MinD/A2MaxD/A2TH` 与 `MonLvl`，A1／A2 选择使用原 `AI=Brute` 的 `aip4`。攻击模式写入 v26 存档并在恢复时验证。只在 A2 资源、数值和命中资料齐全时选择该动作。
7. **普通骷髅 A2：已完成基础分支。** `skeleton1` 的 A2 使用运行时 MPQ 的 `SKA21HS` COF/DCC、`AnimData.d2` 动作事件及 `MonStats` A2 伤害／命中；`AI=Skeleton` 的 `aip4` 在攻击决策通过后选择 A1／A2。v27 存档保存模式并按当前资源验证。该分支仍只覆盖普通难度、普通级别的已实现外观。
8. **普通僵尸 A2：已完成基础分支。** `zombie1` 的 A2 使用 MPQ 原 `ZMA2HTH` COF/DCC、`AnimData.d2` 动作事件和 `MonStats` A2 伤害／命中；`AI=Zombie` 的 `aip4` 决定近身攻击模式。v28 存档保存模式并按当前资源验证。接近目标的专属 AI 仍待单独实现。
9. **调试定向刷怪与扣血：已完成。** 命名管道按当前 MPQ `MonStats.Id` 和可走坐标生成单个敌对怪物，沿用正式生命掷骰；扣血、死亡经验与掉落走正式结算。存档使用 Debug 来源区分自然生成，v29 恢复校验身份、位置与唯一键。未实现的敌对类型仍显式标记沉沦魔替身。
10. **后续候选：其他专属 AI 与动作。** 逐种核对原 AI 规则和 MPQ 资源，再处理远程弹体与技能、尸体复活、各难度与等级缩放、抗性和元素伤害、精英／首领词缀和 Boss 特性。

当前数值计算依据本地 `reference/d2moo/source/D2Common/src/DataTbls/MonsterTbls.cpp` 的 `DATATBLS_CalculateMonsterStatsByLevel` 和 `reference/d2moo/source/D2Game/src/MONSTER/Monster.cpp` 的生命掷骰；AI 规则核对 `reference/d2moo/source/D2Game/src/AI/AiThink.cpp` 的 Skeleton 与 Brute 分支，动作帧核对 `reference/d2moo/source/D2Common/src/DataTbls/AnimTbls.cpp` 与 `Units.cpp`。区间、AI 参数和动作时序始终从用户挂载的 MPQ 读取，参考仓库不作为运行时数据源。当前目标选择、AI 调度和随机流仍是项目适配，不能视为原版逐帧复刻。
