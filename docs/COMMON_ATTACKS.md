# 通用攻击与导弹效果

当前源码实现普通攻击、Throw、Left Hand Swing、Left Hand Throw 的基础执行链；**不能标为全面复刻**。主体实现曾编译并更新 dist/current；2026-09-27 按当前 1.13c MPQ 与本地 reference 补查瘟疫标枪、爆炸箭和全部六种投掷药瓶，修正瓶体速度、范围过滤及毒云碰撞／到期顺序，本次修订未构建、测试、运行或打包。未实现的职业技能、Kick 和 Unsummon 不借用这些动作。

## 数据与入口

| 层 | 入口与职责 |
| --- | --- |
| 内容 | `content/skills/skill_animation.*` 从 `Skills.anim`、`AnimData.d2` 与原 COF 建立职业／武器类／动作的帧数、速度及动作事件；`item_projectiles.*` 解析武器弹体、投掷种类与资源。 |
| 效果适配 | `content/skills/missile_effects.*` 导入固定表值的范围命中、命中视觉与毒云子导弹；技能公式须在内容／技能解析层求值，不能在玩法中按物品名猜测。 |
| 派生数值 | `items/equipment_stats.*` 为每把有效武器分别派生伤害、命中、IAS、基础速度、射程和目标类型加成。普通攻击不再轮流混用两把武器；显式左手动作选择左手。 |
| 攻击过程 | `combat/attacking.cpp` 创建 `WeaponAttackState`，按 25 Hz 推进，在动作事件帧进行近战检定或发射；`weapon_attack.*` 提供帧量化、近战距离和弹体／单位占位求交。 |
| 弹体 | `physical_projectiles.cpp` 保存发射时的伤害、命中、等级与元素快照；`missile_effects.cpp` 执行共享范围命中、落点命中及移动毒云。 |
| 表现 | 人物读取同一个攻击帧状态；弹体及命中效果只按原 ID 加载 DCC、动画列与声音。`presentation/world/projectile_view.cpp` 管理无伤害的客户端子效果及独立随机流，不占用玩法实体 ID。攻击开始时保留装备定义及武器类，最后一枚投掷物耗尽不会在动作中途换成空手图。 |

UI 仍只提交 `Attack`／`UseSkill`。Shift 原地攻击保留当前指向的单位；近战出手时再次验证距离，空地挥击不制造范围伤害。弓弩和投掷允许瞄准空地。死亡、受击及换区会清除未释放攻击；切换武器组取消当前动作和待追击目标。

## 武器组件与朝向

角色世界动画与角色列表预览共用 `presentation/actors/equipment_appearance.hpp::equippedHandComponent`：只有 `1hs`／`1ht`／`ht1` 武器按手位选择 RH／LH，其余保持原表 `component`。依据本地 D2MOO `D2Common/src/D2Inventory.cpp::INVENTORY_GetCompositItem`。当前 MPQ 的短弓 `sbw` 使用组件 6（LH），女巫 `SOA1BOW.cof` 包含 LH 而无 RH，对应原图 `chars/so/lh/solhsbwa1bow.dcc`；旧代码把所有主手武器强制放到 RH，导致弓被遗漏。这里只修正图层选择，双持主手仍遵循下文的项目边界。

`presentation/graphics/graphics/graphics/primitives.cpp::direction` 接收世界坐标方向，在不压缩纵轴的 45° 旋转坐标中量化，再映射原 DCC 的 8／16／32 方向索引。依据 Diablerie `Engine/Iso.cs::Direction`、`Engine/IO/D2Formats/DirectionMapping.cs` 与 `Engine/Entities/Missile.cs::Create`；当前 MPQ `missiles/arrow.dcc` 为 32 方向、每方向 1 帧。原图已经包含等角投影，不能再用 `project()` 的半高屏幕角度选择方向帧。人物、佣兵、怪物与弹体共用该入口，运动、碰撞及伤害不变。

