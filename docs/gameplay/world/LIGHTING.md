# 场景照明

世界光照由公共SceneView::drawWorld／LightingView／PL2消费，当前MPQ灯光定义由SceneAssets加载，原服模式与独立连续显示位置提供来源。已接原1.13c整数距离／定点衰减、独立强度与RGB的饱和合成、玩家和物件的传播遮挡。原0x53的tainted已接日蚀开关，完整昼夜时钟未同步；客户端环境推进不等价于原服时序。灯光网格在世界绘制前准备；地砖／墙体／单位分别取光后绘制和混色，文字、小地图和HUD不受世界光照影响。完整原版绘图顺序仍未移植。

Video Options的Lighting Quality按用户要求改为On／Off，默认On；这属于d2x的开关语义，不对应原版High／Medium／Low三档质量。On沿用当前光照路径，Off在`PaletteBlendView::drawLit/drawWall`统一使用强度255（当前MPQ Shadows第31行恒等映射），覆盖地面、墙体、屋顶、单位、地面物品及特效，保留原索引、透明／Screen混色、物件阴影和墙体显隐。环境时钟、光源状态和光照网格继续更新，切回On直接使用当前状态。客户端偏好`lighting`为布尔值并默认true，不进入角色D2S；菜单仍用原英文DC6、字号及坐标。实际切换画面尚未运行确认。

## 数据入口

- `WorldCatalog` 从当前MPQ的 `Levels.txt` 读取 `IsInside`、`LOSDraw`、`Intensity` 和RGB。普通房间RGB非零时直接使用表强度（包括0），全零才回退幕环境；不再用IsInside二选一。`SceneLightRegion`借用当前活动房间near列表的原边界和LevelRecord，在观察者底光上覆盖邻房间各自范围；原DS1可绘制重叠边不扩大碰撞房间。Cloak保留当前区域缓存底光对邻房间的作用。邪恶洞窟覆盖已新增：仅原版5E初始化表第1项非零且本局Den尚未完成时，8区底光为255／64／48、强度80；FX0的30帧时钟按floor(128×帧／30)查余弦衰减至0，完成后恢复Levels／幕环境。该覆盖按房间分别消费，不修改单位局部灯光，Cloak继续作用于其强度；PD2不套用未核实的零售覆盖。第四幕特殊事件等环境覆盖仍未接齐，新增Den路径已构建，行为仍待实测。`NoPer`是透视控制。
- 玩家基础半径13加 `item_lightradius`；原1D–1F／20提供本人／其他玩家的基础属性，完整已观察9D装备、镶孔、护符、激活套装及状态列表沿同一客户端投影相加。NOEQUIP／BROKEN、当前装备位置和原表决定资格，资料未齐保留旧光源，不从外观猜数值。正半径立即设定并封顶18，非正请求保留原半径；新光源初值13。`item_lightcolor`同源读取，0恢复白色、非零按RRGGBB解码；其他玩家使用原type1不遮挡来源。物品基础表 `lightradius` 不作为玩家属性加值。怪物装备基础光及完整颜色回调仍未接齐。
- `SceneAssets.objectLights` 读取 `Objects.txt.Lit0–7`、RGB和Flicker；半径按原DLL用Lit右移1位取整，全部来源在绘制入口封顶18。光源按对象原身份、NU／OP／ON／S1–5模式选择；传送点、回城门和凯恩红门使用与原图相同的显示阶段。物件光使用原type2传播遮挡。Flicker元数据保留，未核实的闪烁程序暂缓，当前仅使用表中不加调制的基值。
- 全部已接入弹体、命中特效与客户端碎片共用 `Missiles.Light`、RGB、InitSteps 和实际生命周期；不再限定冰封球。球体为 6、冰弹为 4、RGB 81/81/255。区域坐标使用共享偏移，只收集已激活房间中的来源；非零 Flicker 同样暂用基值，不自造随机闪动。
- `MonsterCatalog` 按真实 MonStats 身份关联 `MonStatsEx → MonStats2.Light/light-r/light-g/light-b`；存活怪物／召唤物和 NPC 使用原值，敌对替身的原身份仍决定灯光。沉沦魔巫师为 Light=5、RGB 230/168/255，安达利尔为 Light=8、白光；没有按外观猜色。
- Overlay读取 `InitRadius/Radius` 和RGB；非正目标／初始半径不分配光。`LightingView::Sources`按所属单位、Overlay及创建时刻区分实例，在已有客户端25Hz时钟发生变化的显示帧向目标推进一个子格，同一显示帧不会重复推进，也不按跳过的逻辑帧数补步。常量与动态叠层共用路径，列表移除时释放，游戏／区域更换时清空。不是按GPU帧数或连续age插值；暂停保持时钟，房间重新显示按新叠层实例创建。飞弹的完整创建首帧／生命周期仍未移植，继续保留目标半径适配。

