# 数据与生命周期

## 原始资源

- 当前来源：`assets/mpq2/{d2data,d2char,d2exp,d2sfx,Patch_D2}.mpq`。
- `Archives` 统一路径与包优先级；完整源与试玩不混用。运行不需要原版 EXE。
- `lod-named-txt-v1`、`classic-1.04-txt-v1` 是表结构适配标识，不代表逐补丁等价。
- 原始素材不受项目 GPL 许可覆盖，不提交 Git。

## 世界

`Levels/LvlPrest/LvlTypes/LvlMaze/LvlSub/LvlWarp → WorldCatalog → WorldPlan → MapRecipe → Map → Region`。

- `Levels` 决定关卡身份；户外邻接另需原引擎连接规则，不能只读 Vis。
- DS1 保存布局、原生单位和标记；DT1 保存像素与子格碰撞。没有地图 JSON。
- `MapRecipe.pieces` 表达原模板及平移，拼接时共享 DS1 的额外边行，不拉伸图像。
- `outdoor_layout` 管区域坐标；`outdoor` 管矩形边界和原预设；`maze` 管 Cave/Crypt 房间图。
- `exits` 解析隐藏与可见出口，按 `Vis/Warp` 关联；`session_exits` 管走近、跨区和返回。
- 地图种子默认 210；`--map-seed` 与刷怪种子、掉落种子独立。难度影响迷宫房间数。
- 地形目前启动时整体装载。房间激活只管理怪物，不是完整地形流式加载。

## 怪物

`原表与房间 → PopulationPlan → AreaState.pendingSpawns → 附近房间成组实例化 → Enemy`。

- 先计算计划；待生成单位没有实体 ID、AI 或小地图标记。
- `RoomLayout` 以所在房间及相接房间判断附近范围；不是屏幕像素距离。
- 原生群组成组创建一次。远处已创建单位保留状态、停止 AI 和动画计时。
- 切区保存整个 `AreaState`；回来不重刷。`R` 才主动重置当前区。
- 真实怪物／首领身份与运行替身类型分离；掉落请求使用真实身份。
- 当前激活是原版房间状态模型的适配，尚非完整 DRLG 客户端／服务器四级调度。

## 物品与存档

- `ItemDefinition` 是只读原表定义；`ItemInstance` 是带稳定 ID 的实例。
- 位置由地面区域或容器格子唯一确定；转移通过 `InventoryService`，禁止 UI 直接改归属。
- 私人箱访问权是临时交互状态，不能通过存档恢复远程访问。
- `.d2xsave` v3 保存地图种子、难度、原怪物身份、待生成计划、已创建单位、物品、ID 和随机状态。
- 读取先完整校验，再提交状态。地图配置、内容／规则指纹不匹配则拒绝；旧 v1/v2 不迁移。
- `--load` 启动时使用存档地图种子与难度重建地图；自选 `--preset/--variant` 仍需保持同一配置。
- 临时输入、拖拽、出口／拾取／交互请求不保存；对应的自动寻路在快照中清除。
