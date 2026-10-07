# MPQ人口报告与原服出生边界

联机单位出生由D2GS回包决定。world/population仅供独立d2x_assets人口计划报告，产品不会把计划转成Enemy或本地pendingSpawns，也没有房间AI调度器。

## 保留原数据

Levels的mon／nmon／umon、MonDen、MonUMin／Max、MonStats／MonStats2、MonPreset、SuperUniques、MonPlace与MonUMod用于候选、密度、随从／精英及固定身份分析。原表决定真实身份，不按示例怪物写死池或概率。

独立报告的选位／房间空间和随机流仍有适配；不能从报告输出推断原服同种子出生逐格等价。MonStats2.spawnCol用于原出生掩码分析，飞行／Wraith移动掩码不替代出生规则。动态单位拥挤／占位最终归原服。

## 显示与证据

客户端展示原服分配的真实classId／GUID。未支持的敌对类型允许沉沦魔外观并保留身份；NPC、鸡、牛等中立／环境单位不得变成敌人。替身不是该身份完整表现验收。

本地D2MOO固定版本的MonsterRegion、MonsterChoose、MonsterSpawn、MonsterUnique和MonsterTbls是规则核对入口；当前MPQ是参数来源，参考表不进入源码。模块边界见[怪物](../../modules/MONSTERS.md)，表现见[怪物专题](MONSTERS.md)，许可见[资料来源](../../resources/THIRD_PARTY.md)。
