# 场景照明

## 数据入口

- `WorldCatalog` 在运行时从当前 MPQ 的 `Levels.txt` 读取 `IsInside`、`LOSDraw`、`Intensity` 和 RGB。室内使用原表的环境光值，不再用 LOSDraw 选择 19%／27% 底值；户外使用环境昼夜状态，不再固定 53%。`NoPer` 是透视控制，与环境光无关。
- 玩家基础光照半径为 13；`Properties.txt` 的 `light → item_lightradius` 经有效装备和已解析的物品词缀汇总，最终限制为 1–18。物品基础表的 `lightradius` 仍保留原字段，但不作为玩家属性加值；先前把其最大值叠到玩家半径属于错误。装备需求和耐久规则与其他属性共用。
- `SceneAssets.objectLights` 读取 `Objects.txt.Lit0–7`、RGB 和 Flicker；OpenDiablo2 将 Lit 定义为直径，当前换算半径为直径的一半。光源按对象原身份、NU／OP／ON／S1–5 模式选择；传送点使用真实激活阶段，回城门和凯恩红门使用与原图相同的开门／常开阶段。已移除按 rb/to Token 分类的统一火焰灯、16 盏限制及自造橙色渐变光圈。Flicker 元数据保留，未核实的闪烁程序暂缓，当前仅使用表中不加调制的基值。
- 全部已接入弹体、命中特效与客户端碎片共用 `Missiles.Light`、RGB、InitSteps 和实际生命周期；不再限定冰封球。球体为 6、冰弹为 4、RGB 81/81/255。区域坐标使用共享偏移，只收集已激活房间中的来源；非零 Flicker 同样暂用基值，不自造随机闪动。
- `MonsterCatalog` 按真实 MonStats 身份关联 `MonStatsEx → MonStats2.Light/light-r/light-g/light-b`；存活怪物／召唤物和 NPC 使用原值，敌对替身的原身份仍决定灯光。沉沦魔巫师为 Light=5、RGB 230/168/255，安达利尔为 Light=8、白光；没有按外观猜色。
- Overlay 读取 `InitRadius/Radius` 和 RGB。已显示的瞬时／单位／状态叠层仅在两种半径相等时接入常量光源；半径增长／收缩的客户端程序未核实，明确暂缓，不能用自造插值补全。状态本身改变灯光色、怪物装备基础光与玩家光源颜色规则也仍待核实。

## 绘制

`LightingView` 是纯展示层。玩家在室内／室外均使用实际派生的 1–18 半径，移除户外至少 26 子格的扩大；室内遮光仍读 DT1 的 LOS／BlockLight 和 Objects.BlocksLight0–7，对象遮光开关独立于 HasCollision。已移除墙后额外保留 35% 玩家补光的经验规则；遮挡后只保留区域环境光及当前其他来源贡献。户外玩家光仍沿既有不投地形阴影的适配。角色跨格、切换区域、半径或对象遮挡版本改变、会话恢复时重算掩码。当前地图范围为玩家周围 61×61 子格；全部来源按该网格计算，再按 D2Gfx 高质量子块入口对每个角点的四个相邻样本作整数平均（和右移 2 位），在等距逆投影后插值角点光。着色器将整数世界角点对齐纹理像素中心，修正半子格错位。该步骤缓和径向等值线及二值遮挡边界，仍是整帧展示适配，不等于原索引地砖的完整 Gouraud 光栅化。掩码、GPU 资源、昼夜状态与灯光实例不进入 D2S。

户外环境时钟以 D2MOO `ENVIRONMENT_AllocDrlgEnvironment` 的 cycle=2、ticks=0、timeRate=128、初始强度 128／白色开始。`GAME_UpdateProgress → GAME_UpdateEnvironment` 证实每个 25 Hz 玩法帧推进；正常六段色表、夜间加速、整数切段、RGB 舍入与 `float(sin(angle))` 强度计算移入展示层，暂停不推进，室内不应用昼夜结果但时钟继续走。正常强度公式为 `int(float(sin(angle)) × 128 + 128 + 0.5)`，夜半区间 sine 减半；第五幕 170 上限、第四幕逐级趋近与巅峰 200／特殊色的原引擎分支保留。当前可玩世界仅第一幕；其他幕的完整环境生命周期、第二幕日蚀任务及天气／闪电影响未接入，不以这些分支宣称其他幕完成。

`PaletteBlendView` 在运行时读取当前 Act1 `pal.pl2` 的 `Shadows[32][256]`，偏移 `0x400`；标量／等强度灰光由 0–255 整数强度右移 3 位选取原行，再查原像素颜色索引，不再用 RGB 乘法模拟这部分明暗。彩光样本明确保留既有 RGB 乘色适配，暂不把标量表按各通道套用冒充硬件彩光。灰光中的暗部色阶和保亮色按原表结果保留；环境光不再与局部光做 `ambient + light × (1-ambient)` 的加亮。本次直接读取当前 MPQ 的原字节，确认第 0 行为暗部、第 31 行是 256 个索引的恒等映射，且 PL2 页首 RGB 与 Act1 pal.dat 全部相同；没有倒序或调色板错配。PL2 光照与既有 pScreen 混色共用原调色板及 RGB 反查缓存；GPU 复制当前世界画面，再查表覆盖世界视口。先切换着色器再登记纹理，lightMap 使用 texture0，其他四个纹理占用 raylib 的四个额外采样位。地面标题、交互文字、小地图与 HUD 在其后绘制，不受世界光照影响。

