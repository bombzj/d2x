# 场景照明

## 数据入口

- `WorldCatalog` 在运行时从原 MPQ 的 `Levels.txt` 读取 `IsInside`、`LOSDraw` 和光色／强度字段。户外（包括第一幕营地与离城野外）使用较大的本地照明范围及可见的昏暗环境光。室内依据 `IsInside` 与 `LOSDraw` 选择环境光底值。
- 玩家基础光照半径为 13；`Properties.txt` 的 `light → item_lightradius` 经有效装备和已解析的物品词缀汇总，最终限制为 1–18。物品基础表的 `lightradius` 仍保留原字段，但不作为玩家属性加值；先前把其最大值叠到玩家半径属于错误。装备需求和耐久规则与其他属性共用。
- `Objects.txt` 的原图仍由普通物件渲染；现有火焰物件在暗层中额外发光。

## 绘制

`LightingView` 是纯展示层：角色光照经屏幕像素到地图坐标的等距逆投影求圆形距离衰减。户外均显示局部照明，地形不会把玩家周围切成黑暗楔形；洞穴等室内则叠加不受遮挡的圆形底光与受 DT1 子格遮挡的较亮光照。遮挡掩码以相邻子格线性插值，后方保留底光，避免整格黑块。室内远处另保留昏暗环境光。户外的可见光半径至少 26 个子格，远处环境亮度为 53%；洞穴底光取角色圆形衰减亮度的 35%。这些亮度与范围是当前画面对照参数，不改变角色的装备光照属性。暗层只覆盖世界图形，在地面物品标签、交互提示、小地图和 HUD 之前绘制。角色跨格、切换区域或光照半径变化时重算遮挡掩码；掩码、贴图和着色器不进入存档。

绘制暗层时先切换 raylib 着色器，再绑定视线贴图；切换着色器会提交上一批图形并清除登记的采样纹理，顺序颠倒会使角色自身光照失效，而火焰仍有亮度。户外掩码只限距离，室内掩码还应用静态地形遮光位。

本实现使用原表决定室内类型和装备词缀。基础 13 与最大 18 参照 [Blizzard 原物品说明](https://classic.battle.net/diablo2exp/items/magic/suf.shtml)，`light` 的属性 ID 从当前 MPQ `Properties.txt`／`ItemStatCost.txt` 读取。OpenDiablo2 将 DT1 子格位 `2` 标作 `BlockLOS`、位 `32` 标作 `BlockLight`；Diablerie 也将后者与视线区分。先前室内只读位 `2`，现两位都读。[D2MOO 的 D2Gfx 调试入口](https://github.com/ThePhrozenKeep/D2MOO/blob/5596f5cb6c5251a0a07c6637d26458b06099d516/source/D2Gfx/include/D2Gfx.h)同时接收 `pLight` 与 `pPlayerLight`；其[地板绘制函数保留的反编译注释](https://github.com/ThePhrozenKeep/D2MOO/blob/5596f5cb6c5251a0a07c6637d26458b06099d516/source/D2Gfx/src/CmnSubtile.cpp)描述高画质下相邻亮度采样插值。这些只支持分层和平滑采样方向，不能确定完整的 D2Client 合成公式。户外取消地形遮光和洞穴底光亮度还依据用户提供的原版对照图；光照曲线、火焰亮度、室外昼夜时序和原版调色板级别仍未获得可执行的客户端规则。物件的 `BlocksLight` 各模式也未接入。角色、怪物和物件的半高斜向阴影仍独立于环境光。

原版参考截图中的雨线尚未实现。OpenDiablo2 的 `LevelDetailsRecord.EnableRain` 和 Diablerie 的 `LevelInfo.rain` 证实 `Levels.Rain` 是区域天气许可；两个参考客户端未找到可直接核对的完整降雨实现。降雨开始／停止、密度、风向和天气光照时序仍未核实，不能仅因区域允许下雨就永久绘制雨线。正常世界绘制已移除原先供顶部开发文字使用的黑色渐变，不以固定遮罩冒充天气。

参考代码：`reference/d2moo/source/D2Common/src/DataTbls/LevelsTbls.cpp`、`ItemsTbls.cpp`、`Items/ItemMods.cpp`、`Monsters/Monsters.cpp` 和 `D2Environment.cpp`；DT1 标志交叉核对 `reference/diablerie/Assets/Scripts/Diablerie/Engine/IO/D2Formats/DT1.cs`。图形仍从用户挂载的原 MPQ 读取，不导出成独立资源文件。
