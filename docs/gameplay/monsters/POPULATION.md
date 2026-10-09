# MPQ人口计划与权威出生

更新：2026-10-09。本页唯一维护候选、固定身份、出生与房间调度边界；战斗规范见[COMMON](COMMON.md)，按幕覆盖见[怪物目录](README.md)。

原服和自研都由原单位分配消息驱动客户端，客户端不运行人口计划、出生或AI。`world/population`供自研hosting内容准备与独立`d2x_assets`报告共用。

## 数据与所有权

Levels的mon／nmon／umon、MonDen、MonUMin／Max，MonStats／MonStats2、MonPreset、SuperUniques、MonPlace和MonUMod决定候选、密度、族群、随从／精英及固定身份。hosting从当前MPQ准备不可变规则、真实spawn key及DS1节点；server/population调度准入，monsters实际安装实体并持有区域组件池与运行态。server不直接读表。

战斗范围、精英准入及限制统一见[第一幕](ACT1.md)与[精英](ELITES.md)。固定表记录不等于当前地图有刷新点，中立单位和不可击杀墙面发射器不混入敌对战斗池。缺规则或无法放置的人口保留暂缓身份，任务清场不能跳过这些单位。

MonStats2.spawnCol决定出生掩码，飞行／Wraith移动掩码不能替代它。原房间Populate、DS1单位／path nodes保留；空间选位、全区域初始化、房间激活与随机消耗仍有项目适配，不能宣称同种子逐单位等价。动态占位由权威服务端处理。

## 显示与证据

客户端保留classId／GUID、阶级与固定身份。替身适用条件、暂缓与完成口径唯一维护在[COMMON](COMMON.md)；NPC和中立／环境单位不可替换为敌人。

D2MOO MonsterRegion／Choose／Spawn／Unique与MonsterTbls核对规则，当前MPQ提供实际参数。验证见[第一幕有限证据](ACT1.md#有限运行证据)，表现见[表现专题](PRESENTATION.md)，许可见[资料来源](../../resources/THIRD_PARTY.md)。
