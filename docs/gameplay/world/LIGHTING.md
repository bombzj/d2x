# 场景照明

世界光照由公共SceneView::drawWorld／LightingView／PL2消费，当前MPQ灯光定义由SceneAssets加载，原服模式与独立连续显示位置提供来源。玩家基础半径13加已收到item_lightradius，缺少装备增量不重算。原0x53的tainted已接日蚀开关，完整昼夜时钟未同步；客户端环境推进不等价于原服时序。NPC提示、掉落、弹体和屋顶位于世界光照前，文字、小地图和HUD位于其后。

## 数据入口

- `WorldCatalog` 在运行时从当前 MPQ 的 `Levels.txt` 读取 `IsInside`、`LOSDraw`、`Intensity` 和 RGB。当前按IsInside选择室内表值或户外昼夜值；这与原1.13c按房间RGB是否全零回退昼夜值的路径不同，见下方静态定位。`NoPer` 是透视控制，与环境光无关。
- 玩家基础光照半径为 13；原服已收到的 `item_lightradius` 作为增量输入，缺少装备加值不在客户端重算，最终限制为 1–18。物品基础表的 `lightradius` 仍保留原字段，但不作为玩家属性加值；先前把其最大值叠到玩家半径属于错误。最终装备／需求／耐久由原服决定。
- `SceneAssets.objectLights` 读取 `Objects.txt.Lit0–7`、RGB 和 Flicker；当前用Lit×0.5换算浮点半径，原1.13c实际用Lit右移1位，奇数值存在半格差异。光源按对象原身份、NU／OP／ON／S1–5 模式选择；传送点使用真实激活阶段，回城门和凯恩红门使用与原图相同的开门／常开阶段。已移除按 rb/to Token 分类的统一火焰灯、16 盏限制及自造橙色渐变光圈。Flicker 元数据保留，未核实的闪烁程序暂缓，当前仅使用表中不加调制的基值。
- 全部已接入弹体、命中特效与客户端碎片共用 `Missiles.Light`、RGB、InitSteps 和实际生命周期；不再限定冰封球。球体为 6、冰弹为 4、RGB 81/81/255。区域坐标使用共享偏移，只收集已激活房间中的来源；非零 Flicker 同样暂用基值，不自造随机闪动。
- `MonsterCatalog` 按真实 MonStats 身份关联 `MonStatsEx → MonStats2.Light/light-r/light-g/light-b`；存活怪物／召唤物和 NPC 使用原值，敌对替身的原身份仍决定灯光。沉沦魔巫师为 Light=5、RGB 230/168/255，安达利尔为 Light=8、白光；没有按外观猜色。
- Overlay 读取 `InitRadius/Radius` 和 RGB。已显示的瞬时／单位／状态叠层仅在两种半径相等时接入常量光源；原半径增长／收缩的入口已静态定位但尚未移植。Client 189A0的玩家／怪物光源颜色更新入口已定位，装备／状态的完整输入及怪物装备基础光仍未接齐。

## 绘制

`LightingView` 是纯展示层。玩家在室内／室外均使用实际派生的 1–18 半径，移除户外至少 26 子格的扩大；室内遮光仍读 DT1 的 LOS／BlockLight 和 Objects.BlocksLight0–7，对象遮光开关独立于 HasCollision。已移除墙后额外保留 35% 玩家补光的经验规则；遮挡后只保留区域环境光及当前其他来源贡献。户外玩家光仍沿既有不投地形阴影的适配。角色跨格、切换区域、半径或对象遮挡版本改变、新局时重算掩码。当前地图范围为玩家周围 61×61 子格；全部来源按该网格计算，再按 D2Gfx 高质量子块入口对每个角点的四个相邻样本作整数平均（和右移 2 位），在等距逆投影后插值角点光。着色器将整数世界角点对齐纹理像素中心，修正半子格错位。该步骤缓和径向等值线及二值遮挡边界，仍是整帧展示适配，不等于原索引地砖的完整 Gouraud 光栅化。掩码、GPU 资源、昼夜状态与灯光实例不进入 D2S。

户外环境时钟以 D2MOO `ENVIRONMENT_AllocDrlgEnvironment` 的 cycle=2、ticks=0、timeRate=128、初始强度 128／白色开始。`GAME_UpdateProgress → GAME_UpdateEnvironment` 证实每个 25 Hz 玩法帧推进；正常六段色表、夜间加速、整数切段、RGB 舍入与 `float(sin(angle))` 强度计算移入展示层，表现暂停时不推进，普通菜单继续，当前室内不应用昼夜结果但时钟继续走。正常强度公式为 `int(float(sin(angle)) × 128 + 128 + 0.5)`，夜半区间 sine 减半；第五幕 170 上限、第四幕逐级趋近与巅峰 200／特殊色的原引擎分支保留。当前五幕地形可用，但完整原服环境时钟及天气／闪电影响未接入，日蚀开关的接入不认证整个任务／环境时序。