2026-09-28 上述两项显示修订仅完成源码及原资源／reference 对照；按当前交接约定未构建、运行测试或游戏、打包，现有 `dist/current` 不含本次修订，实际画面待验收。

## 动作与基础数值

- 普通攻击使用 A1，投掷使用 TH，左手挥击／投掷使用 S3／S4。读取原动作事件 1／2；缺原动画记录则拒绝动作，不使用固定攻击间隔或其他攻击动画替身。
- 采用 D2Common `UNITS_UpdateAttackAnimRateAndVelocity` 的 IAS 递减公式、基础武器速度及 15%–175% 速率范围；双持基础速度取两武器平均。Amazon／Sorceress 的近战起始帧按 `UNITS_GetFrameBonus` 跳过；表现与命中使用同一计时。
- 空手以 1–2 基础伤害及力量增伤计算；装备武器按原单／双手或投掷基础值、局部增强、固定加值及角色增伤派生。物理和即时元素以 1/256 生命单位掷值；上下限不同时原范围不包含上限。
- 近战先对上下限分别增伤，再掷值；武器弹体先掷原伤害，再应用快照中的百分比。箭矢／普通投掷不继承 `item_normaldamage`；药瓶 `SrcDamage=0`，不带入力量、装备附伤、致命一击或吸取。
- 武器自身命中、IAS 和元素只归属本武器。对恶魔／不死系的命中加值在命中百分比前加入；近战钝器对不死系增加 50% 物理增伤。对恶魔／不死系伤害、忽略防御及百分比减防按真实 MonStats 身份和目标等级／类别结算，不能按显示替身判类别。
- 命中沿用原等级／准确率／防御公式及 5%–95% 限制。原怪物防御或抗性数据缺失时明确拒绝对应结算，不按零防御／零抗性补值。
- 近战距离取 `Weapons.rangeadder`、原单位大小和 D2Common 距离表。物理弹体、毒云及直线单体法术沿扫掠线段与原单位占位求交，并先裁切地形阻挡；不是原引擎定点路径逐随机流等价实现。

弓消耗匹配箭袋、弩消耗匹配弩矢袋；掷斧、飞刀、标枪等消耗对应武器堆叠。出手前仅检查，到了发射帧才扣除一枚；受击取消准备动作不扣。发射前复制数值，因此最后一枚消耗引起的装备重算不影响本次弹体。近战命中仍走共用耐久流程，射击不触发近战武器磨损。武器弹体按 `MISSMODE_HandleMissileCollision` 先判断到期，再查询直接命中；触发到期效果后剩余时间明确归零并在同帧移除，避免浮点微小余量让爆炸箭／瘟疫标枪下一帧重复触发子弹体。

## 爆炸与毒云的共享结构

reference 的直接依据：

- `D2Game/src/MISSILES/MissMode.cpp::MISSMODE_SrvHit01_Fireball_ExplodingArrow_FreezingArrowExplosion`：火球、爆炸箭与冻结箭共用范围命中算法。
- `MISSMODE_SrvHit03_ExplosivePotion_BombOnGround`：落地时委托 `MISSMODE_SrvHit44_ExplodingJavelin`；与上项一起经过 `Skills.cpp::sub_6FD10200` 进行范围目标处理。
- `MISSMODE_SrvHit02_PlagueJavelin_PoisonPotion` 和 `MISSMODE_CreatePoisonCloudHitSubmissiles`：瘟疫标枪与毒气瓶共用毒云生成，不是药瓶专有能力。
- `MISSMODE_SrvDo03_PoisonCloud_Blizzard_ThunderStorm_HandOfGod`：云团作为子导弹移动并执行单位接触；中毒覆盖规则见 `SUnitDmg.cpp::SUNITDMG_ApplyPoisonDamage`。