## 装备与护盾类状态

装备属性沿 `item_lightradius` 影响角色光源；状态Overlay是额外光源，不能把Overlay.Radius加到角色属性或用它替换角色半径。原Client 18BC0对玩家取基础13加属性回调值，经A8B60把当前／目标半径同时设为同一值并封顶18，因此普通装备切换不是Overlay的逐步扩张。当前MPQ的“of the Sun”戒指为+5，Duskdeep为−2；局部实机对照确认+5的扩大效果接近原版。D2X双+5与单+5的固定地面选区相同，−2也会缩小范围；这两项D2X内部观察不等于已补齐原版全装备组合对照。

`native_character_wire.cpp`按Common #10513／PlrMsg 822D0发送原Stats基础属性，而不是限幅半径减13，也不是FullStats装备合计。装备／状态通过各自原包提供，避免原版客户端再次叠加装备。双+5时，普通角色的基础光照属性仍为0、两枚物品各携带+5，客户端得到原始合计10后把有效半径设为18。其他玩家的基础属性遵守PlrMsg 82830原范围；其已识别装备仍由原9D同步，客户端共用装备投影，不发送臆造的公开总值。

原18BC0把非正半径请求改成0，A8B60对非正输入直接返回；客户端因此保存上一有效半径，而非强制变1。本人和其他玩家均按此保留展示状态，重新创建单位时恢复13。内核的无历史派生属性不作为这一客户端回调的网络值或光源值；所有极端装备切换与同帧回调顺序尚未逐项原版认证。

当前MPQ的Frozen Armor／Shiver Armor／Chilling Armor常驻Overlay均为 `InitRadius=1, Radius=13, RGB=200/200/255`，受击Overlay为1→6、81/81/255。Client 6B49F仅在目标Radius非零时创建Overlay光源，6B4A6以InitRadius分配type1、强度255的光，6B4D8设置目标Radius；A9BB0按每次获准的客户端光照更新推进一个子格。44B2F递增客户端逻辑帧，8B1AC仅在该计数前进时调用A9E30；动态叠层已按此门控接入。它与玩家遮挡光并存，因此即使基础半径同为13，仍会改变同一区域的强度和遮挡结果。没有按技能或地图固定补亮。

Energy Shield常驻Overlay为0／0，原6B49F不为它分配光源；其八种受击Overlay为3／3，应区分常驻状态和受击事件。Bone Armor、Cyclone Armor和Blade Shield的相应常驻Overlay同为0／0。冰封装甲与能量护盾并存的虫洞同位置对照中，两块地面选区与原版逐像素相同；石柱均值仍差−0.59、前景岩壁偏亮5.25，不能据地面一致认证全帧。这里不认证所有客户端回调、飞弹、受击时序或跨职业实机表现。原图、采样、状态快照与静态读数保存在忽略目录的光照对照报告中。

## 绘制

连续地形条目在不改变排序的作用域内复用照明着色器和固定参数；只有光照样本、索引模式和高亮参数完全相同时才共用待提交批次，参数变化前先刷新。进入单位／阴影绘制、透明墙或Screen混色的背景复制前结束当前批次；作用域退出恢复默认着色器。纹理／顶点容量切换先于额外采样器登记，保留raylib自动刷新边界。原PL2查表、六列墙体插值、透明混色和逐次背景读取算法不变。

世界绘制的`PaletteBlendView::TargetScope`只查询一次当前FBO；作用域内的每次背景复制仍按原顺序刷新、复制及恢复同一目标，不改变PL2或重叠图元的依赖。作用域外保持即时查询，退出时清除缓存；不得跨`BeginTextureMode`／目标切换保留该作用域。它避免逐阴影／透明墙／Screen特效重复调用`glGetIntegerv`，不合并背景读取或改变原版混色阈值。