`PaletteBlendView` 在运行时读取当前幕 `pal.pl2` 的 `Shadows[32][256]`，偏移 `0x400`；标量／等强度灰光由 0–255 整数强度右移 3 位选取原行，再查原像素颜色索引，不再用 RGB 乘法模拟这部分明暗。彩光样本明确保留既有 RGB 乘色适配，暂不把标量表按各通道套用冒充硬件彩光。灰光中的暗部色阶和保亮色按原表结果保留；环境光不再与局部光做 `ambient + light × (1-ambient)` 的加亮。本次直接读取当前 MPQ 的原字节，确认第 0 行为暗部、第 31 行是 256 个索引的恒等映射，且 PL2 页首 RGB 与 Act1 pal.dat 全部相同；没有倒序或调色板错配。PL2 光照与既有 pScreen 混色共用原调色板及 RGB 反查缓存；GPU 复制当前世界画面，再查表覆盖世界视口。先切换着色器再登记纹理，lightMap 使用 texture0，其他四个纹理占用 raylib 的四个额外采样位。地面标题、交互文字、小地图与 HUD 在其后绘制，不受世界光照影响。

**当前照明与原版存在已确认的算法差异。** 当前实现边界如下，原1.13c定位见下一节：

- `unverifiedFalloff` 仍使用欧氏距离、半径22%内恒定及外圈smoothstep；原1.13c使用整数距离近似及定点线性衰减，当前尚未移植。
- 网格仍逐通道取较大值，环境RGB先按强度缩放；原客户端保留独立强度／RGB，并对强度饱和相加、按贡献混色。等强度灰光查原Shadows行，彩光继续RGB乘色适配；仅凭彩光样本不等于灰色就切换算法，也与原版由绘图后端及设置选择路径的方式不同。原客户端网格合成已定位，3D硬件最终彩光／Gamma仍未完整核实。
- 玩家室内光使用现有二值射线遮挡；物件光未移植原type2的遮挡缓存和中间衰减值。掩码只在玩家跨整数格等条件变化时重算，连续位置在同格内移动不会刷新遮挡。高质量子块四邻点平均已接，但原地板Gouraud定点缓存／逐像素索引光栅化、单位落点取光及Flicker仍未移植。飞弹当前直接使用目标半径；原客户端从1开始增长，Overlay的InitRadius／Radius目标推进及其他光源生灭也尚未接齐。
- 当前世界缓冲仍为 RGBA。未染色且调色板 RGB 唯一的像素可准确恢复原索引；同 RGB 不同索引使用首项，染色／半透明／混合后的像素取最近原色。一次世界后处理也不等于原版按地砖和单位先取光、再做透明混色的顺序。完整索引帧缓冲、用户 Gamma 设置及 3D 硬件路径仍暂缓。

