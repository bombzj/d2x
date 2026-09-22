# 架构入口

当前模块与状态所有权统一维护在 [基线：模块与接口](baseline/ARCHITECTURE.md)。

- [数据与生命周期](baseline/DATA.md)
- [地图生成与出口](ACT1_MAPS.md)
- [怪物原表与实例](MONSTER_POPULATION.md)
- [物品与容器事务](ITEM_MODEL.md)
- [存档格式与校验](SAVES.md)
- [经典 HUD](CLASSIC_HUD.md)
- [开发与交接](baseline/DEVELOPMENT.md)

输入提交命令，UI 只读状态；内容导入、生成计划、运行实例分离；文件 I/O 不进入玩法固定步。
