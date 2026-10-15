# MPQ人口计划与权威出生

更新：2026-10-15。本页唯一维护候选、固定身份、出生与房间调度边界；战斗规范见[COMMON](COMMON.md)，按幕覆盖见[怪物目录](README.md)。

原服和自研都由原单位分配消息驱动客户端，客户端不运行人口计划、出生或AI。`world/population`供自研hosting内容准备与独立`d2x_assets`报告共用。

## 数据与所有权

Levels的mon／nmon／umon、MonDen、MonUMin／Max，MonStats／MonStats2、MonPreset、SuperUniques、MonPlace和MonUMod决定候选、密度、族群、随从／精英及固定身份。hosting从当前MPQ准备不可变规则、真实spawn key及DS1节点；server/population调度准入，monsters实际安装实体并持有区域组件池与运行态。server不直接读表。

战斗范围、精英准入及限制统一见[第一幕](ACT1.md)与[精英](ELITES.md)。固定表记录不等于当前地图有刷新点，中立单位和不可击杀墙面发射器不混入敌对战斗池。缺规则或无法放置的人口保留暂缓身份，任务清场不能跳过这些单位。

MonStats2.spawnCol决定出生掩码，飞行／Wraith移动掩码不能替代它。原房间Populate、DS1单位／path nodes保留；空间选位、全区域初始化、房间激活与随机消耗仍有项目适配，不能宣称同种子逐单位等价。动态占位由权威服务端处理。

普通难度精英候选现按`MONSTERCHOOSE_GetPresetMonsterId`在umon选择后直接返回，不执行PlaceSpawn替换或消耗该替换随机；噩梦／地狱仍走区域稀有度选择及PlaceSpawn。新增`gameplay/monsters/region_generation`以纯值目录按关卡ID顺序执行NumMon删选、首个RangedSpawn最多20次重抽及组件池投掷。宿主从当前MPQ准备缓存规则，实例与宿主调用同一算法；实例先分怪物区域子流，再分对象控制子流，首次人口准入校验蓝图目录与组件初值。服务端区域计划已不再自行抽取另一份目录；世界工具默认路径仍保留旧目录适配。原密度布置、精英预算与完整全局初始化交错尚未迁入，不能由目录一致推断实际人口一致。2026-10-15这批通过Windows Release的CMake重配置及d2x、d2x_pvpgn编译链接，未运行／打包；物件生成新编译单元的编辑器解析诊断未复现为编译错误，见[物件](../world/OBJECTS.md#自然生成源码边界)。

Levels的mon／nmon／umon现分别读取原25槽，在首个空／未链接MonStats条目处停止，不压缩空洞。Hit39另保留完整mon列表及isSpawn，不借用NumMon选择后的区域人口，也不改读噩梦／地狱的nmon。`nativeLevelSpawnClass`保留先随机索引、再前进一位才检查的顺序；只尝试首个合格类型，放置失败不换类型。

地图准备在房间瓦片完成后，按DRLG_CreateRoomForRoomEx多抽取一次原房间流，再以low／high=666准备独立活动房间种子；后续接缝选瓦片仍使用已前进的原房间流。生成数据只携带不可变初值，运行随机唯一归AreaStore::RoomState，RoomRandomLoan复验区域代次、房间随机revision及期望值。离区卸载清除运行流，重建使旧贷款失效；完整房间重建和原人口／对象对该流的消费顺序尚未迁完。

普通怪物与NPC的出生候选先从全局流创建实际单位种子，再完成组件选择、特殊DoomKnight组件抽样和HP投掷；HP使用单位流。非恶方按一人初始化，恶方人数包含已入场的死亡玩家及当前虚拟人数。组件池候选、Actor、房间登记和全局随机准备完成后一起安装，失败不提前消耗ID或组件池。四种原城镇居民跳过区域组件池。这只闭合上述出生阶段，MonProp／技能被动及初次AI等完整交错仍需后续迁移。

Hit39已接`monsters::prepareLevelSpawn`及独立NativeBirthPublication。非水面分支使用1.13c实际SpawnNormalMonster的半径3／6／…／15、随机周界起点、先移动后探测及首个成功即停止，复用同步动态碰撞和巢穴／石棺等附加占位检查；无落点仍发布已消费的房间随机。房间流、全局流、实际Actor、ID与组件候选共同发布，不借用巢穴子怪接口或玩家归属。缺原型、水面生成及附魔特殊出生明确未实现；不以沉沦魔或无操作认证该路径。固定D2MOO的SpawnNormalMonster函数体本身为空，普通落点分支因此以本地1.13c D2Game RVAEEF10移植，Hit39／列表选择依据RVA3BC00／EF6B0；字节材料在artifacts/missile-native-20261015/。本节源码已纳入2026-10-16的all构建与两包，未专项运行验收Hit39的各分支。

## 显示与证据

后续工作区已按活动房间创建序号调度，物件／NPC／预设怪物及环境陷阱共用DS1源序号，但按SUNIT_SpawnPresetUnitsInRoom分两遍：物件先于NPC／怪物，各遍保留源序号。异步物件主题会停留在Population阶段，不重复执行World／Social。NPC出生记录不再由当前在场状态推导。`RoomPopulationLayout`导出LvlPrest.Logicals的区域矩形、节点位及格索引，尚未用于实时密度。公共`choosePopulationMonster`／`choosePopulationGroup`已接旧规划器，补全精英进度抽签和非精英末次抽签；仍使用旧规划流与落点，不代表实际人口已一致。

实例启动另补NPCControl、QuestControl子流；石柱和日蚀用任务流，五类随机状态均可只读诊断。精英生命改为先掷基础生命／人数加成，再按定点生命执行UMod2及冠军修正；词缀、随从、姓名仍在宿主预排，首只出生后的完整单位随机顺序未迁入。上述后续源码已随2026-10-16刺客交付完成CMake配置、all构建与打包，实际产物见运行包记录；本轮刺客冒烟不认证完整自然生成。完整密度、精英、预设特殊程序、原GUID与房间恢复仍是当前阻断项。

客户端保留classId／GUID、阶级与固定身份。替身适用条件、暂缓与完成口径唯一维护在[COMMON](COMMON.md)；NPC和中立／环境单位不可替换为敌人。

D2MOO MonsterRegion／Choose／Spawn／Unique与MonsterTbls核对规则，当前MPQ提供实际参数。验证见[第一幕有限证据](ACT1.md#有限运行证据)，表现见[表现专题](PRESENTATION.md)，许可见[资料来源](../../resources/THIRD_PARTY.md)。

当前同级房间近邻按DRLG两轴间隙均小于6个tile判定；目标资格和第一幕死亡恐惧复用这份规则。鸟巢和石棺区分成功施放与实际子怪出生，堵塞是否消耗间隔按各自原AI执行，见[第二幕](ACT2.md#三难度重新核对)。跨区域完整活动房间与原动态碰撞缓存仍未完全还原。
