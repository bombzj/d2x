# 已退场的本地会话

更新：2026-10-07。产品仅支持 D2GS 联机。本地 GameSession、Simulation、SkillRuntime、InventoryService 及其任务／AI／战斗／奖励／库存执行源码已删除，d2x_session 目标已移除；此前本地实现与验证记录可从 Git 历史查看。没有本地备用后端，也没有断线转单机路径。

当前会话由 network/realm_session 管理原服连接、局前、入局与保存退局；UI 读取 client 的值契约并提交原服命令。世界、动画、鼠标／UI、人物／技能／任务提示和声音共用一条表现链，见[客户端](CLIENT.md)、[联网模块](NETWORK.md)与[实际依赖](../architecture/OVERVIEW.md)。

仍有独立用途的原资源／地图／掉落报告和 D2S 编解码保留。CharacterSaveData 已移到 src/persistence/character_save.hpp，字段、D2S v96 编码、规则指纹与存档语义均未改变；客户端不链接 persistence，不读取本地角色档入局。存档工具与限制见[SAVES.md](SAVES.md)。

保留的 gameplay 与 items 只有显示／几何／等级／资源工具计算及原表／存档值。缺原服事实保持未知；不调用旧任务、库存或技能执行器来补功能。旧验证只认证当时版本，当前交付与有限冒烟范围统一见[基线](../../BASELINE.md)。