玩法的 `MissileImpactSpec` 独立描述范围、命中视觉和可选的 `PoisonCloudBurstSpec`；`MissileImpactDamage` 保存本次命中的各伤害通道和持续时间。效果执行器不判断药瓶代码或技能名。范围命中按整数子格半径筛选，各伤害通道分别过抗性后只产生一次直接受击／死亡；毒素单独进入持续伤害入口。原 `SrvHit01/44 → sub_6FD10200` 默认过滤值 `0x8583` 不含 `sub_6FD0FA00` 的视线检查位 `0x200`，已移除旧实现额外施加的落点至目标阻弹视线限制；弹体飞行仍受地形阻挡。火球也复用这条修正，火弹／冰弹／冰风暴通过同一接口播放直接命中效果。

毒云配置将叶子云团与扩散分开：`PoisonCloudSpec` 保存原弹体 ID、毒伤速率、持续时间、寿命及大小，`PoisonCloudBurstSpec` 保存两圈的方向步长与速度。云团按原 16 方向序列发射；第二圈从方向 1 起按 `subStep` 递增（步长 1 包含奇偶方向，步长 2 才只取奇数方向）。`LastCollide` 只排除上一次接触的单位。毒云、普通箭／标枪和药瓶共用地形路径裁切；毒云也检查墙前路径，避免整段漏判。按 `MISSMODE_HandleMissileCollision` 先判断寿命到期，再查询单位；到期帧不再施毒。命中后保存已经过抗性的毒素速率；较弱中毒不替换较强中毒，相同或更强速率重置持续时间，不累加伤害。持续毒素不会每帧重播受击动画。

瘟疫标枪（25）和爆炸箭（16）已经复用这条执行链，详见 [亚马逊技能](AMAZON_SKILLS.md)。`AreaMissileSpec` 将爆炸箭的一帧服务器子弹体与即时命中视觉分离；子弹体继承父弹等级，创建时读取来源角色的当前数值，主弹保留发射快照。冻结箭和其他尚未导入的技能不会因此自动开放。当前共享执行器的目标仍是当前区域内活跃敌人；召唤物、敌对施法者、PvP／友军规则及跨区域伤害须先补齐目标体系。

## 当前 MPQ 的投掷药瓶

以 `Weapons.missiletype` 为准，不能根据物品英文名与 Missiles 行名是否相似重新映射。当前表的对应关系如下；数字从 MPQ 导入，不写入玩法代码。

2026-09-27 装备入口修订：当前 MPQ `ItemTypes.txt` 的 Missile Potion (`tpot`) 为 `Body=1`、`BodyLoc1=rarm`、`BodyLoc2=larm`、`Throwable=1`，并经 `thro → weap` 继承武器类型；D2MOO `D2Common/src/Items/Items.cpp::ITEMS_GetAllowedBodyLocations`／`ITEMS_CheckBodyLocation` 也从 ItemTypes 判断允许部位。项目原先在 `InventoryService::equipmentRequirements` 无条件排除 `tpot`，导致已实现弹体规则无法从背包装备进入；现移除这条旧排除。背包右键和手位拖放继续走共用穿戴事务，普通近战仍拒绝药瓶，Throw 才消耗堆叠并发射原 `missiletype`。当前运行实例是修订前二进制，未用它验证源码修复；按交接约定本轮未构建、运行新程序或打包，实际装备与投掷仍待用户验收。

| 物品代码 | 弹体 ID | 当前命中数据 |
| --- | --- | --- |
| `opl` | 44 | 物理 2–7、火焰 3–8，范围 2；原 `fireexplode` 命中图。 |
| `opm` | 45 | 物理 8–12、火焰 8–18，范围 3；原 `explodingarrowexp` 命中图。 |
| `ops` | 46 | 物理 13–30、火焰 13–34，范围 6；原 `explosivepotionexp` 命中图及 `CltHitSubMissile2/3` 的碎片二选一。 |
| `gpl` | 47 | 云团 222，毒伤速率 123/256 生命／帧，每次中毒 50 帧，8 个子导弹。 |
| `gpm` | 48 | 云团 223，速率 246/256 生命／帧，每次中毒 50 帧，16 个子导弹。 |
| `gps` | 49 | 云团 224，速率 369/256 生命／帧，每次中毒 50 帧，23 个子导弹。 |