`LightingView`是纯展示层。光源坐标按原DLL量化为1/8子格，再加4个内部单位的半格偏移；网格样本位于整数子格坐标。距离为`(983×max(abs(dx),abs(dy))+407×min(abs(dx),abs(dy)))>>10`，以`R=8×radius`、`k=(255<<16)/R`求`((R-distance)×k)>>16`，非正值跳过；已删除22%恒亮区与smoothstep。

`LightOcclusion`读取原地形碰撞及物件实际碰撞掩码中的0x22（VISIBLE／BLANK），不把 `Objects.BlocksLight` 另铺为矩形挡光区域。`UNITS_GetCollisionMask`和HasCollision决定物件登记；普通物件0x400不挡这条光照路径，挡视线的门0x0806含0x02。传送点HasCollision全0，即使BlocksLight全1也不产生挡光足迹。客户端、Region和宿主共用这一掩码边界。阻挡前驱值为16，否则使用其传播值；以中心3×3种子，从第2圈起按原八个方向顺序向外，按主／次轴距离的8位权重混合前驱。目标墙格本身不被直接黑掉，阻挡传播到其后。贡献按`(contribution×(8-(obstruction>>1)))>>3`衰减，值≥16跳过。本人使用原高质量type0路径，室内／户外均可遮挡；物件使用type2路径，其他玩家／怪物／飞弹／Overlay保持所属非遮挡路径。当前每次绘制按连续光源位置和当前碰撞重建所需范围，不等同原物件缓存生命周期。

Client 251AE／25572／25B0D依据 `BlocksLight[mode]` 清除或设置Unit+0xC4的DRAWSHADOW位0x20，不能把同名字段解释成碰撞位0x20。物件阴影按此模式标志及原COF组件掩码决定。地形阴影另走GDI 60A0的原图索引混色：开启混合阴影时调用D2gfx helper+0x14、alpha192，即PL2 trans[0]，删除自造RGB染色／alpha100。

每个样本保存独立的0–255强度和RGB；先填环境，再逐来源把贡献加到强度并封顶255。颜色按`min(255,((oldColor×oldIntensity+sourceColor×contribution)×floor(65536/totalIntensity))>>16)`逐通道处理，保留原除法、截断及封顶顺序。当前来源遍历仍沿场景列表，未恢复原创建链的完整交错；彩光逐次舍入不能据此认证所有来源顺序。

显示网格按`SceneView::worldViewport`四角反投影的子格范围动态准备，随分辨率、缩放、面板和镜头偏移变化；同时覆盖本帧可见精灵的地面锚点及墙体六点表的±5子格取光范围。整数边界向外取整并保留两格供四邻点平均和双线性插值，纹理容量只在不足时增长。原固定61×61网格不能覆盖扩展视口，越界后直接回退观察者底光会在屏幕角落形成明暗接缝，已改为上述范围；该修正尚未构建／运行。地面强度取四邻点整数平均后插值角点，RGB仍沿显示适配插值；这不认证原3D后端的颜色插值。纹理RGB保存颜色、A保存强度，整数角点对齐纹理像素中心并使用边缘钳制。`LightingView::prepare`先生成原始样本和地面角点，单位以自身坐标读取原始样本，屏幕像素仅用于地面插值，不再用高墙顶部／角色头顶的屏幕位置反推另一处地面光。网格、GPU资源、环境状态及来源不进入D2S。

DT1头的`LightDirection`由`decodeDt1`读取并保留，与`orientation`分开。墙体按Client DED18／DEEF8六点表取原始网格样本，A8230使用整数格及边缘钳制；不能从墙顶的屏幕坐标反推地面。GDI／DirectDraw以DT1子块的水平位置右移5位选相邻两个样本，纵向共用，只消费独立强度字节，墙面使用PL2而不乘样本RGB。普通墙仅在两端原强度差≥10时进入高质量Gouraud；透明墙始终进入该路径。Gouraud先把两端强度右移3位，再以32像素定点表插值，不能先插值8位强度再量化。方向0–9的普通／备用表已接；更高数值会越过已核实表，明确拒绝，不把相邻内存数据当作额外方向规则。样本方向和原图均来自当前MPQ，不按区域、墙高或截图补亮，未知方向明确拒绝。

