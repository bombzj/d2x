# 自动地图

原先的角落地图从碰撞格采样单个边缘像素，显示范围、线条与标记均不是原版资源。本轮改为在运行时从原 MPQ 读取 `LvlTypes.txt`、`Automap.txt`、`Objects.txt`，按区域类型和 DT1 瓦片的方向、style、sequence 选择原自动地图 cel；大图和角落图分别绘制 `maximap.dc6` 与 `maximaps.dc6`。普通怪物不再作为红点暴露在地图上，原表具有 `AutoMap` cel 的物件才绘制标记。

`src/content/automap_data.*` 负责类型化原表规则；`src/presentation/automap_assets.cpp` 为每张地图准备图块且只上传实际使用的帧；`src/presentation/automap_view.cpp` 管理已探索房间与透明叠加绘制。探索状态只在当前游戏视图中维持，加载角色后重新开始；不改变玩法或存档格式。默认关闭地图，`Tab` 开关左上角小地图，`V` 交换左右；打开侧栏后限制在剩余场景范围。大图资源和绘制函数保留，完整地图尺寸设置菜单尚未实现。

NPC 与 Stash 显示场景原名称；NPC 的 MonStats 编号不再错误地查询 Objects.txt 图标。用户第六／第八张图中的特殊十字标记尚未找到原客户端映射：NPC 不填造物件图标，人物仍沿用旧位置十字，不能视为标记已完全一致。这些参考图已经足够说明差异，不需要重复补图。

参考代码：`reference/d2moo/source/D2Common/src/DataTbls/LevelsTbls.cpp` 的 `DATATBLS_LoadAutomapTxt`／`DATATBLS_GetAutomapCellId`、D2CMP 瓦片方向及当前 MPQ。原引擎 cel 变体使用共享随机流，当前按瓦片坐标稳定选择。旧阶段有 Windows 构建及洞穴、野外、地下墓穴短帧记录；本次角落模式、名称和侧栏适配已构建，角落地图尚未专项实机验收，像素位置、叠加透明度仍待人工查看。
