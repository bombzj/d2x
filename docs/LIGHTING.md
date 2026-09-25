# 场景照明

## 数据入口

- `WorldCatalog` 在运行时从原 MPQ 的 `Levels.txt` 读取 `IsInside`、`LOSDraw` 和光色／强度字段。第一幕营地使用较大的本地照明范围及可见的昏暗环境光；其他户外仍按白天显示。室内依据 `IsInside` 与 `LOSDraw` 选择环境光底值。
- 玩家基础光照半径为 13；`Properties.txt` 的 `light → item_lightradius` 经有效装备和已解析的物品词缀汇总，最终限制为 1–18。物品基础表的 `lightradius` 仍保留原字段，但不作为玩家属性加值；先前把其最大值叠到玩家半径属于错误。装备需求和耐久规则与其他属性共用。
- `Objects.txt` 的原图仍由普通物件渲染；现有火焰物件在暗层中额外发光。

## 绘制

`LightingView` 是纯展示层：按 DT1 子格的 `Light` 位建立独立遮光网格，不再把不能行走的格子一概变成黑方块。墙面本身可受光，墙后光线被遮挡；洞穴远处保留昏暗环境光，不会降为纯黑。第一幕营地的可见光半径至少 26 个子格，远处环境亮度为 53%；该场景参数是当前画面适配，不改变角色的装备光照属性。视线掩码经屏幕像素到地图坐标的等距逆投影求距离衰减。暗层只覆盖世界图形，在地面物品标签、交互提示、小地图和 HUD 之前绘制。遮挡掩码、贴图和着色器都不进入存档。角色跨格、切换区域或光照半径变化时重算掩码。

绘制暗层时先切换 raylib 着色器，再绑定视线贴图；切换着色器会提交上一批图形并清除登记的采样纹理，顺序颠倒会使角色自身光照失效，而火焰仍有亮度。

本实现使用原表决定室内类型和装备词缀，使用原 DT1 的逐子格 `Light` 位遮挡静态地形。基础 13 与最大 18 参照 [Blizzard 原物品说明](https://classic.battle.net/diablo2exp/items/magic/suf.shtml)，`light` 的属性 ID 从当前 MPQ `Properties.txt`／`ItemStatCost.txt` 读取。光照曲线、火焰亮度、室外昼夜时序和原版调色板级别仍未获得可执行的客户端规则，因此不声称逐像素等价；物件的 `BlocksLight` 各模式与动态门状态尚未接入遮挡网格。角色、怪物和物件新增基于 COF 原像素轮廓的半高斜向阴影，依据 OpenDiablo2／Diablerie；这与室内暗层独立，尚待画面验收。

原版参考截图中的雨线尚未实现。OpenDiablo2 的 `LevelDetailsRecord.EnableRain` 和 Diablerie 的 `LevelInfo.rain` 证实 `Levels.Rain` 是区域天气许可；两个参考客户端未找到可直接核对的完整降雨实现。降雨开始／停止、密度、风向和天气光照时序仍未核实，不能仅因区域允许下雨就永久绘制雨线。正常世界绘制已移除原先供顶部开发文字使用的黑色渐变，不以固定遮罩冒充天气。

参考代码：`reference/d2moo/source/D2Common/src/DataTbls/LevelsTbls.cpp`、`ItemsTbls.cpp`、`Items/ItemMods.cpp`、`Monsters/Monsters.cpp` 和 `D2Environment.cpp`；DT1 标志交叉核对 `reference/diablerie/Assets/Scripts/Diablerie/Engine/IO/D2Formats/DT1.cs`。图形仍从用户挂载的原 MPQ 读取，不导出成独立资源文件。
