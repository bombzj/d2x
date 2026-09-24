# 第一幕可交互物体

运行时读取原始 MPQ 的 `Objects.txt`、`ObjGroup.txt`、`Levels.txt`、`Shrines.txt`、`TreasureClassEx.txt`、`ItemRatio.txt` 和物品表；不保存抽出的表副本。`world/region.cpp` 为 DS1 原对象绑定身份、操作编号、范围与动画，`world/object_population.cpp` 从 Levels 的 ObjGrp/ObjPrb 和 ObjGroup 的 ID/DENSITY/PROB 生成区域物体，`world/shrine_catalog.cpp` 按原类别和最低关卡选择祭坛效果。空间位置和随机流为项目适配，不能按相同种子逐格重现原游戏。

对象悬停提示使用当前图形帧的非透明热区。营地私人箱、传送点和 NPC 也使用同一悬停显示；打开的单次物体不再可点。普通箱、棺材、尸体、隐藏物／石堆、瓮、木桶和装备架读取 Objects 的 OperateFn，按各自操作规则结算。箱子、棺材、尸体等使用本局共用的 TreasureClassEx/ItemRatio 品质及词缀流程；第一幕宝箱 TC 的 A/B/C 档由关卡等级划分，普通箱有原版 25% 空箱判定。瓮和普通木桶使用原版 `<=20` 的掉落判定。爆炸木桶使用 MPQ Damage 百分比、附近受击和爆炸画面。装备架从 MPQ 装备等级、稀有度和金属位筛选并生成一件基础装备。

祭坛从 `Objects.Parm0` 确定效果类别，再从 `Shrines` 原表选择 `Code`、名称、效果描述、持续帧数与重置分钟数。补充生命／法力的即时效果已接；有时限的加成会在画面左下显示名称和剩余秒数，战斗属性暂不生效。其他一次性魔法效果目前只显示领取提示，不施加传送、宝石变化或范围法术。井使用 `Parm0–3` 控制补水间隔、恢复比例、容量和生命／法力标志：只在资源缺损或状态可清除时消耗一份，补水后可继续使用。

原版的上锁箱、火花箱、箱陷阱、物体音效、木桶连锁爆炸、装备架优质品质，以及祭坛一次性魔法效果尚未接入。怪物尸体属于怪物系统，不是 Objects.txt 的野外 Rogue Corpse。`region.objects` 的开启、充能与祭坛持续状态只保留于本局；角色存档重开后重新生成，仍从所在幕城镇开始。

调试管道的 `objects` 可附 `interactiveOnly: true` 列出当前区域的原对象 ID、操作编号、祭坛 Code 与井水剩余次数；`interact` 走正常点击距离与寻路。`grant-shrine` 加 `code` 可不找实物，直接领取当前 MPQ `Shrines.txt` 指定 Code，便于看状态显示；`status` 返回当前限时祭坛效果。参考规则是仓库内 D2MOO 的 `OBJECTS/Objects.cpp`、`ObjMode.cpp`、`ObjRgn.cpp` 和 `ITEMS/ItemMode.cpp`，许可见 [第三方说明](THIRD_PARTY.md)。
