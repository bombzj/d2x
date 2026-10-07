# 经典HUD与游戏界面

当前产品只保留SceneView／SceneController／ViewState一套界面，RemoteUiClients提供原服只读值并提交语义命令。世界、自动地图、原图缓存、动画与声音也使用公共入口；没有Local适配或联机专用HUD。

## 原资源与布局

800ctrlpnl7.dc6提供800像素底栏；原球体／耐力／经验、技能槽、腰带、人物／技能树、背包／箱子／方块、NPC／商店、任务／佣兵、传送点和ESC面板读取当前MPQ原图／字体／TBL。侧栏裁剪、相机和命中共用worldViewport，数据缺失不自绘替代资源。

底栏只加载原联机minipanel，八个按钮绘制／命中统一；组队／聊天仍不可用。面板存在不代表对应服务完成；佣兵、赌博及任务特殊服务保持明确限制。

## 手势与输入

首次世界按下确定移动／交互／拾取／左或右技能；单位技能按GUID锁定，按住不换到另一个目标。空地行走保持世界目标，拖动才更新，到达后可续走；松键／失焦／目标失效／UI接管结束未发意图。0x12仅原地狱火停止语义，已发普通动作可由原服完成。

UI消费的按下归UI直到释放，关闭面板或改变视口不会把同次按住转成世界动作。普通面板热区与世界分流，面板、ESC及失焦不暂停原服／网络；显式测试pause另见[调试入口](../../development/DEBUG_PIPE.md)。

人物、技能与底栏读同一纯投影，缺值显示 `?`。资源固定点按ItemStatCost转换；初始零整数生命且原服存活时HUD1血投影不修改副本，真实死亡等原动作／模式。

## 技能菜单

Skills.skilldesc关联SkillDesc的IconCel／ListRow／ListPool及当前职业图集；48×48原图按底栏比例缩放，跳负行，压缩非空行，左右菜单各向对应侧排列。通用动作在底行，同图卷轴／书候选按ListPool合并；原表及图标身份不代表全部效果完成。

悬停菜单后F1–F8绑定，单按选择不立即施法；原0x51无即时ACK，0x7B重入恢复。菜单的选择资格与底栏施放资格分开，城镇禁施不把菜单所有主动技能染红；完整充能／物品来源和精确禁用调色板仍未认证。

## 传送点与菜单

原800borderframe、waygatebackground／icons、expwaygatetabs及Levels.Waypoint提供边框、九槽、标题／页签／原颜色。只有原0x63确认菜单及历史解锁位可旅行；请求未确认、关闭／迟到回复分别处理，不自行解锁。最新启动期间点击／ON动画修正未入包，见[基线](../../../BASELINE.md)。

ESC先关闭面板／对白，无面板时开原退出菜单；Options只接已有Automap Options，其他设置页未实现。Save and Exit提交原保存退局，确认后返服务器选角，超时保存未知；不会本地写D2S或暂停世界。

## 证据与限制

原图布局核对本地OpenDiablo2 HUD／skill_select_panel／quest_log／escape_menu、Diablerie EnemyBar／PlayerController／MouseSelection及用户原版观察，原请求依据D2MOO PlrMsg。精确采样／Shift组合、全部面板鼠标服务、全部原像素／透明／染色与多人未认证；当前操作表见[README](../../../README.md)，有限原服证据见[联网记录](../../modules/NETWORK.md)。