`TileVisibility`统一持有预设Pops和前墙渐隐的客户端状态，替代仅有屋顶alpha的路径。`NativeMapGenerator::snapshot`投影已有逻辑区域索引、格到区域的映射与原box[0]左上角；与人口用的裁剪box[1]分开，边界专用列表不能误用归零后的人口矩形。默认Client 79F20路径（E24F4初值1）对没有单列表替代的墙读取所属区域：索引与本人不同，且本人整数tileX小于该区域左边或tileY小于上边，则是前墙。无区域指针返回否，主flags的4绕过前墙切换；不按画面下半部、墙高度或特定区域判定。原备用E24F4=0按墙向和20子格距离判断的分支未作为当前默认策略，也不冒充已经支持的质量选项。

可见性状态按原实例顺序推进，通过更新访问标记回收本轮未出现的键，避免逐帧另建一套存活键集合；相同键仍共用原状态并按每次出现更新。近房间资格只读取现有列表，不重建有序集合。Pops、前墙判定、500ms期限和整数舍入保持原次序，未跳过屏外状态或降低更新频率。

原TileData+0x24的bit0表示前墙／备用取光面，bit1表示red→目标的过渡，bit2表示Pops延时隐藏；与DRLG主flags各位分开。Common的Pops先写同一组red／from／to／deadline，Client 7A340再依据前墙状态切换255→128或128→255，9E970推进过渡后绘制。500毫秒期限重定位保留原以255为除数的整数舍入，不能按当前alpha重新匀速插值。静态普通面取DED18，前墙取DEEF8；恢复可见的第一帧即切回普通面，不等待透明度完全恢复。0x200的Pops立即揭示、离开后延时隐藏仍独立保留。状态只在展示层存活，房间释放后丢弃，不修改碰撞或存档。

不透明墙直接写入取光后的来源色；透明墙先以原来源索引查Shadows，再以该结果索引查`trans[litSource][destination]`。D2gfx 9660的alpha阈值为192／128／64，分别选择PL2 trans[0]／[1]／[2]，低于64不画；不是连续RGBA混合。光照表纹理附带原结果索引，避免重复RGB反查改变来源索引；背景仍受现有RGBA世界缓冲的反查限制。目的区域仅在GPU复制，来源、PL2与目标混色共用四个额外采样位，不回读CPU。屋顶／地面另有原绘制入口，不能以普通墙的六点表替代它们。

户外环境时钟以 D2MOO `ENVIRONMENT_AllocDrlgEnvironment` 的 cycle=2、ticks=0、timeRate=128、初始强度 128／白色开始。`GAME_UpdateProgress → GAME_UpdateEnvironment` 证实每个 25 Hz 玩法帧推进；正常六段色表、夜间加速、整数切段、RGB 舍入与 `float(sin(angle))` 强度计算移入展示层，表现暂停时不推进，普通菜单继续；使用表值的房间不应用昼夜结果，幕时钟仍继续走。正常强度公式为 `int(float(sin(angle)) × 128 + 128 + 0.5)`，夜半区间 sine 减半；第五幕 170 上限、第四幕逐级趋近与巅峰 200／特殊色的原引擎分支保留。当前五幕地形可用，但完整原服环境时钟及天气／闪电影响未接入，日蚀开关的接入不认证整个任务／环境时序。

`PaletteBlendView`运行时读取当前幕`pal.pl2`的`Shadows[32][256]`，偏移0x400。标量／等强度灰光以独立强度右移3位选行，再查来源像素索引。地形直接使用原DT1索引；其他已提供索引的精灵同样直查，组合角色等RGBA来源在本图元内反查调色板。光照变换发生在来源透明／加色混合之前，删除了复制已混合世界帧再统一覆盖的后处理。普通绘制以texture0作来源，地面光图、调色板、索引反查和PL2共用四个额外采样位；PL2 Screen与透明墙分别使用四采样位程序。当前世界图元统一沿已核实的GDI／DirectDraw软件标量路径：地面、单位、墙和Screen叠层均按独立强度查PL2，彩色光也不改走RGB乘法。RGB仍在灯光网格中保留，不能以其存在冒充已经支持3D硬件彩光。当前MPQ第0行为暗部、第31行为256索引恒等映射，PL2页首RGB与Act1 pal.dat相同，没有倒序或错配证据。文字、小地图及HUD不受世界光照影响。

