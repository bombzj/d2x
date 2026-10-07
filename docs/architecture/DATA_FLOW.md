# 数据流与生命周期

当前产品只运行联机。模块所有权见[架构](OVERVIEW.md)，字段／协议见[联网模块](../modules/NETWORK.md)。

| 数据 | 当前流向 | 生命周期与权威边界 |
| --- | --- | --- |
| 内容 | Archives → resources 解码 → content 只读定义 | 当前 MPQ 是图形、表和文字来源；不加载参考仓库附带数据 |
| 会话 | 局前 UI／command → RealmSession worker → SID／MCP／D2GS | worker 串行推进网络；tick 发布客户端稳定快照，连接／游戏代次隔离旧事件 |
| 地图 | 原种子／幕／有序房间事件 → RemoteTown → NativeMapGenerator → 活动 Map／Grid | 0x07／08维护视野引用；位置选择当前区域。历史缺口或锚点失败停止显示／移动，要求重入 |
| 单位 | 原回包 → RemoteWorld → RemoteScene 帧投影 | type＋GUID 与代次确定身份；模式、坐标、生命属于副本，显示插值独立 |
| 输入 | FrameInput → SceneController → I*Client → RemoteUiClients／RemoteControl／Combat／Inventory → 原请求 | 按住／释放／失焦在控制器统一；提交成功与原服执行成功分开 |
| 技能／成长 | 原属性／技能／状态 + MPQ → projectCharacterDisplay → 面板／HUD；意图反向提交 | 纯显示公式不执行技能、扣蓝、升级或奖励；不足输入保留未知 |
| 库存 | 原物品位流／增量 → RemoteInventory → InventoryView → 公共面板 | 原 GUID／revision 和交互上下文逐步确认；不以本地预览提交事务 |
| 任务／NPC | 本人任务字／日志状态／原对白 → quest_projection／RemoteUiClients → 公共界面 | 公共任务字独立；不从地图种子或旧任务执行器推断本人资格 |
| 声音／动画 | 原服事件／本人显示衔接 → 公共表现事件 → ActorAnimation／SceneAudio | MPQ 声音选择与播放规则统一；显示时钟和 GPU／声音实例不进入副本 |
| 探索 | 活动地形／可见 DT1 → AutomapExploration → AutomapDrawView | 城镇全揭示，野外记忆实际可见格；同局换区保留，新局清空；产品不读写 .d2xmap |
| 保存 | 原退局请求 → D2GS／D2DBS → 退局结果 | 客户端不读写服务器 D2S；无确认时保存未知，不本地补写 |
| 独立工具 | MPQ + D2S → persistence 解码 → d2x_assets save-info | 仅诊断值，无角色恢复入口；格式与拒绝边界见[存档](../modules/SAVES.md) |
| 客户端偏好 | UI → app/client_preferences → client-settings.json | 原子文件替换；与角色 D2S、服务器保存和探索记忆分离 |

跨帧缓存不保存当前副本容器的可失效指针；同步绘制输入只在当帧借用。死亡、换区、退局及交互变化清理相应未发送意图；取消本地队列不能撤销已执行的原服结果。普通 UI 不暂停网络／原服，显式测试暂停仅冻结表现。
