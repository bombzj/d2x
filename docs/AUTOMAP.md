# 自动地图

原先的角落地图从碰撞格采样单个边缘像素，显示范围、线条与标记均不是原版资源。本轮改为在运行时从原 MPQ 读取 `LvlTypes.txt`、`Automap.txt`、`Objects.txt`，按区域类型和 DT1 瓦片的方向、style、sequence 选择原自动地图 cel；大图和角落图分别绘制 `maximap.dc6` 与 `maximaps.dc6`。普通怪物不再作为红点暴露在地图上，原表具有 `AutoMap` cel 的物件才绘制标记。

`src/content/automap_data.*` 负责类型化原表规则；`src/presentation/automap_assets.cpp` 为每张地图准备图块且只上传实际使用的帧；`src/presentation/automap_view.cpp` 管理已探索房间与透明叠加绘制。探索状态只在当前游戏视图中维持，加载角色后重新从所在区域开始；不改变玩法或存档格式。`Tab` 仍切换大图，角落图在未打开遮挡面板时显示。

参考代码：`reference/d2moo/source/D2Common/src/DataTbls/LevelsTbls.cpp` 的 `DATATBLS_LoadAutomapTxt`／`DATATBLS_GetAutomapCellId`、`reference/d2moo/source/D2CMP/include/D2CMP.h` 的瓦片方向，以及原 MPQ 表和图形。原引擎为 cel 变体使用共享随机流；当前界面用瓦片坐标稳定选择，避免影响玩法随机数。Windows Release 构建及洞穴、野外、地下墓穴短帧截图已通过；洞穴角落图较密，像素位置、叠加透明度、角落显示方式与存档探索语义仍需进一步键鼠对照。
