# PD2地图与区域差异

本页维护地图数据、区域程序及地图词缀的边界。开图及词缀消费者详见[地图物品与词缀](MAP_PROPERTIES.md)。版本见[版本与证据](BUILDS.md)；怪物身份归[怪物](MONSTERS.md)，物品生成／方块算法归[物品](ITEMS.md)，公共地图生成与碰撞归[地图模块](../modules/MAP.md)。

## Levels与LevelDef链接

已确认：Levels.txt的Warp空白按原DWORD编译为0，显式-1保留-1。当前目标2–7号区域的Warp3为空；配套`leveldefs.bin`有211条、每条156字节，Warp数组偏移0x68，相关数组为`[-1,-1,-1,0,1,2,3,-1]`，6号区域第三项为10。空白默认-1会把带洞口warp的连接误判为相邻野外连接。

依据为D2MOO的`DATATBLS_LoadLevelDefsBin`与目标配套BIN。每条544字节的`levels.bin`是另一张运行表，不能套用LevelDef结构偏移。消费者包括`src/content/world/world_catalog.cpp`与公共world房间／出口准备；读取字段须保留空白、0、-1的编译区别，不能为让地图连通而改原资源。

Levels.MonLvl1及Objects数值空白按原编译类型为零，来源为D2MOO的LevelsTbls／ObjectsTbls和配套BIN；缺列仍是未知，不能与已有列的空白混为一谈。MonStats2字段宽度另见[怪物](MONSTERS.md#编译字段宽度与monprop)。

## 普通五幕与扩展区域

当前资源Levels有211条（含0），137–210为74个扩展区域，全部DrlgType=2，各有唯一LvlPrest绑定。其中包括普通地图、多层地图、首领／事件子场地、PvP、BR和公会场地，不能统一当成普通刷图。

`src/hosting/pd2/town_host.cpp::validateArea`允许当前表内的扩展预设布局；仍要求有效幕、正尺寸、唯一LvlPrest及完整原资源，由公共生成器校验。共用NativeMapGenerator按Levels尺寸／Offset／Depend、LvlPrest.Files／Dt1Mask和真实DS1／DT1准备房间、选砖、出口与权威碰撞。LvlPrest.SizeX/Y为0时沿用Levels尺寸；DS1宽／高包含边界格，不能直接当区域tile尺寸。

区域归幕使用Levels.Act，预设单位的幕内索引使用文件内DS1.Act，二者不能互相覆盖。202–206的LvlPrest.Populate=0，不套用普通地图密度生成敌人。管理travel只接受当前角色规则内的Levels身份，原版仍为1–136；PD2扩展区域未指定坐标时沿原tile11／固定位置后备与权威空位搜索入场。管理旅行不授予开图、事件、PvP或首领资格。

人口由公共`prepareMonsterRegionRules`／`prepareCombatPopulation`消费目标Levels.NumMon、mon/nmon、MonDen、MonUMin/Max及区域等级；仅准备城镇的`prepareTownRegionRules`不是扩展区域入口。未实现敌对AI仍依项目授权使用沉沦魔载体并保留真实身份；中立控制器不换敌人。特殊首领、事件、词缀和专属掉落没有因此补齐。

扩展身份的技能准备由`src/hosting/pd2/monster_actions.cpp`按实际技能程序接入，原图和基础数值继续随真实MonStats身份读取；动作事件、链编号及首领专属AI的接入与限制见[怪物基线](MONSTERS.md#怪物技能程序准备)。区域地形可进入、敌对替身可战斗和真实怪物程序可执行须分别判断，不能用其中一项证明地图完整可玩。

## 扩展区域索引

入口仅记录当前Misc.len；不代表物品已有自有开图执行。尺寸为Levels默认难度字段，噩梦／地狱仍各读SizeX/Y(N/H)；变体数来自唯一LvlPrest绑定。

| Level | 内部名 | 尺寸（tile） | 变体 | pSpell12物品 |
| --- | --- | --- | --- | --- |
| 137 | UberDiabloLvl | 47×23 | 1 | dcma |
| 138 | Mesa | 150×150 | 6 | — |
| 139 | Siege Map | 199×199 | 2 | t23 |
| 140 | Palace | 150×150 | 6 | — |
| 141 | Sewers | 179×179 | 3 | t25 |
| 142 | Arcane | 179×179 | 2 | t12 |
| 143 | Kurast | 150×150 | 3 | t11 |
| 144 | River Of Blood | 240×240 | 4 | t31 |
| 145 | Lava | 249×249 | 3 | t21 |
| 146 | Ice Map | 199×199 | 3 | t34 |
| 147 | Desert Map | 249×249 | 1 | t39 |
| 148 | Jungle Map | 250×250 | 3 | t22 |
| 149 | Fortress Map | 180×105 | 3 | t13 |
| 150 | Throne Map | 169×105 | 3 | t32 |
| 151 | Tomb Map | 249×249 | 4 | t24 |
| 152 | Monastery Map | 190×240 | 1 | t41 |
| 153 | Monastery Map Basement | 22×31 | 1 | — |
| 154 | Graveyard Map | 190×240 | 3 | t35 |
| 155 | Caldeum Map | 179×159 | 3 | t36 |
| 156 | Pandemonium Map | 179×179 | 3 | t37 |
| 157 | Pvp Map | 179×159 | 1 | t61 |
| 158 | Spider Map | 249×249 | 3 | t33 |
| 159 | Pvp Map 2 | 190×240 | 1 | t62 |
| 160 | Frozen Forest Map | 149×149 | 3 | t38 |
| 161 | Necropolis Jungle | 179×159 | 1 | rtma |
| 162 | Necropolis Swamp | 179×159 | 1 | — |
| 163 | Necropolis Void | 150×150 | 1 | — |
| 164 | Realm of Terror | 201×200 | 3 | t42 |
| 165 | Hole of Terror | 67×91 | 1 | — |
| 166 | BR Arena | 190×240 | 1 | — |
| 167 | Library Map | 150×150 | 3 | t14 |
| 168 | Uber Ancients | 20×28 | 1 | uba |
| 169 | Westmarch Map | 159×149 | 3 | t26 |
| 170 | Crypts Map | 149×149 | 3 | t15 |
| 171 | Sanctuary Of Sin Map | 169×199 | 3 | t43 |
| 172 | Black Abyss Map | 149×149 | 1 | — |
| 173 | Butcher Event Map | 59×59 | 1 | — |
| 174 | Ruined Cistern Map | 149×189 | 3 | t16 |
| 175 | Ashen Plains Map | 200×200 | 3 | t3a |
| 176 | Zhar Library | 49×49 | 1 | t51 |
| 177 | Zhar Rivers | 169×199 | 1 | — |
| 178 | Zhar Ice | 148×148 | 1 | — |
| 179 | Zhar Kurast | 149×149 | 1 | — |
| 180 | Outer Void Map | 200×200 | 1 | t55 |
| 181 | Hellcaves | 150×150 | 1 | t52 |
| 182 | Hellcave Fortress | 150×75 | 1 | — |
| 183 | Fallen Gardens | 150×150 | 1 | t53 |
| 184 | Diamond Gate | 75×75 | 1 | — |
| 185 | PD2 Pandemonium Finale | 43×48 | 1 | ubtm |
| 186 | Kanemith Outside | 149×149 | 1 | t44 |
| 187 | Kanemith Dungeon | 149×149 | 1 | — |
| 188 | Lucion Arena | 49×49 | 1 | luca |
| 189 | Lucion Vault | 60×60 | 1 | — |
| 190 | Skovos Stronghold Map | 179×149 | 2 | t28 |
| 191 | Demon Road Map | 200×200 | 4 | t27 |
| 192 | Imperial Palace Map | 149×149 | 1 | t54 |
| 193 | Halls of Torture Map | 149×149 | 3 | t17 |
| 194 | Imperial Boss Map | 25×25 | 1 | — |
| 195 | Ureh City Map | 149×149 | 1 | t56 |
| 196 | Nemyr Monastery Map | 32×32 | 1 | — |
| 197 | Djinns Domain Map | 124×124 | 1 | t57 |
| 198 | Djinns Domain Red Map | 124×124 | 1 | — |
| 199 | Djinns Domain Boss Map | 19×19 | 1 | — |
| 200 | Na-Krul's Abyss Map | 149×149 | 2 | t58 |
| 201 | Kyovoshad Map | 200×200 | 2 | t3b |
| 202 | Guild 1 | 39×39 | 1 | — |
| 203 | Guild 2 | 49×49 | 1 | — |
| 204 | Guild 3 | 54×51 | 1 | — |
| 205 | Guild 4 | 54×54 | 1 | — |
| 206 | Guild 5 | 59×67 | 1 | — |
| 207 | Tomb Event Map | 100×50 | 1 | — |
| 208 | Poisoned Well Map | 149×149 | 3 | t18 |
| 209 | Dark Temple Map | 174×174 | 3 | t29 |
| 210 | Descent Map | 149×149 | 4 | t2a |

## DS1怪物标记与编译身份

D2MOO `DRLGPRESET_LoadDrlgFile`的MonPreset翻译有范围条件：版本大于4且编号在文件内Act对应的MonPreset段内才翻译；段外编号保留原值，再执行各幕特殊单位转换。当前KurastOpenWide3.ds1含怪物标记168，而Act III的MonPreset段仅60项。这不是“缺少第168条MonPreset”。`src/world/retail/preset_scan.cpp::resolveUnit`现保留这个直接编号，不再拒绝整图；负编号不分配单位。

MonStats类编号使用编译记录顺序，跳过Expansion分隔行，同名首次链接。`src/content/monsters/monster_identity.cpp`提供内容行／编译编号双向转换，目录、模式／生命／类型、人口、掉落、声音、召唤和出生参数保持相同身份，避免1198之后TXT hcIdx空号造成错怪。SuperUniques／MonPlace拼接仍用各自真实数量，见[怪物身份](MONSTERS.md#monstats与预设身份)。

## 无目标出口标记

当前157／161号预设保留房间出口标志，但对应Levels.Vis为0。配套D2Common的RVA `0x453D0`沿Vis查目标，0对应空Levels记录；`0x2E360`对DrlgType=0不生成房间，不产生回链。`src/world/retail/tile_materialization.cpp::establishNear`现跳过这个空目标，不拒绝来源地形，也不制造可用出口；非零目标缺资源仍报错。

## 地图物品、词缀与状态

当前ItemStatCost包含`map_glob_*`、`map_mon_*`、`map_play_*`及`map_force_event`等字段；CubeMain也有地图升级、重掷、腐化等配方。字段存在、物品能解码或配方能匹配，分别只证明数据／格式路径，不能证明区域、怪物、玩家或掉落系统已消费该字段。

| 领域 | 已知依据与当前边界 |
| --- | --- |
| 地图属性广播 | 自有实例已有55权威事实和Pd2Local专属接收；原属性／转换后运行属性分开，见[词缀基线](MAP_PROPERTIES.md)。CN字段仍归[协议取证](../resources/PD2_CN_DLL_ANALYSIS.md#状态扩展接收链) |
| 生成与合成 | 普通物品与CubeMain基础算法见[方块链与重掷](ITEMS.md#方块链与重掷)；匹配地图配方不等于可以开图，未支持后继程序不得先消耗材料 |
| 区域／人口 | 密度、等级、稀有度、新增类、Skirmish及额外预设首领已有实例消费者；普通每局一张图、结束游戏释放；特殊控制器仍未齐 |
| 战斗／掉落 | 真实198状态、物理附加元素、额外TC及自动腐化／孔数／无形已接入，死亡先冻结掉落输入再撤销来源；特殊首领奖励及全随机交错未齐，详见[词缀基线](MAP_PROPERTIES.md) |

## 开图程序与属性分派

当前核心RVA `0x36A240`通过配套D2Game的pSpell表（RVA `0x107238`）将偏移0x64，即pSpell12的Do槽，写为`0x3810A0`。按Level反查物品的`0x37AA90`不是入口映射：它有过时／异常值，例如190／191与当前Misc.len相反，174返回含零的DWORD。

`0x3810A0`读取Misc.len（编译偏移0xB0），经原公式求值取低WORD为目的区域；Misc.missiletype（0xFA）非零时作为门户物件类，否则用60，当前普通地图为584。非PvP普通路径检查地狱、等级80（另有资格例外）、Harrogath及原0x27任务位；PvP有独立来源条件。`0x388340`创建双端门户并连接真实目标区域；首领／地下城另写游戏控制程序，不能统一套普通开门。

属性输入来自地图物品原StatList，**不是把地图当装备穿上**。`0x3810A0`按当前ItemStatCost.Divide分派，并分别保留层／参数和值：

| Divide | 原分派 |
| --- | --- |
| 0 | 通常跳过；原属性500单独标记强制事件 |
| 1024 | 保留原stat身份进入怪物列表，例如map_defense；不是玩家stat24 |
| 其他小于1000 | 怪物列表，目标ID为Divide低段；包含专属掉落资格等特殊属性 |
| 1000–1999 | 玩家列表，目标ID为Divide%1000 |
| 2000–2999 | 全局程序列表，程序号为Divide%1000 |
| 3000–3999 | 经`0x3875C0`及后继准备额外物理转元素数据，不能当普通属性直接相加 |

55广播的两个头分别取开门返回物件结构的+0x0C和+0x00，随后有最多24项原物品属性、区域、品质和物品记录索引；广播清单与转换后的三类运行属性不同。

`0x388A40`把怪物地图效果放入状态198的StatList。432–436的物理附加元素按施加时物理最小／最大值计算；原76最大生命百分比对怪物另走基础maxhp增量分支及标志写入。不能因此把相同清单附给所有伙伴。

`0x389510`消费全局程序：0人口密度、1区域等级、2稀有／精英数量、3扩充MonReg类来源；后续有额外首领、掉落、堡垒／巅峰及元素破免等独立分支。存在float计算和窄字段写回。普通地图还会按品质、随机流和强制标记选择事件；地下城／首领有额外控制实体、阶段和子区域。

普通pSpell12、三类属性分派及部分人口／掉落程序已接入自有宿主，具体消费者、拒绝范围及未验证状态见[地图物品与词缀](MAP_PROPERTIES.md)。普通同局一张图符合当前DLL；特殊控制器、事件及完整首领奖励尚未齐，不能声称完整地图玩法已经完成。

## 特殊地图控制器

当前`0x3810A0`在成功开门路径选择Game+0x1DF4的程序号；这些程序拥有游戏内状态，不能仅以目标Levels或者首领是否死亡代替。已核实的入口如下，D2X仍拒绝需要这些未闭合控制器的地图物品。

| 入口Level | 控制程序 | 入口状态及关联区域 |
| --- | --- | --- |
| 176 Zhar Library | 30 | Game+0x262A字节清零，地图属性另准备177／178／179 |
| 181 Hellcaves | 31 | Game+0x262A字节清零，另准备182 |
| 183 Fallen Gardens | 32 | Game+0x262A字节清零，另准备184 |
| 180 Outer Void | 33 | Game+0x2628 DWORD清零；阶段属于同一地图 |
| 192 Imperial Palace | 34 | Game+0x2628 DWORD清零，另准备194 |
| 195 Ureh City | 35 | Game+0x2628 DWORD清零，另准备196 |
| 197 Djinns Domain | 36 | Game+0x2628 DWORD清零，另准备198／199 |
| 185／137／161／168／188 | 41／42／43／44／45 | 各有专用入口与后继，不从30–36的字段含义推导其阶段 |

死亡回调的当前静态锚点包括`0x3751D0／0x3752B0`（30）、`0x3753C0`（31）、`0x3758A0／0x375900`（32）、`0x377580`（33）、`0x3759C0／0x375A80`（34）、`0x375680`（35）、`0x374FA0`（44）。例如32的两个分支分别创建通往184的584门户、通往109的586门户；34另有`lpp`与`lbox`物品创建。33使用上下文多个首领身份槽驱动0→1→2→3阶段、开场景物件及最终回城。`0x377660`位于`0x377580`内部，不能记成另一个控制器入口。完整死亡分派、奖励资格、物件操作和随机池尚未闭合，以上不作为直接发奖规则或可玩场景声明。

44／Uber Ancients的`0x374FA0`只在`(flags & 0x110)==0x10`执行；类989／990／991分别置0x20／0x40／0x80，与选择位0x2／0x4／0x8的计数比较，再置完成位0x100。后继还有物件592、帧事件、重复奖励、门户和玩家计数写回，不能仅在第三个死亡时固定掉一件物品。选择／出生阶段、完整AI、事件及奖励仍未接入，因此168入口继续拒绝。

## 特殊PvP区域

当前核心`0x37AA30→0x37A9C0`判断Level157／159／166，`0x37AA60→0x37A9F0`仅判断166；这两项不是城镇判断。不同接收程序各自使用这些资格，不能用一个“PvP区域”布尔值统一替换全部规则。

`0x37B650`使用Level157的全局坐标矩形x=0x50B0..0x50DA、y=0x281C..0x2852，以及Level159的x=0x4FF9..0x5025、y=0x2D79..0x2D98判断施法安全区，端点包含。这是核心固定矩形，不是DT1瓦片属性。普通施法触发的准备已接此门槛，见`src/server/systems/skills/pd2/item_events.cpp`；布局现可由管理旅行检查，正常PvP／BR入口和胜负程序仍未实现。

## 待核实与未完成

扩展布局、表驱动人口和原生出口复用已有消费者；开图／入口、实例寿命与恢复、地图词缀全链、特殊事件、首领和掉落仍未形成完整实现。已有布局或管理旅行不等于地图物品可用。后续逐项记录物品→区域→人口→战斗／掉落→离开／保存的真实调用链、参数来源和拒绝条件；不自造地图、词缀数值或概率。