**当前仍不能宣称完整光照与原版一致。** 已移植上述网格规则，剩余边界如下：

- 原创建链交错、物件缓存的原生命期、完整怪物装备／状态光源与光源生灭尚未接齐；Overlay已接上述实例及客户端门控，飞弹完整创建首帧仍暂缓。完整Flicker不自造调制。
- 网格强度／颜色合成已按原DLL，世界图元选择软件PL2强度路径；3D后端最终彩光、Gamma及逐图元索引取色／混色仍未完整核实。
- 当前采用原高质量遮挡路径；原1.13c A8230将取光索引钳制到0–47，固定48×48网格属于其视口约束，D2X按扩展视口动态适配并保留取光／插值规则。质量选项切换及全场景帧间对照尚未接齐。邪恶洞窟任务完成光照、第四幕特殊事件、完整原服环境同步与天气仍有缺口。
- 世界目标仍为RGBA，地形已直接按原索引取光；组合单位的RGBA来源和透明墙背景仍需在图元内反查，重复RGB使用首项，染色值取最近原色。完整索引合成、全部COF效果／自发光分支、原绘制桶的全部交错、屋顶专用逐子块光栅化、用户Gamma及3D硬件路径仍暂缓。已核实LightDirection表之外的方向拒绝加载，不能宣称所有修改版资源／遮挡状态都已覆盖。