原版依据：基础13另由下方原DLL确认，属性映射来自当前Properties／ItemStatCost；最大18的客户端限幅见RVA A8C20／A8B60。环境计算见 [D2MOO D2Environment.cpp](https://github.com/ThePhrozenKeep/D2MOO/blob/5596f5cb6c5251a0a07c6637d26458b06099d516/source/D2Common/src/D2Environment.cpp)，推进入口见同快照 `D2Game/src/GAME/Game.cpp::GAME_UpdateEnvironment`。原 PL2 排列见 OpenD2 `Engine/Palette.hpp` 与 OpenDiablo2 `d2pl2`；行选择及 Gouraud 缓存见 [D2Gfx CmnSubtile.cpp](https://github.com/ThePhrozenKeep/D2MOO/blob/5596f5cb6c5251a0a07c6637d26458b06099d516/source/D2Gfx/src/CmnSubtile.cpp) 的 `sub_6FA71070`、`sub_6FA73130`、`DGFX_InitGouraudCache_6FA72570`；高质量四邻点平均见 `D2GFX_FloorTileDraw_6FA73410` 内 v44/v45/v46/v47，子块 Gouraud 见 `TILE_TileDrawLit_6FA729D0`。物件模式及怪物 Light 字段见 D2MOO ObjectsTbls／MonsterTbls 和 OpenDiablo2 对应记录。DT1 的位 2／32 分别为 LOS／BlockLight，仍与单位半高斜向阴影独立。

## 原1.13c静态定位与当前差异

只读核对本机1.13c `D2Client.dll`（MD5 `f5860c629d309b8fc1f96174babcf633`，ImageBase `0x6fab0000`）与 `D2Common.dll`（MD5 `ee1238806ef6d6d9801d12a09d128fe1`，ImageBase `0x6fd50000`）。下表地址均为各自模块RVA；D2MOO固定快照缺少D2Client光源生成实现，不能只从D2Environment推断整条客户端绘图链。本节记录的是静态代码及当前MPQ读数，未运行原游戏或D2X作画面对照；原DLL、反汇编及提取资源留忽略目录，不提交／分发。

| 环节 | 原版证据与行为 | 当前代码差异及影响 |
| --- | --- | --- |
| 距离与衰减 | Client A8760／A87B0计算`d=(983×max(abs(dx),abs(dy))+407×min(abs(dx),abs(dy)))>>10`；内部坐标／半径以1/8子格计。A9020／A9079、A9728／A97DE、A992A／A997A以`k=(I<<16)/R`、`((R-d)×k)>>16`求贡献，再应用所属遮挡分支，非正值跳过 | `lighting_view.cpp::unverifiedFalloff`用欧氏距离及22%平台／smoothstep，中心亮区、过渡和等值线形状均不同 |
| 多光源与颜色 | Client A8A40–A8B58读取旧强度，先与新贡献相加并封顶255；A99E0初始化倒数表`floor(65536/i)`，彩光开启时对旧颜色×旧强度＋新颜色×新贡献乘倒数、右移16、逐通道封顶。网格记录独立的强度和RGB，关闭彩光仍保留标量强度 | 当前SceneLight／网格只有RGB，逐通道max，重叠光不会按原版积累；彩光覆盖灰光后又从PL2转为RGB乘色，材质色阶与亮部保留规则也随之改变 |
| 区域底光 | Common ordinal10636／3C6A0→44D90从LevelDef的88–8B读取Intensity／RGB，与D2MOO DRLGROOM_GetRGB_IntensityFromRoomEx一致。Client A9A20–A9B46先处理特殊区域／环境效果，普通房间RGB非零时使用表值；RGB全零才回退幕环境。A9C90按当前房间和邻房间范围分别填网格 | 当前用整个观察者区域的IsInside二选一，丢失邻房间底光和RGB零值回退。当前MPQ区域125–127为IsInside=0、Intensity=120、白色，当前会忽略120而用昼夜；第一幕洞穴8–16的表强度确为0，不应靠抬高固定底值修补算法差异 |
| 遮挡 | Client A8880逐格读取碰撞掩码0x22；物件创建入口24920以type2分配光源，A9B50→A9100建立其遮挡缓存，A8F60用缓存值`o`施加`(contribution×(8-(o>>1)))>>3`；A8310含相邻方向插值传播 | 当前只有玩家且IsInside时调用Grid::lightSegment，其他光源不遮挡；玩家二值射线也不等价于原缓存传播。墙后火光、门边和拐角过渡会不同 |
| 半径与生命周期 | Client 192DF–192F5创建白色、强度255、半径13的玩家光；24962／2497C用Lit右移1位，A8C20限幅到18。A0F60–A1001创建飞弹光时半径为1、目标为Missiles.Light；6B49F–6B4E5读取Overlay.InitRadius／Radius；A9BB0–A9BF4把当前内部半径按±8趋近目标 | 当前物件用Lit×0.5；当前MPQ物件39的Lit1=19、358的Lit0=19，原整数基值9而当前9.5。飞弹直接目标半径，动态Overlay被排除；其他玩家没有进入WorldDrawView光源列表，原版192CE–192F5会为其他玩家创建type1光 |
| 取光与绘图顺序 | Client 13715–1375E为地砖网格写入各点强度／RGB；9EB1A取光后由9EBA8／9EBBF传入透明／普通Tile绘制。D2MOO CmnSubtile保留原索引取色、子块Gouraud及透明分支 | 当前world_renderer先画地形、单位、弹体和屋顶，再统一逆投影屏幕像素取光；高墙／角色上部按屏幕位置误取另一地面点的光，透明叠层已先混色。四邻点平均不能消除此顺序差异 |

当前MPQ第二幕下水道47–49的Intensity=8，第五幕冰洞113–116／118–119为30，均为白色；与第一幕强度0的洞穴相比，当前max合成还会把这些非零底光当成局部光的下限，而原网格先填底光、再累加局部贡献。当前Act1 PL2的Shadows第31行仍是256索引恒等映射，没有发现倒序证据。

修正入口依次为独立强度／RGB及饱和合成、原距离／定点衰减、物件与玩家所属遮挡、源半径／生命周期和每房间底光，再处理按地砖／单位取光及索引混色顺序。完整Flicker、3D后端彩光、Gamma、天气和全场景原版画面对照仍待核实；本节不把已定位函数等同于已完成移植。

原版参考截图中的雨线尚未实现。OpenDiablo2 的 `LevelDetailsRecord.EnableRain` 和 Diablerie 的 `LevelInfo.rain` 证实 `Levels.Rain` 是区域天气许可；两个参考客户端未找到可直接核对的完整降雨实现。降雨开始／停止、密度、风向和天气光照时序仍未核实，不能仅因区域允许下雨就永久绘制雨线。正常世界绘制已移除原先供顶部开发文字使用的黑色渐变，不以固定遮罩冒充天气。

参考代码：`reference/d2moo/source/D2Common/src/DataTbls/LevelsTbls.cpp`、`ItemsTbls.cpp`、`Items/ItemMods.cpp`、`Monsters/Monsters.cpp` 和 `D2Environment.cpp`；DT1 标志交叉核对 `reference/diablerie/Assets/Scripts/Diablerie/Engine/IO/D2Formats/DT1.cs`。图形仍从用户挂载的原 MPQ 读取，不导出成独立资源文件。

完整光照一致性仍受上述距离衰减、彩光、遮挡、索引绘制与环境时钟缺口限制；原版参考和当前实现分别核对，不以窗口启动或局部显示代替原版对照。
