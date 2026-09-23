# 怪物实施计划

怪物身份、区域名单、分组和掉落继续以运行时 MPQ 为准；类型替身只用于尚未实现的敌对外观。一个怪物完成外观不表示其 AI、精英词缀或战斗帧已与原版一致。

## 分步边界

1. **普通近战基础数值：已完成。** `content/monster_combat.*` 从运行时 `MonStats.txt` 的 `minHP/maxHP/A1MinD/A1MaxD/noRatio/Level` 和 `MonLvl.txt` 的 `L-HP/L-DM` 解析普通难度基础区间；`Simulation` 按真实身份在生成时掷生命、攻击时掷 A1 伤害。只支持普通级别、非 Boss、非远程记录；其他类型保持明确的既有适配值。敌人最大生命随 v23 存档保存并校验，血条使用该实例的最大生命。
2. **单个普通怪物外观：已完成。** `brute1` 使用 MPQ `YE` 的 NU/WL/A1/DT COF/DCC；原 `MonStats2` 只有躯干组件，四种动作资源均在本机 MPQ 中。该身份从沉沦魔类型替身切换为原外观，仍沿用公共近战决策。`brute2` 等不同变体继续为替身。
3. **骷髅 AI 决策：已完成基础分支。** `content/monster_ai_data.*` 从挂载 MPQ 的 `MonStats.AI/aip1–aip8` 按难度读取参数；`gameplay/monsters/skeleton_ai.*` 对原 `AI=Skeleton` 的普通骷髅使用接近几率、停顿帧数和近身攻击几率。追击和停顿状态写入 v24 存档并校验。A1／A2 动作选择、逐帧 AI 调度仍待后续。
4. **Brute 受伤加速：已完成基础分支。** `gameplay/monsters/brute_ai.*` 按本地 D2MOO 的 `AITHINK_Fn007_Brute`，从当前生命百分比计算原 AI 的 40% 下限和最高 60% 行走速度加成；只对 MPQ `AI=Brute` 且已实现外观的普通怪物应用。该 AI 的 A1／A2 选择、近身盘绕与停顿仍待完成。
5. **下一项候选：攻击动作与出伤帧。** 读取运行时 `AnimData.d2` 的速度和帧标记，同步 A1／A2 演出与结算，并核对 `MonStats2` 模式及 COF/DCC 是否完整；当前 COF 解码只保存图层顺序，不提供出伤时机。完成后再逐种处理更多 AI、远程弹体与技能、尸体复活、各难度与等级缩放、抗性和元素伤害、精英／首领词缀和 Boss 特性。

当前数值计算依据本地 `reference/d2moo/source/D2Common/src/DataTbls/MonsterTbls.cpp` 的 `DATATBLS_CalculateMonsterStatsByLevel` 和 `reference/d2moo/source/D2Game/src/MONSTER/Monster.cpp` 的生命掷骰；AI 规则核对 `reference/d2moo/source/D2Game/src/AI/AiThink.cpp` 的 Skeleton 与 Brute 分支。区间和 AI 参数始终从用户挂载的 MPQ 读取，参考仓库不作为运行时数据源。当前攻击触发时刻、目标选择、AI 调度和随机流仍是项目适配，不能视为原版逐帧复刻。
