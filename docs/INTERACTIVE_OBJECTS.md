# 第一幕可交互物体

运行时读取原始 MPQ 的 `Objects.txt`、`ObjGroup.txt`、`Levels.txt`、`Shrines.txt`、`TreasureClassEx.txt`、`ItemRatio.txt` 和物品表；不保存抽出的表副本。`world/region.cpp` 为 DS1 原对象绑定身份、操作编号、范围与动画，`world/object_population.cpp` 从 Levels 的 ObjGrp/ObjPrb 和 ObjGroup 的 ID/DENSITY/PROB 生成区域物体，`world/shrine_catalog.cpp` 按原类别和最低关卡选择祭坛效果。空间位置和随机流为项目适配，不能按相同种子逐格重现原游戏。

对象碰撞（2026-09-27 源码）：已支持外观的第一幕 DS1 对象按 D2MOO `DRLGPRESET_GetObjectIndexFromObjPreset` 的版本／Act I 索引映射确定 `Objects.Id`，不再仅凭共用 Token 选第一条记录。原表 `SizeX/SizeY`、`HasCollision0–7`、`IsDoor/BlocksVis/BlockMissile/SubClass` 生成对象占位，矩形按 `COLLISION_CreateBoundingBox` 的整数中心及偶数尺寸偏移放置，掩码依据 `UNITS_GetCollisionMask`。`Region::refreshObjectCollision` 在模式变化时重建独立对象层，保留地形及重叠物体；角色、键盘移动、寻路和全部弹体共同消费。loose rock/boulder（174/175）原表尺寸 1×1、三个常用模式均有碰撞、`BlockMissile=0`，应挡角色而允许普通弹体越过；装饰瓦片仍按 DT1 原标记，不凭画面把所有石头设为相同规则。

交互靠近会寻找角色可达的范围内站位，视线只忽略目标对象自身占位，仍阻止穿越其他物体和墙；落在原地形阻挡格上的对象保留外部 accessPoint。已支持的普通 `OperateFn=8` 门按 `OBJECTS_OperateFunction08_Door` 直接切换 NU/ON，沿用 0.5 秒操作间隔并拒绝关闭在当前单位／尸体位置上，碰撞与原模式图一起切换。锁门、特殊门模式／声音、完整单位体积和尸体挤门动画尚未接入。常规操作动画与碰撞共用 `WorldObject::modeAt`；传送点各模式目前仍沿用既有状态入口。本次无存档格式变化；仅原表与参考源码阅读、源码／文档编辑，未构建、测试、运行或打包。

对象悬停提示使用当前图形帧的非透明包围盒热区；普通名称统一用白色原 `font16`，不套用品质金色。依据本地 OpenDiablo2 `Object.Label` → `hud.go` 的 Font16 标签 → `Label.processColorTokens` 的无标记白色规则，Diablerie `StaticObject`／`Label` 也走普通名称标签。营地私人箱、传送点和 NPC 也使用同一悬停显示；打开的单次物体不再可点。鼠标指向当前可操作对象、存活怪物、传送门或地面物品时，只提高所选精灵的 RGB 亮度，保留其透明像素、原调色板及独立阴影；面板遮挡世界时不高亮。重叠目标沿用点击优先级。亮度倍率 2 参照 [OpenDiablo2 对单位的悬停渲染](https://github.com/OpenDiablo2/OpenDiablo2/blob/7f92c571bf04057a7fbdfb5d25a486f7d775e3c3/d2core/d2map/d2mapentity/animated_entity.go)及[可选物体绘制](https://github.com/OpenDiablo2/OpenDiablo2/blob/7f92c571bf04057a7fbdfb5d25a486f7d775e3c3/d2core/d2map/d2mapentity/object.go)；[Diablerie 的材质实现](https://github.com/mofr/Diablerie/blob/9e42ef257228257825ebbbeb905d4b1d894b6e98/Assets/Scripts/Diablerie/Engine/Materials.cs)也采用亮度倍增，但倍率不同。这些是参考项目的实现，尚不能断言与原版客户端逐像素一致。普通箱、棺材、尸体、隐藏物／石堆、瓮、木桶和装备架读取 Objects 的 OperateFn，按各自操作规则结算。箱子、棺材、尸体等使用本局共用的 TreasureClassEx/ItemRatio 品质及词缀流程；第一幕宝箱 TC 的 A/B/C 档由关卡等级划分，普通箱有原版 25% 空箱判定。瓮和普通木桶使用原版 `<=20` 的掉落判定。爆炸木桶使用 MPQ Damage 百分比、附近受击和爆炸画面。装备架从 MPQ 装备等级、稀有度和金属位筛选并生成一件基础装备。

祭坛从 `Objects.Parm0` 确定效果类别，再从 `Shrines` 原表选择 `Code`、名称、效果描述、持续帧数与重置分钟数。补充生命／法力的即时效果已接；有时限的加成会在画面左下显示名称和剩余秒数，战斗属性暂不生效。其他一次性魔法效果目前只显示领取提示，不施加传送、宝石变化或范围法术。井使用 `Parm0–3` 控制补水间隔、恢复比例、容量和生命／法力标志：只在资源缺损或状态可清除时消耗一份，补水后可继续使用。

原版的上锁箱、火花箱、箱陷阱、物体音效、木桶连锁爆炸、装备架优质品质，以及祭坛一次性魔法效果尚未接入。怪物尸体属于怪物系统，不是 Objects.txt 的野外 Rogue Corpse。`region.objects` 的开启、充能与祭坛持续状态只保留于本局；角色存档重开后重新生成，仍从所在幕城镇开始。

调试管道的 `objects` 可附 `interactiveOnly: true` 列出当前区域的原对象 ID、操作编号、祭坛 Code 与井水剩余次数；`interact` 走正常点击距离与寻路。`grant-shrine` 加 `code` 可不找实物，直接领取当前 MPQ `Shrines.txt` 指定 Code，便于看状态显示；`status` 返回当前限时祭坛效果。参考规则是仓库内 D2MOO 的 `OBJECTS/Objects.cpp`、`ObjMode.cpp`、`ObjRgn.cpp` 和 `ITEMS/ItemMode.cpp`，许可见 [第三方说明](THIRD_PARTY.md)。
