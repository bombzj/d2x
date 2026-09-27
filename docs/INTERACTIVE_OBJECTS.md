# 第一幕可交互物体

运行时读取原始 MPQ 的 `Objects.txt`、`ObjGroup.txt`、`Levels.txt`、`Shrines.txt`、`TreasureClassEx.txt`、`ItemRatio.txt` 和物品表；不保存抽出的表副本。`world/region.cpp` 为 DS1 原对象绑定身份、操作编号、范围与动画，`world/object_population.cpp` 从 Levels 的 ObjGrp/ObjPrb 和 ObjGroup 的 ID/DENSITY/PROB 生成区域物体，`world/shrine_catalog.cpp` 按原类别和最低关卡选择祭坛效果。空间位置和随机流为项目适配，不能按相同种子逐格重现原游戏。

对象碰撞（2026-09-27 源码）：已支持外观的第一幕 DS1 对象按 D2MOO `DRLGPRESET_GetObjectIndexFromObjPreset` 的版本／Act I 索引映射确定 `Objects.Id`，不再仅凭共用 Token 选第一条记录。原表 `SizeX/SizeY`、`HasCollision0–7`、`IsDoor/BlocksVis/BlockMissile/SubClass` 生成对象占位，矩形按 `COLLISION_CreateBoundingBox` 的整数中心及偶数尺寸偏移放置，掩码依据 `UNITS_GetCollisionMask`。`Region::refreshObjectCollision` 在模式变化时重建独立对象层，保留地形及重叠物体；角色、键盘移动、寻路和全部弹体共同消费。loose rock/boulder（174/175）原表尺寸 1×1、三个常用模式均有碰撞、`BlockMissile=0`，应挡角色而允许普通弹体越过；装饰瓦片仍按 DT1 原标记，不凭画面把所有石头设为相同规则。

交互靠近会寻找角色可达的范围内站位，视线只忽略目标对象自身占位，仍阻止穿越其他物体和墙；落在原地形阻挡格上的对象保留外部 accessPoint。已支持的普通 `OperateFn=8` 门按 `OBJECTS_OperateFunction08_Door` 直接切换 NU/ON，沿用 0.5 秒操作间隔并拒绝关闭在当前单位／尸体位置上，碰撞与原模式图一起切换。锁门、特殊门模式／声音、完整单位体积和尸体挤门动画尚未接入。常规操作动画与碰撞共用 `WorldObject::modeAt`；传送点各模式目前仍沿用既有状态入口。存档仍为角色专用 D2S v96；构建与交付状态见 [构建](BUILD_AND_SHARE.md)。

