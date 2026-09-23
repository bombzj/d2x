# 怪物实施计划

怪物身份、区域名单、分组和掉落继续以运行时 MPQ 为准；类型替身只用于尚未实现的敌对外观。一个怪物完成外观不表示其 AI、精英词缀或战斗帧已与原版一致。

## 分步边界

1. **普通近战基础数值：已完成。** `content/monster_combat.*` 从运行时 `MonStats.txt` 的 `minHP/maxHP/A1MinD/A1MaxD/noRatio/Level` 和 `MonLvl.txt` 的 `L-HP/L-DM` 解析普通难度基础区间；`Simulation` 按真实身份在生成时掷生命、攻击时掷 A1 伤害。只支持普通级别、非 Boss、非远程记录；其他类型保持明确的既有适配值。敌人最大生命随 v23 存档保存并校验，血条使用该实例的最大生命。
2. **单个普通怪物外观：已完成。** `brute1` 使用 MPQ `YE` 的 NU/WL/A1/DT COF/DCC；原 `MonStats2` 只有躯干组件，四种动作资源均在本机 MPQ 中。该身份从沉沦魔类型替身切换为原外观，仍沿用公共近战决策。`brute2` 等不同变体继续为替身。
3. **下一项候选：行为基线。** 先核对本地 D2MOO 的原 AI 事件时机和当前 MPQ 的 `AI/aip*/MonStats2`，再选一种简单普通怪物实现专属决策，逐个验证追击、攻击与存读档。未核实的计时和视距不填固定猜测值。
4. **后续阶段：** 远程弹体与技能、尸体复活、各难度与等级缩放、抗性和元素伤害、精英／首领词缀和 Boss 特性。每项同时核对原资源、玩法状态、存档语义和调试接口；必要时独立成小文件，不累加到公共 AI 大文件。

当前数值计算依据本地 `reference/d2moo/source/D2Common/src/DataTbls/MonsterTbls.cpp` 的 `DATATBLS_CalculateMonsterStatsByLevel` 和 `reference/d2moo/source/D2Game/src/MONSTER/Monster.cpp` 的生命掷骰。区间值始终在启动时从用户挂载的 MPQ 读取，参考仓库不作为运行时数据源。当前攻击触发时刻、AI 决策和随机流仍是项目适配，不能视为原版逐帧复刻。