药瓶使用 `CollideType=6`，经过怪物时不提前破碎，到目标距离对应的到期帧或地形阻挡处触发效果。`Skills.cpp::sub_6FD11710` 使用 `0x400` 标志；`MISSILES_CreateMissileFromParams` 仅按 `max(1, UNITS_GetDistanceToCoordinates) × 4096 / 原路径速度` 截断得到剩余帧，不改变速度。当前 Vel 16 经 75% 得 3072 路径单位／帧，即 18.75 子格／秒；已删除为了强行到达鼠标精确坐标而重算速度的旧适配。距离采用每轴整数子格的 `max(dx,dy) + min(dx,dy)/2`；飞行方向仍使用项目连续坐标，未移植原方向查表与逐帧整数路径，不能宣称落点逐子格一致。

三个毒云均按原 Range 60、SubLoop、SubStart 10、SubStop 19 和三次附加循环得到 87 帧寿命；这与每次中毒 50 帧是两种独立计时。毒伤取云团自身列，不能把父药瓶的 EMin 再附加一次。固定伤害导入器会拒绝尚未解析的技能／弹体来源伤害、等级成长、公式或支配加成；毒云也校验 CollideType、LastCollide、ToHit、NextHit 等执行器依赖的字段，避免未来新弹体静默使用不完整规则。

客户端 `CltHit03/HitOilPotion` 的主爆炸加碎片二选一，补充核对 [D2R Data Guide 的 Missiles 函数说明](https://locbones.github.io/D2R_DataGuide/#missilestxt)，只借用函数用途，不取代当前 MPQ 数值。药瓶 46 的原碎片 52/54 使用正常透明索引绘制（Trans 为空），不是火焰的软加色；无 Range 的 Explosion 图按自身 29 帧非循环动画退场。碎片存于 SceneView，在换区／恢复会话时清除，不参与碰撞、伤害或存档。主爆炸 51 的 Range=12、AnimLen=29 存在差异；其客户端是否另行重设寿命尚未核实，暂保留原 Range，不凭画面感受延长。

## 尚未完成的原版细节

- 药瓶 `SpecialSetup` 的客户端抛物线，以及 `PoisonSparks → poisonpuff` 的客户端发射节拍，现有 reference 未提供足够实现证据。用户反馈毒雾太稀、像散开光点，与未生成 poisonpuff 原烟雾一致。RandStart、云团循环音也未完成。基础动画速率依据 D2MOO `Units.cpp` 的 UNIT_MISSILE 分支（本批 animrate 1024 对应 25 帧／秒），软加色依据 Diablerie `MissileInfo.Load`；这些依据不覆盖上述缺失客户端行为。RandStart 的旧版社区指南与 D2R 字段说明存在“固定起始帧／随机起始帧”冲突，未选择一种猜测实现。
- 火球额外爆炸散布图块、投射物动态光照、逐武器命中／挥击声音选择和完整原版调色板效果尚未完成。
- 物品穿透、怪物格挡、武器专精／致命攻击被动、命中触发／充能、完整元素支配与武器附伤交互、MonType 分层加成和原受击恢复门槛尚未完整接入。已有会话只对已解析的普通怪物提供完整防御／抗性；精英／首领规则仍按怪物模块的边界执行。
- 双持普通攻击仍使用项目的右手优先主手定义，未复刻原装备顺序带来的主手选择；完整双持专属动作、变形／序列技能速度与取消窗口不在本次完成范围。
- 保留原 D2S v96 的字段和语义；攻击准备、装备外观快照、导弹效果和毒云均不跨局保存，不迁移历史内部存档。

状态容器的生命周期及后续 Buff 扩展见 [技能与状态](SKILLS.md#状态效果系统)，数值属性消费者见 [战斗数值](COMBAT_NUMBERS.md)。
