# 场景照明

## 数据入口

- `WorldCatalog` 在运行时从原 MPQ 的 `Levels.txt` 读取 `IsInside`、`LOSDraw` 和光色／强度字段。`IsInside` 决定室内暗层，`LOSDraw` 区分带视线遮挡的洞穴、墓穴和地牢。户外目前按白天显示。
- 物品基础表的 `lightradius` 与 `Properties.txt` 的 `light → item_lightradius` 通过装备派生进入角色光照半径。基础装备取当前有效装备中最大的原表半径；词缀值叠加，最终限制为 1–18，未装备时基准为 13。装备需求和耐久规则与其他属性共用。
- `Objects.txt` 的原图仍由普通物件渲染；现有火焰物件在暗层中额外发光。

## 绘制

`LightingView` 是纯展示层：按地图的子格碰撞网格求角色视线，缓存邻近格的遮挡掩码，再按屏幕像素到地图坐标的等距逆投影求距离衰减。暗层只覆盖世界图形，在地面物品标签、交互提示、小地图和 HUD 之前绘制。遮挡掩码、贴图和着色器都不进入存档。角色跨格、切换区域或光照半径变化时重算掩码。

本实现使用原表决定室内类型和装备范围，使用现有地图碰撞近似墙体遮光。光照曲线、火焰亮度、室外昼夜时序和原版调色板级别仍未获得可执行的客户端规则，因此不声称逐像素等价；物件的 `BlocksLight` 各模式与动态门状态尚未接入遮挡网格。

参考代码：`reference/d2moo/source/D2Common/src/DataTbls/LevelsTbls.cpp`、`ItemsTbls.cpp`、`Items/ItemMods.cpp`、`Monsters/Monsters.cpp` 和 `D2Environment.cpp`。图形仍从用户挂载的原 MPQ 读取，不导出成独立资源文件。