原版依据：基础13另由下方原DLL确认，属性映射来自当前Properties／ItemStatCost；最大18的客户端限幅见RVA A8C20／A8B60。环境计算见 [D2MOO D2Environment.cpp](https://github.com/ThePhrozenKeep/D2MOO/blob/5596f5cb6c5251a0a07c6637d26458b06099d516/source/D2Common/src/D2Environment.cpp)，推进入口见同快照 `D2Game/src/GAME/Game.cpp::GAME_UpdateEnvironment`。原 PL2 排列见 OpenD2 `Engine/Palette.hpp` 与 OpenDiablo2 `d2pl2`；行选择及 Gouraud 缓存见 [D2Gfx CmnSubtile.cpp](https://github.com/ThePhrozenKeep/D2MOO/blob/5596f5cb6c5251a0a07c6637d26458b06099d516/source/D2Gfx/src/CmnSubtile.cpp) 的 `sub_6FA71070`、`sub_6FA73130`、`DGFX_InitGouraudCache_6FA72570`；高质量四邻点平均见 `D2GFX_FloorTileDraw_6FA73410` 内 v44/v45/v46/v47，子块 Gouraud 见 `TILE_TileDrawLit_6FA729D0`。物件模式及怪物 Light 字段见 D2MOO ObjectsTbls／MonsterTbls 和 OpenDiablo2 对应记录。DT1 的位 2／32 分别为 LOS／BlockLight，仍与单位半高斜向阴影独立。

## 原1.13c静态定位与当前差异

只读核对本机1.13c `D2Client.dll`（MD5 `f5860c629d309b8fc1f96174babcf633`，ImageBase `0x6fab0000`）与 `D2Common.dll`（MD5 `ee1238806ef6d6d9801d12a09d128fe1`，ImageBase `0x6fd50000`）。墙体后端另核对`D2Gdi.dll`（MD5 `dc117a4fa4379c157248bb3dcca022df`）、`D2DDraw.dll`（MD5 `9fffd50394795c10dd12aa8629e75aed`）及`D2gfx.dll`（MD5 `ff48757b0164d03d7bde143a86e75864`）。下表地址均为各自模块RVA；D2MOO固定快照缺少D2Client光源生成实现，不能只从D2Environment推断整条客户端绘图链。表中规则依据为静态代码及当前MPQ读数，局部实机对照不代替完整绘图链核实；原DLL、反汇编及提取资源留忽略目录，不提交／分发。

| 环节 | 原版证据与行为 | 当前实现与边界 |
| --- | --- | --- |
| 距离与衰减 | Client A8760／A87B0计算`d=(983×max(abs(dx),abs(dy))+407×min(abs(dx),abs(dy)))>>10`；内部坐标／半径以1/8子格计。A9020／A9079、A9728／A97DE、A992A／A997A以`k=(I<<16)/R`、`((R-d)×k)>>16`求贡献，再应用所属遮挡分支，非正值跳过 | `lighting_view.cpp::lightCoordinate/lightDistance`及prepare已使用该定点链；Common ordinal11080／11168的30340／302E0返回动态精确坐标或静态坐标左移16，Client A8C99–A8CBC／A9B8F–A9BAC右移13并加4的中心偏移已接 |
| 多光源与颜色 | Client A8A40–A8B58读取旧强度，先与新贡献相加并封顶255；A99E0初始化倒数表`floor(65536/i)`，彩光开启时对旧颜色×旧强度＋新颜色×新贡献乘倒数、右移16、逐通道封顶。网格记录独立的强度和RGB，关闭彩光仍保留标量强度 | `accumulateLight`按此合成，纹理A和Shader独立传递强度；原创建链顺序与3D最终彩光仍待接齐 |
| 区域底光 | Common ordinal10636／3C6A0→44D90从LevelDef的88–8B读取Intensity／RGB，与D2MOO DRLGROOM_GetRGB_IntensityFromRoomEx一致。Client A9A20–A9B46先处理特殊区域／环境效果，普通房间RGB非零时使用表值；RGB全零才回退幕环境。A9C90按当前房间和邻房间范围分别填网格 | `roomAmbient`与SceneLightRegion已接普通表值／回退及near范围；Cloak作用强度，保留当前区域缓存。当前MPQ区域125–127的IsInside=0、Intensity=120、白色已不再误选昼夜；邪恶洞窟／第四幕特殊覆盖仍暂缓 |
| 遮挡 | Client A8880逐格读取碰撞掩码0x22；物件创建入口24920以type2分配光源，A9B50→A9100建立其遮挡缓存，A8F60用缓存值`o`施加`(contribution×(8-(o>>1)))>>3`；A8310／A8470含相邻方向插值传播；A9C1F→A9480为高质量本人路径 | `LightOcclusion`已接3×3种子、第2圈起八方向、8位权重前驱传播及贡献衰减；本人和物件均按当前碰撞重建。原缓存的房间失效时序与质量选项仍待接齐 |
| 半径与生命周期 | Client 192DF–192F5创建白色、强度255、半径13的玩家光；24962／2497C用Lit右移1位，A8C20限幅到18。A0F60–A1001创建飞弹光时半径为1、目标为Missiles.Light；6B49F–6B4E5读取Overlay.InitRadius／Radius；A9BB0–A9BF4把当前内部半径按±8趋近目标；8B1AC–8B1BE按客户端帧变化门控光照更新 | 物件整数半径及全来源18上限已接；当前MPQ物件39的Lit1=19、358的Lit0=19均取9。其他玩家type1来源及189A0→A86C0的原颜色解码已接。Overlay已按所属单位／实例保存初始半径，并按25Hz客户端计数变化门控单步推进；飞弹目标半径适配、完整源创建顺序及Flicker仍保留缺口 |
| 墙可见性／过渡 | Common的DRLGLOGIC_SetCoordListForTiles给墙保存逻辑区域；Client 7AC51记录本人索引，79F20默认分支比较索引及box[0]前后位置，7A340写TileData+24的bit0／bit1及500ms期限。9EA01先推进red，再用bit0选DED18／DEEF8 | `MapTerrain::PreparedRoom`保存只读逻辑数据，`TileVisibility`共用Pops和墙的过渡状态；不采用屏幕区域或地图ID补丁，备用距离／墙向判定未作为默认路径 |
| 取光与绘图顺序 | Client 13715–1375E写地面各点强度／RGB；9E940／9EAAB从DT1 LightDirection选择DED18／DEEF8，9EB1A取光后经9EBA8／9EBBF传入Tile绘制。D2gfx ordinal10001／10058的BB50／BB20分派；GDI 5E50／5F70、DDraw 7320／7440以block.x>>5取六点中的相邻两项，只读强度、纵向复制；CmnSubtile 729D0及Gouraud缓存先量化再插值 | `decodeDt1`保留LightDirection；`prepareWorldLighting`先准备灯光，`tileLights`按显示状态选普通／备用面，`PaletteBlendView`在来源上应用PL2后再混色；地面／屋顶／单位另走各自入口，未完成的索引、特殊图元及彩光边界见上文 |
| 透明墙混色 | GDI 5F70／DDraw 7440走D2gfx helper+18，当前DLL RVA9660以192／128／64选三表；先Shadows[source]再trans[litSource][destination]。D2WinPalette直接复制当前幕PL2，未转置原表 | `drawWall`保留精确来源和PL2结果索引，以原三表混色；已有UI混色纹理是转置布局，墙路径交换坐标访问。背景反查及完整索引目标仍有限制 |

当前MPQ第一幕洞穴8–16的Intensity=0，第二幕下水道47–49为8，第五幕冰洞113–116／118–119为30，均为白色；当前先填原底光，再累加局部贡献，不通过提高固定底值掩盖差异。移除传送点的错误挡光足迹后，区域115同位置两块冰面选区RGB均值为原版37.18／46.09、D2X39.17／44.42；冰壁选区逐像素相同，冰柱相同率99.84%。大面积偏暗已消除，局部光栅化差异仍保留。当前Act1 PL2的Shadows第31行仍是256索引恒等映射，没有发现倒序证据。

局部原版画面参考使用1.13c窗口模式、普通难度及当前MPQ，D2X Host／原版Join共用种子248942108。洞窟9在协议坐标7525／6525分别观察单玩家及另一同职业玩家位于7529／6525的场景；单玩家采样时另一角色移至城镇，房间物件完成恢复后再截图。两端均有原物件37的火把，不能将暂停旅行后尚未发送物件的画面解释成资源／灯光规则差异。对应地面部分采样接近，墙体上部和火焰仍有可见差异，下水道47亦保留这些显示边界；动画帧和完整后端状态未同步，不据单张火焰截图推定衰减或闪烁根因。上述局部画面不能单独认证新的逐图元取光路径，未认证逐像素一致。

Den依据本机D2Client524D0／A9A20、62E30／AC090的37字节5E投影，以及Fog #10083_Cos_LUT（本机16730）。30个整数帧的最终截断强度已从原LUT静态核对为80、79、79、79、78、77、76、74、73、71、69、67、64、62、59、56、53、50、47、43、40、36、33、28、25、21、17、12、8、4；代码用相同量化角度和单精度余弦值后截断，不将静态核对当作运行画面验收。

后续需核实并接齐源创建／更新时序、其余特殊环境覆盖及创建链顺序，继续核对全部DT1方向／备用面、地面子块光栅化和完整索引混色。完整Flicker、3D后端彩光、Gamma、天气和全场景原版画面对照仍待核实；本节不把网格规则移植等同于完整客户端光照复刻。

原版参考截图中的雨线尚未实现。OpenDiablo2 的 `LevelDetailsRecord.EnableRain` 和 Diablerie 的 `LevelInfo.rain` 证实 `Levels.Rain` 是区域天气许可；两个参考客户端未找到可直接核对的完整降雨实现。降雨开始／停止、密度、风向和天气光照时序仍未核实，不能仅因区域允许下雨就永久绘制雨线。正常世界绘制已移除原先供顶部开发文字使用的黑色渐变，不以固定遮罩冒充天气。

参考代码：`reference/d2moo/source/D2Common/src/DataTbls/LevelsTbls.cpp`、`ItemsTbls.cpp`、`Items/ItemMods.cpp`、`Monsters/Monsters.cpp` 和 `D2Environment.cpp`；DT1 标志交叉核对 `reference/diablerie/Assets/Scripts/Diablerie/Engine/IO/D2Formats/DT1.cs`。图形仍从用户挂载的原 MPQ 读取，不导出成独立资源文件。

完整光照一致性仍受上述来源时序、缓存生命期、彩光后端、索引绘制及环境同步缺口限制；原版参考和当前实现分别核对，不以窗口启动或局部显示代替原版对照。