**不能据此确认整个项目的光照衰减已经与原版一致。** 仍未核实的入口如下：

- `unverifiedFalloff` 明确保留旧的欧氏距离曲线：半径 22% 内恒定，随后 smoothstep 到边缘；这不是已证实的 D2Client 距离表。本地 D2MOO 没有 D2Client 光源生成实现，另三个参考客户端也没有完整距离衰减程序。未用线性／平方反比等新公式冒充原版。
- 网格仍逐通道取较大值；环境 RGB 先按强度缩放。等强度灰光查原 Shadows 行；彩光继续原 RGB 乘色适配，未另造通道查表／归一化规则。D2Gfx 软件入口证明的是标量 `intensity >> 3`，不能证明经典硬件彩光合成方式；等强度白光的颜色变换已按原表，彩光整体等价仍暂缓。
- 玩家室内光使用现有射线遮挡；其他光源暂没有原客户端的遮挡／穿墙规则。已接入高质量子块的四邻点平均及角点对齐；原地板 Gouraud 定点缓存／逐像素索引光栅化、单位落点取光、灯光生灭／淡出、Flicker 和动态 Overlay 半径仍未移植。
- 当前世界缓冲仍为 RGBA。未染色且调色板 RGB 唯一的像素可准确恢复原索引；同 RGB 不同索引使用首项，染色／半透明／混合后的像素取最近原色。一次世界后处理也不等于原版按地砖和单位先取光、再做透明混色的顺序。完整索引帧缓冲、用户 Gamma 设置及 3D 硬件路径仍暂缓。

户外整屏亮度仍遵循原昼夜状态：初始强度 128，正常白天可到 255（原 PL2 第 31 行保持原色），原表的营地与血腥荒地均为 IsInside=0，不能凭区域名另设较暗百分比。当前源码未发现可证实的户外全亮回归；报告是否发生在新局刚出城、当时昼夜位置及原版同条件画面对照尚缺信息，因此不能宣称此项已经修复。当前 MPQ 和本地参考仍没有完整 D2Client 距离表／天气光照程序，旧欧氏距离曲线也未核实；四邻点平均不解决这项证据缺口。

本次角点光／交互修订已随当前完整源码通过 Windows Release 构建并更新 `dist/current`，同包包含界面关闭手势隔离；协作 agent 没有编写或运行测试，未启动游戏；用户已确认本次运行包没有问题。户外亮度与未核实距离衰减的边界仍按上述说明保留。球体混色与声音边界另见 [冰封球](../skills/SORCERESS.md#冰封球依据与当前实现)。

原版依据：基础 13 与最大 18 见 [Blizzard 物品说明](https://classic.battle.net/diablo2exp/items/magic/suf.shtml)，属性映射来自当前 Properties／ItemStatCost。室内／户外规则交叉核对本地 OpenDiablo2 `level_details_record.go::IsInside`；环境计算见 [D2MOO D2Environment.cpp](https://github.com/ThePhrozenKeep/D2MOO/blob/5596f5cb6c5251a0a07c6637d26458b06099d516/source/D2Common/src/D2Environment.cpp)，推进入口见同快照 `D2Game/src/GAME/Game.cpp::GAME_UpdateEnvironment`。原 PL2 排列见 OpenD2 `Engine/Palette.hpp` 与 OpenDiablo2 `d2pl2`；行选择及 Gouraud 缓存见 [D2Gfx CmnSubtile.cpp](https://github.com/ThePhrozenKeep/D2MOO/blob/5596f5cb6c5251a0a07c6637d26458b06099d516/source/D2Gfx/src/CmnSubtile.cpp) 的 `sub_6FA71070`、`sub_6FA73130`、`DGFX_InitGouraudCache_6FA72570`；高质量四邻点平均见 `D2GFX_FloorTileDraw_6FA73410` 内 v44/v45/v46/v47，子块 Gouraud 见 `TILE_TileDrawLit_6FA729D0`。物件模式及怪物 Light 字段见 D2MOO ObjectsTbls／MonsterTbls 和 OpenDiablo2 对应记录。DT1 的位 2／32 分别为 LOS／BlockLight，仍与单位半高斜向阴影独立。

原版参考截图中的雨线尚未实现。OpenDiablo2 的 `LevelDetailsRecord.EnableRain` 和 Diablerie 的 `LevelInfo.rain` 证实 `Levels.Rain` 是区域天气许可；两个参考客户端未找到可直接核对的完整降雨实现。降雨开始／停止、密度、风向和天气光照时序仍未核实，不能仅因区域允许下雨就永久绘制雨线。正常世界绘制已移除原先供顶部开发文字使用的黑色渐变，不以固定遮罩冒充天气。

参考代码：`reference/d2moo/source/D2Common/src/DataTbls/LevelsTbls.cpp`、`ItemsTbls.cpp`、`Items/ItemMods.cpp`、`Monsters/Monsters.cpp` 和 `D2Environment.cpp`；DT1 标志交叉核对 `reference/diablerie/Assets/Scripts/Diablerie/Engine/IO/D2Formats/DT1.cs`。图形仍从用户挂载的原 MPQ 读取，不导出成独立资源文件。
