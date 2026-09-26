# 自动地图

原先的角落地图从碰撞格采样单个边缘像素，显示范围、线条与标记均不是原版资源。本轮改为在运行时从原 MPQ 读取 `LvlTypes.txt`、`Automap.txt`、`Objects.txt`，按区域类型和 DS1 格子的方向、style、sequence 选择原自动地图 cel，不再要求该格先匹配到 DT1 图形瓦片；大图和角落图分别绘制 `maximap.dc6` 与 `maximaps.dc6`。边缘可见性按解码后 cel 的实际纹理范围裁剪，不再使用固定的 32 像素容差。普通怪物不再作为红点暴露在地图上，原表具有 `AutoMap` cel 的物件才绘制标记。

`src/content/automap_data.*` 负责类型化原表规则；`src/presentation/automap_assets.cpp` 为每张地图准备图块且只上传实际使用的帧；`src/presentation/automap_view.cpp` 管理已探索房间与透明叠加绘制。大小图按原 DC6 帧的地面基点定位，修正格间距并对齐整数像素；方向 3 墙角补方向 4，同格相同 cel 去重。探索复用 `RoomLayout`，包括固定场景的房间回退及共享边缘。沿无缝边界连接的已探索区域按 `worldX/worldY` 绘制，出城不再丢弃营地图块；洞口、楼梯和传送门不连接地图层。探索只在当前游戏视图中维持，读档重置，不改变存档格式。

默认关闭地图，`Tab` 开关、`Shift+Tab` 切换大小图、`V` 交换小图左右、方向键平移已打开的地图、`Home` 居中、`F12` 开关名称；截图移至 `Ctrl+F12`。侧栏打开后限制在剩余场景范围。完整地图设置菜单尚未实现，`Shift+Tab` 是本项目临时尺寸入口，不声称为原版默认键。调试 `ui-input` 支持 `automap-size/side/center/names` 及方向键；`status.automap` 返回模式、偏移和参与绘制的已探索区域。

NPC 与 Stash 名称受名称开关控制；NPC 通过 `MonStats.MonStatsEx` 查询 `MonStats2.automapCel`，不再用 MonStats 编号查询 Objects。当前 MPQ 城镇 NPC 没有非零 automapCel，Objects 的 Portal 59／60 的 AutoMap 也为 0。D2MOO `D2C_AutomapCells` 已确认红十字 221／蓝十字 317，但没有身份到帧号的客户端映射。人物暂留旧自画十字、城镇 NPC 专属符号及动态传送门标记仍未复刻；旧实现不是验收标准，后续须以原版证据替换。

依据包括 D2MOO `LevelsTbls.cpp` 的 Automap 表匹配及 `D2Constants.h`，OpenDiablo2 的 `monster_stats2_record.go`、`game_event.go` 和 `key_map.go`。补充核对 D2BS `f4b99bbe8de6916384991dfdd198ecf234cef1c0` 的 `D2Helpers.cpp::ScreenToAutomap` 等轴坐标公式；未移植其代码。原引擎 cel 变体使用共享随机流，当前按瓦片坐标稳定选择；精确基点、叠加透明度与标记仍待原版对照。

2026-09-26：Windows 游戏构建及修改文件编译通过，已启动原 MPQ 暂停实例并截图，确认营地步行进入血腥荒地、地图平移不移动角色。跨区域层诊断最后修改只做编译；洞穴隔离、返回营地及最终独立打包尚未验收。按用户要求停止后续核对并提交源码，不宣称完整原版一致或最终验收完成。