对象悬停提示使用当前图形帧的非透明包围盒热区；普通名称统一用白色原 `font16`，不套用品质金色。依据本地 OpenDiablo2 `Object.Label` → `hud.go` 的 Font16 标签 → `Label.processColorTokens` 的无标记白色规则，Diablerie `StaticObject`／`Label` 也走普通名称标签。营地私人箱、传送点和 NPC 也使用同一悬停显示；打开的单次物体不再可点。鼠标指向当前可操作对象、存活怪物、传送门或地面物品时，只提高所选精灵的 RGB 亮度，保留其透明像素、原调色板及独立阴影；鼠标位于面板覆盖区域时不高亮，背包打开不禁用其余可见场景的悬停。重叠目标沿用点击优先级。亮度倍率 2 参照 [OpenDiablo2 对单位的悬停渲染](https://github.com/OpenDiablo2/OpenDiablo2/blob/7f92c571bf04057a7fbdfb5d25a486f7d775e3c3/d2core/d2map/d2mapentity/animated_entity.go)及[可选物体绘制](https://github.com/OpenDiablo2/OpenDiablo2/blob/7f92c571bf04057a7fbdfb5d25a486f7d775e3c3/d2core/d2map/d2mapentity/object.go)；[Diablerie 的材质实现](https://github.com/mofr/Diablerie/blob/9e42ef257228257825ebbbeb905d4b1d894b6e98/Assets/Scripts/Diablerie/Engine/Materials.cs)的地面物品悬停使用亮度 3、对比度 1.01，现已用于共用物品绘制；其他单位保留上述倍率 2。这些是参考项目的实现，尚不能断言与原版客户端逐像素一致。普通箱、棺材、尸体、隐藏物／石堆、瓮、木桶和装备架读取 Objects 的 OperateFn，按各自操作规则结算。箱子、棺材、尸体等使用本局共用的 TreasureClassEx/ItemRatio 品质及词缀流程；宝箱 TC 的 A/B/C 档由所在幕的关卡等级划分，并区分普通／噩梦／地狱，未上锁的普通箱有原版 25% 空箱判定。瓮和普通木桶使用原版 `<=20` 的掉落判定。爆炸木桶使用 MPQ Damage 百分比与附近受击；原爆裂图形尚未接入，旧演示火球显示已随技能清理删除。装备架从 MPQ 装备等级、稀有度和金属位筛选并生成一件基础装备。

## 上锁宝箱（2026-09-27）

`world/chest.cpp` 共用初始化 DS1 与 ObjGroup 生成的宝箱；`content/chest_loot.cpp` 规划掉落，`session_objects.cpp` 负责钥匙库存事务和一次性开启。界面只读取状态并发送原交互命令。概率不按 Token、角色等级或当前难度猜测，原图继续使用 Objects.Token 对应的 COF/DCC。

| 环节 | 实现与依据 |
| --- | --- |
| 原地图宝箱 | 第一幕 580/581 占位编号先按 `OBJECTS_SpawnPresetChest` 从 13 个实际 Objects.Id 中等权选择，再绑定原图；580 保留特殊掉落标志。塔楼第五层的 580 按原逻辑映射到任务物体 371，不能作为普通钥匙宝箱提前开启。 |
| 随机宝箱 | 沿 Levels.ObjGrp/ObjPrb、ObjGroup.ID/PROB/DENSITY；数量为 `density * (roomArea >> 7) >> 8`。`OBJECTS_CreateObject` 的五次找位、每轴取值范围、`SizeX+6`／`SizeY+6` 矩形与 `COLLIDE_MASK_PLACEMENT` 用于宝箱放置，跳过有传送点的房间；不再用最大边长方形、额外径向距离或 32 个上限替代箱体规则。 |
| 上锁 | `InitFn=3/57` 且 `Lockable=1`：`roll(100) < Levels.MonLvl1 / 2 + 8`，整数除法，三难度均取普通经典等级列。保留先掷陷阱、后掷锁、再初始化独立掉落种子的调用顺序。血荒地 MonLvl1=1，对应 8%；地下墓穴四层=12，对应 14%。 |
| 钥匙 | `ItemMode::D2GAME_DoKeyCheck_6FC4A4B0`：只取背包中的 `ItemTypes=key`（含类型继承），每次开锁扣 1；最后一把移除物品实例。仓库、方块、腰带均不提供钥匙。刺客免消耗。MPQ Skeleton Key 的堆叠上限仍由 Misc.maxstack=12 导入。 |
| 失败和重复操作 | 缺钥匙时不改随机流、锁、开启模式、掉落或库存；显示提示并播对应职业原 `*_needkey_1`。数据／物品生成不支持时明确 deferred，保留钥匙和箱子。成功后撤销交互，并按对象 ID 一次性结算；同局离开再回来仍已打开。 |
| 普通锁箱掉落 | `OBJECTS_OperateFunction04_Chest`：跳过 25% 空箱门槛，连续执行两次独立 TC，原表 NoDrop 仍然适用，因此不是保证有物品。每轮六件上限独立，稀有唯一品记录在轮次之间共享；不开两倍的同一份物品。 |
| 特殊宝箱 | 普通外观的特殊标志箱使用 5% Rare／95% Magic、首件品质判定及最多十轮补掷；397 按原六个概率区间、品质／补掷／金币与 hp3/mp3 分支执行，上锁不额外乘这些分支轮数。强制品质复用通用物品类型限制、词缀与特殊物品降级。额外直接生成的金币／药水按 `ITEMS_GetItemLevelForNewItem(object, 0)` 取等级 1，不套用玩家 MF/GF。 |
| TC 和等级 | `OBJMODE_DropFromChestTCWithQuality` 与 MonsterTbls：幕边界为 2/37、41/73、76/102、104/108、109/132；按难度对应 MonLvl*Ex 和三等分选 `Act X[ (N)/(H)] Chest A/B/C`。修复过去噩梦／地狱仍取普通 TC 的错误；通用解析支持五幕，当前世界生成覆盖仍为第一幕。 |
| 显示／声音／动画 | `lockedchest` 原 TBL 显示 Locked Chest；成功解锁播 Sounds.item_key_used，缺钥匙播原职业语音第一变体，声音路径均取原表。开启结束／碰撞在原 `FrameCnt1+1` 个 25 Hz 帧后切换 ON，无 OP 模式则直接 ON；帧图继续按原 FrameDelta 播放。 |

上锁位与陷阱类型分别保存，避免解锁抹掉低七位。**箱陷阱的延迟执行、陷阱单位／火焰效果仍未接入**，此次只保留其原初始化类型；不以任意伤害或沉沦魔模拟陷阱。塔楼 371 的任务释放、通用开箱声音／火花附加效果也不在当前执行覆盖中。声音变体选择、地图房间布局、原生对象控制器随机流与本项目的区域随机流不是旧客户端逐帧等价；同种子不承诺重现原地图的锁箱位置或掉落。角色磁盘存档不保存世界箱体、锁或随机流，本次没有改变 D2S 字段或旧档语义。

规则依据为本地 D2MOO 固定 `5596f5c` 的 `OBJECTS/Objects.cpp`（SpawnPresetChest、InitFunction03/57、PopulateFn3、CreateObject）、`OBJECTS/ObjMode.cpp`（Chest、ChestEnd、DropFromChestTCWithQuality）、`ITEMS/ItemMode.cpp`（DoKeyCheck、品质约束）、`ITEMS/Items.cpp`（物品等级／堆叠）、`D2Common/DataTbls/MonsterTbls.cpp`（难度 TC 名称）及当前 1.13c MPQ；许可见 [第三方说明](THIRD_PARTY.md)。

## 祭坛实际效果（2026-09-27）

`Objects.Parm0` 选择类别，`content/shrine_data.*` 导入当前 1.13c MPQ `Shrines.txt` 的 Code、Arg0/1、文本、持续帧数和重置时间，`session_shrines.cpp` 执行效果。22 个非空 Code 中，原引擎将 4→2、5→3、16→18；因此共有 19 种实际效果，调试领取也走相同映射。没有只显示提示而跳过效果的祭坛分支。

| Code／效果 | 当前执行 | 原表数值与规则依据 |
| --- | --- | --- |
| 1 补充 | 生命、法力补满 | ObjMode Refill |
| 2 生命 | 生命补满 | ObjMode Health；4 为退休映射 |
| 3 法力 | 法力补满 | ObjMode Mana；5 为退休映射 |
| 6 护甲 | 通用技能防御百分比加成 | Arg0=100，2400 帧 |
| 7 战斗 | 领取时计算平面准确率增量，并增加物理伤害 | Arg0/1=200/200，2400 帧；`OBJMODE_GetToHitPercentage` 原整数除法与职业命中因子，非直接修改最终面板乘数 |
| 8 抗火 | 火抗加成；携带禁止 burn 长度标志 | 75，3600 帧；`SUnitDmg` 对 shrine_resist_fire 的规则。现有玩家受伤流程无独立 burn 持续伤害来源，不将直接火伤视为免疫 |
| 9 抗寒 | 冰抗加成 | 75，3600 帧 |
| 10 抗闪电 | 电抗加成 | 75，3600 帧 |
| 11 抗毒 | 毒抗加成并拒绝新的中毒 | 75，3600 帧；不擅自移除领取前已有的中毒 |
| 12 技能 | 已可用技能的通用等级增加 2 | 2400 帧；D2Common Skills 的 shrine_skill 固定加成 |
| 13 法力恢复 | 通用法力恢复加成 | 400，2400 帧 |
| 14 耐力 | 补充耐力、恢复加成、蓝条；到期／被替换后再次补满 | 4800 帧；ObjMode 原规则为 Arg0=200 乘现有 `skill_staminapercent`／100，并附加恢复 1000，通常不会增加最大耐力；不是直接将容量增加 200% |
| 15 经验 | 在原等级／高等级惩罚后增加角色击杀经验 | 50%，3600 帧；不增加佣兵独立经验结算 |
| 17 传送 | 生成连通当前区域和营地的公共传送门 | ObjMode Portal；与卷轴门独立，回程不消耗，无玩家所有者；沿现有地图碰撞找可用位置 |
| 18 宝石 | 升级背包中第一个有 `betterGem` 的物品并落地；没有时生成随机碎裂宝石 | Misc.betterGem；六种颜色等权，原回退不含骷髅；成功生成后才消耗原宝石，失败回滚；16 为退休映射 |
| 19 火焰／Storm | 邻房范围内活体直接扣除当前生命的一部分，同时发射 16 个火球 | Arg0=50、Arg1=2000；原固定方向，弹体 62；等级夹在 1–8，取角色等级／5，火球伤害与人物技能／精通走共用解析 |
| 20 怪物 | 将最近符合条件的普通怪物强化为随机独特或勇士，恢复生命并接通词缀 | ObjMode Monster、MonsterUnique；MonUMod 的难度权重／排除／fPick，当前勇士概率 20%；详见 [怪物](MONSTERS.md#怪物祭坛强化) |
| 21 爆炸 | 掉落燃烧瓶，并向原六个落点投射药瓶、爆炸 | Arg0/1=5/10，即 5–9 件数量为 1 的 opm；弹体 45 与原 46 爆炸链 |
| 22 毒素 | 掉落毒瓶，向原六个落点投射药瓶并生成毒云 | 5–9 件数量为 1 的 gpm；弹体 48 与其原子云链 |

效果持续按 25 Hz 的整数帧。祭坛重置遵循 `ObjMode` 的 `gameFrame + 1200 * minutes + 1`，没有按现实一分钟误乘 1500；生命／法力及十种限时祭坛均读取各自原表重置值，经验和特殊祭坛为零时不会重置。即时恢复不擅自清除全部状态。限时效果共用 `CombatEffectSet` 和角色派生属性，按原 `States.curse` 与其他诅咒互斥，替换、到期及死亡统一撤销；耐力的移除回调在新属性重算后执行。

Code 6–15 的原状态映射集中在 `content/state_data.cpp::shrineStateName`；表现层读取原 Overlay DCC、overlay1/2、Height1–4、X/Yoffset 与 Trans。未领取时显示在祭坛上，领取后显示在玩家头上；物体使用高度索引 0、玩家使用 1，依据 `UNITS_GetOverlayHeight`。当前祭坛图为 15 帧、AnimRate=8，沿 Diablerie `Overlay.Create` 的 AnimRate×1.5 播放，Trans=3 使用软加色。怪物光环和诅咒读取各自原状态图，并分别放在单位前后层。客户端时钟、投影、定位及软加色仍是参考适配，未以字段存在代替原版逐像素验收。

投瓶、爆炸和毒云复用普通药瓶的伤害、碰撞、固定速度／飞行帧、子弹体、原图及声音入口；没有另造祭坛专用粒子或伤害常数。此前用户指出的毒雾稀疏／光点感，以及旧客户端抛物线和烟雾发射节拍，仍属于 [通用攻击](COMMON_ATTACKS.md) 的画面待核实项，本轮不声称已经复刻该客户端表现。附近房间几何、路径、公共传送门找位和随机流仍为本项目适配；跨区域休眠和佣兵战斗覆盖沿用现有系统边界。

实现只增加本局状态，不修改 D2S v96 的字段、保存语义或原 MPQ；角色读档重新生成世界，不恢复祭坛效果、怪物词缀或公共传送门。井仍按 Parm0–3 恢复、使用容量并补水。本批完成后已统一通过 Windows Release 构建并更新 dist/current，未编写或运行测试，未启动游戏；构建与交付状态见 [构建](BUILD_AND_SHARE.md)。

箱陷阱执行、通用物体音效、火花附加图形、木桶连锁爆炸、装备架优质品质尚未接入；上锁与特殊宝箱的掉落覆盖见上表。怪物尸体属于怪物系统，不是 Objects.txt 的野外 Rogue Corpse。`region.objects` 的开启、充能与祭坛持续状态只保留于本局；角色存档重开后重新生成，仍从所在幕城镇开始。

调试管道的 `objects` 可附 `interactiveOnly: true` 列出当前区域的原对象 ID、操作编号、祭坛 Code、井水剩余次数，以及宝箱的 locked/sparkly/trap/opened 状态；`interact` 走正常点击距离与寻路。`grant-shrine` 加 `code` 可不找实物，直接领取当前 MPQ `Shrines.txt` 指定 Code，执行对应实际效果；`status` 返回当前限时祭坛效果。参考规则是仓库内 D2MOO 的 `OBJECTS/Objects.cpp`、`ObjMode.cpp`、`ObjRgn.cpp` 和 `ITEMS/ItemMode.cpp`，许可见 [第三方说明](THIRD_PARTY.md)。
